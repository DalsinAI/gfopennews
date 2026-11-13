/* GFOpenNews CLI: the headless reader (design step 2). Lists groups, fetches
 * headers into the overview cache and prints articles.
 *
 *   gfopennews-cli [options] groups
 *   gfopennews-cli [options] headers GROUP [LAST]      (default: the last 100)
 *   gfopennews-cli [options] article GROUP NUMBER
 *   gfopennews-cli [options] unread                     (needs --newsrc)
 *   gfopennews-cli [options] mark GROUP LOW[-HIGH]      (needs --newsrc)
 *   gfopennews-cli [options] nzb FILE.nzb               (queue it and fetch)
 *   gfopennews-cli [options] resume                     (carry on the queue)
 *   gfopennews-cli par2 verify|repair FILE.par2         (no server needed)
 * After a queue run with damaged or missing files, the PAR2 index is read,
 * just enough paused recovery volumes are fetched, and the files repaired.
 * options: --server HOST --port N --user U --pass-env VAR --socks HOST:PORT
 *          --newsrc FILE --cache DIR --out DIR --queue DIR --server2 HOST:PORT
 *          --conns N (per server) --limit N (stop after N segments)
 * The password comes from an environment variable for now; with
 * gfopenkeys.library it comes from the keystore (entry nntp/HOST/USER).
 *
 * This file is the host build (POSIX sockets); the Amiga CLI with ReadArgs,
 * bsdsocket and gfopentls is the next step.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennews/nntp.h"
#include "gfopennews/newsrc.h"
#include "gfopennews/overview.h"
#include "gfopennews/queue.h"
#include "gfopennews/par2.h"
#include <dirent.h>

typedef struct {
    int  code;
    char text[512];
    int  err;
    int  done;
} Reply;

static void on_done(void *u, int code, const char *text, size_t len, int err)
{
    Reply *r = (Reply *)u;
    r->code = code;
    r->err = err;
    if (len >= sizeof(r->text))
        len = sizeof(r->text) - 1;
    memcpy(r->text, text, len);
    r->text[len] = 0;
    r->done = 1;
}

static int wait_all(NNClient *c)
{
    uint32_t tick = 0;
    while (nn_waiting(c)) {
        struct pollfd p;
        int r;
        p.fd = nk_conn_fd(nn_conn(c));
        p.events = POLLIN | (nk_conn_outq(nn_conn(c)) ? POLLOUT : 0);
        poll(&p, 1, 1000);
        tick += 1000;
        if ((r = nn_pump(c, tick)) != NK_OK)
            return r;
    }
    return NK_OK;
}

static int cmd(NNClient *c, const char *line, int multi, NNLineFn lf, void *lu, Reply *r)
{
    int e;
    memset(r, 0, sizeof(*r));
    if ((e = nn_cmd(c, line, multi, lf, lu, on_done, r)) != NK_OK)
        return e;
    return wait_all(c);
}

/* ---- block printers -------------------------------------------------------- */
static int print_line(void *u, const char *l, size_t n)
{
    (void)u;
    fwrite(l, 1, n, stdout);
    putchar('\n');
    return 0;
}

typedef struct { NNNewsrc *rc; } GroupsCtx;
static int print_active(void *u, const char *l, size_t n)
{
    GroupsCtx *g = (GroupsCtx *)u;
    NNActive a;
    if (!nn_parse_active(l, n, &a))
        return 0;
    if (g->rc) {
        NNGroupRc *rg = nn_rc_group(g->rc, a.name, 0);
        if (!rg || !rg->subscribed)
            return 0;
        printf("%s\t%lu unread\n", a.name, (unsigned long)nn_rc_unread(rg, a.low, a.high));
    } else
        printf("%s\t%lu\t%lu\t%c\n", a.name, (unsigned long)a.high, (unsigned long)a.low, a.status);
    return 0;
}

typedef struct { NNOvCache *cache; unsigned long n; } HeadCtx;
static int take_over(void *u, const char *l, size_t n)
{
    HeadCtx *h = (HeadCtx *)u;
    if (h->cache)
        nn_ov_add(h->cache, l, n);
    h->n++;
    return 0;
}
static int print_over(void *u, const NNOver *ov)
{
    (void)u;
    printf("%lu\t%s\t%s\n", (unsigned long)ov->number, ov->subject, ov->from);
    return 0;
}


