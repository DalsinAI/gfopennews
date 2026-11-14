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
 *          --tls (port 563 by default) --insecure (no certificate check)
 *          --allow-plain-auth (AUTHINFO without TLS: refused otherwise)
 *          --newsrc FILE --cache DIR --out DIR --queue DIR --server2 HOST:PORT
 *          --conns N (per server) --limit N (stop after N segments)
 *          --unpack "COMMAND %d %n" (run when a queue run ends with every file
 *          done: %d the output directory, %n the NZB's name, both quoted)
 *          --score FILE (headers: a score column, killed articles left out)
 * The host build (plat_posix.c) has no TLS and takes the password from the
 * variable --pass-env names. The Amiga build (amiga/plat_amiga.c) uses
 * bsdsocket.library and gfopentls.library, and takes the password from
 * gfopenkeys.library (entry nntp/HOST/USER); --pass-env does nothing there.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennews/nntp.h"
#include "gfopennews/newsrc.h"
#include "gfopennews/overview.h"
#include "gfopennews/queue.h"
#include "gfopennews/par2.h"
#include "gfopennews/path.h"
#include "gfopennews/unpack.h"
#include "gfopennews/score.h"
#include "plat.h"

static NNPlatOpts plat;
static int stopped;

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
        NKConn *conn = nn_conn(c);
        int r;
        if (nn_plat_wait(&conn, 1, 1000)) {
            stopped = 1;
            return NK_EIO;
        }
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
typedef struct { const NNScore *score; const char *group; } OverCtx;
static int print_over(void *u, const NNOver *ov)
{
    OverCtx *o = (OverCtx *)u;
    if (o && o->score) {
        long sc = nn_score_of(o->score, o->group, ov);
        if (nn_score_killed(sc))
            return 0;
        printf("%lu\t%ld\t%s\t%s\n", (unsigned long)ov->number, sc, ov->subject, ov->from);
    } else
        printf("%lu\t%s\t%s\n", (unsigned long)ov->number, ov->subject, ov->from);
    return 0;
}

/* Reads the kill and score file (at most 1 MB). NULL with the reason printed. */
static NNScore *load_score(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *t;
    size_t n;
    unsigned bad = 0;
    NNScore *s;
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return 0; }
    t = (char *)malloc((1u << 20) + 1);
    n = t ? fread(t, 1, (1u << 20) + 1, f) : 0;
    fclose(f);
    if (!t || n > (1u << 20)) {
        fprintf(stderr, "%s: too big\n", path);
        free(t);
        return 0;
    }
    s = nn_score_parse(t, n, &bad);
    free(t);
    if (!s) {
        if (bad) fprintf(stderr, "%s: line %u is not a rule\n", path, bad);
        else fprintf(stderr, "%s: too many rules (%u at most)\n", path, (unsigned)NN_SCORE_RULES);
    }
    return s;
}


/* a store the compiler can't drop, for buffers that held a password */
static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

static void usage(void);
static const char *nn_cli_server;
static int nn_cli_port;

