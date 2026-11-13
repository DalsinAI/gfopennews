/* GFOpenNews .newsrc: "group: ranges" (subscribed) or "group! ranges".
 * Order of groups is kept. Saved atomically (written aside, swapped in).
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_NEWSRC_H
#define GFOPENNEWS_NEWSRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NNRange { uint32_t lo, hi; } NNRange;

typedef struct NNGroupRc {
    char    *name;
    int      subscribed;
    NNRange *r;           /* sorted, not touching each other */
    size_t   n, cap;
} NNGroupRc;

typedef struct NNNewsrc {
    NNGroupRc *g;
    size_t     n, cap;
    unsigned long bad_lines;   /* lines that could not be read on load */
} NNNewsrc;

void       nn_rc_init(NNNewsrc *rc);
void       nn_rc_free(NNNewsrc *rc);
/* Parses a whole file's text. Unreadable lines are skipped and counted. */
int        nn_rc_parse(NNNewsrc *rc, const char *text, size_t len);
/* Missing file = empty newsrc (NK_OK). */
int        nn_rc_load(NNNewsrc *rc, const char *path);
int        nn_rc_save(const NNNewsrc *rc, const char *path);
NNGroupRc *nn_rc_group(NNNewsrc *rc, const char *name, int create);
/* Marks lo..hi read (read != 0) or unread. */
int        nn_rc_mark(NNGroupRc *g, uint32_t lo, uint32_t hi, int read);
int        nn_rc_is_read(const NNGroupRc *g, uint32_t n);
/* Unread articles between the server's low and high marks. */
uint32_t   nn_rc_unread(const NNGroupRc *g, uint32_t low, uint32_t high);
/* Formats "name: 1-5,7" into buf; answers the length needed. */
size_t     nn_rc_format(const NNGroupRc *g, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