static void usage(void);
static const char *nn_cli_server;
static int nn_cli_port;

static NNClient *open_server(const char *server, int port, const char *socks,
                             const char *user, const char *passenv)
{
    NKTransport t;
    NKConn *conn;
    NNClient *c;
    Reply r;
    char line[600];
    if (socks) {
        char host[256];
        const char *colon = strrchr(socks, ':');
        NKSocks5 s;
        int k;
        if (!colon || (size_t)(colon - socks) >= sizeof(host)) { usage(); return 0; }
        memcpy(host, socks, (size_t)(colon - socks));
        host[colon - socks] = 0;
        if (nk_posix_tcp(host, (uint16_t)atoi(colon + 1), &t) != NK_OK) {
            fprintf(stderr, "cannot reach proxy %s\n", socks);
            return 0;
        }
        nk_socks5_init(&s, server, (uint16_t)port, 0, 0);
        while ((k = nk_socks5_step(&s, &t)) == NK_WOULDBLOCK) {
            struct pollfd p;
            p.fd = t.fd(t.ctx);
            p.events = POLLIN;
            poll(&p, 1, 1000);
        }
        if (k != 1) {
            fprintf(stderr, "proxy: %s\n", nk_strerror(k));
            nk_transport_close(&t);
            return 0;
        }
    } else if (nk_posix_tcp(server, (uint16_t)port, &t) != NK_OK) {
        fprintf(stderr, "cannot reach %s:%d\n", server, port);
        return 0;
    }
    conn = nk_conn_new(0, t, 0);
    memset(&r, 0, sizeof(r));
    c = nn_new(conn, on_done, &r);
    if (!c || wait_all(c) != NK_OK || r.code / 100 != 2) {
        fprintf(stderr, "no greeting: %d %s\n", r.code, r.text);
        nn_free(c);
        return 0;
    }
    cmd(c, "MODE READER", NN_SINGLE, 0, 0, &r);
    if (user) {
        const char *pw = passenv ? getenv(passenv) : 0;
        snprintf(line, sizeof(line), "AUTHINFO USER %s", user);
        if (cmd(c, line, NN_SINGLE, 0, 0, &r) != NK_OK) goto fail;
        if (r.code == 381) {
            if (!pw) { fprintf(stderr, "password needed (--pass-env)\n"); goto fail; }
            snprintf(line, sizeof(line), "AUTHINFO PASS %s", pw);
            if (cmd(c, line, NN_SINGLE, 0, 0, &r) != NK_OK) goto fail;
            memset(line, 0, sizeof(line));
        }
        if (r.code != 281) { fprintf(stderr, "login refused: %d %s\n", r.code, r.text); goto fail; }
    }
    return c;
fail:
    nn_free(c);
    return 0;
}

static const char *state_name(int st)
{
    switch (st) {
    case NN_QF_QUEUED: return "queued";
    case NN_QF_PAUSED: return "paused";
    case NN_QF_DONE: return "done";
    case NN_QF_BADCRC: return "bad-crc";
    default: return "incomplete";
    }
}

/* Loads FILE.par2 and every FILE.*.par2 beside it. */
static int par2_load_set(NNPar2 *p, const char *index, char *dir, size_t dirsz)
{
    const char *slash = strrchr(index, '/'), *base = slash ? slash + 1 : index;
    size_t bl = strlen(base);
    DIR *d;
    struct dirent *de;
    int r;
    memset(p, 0, sizeof(*p));
    if (slash) {
        size_t n = (size_t)(slash - index);
        if (n >= dirsz) return NK_EARG;
        memcpy(dir, index, n);
        dir[n] = 0;
    } else
        snprintf(dir, dirsz, ".");
    if ((r = nn_par2_load(p, index)) != NK_OK)
        return r;
    if (bl > 5 && !strcmp(base + bl - 5, ".par2"))
        bl -= 5;
    if ((d = opendir(dir)) == 0)
        return NK_OK;
    while ((de = readdir(d)) != 0) {
        size_t nl = strlen(de->d_name);
        char path[1024];
        if (nl > bl + 6 && !strncmp(de->d_name, base, bl) && de->d_name[bl] == '.' &&
            !strcmp(de->d_name + nl - 5, ".par2") && strcmp(de->d_name + bl, ".par2")) {
            snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
            nn_par2_load(p, path);
        }
    }
    closedir(d);
    return NK_OK;
}

