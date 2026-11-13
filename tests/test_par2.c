/* PAR2 unit tests: MD5, GF(2^16), slice constants, volume names, picking
 * volumes, and reading packets (good, damaged, another set). The whole
 * verify/repair path runs in test_par2.py against sets made by an
 * independent Python writer. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#include "gfopennews/par2.h"
#include "nk_test.h"

static void md5hex(const char *s, size_t n, char out[33])
{
    NNMd5 m;
    uint8_t d[16];
    int i;
    nn_md5_init(&m);
    nn_md5_update(&m, s, n);
    nn_md5_final(&m, d);
    for (i = 0; i < 16; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

/* one packet: header + body (body length a multiple of 4) */
static size_t packet(uint8_t *out, const uint8_t set[16], const char type[16],
                     const uint8_t *body, size_t bl)
{
    NNMd5 m;
    memcpy(out, "PAR2\0PKT", 8);
    put64(out + 8, 64 + bl);
    memcpy(out + 32, set, 16);
    memcpy(out + 48, type, 16);
    memcpy(out + 64, body, bl);
    nn_md5_init(&m);
    nn_md5_update(&m, out + 32, 32 + bl);
    nn_md5_final(&m, out + 16);
    return 64 + bl;
}

#define T_MAIN "PAR 2.0\0Main\0\0\0\0"
#define T_DESC "PAR 2.0\0FileDesc"
#define T_IFSC "PAR 2.0\0IFSC\0\0\0\0"
#define T_RECV "PAR 2.0\0RecvSlic"

static size_t mk_main(uint8_t *out, const uint8_t set[16], uint64_t slice,
                      const uint8_t (*ids)[16], uint32_t n)
{
    static uint8_t b[12 + 16 * 8];
    uint32_t i;
    put64(b, slice);
    put32(b + 8, n);
    for (i = 0; i < n; i++)
        memcpy(b + 12 + 16 * i, ids[i], 16);
    return packet(out, set, T_MAIN, b, 12 + 16 * n);
}

static size_t mk_desc(uint8_t *out, const uint8_t set[16], const uint8_t id[16],
                      uint64_t length, const char *name)
{
    static uint8_t b[56 + 64];
    size_t nl = strlen(name), bl = 56 + ((nl + 3) & ~(size_t)3);
    memset(b, 0, sizeof(b));
    memcpy(b, id, 16);
    put64(b + 48, length);
    memcpy(b + 56, name, nl);
    return packet(out, set, T_DESC, b, bl);
}

