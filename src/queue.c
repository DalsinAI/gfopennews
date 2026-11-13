/* Download queue: journal, segment scheduling, server failover, assembly.
 * MIT licence, (c) 2026 AmiCradle Team.
 *
 * queue.db records (one per line, the name always last):
 *   F id nsegs bytes name     a file from an NZB
 *   S id number bytes msgid   one of its segments
 *   Z id size                 the file's real size (from yEnc)
 *   D id number               segment fetched and written
 *   E id number               segment missing on every server
 *   P id 0|1                  paused or not
 *   C id state path           finished (state as NN_QF_*)
 *   R id                      removed */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/binaries.h"
#include "gfopennews/queue.h"

enum { S_TODO, S_FLIGHT, S_DONE, S_FAILED };

typedef struct QSeg {
    uint32_t number, bytes;
    char    *msgid;
    uint8_t  state, tried;
} QSeg;

typedef struct QFile {
    uint32_t    id;
    char       *name, *path;
    int         state, removed;
    QSeg       *segs;
    uint32_t    nsegs, nset, ndone, nfailed, inflight;
    uint64_t    bytes, size;
    NNPartFile *pf;
} QFile;

typedef struct QConn {
    NNClient *c;
    int server, depth, inflight;
} QConn;

typedef struct Job {
    NNQueue *q;
    uint32_t fid;
    uint32_t si;
    NNClient *c;
    int      server;
    NNYenc   y;
    int      write_err;
} Job;

struct NNQueue {
    char      *dir, *out;
    NKJournal *j;
    QFile     *f;
    size_t     n, cap;
    uint32_t   next_id;
    QConn      conns[32];
    int        nconns, nservers;
    long       limit;            /* -1 = none */
};

static QFile *find(NNQueue *q, uint32_t id)
{
    size_t i;
    for (i = 0; i < q->n; i++)
        if (q->f[i].id == id && !q->f[i].removed)
            return &q->f[i];
    return 0;
}

static QSeg *seg_of(QFile *f, uint32_t number)
{
    size_t a = 0, b = f->nset;
    while (a < b) {               /* segments are kept sorted */
        size_t m = (a + b) / 2;
        if (f->segs[m].number == number) return &f->segs[m];
        if (f->segs[m].number < number) a = m + 1; else b = m;
    }
    return 0;
}

static char *sdup(const char *s)
{
    size_t l = strlen(s);
    char *p = (char *)malloc(l + 1);
    if (p) memcpy(p, s, l + 1);
    return p;
}

static QFile *new_file(NNQueue *q, uint32_t id, uint32_t nsegs, uint64_t bytes, const char *name)
{
    QFile *f;
    if (q->n == q->cap) {
        size_t nc = q->cap ? q->cap * 2 : 16;
        QFile *nf = (QFile *)realloc(q->f, nc * sizeof(*nf));
        if (!nf) return 0;
        q->f = nf;
        q->cap = nc;
    }
    f = &q->f[q->n];
    memset(f, 0, sizeof(*f));
    f->segs = (QSeg *)calloc(nsegs ? nsegs : 1, sizeof(QSeg));
    f->name = sdup(name);
    if (!f->segs || !f->name) {
        free(f->segs);
        free(f->name);
        return 0;
    }
    f->id = id;
    f->nsegs = nsegs;
    f->bytes = bytes;
    q->n++;
    if (id >= q->next_id)
        q->next_id = id + 1;
    return f;
}

static void free_file(QFile *f)
{
    uint32_t i;
    for (i = 0; i < f->nset; i++)
        free(f->segs[i].msgid);
    free(f->segs);
    free(f->name);
    free(f->path);
    nn_pf_close(f->pf);
    f->pf = 0;
}

static int rec(NNQueue *q, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return NK_EARG;
    return nk_journal_append(q->j, buf, (size_t)n);
}