/* par2 verify|repair FILE.par2: 0 all good (or repaired), 3 damaged, 1 error */
static int run_par2(const char *what, const char *index)
{
    NNPar2 p;
    char dir[1024];
    uint32_t bad, i;
    int r, status = 0;
    if ((r = par2_load_set(&p, index, dir, sizeof(dir))) != NK_OK) {
        fprintf(stderr, "cannot read %s: %s\n", index, nk_strerror(r));
        nn_par2_free(&p);
        return 1;
    }
    bad = nn_par2_verify(&p, dir);
    for (i = 0; i < p.nfiles; i++)
        printf("%s\t%s\t%lu/%lu\n", p.files[i].name,
               !p.files[i].present ? "missing" : p.files[i].damaged ? "damaged" :
               p.files[i].too_long ? "too long" : "ok",
               (unsigned long)p.files[i].damaged, (unsigned long)p.files[i].nslices);
    printf("damaged slices %lu, recovery blocks %lu\n", (unsigned long)bad, (unsigned long)p.nrec);
    if (!bad && !p.too_long) {
        nn_par2_free(&p);
        return 0;
    }
    if (strcmp(what, "repair")) {
        status = 3;
    } else if (bad > NN_PAR2_MAXMISS) {
        printf("more damage than one repair takes (%lu)\n", (unsigned long)NN_PAR2_MAXMISS);
        status = 3;
    } else if (bad > p.nrec) {
        printf("need %lu more recovery blocks\n", (unsigned long)(bad - p.nrec));
        status = 3;
    } else if ((r = nn_par2_repair(&p, dir)) != NK_OK) {
        fprintf(stderr, "repair failed: %s\n", nk_strerror(r));
        status = 1;
    } else {
        bad = nn_par2_verify(&p, dir);
        printf("repaired: %s\n", bad || p.too_long ? "no" : "yes");
        status = bad || p.too_long ? 1 : 0;
    }
    nn_par2_free(&p);
    return status;
}

/* After a queue run: if files are damaged and the set's PAR2 index is in,
 * unpause just enough recovery volumes (answers 1: fetch again), or repair
 * (answers 0 repaired, 3 not). -1: nothing to do. */
static int queue_par2(NNQueue *q, const char *out_dir, int *asked)
{
    NNQFileInfo fi;
    NNPar2 p;
    size_t i, nv = 0;
    const char *index = 0;
    uint32_t bad, blocks[64], ids[64];
    uint8_t pick[64];
    int damaged = 0, r;
    char dir[1024];
    for (i = 0; nn_q_info(q, i, &fi); i++) {
        size_t nl = strlen(fi.name);
        if (fi.state == NN_QF_BADCRC || fi.state == NN_QF_INCOMPLETE)
            damaged = 1;
        if (fi.state == NN_QF_DONE && nl > 5 && !strcmp(fi.name + nl - 5, ".par2") &&
            !nn_par2_vol_blocks(fi.name) && fi.path)
            index = fi.path;
    }
    if (!damaged || !index)
        return -1;
    if (par2_load_set(&p, index, dir, sizeof(dir)) != NK_OK) {
        nn_par2_free(&p);
        return -1;
    }
    bad = nn_par2_verify(&p, out_dir);
    printf("par2: %lu damaged slices, %lu recovery blocks in\n", (unsigned long)bad,
           (unsigned long)p.nrec);
    if (bad > NN_PAR2_MAXMISS) {
        /* refused before anything more is fetched */
        printf("par2: more damage than one repair takes (%lu)\n", (unsigned long)NN_PAR2_MAXMISS);
        nn_par2_free(&p);
        return 3;
    }
    if (bad > p.nrec && !*asked) {
        for (i = 0; nn_q_info(q, i, &fi) && nv < 64; i++)
            if (fi.state == NN_QF_PAUSED && nn_par2_vol_blocks(fi.name)) {
                blocks[nv] = nn_par2_vol_blocks(fi.name);
                ids[nv++] = fi.id;
            }
        nn_par2_pick(blocks, nv, bad - p.nrec, pick);
        for (i = 0; i < nv; i++)
            if (pick[i]) {
                nn_q_pause(q, ids[i], 0);
                printf("par2: fetching %lu more blocks\n", (unsigned long)blocks[i]);
            }
        *asked = 1;
        nn_par2_free(&p);
        return 1;
    }
    if (bad > p.nrec) {
        printf("par2: need %lu more recovery blocks\n", (unsigned long)(bad - p.nrec));
        nn_par2_free(&p);
        return 3;
    }
    r = nn_par2_repair(&p, out_dir);
    bad = r == NK_OK ? nn_par2_verify(&p, out_dir) : 1;
    printf("par2: repaired %s\n", bad ? "no" : "yes");
    nn_par2_free(&p);
    return bad ? 3 : 0;
}

