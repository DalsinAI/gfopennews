/* NZB reader tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/nzb.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

int main(void)
{
    NNNzb n;
    static const char good[] =
        "<?xml version=\"1.0\" encoding=\"iso-8859-1\" ?>\n"
        "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.1//EN\" \"http://www.newzbin.com/DTD/nzb/nzb-1.1.dtd\">\n"
        "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n"
        " <head>\n"
        "  <meta type=\"title\">Amiga demos &amp; tools</meta>\n"
        "  <meta type=\"password\"> s&#101;cret </meta>\n"
        " </head>\n"
        " <!-- a comment <file> that must be ignored -->\n"
        " <file poster=\"Dale &lt;dale@example.org&gt;\" date=\"1763056800\"\n"
        "       subject='Demos [1/2] - &quot;demo.lha&quot; yEnc (1/3)'>\n"
        "  <groups><group>alt.binaries.amiga</group><group> alt.binaries.test </group></groups>\n"
        "  <segments>\n"
        "   <segment bytes=\"500\" number=\"3\">part3of3@news.example</segment>\n"
        "   <segment bytes=\"700\" number=\"1\">&lt;part1of3@news.example&gt;</segment>\n"
        "   <segment bytes=\"700\" number=\"2\"><![CDATA[part2of3@news.example]]></segment>\n"
        "   <segment bytes=\"700\" number=\"2\">dup@news.example</segment>\n"
        "   <segment bytes=\"1\" number=\"4\">bad id with spaces@x</segment>\n"
        "   <segment bytes=\"1\" number=\"0\">zero@x</segment>\n"
        "   <segment bytes=\"1\" number=\"5\">no-at-sign</segment>\n"
        "  </segments>\n"
        " </file>\n"
        " <file subject=\"Demos [2/2] - &quot;demo.par2&quot; yEnc (1/1)\" poster=\"p\" date=\"0\">\n"
        "  <groups><group>alt.binaries.amiga</group></groups>\n"
        "  <segments><segment bytes=\"10\" number=\"1\">par@x</segment></segments>\n"
        " </file>\n"
        "</nzb>\n";

    CHECK_EQ(nn_nzb_parse(&n, S(good)), NK_OK);
    CHECK_EQ(n.nfiles, 2);
    CHECK(n.title && strcmp(n.title, "Amiga demos & tools") == 0);
    CHECK(n.password && strcmp(n.password, "secret") == 0);
    CHECK(strcmp(n.files[0].subject, "Demos [1/2] - \"demo.lha\" yEnc (1/3)") == 0);
    CHECK(strcmp(n.files[0].poster, "Dale <dale@example.org>") == 0);
    CHECK_EQ(n.files[0].date, 1763056800u);
    CHECK_EQ(n.files[0].ngroups, 2);
    CHECK(strcmp(n.files[0].groups[1], "alt.binaries.test") == 0);
    CHECK_EQ(n.files[0].nsegs, 3);                      /* sorted, dup and bad ones out */
    CHECK(n.files[0].segs[0].number == 1 && strcmp(n.files[0].segs[0].msgid, "part1of3@news.example") == 0);
    CHECK(n.files[0].segs[1].number == 2 && strcmp(n.files[0].segs[1].msgid, "part2of3@news.example") == 0);
    CHECK(n.files[0].segs[2].number == 3 && n.files[0].segs[2].bytes == 500);
    CHECK_EQ(n.bad_segments, 3);
    CHECK_EQ(n.files[1].nsegs, 1);
    nn_nzb_free(&n);

    CHECK_EQ(nn_nzb_parse(&n, S("<html><body>not an nzb</body></html>")), NK_EPROTO);
    CHECK_EQ(nn_nzb_parse(&n, S("<nzb><file subject=\"x\"><!-- never closed")), NK_EPROTO);
    CHECK_EQ(nn_nzb_parse(&n, S("<nzb><file subject=\"x\"><segments><segment number=\"1\" bytes=\"2\">a@b</segment>")), NK_OK);
    CHECK_EQ(n.nfiles, 1);                               /* cut short: what was read stays */
    nn_nzb_free(&n);
    CHECK_EQ(nn_nzb_parse(&n, S("<nzb/>")), NK_OK);
    CHECK_EQ(n.nfiles, 0);
    nn_nzb_free(&n);

    CHECK(nn_msgid_ok("a@b"));
    CHECK(!nn_msgid_ok("a b@c"));
    CHECK(!nn_msgid_ok("a@b>\r\nQUIT"));
    CHECK(!nn_msgid_ok(""));
    CHECK(!nn_msgid_ok("<a@b>"));
    TEST_DONE();
}