/* ---- replay ------------------------------------------------------------------- */
static const char *word(const char *s, const char *e, unsigned long long *v)
{
    unsigned long long x = 0;
    int d = 0;
    while (s < e && *s == ' ') s++;
    while (s < e && *s >= '0' && *s <= '9') x = x * 10 + (unsigned long long)(*s++ - '0'), d++;
    if (!d) return 0;
    *v = x;
    return s;
}

static int replay(void *u, const char *r, size_t len)
{
    NNQueue *q = (NNQueue *)u;
    const char *e = r + len, *p;
    unsigned long long id, a, b;
    QFile *f;
    QSeg *s;
    char tmp[600];
    if (len < 3 || r[1] != ' ')
        return 0;
    if (!(p = word(r + 2, e, &id)))
        return 0;
    f = find(q, (uint32_t)id);
    switch (r[0]) {
    case 'F':
        if (f || !(p = word(p, e, &a)) || !(p = word(p, e, &b)) || p >= e || *p != ' ')
            return 0;
        if ((size_t)(e - p - 1) >= sizeof(tmp) || a > 1000000)
            return 0;
        memcpy(tmp, p + 1, (size_t)(e - p - 1));
        tmp[e - p - 1] = 0;
        new_file(q, (uint32_t)id, (uint32_t)a, b, tmp);
        return 0;
    case 'S':
        if (!f || !(p = word(p, e, &a)) || !(p = word(p, e, &b)) || p >= e || *p != ' ' ||
            f->nset >= f->nsegs)
            return 0;
        if ((size_t)(e - p - 1) >= sizeof(tmp))
            return 0;
        memcpy(tmp, p + 1, (size_t)(e - p - 1));
        tmp[e - p - 1] = 0;
        if (!nn_msgid_ok(tmp) || (f->nset && f->segs[f->nset - 1].number >= a))
            return 0;
        f->segs[f->nset].number = (uint32_t)a;
        f->segs[f->nset].bytes = (uint32_t)b;
        f->segs[f->nset].msgid = sdup(tmp);
        f->nset++;
        return 0;
    case 'Z':
        if (f && word(p, e, &a))
            f->size = a;
        return 0;
    case 'D':
    case 'E':
        if (f && word(p, e, &a) && (s = seg_of(f, (uint32_t)a)) && s->state == S_TODO) {
            if (r[0] == 'D') { s->state = S_DONE; f->ndone++; }
            else { s->state = S_FAILED; f->nfailed++; }
        }
        return 0;
    case 'P':
        if (f && word(p, e, &a) && f->state <= NN_QF_PAUSED)
            f->state = a ? NN_QF_PAUSED : NN_QF_QUEUED;
        return 0;
    case 'C':
        if (f && (p = word(p, e, &a)) && p < e && *p == ' ') {
            f->state = (int)a;
            free(f->path);
            if ((size_t)(e - p - 1) < sizeof(tmp)) {
                memcpy(tmp, p + 1, (size_t)(e - p - 1));
                tmp[e - p - 1] = 0;
                f->path = sdup(tmp);
            }
        }
        return 0;
    case 'R':
        if (f) {
            free_file(f);
            f->removed = 1;
        }
        return 0;
    }
    return 0;
}

static char *out_path(NNQueue *q, const QFile *f, int with_id)
{
    size_t l = strlen(q->out) + strlen(f->name) + 24;
    char *p = (char *)malloc(l);
    char last = q->out[0] ? q->out[strlen(q->out) - 1] : '/';
    if (!p) return 0;
    snprintf(p, l, with_id ? "%s%s%s.%lu" : "%s%s%s", q->out,
             (last == '/' || last == ':') ? "" : "/", f->name, (unsigned long)f->id);
    return p;
}

static int open_pf(NNQueue *q, QFile *f)
{
    int e, k;
    for (k = 0; k < 2 && !f->pf; k++) {
        char *p = out_path(q, f, k);
        if (!p) return NK_ENOMEM;
        f->pf = nn_pf_open(p, f->size, f->nsegs, &e);
        free(p);
        if (!f->pf && e != NK_EARG)
            return e;
    }
    return f->pf ? NK_OK : NK_EIO;
}

