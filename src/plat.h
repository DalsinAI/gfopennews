/* What the CLI needs from the system: the host build (plat_posix.c, POSIX
 * sockets, password from an environment variable, no TLS) and the Amiga
 * build (amiga/plat_amiga.c, bsdsocket, gfopentls, password from
 * gfopenkeys). MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_PLAT_H
#define GFOPENNEWS_PLAT_H
#include <stddef.h>
#include "gfopennetkit/netkit.h"

typedef struct {
    int         tls;        /* TLS on every connection */
    int         insecure;   /* no certificate check */
    const char *socks;      /* HOST:PORT of a SOCKS5 proxy, or 0 */
    const char *passenv;    /* host build: the variable that holds the password */
} NNPlatOpts;

/* the current directory, as a default for --out */
extern const char NN_PLAT_CURDIR[];
/* 1 when this build can do TLS */
int  nn_plat_has_tls(void);
/* libraries opened, once; 0 and a message on stderr when something is missing */
int  nn_plat_init(const NNPlatOpts *o);
void nn_plat_exit(void);
/* a connected transport (through the proxy, TLS done); NK_OK or a message on stderr */
int  nn_plat_connect(const NNPlatOpts *o, const char *host, int port, NKTransport *t);
/* waits up to `ms` until one of the connections can go on; 1 when the user
 * asked to stop (Ctrl-C) */
int  nn_plat_wait(NKConn *const *conns, int n, int ms);
/* the password for nntp/<host>/<user> into buf; 1 found, 0 not */
int  nn_plat_password(const NNPlatOpts *o, const char *host, const char *user,
                      char *buf, size_t cap);
/* the shell the unpack hook's line is quoted for (NN_SHELL_POSIX or
 * NN_SHELL_AMIGADOS, unpack.h) */
extern const int NN_PLAT_SHELL;
/* runs a command line through the system's shell; its return code, or -1
 * when it could not be started */
int  nn_plat_run(const char *line);
/* calls fn for each name in dir ("" is the current directory) */
int  nn_plat_list(const char *dir, void (*fn)(void *u, const char *name), void *u);

#endif
