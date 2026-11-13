/* GFOpenNews reader models: groups, threads, the queue panel and posting.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/ui_model.h"

static char *dupstr(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *d = malloc(n + 1);
    if (d) memcpy(d, s ? s : "", n + 1);
    return d;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* holds needle, any case */
static int has_text(const char *hay, const char *needle)
{
    size_t i, n = strlen(needle);
    if (!n) return 1;
    for (; *hay; hay++) {
        for (i = 0; i < n && hay[i] && lower((unsigned char)hay[i]) == lower((unsigned char)needle[i]); i++)
            ;
        if (i == n) return 1;
    }
    return 0;
}

/* ---- Groups ------------------------------------------------------------- */

struct NNUiGroups {
    NNUiGroup *all;
    size_t     n;
    size_t    *show;
    size_t     nshow;
    char      *filter;
};

NNUiGroups *nn_ug_new(void)
{
    NNUiGroups *g = calloc(1, sizeof(*g));
    if (g && !(g->filter = dupstr(""))) { free(g); return NULL; }
    return g;
}

static void ug_clear(NNUiGroups *g)
{
    size_t i;
    for (i = 0; i < g->n; i++) free((char *)g->all[i].name);
    free(g->all); free(g->show);
    g->all = NULL; g->show = NULL; g->n = g->nshow = 0;
}

void nn_ug_free(NNUiGroups *g)
{
    if (!g) return;
    ug_clear(g);
    free(g->filter);
    free(g);
}

void nn_ug_recount(NNUiGroups *g, const NNNewsrc *rc)
{
    size_t i, j;
    for (i = 0; i < g->n; i++) {
        NNUiGroup *u = &g->all[i];
        u->unread = 0;
        if (!u->high) continue;
        for (j = 0; j < rc->n; j++)
            if (!strcmp(rc->g[j].name, u->name)) { u->unread = nn_rc_unread(&rc->g[j], u->low, u->high); break; }
    }
}

int nn_ug_set(NNUiGroups *g, const NNNewsrc *rc, const NNActive *act, size_t nact)
{
    size_t i, j, n = 0;
    ug_clear(g);
    for (i = 0; i < rc->n; i++) n += rc->g[i].subscribed != 0;
    g->all = calloc(n ? n : 1, sizeof(*g->all));
    g->show = calloc(n ? n : 1, sizeof(*g->show));
    if (!g->all || !g->show) { ug_clear(g); return NK_ENOMEM; }
    for (i = 0; i < rc->n; i++) {
        NNUiGroup *u;
        if (!rc->g[i].subscribed) continue;
        u = &g->all[g->n];
        if (!(u->name = dupstr(rc->g[i].name))) { ug_clear(g); return NK_ENOMEM; }
        g->n++;
        for (j = 0; j < nact; j++)
            if (!strcmp(act[j].name, u->name)) { u->low = act[j].low; u->high = act[j].high; break; }
    }
    nn_ug_recount(g, rc);
    nn_ug_filter(g, g->filter);
    return NK_OK;
}

size_t nn_ug_filter(NNUiGroups *g, const char *text)
{
    size_t i;
    if (text != g->filter) {
        char *f = dupstr(text);
        if (f) { free(g->filter); g->filter = f; }
    }
    g->nshow = 0;
    for (i = 0; i < g->n; i++)
        if (has_text(g->all[i].name, g->filter)) g->show[g->nshow++] = i;
    return g->nshow;
}

size_t nn_ug_rows(const NNUiGroups *g) { return g->nshow; }

const NNUiGroup *nn_ug_row(const NNUiGroups *g, size_t row)
{
    return row < g->nshow ? &g->all[g->show[row]] : NULL;
}

long nn_ug_find(const NNUiGroups *g, const char *name)
{
    size_t i;
    for (i = 0; i < g->nshow; i++)
        if (!strcmp(g->all[g->show[i]].name, name)) return (long)i;
    return -1;
}

