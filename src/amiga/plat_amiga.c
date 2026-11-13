/* The CLI's system calls on AmigaOS 3.x: bsdsocket.library, TLS through
 * gfopentls.library (--tls, the certificate checked against the system
 * trust store unless --insecure), the AUTHINFO password from the
 * gfopenkeys.library entry nntp/<server>/<user> (never on the command
 * line), Ctrl-C to stop, directories read with Lock/Examine/ExNext.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <libraries/gfopentls.h>
#include <proto/gfopentls.h>
#ifdef HAVE_GFOPENKEYS
#include <libraries/gfopenkeys.h>
#include <proto/gfopenkeys.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit_amiga.h"
#include "../plat.h"

static const char VERSTAG[] = "$VER: GFOpenNews 0.1 (13.11.2026)";

struct Library *SocketBase;
struct Library *OpenTLSBase;
#ifdef HAVE_GFOPENKEYS
struct Library *GFOpenKeysBase;
#endif
static struct OTContext *ctx;

const char NN_PLAT_CURDIR[] = "";

int nn_plat_has_tls(void) { return 1; }

int nn_plat_init(const NNPlatOpts *o)
{
    (void)VERSTAG;
    if (!(SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4))) {
        fputs("no TCP/IP stack (bsdsocket.library)\n", stderr);
        return 0;
    }
    if (o->tls) {
        struct OTContextConfig cc;
        LONG err = 0;
        if (!(OpenTLSBase = OpenLibrary((CONST_STRPTR)OPENTLSLIB_NAME, OPENTLSLIB_VERSION))) {
            fprintf(stderr, "--tls needs %s\n", OPENTLSLIB_NAME);
            nn_plat_exit();
            return 0;
        }
        memset(&cc, 0, sizeof(cc));
        cc.Size = sizeof(cc);
        if (!(ctx = OT_NewContext(&cc, &err))) {
            fprintf(stderr, "TLS: %s\n", (const char *)OT_ErrorString(err));
            nn_plat_exit();
            return 0;
        }
    }
#ifdef HAVE_GFOPENKEYS
    GFOpenKeysBase = OpenLibrary((CONST_STRPTR)GFOPENKEYS_NAME, GFOPENKEYS_VERSION);
#endif
    return 1;
}

void nn_plat_exit(void)
{
    if (ctx) OT_FreeContext(ctx);
    ctx = 0;
#ifdef HAVE_GFOPENKEYS
    if (GFOpenKeysBase) CloseLibrary(GFOpenKeysBase);
    GFOpenKeysBase = 0;
#endif
    if (OpenTLSBase) CloseLibrary(OpenTLSBase);
    OpenTLSBase = 0;
    if (SocketBase) CloseLibrary(SocketBase);
    SocketBase = 0;
}

/* waits for the socket, `ms` at most; 1 on Ctrl-C */
static int wait_fds(const int *fd, const int *wr, int n, long ms)
{
    fd_set r, w;
    struct timeval tv;
    ULONG sigs = SIGBREAKF_CTRL_C;
    int k, top = -1;
    FD_ZERO(&r);
    FD_ZERO(&w);
    for (k = 0; k < n; k++) {
        if (fd[k] < 0) continue;
        FD_SET(fd[k], &r);
        if (wr[k]) FD_SET(fd[k], &w);
        if (fd[k] > top) top = fd[k];
    }
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    WaitSelect(top + 1, &r, &w, 0, &tv, &sigs);
    return (sigs & SIGBREAKF_CTRL_C) != 0;
}

int nn_plat_wait(NKConn *const *conns, int n, int ms)
{
    int fd[16], wr[16], k;
    if (n > 16) n = 16;
    for (k = 0; k < n; k++) {
        fd[k] = nk_conn_fd(conns[k]);
        wr[k] = nk_conn_outq(conns[k]) != 0;
    }
    return wait_fds(fd, wr, n, ms);
}