NNQueue *nn_q_open(const char *dir, const char *out_dir, int nservers, int *err)
{
    NNQueue *q = (NNQueue *)calloc(1, sizeof(*q));
    char *path;
    size_t l, i;
    int e = NK_ENOMEM;
    if (!q || nservers < 1 || nservers > NN_Q_MAXSERVERS)
        goto fail;
    q->dir = sdup(dir);
    q->out = sdup(out_dir);
    q->nservers = nservers;
    q->limit = -1;
    q->next_id = 1;
    l = strlen(dir) + 16;
    path = (char *)malloc(l);
    if (!q->dir || !q->out || !path)
        goto fail;
    snprintf(path, l, "%s%squeue.db", dir,
             dir[0] && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != ':' ? "/" : "");
    q->j = nk_journal_open(path, replay, q, 0, &e);
    free(path);
    if (!q->j)
        goto fail;
    /* unfinished files: open their part files and believe them over D */
    for (i = 0; i < q->n; i++) {
        QFile *f = &q->f[i];
        uint32_t k;
        if (f->removed || f->state > NN_QF_PAUSED || !f->size)
            continue;
        if (open_pf(q, f) != NK_OK)
            continue;
        for (k = 0; k < f->nset; k++)
            if (f->segs[k].state == S_DONE && !nn_pf_have(f->pf, f->segs[k].number)) {
                f->segs[k].state = S_TODO;
                f->ndone--;
            }
    }
    if (err) *err = NK_OK;
    return q;
fail:
    nn_q_close(q);
    if (err) *err = e;
    return 0;
}

void nn_q_close(NNQueue *q)
{
    size_t i;
    if (!q) return;
    for (i = 0; i < q->n; i++)
        if (!q->f[i].removed)
            free_file(&q->f[i]);
    free(q->f);
    if (q->j) nk_journal_close(q->j);
    free(q->dir);
    free(q->out);
    free(q);
}

static int is_par2(const char *name, int *vol)
{
    size_t l = strlen(name);
    *vol = 0;
    if (l < 5) return 0;
    if (strcmp(name + l - 5, ".par2") && strcmp(name + l - 5, ".PAR2")) return 0;
    *vol = strstr(name, ".vol") != 0 || strstr(name, ".VOL") != 0;
    return 1;
}

int nn_q_add_nzb(NNQueue *q, const NNNzb *nzb)
{
    int added = 0, pass;
    size_t i, k;
    /* pass 0: data files, pass 1: the PAR2 index, pass 2: PAR2 volumes
     * (added paused: verify decides how many are needed) */
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < nzb->nfiles; i++) {
            const NNNzbFile *nf = &nzb->files[i];
            char name[256];
            uint32_t pp, pt;
            uint64_t bytes = 0;
            int vol, par = 0;
            QFile *f;
            nn_bin_subject(nf->subject, name, sizeof(name), &pp, &pt);
            if (!name[0])
                snprintf(name, sizeof(name), "file%lu", (unsigned long)q->next_id);
            par = is_par2(name, &vol);
            if ((pass == 0 && par) || (pass == 1 && (!par || vol)) || (pass == 2 && (!par || !vol)))
                continue;
            if (!nf->nsegs)
                continue;
            for (k = 0; k < nf->nsegs; k++)
                bytes += nf->segs[k].bytes;
            f = new_file(q, q->next_id, (uint32_t)nf->nsegs, bytes, name);
            if (!f)
                return added;
            rec(q, "F %lu %lu %llu %s", (unsigned long)f->id, (unsigned long)f->nsegs,
                (unsigned long long)bytes, name);
            for (k = 0; k < nf->nsegs; k++) {
                f->segs[k].number = nf->segs[k].number;
                f->segs[k].bytes = nf->segs[k].bytes;
                f->segs[k].msgid = sdup(nf->segs[k].msgid);
                rec(q, "S %lu %lu %lu %s", (unsigned long)f->id, (unsigned long)nf->segs[k].number,
                    (unsigned long)nf->segs[k].bytes, nf->segs[k].msgid);
            }
            f->nset = f->nsegs;
            if (pass == 2) {
                f->state = NN_QF_PAUSED;
                rec(q, "P %lu 1", (unsigned long)f->id);
            }
            added++;
        }
    nk_journal_sync(q->j);
    return added;
}