size_t nn_ug_label(const NNUiGroups *g, size_t row, char *buf, size_t cap)
{
    const NNUiGroup *u = nn_ug_row(g, row);
    int n;
    if (!u) n = snprintf(buf, cap, "%s", "");
    else if (u->unread) n = snprintf(buf, cap, "%s (%lu)", u->name, (unsigned long)u->unread);
    else n = snprintf(buf, cap, "%s", u->name);
    return n < 0 ? 0 : (size_t)n;
}

/* ---- Threads ------------------------------------------------------------ */

typedef struct Cont {
    NNUiRow row;
    char   *msgid, *refs;     /* owned; row's strings point at these and the rest */
    char   *subject, *from, *date;
    int     has;              /* an article, not only a message-id seen in References */
    long    parent, kid, next, tail;
    uint32_t min;             /* lowest article number in it and under it */
    int     dead;             /* pruned */
} Cont;

struct NNUiThreads {
    Cont   *c;
    size_t  n, cap;
    long   *hash;             /* message-id -> container, open addressing */
    size_t  hcap;
    long   *order;            /* every live container, depth first */
    size_t  norder;
    long   *rows;             /* those shown */
    size_t  nrows;
    long    roots;            /* first root */
};

NNUiThreads *nn_ut_new(void) { NNUiThreads *t = calloc(1, sizeof(*t)); if (t) t->roots = -1; return t; }

void nn_ut_free(NNUiThreads *t)
{
    size_t i;
    if (!t) return;
    for (i = 0; i < t->n; i++) {
        free(t->c[i].msgid); free(t->c[i].refs);
        free(t->c[i].subject); free(t->c[i].from); free(t->c[i].date);
    }
    free(t->c); free(t->hash); free(t->order); free(t->rows);
    free(t);
}


static unsigned long hash_n(const char *s, size_t n)
{
    unsigned long h = 5381;
    while (n--) h = h * 33 + (unsigned char)*s++;
    return h;
}

static int hash_grow(NNUiThreads *t)
{
    size_t i, cap = t->hcap ? t->hcap * 2 : 64;
    long *h = malloc(cap * sizeof(*h));
    if (!h) return NK_ENOMEM;
    for (i = 0; i < cap; i++) h[i] = -1;
    for (i = 0; i < t->n; i++) {
        size_t k = hash_n(t->c[i].msgid, strlen(t->c[i].msgid)) & (cap - 1);
        while (h[k] >= 0) k = (k + 1) & (cap - 1);
        h[k] = (long)i;
    }
    free(t->hash); t->hash = h; t->hcap = cap;
    return NK_OK;
}

static long find_id(const NNUiThreads *t, const char *id, size_t len)
{
    size_t k;
    if (!t->hcap) return -1;
    for (k = hash_n(id, len) & (t->hcap - 1); t->hash[k] >= 0; k = (k + 1) & (t->hcap - 1)) {
        const char *m = t->c[t->hash[k]].msgid;
        if (strlen(m) == len && !memcmp(m, id, len)) return t->hash[k];
    }
    return -1;
}

/* A new container for id (len bytes); its index, or -1 when memory ran out. */
static long new_cont(NNUiThreads *t, const char *id, size_t len)
{
    Cont *c;
    size_t k;
    if (t->n == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 64;
        Cont *nc = realloc(t->c, cap * sizeof(*nc));
        if (!nc) return -1;
        t->c = nc; t->cap = cap;
    }
    if ((t->n + 1) * 2 > t->hcap && hash_grow(t) != NK_OK) return -1;
    c = &t->c[t->n];
    memset(c, 0, sizeof(*c));
    if (!(c->msgid = malloc(len + 1))) return -1;
    memcpy(c->msgid, id, len); c->msgid[len] = 0;
    c->parent = c->kid = c->next = c->tail = -1;
    for (k = hash_n(id, len) & (t->hcap - 1); t->hash[k] >= 0; k = (k + 1) & (t->hcap - 1))
        ;
    t->hash[k] = (long)t->n;
    return (long)t->n++;
}

