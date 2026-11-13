/* yEnc, UU, base64, subject and part file tests.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/binaries.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

/* ---- a yEnc encoder for the tests (lines of `width` before escapes) ---- */
static size_t ye_encode(const uint8_t *d, size_t n, int width, char *out, int esc_dot)
{
    size_t o = 0, i;
    int col = 0;
    for (i = 0; i < n; i++) {
        uint8_t c = (uint8_t)(d[i] + 42);
        int esc = c == 0 || c == '\n' || c == '\r' || c == '=' ||
                  (esc_dot && col == 0 && c == '.') || (col == 0 && (c == ' ' || c == '\t'));
        if (esc) {
            out[o++] = '=';
            out[o++] = (char)(c + 64);
        } else
            out[o++] = (char)c;
        col++;
        if (col >= width && i + 1 < n) {
            out[o++] = '\n';
            col = 0;
        }
    }
    out[o++] = '\n';
    return o;
}

/* feeds LF-separated text to a yEnc decoder */
static void feed_ye(NNYenc *y, const char *t, size_t n)
{
    size_t i = 0;
    while (i < n) {
        const char *lf = memchr(t + i, '\n', n - i);
        size_t l = lf ? (size_t)(lf - (t + i)) : n - i;
        nn_ye_line(y, t + i, l);
        i += l + 1;
    }
}

static uint8_t sink[70000];
static size_t sink_hi;
static int to_sink(void *u, uint64_t off, const uint8_t *d, size_t n)
{
    (void)u;
    if (off + n > sizeof(sink)) return -1;
    memcpy(sink + off, d, n);
    if (off + n > sink_hi) sink_hi = off + n;
    return 0;
}
static int fail_write(void *u, uint64_t off, const uint8_t *d, size_t n)
{
    (void)u; (void)off; (void)d; (void)n;
    return -1;
}

static int to_pf(void *u, uint64_t off, const uint8_t *d, size_t n)
{
    return nn_pf_put((NNPartFile *)u, off, d, n) == NK_OK ? 0 : -1;
}

static uint8_t data[60000];
static char enc[200000];

static void make_part(char *out, size_t *on, uint32_t part, uint32_t total,
                      size_t b, size_t e, uint32_t whole_crc, int width)
{
    size_t o = (size_t)sprintf(out, "Some text before the post\n=ybegin part=%lu total=%lu line=%d size=%lu name=big file.bin\n"
                               "=ypart begin=%lu end=%lu\n", (unsigned long)part, (unsigned long)total,
                               width, (unsigned long)sizeof(data), (unsigned long)b, (unsigned long)e);
    o += ye_encode(data + b - 1, e - b + 1, width, out + o, 1);
    o += (size_t)sprintf(out + o, "=yend size=%lu part=%lu pcrc32=%08lx crc32=%08lx\n",
                         (unsigned long)(e - b + 1), (unsigned long)part,
                         (unsigned long)nk_crc32_update(0, data + b - 1, e - b + 1),
                         (unsigned long)whole_crc);
    *on = o;
}

