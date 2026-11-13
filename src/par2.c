/* PAR2 reading, verifying and repairing. Our own code from the Parchive 2.0
 * specification. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/par2.h"

/* ---- GF(2^16) --------------------------------------------------------------- */
static uint16_t gf_log[65536], gf_exp[65536 * 2];
static int gf_ready;

static void gf_init(void)
{
    uint32_t x = 1, i;
    for (i = 0; i < 65535; i++) {
        gf_exp[i] = (uint16_t)x;
        gf_log[x] = (uint16_t)i;
        x <<= 1;
        if (x & 0x10000)
            x ^= 0x1100B;
    }
    for (i = 65535; i < 65536 * 2; i++)
        gf_exp[i] = gf_exp[i - 65535];
    gf_ready = 1;
}

uint16_t nn_gf_mul(uint16_t a, uint16_t b)
{
    if (!gf_ready) gf_init();
    if (!a || !b) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

uint16_t nn_gf_div(uint16_t a, uint16_t b)
{
    if (!gf_ready) gf_init();
    if (!a || !b) return 0;
    return gf_exp[gf_log[a] + 65535 - gf_log[b]];
}

uint16_t nn_gf_pow(uint16_t a, uint32_t e)
{
    if (!gf_ready) gf_init();
    if (!e) return 1;
    if (!a) return 0;
    return gf_exp[(uint32_t)(((uint64_t)gf_log[a] * e) % 65535)];
}

uint16_t nn_par2_const(uint32_t i)
{
    uint32_t n = 0, k = 0;
    if (!gf_ready) gf_init();
    for (;;) {
        n++;
        if (n % 3 && n % 5 && n % 17 && n % 257) {
            if (k == i)
                return gf_exp[n % 65535];
            k++;
        }
    }
}

/* ---- packets ------------------------------------------------------------------ */
static const uint8_t MAGIC[8] = { 'P', 'A', 'R', '2', 0, 'P', 'K', 'T' };
#define T_MAIN "PAR 2.0\0Main\0\0\0\0"
#define T_DESC "PAR 2.0\0FileDesc"
#define T_IFSC "PAR 2.0\0IFSC\0\0\0\0"
#define T_RECV "PAR 2.0\0RecvSlic"

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static NNPar2File *file_by_id(NNPar2 *p, const uint8_t *id)
{
    uint32_t i;
    for (i = 0; i < p->nfiles; i++)
        if (!memcmp(p->files[i].id, id, 16))
            return &p->files[i];
    return 0;
}

static int id_cmp(const void *a, const void *b)
{
    return memcmp(((const NNPar2File *)a)->id, ((const NNPar2File *)b)->id, 16);
}

/* slice numbers for every file; a set over NN_PAR2_MAXSLICES is refused
 * (so nslices, first and the totals never wrap, even on 32 bits) */
static void layout(NNPar2 *p)
{
    uint32_t i;
    uint64_t at = 0;
    if (!p->slice) return;
    for (i = 0; i < p->nfiles; i++) {
        NNPar2File *f = &p->files[i];
        if (f->length || f->name[0]) {
            uint64_t n = f->length / p->slice + (f->length % p->slice != 0);
            if (n > NN_PAR2_MAXSLICES) {
                p->too_big = 1;
                n = 0;
            }
            f->nslices = (uint32_t)n;
        }
        f->first = (uint32_t)at;
        at += f->nslices;
        if (at > NN_PAR2_MAXSLICES) {
            p->too_big = 1;
            at = NN_PAR2_MAXSLICES;
        }
    }
    p->total_slices = (uint32_t)at;
}

static void free_file_data(NNPar2File *f)
{
    free(f->slice_md5);
    free(f->slice_crc);
    free(f->bad);
    f->slice_md5 = 0;
    f->slice_crc = 0;
    f->bad = 0;
    f->nifsc = 0;
}

static int on_packet(NNPar2 *p, const char *path, long off, const uint8_t *pk, uint64_t len)
{
    const uint8_t *body = pk + 64;
    uint64_t bl = len - 64;
    if (p->have_main || !memcmp(pk + 48, T_MAIN, 16)) {
        if (p->have_main && memcmp(pk + 32, p->set_id, 16))
            return 0;             /* another recovery set */
    }
    if (!memcmp(pk + 48, T_MAIN, 16)) {
        uint32_t n, i;
        if (p->have_main || bl < 12) return 0;
        n = le32(body + 8);
        if (12 + (uint64_t)n * 16 > bl || n > NN_PAR2_MAXSLICES) return 0;
        if (!le64(body) || le64(body) % 4 || le64(body) > NN_PAR2_MAXSLICE) return 0;
        /* files described before the main packet keep their details */
        {
            NNPar2File *nf = (NNPar2File *)calloc(n ? n : 1, sizeof(NNPar2File));
            if (!nf) return NK_ENOMEM;
            for (i = 0; i < n; i++)
                memcpy(nf[i].id, body + 12 + i * 16, 16);
            qsort(nf, n, sizeof(NNPar2File), id_cmp);
            for (i = 1; i < n; i++)
                if (!memcmp(nf[i - 1].id, nf[i].id, 16)) {
                    free(nf);      /* the same file twice: a broken packet */
                    return 0;
                }
            for (i = 0; i < n; i++) {
                NNPar2File *old = file_by_id(p, nf[i].id);
                if (old) {
                    nf[i] = *old;
                    memset(old->id, 0, 16);     /* moved: not freed below */
                    old->slice_md5 = 0;
                    old->slice_crc = 0;
                    old->bad = 0;
                }
            }
            for (i = 0; i < p->nfiles; i++)
                free_file_data(&p->files[i]);   /* kept but not in the set */
            free(p->files);
            p->files = nf;
            p->nfiles = n;
        }
        memcpy(p->set_id, pk + 32, 16);
        p->slice = le64(body);
        p->have_main = 1;
        /* recovery packets kept before this: only this set's, whole slices */
        {
            uint32_t k = 0;
            for (i = 0; i < p->nrec; i++) {
                if (!memcmp(p->rec[i].set_id, p->set_id, 16) && p->rec[i].len == p->slice)
                    p->rec[k++] = p->rec[i];
                else
                    free(p->rec[i].path);
            }
            p->nrec = k;
        }
        layout(p);
    } else if (!memcmp(pk + 48, T_DESC, 16)) {
        NNPar2File *f;
        size_t nl;
        char name[256];
        if (bl < 56) return 0;
        nl = (size_t)(bl - 56);
        if (nl >= sizeof(name)) nl = sizeof(name) - 1;
        memcpy(name, body + 56, nl);
        name[nl] = 0;
        /* never a path: keep the last part */
        {
            char *c = strrchr(name, '/'), *b = strrchr(name, '\\'), *k = strrchr(name, ':');
            if (b > c) c = b;
            if (k > c) c = k;
            if (c) memmove(name, c + 1, strlen(c + 1) + 1);
        }
        if (!name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
            return 0;
        f = file_by_id(p, body);
        if (!f) {
            if (p->have_main) return 0;      /* not in the recovery set */
            /* keep it until the main packet arrives */
            {
                NNPar2File *nf = (NNPar2File *)realloc(p->files, (p->nfiles + 1) * sizeof(NNPar2File));
                if (!nf) return NK_ENOMEM;
                p->files = nf;
                f = &p->files[p->nfiles++];
                memset(f, 0, sizeof(*f));
                memcpy(f->id, body, 16);
            }
        }
        memcpy(f->md5, body + 16, 16);
        memcpy(f->md5_16k, body + 32, 16);
        f->length = le64(body + 48);
        memcpy(f->name, name, sizeof(f->name));
        if (p->have_main) layout(p);
    } else if (!memcmp(pk + 48, T_IFSC, 16)) {
        NNPar2File *f = bl >= 16 ? file_by_id(p, body) : 0;
        uint32_t n, i;
        if (!f || f->slice_md5) return 0;
        if ((bl - 16) / 20 > NN_PAR2_MAXSLICES) return 0;
        n = (uint32_t)((bl - 16) / 20);
        f->nifsc = n;
        f->slice_md5 = (uint8_t (*)[16])malloc((n ? n : 1) * 16);
        f->slice_crc = (uint32_t *)malloc((n ? n : 1) * sizeof(uint32_t));
        if (!f->slice_md5 || !f->slice_crc) return NK_ENOMEM;
        for (i = 0; i < n; i++) {
            memcpy(f->slice_md5[i], body + 16 + i * 20, 16);
            f->slice_crc[i] = le32(body + 16 + i * 20 + 16);
        }
        if (!f->nslices) f->nslices = n;
    } else if (!memcmp(pk + 48, T_RECV, 16)) {
        NNPar2Rec *nr;
        uint32_t e, i;
        if (bl < 4 || (p->have_main && bl - 4 != p->slice)) return 0;
        e = le32(body);
        for (i = 0; i < p->nrec; i++)
            if (p->rec[i].exponent == e) return 0;      /* have it */
        nr = (NNPar2Rec *)realloc(p->rec, (p->nrec + 1) * sizeof(NNPar2Rec));
        if (!nr) return NK_ENOMEM;
        p->rec = nr;
        nr[p->nrec].exponent = e;
        nr[p->nrec].offset = off + 68;
        nr[p->nrec].len = bl - 4;
        memcpy(nr[p->nrec].set_id, pk + 32, 16);
        nr[p->nrec].path = (char *)malloc(strlen(path) + 1);
        if (!nr[p->nrec].path) return NK_ENOMEM;
        strcpy(nr[p->nrec].path, path);
        p->nrec++;
    }
    return 0;
}

int nn_par2_load(NNPar2 *p, const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t hdr[64];
    long off = 0;
    if (!f) return NK_EIO;
    for (;;) {
        uint64_t len;
        uint8_t *pk, digest[16];
        NNMd5 m;
        if (fseek(f, off, SEEK_SET) != 0 || fread(hdr, 1, 64, f) != 64)
            break;
        if (memcmp(hdr, MAGIC, 8)) {
            /* damaged: look for the next magic, 4-byte aligned */
            off += 4;
            continue;
        }
        len = le64(hdr + 8);
        if (len < 64 || len % 4 || len > (1u << 30)) {
            p->bad_packets++;
            off += 8;
            continue;
        }
        /* recovery slices are only read whole when repairing */
        pk = (uint8_t *)malloc((size_t)len);
        if (!pk) { fclose(f); return NK_ENOMEM; }
        memcpy(pk, hdr, 64);
        if (fread(pk + 64, 1, (size_t)(len - 64), f) != len - 64) {
            free(pk);
            break;
        }
        nn_md5_init(&m);
        nn_md5_update(&m, pk + 32, (size_t)(len - 32));
        nn_md5_final(&m, digest);
        if (memcmp(digest, pk + 16, 16)) {
            p->bad_packets++;
            free(pk);
            off += 8;
            continue;
        }
        if (on_packet(p, path, off, pk, len) == NK_ENOMEM) {
            free(pk);
            fclose(f);
            return NK_ENOMEM;
        }
        free(pk);
        off += (long)len;
    }
    fclose(f);
    return p->have_main && !p->too_big ? NK_OK : NK_EPROTO;
}

void nn_par2_free(NNPar2 *p)
{
    uint32_t i;
    for (i = 0; i < p->nfiles; i++)
        free_file_data(&p->files[i]);
    for (i = 0; i < p->nrec; i++)
        free(p->rec[i].path);
    free(p->files);
    free(p->rec);
    memset(p, 0, sizeof(*p));
}

static char *join(const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    char *s = (char *)malloc(dl + strlen(name) + 2);
    if (!s) return 0;
    strcpy(s, dir);
    if (dl && dir[dl - 1] != '/' && dir[dl - 1] != ':')
        strcat(s, "/");
    strcat(s, name);
    return s;
}

static char *part_name(const char *path)
{
    size_t l = strlen(path);
    char *s = (char *)malloc(l + 6);
    if (s) {
        memcpy(s, path, l);
        memcpy(s + l, ".part", 6);
    }
    return s;
}

/* A file the downloader left as <name>.part takes its name before repair;
 * its .parts list of held parts means nothing after that. */
static void adopt_part(const char *dir, const char *name)
{
    char *path = join(dir, name), *pp = path ? part_name(path) : 0;
    FILE *fp = path ? fopen(path, "rb") : 0;
    if (fp)
        fclose(fp);
    else if (pp && rename(pp, path) == 0) {
        size_t l = strlen(pp);
        char *ps = (char *)malloc(l + 2);
        if (ps) {
            memcpy(ps, pp, l);
            memcpy(ps + l, "s", 2);
            remove(ps);
            free(ps);
        }
    }
    free(pp);
    free(path);
}

/* reads slice k of a file of `length` bytes into buf, zero-padded (bytes
 * past `length` in a file too long are not part of the slice); answers
 * bytes read */
static size_t read_slice(FILE *f, uint64_t slice, uint32_t k, uint64_t length, uint8_t *buf)
{
    uint64_t at = slice * k, n = slice;
    memset(buf, 0, (size_t)slice);
    if (at >= length || fseek(f, (long)at, SEEK_SET) != 0)
        return 0;
    if (n > length - at)
        n = length - at;
    return fread(buf, 1, (size_t)n, f);
}

uint32_t nn_par2_verify(NNPar2 *p, const char *dir)
{
    uint32_t i, k, total = 0;
    uint8_t *buf;
    if (p->too_big || p->slice > NN_PAR2_MAXSLICE)
        return 0xffffffffu;
    buf = (uint8_t *)malloc((size_t)p->slice ? (size_t)p->slice : 1);
    if (!buf) return 0xffffffffu;
    p->too_long = 0;
    for (i = 0; i < p->nfiles; i++) {
        NNPar2File *f = &p->files[i];
        char *path = join(dir, f->name);
        FILE *fp = path && f->name[0] ? fopen(path, "rb") : 0;
        long flen = -1;
        if (!fp && path && f->name[0]) {
            /* a download that stopped short keeps its data in <name>.part */
            char *pp = part_name(path);
            fp = pp ? fopen(pp, "rb") : 0;
            free(pp);
        }
        free(path);
        free(f->bad);
        f->bad = (uint8_t *)calloc(f->nslices ? f->nslices : 1, 1);
        f->damaged = 0;
        f->present = fp != 0;
        if (fp && fseek(fp, 0, SEEK_END) == 0)
            flen = ftell(fp);
        for (k = 0; k < f->nslices; k++) {
            int ok = 0;
            if (fp && f->slice_md5 && k < f->nifsc && (uint64_t)flen >= (uint64_t)k * p->slice) {
                uint8_t d[16];
                NNMd5 m;
                uint64_t want = f->length - (uint64_t)k * p->slice;
                size_t got = read_slice(fp, p->slice, k, f->length, buf);
                if (want > p->slice) want = p->slice;
                if (got >= want &&
                    nk_crc32_update(0, buf, (size_t)p->slice) == f->slice_crc[k]) {
                    nn_md5_init(&m);
                    nn_md5_update(&m, buf, (size_t)p->slice);
                    nn_md5_final(&m, d);
                    ok = !memcmp(d, f->slice_md5[k], 16);
                }
            }
            if (!ok) {
                f->bad[k] = 1;
                f->damaged++;
            }
        }
        /* a file longer than it should be: the data is good, repair cuts it */
        f->too_long = fp && flen >= 0 && (uint64_t)flen > f->length;
        if (f->too_long) p->too_long++;
        if (fp) fclose(fp);
        total += f->damaged;
    }
    free(buf);
    return total;
}

uint32_t nn_par2_vol_blocks(const char *name)
{
    const char *v = strstr(name, ".vol");
    unsigned long a, b;
    char *e;
    if (!v) v = strstr(name, ".VOL");
    if (!v) return 0;
    a = strtoul(v + 4, &e, 10);
    if (e == v + 4 || (*e != '+' && *e != '-')) return 0;
    b = strtoul(e + 1, &e, 10);
    (void)a;
    return (uint32_t)b;
}

uint32_t nn_par2_pick(const uint32_t *blocks, size_t n, uint32_t need, uint8_t *pick)
{
    /* smallest total that reaches `need`: a 0/1 knapsack over the volume
     * sizes (volumes come in powers of two, so n is small) */
    uint32_t best = 0xffffffffu, sum;
    size_t i;
    unsigned long mask, bestm = 0, lim;
    memset(pick, 0, n);
    if (!need) return 0;
    if (n > 20) {
        /* many volumes: biggest first, then trim */
        uint32_t got = 0;
        for (i = n; i-- > 0 && got < need;) {
            pick[i] = 1;
            got += blocks[i];
        }
        return got;
    }
    lim = 1ul << n;
    for (mask = 1; mask < lim; mask++) {
        sum = 0;
        for (i = 0; i < n; i++)
            if (mask & (1ul << i)) sum += blocks[i];
        if (sum >= need && sum < best) {
            best = sum;
            bestm = mask;
        }
    }
    if (best == 0xffffffffu) {
        sum = 0;
        for (i = 0; i < n; i++) { pick[i] = 1; sum += blocks[i]; }
        return sum;
    }
    for (i = 0; i < n; i++)
        pick[i] = (bestm >> i) & 1;
    return best;
}

/* ---- repair -------------------------------------------------------------------- */
int nn_par2_repair(NNPar2 *p, const char *dir)
{
    uint32_t nmiss = 0, i, k, r, c;
    uint32_t *miss = 0;          /* global slice index of each missing slice */
    uint16_t *mat = 0;           /* nmiss x nmiss */
    uint16_t **acc = 0;          /* right-hand side per used recovery block */
    uint8_t *buf = 0;
    uint16_t *cst = 0;           /* input constant of every slice */
    size_t words = (size_t)(p->slice / 2);
    int ret = NK_ENOMEM;

    if (p->too_big || p->slice > NN_PAR2_MAXSLICE)
        return NK_EPROTO;
    for (i = 0; i < p->nfiles; i++) {
        if (!p->files[i].name[0])
            return NK_EPROTO;        /* no file description: slices can't be placed */
        nmiss += p->files[i].damaged;
    }
    if (!nmiss) {
        ret = NK_OK;
        goto truncate;
    }
    if (nmiss > p->nrec)
        return NK_EARG;
    /* verify read <name>.part where there was no <name>: so does repair */
    for (i = 0; i < p->nfiles; i++)
        adopt_part(dir, p->files[i].name);
    miss = (uint32_t *)malloc(nmiss * sizeof(uint32_t));
    mat = (uint16_t *)malloc((size_t)nmiss * nmiss * sizeof(uint16_t));
    acc = (uint16_t **)calloc(nmiss, sizeof(uint16_t *));
    buf = (uint8_t *)malloc((size_t)p->slice);
    cst = (uint16_t *)malloc((p->total_slices ? p->total_slices : 1) * sizeof(uint16_t));
    if (!miss || !mat || !acc || !buf || !cst)
        goto out;
    {
        uint32_t n = 0, j = 0;
        while (j < p->total_slices) {
            n++;
            if (n % 3 && n % 5 && n % 17 && n % 257)
                cst[j++] = nn_gf_pow(2, n);
        }
    }
    for (i = 0, k = 0; i < p->nfiles; i++) {
        uint32_t s;
        for (s = 0; s < p->files[i].nslices; s++)
            if (p->files[i].bad[s])
                miss[k++] = p->files[i].first + s;
    }
    /* right-hand sides: each recovery block minus what the good slices give */
    for (r = 0; r < nmiss; r++) {
        FILE *fp = fopen(p->rec[r].path, "rb");
        uint16_t *a = (uint16_t *)malloc((size_t)p->slice);
        uint32_t e = p->rec[r].exponent;
        size_t w;
        acc[r] = a;
        if (!a || !fp) {
            if (fp) fclose(fp);
            ret = a ? NK_EIO : NK_ENOMEM;
            goto out;
        }
        if (fseek(fp, p->rec[r].offset, SEEK_SET) != 0 || fread(buf, 1, (size_t)p->slice, fp) != p->slice) {
            fclose(fp);
            ret = NK_EIO;
            goto out;
        }
        fclose(fp);
        for (w = 0; w < words; w++)
            a[w] = (uint16_t)(buf[2 * w] | buf[2 * w + 1] << 8);
        for (c = 0; c < nmiss; c++)
            mat[r * nmiss + c] = nn_gf_pow(cst[miss[c]], e);
    }
    for (i = 0; i < p->nfiles; i++) {
        NNPar2File *f = &p->files[i];
        char *path;
        FILE *fp;
        uint32_t s;
        if (f->damaged == f->nslices)
            continue;
        path = join(dir, f->name);
        fp = path ? fopen(path, "rb") : 0;
        free(path);
        if (!fp) {
            ret = NK_EIO;       /* verify read its good slices: they must be there */
            goto out;
        }
        for (s = 0; s < f->nslices; s++) {
            if (f->bad[s]) continue;
            read_slice(fp, p->slice, s, f->length, buf);
            for (r = 0; r < nmiss; r++) {
                uint16_t m = nn_gf_pow(cst[f->first + s], p->rec[r].exponent);
                size_t w;
                if (!m) continue;
                for (w = 0; w < words; w++) {
                    uint16_t v = (uint16_t)(buf[2 * w] | buf[2 * w + 1] << 8);
                    if (v) acc[r][w] ^= nn_gf_mul(m, v);
                }
            }
        }
        fclose(fp);
    }
    /* Gauss-Jordan over GF(2^16), applied to the slices as we go */
    for (c = 0; c < nmiss; c++) {
        uint32_t piv = c;
        uint16_t inv;
        while (piv < nmiss && !mat[piv * nmiss + c]) piv++;
        if (piv == nmiss) { ret = NK_EPROTO; goto out; }
        if (piv != c) {
            uint16_t *t = acc[piv];
            for (k = 0; k < nmiss; k++) {
                uint16_t x = mat[c * nmiss + k];
                mat[c * nmiss + k] = mat[piv * nmiss + k];
                mat[piv * nmiss + k] = x;
            }
            acc[piv] = acc[c];
            acc[c] = t;
        }
        inv = nn_gf_div(1, mat[c * nmiss + c]);
        for (k = 0; k < nmiss; k++)
            mat[c * nmiss + k] = nn_gf_mul(mat[c * nmiss + k], inv);
        for (k = 0; k < words; k++)
            acc[c][k] = nn_gf_mul(acc[c][k], inv);
        for (r = 0; r < nmiss; r++) {
            uint16_t m = mat[r * nmiss + c];
            if (r == c || !m) continue;
            for (k = 0; k < nmiss; k++)
                mat[r * nmiss + k] ^= nn_gf_mul(m, mat[c * nmiss + k]);
            for (k = 0; k < words; k++)
                if (acc[c][k]) acc[r][k] ^= nn_gf_mul(m, acc[c][k]);
        }
    }
    /* acc[c] is now missing slice miss[c]: write them back */
    for (i = 0, c = 0; i < p->nfiles; i++) {
        NNPar2File *f = &p->files[i];
        char *path;
        FILE *fp;
        uint32_t s;
        if (!f->damaged) continue;
        path = join(dir, f->name);
        if (!path) goto out;
        fp = fopen(path, "r+b");
        if (!fp) fp = fopen(path, "w+b");
        free(path);
        if (!fp) { ret = NK_EIO; goto out; }
        for (s = 0; s < f->nslices; s++) {
            uint64_t at = (uint64_t)s * p->slice, n;
            size_t w;
            if (!f->bad[s]) continue;
            for (c = 0; c < nmiss && miss[c] != f->first + s; c++)
                ;
            if (c == nmiss) { fclose(fp); ret = NK_EPROTO; goto out; }
            for (w = 0; w < words; w++) {
                buf[2 * w] = (uint8_t)acc[c][w];
                buf[2 * w + 1] = (uint8_t)(acc[c][w] >> 8);
            }
            /* a rebuilt slice is written only when its MD5 is right */
            if (f->slice_md5 && s < f->nifsc) {
                uint8_t d[16];
                NNMd5 m;
                nn_md5_init(&m);
                nn_md5_update(&m, buf, (size_t)p->slice);
                nn_md5_final(&m, d);
                if (memcmp(d, f->slice_md5[s], 16)) {
                    fclose(fp);
                    ret = NK_EPROTO;
                    goto out;
                }
            }
            n = f->length - at < p->slice ? f->length - at : p->slice;
            /* a missing file: fill up to here with zeros first (no seeking
             * past the end on AmigaDOS); good slices are written below */
            fseek(fp, 0, SEEK_END);
            {
                long end = ftell(fp);
                static const uint8_t z[1024];
                while (end >= 0 && (uint64_t)end < at) {
                    size_t kk = at - (uint64_t)end > sizeof(z) ? sizeof(z) : (size_t)(at - (uint64_t)end);
                    fwrite(z, 1, kk, fp);
                    end += (long)kk;
                }
            }
            if (fseek(fp, (long)at, SEEK_SET) != 0 || fwrite(buf, 1, (size_t)n, fp) != n) {
                fclose(fp);
                ret = NK_EIO;
                goto out;
            }
            f->bad[s] = 0;
        }
        fclose(fp);
        f->damaged = 0;
    }
    ret = NK_OK;
truncate:
    /* files longer than their PAR2 length are cut back */
    for (i = 0; i < p->nfiles && ret == NK_OK; i++) {
        NNPar2File *f = &p->files[i];
        char *path = join(dir, f->name);
        FILE *fp = path ? fopen(path, "rb") : 0;
        long len = -1;
        if (fp) {
            fseek(fp, 0, SEEK_END);
            len = ftell(fp);
            fclose(fp);
        }
        if (len > 0 && (uint64_t)len > f->length) {
            /* copy the first `length` bytes aside and swap */
            size_t pl = strlen(path);
            char *tmp = (char *)malloc(pl + 5);
            FILE *in = fopen(path, "rb"), *o;
            uint64_t left = f->length;
            uint8_t cp[4096];
            if (tmp && in) {
                memcpy(tmp, path, pl);
                memcpy(tmp + pl, ".tmp", 5);
                o = fopen(tmp, "wb");
                while (o && left) {
                    size_t kk = left > sizeof(cp) ? sizeof(cp) : (size_t)left;
                    if (fread(cp, 1, kk, in) != kk || fwrite(cp, 1, kk, o) != kk) break;
                    left -= kk;
                }
                if (o) fclose(o);
                fclose(in);
                in = 0;
                /* the old file goes aside until the new one has its name,
                 * so a crash leaves one of them under a name we know */
                if (!left) {
                    char *old = (char *)malloc(pl + 5);
                    if (old) {
                        memcpy(old, path, pl);
                        memcpy(old + pl, ".old", 5);
                        remove(old);
                        if (rename(path, old) == 0) {
                            if (rename(tmp, path) == 0) {
                                remove(old);
                                f->too_long = 0;
                            } else
                                rename(old, path);
                        }
                        free(old);
                    }
                }
                remove(tmp);
            }
            if (in) fclose(in);
            free(tmp);
        }
        free(path);
    }
out:
    if (acc) {
        for (r = 0; r < nmiss; r++) free(acc[r]);
        free(acc);
    }
    free(miss);
    free(mat);
    free(buf);
    free(cst);
    return ret;
}
