/* NZB reader. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/nzb.h"

int nn_msgid_ok(const char *id)
{
    const unsigned char *p = (const unsigned char *)id;
    if (!*p || strlen(id) > 250)
        return 0;
    for (; *p; p++)
        if (*p <= ' ' || *p == '<' || *p == '>' || *p == 0x7f)
            return 0;
    return strchr(id, '@') != 0;
}

/* ---- a small XML reader ------------------------------------------------------ */
typedef struct {
    const char *s, *e;
} X;

static char *dup_decoded(const char *s, size_t n)
{
    char *o = (char *)malloc(n + 1), *w = o;
    size_t i = 0;
    if (!o)
        return 0;
    while (i < n) {
        if (s[i] == '&') {
            const char *semi = (const char *)memchr(s + i, ';', n - i < 12 ? n - i : 12);
            if (semi) {
                size_t k = (size_t)(semi - (s + i));
                unsigned long cp = 0;
                int ok = 1;
                if (k == 4 && !memcmp(s + i, "&amp", 4)) cp = '&';
                else if (k == 3 && !memcmp(s + i, "&lt", 3)) cp = '<';
                else if (k == 3 && !memcmp(s + i, "&gt", 3)) cp = '>';
                else if (k == 5 && !memcmp(s + i, "&quot", 5)) cp = '"';
                else if (k == 5 && !memcmp(s + i, "&apos", 5)) cp = '\'';
                else if (k > 2 && s[i + 1] == '#') {
                    char *end;
                    char num[12];
                    memcpy(num, s + i + 2, k - 2);
                    num[k - 2] = 0;
                    cp = (num[0] == 'x' || num[0] == 'X') ? strtoul(num + 1, &end, 16)
                                                          : strtoul(num, &end, 10);
                    ok = *end == 0 && cp > 0 && cp <= 0x10ffff;
                } else
                    ok = 0;
                if (ok) {
                    /* UTF-8 */
                    if (cp < 0x80) *w++ = (char)cp;
                    else if (cp < 0x800) { *w++ = (char)(0xc0 | cp >> 6); *w++ = (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { *w++ = (char)(0xe0 | cp >> 12); *w++ = (char)(0x80 | ((cp >> 6) & 63)); *w++ = (char)(0x80 | (cp & 63)); }
                    else { *w++ = (char)(0xf0 | cp >> 18); *w++ = (char)(0x80 | ((cp >> 12) & 63)); *w++ = (char)(0x80 | ((cp >> 6) & 63)); *w++ = (char)(0x80 | (cp & 63)); }
                    i += k + 1;
                    continue;
                }
            }
        }
        *w++ = s[i++];
    }
    *w = 0;
    return o;
}

static char *trim_dup(const char *s, size_t n)
{
    while (n && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++, n--;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    return dup_decoded(s, n);
}

/* finds attr="value" (or 'value') inside a start tag */
static char *attr(const char *tag, size_t tl, const char *name)
{
    size_t nl = strlen(name), i;
    for (i = 0; i + nl + 2 < tl; i++) {
        if ((tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\n' || tag[i] == '\r') &&
            !memcmp(tag + i + 1, name, nl)) {
            size_t j = i + 1 + nl;
            char q;
            while (j < tl && tag[j] == ' ') j++;
            if (j >= tl || tag[j] != '=') continue;
            j++;
            while (j < tl && tag[j] == ' ') j++;
            if (j >= tl || (tag[j] != '"' && tag[j] != '\'')) continue;
            q = tag[j++];
            {
                const char *v = tag + j, *end = (const char *)memchr(v, q, tl - j);
                if (!end) return 0;
                return dup_decoded(v, (size_t)(end - v));
            }
        }
    }
    return 0;
}

typedef struct {
    int      kind;        /* 0 text, 1 start, 2 end, 3 empty element */
    const char *name;     /* element name */
    size_t   nl;
    const char *tag;      /* the whole start tag inside < > */
    size_t   tl;
    const char *text;
    size_t   textl;
} Tok;

/* answers 1 with a token, 0 at the end, -1 on broken XML */
static int next_tok(X *x, Tok *t)
{
    const char *s = x->s;
    if (s >= x->e)
        return 0;
    if (*s != '<') {
        const char *lt = (const char *)memchr(s, '<', (size_t)(x->e - s));
        if (!lt) lt = x->e;
        t->kind = 0;
        t->text = s;
        t->textl = (size_t)(lt - s);
        x->s = lt;
        return 1;
    }
    if (x->e - s >= 4 && !memcmp(s, "<!--", 4)) {
        const char *p;
        for (p = s + 4; p + 3 <= x->e; p++)
            if (!memcmp(p, "-->", 3)) {
                x->s = p + 3;
                return next_tok(x, t);
            }
        return -1;
    }
    if (x->e - s >= 9 && !memcmp(s, "<![CDATA[", 9)) {
        const char *p;
        for (p = s + 9; p + 3 <= x->e; p++)
            if (!memcmp(p, "]]>", 3)) {
                t->kind = 0;
                t->text = s + 9;
                t->textl = (size_t)(p - (s + 9));
                x->s = p + 3;
                return 1;
            }
        return -1;
    }
    {
        const char *gt, *n;
        char q = 0;
        /* the closing '>', not one inside a quoted attribute value */
        for (gt = s + 1; gt < x->e; gt++) {
            if (q) {
                if (*gt == q) q = 0;
            } else if (*gt == '"' || *gt == '\'') {
                q = *gt;
            } else if (*gt == '>')
                break;
        }
        if (gt >= x->e)
            return -1;
        x->s = gt + 1;
        if (s[1] == '?' || s[1] == '!')
            return next_tok(x, t);          /* <?xml ...?>, <!DOCTYPE ...> */
        t->tag = s + 1;
        t->tl = (size_t)(gt - s - 1);
        if (s[1] == '/') {
            t->kind = 2;
            n = s + 2;
        } else {
            t->kind = gt[-1] == '/' ? 3 : 1;
            n = s + 1;
        }
        t->name = n;
        while (n < gt && *n != ' ' && *n != '\t' && *n != '\r' && *n != '\n' && *n != '/')
            n++;
        t->nl = (size_t)(n - t->name);
        return 1;
    }
}

static int is(const Tok *t, const char *name)
{
    size_t l = strlen(name);
    return t->nl == l && !memcmp(t->name, name, l);
}

static int seg_cmp(const void *a, const void *b)
{
    const NNNzbSeg *x = (const NNNzbSeg *)a, *y = (const NNNzbSeg *)b;
    return x->number < y->number ? -1 : x->number > y->number;
}

void nn_nzb_free(NNNzb *nzb)
{
    size_t i, k;
    for (i = 0; i < nzb->nfiles; i++) {
        NNNzbFile *f = &nzb->files[i];
        free(f->subject);
        free(f->poster);
        for (k = 0; k < f->ngroups; k++) free(f->groups[k]);
        free(f->groups);
        for (k = 0; k < f->nsegs; k++) free(f->segs[k].msgid);
        free(f->segs);
    }
    free(nzb->files);
    free(nzb->password);
    free(nzb->title);
    memset(nzb, 0, sizeof(*nzb));
}

#define GROW(arr, n, cap, type) do { if ((n) == (cap)) { size_t _c = (cap) ? (cap) * 2 : 8; \
    type *_p = (type *)realloc((arr), _c * sizeof(type)); if (!_p) goto oom; (arr) = _p; (cap) = _c; } } while (0)

int nn_nzb_parse(NNNzb *nzb, const char *xml, size_t len)
{
    X x;
    Tok t;
    int r, seen_nzb = 0;
    size_t fcap = 0, gcap = 0, scap = 0;
    NNNzbFile *f = 0;
    char *meta_type = 0;
    /* where text goes */
    enum { T_NONE, T_META, T_GROUP, T_SEG } want = T_NONE;
    uint32_t seg_num = 0, seg_bytes = 0;
    const char *txt = 0;
    size_t txtl = 0;

    memset(nzb, 0, sizeof(*nzb));
    x.s = xml;
    x.e = xml + len;
    while ((r = next_tok(&x, &t)) == 1) {
        if (t.kind == 0) {
            if (want != T_NONE && !txt) {
                txt = t.text;
                txtl = t.textl;
            }
            continue;
        }
        if (t.kind == 1 || t.kind == 3) {
            if (is(&t, "nzb"))
                seen_nzb = 1;
            else if (is(&t, "meta") && t.kind == 1) {
                free(meta_type);
                meta_type = attr(t.tag, t.tl, "type");
                want = T_META;
                txt = 0;
            } else if (is(&t, "file")) {
                char *d;
                GROW(nzb->files, nzb->nfiles, fcap, NNNzbFile);
                f = &nzb->files[nzb->nfiles++];
                memset(f, 0, sizeof(*f));
                gcap = scap = 0;
                f->subject = attr(t.tag, t.tl, "subject");
                f->poster = attr(t.tag, t.tl, "poster");
                d = attr(t.tag, t.tl, "date");
                f->date = d ? (uint32_t)strtoul(d, 0, 10) : 0;
                free(d);
                if (!f->subject) {
                    f->subject = (char *)malloc(1);
                    if (!f->subject) goto oom;
                    f->subject[0] = 0;
                }
            } else if (is(&t, "group") && t.kind == 1 && f) {
                want = T_GROUP;
                txt = 0;
            } else if (is(&t, "segment") && t.kind == 1 && f) {
                char *b = attr(t.tag, t.tl, "bytes"), *n = attr(t.tag, t.tl, "number");
                seg_bytes = b ? (uint32_t)strtoul(b, 0, 10) : 0;
                seg_num = n ? (uint32_t)strtoul(n, 0, 10) : 0;
                free(b);
                free(n);
                want = T_SEG;
                txt = 0;
            }
            continue;
        }
        /* end tag */
        if (is(&t, "meta") && want == T_META) {
            char *v = txt ? trim_dup(txt, txtl) : 0;
            if (meta_type && !strcmp(meta_type, "password") && !nzb->password) nzb->password = v;
            else if (meta_type && !strcmp(meta_type, "title") && !nzb->title) nzb->title = v;
            else free(v);
            want = T_NONE;
        } else if (is(&t, "group") && want == T_GROUP && f) {
            char *v = txt ? trim_dup(txt, txtl) : 0;
            if (v && *v) {
                GROW(f->groups, f->ngroups, gcap, char *);
                f->groups[f->ngroups++] = v;
            } else
                free(v);
            want = T_NONE;
        } else if (is(&t, "segment") && want == T_SEG && f) {
            char *v = txt ? trim_dup(txt, txtl) : 0;
            /* some NZBs keep the < > */
            if (v && v[0] == '<') {
                size_t l = strlen(v);
                if (l > 1 && v[l - 1] == '>') {
                    memmove(v, v + 1, l - 2);
                    v[l - 2] = 0;
                }
            }
            if (v && seg_num && nn_msgid_ok(v)) {
                GROW(f->segs, f->nsegs, scap, NNNzbSeg);
                f->segs[f->nsegs].number = seg_num;
                f->segs[f->nsegs].bytes = seg_bytes;
                f->segs[f->nsegs].msgid = v;
                f->nsegs++;
            } else {
                free(v);
                nzb->bad_segments++;
            }
            want = T_NONE;
        } else if (is(&t, "file"))
            f = 0;
    }
    free(meta_type);
    meta_type = 0;
    if (r < 0 || !seen_nzb) {
        nn_nzb_free(nzb);
        return NK_EPROTO;
    }
    /* segments in order, duplicates out */
    {
        size_t i, k, w;
        for (i = 0; i < nzb->nfiles; i++) {
            NNNzbFile *ff = &nzb->files[i];
            qsort(ff->segs, ff->nsegs, sizeof(NNNzbSeg), seg_cmp);
            for (k = 0, w = 0; k < ff->nsegs; k++) {
                if (w && ff->segs[w - 1].number == ff->segs[k].number) {
                    free(ff->segs[k].msgid);
                    continue;
                }
                ff->segs[w++] = ff->segs[k];
            }
            ff->nsegs = w;
        }
    }
    return NK_OK;
oom:
    free(meta_type);
    nn_nzb_free(nzb);
    return NK_ENOMEM;
}

int nn_nzb_load(NNNzb *nzb, const char *path)
{
    FILE *fp = fopen(path, "rb");
    char *buf;
    long sz;
    int r;
    memset(nzb, 0, sizeof(*nzb));
    if (!fp)
        return NK_EIO;
    if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0 ||
        sz > 64L * 1024 * 1024) {
        fclose(fp);
        return NK_EIO;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NK_ENOMEM;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return NK_EIO;
    }
    fclose(fp);
    r = nn_nzb_parse(nzb, buf, (size_t)sz);
    free(buf);
    return r;
}
