/* Part file: assembles a binary from parts in any order, resumable.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/binaries.h"

typedef struct { uint32_t part; uint64_t off, len; uint32_t crc; } Held;

struct NNPartFile {
    char      *final, *data_path, *state_path, *used;
    FILE      *f;
    uint64_t   size, fsize;
    uint32_t   nparts, count;
    uint8_t   *bits;
    Held      *held;
    NKJournal *j;
    uint32_t   expect;
    int        have_expect;
    int        header_bad;
};

static char *cat(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char *)malloc(la + lb + 1);
    if (p) {
        memcpy(p, a, la);
        memcpy(p + la, b, lb + 1);
    }
    return p;
}

static int mark(NNPartFile *pf, uint32_t part, uint64_t off, uint64_t len, uint32_t crc)
{
    if (!part || part > pf->nparts || (pf->bits[(part - 1) / 8] & (1u << ((part - 1) % 8))))
        return 0;
    pf->bits[(part - 1) / 8] |= (uint8_t)(1u << ((part - 1) % 8));
    pf->held[pf->count].part = part;
    pf->held[pf->count].off = off;
    pf->held[pf->count].len = len;
    pf->held[pf->count].crc = crc;
    pf->count++;
    return 1;
}

static int replay(void *u, const char *rec, size_t len)
{
    NNPartFile *pf = (NNPartFile *)u;
    char buf[128];
    unsigned long long a, b, c;
    unsigned long d;
    if (len >= sizeof(buf))
        return 0;
    memcpy(buf, rec, len);
    buf[len] = 0;
    if (sscanf(buf, "m %llu %llu", &a, &b) == 2) {
        if (a != pf->size || b != pf->nparts)
            pf->header_bad = 1;
    } else if (sscanf(buf, "p %llu %llu %llu %lx", &a, &b, &c, &d) == 4) {
        if (a <= 0xffffffffu && b + c <= pf->size)
            mark(pf, (uint32_t)a, b, c, (uint32_t)d);
    } else if (sscanf(buf, "c %lx", &d) == 1) {
        pf->expect = (uint32_t)d;
        pf->have_expect = 1;
    }
    return 0;
}

NNPartFile *nn_pf_open(const char *final_path, uint64_t size, uint32_t nparts, int *err)
{
    NNPartFile *pf = (NNPartFile *)calloc(1, sizeof(*pf));
    int e = NK_ENOMEM;
    NKJournalStats st;
    char rec[64];
    if (!pf || !nparts)
        goto fail;
    pf->size = size;
    pf->nparts = nparts;
    pf->final = cat(final_path, "");
    pf->data_path = cat(final_path, ".part");
    pf->state_path = cat(final_path, ".parts");
    pf->bits = (uint8_t *)calloc((nparts + 7) / 8, 1);
    pf->held = (Held *)calloc(nparts, sizeof(Held));
    if (!pf->final || !pf->data_path || !pf->state_path || !pf->bits || !pf->held)
        goto fail;
    pf->j = nk_journal_open(pf->state_path, replay, pf, &st, &e);
    if (!pf->j)
        goto fail;
    pf->f = fopen(pf->data_path, "r+b");
    if (!pf->f) {
        /* no data: whatever the state said is gone too */
        if (pf->count) {
            pf->count = 0;
            memset(pf->bits, 0, (nparts + 7) / 8);
            st.records = 0;
            nk_journal_close(pf->j);
            remove(pf->state_path);
            pf->j = nk_journal_open(pf->state_path, 0, 0, 0, &e);
            if (!pf->j)
                goto fail;
        }
        pf->f = fopen(pf->data_path, "w+b");
    }
    if (!pf->f) {
        e = NK_EIO;
        goto fail;
    }
    if (pf->header_bad) {
        e = NK_EARG;     /* same name, different file: the caller picks another */
        goto fail;
    }
    if (st.records == 0) {
        snprintf(rec, sizeof(rec), "m %llu %lu", (unsigned long long)size, (unsigned long)nparts);
        nk_journal_append(pf->j, rec, strlen(rec));
        nk_journal_sync(pf->j);
    }
    fseek(pf->f, 0, SEEK_END);
    pf->fsize = (uint64_t)ftell(pf->f);
    if (err)
        *err = NK_OK;
    return pf;
fail:
    nn_pf_close(pf);
    if (err)
        *err = e;
    return 0;
}

int nn_pf_put(NNPartFile *pf, uint64_t offset, const uint8_t *data, size_t len)
{
    if (offset + len > pf->size)
        return NK_EARG;
    /* AmigaDOS can't seek past the end: fill the gap with zeros */
    if (offset > pf->fsize) {
        static const uint8_t zero[4096];
        if (fseek(pf->f, (long)pf->fsize, SEEK_SET) != 0)
            return NK_EIO;
        while (pf->fsize < offset) {
            size_t k = offset - pf->fsize > sizeof(zero) ? sizeof(zero) : (size_t)(offset - pf->fsize);
            if (fwrite(zero, 1, k, pf->f) != k)
                return NK_EIO;
            pf->fsize += k;
        }
    }
    if (fseek(pf->f, (long)offset, SEEK_SET) != 0 || fwrite(data, 1, len, pf->f) != len)
        return NK_EIO;
    if (offset + len > pf->fsize)
        pf->fsize = offset + len;
    return NK_OK;
}

