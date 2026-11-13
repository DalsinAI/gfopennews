/* GFOpenNews binaries: yEnc 1.3, UUencode and base64 decoding (streaming,
 * line by line, as NNTP hands lines over), multi-part subjects, and the
 * part file that assembles a file from parts arriving in any order.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_BINARIES_H
#define GFOPENNEWS_BINARIES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded bytes go here: `offset` is the byte's place in the whole file. */
typedef int (*NNWriteFn)(void *user, uint64_t offset, const uint8_t *data, size_t len);

/* ---- yEnc ------------------------------------------------------------------ */
#define NN_YE_HAVE_PCRC 0x01
#define NN_YE_HAVE_CRC  0x02
#define NN_YE_HAVE_PART 0x04

enum {
    NN_YE_OK = 0,
    NN_YE_NOBEGIN,       /* no =ybegin seen */
    NN_YE_NOEND,         /* =yend missing (article cut short) */
    NN_YE_BADHEADER,     /* =ybegin/=ypart/=yend unreadable */
    NN_YE_SIZE,          /* decoded length differs from =ypart/=yend */
    NN_YE_PCRC,          /* part CRC differs */
    NN_YE_CRC,           /* whole-file CRC differs (single-part posts) */
    NN_YE_WRITE          /* the write callback failed */
};

typedef struct NNYenc {
    /* from the headers */
    char     name[256];
    uint64_t size;         /* whole file */
    uint32_t part, total;  /* 0 when single-part */
    uint64_t begin, end;   /* 1-based, inclusive (=ypart) */
    uint32_t pcrc32, crc32;
    unsigned have;
    /* results */
    uint64_t got;          /* bytes decoded for this part */
    uint32_t crc;          /* CRC of those bytes */
    int      status;       /* NN_YE_* */
    /* private */
    int      state, esc;
    NNWriteFn write;
    void    *user;
} NNYenc;

void nn_ye_init(NNYenc *y, NNWriteFn write, void *user);
/* One article body line (without CR LF, already dot-unstuffed). Lines before
 * =ybegin are ignored (text around the post). */
void nn_ye_line(NNYenc *y, const char *line, size_t len);
/* Call at the end of the article: sets and answers the final status. */
int  nn_ye_finish(NNYenc *y);
const char *nn_ye_strerror(int status);

/* ---- UUencode -------------------------------------------------------------- */
typedef struct NNUu {
    char     name[256];
    unsigned mode;
    uint64_t got;
    int      state;        /* 0 before begin, 1 in body, 2 after end */
    int      bad_lines;
    NNWriteFn write;
    void    *user;
} NNUu;

void nn_uu_init(NNUu *u, NNWriteFn write, void *user);
void nn_uu_line(NNUu *u, const char *line, size_t len);
/* 1 when "begin" and "end" were both seen */
int  nn_uu_done(const NNUu *u);

/* ---- base64 (MIME attachments) -------------------------------------------- */
typedef struct NNB64 {
    uint32_t acc;
    int      bits, ended;
    uint64_t got;
    NNWriteFn write;
    void    *user;
} NNB64;

void nn_b64_init(NNB64 *b, NNWriteFn write, void *user);
int  nn_b64_line(NNB64 *b, const char *line, size_t len);   /* NK_OK or NK_EIO */

/* ---- multi-part subjects --------------------------------------------------- */
/* Finds the file name ("quoted", or the word before "yEnc") and the last
 * (n/m) or [n/m] in a subject. Answers 1 when a part count was found. */
int nn_bin_subject(const char *subject, char *name, size_t name_cap,
                   uint32_t *part, uint32_t *total);

/* ---- part file -------------------------------------------------------------
 * <final>.part holds the data at its offsets, <final>.parts (a gfopennetkit
 * journal) the parts held: number, offset, length and CRC. A restart reads
 * it back, so only missing parts are fetched again. When all parts are in,
 * the CRCs are joined in offset order (no re-reading of the data), checked
 * against the whole-file CRC when one is known, and the file takes its
 * final name. */
typedef struct NNPartFile NNPartFile;

enum {
    NN_PF_INCOMPLETE = 0,
    NN_PF_DONE,           /* renamed to the final name */
    NN_PF_BADCRC,         /* all parts in, whole-file CRC wrong (kept as .part) */
    NN_PF_GAPS            /* all parts in, but they leave holes or overlap */
};

NNPartFile *nn_pf_open(const char *final_path, uint64_t size, uint32_t nparts, int *err);
/* Writes one decoded part: data at offset (0-based). Marks it held once
 * written and synced. Parts already held are skipped (answers NK_OK). */
int      nn_pf_write(NNPartFile *pf, uint32_t part, uint64_t offset,
                     const uint8_t *data, size_t len);
/* For decoders that stream: begin, write pieces, then commit with the
 * part's CRC (nn_pf_commit checks that the pieces covered len bytes). */
int      nn_pf_put(NNPartFile *pf, uint64_t offset, const uint8_t *data, size_t len);
int      nn_pf_commit(NNPartFile *pf, uint32_t part, uint64_t offset, uint64_t len, uint32_t crc);
int      nn_pf_have(const NNPartFile *pf, uint32_t part);
uint32_t nn_pf_count(const NNPartFile *pf);
/* Whole-file CRC to check against (from yEnc =yend crc32), when known. */
void     nn_pf_expect_crc(NNPartFile *pf, uint32_t crc);
/* When every part is in: check and rename. Answers NN_PF_*. */
int      nn_pf_finish(NNPartFile *pf);
/* Final name actually used (a numbered one when the name was taken). */
const char *nn_pf_final(const NNPartFile *pf);
void     nn_pf_close(NNPartFile *pf);

#ifdef __cplusplus
}
#endif
#endif
