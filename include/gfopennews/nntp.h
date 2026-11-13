/* GFOpenNews NNTP core (RFC 3977, AUTHINFO RFC 4643), over a gfopennetkit
 * connection. Non-blocking and pipelined: commands are queued and sent at
 * once, replies are matched in order.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_NNTP_H
#define GFOPENNEWS_NNTP_H

#include <stddef.h>
#include <stdint.h>
#include "gfopennetkit/netkit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NNClient NNClient;

/* One line of a multi-line block, dot-unstuffed, without CR LF. Answer 0 to
 * go on, anything else to stop the delivery (the block is still read off
 * the wire). */
typedef int (*NNLineFn)(void *user, const char *line, size_t len);
/* The reply: code and the text after it. err is NK_OK, NK_ETOOLONG (a block
 * line over the cap was dropped), or a connection error when the link went
 * down before the reply (code 0 then). */
typedef void (*NNDoneFn)(void *user, int code, const char *text, size_t len, int err);

#define NN_SINGLE 0
#define NN_MULTI  1   /* a block follows a success (1xx/2xx/3xx) reply */

/* Takes the connection over (freed with the client). The server greeting
 * comes to greet_fn as the first reply. */
NNClient *nn_new(NKConn *conn, NNDoneFn greet_fn, void *greet_user);
void      nn_free(NNClient *c);
/* Queues a command line (no CR LF). NK_EARG on CR/LF inside. */
int       nn_cmd(NNClient *c, const char *line, int multi,
                 NNLineFn line_fn, void *line_user, NNDoneFn done_fn, void *done_user);
/* Sends a POST/IHAVE body after the 340/335 reply: dot-stuffs each line and
 * ends with ".". `text` is the article, LF or CR LF lines. The 240/235 reply
 * comes to done_fn. */
int       nn_send_article(NNClient *c, const char *text, size_t len,
                          NNDoneFn done_fn, void *user);
/* Moves bytes and calls the callbacks. NK_OK, or the connection's error
 * (every waiting command is then answered with code 0 and that error). */
int       nn_pump(NNClient *c, uint32_t now_ms);
size_t    nn_waiting(const NNClient *c);   /* commands without a reply yet */
NKConn   *nn_conn(NNClient *c);

/* ---- reply parsers ------------------------------------------------------- */
/* "211 count low high group" text part (after the code). */
int nn_parse_group(const char *text, size_t len, uint32_t *count, uint32_t *low,
                   uint32_t *high, char *name, size_t name_cap);

/* LIST ACTIVE line: "group high low status". */
typedef struct NNActive {
    char     name[256];
    uint32_t high, low;
    char     status;      /* y, n, m, x, j, = */
} NNActive;
int nn_parse_active(const char *line, size_t len, NNActive *out);

/* OVER/XOVER line (RFC 3977 8.3): fields point into a copy in `buf`. */
typedef struct NNOver {
    uint32_t    number;
    const char *subject, *from, *date, *msgid, *references;
    uint32_t    bytes, lines;
    const char *xref;     /* "Xref:" extra field, or "" */
} NNOver;
int nn_parse_over(const char *line, size_t len, NNOver *out, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
