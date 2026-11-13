/* .newsrc reading, marking and atomic saving. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/newsrc.h"

void nn_rc_init(NNNewsrc *rc)
{
    memset(rc, 0, sizeof(*rc));
}

void nn_rc_free(NNNewsrc *rc)
{
    size_t i;
    for (i = 0; i < rc->n; i++) {
        free(rc->g[i].name);
        free(rc->g[i].r);
    }
    free(rc->g);
    nn_rc_init(rc);
}

NNGroupRc *nn_rc_group(NNNewsrc *rc, const char *name, int create)
{
    size_t i, l;
    NNGroupRc *g;
    for (i = 0; i < rc->n; i++)
        if (strcmp(rc->g[i].name, name) == 0)
            return &rc->g[i];
    if (!create)
        return 0;
    l = strlen(name);
    if (!l || strpbrk(name, ":!, \t\r\n"))
        return 0;
    if (rc->n == rc->cap) {
        size_t nc = rc->cap ? rc->cap * 2 : 16;
        NNGroupRc *ng = (NNGroupRc *)realloc(rc->g, nc * sizeof(*ng));
        if (!ng)
            return 0;
        rc->g = ng;
        rc->cap = nc;
    }
    g = &rc->g[rc->n];
    memset(g, 0, sizeof(*g));
    g->name = (char *)malloc(l + 1);
    if (!g->name)
        return 0;
    memcpy(g->name, name, l + 1);
    rc->n++;
    return g;
}

/* Sets lo..hi read or unread, keeping the ranges sorted and apart. */
int nn_rc_mark(NNGroupRc *g, uint32_t lo, uint32_t hi, int read)
{
    NNRange *out;
    size_t i, n = 0, cap;
    int placed = 0;
    if (lo > hi)
        return NK_EARG;
    cap = g->n + 2;
    out = (NNRange *)malloc(cap * sizeof(*out));
    if (!out)
        return NK_ENOMEM;
    for (i = 0; i < g->n; i++) {
        NNRange r = g->r[i];
        if (read) {
            /* join ranges that touch or overlap lo..hi */
            if ((uint64_t)r.hi + 1 < lo) {
                out[n++] = r;
            } else if (r.lo > (uint64_t)hi + 1) {
                if (!placed) {
                    out[n].lo = lo;
                    out[n++].hi = hi;
                    placed = 1;
                }
                out[n++] = r;
            } else {
                if (r.lo < lo) lo = r.lo;
                if (r.hi > hi) hi = r.hi;
            }
        } else {
            if (r.hi < lo || r.lo > hi) {
                out[n++] = r;
            } else {
                if (r.lo < lo) {
                    out[n].lo = r.lo;
                    out[n++].hi = lo - 1;
                }
                if (r.hi > hi) {
                    out[n].lo = hi + 1;
                    out[n++].hi = r.hi;
                }
            }
        }
    }
    if (read && !placed) {
        out[n].lo = lo;
        out[n++].hi = hi;
    }
    free(g->r);
    g->r = out;
    g->n = n;
    g->cap = cap;
    return NK_OK;
}

int nn_rc_is_read(const NNGroupRc *g, uint32_t n)
{
    size_t a = 0, b = g->n;
    while (a < b) {
        size_t m = (a + b) / 2;
        if (n < g->r[m].lo)
            b = m;
        else if (n > g->r[m].hi)
            a = m + 1;
        else
            return 1;
    }
    return 0;
}

uint32_t nn_rc_unread(const NNGroupRc *g, uint32_t low, uint32_t high)
{
    uint64_t total, read = 0;
    size_t i;
    if (!low)
        low = 1;
    if (high < low)
        return 0;
    total = (uint64_t)high - low + 1;
    for (i = 0 ; g && i < g->n; i++) {
        uint32_t a = g->r[i].lo > low ? g->r[i].lo : low;
        uint32_t b = g->r[i].hi < high ? g->r[i].hi : high;
        if (a <= b)
            read += (uint64_t)b - a + 1;
    }
    return (uint32_t)(total - read);
}

size_t nn_rc_format(const NNGroupRc *g, char *buf, size_t cap)
{
    size_t need = 0, i;
    char tmp[32];
#define PUT(s, l) do { size_t _l = (l); \
        if (cap && need < cap - 1) memcpy(buf + need, (s), _l < cap - 1 - need ? _l : cap - 1 - need); \
        need += _l; } while (0)
    PUT(g->name, strlen(g->name));
    PUT(g->subscribed ? ":" : "!", 1);
    for (i = 0; i < g->n; i++) {
        int k = g->r[i].lo == g->r[i].hi
              ? snprintf(tmp, sizeof(tmp), "%s%lu", i ? "," : " ", (unsigned long)g->r[i].lo)
              : snprintf(tmp, sizeof(tmp), "%s%lu-%lu", i ? "," : " ",
                         (unsigned long)g->r[i].lo, (unsigned long)g->r[i].hi);
        PUT(tmp, (size_t)k);
    }
