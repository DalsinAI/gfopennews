/* NNTP client core. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include <string.h>
#include "gfopennews/nntp.h"

typedef struct Pending {
    struct Pending *next;
    int        multi;
    NNLineFn   line_fn;
    void      *line_user;
    NNDoneFn   done_fn;
    void      *user;
} Pending;

struct NNClient {
    NKConn  *conn;
    Pending *head, *tail;
    size_t   waiting;
    /* reply being read */
    int      in_block;     /* reading a block for head */
    int      stop_lines;   /* line_fn asked to stop */
    int      block_err;
    int      code;
    char    *text;         /* status text kept while the block is read */
    int      dead;
};

static int push(NNClient *c, int multi, NNLineFn lf, void *lu, NNDoneFn df, void *user)
{
    Pending *p = (Pending *)calloc(1, sizeof(*p));
    if (!p)
        return NK_ENOMEM;
    p->multi = multi;
    p->line_fn = lf;
    p->line_user = lu;
    p->done_fn = df;
    p->user = user;
    if (c->tail)
        c->tail->next = p;
    else
        c->head = p;
    c->tail = p;
    c->waiting++;
    return NK_OK;
}

static Pending *pop(NNClient *c)
{
    Pending *p = c->head;
    if (p) {
        c->head = p->next;
        if (!c->head)
            c->tail = 0;
        c->waiting--;
    }
    return p;
}

NNClient *nn_new(NKConn *conn, NNDoneFn greet_fn, void *greet_user)
{
    NNClient *c = (NNClient *)calloc(1, sizeof(*c));
    if (!c)
        return 0;
    c->conn = conn;
    if (push(c, NN_SINGLE, 0, 0, greet_fn, greet_user) != NK_OK) {
        free(c);
        return 0;
    }
    return c;
}

static void fail_all(NNClient *c, int err)
{
    Pending *p;
    while ((p = pop(c)) != 0) {
        if (p->done_fn)
            p->done_fn(p->user, 0, "", 0, err);
        free(p);
    }
    free(c->text);
    c->text = 0;
    c->in_block = 0;
}

void nn_free(NNClient *c)
{
    if (!c)
        return;
    fail_all(c, NK_ECANCELLED);
    nk_conn_free(c->conn);
    free(c);
}

int nn_cmd(NNClient *c, const char *line, int multi,
           NNLineFn line_fn, void *line_user, NNDoneFn done_fn, void *user)
{
    int r;
    if (c->dead)
        return c->dead;
    if (strchr(line, '\r') || strchr(line, '\n'))
        return NK_EARG;
    r = nk_conn_sendline(c->conn, line);
    if (r != NK_OK)
        return r;
    return push(c, multi, line_fn, line_user, done_fn, user);
}

int nn_send_article(NNClient *c, const char *text, size_t len,
                    NNDoneFn done_fn, void *user)
{
    size_t i = 0;
    int r;
    if (c->dead)
        return c->dead;
    while (i < len) {
        const char *s = text + i, *lf = (const char *)memchr(s, '\n', len - i);
        size_t n = lf ? (size_t)(lf - s) : len - i;
        size_t step = lf ? n + 1 : n;
        if (n && s[n - 1] == '\r')
            n--;
        if (n && s[0] == '.' && (r = nk_conn_send(c->conn, ".", 1)) != NK_OK)
            return r;
        if ((r = nk_conn_send(c->conn, s, n)) != NK_OK ||
            (r = nk_conn_send(c->conn, "\r\n", 2)) != NK_OK)
            return r;
        i += step;
    }
    if ((r = nk_conn_send(c->conn, ".\r\n", 3)) != NK_OK)
        return r;
    return push(c, NN_SINGLE, 0, 0, done_fn, user);
}

static int parse_status(const char *l, size_t n, int *code, const char **text, size_t *tl)
{
    if (n < 3 || l[0] < '1' || l[0] > '5' || l[1] < '0' || l[1] > '9' ||
        l[2] < '0' || l[2] > '9' || (n > 3 && l[3] != ' '))
        return 0;
    *code = (l[0] - '0') * 100 + (l[1] - '0') * 10 + (l[2] - '0');
    *text = n > 4 ? l + 4 : l + n;
    *tl = n > 4 ? n - 4 : 0;
    return 1;
}

static void finish(NNClient *c, int code, const char *text, size_t tl, int err)
{
    Pending *p = pop(c);
    if (p && p->done_fn)
        p->done_fn(p->user, code, text, tl, err);
    free(p);
}