/* nzb FILE | resume: fetch over `conns` connections per server */
static int run_queue(NNClient *first, int argc, char **argv, const char *server2,
                     const char *socks, const char *user, const char *passenv,
                     const char *out_dir, const char *queue_dir, long limit, int conns)
{
    NNQueue *q;
    NNClient *cl[16];
    int ncl = 0, nservers = server2 ? 2 : 1, e, k, status = 0, par2, asked = 0;
    size_t i;
    NNQFileInfo fi;
    uint32_t tick = 0;
    q = nn_q_open(queue_dir ? queue_dir : out_dir, out_dir, nservers, &e);
    if (!q) {
        fprintf(stderr, "cannot open the queue: %s\n", nk_strerror(e));
        nn_free(first);
        return 1;
    }
    if (!strcmp(argv[0], "nzb")) {
        NNNzb nzb;
        if (argc < 2 || nn_nzb_load(&nzb, argv[1]) != NK_OK) {
            fprintf(stderr, "cannot read the NZB\n");
            nn_free(first);
            nn_q_close(q);
            return 1;
        }
        printf("added %d files\n", nn_q_add_nzb(q, &nzb));
        nn_nzb_free(&nzb);
    }
    cl[ncl] = first;
    nn_q_attach(q, cl[ncl++], 0, 2);
    for (k = 1; k < conns && ncl < 16; k++)
        if ((cl[ncl] = open_server(nn_cli_server, nn_cli_port, socks, user, passenv)) != 0)
            nn_q_attach(q, cl[ncl++], 0, 2);
    if (server2) {
        char host[256];
        const char *colon = strrchr(server2, ':');
        size_t hl = colon ? (size_t)(colon - server2) : strlen(server2);
        if (hl >= sizeof(host)) hl = sizeof(host) - 1;
        memcpy(host, server2, hl);
        host[hl] = 0;
        for (k = 0; k < conns && ncl < 16; k++)
            if ((cl[ncl] = open_server(host, colon ? atoi(colon + 1) : 119, socks, user, passenv)) != 0)
                nn_q_attach(q, cl[ncl++], 1, 2);
    }
    if (limit >= 0)
        nn_q_limit(q, limit);
again:
    while (nn_q_step(q)) {
        struct pollfd p[16];
        for (k = 0; k < ncl; k++) {
            p[k].fd = nk_conn_fd(nn_conn(cl[k]));
            p[k].events = POLLIN | (nk_conn_outq(nn_conn(cl[k])) ? POLLOUT : 0);
        }
        if (!ncl)
            break;
        poll(p, (nfds_t)ncl, 1000);
        tick += 10;
        for (k = 0; k < ncl; k++)
            if (nn_pump(cl[k], tick) != NK_OK) {
                nn_q_detach(q, cl[k]);
                nn_free(cl[k]);
                cl[k--] = cl[--ncl];
            }
    }
    par2 = ncl && limit < 0 ? queue_par2(q, out_dir, &asked) : -1;
    if (par2 == 1)
        goto again;
    for (i = 0; nn_q_info(q, i, &fi); i++) {
        printf("%s\t%s\t%lu/%lu\t%s\n", fi.name, state_name(fi.state), (unsigned long)fi.done,
               (unsigned long)fi.nsegs, fi.path ? fi.path : "-");
        if (fi.state == NN_QF_BADCRC || fi.state == NN_QF_INCOMPLETE)
            status = 3;
    }
    if (par2 >= 0)
        status = par2;
    nn_q_compact(q);
    nn_q_close(q);
    for (k = 0; k < ncl; k++)
        nn_free(cl[k]);
    return status;
}

