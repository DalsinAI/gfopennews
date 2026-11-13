/* Overview cache on disk. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennews/overview.h"

struct NNOvCache {
    FILE    *f;
    uint32_t high, count;
};

size_t nn_ov_path(const char *dir, const char *group, char *out, size_t cap)
{
    size_t dl = strlen(dir), gl = strlen(group), need = dl + 1 + gl + 3, i, w = 0;
    int sep = dl && dir[dl - 1] != '/' && dir[dl - 1] != ':';
    if (!sep)
        need--;
    if (need + 1 > cap)
        return need;
    memcpy(out, dir, dl);
    w = dl;
    if (sep)
        out[w++] = '/';
    for (i = 0; i < gl; i++) {
        char c = group[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '_';
        out[w++] = ok && !(i == 0 && c == '.') ? c : '_';
    }
    memcpy(out + w, ".ov", 4);
    return need;
}

static uint32_t first_num(const char *s)
{
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9' && v <= 0xffffffffu)
        v = v * 10 + (uint64_t)(*s++ - '0');
    return (*s == '\t' && v <= 0xffffffffu) ? (uint32_t)v : 0;
}

NNOvCache *nn_ov_open(const char *path, int *err)
{
    NNOvCache *c = (NNOvCache *)calloc(1, sizeof(*c));
    FILE *f;
    char line[64];
    int at_start = 1;
    if (!c) {
        if (err) *err = NK_ENOMEM;
        return 0;
    }
    /* find the highest number held (lines can be long: read their starts) */
    if ((f = fopen(path, "rb")) != 0) {
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            if (at_start) {
                uint32_t v = first_num(line);
                if (v) {
                    c->count++;
                    if (v > c->high)
                        c->high = v;
                }
            }
            at_start = n && line[n - 1] == '\n';
        }
        fclose(f);
    }
    c->f = fopen(path, "ab");
    if (!c->f) {
        free(c);
        if (err) *err = NK_EIO;
        return 0;
    }
    if (err) *err = NK_OK;
    return c;
}

uint32_t nn_ov_high(const NNOvCache *c) { return c->high; }
uint32_t nn_ov_count(const NNOvCache *c) { return c->count; }

int nn_ov_add(NNOvCache *c, const char *line, size_t len)
{
    NNOver ov;
    char *buf;
    int ok;
    if (memchr(line, '\n', len) || memchr(line, '\r', len))
        return 0;
    buf = (char *)malloc(len + 1);
    if (!buf)
        return 0;
    ok = nn_parse_over(line, len, &ov, buf, len + 1);
    free(buf);
    if (!ok || ov.number <= c->high)
        return 0;
    if (fwrite(line, 1, len, c->f) != len || putc('\n', c->f) == EOF)
        return 0;
    c->high = ov.number;
    c->count++;
    return 1;
}

int nn_ov_flush(NNOvCache *c)
{
    return fflush(c->f) == 0 ? NK_OK : NK_EIO;
}

void nn_ov_close(NNOvCache *c)
{
    if (!c)
        return;
    fclose(c->f);
    free(c);
}

int nn_ov_read(const char *path, uint32_t lo, uint32_t hi, NNOvFn fn, void *user)
{
    FILE *f = fopen(path, "rb");
    char *line = 0, *buf = 0;
    size_t cap = 0;
    int r = NK_OK;
    if (!f)
        return NK_OK;
    for (;;) {
        size_t n = 0;
        int ch;
        while ((ch = getc(f)) != EOF && ch != '\n') {
            if (n + 1 >= cap) {
                size_t nc = cap ? cap * 2 : 512;
                char *nl = (char *)realloc(line, nc), *nb;
                if (!nl) { r = NK_ENOMEM; goto out; }
                line = nl;
                nb = (char *)realloc(buf, nc);
                if (!nb) { r = NK_ENOMEM; goto out; }
                buf = nb;
                cap = nc;
            }
            line[n++] = (char)ch;
        }
        if (ch == EOF && n == 0)
            break;
        if (n) {
            NNOver ov;
            if (nn_parse_over(line, n, &ov, buf, cap) && ov.number >= lo && ov.number <= hi) {
                if (fn(user, &ov) != 0)
                    break;
            }
        }
        if (ch == EOF)
            break;
    }
out:
    fclose(f);
    free(line);
    free(buf);
    return r;
}