int nn_ut_add(NNUiThreads *t, const NNOver *ov)
{
    char key[32];
    const char *id = ov->msgid ? ov->msgid : "";
    size_t len = strlen(id);
    long i;
    Cont *c;
    if (!len) {                      /* no message-id: an article of its own */
        len = (size_t)snprintf(key, sizeof(key), "#%lu", (unsigned long)ov->number);
        id = key;
    }
    i = find_id(t, id, len);
    if (i >= 0 && t->c[i].has) return NK_OK;
    if (i < 0 && (i = new_cont(t, id, len)) < 0) return NK_ENOMEM;
    c = &t->c[i];
    c->subject = dupstr(ov->subject); c->from = dupstr(ov->from);
    c->date = dupstr(ov->date); c->refs = dupstr(ov->references);
    if (!c->subject || !c->from || !c->date || !c->refs) return NK_ENOMEM;
    c->has = 1;
    c->row.number = ov->number;
    c->row.bytes = ov->bytes; c->row.lines = ov->lines;
    return NK_OK;
}

/* 1 when making p the parent of c would close a loop */
static int loops(const NNUiThreads *t, long c, long p)
{
    for (; p >= 0; p = t->c[p].parent)
        if (p == c) return 1;
    return 0;
}

/* The next "<...>" in refs from *pos: its start and length, 0 at the end. */
static int next_ref(const char *refs, size_t *pos, const char **id, size_t *len)
{
    const char *s = refs + *pos, *e;
    while (*s && *s != '<') s++;
    if (!*s) { *pos = (size_t)(s - refs); return 0; }
    for (e = s; *e && *e != '>' && *e != ' ' && *e != '\t'; e++)
        ;
    if (*e != '>') { *pos = (size_t)(e - refs); return next_ref(refs, pos, id, len); }
    *id = s; *len = (size_t)(e - s + 1);
    *pos = (size_t)(e + 1 - refs);
    return 1;
}

const char *nn_base_subject(const char *s)
{
    if (!s) return "";
    for (;;) {
        const char *p = s;
        while (*p == ' ' || *p == '\t') p++;
        if (lower((unsigned char)p[0]) == 'r' && lower((unsigned char)p[1]) == 'e') {
            const char *q = p + 2;
            if (*q == '[') {                     /* Re[2]: */
                q++;
                while (*q >= '0' && *q <= '9') q++;
                if (*q != ']') return p;
                q++;
            }
            if (*q == ':') { s = q + 1; continue; }
        }
        return p;
    }
}

static int is_reply(const char *s) { return nn_base_subject(s) != s && *s; }

/* qsort has no user pointer: the threads are in a static just for the sort */
static const NNUiThreads *sort_t;
static int cmp_min(const void *a, const void *b)
{
    uint32_t x = sort_t->c[*(const long *)a].min, y = sort_t->c[*(const long *)b].min;
    return x < y ? -1 : x > y;
}
static int cmp_num(const void *a, const void *b)
{
    uint32_t x = sort_t->c[*(const long *)a].row.number, y = sort_t->c[*(const long *)b].row.number;
    return x < y ? -1 : x > y;
}

/* Each container's lowest article number in it and under it, without
 * recursion (a long chain of answers must not run the stack out): the
 * articles from the lowest number up each fill in their ancestors until one
 * already has a number, which is then lower. */
static int fill_min(NNUiThreads *t, long *idx)
{
    size_t i, m = 0;
    for (i = 0; i < t->n; i++) {
        t->c[i].min = UINT32_MAX;
        if (!t->c[i].dead && t->c[i].has) idx[m++] = (long)i;
    }
    sort_t = t;
    qsort(idx, m, sizeof(*idx), cmp_num);
    for (i = 0; i < m; i++) {
        long k = idx[i];
        uint32_t num = t->c[k].row.number;
        for (; k >= 0 && t->c[k].min == UINT32_MAX; k = t->c[k].parent) t->c[k].min = num;
    }
    return NK_OK;
}

static void link_kids(NNUiThreads *t)
{
    size_t i;
    for (i = 0; i < t->n; i++) t->c[i].kid = t->c[i].next = t->c[i].tail = -1;
    t->roots = -1;
    {
        long tail = -1;
        for (i = 0; i < t->n; i++) {
            Cont *c = &t->c[i];
            if (c->dead) continue;
            if (c->parent >= 0) {
                Cont *p = &t->c[c->parent];
                if (p->tail >= 0) t->c[p->tail].next = (long)i; else p->kid = (long)i;
                p->tail = (long)i;
            } else {
                if (tail >= 0) t->c[tail].next = (long)i; else t->roots = (long)i;
                tail = (long)i;
            }
        }
    }
}

