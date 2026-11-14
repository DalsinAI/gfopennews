/* GFOpenNews kill and score files: see score.h for the format.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include <string.h>
#include "gfopennews/score.h"

enum { F_FROM, F_SUBJECT, F_MSGID, F_REFS, F_XREF, F_LINES_LT, F_LINES_GT, F_BYTES_LT, F_BYTES_GT };

typedef struct Rule {
    int      field;
    long     score;
    unsigned section;   /* index into groups; 0 = every group */
    char    *pat;       /* text rules */
    unsigned long num;  /* number rules */
} Rule;

struct NNScore {
    Rule    *rules;
    size_t   n, cap;
    char   **groups;    /* section i's group patterns, comma separated (groups[0] unused) */
    unsigned nsec, capsec;
};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int nn_score_glob(const char *p, const char *s)
{
    const char *star = 0, *back = 0;
    if (!p || !s) return 0;
    while (*s) {
        if (*p == '*') { star = ++p; back = s; continue; }
        if (*p && (*p == '?' || lower((unsigned char)*p) == lower((unsigned char)*s))) { p++; s++; continue; }
        if (!star) return 0;
        p = star;
        s = ++back;
    }
    while (*p == '*') p++;
    return !*p;
}

static char *dup_n(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

static const char *skip_ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static void trim_end(const char *b, const char **e)
{
    while (*e > b && ((*e)[-1] == ' ' || (*e)[-1] == '\t' || (*e)[-1] == '\r')) (*e)--;
}

static const struct { const char *name; int field; } FIELDS[] = {
    { "from:", F_FROM }, { "subject:", F_SUBJECT }, { "msgid:", F_MSGID },
    { "message-id:", F_MSGID }, { "references:", F_REFS }, { "xref:", F_XREF },
    { "lines<", F_LINES_LT }, { "lines>", F_LINES_GT }, { "bytes<", F_BYTES_LT }, { "bytes>", F_BYTES_GT },
};

static int add_rule(NNScore *s, const Rule *r)
{
    if (s->n == NN_SCORE_RULES) return 0;
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        Rule *nr = (Rule *)realloc(s->rules, nc * sizeof(Rule));
        if (!nr) return 0;
        s->rules = nr;
        s->cap = nc;
    }
    s->rules[s->n++] = *r;
    return 1;
}

static int add_section(NNScore *s, const char *b, const char *e)
{
    if (s->nsec + 1 >= s->capsec) {
        unsigned nc = s->capsec ? s->capsec * 2 : 8;
        char **ng = (char **)realloc(s->groups, nc * sizeof(char *));
        if (!ng) return 0;
        s->groups = ng;
        s->capsec = nc;
        if (!s->nsec) s->groups[0] = 0;
    }
    if (!(s->groups[++s->nsec] = dup_n(b, (size_t)(e - b)))) { s->nsec--; return 0; }
    return 1;
}

/* One rule line: "score N FIELD VALUE" or "kill FIELD VALUE".
 * 1 taken, 0 not a rule, -1 out of memory. */
static int parse_rule(NNScore *s, const char *p, const char *e)
{
    Rule r;
    size_t i, fl;
    memset(&r, 0, sizeof(r));
    r.section = s->nsec;
    if (e - p > 5 && !memcmp(p, "kill", 4) && (p[4] == ' ' || p[4] == '\t')) {
        r.score = NN_SCORE_KILL;
        p += 4;
    } else if (e - p > 6 && !memcmp(p, "score", 5) && (p[5] == ' ' || p[5] == '\t')) {
        char num[16];
        size_t k = 0;
        char *end;
        p = skip_ws(p + 5, e);
        while (p < e && *p != ' ' && *p != '\t' && k < sizeof(num) - 1) num[k++] = *p++;
        num[k] = 0;
        r.score = strtol(num, &end, 10);
        if (!k || *end || r.score > NN_SCORE_MAX || r.score < -NN_SCORE_MAX) return 0;
    } else
        return 0;
    p = skip_ws(p, e);
    for (i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); i++) {
        fl = strlen(FIELDS[i].name);
        if ((size_t)(e - p) >= fl) {
            size_t k;
            for (k = 0; k < fl && lower((unsigned char)p[k]) == FIELDS[i].name[k]; k++)
                ;
            if (k == fl) break;
        }
    }
    if (i == sizeof(FIELDS) / sizeof(FIELDS[0])) return 0;
    r.field = FIELDS[i].field;
    p = skip_ws(p + fl, e);
    if (p == e) return 0;
    if (r.field >= F_LINES_LT) {
        char num[24], *end;
        size_t k = (size_t)(e - p);
        if (k >= sizeof(num) || *p == '-') return 0;
        memcpy(num, p, k);
        num[k] = 0;
        r.num = strtoul(num, &end, 10);
        if (*end) return 0;
    } else if (!(r.pat = dup_n(p, (size_t)(e - p))))
        return -1;
    if (!add_rule(s, &r)) { free(r.pat); return -1; }
    return 1;
}

