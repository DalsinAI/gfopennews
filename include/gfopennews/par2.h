/* GFOpenNews PAR2: read a recovery set, verify the files, say how many
 * recovery blocks are needed (so only enough .vol files are fetched), and
 * repair with Reed-Solomon over GF(2^16). Our own code from the Parchive 2.0
 * specification (public); par2cmdline and libpar2 (GPL) were not used.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_PAR2_H
#define GFOPENNEWS_PAR2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- MD5 (RFC 1321): used by PAR2. On the Amiga build this is where
 * gfopencrypto's OC_MD5Blocks plugs in. */
typedef struct NNMd5 { uint32_t s[4]; uint64_t n; uint8_t buf[64]; } NNMd5;
void nn_md5_init(NNMd5 *m);
void nn_md5_update(NNMd5 *m, const void *d, size_t n);
void nn_md5_final(NNMd5 *m, uint8_t out[16]);

/* ---- GF(2^16), generator 0x1100B as PAR2 defines it ---- */
uint16_t nn_gf_mul(uint16_t a, uint16_t b);
uint16_t nn_gf_div(uint16_t a, uint16_t b);
uint16_t nn_gf_pow(uint16_t a, uint32_t e);
/* The input-slice constant for slice index i (0-based over the whole set):
 * 2 to the power of the i-th exponent not divisible by 3, 5, 17 or 257. */
uint16_t nn_par2_const(uint32_t i);

typedef struct NNPar2File {
    uint8_t  id[16];
    uint8_t  md5[16], md5_16k[16];
    uint64_t length;
    char     name[256];
    uint32_t nslices;          /* ceil(length / slice) */
    uint32_t first;            /* index of its first slice in the whole set */
    uint8_t (*slice_md5)[16];  /* from the IFSC packet, or NULL if not seen */
    uint32_t *slice_crc;
    uint32_t nifsc;            /* entries in slice_md5 and slice_crc */
    /* verify results */
    int      present;          /* the file could be opened */
    uint32_t damaged;          /* slices missing or wrong */
    uint8_t *bad;              /* one byte per slice: 1 = needs repair */
    int      too_long;         /* good data but longer than the PAR2 length */
} NNPar2File;

typedef struct NNPar2Rec {
    uint32_t exponent;
    char    *path;             /* which .par2 file holds it */
    long     offset;           /* where its data starts */
    uint64_t len;              /* bytes of data in the packet (must be the slice size) */
    uint8_t  set_id[16];       /* checked when the main packet arrives */
} NNPar2Rec;

#define NN_PAR2_MAXSLICE  (64ul << 20)  /* bytes in a slice we take */
#define NN_PAR2_MAXSLICES 32768u        /* input slices in a set (the spec's limit) */
#define NN_PAR2_MAXMISS   4096u         /* damaged slices one repair takes (memory: 2 * n^2 bytes) */

typedef struct NNPar2 {
    uint8_t     set_id[16];
    uint64_t    slice;
    NNPar2File *files;
    uint32_t    nfiles;
    uint32_t    total_slices;
    NNPar2Rec  *rec;
    uint32_t    nrec;
    unsigned long bad_packets;  /* checksum failures (damaged .par2 data) */
    int         have_main;
    uint32_t    too_long;       /* files verify found too long (repair cuts) */
    int         too_big;        /* the set breaks NN_PAR2_MAXSLICE(S): refused */
} NNPar2;

/* Reads every packet of one .par2 file into the set (call it for the index
 * and for each volume). Packets of another recovery set are skipped. */
int  nn_par2_load(NNPar2 *p, const char *path);
void nn_par2_free(NNPar2 *p);
/* Checks every file in `dir` slice by slice (MD5 and CRC32). Answers the
 * number of damaged or missing slices over the set; p->too_long counts
 * files whose data is good but which are too long (repair cuts them). */
uint32_t nn_par2_verify(NNPar2 *p, const char *dir);
/* Blocks in a volume name such as "x.vol07+08.par2" (8), or 0. */
uint32_t nn_par2_vol_blocks(const char *name);
/* Picks volumes (names, blocks each) whose blocks reach `need` with the
 * fewest blocks; sets pick[i] to 1. Answers the blocks picked, or less
 * than need when there are not enough. */
uint32_t nn_par2_pick(const uint32_t *blocks, size_t n, uint32_t need, uint8_t *pick);
/* Rebuilds every damaged slice from the recovery blocks loaded. A damaged
 * file is rebuilt whole (to its PAR2 length) as <name>.repair, which takes
 * the name once every slice is in and checks; the original stays as
 * <name>.1 (or the next free number). Files verify found too long are cut.
 * NK_OK, NK_EARG when there are too few blocks, NK_ETOOLONG over
 * NN_PAR2_MAXMISS damaged slices, NK_EPROTO when a rebuilt slice is wrong,
 * NK_EIO. */
int  nn_par2_repair(NNPar2 *p, const char *dir);

#ifdef __cplusplus
}
#endif
#endif