static int kids_of(const NNUiThreads *t, long i)
{
    int n = 0;
    long k;
    for (k = t->c[i].kid; k >= 0; k = t->c[k].next) n++;
    return n;
}

/* Lays the rows out again from the open flags: every live container in
 * order[], depth first, and those not under a folded one in rows[]. */
static void flatten(NNUiThreads *t)
{
    long r;
    t->nrows = 0; t->norder = 0;
    for (r = t->roots; r >= 0; r = t->c[r].next) {
        long c = r;
        int depth = 0, hidden = -1;     /* depth of the folded row hiding what is under it */
        for (;;) {
            Cont *k = &t->c[c];
            k->row.depth = depth;
            t->order[t->norder++] = c;
            if (hidden < 0) t->rows[t->nrows++] = c;
            if (k->kid >= 0) {
                if (hidden < 0 && !k->row.open) hidden = depth;
                c = k->kid; depth++;
                continue;
            }
            while (c != r && t->c[c].next < 0) {
                c = t->c[c].parent; depth--;
                if (hidden == depth) hidden = -1;
            }
            if (c == r) break;
            c = t->c[c].next;
        }
    }
}

static void count_unread(NNUiThreads *t)
{
    size_t i;
    /* order[] is depth first: walking it backwards sees answers before their parents */
    for (i = 0; i < t->norder; i++) t->c[t->order[i]].row.unread_below = 0;
    for (i = t->norder; i-- > 0;) {
        Cont *c = &t->c[t->order[i]];
        if (c->parent >= 0)
            t->c[c->parent].row.unread_below += c->row.unread_below + (c->has && !c->row.read);
    }
}

