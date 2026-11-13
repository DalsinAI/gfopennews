/* GFOpenNews NZB 1.1 reader (the public newzbin DTD), with our own small
 * XML reader for the subset NZB uses: elements, attributes, text, comments,
 * <?xml?>, <!DOCTYPE>, CDATA and character entities.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_NZB_H
#define GFOPENNEWS_NZB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NNNzbSeg {
    uint32_t number;
    uint32_t bytes;
    char    *msgid;        /* without < > */
} NNNzbSeg;

typedef struct NNNzbFile {
    char     *subject, *poster;
    uint32_t  date;
    char    **groups;
    size_t    ngroups;
    NNNzbSeg *segs;        /* sorted by number, duplicates dropped */
    size_t    nsegs;
} NNNzbFile;

typedef struct NNNzb {
    NNNzbFile *files;
    size_t     nfiles;
    char      *password;   /* <meta type="password">, or NULL */
    char      *title;      /* <meta type="title">, or NULL */
    unsigned long bad_segments;   /* refused: bad number or message-id */
} NNNzb;

/* NK_OK, NK_EPROTO (not an NZB / broken XML), NK_ENOMEM. */
int  nn_nzb_parse(NNNzb *nzb, const char *xml, size_t len);
int  nn_nzb_load(NNNzb *nzb, const char *path);
void nn_nzb_free(NNNzb *nzb);
/* A message-id safe to put in "BODY <id>": no spaces, controls, < or >. */
int  nn_msgid_ok(const char *id);

#ifdef __cplusplus
}
#endif
#endif