size_t nn_q_files(const NNQueue *q)
{
    size_t i, n = 0;
    for (i = 0; i < q->n; i++)
        n += !q->f[i].removed;
    return n;
}

int nn_q_info(const NNQueue *q, size_t index, NNQFileInfo *o)
{
    size_t i;
    for (i = 0; i < q->n; i++) {
        const QFile *f = &q->f[i];
        if (f->removed) continue;
        if (index--) continue;
        o->id = f->id;
        o->name = f->name;
        o->state = f->state;
        o->nsegs = f->nsegs;
        o->done = f->ndone;
        o->failed = f->nfailed;
        o->bytes = f->bytes;
        o->path = f->path;
        return 1;
    }
    return 0;
}

int nn_q_pause(NNQueue *q, uint32_t id, int pause)
{
    QFile *f = find(q, id);
    if (!f || f->state > NN_QF_PAUSED) return NK_EARG;
    f->state = pause ? NN_QF_PAUSED : NN_QF_QUEUED;
    rec(q, "P %lu %d", (unsigned long)id, pause ? 1 : 0);
    return nk_journal_sync(q->j);
}

int nn_q_remove(NNQueue *q, uint32_t id)
{
    QFile *f = find(q, id);
    if (!f || f->inflight) return NK_EARG;
    rec(q, "R %lu", (unsigned long)id);
    free_file(f);
    f->removed = 1;
    return nk_journal_sync(q->j);
}

int nn_q_attach(NNQueue *q, NNClient *c, int server, int depth)
{
    if (q->nconns >= (int)(sizeof(q->conns) / sizeof(q->conns[0])) || server < 0 ||
        server >= q->nservers || depth < 1)
        return NK_EARG;
    q->conns[q->nconns].c = c;
    q->conns[q->nconns].server = server;
    q->conns[q->nconns].depth = depth;
    q->conns[q->nconns].inflight = 0;
    q->nconns++;
    return NK_OK;
}

void nn_q_detach(NNQueue *q, NNClient *c)
{
    int i;
    for (i = 0; i < q->nconns; i++)
        if (q->conns[i].c == c) {
            q->conns[i] = q->conns[--q->nconns];
            return;
        }
}

void nn_q_limit(NNQueue *q, long n)
{
    q->limit = n;
}

typedef struct { char **r; size_t n, at; } RecIter;
static int compact_next(void *u, const char **rec, size_t *len)
{
    RecIter *it = (RecIter *)u;
    if (it->at >= it->n) return 0;
    *rec = it->r[it->at];
    *len = strlen(it->r[it->at]);
    it->at++;
    return 1;
}

/* ---- the fetch ------------------------------------------------------------------ */
static int job_write(void *u, uint64_t off, const uint8_t *d, size_t n)
{
    Job *jb = (Job *)u;
    NNQueue *q = jb->q;
    QFile *f = find(q, jb->fid);
    if (!f) return -1;
    if (!f->pf) {
        if (!f->size) {
            f->size = jb->y.size;
            rec(q, "Z %lu %llu", (unsigned long)f->id, (unsigned long long)f->size);
        }
        if (f->size != jb->y.size || open_pf(q, f) != NK_OK) {
            jb->write_err = 1;
            return -1;
        }
    }
    if (nn_pf_put(f->pf, off, d, n) != NK_OK) {
        jb->write_err = 1;
        return -1;
    }
    return 0;
}

static int job_line(void *u, const char *l, size_t n)
{
    nn_ye_line(&((Job *)u)->y, l, n);
    return 0;
}