int nn_ut_build(NNUiThreads *t, int fold)
{
    size_t i, n0 = t->n;
    int changed;
    long *idx;
    for (i = 0; i < t->n; i++) { t->c[i].parent = -1; t->c[i].dead = 0; }   /* built again after more were added */
    /* 1. parents from References: each id under the one before it, the
     *    article under the last one */
    for (i = 0; i < n0; i++) {
        size_t pos = 0, len;
        const char *id;
        long prev = -1;
        if (!t->c[i].has) continue;
        while (next_ref(t->c[i].refs, &pos, &id, &len)) {
            long r = find_id(t, id, len);
            if (r < 0 && (r = new_cont(t, id, len)) < 0) return NK_ENOMEM;
            if (prev >= 0 && r != prev && t->c[r].parent < 0 && !loops(t, r, prev))
                t->c[r].parent = prev;
            prev = r;
        }
        if (prev >= 0 && prev != (long)i && !loops(t, (long)i, prev)) t->c[i].parent = prev;
    }
    /* 2. empty containers out: an answer of a missing article moves up to
     *    the missing one's parent; a missing root with one answer gives way
     *    to it; one with none goes */
    do {
        changed = 0;
        link_kids(t);
        for (i = 0; i < t->n; i++) {
            Cont *c = &t->c[i];
            long k;
            if (c->dead || c->has) continue;
            if (c->kid < 0) { c->dead = 1; changed = 1; continue; }
            if (c->parent >= 0 || t->c[c->kid].next < 0) {   /* not a root, or one answer */
                for (k = c->kid; k >= 0; k = t->c[k].next) t->c[k].parent = c->parent;
                c->dead = 1; changed = 1;
            }
        }
    } while (changed);
    link_kids(t);
    /* 3. a root whose subject is a reply joins a root of the same subject
     *    that is not one */
    for (i = 0; i < t->n; i++) {
        Cont *c = &t->c[i];
        long r;
        if (c->dead || c->parent >= 0 || !c->has || !is_reply(c->subject)) continue;
        for (r = t->roots; r >= 0; r = t->c[r].next) {
            Cont *a = &t->c[r];
            if (r == (long)i || !a->has || is_reply(a->subject)) continue;
            if (*nn_base_subject(a->subject) && !strcmp(nn_base_subject(a->subject), nn_base_subject(c->subject))) {
                c->parent = r;
                break;
            }
        }
    }
    link_kids(t);
    /* 4. order: threads and answers by their lowest article number */
    idx = malloc((t->n ? t->n : 1) * sizeof(*idx));
    free(t->order); free(t->rows);
    t->order = malloc((t->n ? t->n : 1) * sizeof(*t->order));
    t->rows = malloc((t->n ? t->n : 1) * sizeof(*t->rows));
    if (!idx || !t->order || !t->rows) { free(idx); return NK_ENOMEM; }
    fill_min(t, idx);
    {
        size_t m = 0;
        for (i = 0; i < t->n; i++) if (!t->c[i].dead) idx[m++] = (long)i;
        sort_t = t;
        qsort(idx, m, sizeof(*idx), cmp_min);
        for (i = 0; i < t->n; i++) t->c[i].kid = t->c[i].next = t->c[i].tail = -1;
        t->roots = -1;
        {
            long tail = -1;
            for (i = 0; i < m; i++) {
                long k = idx[i];
                Cont *c = &t->c[k];
                if (c->parent >= 0) {
                    Cont *p = &t->c[c->parent];
                    if (p->tail >= 0) t->c[p->tail].next = k; else p->kid = k;
                    p->tail = k;
                } else {
                    if (tail >= 0) t->c[tail].next = k; else t->roots = k;
                    tail = k;
                }
            }
        }
    }
    free(idx);
    /* 5. rows */
    for (i = 0; i < t->n; i++) {
        Cont *c = &t->c[i];
        if (c->dead) continue;
        c->row.children = kids_of(t, (long)i);
        c->row.open = c->row.children ? !fold : 1;
        c->row.msgid = c->msgid;
        c->row.references = c->has ? c->refs : "";
        if (c->has) {
            c->row.subject = c->subject; c->row.from = c->from; c->row.date = c->date;
        } else {                       /* a missing article: its first answer's subject */
            long k = c->kid;
            while (k >= 0 && !t->c[k].has) k = t->c[k].kid;
            c->row.number = 0;
            c->row.subject = k >= 0 ? t->c[k].subject : "";
            c->row.from = ""; c->row.date = "";
        }
    }
    flatten(t);
    count_unread(t);
    return NK_OK;
}

void nn_ut_set_read(NNUiThreads *t, const NNGroupRc *g)
{
    size_t i;
    for (i = 0; i < t->n; i++) {
        Cont *c = &t->c[i];
        c->row.read = c->has ? (g && nn_rc_is_read(g, c->row.number)) : 1;
    }
    count_unread(t);
}

size_t nn_ut_rows(const NNUiThreads *t) { return t->nrows; }

const NNUiRow *nn_ut_row(const NNUiThreads *t, size_t row)
{
    return row < t->nrows ? &t->c[t->rows[row]].row : NULL;
}

int nn_ut_open(NNUiThreads *t, size_t row, int open)
{
    Cont *c;
    if (row >= t->nrows) return 0;
    c = &t->c[t->rows[row]];
    if (!c->row.children || c->row.open == (open != 0)) return 0;
    c->row.open = open != 0;
    flatten(t);
    return 1;
}

/* Opens what is above container k and answers its row. */
static long reveal(NNUiThreads *t, long k)
{
    size_t i;
    long p;
    int changed = 0;
    for (p = t->c[k].parent; p >= 0; p = t->c[p].parent)
        if (!t->c[p].row.open) { t->c[p].row.open = 1; changed = 1; }
    if (changed) flatten(t);
    for (i = 0; i < t->nrows; i++)
        if (t->rows[i] == k) return (long)i;
    return -1;
}

long nn_ut_show(NNUiThreads *t, uint32_t number)
{
    size_t i;
    if (!number) return -1;
    for (i = 0; i < t->norder; i++) {
        Cont *c = &t->c[t->order[i]];
        if (c->has && c->row.number == number) return reveal(t, t->order[i]);
    }
    return -1;
}

