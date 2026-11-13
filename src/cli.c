/* GFOpenNews CLI: the headless reader (design step 2). Lists groups, fetches
 * headers into the overview cache and prints articles.
 *
 *   gfopennews-cli [options] groups
 *   gfopennews-cli [options] headers GROUP [LAST]      (default: the last 100)
 *   gfopennews-cli [options] article GROUP NUMBER
 *   gfopennews-cli [options] unread                     (needs --newsrc)
 *   gfopennews-cli [options] mark GROUP LOW[-HIGH]      (needs --newsrc)
 * options: --server HOST --port N --user U --pass-env VAR --socks HOST:PORT
 *          --newsrc FILE --cache DIR
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

static void usage(void)
{
    fputs("usage: gfopennews-cli [--server H] [--port N] [--user U] [--pass-env VAR]\n"
          "       [--socks H:P] [--newsrc FILE] [--cache DIR]\n"
          "       groups | headers GROUP [LAST] | article GROUP N | unread | mark GROUP LO[-HI]\n",
          stderr);
}

int main(int argc, char **argv)
{
    const char *server = "localhost", *user = 0, *passenv = 0, *socks = 0;
    const char *newsrc = 0, *cache = ".";
    int port = 119, i, rc_dirty = 0, status = 0;
    NNNewsrc rc;
    NKTransport t;
    NKConn *conn;
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
        else { usage(); return 2; }
    }
    if (i >= argc) { usage(); return 2; }
    nn_rc_init(&rc);
    if (newsrc && nn_rc_load(&rc, newsrc) != NK_OK) {
        fprintf(stderr, "cannot read %s\n", newsrc);
        return 1;
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

    if (socks) {
        char host[256];
        const char *colon = strrchr(socks, ':');
        NKSocks5 s;
        int k;
        if (!colon || (size_t)(colon - socks) >= sizeof(host)) { usage(); return 2; }
        memcpy(host, socks, (size_t)(colon - socks));
        host[colon - socks] = 0;
        if (nk_posix_tcp(host, (uint16_t)atoi(colon + 1), &t) != NK_OK) {
            fprintf(stderr, "cannot reach proxy %s\n", socks);
            return 1;
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
            return 1;
        }
    } else if (nk_posix_tcp(server, (uint16_t)port, &t) != NK_OK) {
        fprintf(stderr, "cannot reach %s:%d\n", server, port);
        return 1;
    }
    conn = nk_conn_new(0, t, 0);
    memset(&r, 0, sizeof(r));
    c = nn_new(conn, on_done, &r);
    if (!c || wait_all(c) != NK_OK || r.code / 100 != 2) {
        fprintf(stderr, "no greeting: %d %s\n", r.code, r.text);
        return 1;
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
