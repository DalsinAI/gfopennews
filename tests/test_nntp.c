/* NNTP core tests over netkit's scripted transport.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/nntp.h"
#include "mock_transport.h"

#define S(x) x, sizeof(x) - 1

typedef struct { int code, err, calls; char text[128]; } Rep;
static void done(void *u, int code, const char *t, size_t n, int err)
{
    Rep *r = (Rep *)u;
    r->code = code;
    r->err = err;
    r->calls++;
    if (n >= sizeof(r->text)) n = sizeof(r->text) - 1;
    memcpy(r->text, t, n);
    r->text[n] = 0;
}

typedef struct { char all[512]; size_t n; int lines, stop_after; } Lines;
static int lines(void *u, const char *l, size_t n)
{
    Lines *b = (Lines *)u;
    if (b->n + n + 1 < sizeof(b->all)) {
        memcpy(b->all + b->n, l, n);
        b->n += n;
        b->all[b->n++] = '|';
        b->all[b->n] = 0;
    }
    b->lines++;
    return b->stop_after && b->lines >= b->stop_after;
}

static int pump_all(NNClient *c)
{
    int i, r = NK_OK;
    for (i = 0; i < 100 && nn_waiting(c) && r == NK_OK; i++)
        r = nn_pump(c, 0);
    return r;
}

int main(void)
{
    Mock m;
    NNClient *c;
    Rep greet, g, a, q, x;
    Lines b;
    static const char script[] =
        "200 news.example ready\r\n"
        "211 3 1 3 comp.sys.amiga.misc\r\n"
        "220 3 <3@x> article\r\n"
        "Subject: hi\r\n"
        "\r\n"
        "..leading dot\r\n"
        "...two\r\n"
        ".\r\n"
        "423 no such article\r\n"
        "205 bye\r\n";

    memset(&m, 0, sizeof(m));
    memset(&greet, 0, sizeof(greet)); g = a = q = x = greet;
    memset(&b, 0, sizeof(b));
    m.in = script; m.in_len = sizeof(script) - 1;
    m.chunk = 7;                       /* replies split anywhere */
    c = nn_new(nk_conn_new(0, mock_transport(&m), 0), done, &greet);
    /* pipelined: all four go out before any reply is read */
    CHECK_EQ(nn_cmd(c, "GROUP comp.sys.amiga.misc", NN_SINGLE, 0, 0, done, &g), NK_OK);
    CHECK_EQ(nn_cmd(c, "ARTICLE 3", NN_MULTI, lines, &b, done, &a), NK_OK);
    CHECK_EQ(nn_cmd(c, "ARTICLE 9", NN_MULTI, lines, &b, done, &x), NK_OK);
    CHECK_EQ(nn_cmd(c, "QUIT", NN_SINGLE, 0, 0, done, &q), NK_OK);
    CHECK_EQ(nn_cmd(c, "GROUP a\r\nPOST", NN_SINGLE, 0, 0, done, &q), NK_EARG);
    CHECK_EQ(nn_waiting(c), 5);
    CHECK_EQ(pump_all(c), NK_OK);
    CHECK_STRN(m.out, m.out_len, "GROUP comp.sys.amiga.misc\r\nARTICLE 3\r\nARTICLE 9\r\nQUIT\r\n");
    CHECK_EQ(greet.code, 200);
    CHECK(strcmp(greet.text, "news.example ready") == 0);
    CHECK_EQ(g.code, 211);
    CHECK_EQ(a.code, 220);
    CHECK(strcmp(a.text, "3 <3@x> article") == 0);
    CHECK(strcmp(b.all, "Subject: hi||.leading dot|..two|") == 0);   /* dots unstuffed */
    CHECK_EQ(x.code, 423);                 /* failure: no block expected */
    CHECK_EQ(x.calls, 1);
    CHECK_EQ(q.code, 205);
    CHECK_EQ(nn_waiting(c), 0);
    nn_free(c);

    /* the line callback can stop delivery; the block is still read off */
    {
        static const char s2[] = "200 ok\r\n215 list\r\na 1 1 y\r\nb 1 1 y\r\nc 1 1 y\r\n.\r\n211 0 0 0 g\r\n";
        memset(&m, 0, sizeof(m));
        memset(&b, 0, sizeof(b));
        memset(&g, 0, sizeof(g));
        memset(&a, 0, sizeof(a));
        b.stop_after = 1;
        m.in = s2; m.in_len = sizeof(s2) - 1;
        c = nn_new(nk_conn_new(0, mock_transport(&m), 0), 0, 0);
        nn_cmd(c, "LIST", NN_MULTI, lines, &b, done, &a);
        nn_cmd(c, "GROUP g", NN_SINGLE, 0, 0, done, &g);
        CHECK_EQ(pump_all(c), NK_OK);
        CHECK_EQ(b.lines, 1);
        CHECK_EQ(a.code, 215);
        CHECK_EQ(g.code, 211);
        nn_free(c);
    }
    /* a block line over the cap is dropped and reported, the rest arrives */
    {
        static char s3[600];
        NKConnConfig cfg = { 256, 64, 1024, 0 };
        memset(s3, 0, sizeof(s3));
        strcpy(s3, "200 ok\r\n222 1 <1@x>\r\nshort\r\n");
        memset(s3 + strlen(s3), 'y', 100);
        strcat(s3, "\r\nafter\r\n.\r\n");
        memset(&m, 0, sizeof(m));
        memset(&b, 0, sizeof(b));
        memset(&a, 0, sizeof(a));
        m.in = s3; m.in_len = strlen(s3);
        c = nn_new(nk_conn_new(&cfg, mock_transport(&m), 0), 0, 0);
        nn_cmd(c, "BODY 1", NN_MULTI, lines, &b, done, &a);
        CHECK_EQ(pump_all(c), NK_OK);
        CHECK_EQ(a.code, 222);
        CHECK_EQ(a.err, NK_ETOOLONG);
        CHECK(strcmp(b.all, "short|after|") == 0);
        nn_free(c);
    }
    /* link drops mid-block: every waiting command hears about it */
    {
        static const char s4[] = "200 ok\r\n220 1 <1@x>\r\nhalf";
        memset(&m, 0, sizeof(m));
        memset(&a, 0, sizeof(a));
        memset(&q, 0, sizeof(q));
        m.in = s4; m.in_len = sizeof(s4) - 1;
        m.eof_at_end = 1;
        c = nn_new(nk_conn_new(0, mock_transport(&m), 0), 0, 0);
        nn_cmd(c, "ARTICLE 1", NN_MULTI, 0, 0, done, &a);
        nn_cmd(c, "QUIT", NN_SINGLE, 0, 0, done, &q);
        CHECK_EQ(pump_all(c), NK_EOF);
        CHECK(a.code == 0 && a.err == NK_EOF && a.calls == 1);
        CHECK(q.code == 0 && q.err == NK_EOF && q.calls == 1);
        CHECK_EQ(nn_waiting(c), 0);
        CHECK_EQ(nn_cmd(c, "QUIT", NN_SINGLE, 0, 0, 0, 0), NK_EOF);
        nn_free(c);
    }
    /* a reply nobody asked for is a protocol error */
    {
        memset(&m, 0, sizeof(m));
        mock_feed(&m, "200 ok\r\n500 surprise\r\n");
        c = nn_new(nk_conn_new(0, mock_transport(&m), 0), 0, 0);
        CHECK_EQ(nn_pump(c, 0), NK_EPROTO);
        nn_free(c);
        memset(&m, 0, sizeof(m));
        mock_feed(&m, "hello\r\n");
        memset(&greet, 0, sizeof(greet));
        c = nn_new(nk_conn_new(0, mock_transport(&m), 0), done, &greet);
        CHECK_EQ(nn_pump(c, 0), NK_EPROTO);
        CHECK(greet.calls == 1 && greet.err == NK_EPROTO);
        nn_free(c);
    }
    /* posting: dot-stuffing and the end mark */
    {
        Rep p1, p2;
        memset(&m, 0, sizeof(m));
        memset(&p1, 0, sizeof(p1));
        memset(&p2, 0, sizeof(p2));
        mock_feed(&m, "200 ok\r\n340 send article\r\n240 article posted\r\n");
        m.in_len -= strlen("240 article posted\r\n");   /* the server waits for the article */
        c = nn_new(nk_conn_new(0, mock_transport(&m), 0), 0, 0);
        nn_cmd(c, "POST", NN_SINGLE, 0, 0, done, &p1);
        CHECK_EQ(nn_pump(c, 0), NK_OK);
        CHECK_EQ(p1.code, 340);
        CHECK_EQ(nn_send_article(c, S("Subject: t\n\n.dot\nend"), done, &p2), NK_OK);
        m.in_len += strlen("240 article posted\r\n");
        CHECK_EQ(pump_all(c), NK_OK);
        CHECK_EQ(p2.code, 240);
        CHECK_STRN(m.out, m.out_len, "POST\r\nSubject: t\r\n\r\n..dot\r\nend\r\n.\r\n");
        nn_free(c);
    }
    /* parsers */
    {
        uint32_t cnt, lo, hi;
        char name[64], buf[256];
        NNActive ac;
        NNOver ov;
        CHECK(nn_parse_group(S("3 1 3 comp.sys.amiga.misc"), &cnt, &lo, &hi, name, sizeof(name)));
        CHECK(cnt == 3 && lo == 1 && hi == 3 && strcmp(name, "comp.sys.amiga.misc") == 0);
        CHECK(!nn_parse_group(S("3 1"), &cnt, &lo, &hi, name, sizeof(name)));
        CHECK(!nn_parse_group(S("99999999999 1 3 g"), &cnt, &lo, &hi, name, sizeof(name)));
        CHECK(nn_parse_active(S("alt.test 0000012345 0000000002 m"), &ac));
        CHECK(ac.high == 12345 && ac.low == 2 && ac.status == 'm' && strcmp(ac.name, "alt.test") == 0);
        CHECK(!nn_parse_active(S("broken"), &ac));
        CHECK(nn_parse_over(S("7\tRe: hi\ta@b\t13 Nov 2026\t<7@x>\t<1@x> <3@x>\t1234\t20\tXref: srv g:7"),
                            &ov, buf, sizeof(buf)));
        CHECK(ov.number == 7 && ov.bytes == 1234 && ov.lines == 20);
        CHECK(strcmp(ov.subject, "Re: hi") == 0 && strcmp(ov.references, "<1@x> <3@x>") == 0);
        CHECK(strcmp(ov.xref, "srv g:7") == 0);
        CHECK(nn_parse_over(S("8\ts\tf\td\t<8@x>\t\t\t"), &ov, buf, sizeof(buf)));
        CHECK(ov.bytes == 0 && ov.lines == 0 && ov.references[0] == 0 && ov.xref[0] == 0);
        CHECK(!nn_parse_over(S("x\ts\tf\td\tm\tr\t1\t2"), &ov, buf, sizeof(buf)));
        CHECK(!nn_parse_over(S("9\tshort"), &ov, buf, sizeof(buf)));
        CHECK(!nn_parse_over(S("9\ts\tf\td\tm\tr\t1\t2"), &ov, buf, 5));
    }
    TEST_DONE();
}