int nn_pump(NNClient *c, uint32_t now_ms)
{
    int r;
    if (c->dead)
        return c->dead;
    r = nk_conn_pump(c->conn, now_ms);
    for (;;) {
        const char *l;
        size_t n;
        int k = nk_conn_line(c->conn, &l, &n);
        if (k == 0)
            break;
        if (k == NK_ETOOLONG) {
            if (c->in_block)
                c->block_err = NK_ETOOLONG;
            else {
                /* a status line over the cap: the server is broken */
                r = NK_EPROTO;
                break;
            }
            continue;
        }
        if (c->in_block) {
            Pending *p = c->head;
            if (n == 1 && l[0] == '.') {
                char *t = c->text;
                c->text = 0;
                c->in_block = 0;
                finish(c, c->code, t ? t : "", t ? strlen(t) : 0, c->block_err);
                free(t);
                continue;
            }
            if (n && l[0] == '.') {   /* dot-stuffed */
                l++;
                n--;
            }
            if (p && p->line_fn && !c->stop_lines && p->line_fn(p->line_user, l, n) != 0)
                c->stop_lines = 1;
            continue;
        }
        {
            int code;
            const char *t;
            size_t tl;
            Pending *p = c->head;
            if (!p || !parse_status(l, n, &code, &t, &tl)) {
                r = NK_EPROTO;     /* a reply nobody asked for, or not a reply */
                break;
            }
            if (p->multi && code < 400) {
                c->in_block = 1;
                c->stop_lines = 0;
                c->block_err = NK_OK;
                c->code = code;
                c->text = (char *)malloc(tl + 1);
                if (!c->text) {
                    r = NK_ENOMEM;
                    break;
                }
                memcpy(c->text, t, tl);
                c->text[tl] = 0;
            } else
                finish(c, code, t, tl, NK_OK);
        }
    }
    if (r != NK_OK) {
        c->dead = r;
        fail_all(c, r);
    }
    return r;
}

size_t nn_waiting(const NNClient *c)
{
    return c->waiting;
}

NKConn *nn_conn(NNClient *c)
{
    return c->conn;
}

/* ---- parsers ------------------------------------------------------------- */
static int num(const char **p, const char *end, uint32_t *v)
{
    const char *s = *p;
    uint64_t x = 0;
    while (s < end && *s == ' ')
        s++;
    if (s >= end || *s < '0' || *s > '9')
        return 0;
    while (s < end && *s >= '0' && *s <= '9') {
        x = x * 10 + (uint64_t)(*s - '0');
        if (x > 0xffffffffu)
            return 0;
        s++;
    }
    *v = (uint32_t)x;
    *p = s;
    return 1;
}

int nn_parse_group(const char *text, size_t len, uint32_t *count, uint32_t *low,
                   uint32_t *high, char *name, size_t name_cap)
{
    const char *p = text, *e = text + len, *s;
    size_t n;
    if (!num(&p, e, count) || !num(&p, e, low) || !num(&p, e, high))
        return 0;
    while (p < e && *p == ' ')
        p++;
    s = p;
    while (p < e && *p != ' ')
        p++;
    n = (size_t)(p - s);
    if (!n || n >= name_cap)
        return 0;
    memcpy(name, s, n);
    name[n] = 0;
    return 1;
}

int nn_parse_active(const char *line, size_t len, NNActive *out)
{
    const char *p = line, *e = line + len, *s;
    size_t n;
    while (p < e && *p != ' ')
        p++;
    n = (size_t)(p - line);
    if (!n || n >= sizeof(out->name))
        return 0;
    memcpy(out->name, line, n);
    out->name[n] = 0;
    if (!num(&p, e, &out->high) || !num(&p, e, &out->low))
        return 0;
    while (p < e && *p == ' ')
        p++;
    out->status = p < e ? *p : 'y';
    (void)s;
    return 1;
}

int nn_parse_over(const char *line, size_t len, NNOver *out, char *buf, size_t cap)
{
    char *f[16];
    size_t nf = 0, i;
    const char *p;
    if (len + 1 > cap)
        return 0;
    memcpy(buf, line, len);
    buf[len] = 0;
    f[nf++] = buf;
    for (i = 0; i < len && nf < 16; i++)
        if (buf[i] == '\t') {
            buf[i] = 0;
            f[nf++] = buf + i + 1;
        }
    if (nf < 8)
        return 0;
    p = f[0];
    if (!num(&p, f[0] + strlen(f[0]), &out->number) || *p)
        return 0;
    out->subject = f[1];
    out->from = f[2];
    out->date = f[3];
    out->msgid = f[4];
    out->references = f[5];
    p = f[6];
    if (!num(&p, f[6] + strlen(f[6]), &out->bytes))
        out->bytes = 0;
    p = f[7];
    if (!num(&p, f[7] + strlen(f[7]), &out->lines))
        out->lines = 0;
    out->xref = "";
    for (i = 8; i < nf; i++)
        if ((f[i][0] == 'X' || f[i][0] == 'x') && strncmp(f[i] + 1, "ref: ", 5) == 0) {
            out->xref = f[i] + 6;
            break;
        }
    return 1;
}