void nn_score_free(NNScore *s)
{
    size_t i;
    if (!s) return;
    for (i = 0; i < s->n; i++) free(s->rules[i].pat);
    for (i = 1; i <= s->nsec; i++) free(s->groups[i]);
    free(s->rules);
    free(s->groups);
    free(s);
}

NNScore *nn_score_parse(const char *text, size_t len, unsigned *bad_line)
{
    NNScore *s = (NNScore *)calloc(1, sizeof(NNScore));
    const char *p = text, *end = text + len;
    unsigned line = 0;
    if (bad_line) *bad_line = 0;
    if (!s) return 0;
    while (p < end) {
        const char *e = (const char *)memchr(p, '\n', (size_t)(end - p)), *b;
        const char *next = e ? e + 1 : end;
        if (!e) e = end;
        line++;
        b = skip_ws(p, e);
        trim_end(b, &e);
        if (b == e || *b == '#') { p = next; continue; }
        if (*b == '[') {
            if (e[-1] != ']' || e - b < 3) {
                if (bad_line) *bad_line = line;
                nn_score_free(s);
                return 0;
            }
            if (!add_section(s, b + 1, e - 1)) {
                nn_score_free(s);
                return 0;
            }
        } else {
            int r = parse_rule(s, b, e);
            if (r != 1) {
                if (bad_line && r == 0) *bad_line = line;
                nn_score_free(s);
                return 0;
            }
        }
        p = next;
    }
    return s;
}

size_t nn_score_rules(const NNScore *s) { return s ? s->n : 0; }

/* group is in the comma-separated list of patterns */
static int group_in(const char *list, const char *group)
{
    char pat[256];
    while (*list) {
        size_t k = 0;
        while (*list == ' ' || *list == '\t' || *list == ',') list++;
        while (*list && *list != ',' && k < sizeof(pat) - 1) pat[k++] = *list++;
        while (k && (pat[k - 1] == ' ' || pat[k - 1] == '\t')) k--;
        pat[k] = 0;
        if (k && nn_score_glob(pat, group)) return 1;
    }
    return 0;
}

long nn_score_of(const NNScore *s, const char *group, const NNOver *ov)
{
    long total = 0;
    size_t i;
    unsigned sec = (unsigned)-1;
    int in = 1;
    if (!s || !ov) return 0;
    for (i = 0; i < s->n; i++) {
        const Rule *r = &s->rules[i];
        const char *v = 0;
        int hit = 0;
        if (r->section != sec) {
            sec = r->section;
            in = !sec || group_in(s->groups[sec], group ? group : "");
        }
        if (!in) continue;
        switch (r->field) {
        case F_FROM: v = ov->from; break;
        case F_SUBJECT: v = ov->subject; break;
        case F_MSGID: v = ov->msgid; break;
        case F_REFS: v = ov->references; break;
        case F_XREF: v = ov->xref; break;
        case F_LINES_LT: hit = ov->lines < r->num; break;
        case F_LINES_GT: hit = ov->lines > r->num; break;
        case F_BYTES_LT: hit = ov->bytes < r->num; break;
        case F_BYTES_GT: hit = ov->bytes > r->num; break;
        }
        if (r->pat) hit = nn_score_glob(r->pat, v ? v : "");
        if (!hit) continue;
        if (r->score == NN_SCORE_KILL) return NN_SCORE_KILL;
        total += r->score;
    }
    /* only a kill rule kills: many low scores together stay just above it */
    return total <= NN_SCORE_KILL ? NN_SCORE_KILL + 1 : total;
}

int nn_score_killed(long score) { return score <= NN_SCORE_KILL; }
