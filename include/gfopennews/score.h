/* GFOpenNews kill and score files (design step 8): plain-text rules that
 * give each article in the thread list a score, and hide the ones that are
 * killed. Our own format (the idea of score files comes from slrn and tin,
 * whose code was not used):
 *
 *   # a comment
 *   [comp.sys.amiga.*, de.comp.sys.amiga]   the groups the rules below apply to
 *   score +50 subject: *A1200*
 *   score -20 from: *@example.invalid
 *   kill from: spammer@*
 *   score -10 lines< 3
 *   [*]
 *   kill subject: *MAKE MONEY FAST*
 *
 * A section starts with a list of group patterns; rules before the first
 * section apply to every group. A text rule matches a header (from, subject,
 * msgid, references, xref) against a pattern: * is any run of characters,
 * ? any one character, and the match is on the whole value, ignoring case
 * (write *word* to find a word anywhere). lines< N, lines> N, bytes< N and
 * bytes> N compare numbers. Every matching rule adds its score; "kill" adds
 * NN_SCORE_KILL, so a killed article stays killed whatever else matches.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_SCORE_H
#define GFOPENNEWS_SCORE_H

#include <stddef.h>
#include "gfopennews/nntp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NN_SCORE_KILL   (-1000000)
#define NN_SCORE_MAX    100000      /* the largest score one rule may give */
#define NN_SCORE_RULES  4096        /* rules in one file */

typedef struct NNScore NNScore;

/* Reads the rules. NULL with *bad_line set to the first line that is not a
 * rule (1-based), or 0 when out of memory or over NN_SCORE_RULES. */
NNScore *nn_score_parse(const char *text, size_t len, unsigned *bad_line);
void     nn_score_free(NNScore *s);
size_t   nn_score_rules(const NNScore *s);
/* The article's score in group (0 with no rules, or s NULL). */
long     nn_score_of(const NNScore *s, const char *group, const NNOver *ov);
/* 1 when that score means the article is killed (hidden). */
int      nn_score_killed(long score);
/* Pattern match as the rules use it (exported for the tests and the UI). */
int      nn_score_glob(const char *pat, const char *s);

#ifdef __cplusplus
}
#endif
#endif