int nn_pf_commit(NNPartFile *pf, uint32_t part, uint64_t offset, uint64_t len, uint32_t crc)
{
    char rec[96];
    if (!part || part > pf->nparts || offset + len > pf->size)
        return NK_EARG;
    if (nn_pf_have(pf, part))
        return NK_OK;
    /* the data is on disk before the record says so */
    if (fflush(pf->f) != 0)
        return NK_EIO;
    snprintf(rec, sizeof(rec), "p %lu %llu %llu %08lx", (unsigned long)part,
             (unsigned long long)offset, (unsigned long long)len, (unsigned long)crc);
    if (nk_journal_append(pf->j, rec, strlen(rec)) != NK_OK || nk_journal_sync(pf->j) != NK_OK)
        return NK_EIO;
    mark(pf, part, offset, len, crc);
    return NK_OK;
}

int nn_pf_write(NNPartFile *pf, uint32_t part, uint64_t offset, const uint8_t *data, size_t len)
{
    int r;
    if (nn_pf_have(pf, part))
        return NK_OK;
    if ((r = nn_pf_put(pf, offset, data, len)) != NK_OK)
        return r;
    return nn_pf_commit(pf, part, offset, len, nk_crc32_update(0, data, len));
}

int nn_pf_have(const NNPartFile *pf, uint32_t part)
{
    return part && part <= pf->nparts && (pf->bits[(part - 1) / 8] & (1u << ((part - 1) % 8)));
}

uint32_t nn_pf_count(const NNPartFile *pf)
{
    return pf->count;
}

void nn_pf_expect_crc(NNPartFile *pf, uint32_t crc)
{
    char rec[16];
    if (pf->have_expect && pf->expect == crc)
        return;
    pf->expect = crc;
    pf->have_expect = 1;
    snprintf(rec, sizeof(rec), "c %08lx", (unsigned long)crc);
    nk_journal_append(pf->j, rec, strlen(rec));
}

static int by_off(const void *a, const void *b)
{
    const Held *x = (const Held *)a, *y = (const Held *)b;
    return x->off < y->off ? -1 : x->off > y->off;
}

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != 0;
}

int nn_pf_finish(NNPartFile *pf)
{
    uint64_t pos = 0;
    uint32_t crc = 0, i;
    Held *h;
    if (pf->count < pf->nparts)
        return NN_PF_INCOMPLETE;
    h = (Held *)malloc(pf->count * sizeof(Held));
    if (!h)
        return NN_PF_INCOMPLETE;
    memcpy(h, pf->held, pf->count * sizeof(Held));
    qsort(h, pf->count, sizeof(Held), by_off);
    for (i = 0; i < pf->count; i++) {
        if (h[i].off != pos) {
            free(h);
            return NN_PF_GAPS;
        }
        crc = i ? nk_crc32_combine(crc, h[i].crc, h[i].len) : h[i].crc;
        pos += h[i].len;
    }
    free(h);
    if (pos != pf->size)
        return NN_PF_GAPS;
    if (pf->have_expect && crc != pf->expect)
        return NN_PF_BADCRC;
    if (fflush(pf->f) != 0)
        return NN_PF_INCOMPLETE;
    fclose(pf->f);
    pf->f = 0;
    {
        size_t l = strlen(pf->final) + 8;
        char *name = (char *)malloc(l);
        int k;
        if (!name)
            return NN_PF_INCOMPLETE;
        strcpy(name, pf->final);
        for (k = 1; exists(name) && k < 1000; k++)
            snprintf(name, l, "%s.%d", pf->final, k);
        if (rename(pf->data_path, name) != 0) {
            free(name);
            pf->f = fopen(pf->data_path, "r+b");
            return NN_PF_INCOMPLETE;
        }
        pf->used = name;
    }
    nk_journal_close(pf->j);
    pf->j = 0;
    remove(pf->state_path);
    return NN_PF_DONE;
}

const char *nn_pf_final(const NNPartFile *pf)
{
    return pf->used ? pf->used : pf->final;
}

void nn_pf_close(NNPartFile *pf)
{
    if (!pf)
        return;
    if (pf->f)
        fclose(pf->f);
    if (pf->j)
        nk_journal_close(pf->j);
    free(pf->final);
    free(pf->data_path);
    free(pf->state_path);
    free(pf->used);
    free(pf->bits);
    free(pf->held);
    free(pf);
}
