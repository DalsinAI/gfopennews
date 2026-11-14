/* GFOpenNews external-unpack hook: see unpack.h.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gfopennews/unpack.h"
#include "gfopennetkit/netkit.h"

typedef struct { char *p; size_t cap, len; } Buf;

static void put(Buf *b, char c)
{
    if (b->len + 1 < b->cap) b->p[b->len] = c;
    b->len++;
}

static int quote(Buf *b, const char *v, int shell)
{
    const unsigned char *s = (const unsigned char *)(v ? v : "");
    for (; *s; s++) {
        if (*s < 0x20 || *s == 0x7f) return NK_EARG;
        /* The AmigaDOS Shell acts on ` and $ even inside double quotes ($name
         * and ${name} are variables, `...` a command run for its output), and
         * its "*" escape is the Shell's to read, not ours to trust: a value
         * holding any of ` $ " * is refused, so nothing in it is ever escaped */
        if (shell == NN_SHELL_AMIGADOS && (*s == '`' || *s == '$' || *s == '"' || *s == '*')) return NK_EARG;
    }
    s = (const unsigned char *)(v ? v : "");
    if (shell == NN_SHELL_AMIGADOS) {
        put(b, '"');
        for (; *s; s++)
            put(b, (char)*s);
        put(b, '"');
    } else {
        put(b, '\'');
        for (; *s; s++) {
            if (*s == '\'') { put(b, '\''); put(b, '\\'); put(b, '\''); }
            put(b, (char)*s);
        }
        put(b, '\'');
    }
    return NK_OK;
}

int nn_unpack_command(const char *tmpl, const char *dir, const char *name, int shell,
                      char *out, size_t cap)
{
    Buf b;
    const char *t;
    if (!tmpl || !out || !cap) return NK_EARG;
    b.p = out;
    b.cap = cap;
    b.len = 0;
    for (t = tmpl; *t; t++) {
        if ((unsigned char)*t < 0x20) { out[0] = 0; return NK_EARG; }
        if (*t != '%') { put(&b, *t); continue; }
        t++;
        if (*t == '%') put(&b, '%');
        else if ((*t == 'd' && quote(&b, dir, shell) == NK_OK) ||
                 (*t == 'n' && quote(&b, name, shell) == NK_OK))
            ;
        else { out[0] = 0; return NK_EARG; }
    }
    if (b.len >= cap) { out[0] = 0; return NK_ETOOLONG; }
    out[b.len] = 0;
    return NK_OK;
}

void nn_unpack_name(const char *nzb_path, char *out, size_t cap)
{
    const char *s = nzb_path ? nzb_path : "", *p;
    size_t n;
    if (!cap) return;
    for (p = s; *p; p++)
        if (*p == '/' || *p == ':') s = p + 1;
    n = strlen(s);
    if (n >= 4 && s[n - 4] == '.' && (s[n - 3] | 32) == 'n' && (s[n - 2] | 32) == 'z' && (s[n - 1] | 32) == 'b')
        n -= 4;
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
}