static void finish_file(NNQueue *q, QFile *f)
{
    int st;
    if (f->inflight || f->ndone + f->nfailed < f->nsegs || f->state > NN_QF_PAUSED)
        return;
    if (f->nfailed || !f->pf)
        st = NN_QF_INCOMPLETE;      /* keep the .part for PAR2 repair */
    else {
        int r = nn_pf_finish(f->pf);
        st = r == NN_PF_DONE ? NN_QF_DONE : r == NN_PF_BADCRC ? NN_QF_BADCRC : NN_QF_INCOMPLETE;
        if (r == NN_PF_DONE) {
            free(f->path);
            f->path = sdup(nn_pf_final(f->pf));
        }
    }
    f->state = st;
    rec(q, "C %lu %d %s", (unsigned long)f->id, st, f->path ? f->path : "-");
    nk_journal_sync(q->j);
}

static void job_done(void *u, int code, const char *text, size_t tl, int err)
{
    Job *jb = (Job *)u;
    NNQueue *q = jb->q;
    QFile *f = find(q, jb->fid);
    QSeg *s = f ? &f->segs[jb->si] : 0;
    int ys;
    (void)text; (void)tl;
    {
        int i;
        for (i = 0; i < q->nconns; i++)
            if (q->conns[i].c == jb->c && q->conns[i].inflight > 0)
                q->conns[i].inflight--;
    }
    if (!f) {
        free(jb);
        return;
    }
    f->inflight--;
    ys = nn_ye_finish(&jb->y);
    if (code == 222 && err == NK_OK && ys == NN_YE_OK && jb->y.got && !jb->write_err &&
        nn_pf_commit(f->pf, s->number, jb->y.begin - 1, jb->y.got, jb->y.crc) == NK_OK) {
        if ((jb->y.have & NN_YE_HAVE_CRC) &&
            (!(jb->y.have & NN_YE_HAVE_PART) || !(jb->y.have & NN_YE_HAVE_PCRC) ||
             jb->y.crc32 != jb->y.pcrc32 || f->nsegs == 1))
            nn_pf_expect_crc(f->pf, jb->y.crc32);
        s->state = S_DONE;
        f->ndone++;
        rec(q, "D %lu %lu", (unsigned long)f->id, (unsigned long)s->number);
    } else if (code == 0 || jb->write_err) {
        /* the link went (or the disk): try again later, same servers */
        s->state = S_TODO;
        if (jb->write_err) {
            s->state = S_FAILED;
            f->nfailed++;
            rec(q, "E %lu %lu", (unsigned long)f->id, (unsigned long)s->number);
        }
    } else {
        /* 430/423, damaged data, or the server refused: next server */
        s->tried |= (uint8_t)(1u << jb->server);
        if (s->tried == (uint8_t)((1u << q->nservers) - 1)) {
            s->state = S_FAILED;
            f->nfailed++;
            rec(q, "E %lu %lu", (unsigned long)f->id, (unsigned long)s->number);
        } else
            s->state = S_TODO;
    }
    finish_file(q, f);
    free(jb);
}

/* the next segment connection `cn` may fetch, or 0 */
static int pick(NNQueue *q, int server, QFile **fo, uint32_t *si)
{
    size_t i;
    uint32_t k;
    for (i = 0; i < q->n; i++) {
        QFile *f = &q->f[i];
        if (f->removed || f->state != NN_QF_QUEUED)
            continue;
        for (k = 0; k < f->nset; k++) {
            QSeg *s = &f->segs[k];
            if (s->state == S_TODO && !(s->tried & (1u << server))) {
                *fo = f;
                *si = k;
                return 1;
            }
        }
    }
    return 0;
}