long nn_ut_next_unread(NNUiThreads *t, long row)
{
    size_t i = 0;
    if (row >= 0 && (size_t)row < t->nrows) {
        long k = t->rows[row];
        while (i < t->norder && t->order[i] != k) i++;
        i++;
    }
    for (; i < t->norder; i++) {
        Cont *c = &t->c[t->order[i]];
        if (c->has && !c->row.read) return reveal(t, t->order[i]);
    }
    return -1;
}

size_t nn_ut_label(const NNUiThreads *t, size_t row, char *buf, size_t cap)
{
    const NNUiRow *r = nn_ut_row(t, row);
    char pad[48];
    int d, n;
    if (!r) return (size_t)snprintf(buf, cap, "%s", "");
    d = r->depth > 20 ? 20 : r->depth;
    memset(pad, ' ', (size_t)d * 2); pad[d * 2] = 0;
    n = snprintf(buf, cap, "%s%s%s", pad, r->children ? (r->open ? "- " : "+ ") : "",
                 r->number ? r->subject : "(not on the server)");
    return n < 0 ? 0 : (size_t)n;
}

/* ---- The download queue ------------------------------------------------- */

int nn_uq_percent(const NNUiQFile *f)
{
    uint64_t p;
    if (!f->nsegs) return f->state == NN_UQ_DONE ? 100 : 0;
    if (f->done >= f->nsegs) return 100;
    p = (uint64_t)f->done * 100 / f->nsegs;
    return (int)(p > 99 ? 99 : p);
}

size_t nn_uq_state(const NNUiQFile *f, char *buf, size_t cap)
{
    int n;
    switch (f->state) {
    case NN_UQ_PAUSED:  n = snprintf(buf, cap, "Paused"); break;
    case NN_UQ_DONE:    n = snprintf(buf, cap, "Done"); break;
    case NN_UQ_BADCRC:  n = snprintf(buf, cap, "Damaged"); break;
    case NN_UQ_INCOMPLETE:
        n = snprintf(buf, cap, "Incomplete (%lu missing)", (unsigned long)f->failed); break;
    default:            n = snprintf(buf, cap, "Queued"); break;
    }
    return n < 0 ? 0 : (size_t)n;
}

size_t nn_uq_size(uint64_t bytes, char *buf, size_t cap)
{
    static const char *unit[] = { "KB", "MB", "GB", "TB" };
    uint64_t scale = 1024;
    int u = 0, n;
    if (bytes < 1024) n = snprintf(buf, cap, "%lu bytes", (unsigned long)bytes);
    else {
        uint64_t tenths;
        while (u < 3 && bytes >= scale * 1024) { scale *= 1024; u++; }
        tenths = (bytes * 10 + scale / 2) / scale;
        n = snprintf(buf, cap, "%lu.%lu %s", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10), unit[u]);
    }
    return n < 0 ? 0 : (size_t)n;
}

/* ---- Posting ------------------------------------------------------------ */

static void copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src ? src : "");
    if (n >= cap) n = cap - 1;
    memcpy(dst, src ? src : "", n);
    dst[n] = 0;
}

void nn_post_new(NNPost *p, const char *group, const char *from)
{
    memset(p, 0, sizeof(*p));
    copy(p->newsgroups, sizeof(p->newsgroups), group);
    copy(p->from, sizeof(p->from), from);
}

void nn_post_followup(NNPost *p, const char *group, const NNOver *parent, const char *from)
{
    char refs[2048];
    size_t len;
    nn_post_new(p, group, from);
    snprintf(p->subject, sizeof(p->subject), "Re: %s", nn_base_subject(parent->subject));
    snprintf(refs, sizeof(refs), "%s%s%s", parent->references ? parent->references : "",
             parent->references && *parent->references ? " " : "", parent->msgid ? parent->msgid : "");
    /* too long: drop the second oldest id until it fits (the first is kept) */
    while ((len = strlen(refs)) > NN_POST_REFS_MAX) {
        char *a = strchr(refs, ' '), *b;
        if (!a) break;
        b = strchr(a + 1, ' ');
        if (!b) break;
        memmove(a, b, strlen(b) + 1);
    }
    copy(p->references, sizeof(p->references), refs);
}