#undef PUT
    if (cap)
        buf[need < cap ? need : cap - 1] = 0;
    return need;
}

static int parse_line(NNNewsrc *rc, const char *s, size_t n)
{
    size_t i = 0;
    NNGroupRc *g;
    char name[512];
    while (i < n && s[i] != ':' && s[i] != '!')
        i++;
    if (i == 0 || i >= n || i >= sizeof(name))
        return 0;
    memcpy(name, s, i);
    name[i] = 0;
    while (i > 0 && (name[i - 1] == ' ' || name[i - 1] == '\t'))
        name[--i] = 0;
    if (nn_rc_group(rc, name, 0))
        return 0;                 /* a group listed twice: keep the first */
    g = nn_rc_group(rc, name, 1);
    if (!g)
        return 0;
    i = strlen(name);
    while (s[i] != ':' && s[i] != '!')
        i++;
    g->subscribed = s[i] == ':';
    i++;
    while (i < n) {
        uint64_t lo = 0, hi;
        int digits = 0;
        while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == '\t'))
            i++;
        if (i >= n)
            break;
        while (i < n && s[i] >= '0' && s[i] <= '9' && lo <= 0xffffffffu) {
            lo = lo * 10 + (uint64_t)(s[i++] - '0');
            digits++;
        }
        if (!digits || lo > 0xffffffffu)
            return 1;             /* rest of line unreadable: keep what we have */
        hi = lo;
        if (i < n && s[i] == '-') {
            i++;
            hi = 0;
            digits = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9' && hi <= 0xffffffffu) {
                hi = hi * 10 + (uint64_t)(s[i++] - '0');
                digits++;
            }
            if (!digits || hi > 0xffffffffu || hi < lo)
                return 1;
        }
        if (nn_rc_mark(g, (uint32_t)lo, (uint32_t)hi, 1) != 0)
            return 1;
    }
    return 1;
}

int nn_rc_parse(NNNewsrc *rc, const char *text, size_t len)
{
    size_t i = 0;
    while (i < len) {
        const char *lf = (const char *)memchr(text + i, '\n', len - i);
        size_t n = lf ? (size_t)(lf - (text + i)) : len - i;
        size_t l = n;
        if (l && text[i + l - 1] == '\r')
            l--;
        if (l && text[i] != '#' && !parse_line(rc, text + i, l))
            rc->bad_lines++;
        i += n + 1;
    }
    return NK_OK;
}

int nn_rc_load(NNNewsrc *rc, const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long sz;
    int r;
    if (!f)
        return NK_OK;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NK_EIO;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NK_ENOMEM;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NK_EIO;
    }
    fclose(f);
    r = nn_rc_parse(rc, buf, (size_t)sz);
    free(buf);
    return r;
}

/* write to <path>.new, then swap in three steps (AmigaDOS Rename() never
 * replaces a file); a crash leaves either the old or the new file whole */
int nn_rc_save(const NNNewsrc *rc, const char *path)
{
    size_t pl = strlen(path), i;
    char *pn = (char *)malloc(pl + 5), *po = (char *)malloc(pl + 5), *line = 0;
    size_t lcap = 0;
    FILE *f;
    int r = NK_EIO;
    if (!pn || !po)
        goto out;
    memcpy(pn, path, pl);
    memcpy(pn + pl, ".new", 5);
    memcpy(po, path, pl);
    memcpy(po + pl, ".old", 5);
    f = fopen(pn, "wb");
    if (!f)
        goto out;
    for (i = 0; i < rc->n; i++) {
        size_t need = nn_rc_format(&rc->g[i], 0, 0) + 1;
        if (need > lcap) {
            char *nl = (char *)realloc(line, need);
            if (!nl) {
                fclose(f);
                remove(pn);
                r = NK_ENOMEM;
                goto out;
            }
            line = nl;
            lcap = need;
        }
        nn_rc_format(&rc->g[i], line, lcap);
        if (fputs(line, f) == EOF || putc('\n', f) == EOF) {
            fclose(f);
            remove(pn);
            goto out;
        }
    }
    if (fclose(f) != 0) {
        remove(pn);
        goto out;
    }
    remove(po);
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        if (rename(path, po) != 0)
            goto out;
    }
    if (rename(pn, path) != 0) {
        rename(po, path);
        goto out;
    }
    remove(po);
    r = NK_OK;
out:
    free(pn);
    free(po);
    free(line);
    return r;
}