int nn_q_step(NNQueue *q)
{
    int i, busy = 0;
    size_t k;
    for (i = 0; i < q->nconns; i++) {
        QConn *cn = &q->conns[i];
        while (cn->inflight < cn->depth && q->limit != 0) {
            QFile *f;
            uint32_t si;
            Job *jb;
            char line[300];
            if (!pick(q, cn->server, &f, &si))
                break;
            jb = (Job *)calloc(1, sizeof(*jb));
            if (!jb)
                break;
            jb->q = q;
            jb->fid = f->id;
            jb->si = si;
            jb->c = cn->c;
            jb->server = cn->server;
            nn_ye_init(&jb->y, job_write, jb);
            snprintf(line, sizeof(line), "BODY <%s>", f->segs[si].msgid);
            if (nn_cmd(cn->c, line, NN_MULTI, job_line, jb, job_done, jb) != NK_OK) {
                free(jb);
                break;
            }
            f->segs[si].state = S_FLIGHT;
            f->inflight++;
            cn->inflight++;
            if (q->limit > 0)
                q->limit--;
        }
        busy |= cn->inflight > 0;
    }
    /* files whose last segments failed on every server, or a restart that
     * found them all done */
    for (k = 0; k < q->n; k++)
        if (!q->f[k].removed && q->f[k].state == NN_QF_QUEUED) {
            QFile *f = &q->f[k];
            if (f->ndone + f->nfailed == f->nsegs && !f->pf && f->size)
                open_pf(q, f);
            finish_file(q, f);
        }
    if (busy)
        return 1;
    if (q->limit == 0)
        return 0;
    for (k = 0; k < q->n; k++) {
        QFile *f = &q->f[k];
        if (!f->removed && f->state == NN_QF_QUEUED)
            for (i = 0; i < q->nconns; i++) {
                QFile *fo;
                uint32_t si;
                if (pick(q, q->conns[i].server, &fo, &si))
                    return 1;
            }
    }
    return 0;
}

typedef struct { char **r; size_t n, cap; int oom; } RecList;
static void ADD(RecList *L, const char *fmt, ...)
{
    char b[700];
    va_list ap;
    int l;
    va_start(ap, fmt);
    l = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (L->oom || l < 0 || (size_t)l >= sizeof(b))
        return;
    if (L->n == L->cap) {
        size_t c = L->cap ? L->cap * 2 : 64;
        char **p = (char **)realloc(L->r, c * sizeof(char *));
        if (!p) { L->oom = 1; return; }
        L->r = p;
        L->cap = c;
    }
    if (!(L->r[L->n] = sdup(b))) { L->oom = 1; return; }
    L->n++;
}

int nn_q_compact(NNQueue *q)
{
    /* rebuild as a list of records, then hand them to the journal */
    RecList L = { 0, 0, 0, 0 };
    size_t i, k;
    int r;
    RecIter it;
    for (i = 0; i < q->n; i++) {
        QFile *f = &q->f[i];
        if (f->removed) continue;
        ADD(&L, "F %lu %lu %llu %s", (unsigned long)f->id, (unsigned long)f->nsegs, (unsigned long long)f->bytes, f->name);
        for (k = 0; k < f->nset; k++)
            ADD(&L, "S %lu %lu %lu %s", (unsigned long)f->id, (unsigned long)f->segs[k].number,
                (unsigned long)f->segs[k].bytes, f->segs[k].msgid);
        if (f->size)
            ADD(&L, "Z %lu %llu", (unsigned long)f->id, (unsigned long long)f->size);
        for (k = 0; k < f->nset; k++) {
            if (f->segs[k].state == S_DONE) ADD(&L, "D %lu %lu", (unsigned long)f->id, (unsigned long)f->segs[k].number);
            if (f->segs[k].state == S_FAILED) ADD(&L, "E %lu %lu", (unsigned long)f->id, (unsigned long)f->segs[k].number);
        }
        if (f->state == NN_QF_PAUSED)
            ADD(&L, "P %lu 1", (unsigned long)f->id);
        if (f->state > NN_QF_PAUSED)
            ADD(&L, "C %lu %d %s", (unsigned long)f->id, f->state, f->path ? f->path : "-");
    }
    it.r = L.r;
    it.n = L.n;
    it.at = 0;
    r = L.oom ? NK_ENOMEM : nk_journal_compact(q->j, compact_next, &it);
    for (i = 0; i < L.n; i++) free(L.r[i]);
    free(L.r);
    return r;
}