static int bad_header(const char *s) { return strchr(s, '\r') || strchr(s, '\n'); }

static int group_name_ok(const char *s, size_t n)
{
    size_t i;
    if (!n || s[0] == '.' || s[n - 1] == '.') return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.' && s[i + 1] == '.') return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

int nn_post_check(const NNPost *p, const char *body, char *why, size_t cap)
{
    const char *s = p->newsgroups, *b;
    int groups = 0;
    if (bad_header(p->newsgroups) || bad_header(p->subject) || bad_header(p->from) || bad_header(p->references)) {
        snprintf(why, cap, "A header can't hold a line break.");
        return 0;
    }
    while (*s) {
        const char *e;
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        for (e = s; *e && *e != ',' && *e != ' '; e++)
            ;
        if (!group_name_ok(s, (size_t)(e - s))) {
            snprintf(why, cap, "\"%.*s\" is not a group name.", (int)(e - s), s);
            return 0;
        }
        groups++;
        s = e;
    }
    if (!groups) { snprintf(why, cap, "Say which group to post to."); return 0; }
    if (groups > NN_POST_MAX_GROUPS) {
        snprintf(why, cap, "Posted to %d groups: %d at most.", groups, NN_POST_MAX_GROUPS);
        return 0;
    }
    for (s = p->subject; *s == ' ' || *s == '\t'; s++)
        ;
    if (!*s) { snprintf(why, cap, "The subject is empty."); return 0; }
    if (!strchr(p->from, '@')) { snprintf(why, cap, "Set your address (From) in the settings."); return 0; }
    for (b = body ? body : ""; *b == ' ' || *b == '\t' || *b == '\r' || *b == '\n'; b++)
        ;
    if (!*b) { snprintf(why, cap, "The article is empty."); return 0; }
    if (cap) why[0] = 0;
    return 1;
}

typedef struct Out { char *p; size_t cap, len; } Out;

static void put(Out *o, const char *s, size_t n)
{
    if (o->len < o->cap) {
        size_t k = o->cap - o->len - 1 < n ? o->cap - o->len - 1 : n;
        memcpy(o->p + o->len, s, k);
        o->p[o->len + k] = 0;
    }
    o->len += n;
}
static void puts0(Out *o, const char *s) { put(o, s, strlen(s)); }

size_t nn_post_quote(const char *who, const char *text, size_t len, char *out, size_t cap)
{
    Out o = { out, cap, 0 };
    size_t i = 0;
    if (cap) out[0] = 0;
    puts0(&o, who && *who ? who : "Someone");
    puts0(&o, " wrote:\n");
    while (i < len) {
        size_t e = i, n;
        while (e < len && text[e] != '\n') e++;
        n = e - i;
        if (n && text[i + n - 1] == '\r') n--;
        if (n == 3 && !memcmp(text + i, "-- ", 3)) break;
        if (n && text[i] == '>') put(&o, ">", 1);
        else if (n) put(&o, "> ", 2);
        else put(&o, ">", 1);
        put(&o, text + i, n);
        put(&o, "\n", 1);
        i = e + 1;
    }
    return o.len;
}

size_t nn_post_format(const NNPost *p, const char *body, char *out, size_t cap)
{
    Out o = { out, cap, 0 };
    const char *s;
    if (cap) out[0] = 0;
    puts0(&o, "From: "); puts0(&o, p->from); puts0(&o, "\r\n");
    puts0(&o, "Newsgroups: "); puts0(&o, p->newsgroups); puts0(&o, "\r\n");
    puts0(&o, "Subject: "); puts0(&o, p->subject); puts0(&o, "\r\n");
    if (p->references[0]) { puts0(&o, "References: "); puts0(&o, p->references); puts0(&o, "\r\n"); }
    puts0(&o, "User-Agent: GFOpenNews\r\n\r\n");
    for (s = body ? body : ""; *s; s++) {
        if (*s == '\r' && s[1] == '\n') continue;
        if (*s == '\n') put(&o, "\r\n", 2);
        else put(&o, s, 1);
    }
    if (o.len >= 2 && !(body && *body && body[strlen(body) - 1] == '\n')) put(&o, "\r\n", 2);
    return o.len;
}
