/* GFOpenNews overview cache: the OVER lines of a group kept on disk,
 * GFOpenNews:cache/<server>/<group>.ov, read back in windows.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_OVERVIEW_H
#define GFOPENNEWS_OVERVIEW_H

#include <stddef.h>
#include <stdint.h>
#include "gfopennews/nntp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds dir/<group>.ov, keeping only safe characters of the group name
 * (anything else becomes '_'). Answers the length needed. */
size_t nn_ov_path(const char *dir, const char *group, char *out, size_t cap);
/* Appends OVER lines (as received) whose number is above what the file
 * already holds; answers how many were taken. */
typedef struct NNOvCache NNOvCache;
NNOvCache *nn_ov_open(const char *path, int *err);
uint32_t   nn_ov_high(const NNOvCache *c);   /* highest number held, 0 when empty */
uint32_t   nn_ov_count(const NNOvCache *c);
int        nn_ov_add(NNOvCache *c, const char *line, size_t len);  /* 1 taken, 0 old/bad */
int        nn_ov_flush(NNOvCache *c);
void       nn_ov_close(NNOvCache *c);
/* Reads records numbered lo..hi, in order. fn answers 0 to go on. */
typedef int (*NNOvFn)(void *user, const NNOver *ov);
int        nn_ov_read(const char *path, uint32_t lo, uint32_t hi, NNOvFn fn, void *user);

#ifdef __cplusplus
}
#endif
#endif