static int write_file(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(d, 1, n, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    char h[33], path[512];
    const char *dir = argc > 1 ? argv[1] : ".";

    /* MD5: RFC 1321 test suite, and a run over several blocks in pieces */
    md5hex("", 0, h);
    CHECK(!strcmp(h, "d41d8cd98f00b204e9800998ecf8427e"));
    md5hex("abc", 3, h);
    CHECK(!strcmp(h, "900150983cd24fb0d6963f7d28e17f72"));
    md5hex("message digest", 14, h);
    CHECK(!strcmp(h, "f96b697d7cb7938d525a2f31aaf161d0"));
    {
        static const char s[] = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
        NNMd5 m;
        uint8_t d1[16], d2[16];
        size_t i;
        md5hex(s, 80, h);
        CHECK(!strcmp(h, "57edf4a22be3c955ac49da2e2107b67a"));
        nn_md5_init(&m);
        nn_md5_update(&m, s, 80);
        nn_md5_final(&m, d1);
        nn_md5_init(&m);
        for (i = 0; i < 80; i += 7)
            nn_md5_update(&m, s + i, i + 7 > 80 ? 80 - i : 7);
        nn_md5_final(&m, d2);
        CHECK(!memcmp(d1, d2, 16));
    }

    /* GF(2^16) with 0x1100B */
    CHECK_EQ(nn_gf_mul(0x8000, 2), 0x100B);
    CHECK_EQ(nn_gf_mul(0x1234, 0x5678), 0x6324);
    CHECK_EQ(nn_gf_mul(0, 0x5678), 0);
    CHECK_EQ(nn_gf_pow(2, 65535), 1);
    CHECK_EQ(nn_gf_pow(3, 100), 0xB903);
    CHECK_EQ(nn_gf_pow(0, 0), 1);
    CHECK_EQ(nn_gf_div(nn_gf_mul(0x1234, 0x5678), 0x5678), 0x1234);
    {
        uint32_t a;
        int ok = 1;
        for (a = 1; a < 65536; a += 251)
            if (nn_gf_mul((uint16_t)a, nn_gf_div(1, (uint16_t)a)) != 1) ok = 0;
        CHECK(ok);
    }
    /* constants: 2^n for n = 1, 2, 4, 7, 8, 11, 13, 14 (skipping 3, 5, 6, ...) */
    {
        static const uint16_t want[8] = { 2, 4, 0x10, 0x80, 0x100, 0x800, 0x2000, 0x4000 };
        int i;
        for (i = 0; i < 8; i++)
            CHECK_EQ(nn_par2_const((uint32_t)i), want[i]);
    }

    /* volume names */
    CHECK_EQ(nn_par2_vol_blocks("x.vol07+08.par2"), 8);
    CHECK_EQ(nn_par2_vol_blocks("Demo.part1.vol00+1.PAR2"), 1);
    CHECK_EQ(nn_par2_vol_blocks("x.par2"), 0);
    CHECK_EQ(nn_par2_vol_blocks("x.volume.par2"), 0);

    /* picking: the fewest blocks that reach the need */
    {
        uint32_t b[5] = { 1, 2, 4, 8, 16 };
        uint8_t pick[5];
        CHECK_EQ(nn_par2_pick(b, 5, 6, pick), 6);
        CHECK(!pick[0] && pick[1] && pick[2] && !pick[3] && !pick[4]);
        CHECK_EQ(nn_par2_pick(b, 5, 9, pick), 9);
        CHECK(pick[0] && pick[3] && !pick[1]);
        CHECK_EQ(nn_par2_pick(b, 5, 40, pick), 31);   /* not enough: all */
        CHECK(pick[0] && pick[4]);
        CHECK_EQ(nn_par2_pick(b, 5, 0, pick), 0);
        CHECK(!pick[0] && !pick[4]);
    }

    /* packets: main + one file description; a damaged copy and a packet
     * of another set are skipped */
    {
        static uint8_t buf[4096];
        uint8_t set[16], other[16], body[1100], id[16];
        size_t n = 0, bl;
        FILE *f;
        NNPar2 p;
        memset(set, 0x11, 16);
        memset(other, 0x22, 16);
        memset(id, 0x33, 16);
        put64(body, 1024);
        put32(body + 8, 1);
        memcpy(body + 12, id, 16);
        n += packet(buf + n, set, "PAR 2.0\0Main\0\0\0\0", body, 28);
        memset(body, 0, sizeof(body));
        memcpy(body, id, 16);
        put64(body + 48, 3000);
        memcpy(body + 56, "dir/file.bin", 12);   /* a path: only the name is kept */
        bl = 68;
        /* first a damaged copy (one byte changed after the checksum) */
        n += packet(buf + n, set, "PAR 2.0\0FileDesc", body, bl);
        buf[n - 1] ^= 1;
        n += packet(buf + n, other, "PAR 2.0\0FileDesc", body, bl);
        n += packet(buf + n, set, "PAR 2.0\0FileDesc", body, bl);
        memcpy(buf + n, "junk", 4);               /* garbage between packets */
        n += 4;
        put32(body, 5);
        n += packet(buf + n, set, "PAR 2.0\0RecvSlic", body, 4 + 1024);
        snprintf(path, sizeof(path), "%s/t.par2", dir);
        f = fopen(path, "wb");
        CHECK(f != 0);
        fwrite(buf, 1, n, f);
        fclose(f);
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_OK);
        CHECK_EQ(p.slice, 1024);
        CHECK_EQ(p.nfiles, 1);
        CHECK_EQ(p.files[0].length, 3000);
        CHECK(!strcmp(p.files[0].name, "file.bin"));
        CHECK_EQ(p.files[0].nslices, 3);
        CHECK_EQ(p.total_slices, 3);
        CHECK_EQ(p.bad_packets, 1);
        CHECK_EQ(p.nrec, 1);
        CHECK_EQ(p.rec[0].exponent, 5);
        /* loading again adds no second copy of the same block */
        CHECK_EQ(nn_par2_load(&p, path), NK_OK);
        CHECK_EQ(p.nrec, 1);
        /* the file isn't there: every slice is damaged; with one block
         * repair says there are too few */
        CHECK_EQ(nn_par2_verify(&p, dir), 3);
        CHECK_EQ(p.files[0].present, 0);
        CHECK_EQ(nn_par2_repair(&p, dir), NK_EARG);
        nn_par2_free(&p);
        CHECK_EQ(p.nfiles, 0);
        /* no main packet: not a PAR2 set */
        f = fopen(path, "wb");
        fwrite("not a par2 file at all, sorry", 1, 28, f);
        fclose(f);
        CHECK_EQ(nn_par2_load(&p, path), NK_EPROTO);
        nn_par2_free(&p);
        snprintf(path, sizeof(path), "%s/no-such.par2", dir);
        CHECK_EQ(nn_par2_load(&p, path), NK_EIO);
        nn_par2_free(&p);
    }
    /* crafted packets (Kochanski's review of #4) */
    {
        static uint8_t buf[8192];
        uint8_t set[16], other[16], ids[2][16], b[64];
        size_t n;
        NNPar2 p;
        snprintf(path, sizeof(path), "%s/c.par2", dir);
        memset(set, 0x41, 16);
        memset(other, 0x42, 16);
        memset(ids[0], 0x43, 16);
        memset(ids[1], 0x43, 16);

        /* an IFSC shorter than the file's slices: no over-read in verify */
        n = mk_main(buf, set, 4, ids, 1);
        n += mk_desc(buf + n, set, ids[0], 400, "short.bin");
        memset(b, 0, sizeof(b));
        memcpy(b, ids[0], 16);
        n += packet(buf + n, set, T_IFSC, b, 16 + 20);
        CHECK(write_file(path, buf, n));
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_OK);
        CHECK_EQ(p.files[0].nslices, 100);
        CHECK_EQ(p.files[0].nifsc, 1);
        {
            char fp[600];
            static uint8_t z[400];
            snprintf(fp, sizeof(fp), "%s/short.bin", dir);
            CHECK(write_file(fp, z, sizeof(z)));
            CHECK(nn_par2_verify(&p, dir) >= 99);   /* slices past the IFSC are unknown */
        }
        nn_par2_free(&p);

        /* a main packet naming the same file twice is refused, and what
         * came before it is freed once */
        n = mk_desc(buf, set, ids[0], 8, "dup.bin");
        memset(b, 0, sizeof(b));
        memcpy(b, ids[0], 16);
        n += packet(buf + n, set, T_IFSC, b, 16 + 40);
        n += mk_main(buf + n, set, 4, ids, 2);
        CHECK(write_file(path, buf, n));
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_EPROTO);
        CHECK_EQ(p.have_main, 0);
        nn_par2_free(&p);

        /* a slice over NN_PAR2_MAXSLICE, and a set over NN_PAR2_MAXSLICES */
        n = mk_main(buf, set, (uint64_t)NN_PAR2_MAXSLICE * 2, ids, 1);
        CHECK(write_file(path, buf, n));
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_EPROTO);
        nn_par2_free(&p);
        n = mk_main(buf, set, 4, ids, 1);
        n += mk_desc(buf + n, set, ids[0], 4ull * NN_PAR2_MAXSLICES + 4, "huge.bin");
        CHECK(write_file(path, buf, n));
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_EPROTO);
        CHECK(p.too_big);
        CHECK_EQ(nn_par2_verify(&p, dir), 0xffffffffu);
        CHECK_EQ(nn_par2_repair(&p, dir), NK_EPROTO);
        nn_par2_free(&p);
        {
            /* a length whose slice count does not fit 32 bits */
            n = mk_main(buf, set, 4, ids, 1);
            n += mk_desc(buf + n, set, ids[0], 0xfffffffffffffff0ull, "wrap.bin");
            CHECK(write_file(path, buf, n));
            memset(&p, 0, sizeof(p));
            CHECK_EQ(nn_par2_load(&p, path), NK_EPROTO);
            nn_par2_free(&p);
        }

        /* recovery packets: a short one before main, one of another set
         * before main, a short one after main: none kept */
        memset(b, 0, sizeof(b));
        put32(b, 1);
        n = packet(buf, set, T_RECV, b, 4 + 4);
        put32(b, 2);
        n += packet(buf + n, other, T_RECV, b, 4 + 8);
        n += mk_main(buf + n, set, 8, ids, 1);
        n += mk_desc(buf + n, set, ids[0], 8, "r.bin");
        put32(b, 3);
        n += packet(buf + n, set, T_RECV, b, 4 + 4);
        put32(b, 4);
        n += packet(buf + n, set, T_RECV, b, 4 + 8);   /* the right size: kept */
        CHECK(write_file(path, buf, n));
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_OK);
        CHECK_EQ(p.nrec, 1);
        CHECK(p.nrec == 1 && p.rec[0].exponent == 4 && p.rec[0].len == 8);
        nn_par2_free(&p);

        /* a description named ".." is not taken */
        n = mk_main(buf, set, 4, ids, 1);
        n += mk_desc(buf + n, set, ids[0], 4, "..");
        CHECK(write_file(path, buf, n));
        memset(&p, 0, sizeof(p));
        CHECK_EQ(nn_par2_load(&p, path), NK_OK);
        CHECK(!p.files[0].name[0]);
        CHECK_EQ(nn_par2_repair(&p, dir), NK_EPROTO);
        nn_par2_free(&p);

        /* a rebuilt slice whose MD5 is wrong (bad recovery data) is not
         * written, and repair says so */
        {
            static const uint8_t data[4] = { 'a', 'b', 'c', 'd' };
            char fp[600];
            NNMd5 m;
            n = mk_main(buf, set, 4, ids, 1);
            n += mk_desc(buf + n, set, ids[0], 4, "md5.bin");
            memset(b, 0, sizeof(b));
            memcpy(b, ids[0], 16);
            nn_md5_init(&m);
            nn_md5_update(&m, data, 4);
            nn_md5_final(&m, b + 16);
            put32(b + 32, nk_crc32_update(0, data, 4));
            n += packet(buf + n, set, T_IFSC, b, 36);
            memset(b, 0, sizeof(b));
            put32(b, 1);
            memcpy(b + 4, "zzzz", 4);
            n += packet(buf + n, set, T_RECV, b, 8);
            CHECK(write_file(path, buf, n));
            snprintf(fp, sizeof(fp), "%s/md5.bin", dir);
            remove(fp);
            memset(&p, 0, sizeof(p));
            CHECK_EQ(nn_par2_load(&p, path), NK_OK);
            CHECK_EQ(nn_par2_verify(&p, dir), 1);
            CHECK_EQ(nn_par2_repair(&p, dir), NK_EPROTO);
            CHECK_EQ(p.files[0].damaged, 1);
            nn_par2_free(&p);
        }
    }
    TEST_DONE();
}