int nn_plat_connect(const NNPlatOpts *o, const char *host, int port, NKTransport *t)
{
    NKTransport tcp;
    int r, fd, no = 0;
    if (o->socks) {
        char ph[256];
        const char *colon = strrchr(o->socks, ':');
        size_t hl = colon ? (size_t)(colon - o->socks) : strlen(o->socks);
        NKSocks5 s;
        if (hl >= sizeof(ph)) hl = sizeof(ph) - 1;
        memcpy(ph, o->socks, hl);
        ph[hl] = 0;
        if (nk_amiga_tcp(SocketBase, ph, (uint16_t)(colon ? atoi(colon + 1) : 1080), &tcp) != NK_OK) {
            fprintf(stderr, "cannot reach proxy %s\n", o->socks);
            return NK_EIO;
        }
        fd = tcp.fd(tcp.ctx);
        nk_socks5_init(&s, host, (uint16_t)port, 0, 0);
        while ((r = nk_socks5_step(&s, &tcp)) == NK_WOULDBLOCK)
            if (wait_fds(&fd, &no, 1, 1000)) break;
        if (r != 1) {
            fprintf(stderr, "proxy: %s\n", nk_strerror(r));
            nk_transport_close(&tcp);
            return r < 0 ? r : NK_EIO;
        }
    } else if (nk_amiga_tcp(SocketBase, host, (uint16_t)port, &tcp) != NK_OK) {
        fprintf(stderr, "cannot reach %s:%d\n", host, port);
        return NK_EIO;
    }
    if (!o->tls) {
        *t = tcp;
        return NK_OK;
    }
    fd = tcp.fd(tcp.ctx);
    if ((r = nk_amiga_tls(OpenTLSBase, ctx, host, o->insecure ? OTV_NONE : OTV_FULL,
                          0, &tcp, t)) != NK_OK) {
        fprintf(stderr, "TLS: %s\n", nk_strerror(r));
        nk_transport_close(&tcp);
        return r;
    }
    while ((r = nk_tls_handshake(t)) == NK_WOULDBLOCK)
        if (wait_fds(&fd, &no, 1, 100)) break;
    if (r != NK_OK) {
        const char *why = 0;
        nk_tls_error(t, &why);
        fprintf(stderr, "TLS with %s: %s\n", host, why ? why : nk_strerror(r));
        nk_transport_close(t);
        return r == NK_WOULDBLOCK ? NK_EIO : r;
    }
    return NK_OK;
}

int nn_plat_password(const NNPlatOpts *o, const char *host, const char *user,
                     char *buf, size_t cap)
{
#ifdef HAVE_GFOPENKEYS
    ULONG len = (ULONG)cap - 1;
    char h[256];
    size_t i;
    LONG r;
    (void)o;
    if (!GFOpenKeysBase) {
        fprintf(stderr, "no %s: no password for nntp/%s/%s\n", GFOPENKEYS_NAME, host, user);
        return 0;
    }
    for (i = 0; host[i] && i < sizeof(h) - 1; i++)
        h[i] = (char)(host[i] >= 'A' && host[i] <= 'Z' ? host[i] + 32 : host[i]);
    h[i] = 0;
    if ((r = GK_Find((CONST_STRPTR)GKS_NNTP, (CONST_STRPTR)h, (CONST_STRPTR)user,
                     (UBYTE *)buf, &len, 0)) != GKE_OK) {
        if (r != GKE_NOTFOUND)
            fprintf(stderr, "GFOpenKeys: %s\n", (const char *)GK_ErrorString(r));
        memset(buf, 0, cap);
        return 0;
    }
    buf[len < cap ? len : cap - 1] = 0;
    return 1;
#else
    (void)o; (void)host; (void)user; (void)buf; (void)cap;
    return 0;
#endif
}

int nn_plat_list(const char *dir, void (*fn)(void *u, const char *name), void *u)
{
    BPTR lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib;
    if (!lock)
        return NK_EIO;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, 0);
    if (fib && Examine(lock, fib) && fib->fib_DirEntryType > 0)
        while (ExNext(lock, fib))
            if (fib->fib_DirEntryType < 0)
                fn(u, (const char *)fib->fib_FileName);
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return NK_OK;
}