static NNClient *open_server(const char *server, int port, const char *user)
{
    NKTransport t;
    NKConn *conn;
    NNClient *c;
    Reply r;
    char line[600], pw[512];
    if (nn_plat_connect(&plat, server, port, &t) != NK_OK)
        return 0;
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
        snprintf(line, sizeof(line), "AUTHINFO USER %s", user);
        if (cmd(c, line, NN_SINGLE, 0, 0, &r) != NK_OK) goto fail;
        if (r.code == 381) {
            int k;
            if (!nn_plat_password(&plat, server, user, pw, sizeof(pw))) {
                fprintf(stderr, nn_plat_has_tls() ? "no password for nntp/%s/%s\n"
                                                  : "password needed (--pass-env)\n", server, user);
                goto fail;
            }
            snprintf(line, sizeof(line), "AUTHINFO PASS %s", pw);
            wipe(pw, sizeof(pw));
            k = cmd(c, line, NN_SINGLE, 0, 0, &r);
            wipe(line, sizeof(line));
            if (k != NK_OK) goto fail;
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

typedef struct { NNPar2 *p; const char *dir, *base; size_t bl; } VolScan;
static void take_volume(void *u, const char *name)
{
    VolScan *v = (VolScan *)u;
    size_t nl = strlen(name);
    char path[1024];
    if (nl > v->bl + 6 && !strncmp(name, v->base, v->bl) && name[v->bl] == '.' &&
        !strcmp(name + nl - 5, ".par2") && strcmp(name + v->bl, ".par2") &&
        nn_path_join(v->dir, name, path, sizeof(path)) == NK_OK)
        nn_par2_load(v->p, path);
}

/* Loads FILE.par2 and every FILE.*.par2 beside it. */
static int par2_load_set(NNPar2 *p, const char *index, char *dir, size_t dirsz)
{
    VolScan v;
    size_t at;
    int r;
    memset(p, 0, sizeof(*p));
    if (nn_path_dir(index, dir, dirsz, &at) != NK_OK)
        return NK_EARG;
    if ((r = nn_par2_load(p, index)) != NK_OK)
        return r;
    v.p = p;
    v.dir = dir;
    v.base = index + at;
    v.bl = strlen(v.base);
    if (v.bl > 5 && !strcmp(v.base + v.bl - 5, ".par2"))
        v.bl -= 5;
    nn_plat_list(dir, take_volume, &v);
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
                     const char *user, const char *out_dir, const char *queue_dir, long limit, int conns, const char *unpack)
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
        if ((cl[ncl] = open_server(nn_cli_server, nn_cli_port, user)) != 0)
            nn_q_attach(q, cl[ncl++], 0, 2);
    if (server2) {
        char host[256];
        const char *colon = strrchr(server2, ':');
        size_t hl = colon ? (size_t)(colon - server2) : strlen(server2);
        if (hl >= sizeof(host)) hl = sizeof(host) - 1;
        memcpy(host, server2, hl);
        host[hl] = 0;
        for (k = 0; k < conns && ncl < 16; k++)
            if ((cl[ncl] = open_server(host, colon ? atoi(colon + 1) : plat.tls ? 563 : 119, user)) != 0)
                nn_q_attach(q, cl[ncl++], 1, 2);
    }
    if (limit >= 0)
        nn_q_limit(q, limit);
again:
    while (nn_q_step(q)) {
        NKConn *cn[16];
        for (k = 0; k < ncl; k++)
            cn[k] = nn_conn(cl[k]);
        if (!ncl)
            break;
        if (nn_plat_wait(cn, ncl, 1000)) {
            stopped = 1;        /* the queue is on disk: "resume" carries on */
            break;
        }
        tick += 10;
        for (k = 0; k < ncl; k++)
            if (nn_pump(cl[k], tick) != NK_OK) {
                nn_q_detach(q, cl[k]);
                nn_free(cl[k]);
                cl[k--] = cl[--ncl];
            }
    }
    par2 = ncl && limit < 0 && !stopped ? queue_par2(q, out_dir, &asked) : -1;
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
    /* the external unpacker, only when every file is done */
    if (unpack && status == 0 && !stopped && limit < 0) {
        char name[256], line[2048];
        int r;
        nn_unpack_name(!strcmp(argv[0], "nzb") && argc > 1 ? argv[1] : "", name, sizeof(name));
        if (nn_unpack_command(unpack, out_dir, name, NN_PLAT_SHELL, line, sizeof(line)) != NK_OK) {
            fputs("unpack: the command can't be built (only %d, %n and %% may follow a %,"
                  " and no control characters)\n", stderr);
            status = 4;
        } else if ((r = nn_plat_run(line)) != 0) {
            printf("unpack: failed (%d)\n", r);
            status = 4;
        } else
            printf("unpack: done\n");
    }
    nn_q_compact(q);
    nn_q_close(q);
    for (k = 0; k < ncl; k++)
        nn_free(cl[k]);
    return status;
}

static void usage(void)
{
    fputs("usage: gfopennews-cli [--server H] [--port N] [--user U] [--pass-env VAR]\n"
          "       [--socks H:P] [--tls] [--insecure] [--allow-plain-auth]\n"
          "       [--newsrc FILE] [--cache DIR]\n"
          "       [--out DIR] [--queue DIR] [--server2 H:P] [--conns N] [--limit N]\n"
          "       [--score FILE] groups | headers GROUP [LAST] | article GROUP N | unread | mark GROUP LO[-HI]\n"
          "       | nzb FILE.nzb | resume   ([--unpack \"COMMAND %d %n\"])\n"
          "       gfopennews-cli par2 verify|repair FILE.par2\n",
          stderr);
}

int main(int argc, char **argv)
{
    const char *server = "localhost", *user = 0;
    const char *newsrc = 0, *cache = NN_PLAT_CURDIR, *server2 = 0, *out_dir = NN_PLAT_CURDIR, *queue_dir = 0;
    int port = 0, i, rc_dirty = 0, status = 0, conns = 2;
    long limit = -1;
    int allow_plain = 0;
    const char *unpack = 0;
    const char *score_file = 0;
    NNNewsrc rc;
    NNClient *c;
    Reply r;
    char line[600];

    for (i = 1; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (!strcmp(argv[i], "--allow-plain-auth")) { allow_plain = 1; continue; }
        if (!strcmp(argv[i], "--tls")) { plat.tls = 1; continue; }
        if (!strcmp(argv[i], "--insecure")) { plat.insecure = 1; continue; }
        if (i + 1 >= argc) { usage(); return 2; }
        if (!strcmp(argv[i], "--server")) server = argv[++i];
        else if (!strcmp(argv[i], "--port")) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--user")) user = argv[++i];
        else if (!strcmp(argv[i], "--pass-env")) plat.passenv = argv[++i];
        else if (!strcmp(argv[i], "--socks")) plat.socks = argv[++i];
        else if (!strcmp(argv[i], "--newsrc")) newsrc = argv[++i];
        else if (!strcmp(argv[i], "--cache")) cache = argv[++i];
        else if (!strcmp(argv[i], "--server2")) server2 = argv[++i];
        else if (!strcmp(argv[i], "--out")) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--queue")) queue_dir = argv[++i];
        else if (!strcmp(argv[i], "--limit")) limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--conns")) conns = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--unpack")) unpack = argv[++i];
        else if (!strcmp(argv[i], "--score")) score_file = argv[++i];
        else { usage(); return 2; }
    }
    if (i >= argc) { usage(); return 2; }
    /* AUTHINFO without TLS would send the password in clear: only with
     * --allow-plain-auth (Franklin's note on #1). --insecure skips the
     * certificate check, so a man in the middle could read it too: it
     * counts as plain here. This runs before anything asks gfopenkeys. */
    if (user && (!plat.tls || plat.insecure) && !allow_plain &&
        strcmp(argv[i], "par2") && strcmp(argv[i], "mark")) {
        fputs(plat.tls ? "not sending a password to a server whose certificate isn't checked "
                         "(drop --insecure, or --allow-plain-auth to do it anyway)\n"
                       : "not sending a password over a connection without TLS "
                         "(--tls, or --allow-plain-auth to do it anyway)\n", stderr);
        return 2;
    }
    if (!port)
        port = plat.tls ? 563 : 119;
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

    if (!nn_plat_init(&plat)) {
        nn_rc_free(&rc);
        return 1;
    }
    c = open_server(server, port, user);
    if (!c) {
        nn_plat_exit();
        nn_rc_free(&rc);
        return 1;
    }
    if (!strcmp(argv[i], "nzb") || !strcmp(argv[i], "resume")) {
        status = run_queue(c, argc - i, argv + i, server2, user,
                           out_dir, queue_dir, limit, conns, unpack);
        if (stopped)
            fputs("stopped: \"resume\" carries on\n", stderr);
        nn_plat_exit();
        nn_rc_free(&rc);
        return stopped ? 1 : status;
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
        {
            OverCtx oc;
            oc.group = name;
            oc.score = 0;
            if (score_file && !(oc.score = load_score(score_file))) goto fail;
            nn_ov_read(path, hi >= last ? hi - last + 1 : 0, hi, print_over, &oc);
            nn_score_free((NNScore *)oc.score);
        }
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
    nn_plat_exit();
    nn_rc_free(&rc);
    return status;
fail:
    nn_free(c);
    nn_plat_exit();
    nn_rc_free(&rc);
    return 1;
}
