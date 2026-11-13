/* GFOpenNews download queue: NZB files and their segments, kept in a
 * gfopennetkit journal (queue.db) so a restart asks only for the segments
 * still missing; BODY <message-id> fetches over any number of connections on
 * any number of servers, with a 430/423 (or a damaged part) tried on the next
 * server; yEnc decoded straight into each file's part file.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_QUEUE_H
#define GFOPENNEWS_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include "gfopennews/nntp.h"
#include "gfopennews/nzb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NN_Q_MAXSERVERS 8

enum {                       /* file states */
    NN_QF_QUEUED = 0,
    NN_QF_PAUSED,
    NN_QF_DONE,              /* complete, CRC good, renamed */
    NN_QF_BADCRC,            /* complete but the whole-file CRC is wrong */
    NN_QF_INCOMPLETE         /* every server tried: some segments missing */
};

typedef struct NNQueue NNQueue;

typedef struct NNQFileInfo {
    uint32_t    id;
    const char *name;
    int         state;
    uint32_t    nsegs, done, failed;
    uint64_t    bytes;           /* from the NZB */
    const char *path;            /* final file once DONE */
} NNQFileInfo;

/* Opens (or creates) the queue journal in `dir`; files are written to
 * `out_dir`. nservers: how many servers the caller will attach (in order of
 * preference); a segment fails only once all of them said no. */
NNQueue *nn_q_open(const char *dir, const char *out_dir, int nservers, int *err);
void     nn_q_close(NNQueue *q);
/* Adds every file of an NZB (par2 files last). Answers files added. */
int      nn_q_add_nzb(NNQueue *q, const NNNzb *nzb);
size_t   nn_q_files(const NNQueue *q);
int      nn_q_info(const NNQueue *q, size_t index, NNQFileInfo *out);
int      nn_q_pause(NNQueue *q, uint32_t id, int pause);
int      nn_q_remove(NNQueue *q, uint32_t id);

/* Attaches a logged-in connection to server `server` (0-based). `depth` is
 * how many BODY commands may be in flight on it (pipelining). The queue
 * does not own the client. */
int      nn_q_attach(NNQueue *q, NNClient *c, int server, int depth);
void     nn_q_detach(NNQueue *q, NNClient *c);
/* Hands out work to idle connections. Call after every nn_pump. Answers 1
 * while anything is queued or in flight, 0 when all is finished. */
int      nn_q_step(NNQueue *q);
/* Stops handing out new work after `n` more segments (tests, "pause all"). */
void     nn_q_limit(NNQueue *q, long n);
/* Rewrites the journal with the live state only. */
int      nn_q_compact(NNQueue *q);

#ifdef __cplusplus
}
#endif
#endif