int main(int argc, char **argv)
{
    NNYenc y;
    size_t i, n;
    uint32_t seed = 7, whole;
    char path[512], tmp[600];
    const char *dir = argc > 1 ? argv[1] : ".";

    for (i = 0; i < sizeof(data); i++) {
        seed = seed * 1664525u + 1013904223u;
        data[i] = (uint8_t)(seed >> 24);
    }
    /* every critical byte value, and runs that end a line with an escape */
    for (i = 0; i < 256; i++)
        data[i] = (uint8_t)i;
    for (i = 256; i < 400; i++)
        data[i] = (uint8_t)('=' - 42);
    whole = nk_crc32_update(0, data, sizeof(data));

    /* single-part post */
    n = (size_t)sprintf(enc, "=ybegin line=128 size=%lu name=  spaced name.iso  \n", (unsigned long)sizeof(data));
    n += ye_encode(data, sizeof(data), 128, enc + n, 1);
    n += (size_t)sprintf(enc + n, "=yend size=%lu crc32=%08lx\n", (unsigned long)sizeof(data), (unsigned long)whole);
    nn_ye_init(&y, to_sink, 0);
    sink_hi = 0;
    feed_ye(&y, enc, n);
    CHECK_EQ(nn_ye_finish(&y), NN_YE_OK);
    CHECK(strcmp(y.name, " spaced name.iso") == 0 || strcmp(y.name, "  spaced name.iso") == 0);
    CHECK_EQ(y.got, sizeof(data));
    CHECK_EQ(sink_hi, sizeof(data));
    CHECK(memcmp(sink, data, sizeof(data)) == 0);
    CHECK_EQ(y.crc, whole);
    /* wrong whole CRC */
    n = (size_t)sprintf(enc, "=ybegin line=128 size=10 name=x\n");
    n += ye_encode(data, 10, 128, enc + n, 1);
    n += (size_t)sprintf(enc + n, "=yend size=10 crc32=deadbeef\n");
    nn_ye_init(&y, to_sink, 0);
    feed_ye(&y, enc, n);
    CHECK_EQ(nn_ye_finish(&y), NN_YE_CRC);
    /* wrong size */
    n = (size_t)sprintf(enc, "=ybegin line=128 size=10 name=x\n");
    n += ye_encode(data, 9, 128, enc + n, 1);
    n += (size_t)sprintf(enc + n, "=yend size=10\n");
    nn_ye_init(&y, to_sink, 0);
    feed_ye(&y, enc, n);
    CHECK_EQ(nn_ye_finish(&y), NN_YE_SIZE);
    /* cut short */
    n = (size_t)sprintf(enc, "=ybegin line=128 size=10 name=x\n");
    n += ye_encode(data, 5, 128, enc + n, 1);
    nn_ye_init(&y, to_sink, 0);
    feed_ye(&y, enc, n);
    CHECK_EQ(nn_ye_finish(&y), NN_YE_NOEND);
    /* no yEnc at all, a bad header, a name holding " size=" */
    nn_ye_init(&y, to_sink, 0);
    feed_ye(&y, S("just text\nmore\n"));
    CHECK_EQ(nn_ye_finish(&y), NN_YE_NOBEGIN);
    nn_ye_init(&y, to_sink, 0);
    feed_ye(&y, S("=ybegin line=128 name=nosize\n"));
    CHECK_EQ(nn_ye_finish(&y), NN_YE_BADHEADER);
    nn_ye_init(&y, to_sink, 0);
    n = (size_t)sprintf(enc, "=ybegin line=128 size=3 name=a size=999 b\n");
    n += ye_encode(data + 300, 3, 128, enc + n, 1);
    n += (size_t)sprintf(enc + n, "=yend size=3\n");
    feed_ye(&y, enc, n);
    CHECK_EQ(nn_ye_finish(&y), NN_YE_OK);
    CHECK(y.size == 3 && strcmp(y.name, "a size=999 b") == 0);
    /* =ypart past the end of the file */
    nn_ye_init(&y, to_sink, 0);
    feed_ye(&y, S("=ybegin part=1 line=128 size=10 name=x\n=ypart begin=5 end=11\n"));
    CHECK_EQ(nn_ye_finish(&y), NN_YE_BADHEADER);
    /* the write callback fails */
    nn_ye_init(&y, fail_write, 0);
    n = (size_t)sprintf(enc, "=ybegin line=128 size=10 name=x\n");
    n += ye_encode(data, 10, 128, enc + n, 1);
    n += (size_t)sprintf(enc + n, "=yend size=10\n");
    feed_ye(&y, enc, n);
    CHECK_EQ(nn_ye_finish(&y), NN_YE_WRITE);

    /* multi-part, parts arriving out of order, into a part file */
    {
        static const size_t cut[] = { 1, 20001, 40001, sizeof(data) + 1 };
        static const int order[] = { 3, 1, 2 };
        NNPartFile *pf;
        int e, k;
        FILE *f;
        snprintf(path, sizeof(path), "%s/big file.bin", dir);
        remove(path);
        snprintf(tmp, sizeof(tmp), "%s.1", path); remove(tmp);
        snprintf(tmp, sizeof(tmp), "%s.part", path); remove(tmp);
        snprintf(tmp, sizeof(tmp), "%s.parts", path); remove(tmp);
        pf = nn_pf_open(path, sizeof(data), 3, &e);
        CHECK(pf && e == NK_OK);
        for (k = 0; k < 3; k++) {
            int p = order[k];
            make_part(enc, &n, (uint32_t)p, 3, cut[p - 1], cut[p] - 1, whole, 61 + k * 64);
            nn_ye_init(&y, to_pf, pf);
            feed_ye(&y, enc, n);
            CHECK_EQ(nn_ye_finish(&y), NN_YE_OK);
            CHECK_EQ(y.part, (uint32_t)p);
            CHECK_EQ(y.total, 3);
            CHECK(strcmp(y.name, "big file.bin") == 0);
            CHECK_EQ(nn_pf_commit(pf, y.part, y.begin - 1, y.got, y.crc), NK_OK);
            if (y.have & NN_YE_HAVE_CRC)
                nn_pf_expect_crc(pf, y.crc32);
            if (k == 1) {
                /* restart in the middle: the parts held survive */
                nn_pf_close(pf);
                pf = nn_pf_open(path, sizeof(data), 3, &e);
                CHECK(pf != 0);
                CHECK_EQ(nn_pf_count(pf), 2);
                CHECK(nn_pf_have(pf, 3) && nn_pf_have(pf, 1) && !nn_pf_have(pf, 2));
                CHECK_EQ(nn_pf_finish(pf), NN_PF_INCOMPLETE);
            }
        }
        CHECK_EQ(nn_pf_write(pf, 1, 0, data, 5), NK_OK);   /* held already: skipped */
        CHECK_EQ(nn_pf_finish(pf), NN_PF_DONE);
        CHECK(strcmp(nn_pf_final(pf), path) == 0);
        nn_pf_close(pf);
        f = fopen(path, "rb");
        CHECK(f != 0);
        if (f) {
            n = fread(sink, 1, sizeof(sink), f);
            fclose(f);
            CHECK_EQ(n, sizeof(data));
            CHECK(memcmp(sink, data, sizeof(data)) == 0);
        }
        snprintf(tmp, sizeof(tmp), "%s.parts", path);
        CHECK((f = fopen(tmp, "rb")) == 0);

        /* the same name again: a numbered name, and the whole-file CRC
         * checked without reading the data back */
        pf = nn_pf_open(path, 10, 2, &e);
        CHECK_EQ(nn_pf_write(pf, 2, 5, data + 5, 5), NK_OK);
        CHECK_EQ(nn_pf_write(pf, 1, 0, data, 5), NK_OK);
        nn_pf_expect_crc(pf, 0x12345678);
        CHECK_EQ(nn_pf_finish(pf), NN_PF_BADCRC);
        nn_pf_expect_crc(pf, nk_crc32_update(0, data, 10));
        CHECK_EQ(nn_pf_finish(pf), NN_PF_DONE);
        snprintf(tmp, sizeof(tmp), "%s.1", path);
        CHECK(strcmp(nn_pf_final(pf), tmp) == 0);
        nn_pf_close(pf);
        remove(tmp);

        /* a part carrying more than its =ypart range must not write over
         * the next part (Franklin's repro): part 2 "DEF" is held, then a
         * part 1 for 1-3 brings 6 bytes */
        {
            static const uint8_t def[3] = { 'D', 'E', 'F' }, six[6] = { 'A', 'B', 'C', '0', '0', '0' };
            char t2[700], line[64];
            snprintf(tmp, sizeof(tmp), "%s/overrun", dir);
            snprintf(t2, sizeof(t2), "%s.part", tmp); remove(t2);
            snprintf(t2, sizeof(t2), "%s.parts", tmp); remove(t2);
            pf = nn_pf_open(tmp, 6, 2, &e);
            CHECK(pf != 0);
            CHECK_EQ(nn_pf_write(pf, 2, 3, def, 3), NK_OK);
            nn_ye_init(&y, to_pf, pf);
            feed_ye(&y, S("=ybegin part=1 total=2 line=128 size=6 name=overrun\n=ypart begin=1 end=3\n"));
            n = ye_encode(six, 6, 128, line, 1);
            feed_ye(&y, line, n);
            feed_ye(&y, S("=yend size=6 part=1\n"));
            CHECK_EQ(nn_ye_finish(&y), NN_YE_SIZE);
            CHECK_EQ(y.got, 0);                   /* nothing written at all */
            nn_pf_close(pf);
            snprintf(t2, sizeof(t2), "%s.part", tmp);
            f = fopen(t2, "rb");
            CHECK(f != 0);
            if (f) {
                char got[8] = { 0 };
                size_t k = fread(got, 1, sizeof(got), f);
                fclose(f);
                CHECK_EQ(k, 6);
                CHECK(memcmp(got + 3, "DEF", 3) == 0);   /* part 2 untouched */
            }
            remove(t2);
            snprintf(t2, sizeof(t2), "%s.parts", tmp); remove(t2);
            /* a single-part post longer than its size= is refused the same way */
            nn_ye_init(&y, to_sink, 0);
            feed_ye(&y, S("=ybegin line=128 size=3 name=x\n"));
            n = ye_encode(six, 6, 128, line, 1);
            feed_ye(&y, line, n);
            feed_ye(&y, S("=yend size=6\n"));
            CHECK_EQ(nn_ye_finish(&y), NN_YE_SIZE);
            CHECK_EQ(y.got, 0);
        }
        /* sizes from headers are capped: no terabyte of zeros, no 32-bit
         * long overflow in fseek on the Amiga */
        snprintf(tmp, sizeof(tmp), "%s/huge", dir);
        CHECK(nn_pf_open(tmp, 1000000000000ull, 2, &e) == 0 && e == NK_EARG);
        CHECK(nn_pf_open(tmp, NN_PF_MAX, 2, &e) == 0 && e == NK_EARG);

        /* parts that leave a hole */
        snprintf(tmp, sizeof(tmp), "%s/gappy", dir);
        { char t2[700]; snprintf(t2, sizeof(t2), "%s.part", tmp); remove(t2);
          snprintf(t2, sizeof(t2), "%s.parts", tmp); remove(t2); }
        pf = nn_pf_open(tmp, 10, 2, &e);
        CHECK_EQ(nn_pf_write(pf, 1, 0, data, 4), NK_OK);
        CHECK_EQ(nn_pf_write(pf, 2, 5, data + 5, 5), NK_OK);   /* byte 4 missing */
        CHECK_EQ(nn_pf_finish(pf), NN_PF_GAPS);
        CHECK_EQ(nn_pf_write(pf, 3, 0, data, 1), NK_EARG);      /* no part 3 */
        CHECK_EQ(nn_pf_put(pf, 8, data, 5), NK_EARG);          /* past the end */
        nn_pf_close(pf);
        /* the data file went away: the state starts again */
        { char t2[700]; snprintf(t2, sizeof(t2), "%s.part", tmp); remove(t2); }
        pf = nn_pf_open(tmp, 10, 2, &e);
        CHECK(pf && nn_pf_count(pf) == 0);
        nn_pf_close(pf);
        pf = nn_pf_open(tmp, 10, 2, &e);
        CHECK(pf && nn_pf_count(pf) == 0);
        nn_pf_close(pf);
        /* same name, different file */
        pf = nn_pf_open(tmp, 11, 2, &e);
        CHECK(pf == 0 && e == NK_EARG);
        { char t2[700]; snprintf(t2, sizeof(t2), "%s.part", tmp); remove(t2);
          snprintf(t2, sizeof(t2), "%s.parts", tmp); remove(t2); }
        remove(path);
    }

    /* UUencode */
    {
        NNUu u;
        static const char uu[] =
            "some preamble\n"
            "begin 644 cat.txt\n"
            "#0V%T\n"
            "`\n"
            "end\n";
        nn_uu_init(&u, to_sink, 0);
        sink_hi = 0;
        for (i = 0; i < sizeof(uu) - 1;) {
            const char *lf = memchr(uu + i, '\n', sizeof(uu) - 1 - i);
            size_t l = (size_t)(lf - (uu + i));
            nn_uu_line(&u, uu + i, l);
            i += l + 1;
        }
        CHECK(nn_uu_done(&u));
        CHECK_EQ(u.mode, 0644);
        CHECK(strcmp(u.name, "cat.txt") == 0);
        CHECK_EQ(u.got, 3);
        CHECK(memcmp(sink, "Cat", 3) == 0);
        /* a full 45-byte line and a short last line, trailing pad cut */
        nn_uu_init(&u, to_sink, 0);
        nn_uu_line(&u, S("begin 600 x"));
        nn_uu_line(&u, S("M5&AE(&-A=\"!S870@;VX@=&AE(&UA=\"P@=&AE(&-A=\"!S870@;VX@=&AE(&UA"));
        nn_uu_line(&u, S("!="));
        nn_uu_line(&u, S("end"));
        CHECK(nn_uu_done(&u));
        CHECK_EQ(u.got, 46);
        CHECK(memcmp(sink, "The cat sat on the mat, the cat sat on the mat", 46) == 0);
        CHECK_EQ(u.bad_lines, 0);
        /* names from "begin" and "=ybegin" are never paths either */
        nn_uu_init(&u, to_sink, 0);
        nn_uu_line(&u, S("begin 644 DH0:C/Dir"));
        CHECK(strcmp(u.name, "Dir") == 0);
        nn_ye_init(&y, to_sink, 0);
        feed_ye(&y, S("=ybegin line=128 size=1 name=../../S/Startup-Sequence\n"));
        CHECK(strcmp(y.name, "Startup-Sequence") == 0);
        nn_ye_init(&y, to_sink, 0);
        feed_ye(&y, S("=ybegin line=128 size=1 name=..\n"));
        CHECK(y.name[0] == 0);
    }
    /* base64 */
    {
        NNB64 b;
        nn_b64_init(&b, to_sink, 0);
        CHECK_EQ(nn_b64_line(&b, S("TWFuIGlz")), NK_OK);
        CHECK_EQ(nn_b64_line(&b, S(" IGEg\r")), NK_OK);
        CHECK_EQ(nn_b64_line(&b, S("bWF0Lg==")), NK_OK);
        CHECK_EQ(nn_b64_line(&b, S("ignored after the padding")), NK_OK);
        CHECK_EQ(b.got, 13);
        CHECK(memcmp(sink, "Man is a mat.", 13) == 0);
    }
    /* subjects */
    {
        char name[64];
        uint32_t p, t;
        CHECK(nn_bin_subject("[1/9] - \"amiga.lha\" yEnc (3/40)", name, sizeof(name), &p, &t));
        CHECK(p == 3 && t == 40 && strcmp(name, "amiga.lha") == 0);
        CHECK(nn_bin_subject("demo.adf yEnc (01/12)", name, sizeof(name), &p, &t));
        CHECK(p == 1 && t == 12 && strcmp(name, "demo.adf") == 0);
        CHECK(nn_bin_subject("\"../../S/Startup-Sequence\" [2/2]", name, sizeof(name), &p, &t));
        CHECK(strcmp(name, "Startup-Sequence") == 0);       /* never a path */
        CHECK(nn_bin_subject("\"DH0:C/Dir\" (1/1)", name, sizeof(name), &p, &t));
        CHECK(strcmp(name, "Dir") == 0);
        CHECK(!nn_bin_subject("Re: plain talk (not/parts)", name, sizeof(name), &p, &t));
        CHECK(!nn_bin_subject("bad (5/3)", name, sizeof(name), &p, &t));
        CHECK(!nn_bin_subject("\"..\" yEnc", name, sizeof(name), &p, &t) && name[0] == 0);
    }
    TEST_DONE();
}