static void usage(void)
{
    fputs("usage: gfopennews-cli [--server H] [--port N] [--user U] [--pass-env VAR]\n"
          "       [--socks H:P] [--newsrc FILE] [--cache DIR]\n"
          "       [--out DIR] [--queue DIR] [--server2 H:P] [--conns N] [--limit N]\n"
          "       groups | headers GROUP [LAST] | article GROUP N | unread | mark GROUP LO[-HI]\n"
          "       | nzb FILE.nzb | resume\n"
          "       gfopennews-cli par2 verify|repair FILE.par2\n",
          stderr);
}

int main(int argc, char **argv)
{
    const char *server = "localhost", *user = 0, *passenv = 0, *socks = 0;
    const char *newsrc = 0, *cache = ".", *server2 = 0, *out_dir = ".", *queue_dir = 0;
    int port = 119, i, rc_dirty = 0, status = 0, conns = 2;
    long limit = -1;
    NNNewsrc rc;
    NNClient *c;
    Reply r;
    char line[600];

    for (i = 1; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (i + 1 >= argc) { usage(); return 2; }
        if (!strcmp(argv[i], "--server")) server = argv[++i];
        else if (!strcmp(argv[i], "--port")) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--user")) user = argv[++i];
        else if (!strcmp(argv[i], "--pass-env")) passenv = argv[++i];
        else if (!strcmp(argv[i], "--socks")) socks = argv[++i];
        else if (!strcmp(argv[i], "--newsrc")) newsrc = argv[++i];
        else if (!strcmp(argv[i], "--cache")) cache = argv[++i];
        else if (!strcmp(argv[i], "--server2")) server2 = argv[++i];
        else if (!strcmp(argv[i], "--out")) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--queue")) queue_dir = argv[++i];
        else if (!strcmp(argv[i], "--limit")) limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--conns")) conns = atoi(argv[++i]);
        else { usage(); return 2; }
    }
    if (i >= argc) { usage(); return 2; }
    nn_cli_server = server;
    nn_cli_port = port;
    nn_rc_init(&rc);
    if (newsrc && nn_rc_load(&rc, newsrc) != NK_OK) {
        fprintf(stderr, "cannot read %s\n", newsrc);
        return 1;
    }
    if (!strcmp(argv[i], "par2")) {
        nn_rc_free(&rc);
        if (i + 2 >= argc || (strcmp(argv[i + 1], "verify") && strcmp(argv[i + 1], "repair"))) {
            usage();
            return 2;
        }
        return run_par2(argv[i + 1], argv[i + 2]);
    }
    /* "mark" needs no server */
    if (!strcmp(argv[i], "mark")) {
        unsigned long lo, hi;
        NNGroupRc *g;
        char *e;
        if (!newsrc || i + 2 >= argc) { usage(); return 2; }
        lo = strtoul(argv[i + 2], &e, 10);
        hi = *e == '-' ? strtoul(e + 1, 0, 10) : lo;
        g = nn_rc_group(&rc, argv[i + 1], 1);
        if (!g || nn_rc_mark(g, (uint32_t)lo, (uint32_t)hi, 1) != NK_OK) {
            fprintf(stderr, "cannot mark\n");
            return 1;
        }
        g->subscribed = 1;
        if (nn_rc_save(&rc, newsrc) != NK_OK) {
            fprintf(stderr, "cannot save %s\n", newsrc);
            return 1;
        }
        nn_rc_format(g, line, sizeof(line));
        puts(line);
        nn_rc_free(&rc);
        return 0;
    }

    c = open_server(server, port, socks, user, passenv);
    if (!c) {
        nn_rc_free(&rc);
        return 1;
    }
    if (!strcmp(argv[i], "nzb") || !strcmp(argv[i], "resume")) {
        status = run_queue(c, argc - i, argv + i, server2, socks, user, passenv,
                           out_dir, queue_dir, limit, conns);
        nn_rc_free(&rc);
        return status;
    }

    if (!strcmp(argv[i], "groups") || !strcmp(argv[i], "unread")) {
        GroupsCtx g;
        int unread = !strcmp(argv[i], "unread");
        if (unread && !newsrc) { usage(); goto fail; }
        g.rc = unread ? &rc : 0;
        if (cmd(c, "LIST ACTIVE", NN_MULTI, print_active, &g, &r) != NK_OK || r.code != 215) {
            fprintf(stderr, "%d %s\n", r.code, r.text);
            goto fail;
        }
        cmd(c, "QUIT", NN_SINGLE, 0, 0, &r);
    } else if (!strcmp(argv[i], "headers") && i + 1 < argc) {
        uint32_t cnt, lo, hi, last = i + 2 < argc ? (uint32_t)atoi(argv[i + 2]) : 100;
        char name[256], path[1024];
        HeadCtx h;
        int e;
        snprintf(line, sizeof(line), "GROUP %s", argv[i + 1]);
        if (cmd(c, line, NN_SINGLE, 0, 0, &r) != NK_OK) goto fail;
        if (r.code != 211 || !nn_parse_group(r.text, strlen(r.text), &cnt, &lo, &hi, name, sizeof(name))) {
            fprintf(stderr, "%d %s\n", r.code, r.text);
            goto fail;
        }
        nn_ov_path(cache, name, path, sizeof(path));
        h.cache = nn_ov_open(path, &e);
        h.n = 0;
        if (h.cache && nn_ov_high(h.cache) >= lo)
            lo = nn_ov_high(h.cache) + 1;       /* only what the cache lacks */
        if (hi >= last && hi - last + 1 > lo)
            lo = hi - last + 1;
        if (lo <= hi) {
            snprintf(line, sizeof(line), "OVER %lu-%lu", (unsigned long)lo, (unsigned long)hi);
            if (cmd(c, line, NN_MULTI, take_over, &h, &r) != NK_OK || r.code != 224) goto fail;
        }
        if (h.cache) {
            nn_ov_flush(h.cache);
            nn_ov_close(h.cache);
        }
        nn_ov_read(path, hi >= last ? hi - last + 1 : 0, hi, print_over, 0);
        cmd(c, "QUIT", NN_SINGLE, 0, 0, &r);
    } else if (!strcmp(argv[i], "article") && i + 2 < argc) {
        snprintf(line, sizeof(line), "GROUP %s", argv[i + 1]);
        if (cmd(c, line, NN_SINGLE, 0, 0, &r) != NK_OK || r.code != 211) {
            fprintf(stderr, "%d %s\n", r.code, r.text);
            goto fail;
        }
        snprintf(line, sizeof(line), "ARTICLE %s", argv[i + 2]);
        if (cmd(c, line, NN_MULTI, print_line, 0, &r) != NK_OK) goto fail;
        if (r.code != 220) {
            fprintf(stderr, "%d %s\n", r.code, r.text);
            status = 1;
        } else if (newsrc) {
            NNGroupRc *g = nn_rc_group(&rc, argv[i + 1], 1);
            uint32_t n = (uint32_t)atoi(argv[i + 2]);
            if (g && nn_rc_mark(g, n, n, 1) == NK_OK)
                rc_dirty = 1;
        }
        cmd(c, "QUIT", NN_SINGLE, 0, 0, &r);
    } else {
        usage();
        goto fail;
    }
    if (rc_dirty && nn_rc_save(&rc, newsrc) != NK_OK)
        status = 1;
    nn_free(c);
    nn_rc_free(&rc);
    return status;
fail:
    nn_free(c);
    nn_rc_free(&rc);
    return 1;
}
