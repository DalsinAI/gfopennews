/* GFOpenNews reader: the models behind the window's lists, kept apart from
 * drawing so the host tests reach them. Plain C99, no Amiga calls.
 *
 * - groups (nn_ug_*): the subscribed groups with their unread counts, in
 *   .newsrc order, narrowed by what is typed in the find field;
 * - threads (nn_ut_*): a group's headers threaded by References (JWZ's
 *   algorithm, in our own code), shown as rows a thread can be folded into;
 * - queue (nn_uq_*): the binaries download queue as rows with progress;
 * - posting (nn_post_*): a new article or a follow-up, checked and
 *   formatted for POST.
 *
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_UI_MODEL_H
#define GFOPENNEWS_UI_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include "gfopennews/nntp.h"
#include "gfopennews/newsrc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Groups ------------------------------------------------------------- */

typedef struct NNUiGroup {
    const char *name;
    uint32_t    unread;
    uint32_t    low, high;    /* the server's marks (0, 0: not in LIST ACTIVE) */
} NNUiGroup;

typedef struct NNUiGroups NNUiGroups;

NNUiGroups *nn_ug_new(void);
void        nn_ug_free(NNUiGroups *g);
/* The subscribed groups of rc, in its order, with unread counts from the
 * server's marks in act (a group missing from act shows 0 unread). The
 * filter is kept. NK_OK or NK_ENOMEM. */
int         nn_ug_set(NNUiGroups *g, const NNNewsrc *rc, const NNActive *act, size_t nact);
/* Counts again from rc, after articles were marked read. */
void        nn_ug_recount(NNUiGroups *g, const NNNewsrc *rc);
/* Shows only groups whose name holds text (any case); "" or NULL shows all.
 * Answers how many rows show. */
size_t      nn_ug_filter(NNUiGroups *g, const char *text);
size_t      nn_ug_rows(const NNUiGroups *g);
const NNUiGroup *nn_ug_row(const NNUiGroups *g, size_t row);
/* The row showing name, or -1. */
long        nn_ug_find(const NNUiGroups *g, const char *name);
/* "name" or "name (12)" when there is something unread. Answers the length
 * needed, as snprintf does. */
size_t      nn_ug_label(const NNUiGroups *g, size_t row, char *buf, size_t cap);

/* ---- Threads ------------------------------------------------------------ */

typedef struct NNUiRow {
    uint32_t    number;       /* article number; 0: a missing article that holds a thread together */
    int         depth;        /* 0 for a thread's first article */
    const char *subject, *from, *date, *msgid;
    uint32_t    bytes, lines;
    int         children;     /* how many articles answer this one directly */
    int         open;         /* its answers shown (a row without answers is open) */
    int         read;
    uint32_t    unread_below; /* unread answers anywhere under it */
} NNUiRow;

typedef struct NNUiThreads NNUiThreads;

NNUiThreads *nn_ut_new(void);
void         nn_ut_free(NNUiThreads *t);
/* Adds a header (its strings are copied). A message-id seen before is
 * skipped. NK_OK, or NK_ENOMEM. */
int          nn_ut_add(NNUiThreads *t, const NNOver *ov);
/* Threads what was added: each article under the one its References ends
 * with, missing articles kept as empty rows when they join two or more, no
 * loops; then a thread whose first article is missing a "Re:" root joined
 * to a thread of the same subject. Threads come in the order of their
 * lowest article number, answers by number. Every thread starts folded
 * when fold is 1, open when 0. NK_OK or NK_ENOMEM. */
int          nn_ut_build(NNUiThreads *t, int fold);
/* Read flags from the group's .newsrc entry (NULL: all unread). */
void         nn_ut_set_read(NNUiThreads *t, const NNGroupRc *g);
/* Rows shown: the threads with their open answers. */
size_t       nn_ut_rows(const NNUiThreads *t);
const NNUiRow *nn_ut_row(const NNUiThreads *t, size_t row);
/* Opens or folds the answers under a row. Answers 1 when it changed. */
int          nn_ut_open(NNUiThreads *t, size_t row, int open);
/* The row showing article number, opening the threads above it; -1 when it
 * is not there. */
long         nn_ut_show(NNUiThreads *t, uint32_t number);
/* The next unread article after row (row -1: from the top), opening its
 * thread; -1 when none is left. */
long         nn_ut_next_unread(NNUiThreads *t, long row);
/* The subject as the list shows it: two spaces a level, then "+ " for a
 * folded thread with answers, "- " for an open one, then the subject. A
 * missing article shows "(not on the server)". */
size_t       nn_ut_label(const NNUiThreads *t, size_t row, char *buf, size_t cap);
/* The subject without any "Re:" (any case, any number of them). */
const char  *nn_base_subject(const char *subject);

/* ---- The download queue ------------------------------------------------- */

/* A file of the queue as the panel shows it. The fields mirror the queue's
 * own file information (queue.h), so the queue fills them field for field. */
enum {
    NN_UQ_QUEUED = 0,
    NN_UQ_PAUSED,
    NN_UQ_DONE,
    NN_UQ_BADCRC,
    NN_UQ_INCOMPLETE
};

typedef struct NNUiQFile {
    uint32_t    id;
    const char *name;
    int         state;        /* NN_UQ_... */
    uint32_t    nsegs, done, failed;
    uint64_t    bytes;
} NNUiQFile;

/* The percentage done (segments), 0 to 100; 100 only when every segment is in. */
int          nn_uq_percent(const NNUiQFile *f);
/* "Queued", "Paused", "Done", "Damaged" or "Incomplete (3 missing)". */
size_t       nn_uq_state(const NNUiQFile *f, char *buf, size_t cap);
/* "512 bytes", "12.4 KB", "3.0 MB", "1.2 GB" (1024 a step). */
size_t       nn_uq_size(uint64_t bytes, char *buf, size_t cap);

/* ---- Posting ------------------------------------------------------------ */

#define NN_POST_MAX_GROUPS 5      /* cross-posting past this is refused */
#define NN_POST_REFS_MAX   900    /* References kept under 998 octets a line (RFC 5537 3.4.4) */

typedef struct NNPost {
    char newsgroups[512];     /* comma separated */
    char subject[256];
    char references[1024];    /* "" for a new thread */
    char from[256];
} NNPost;

/* A new article to group. */
void   nn_post_new(NNPost *p, const char *group, const char *from);
/* A follow-up to parent, read in group: "Re: " once, the parent's
 * References with its message-id added, the oldest ones after the first
 * dropped while the line is too long. */
void   nn_post_followup(NNPost *p, const char *group, const NNOver *parent, const char *from);
/* 1 when the article may be sent; else 0 with why (for the window). */
int    nn_post_check(const NNPost *p, const char *body, char *why, size_t cap);
/* The parent's text quoted: "<who> wrote:", then each line with "> " before
 * it (">" before a line already quoted), its signature (after "-- ") left
 * out. Answers the length needed. */
size_t nn_post_quote(const char *who, const char *text, size_t len, char *out, size_t cap);
/* The article for POST: the headers, a blank line, the body with its line
 * ends made CR LF (nntp.c dot-stuffs it). Answers the length needed. */
size_t nn_post_format(const NNPost *p, const char *body, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
