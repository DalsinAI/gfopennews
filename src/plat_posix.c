/* The CLI's system calls on the host: POSIX sockets, no TLS, the password
 * from an environment variable. MIT licence, (c) 2026 AmiCradle Team. */
#include <dirent.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"

const char NN_PLAT_CURDIR[] = ".";

int nn_plat_has_tls(void) { return 0; }

int nn_plat_init(const NNPlatOpts *o)
{
    if (o->tls) {
        fputs("this host build has no TLS\n", stderr);
        return 0;
    }
    return 1;
}

void nn_plat_exit(void) {}

int nn_plat_connect(const NNPlatOpts *o, const char *host, int port, NKTransport *t)
{
    if (o->socks) {
        char ph[256];
        const char *colon = strrchr(o->socks, ':');
        NKSocks5 s;
        int k;
        if (!colon || (size_t)(colon - o->socks) >= sizeof(ph)) {
            fprintf(stderr, "--socks wants HOST:PORT\n");
            return NK_EARG;
        }
        memcpy(ph, o->socks, (size_t)(colon - o->socks));
        ph[colon - o->socks] = 0;
        if (nk_posix_tcp(ph, (uint16_t)atoi(colon + 1), t) != NK_OK) {
            fprintf(stderr, "cannot reach proxy %s\n", o->socks);
            return NK_EIO;
        }
        nk_socks5_init(&s, host, (uint16_t)port, 0, 0);
        while ((k = nk_socks5_step(&s, t)) == NK_WOULDBLOCK) {
            struct pollfd p;
            p.fd = t->fd(t->ctx);
            p.events = POLLIN;
            poll(&p, 1, 1000);
        }
        if (k != 1) {
            fprintf(stderr, "proxy: %s\n", nk_strerror(k));
            nk_transport_close(t);
            return k < 0 ? k : NK_EIO;
        }
        return NK_OK;
    }
    if (nk_posix_tcp(host, (uint16_t)port, t) != NK_OK) {
        fprintf(stderr, "cannot reach %s:%d\n", host, port);
        return NK_EIO;
    }
    return NK_OK;
}

int nn_plat_wait(NKConn *const *conns, int n, int ms)
{
    struct pollfd p[16];
    int k;
    if (n > 16) n = 16;
    for (k = 0; k < n; k++) {
        p[k].fd = nk_conn_fd(conns[k]);
        p[k].events = POLLIN | (nk_conn_outq(conns[k]) ? POLLOUT : 0);
    }
    poll(p, (nfds_t)n, ms);
    return 0;
}

int nn_plat_password(const NNPlatOpts *o, const char *host, const char *user,
                     char *buf, size_t cap)
{
    const char *pw = o->passenv ? getenv(o->passenv) : 0;
    (void)host; (void)user;
    if (!pw || strlen(pw) >= cap)
        return 0;
    strcpy(buf, pw);
    return 1;
}

int nn_plat_list(const char *dir, void (*fn)(void *u, const char *name), void *u)
{
    DIR *d = opendir(dir[0] ? dir : ".");
    struct dirent *de;
    if (!d)
        return NK_EIO;
    while ((de = readdir(d)) != 0)
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
            fn(u, de->d_name);
    closedir(d);
    return NK_OK;
}
