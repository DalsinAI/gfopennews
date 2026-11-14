/* Kill and score file tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/score.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

static NNOver art(const char *subj, const char *from, uint32_t lines, uint32_t bytes)
{
    NNOver o;
    memset(&o, 0, sizeof(o));
    o.number = 1;
    o.subject = subj;
    o.from = from;
    o.msgid = "<1@x.example>";
    o.references = "";
    o.xref = "";
    o.lines = lines;
    o.bytes = bytes;
    return o;
}

int main(void)
{
    NNScore *s;
    NNOver o;
    unsigned bad;
    char big[64];
    size_t i;

    /* the pattern match */
    CHECK(nn_score_glob("*a1200*", "Re: My A1200 trapdoor"));
    CHECK(nn_score_glob("A1200", "a1200"));
    CHECK(!nn_score_glob("A1200", "my a1200"));            /* the whole value */
    CHECK(nn_score_glob("spam?er@*", "spammer@example.invalid"));
    CHECK(!nn_score_glob("spam?er@*", "spamer@example.invalid"));
    CHECK(nn_score_glob("*", ""));
    CHECK(!nn_score_glob("?", ""));
    CHECK(nn_score_glob("**a**b**", "xxaxxbxx"));
    CHECK(!nn_score_glob("*a*b", "xxbxxa"));
    CHECK(!nn_score_glob(0, "x"));

    s = nn_score_parse(S(
        "# rules for every group\n"
        "score -5 from: *@annoying.example\n"
        "\n"
        "[comp.sys.amiga.*, de.comp.sys.amiga]\n"
        "score +50 subject: *A1200*\n"
        "  score 10\tsubject:   *trapdoor*  \r\n"
        "kill from: spammer@*\n"
        "score -10 lines< 3\n"
        "score +1 bytes> 10000\n"
        "[alt.binaries.*]\n"
        "kill subject: *password*\n"
        "[*]\n"
        "score -1 Message-ID: <1@x.example>\n"), &bad);
    CHECK(s != 0);
    CHECK_EQ(nn_score_rules(s), 8);

    o = art("Re: A1200 trapdoor fit", "a@b.example", 20, 500);
    CHECK_EQ(nn_score_of(s, "comp.sys.amiga.hardware", &o), 50 + 10 - 1);
    CHECK_EQ(nn_score_of(s, "de.comp.sys.amiga", &o), 59);
    CHECK_EQ(nn_score_of(s, "comp.sys.atari", &o), -1);     /* only [*] and the top rules */
    o = art("short", "x@annoying.example", 2, 20000);
    CHECK_EQ(nn_score_of(s, "comp.sys.amiga.misc", &o), -5 - 10 + 1 - 1);
    o = art("A1200 for sale", "SPAMMER@shop.example", 20, 500);
    CHECK(nn_score_killed(nn_score_of(s, "comp.sys.amiga.misc", &o)));
    CHECK(!nn_score_killed(nn_score_of(s, "alt.binaries.test", &o)));
    o = art("archive, password inside", "a@b.example", 20, 500);
    CHECK(nn_score_killed(nn_score_of(s, "alt.binaries.test", &o)));
    CHECK(!nn_score_killed(nn_score_of(s, "comp.sys.amiga.misc", &o)));
    CHECK_EQ(nn_score_of(0, "x", &o), 0);
    CHECK_EQ(nn_score_of(s, 0, &o), -1);     /* no group: only rules for every group */
    nn_score_free(s);

    /* many low scores together never kill: only a kill rule does */
    s = nn_score_parse(S("score -100000 subject: *\nscore -100000 subject: *\nscore -100000 subject: *\n"
                         "score -100000 subject: *\nscore -100000 subject: *\nscore -100000 subject: *\n"
                         "score -100000 subject: *\nscore -100000 subject: *\nscore -100000 subject: *\n"
                         "score -100000 subject: *\nscore -100000 subject: *\n"), &bad);
    CHECK(s != 0);
    o = art("x", "a@b.example", 1, 1);
    CHECK(!nn_score_killed(nn_score_of(s, "g", &o)));
    nn_score_free(s);

    /* lines that are not rules: the line number comes back */
    CHECK(nn_score_parse(S("score +5 subject: x\nscore abc subject: x\n"), &bad) == 0);
    CHECK_EQ(bad, 2);
    CHECK(nn_score_parse(S("score +100001 subject: x\n"), &bad) == 0);   /* over NN_SCORE_MAX */
    CHECK_EQ(bad, 1);
    CHECK(nn_score_parse(S("kill body: x\n"), &bad) == 0);               /* no such field */
    CHECK_EQ(bad, 1);
    CHECK(nn_score_parse(S("kill subject:\n"), &bad) == 0);              /* no pattern */
    CHECK_EQ(bad, 1);
    CHECK(nn_score_parse(S("# c\n[comp.*\n"), &bad) == 0);               /* unclosed section */
    CHECK_EQ(bad, 2);
    CHECK(nn_score_parse(S("[]\n"), &bad) == 0);
    CHECK_EQ(bad, 1);
    CHECK(nn_score_parse(S("score -3 lines< -1\n"), &bad) == 0);
    CHECK(nn_score_parse(S("score -3 lines< 3x\n"), &bad) == 0);
    CHECK(nn_score_parse(S("scored +1 subject: x\n"), &bad) == 0);
    CHECK(nn_score_parse(S("kill\n"), &bad) == 0);

    /* an empty file: no rules, every score 0 */
    s = nn_score_parse(S("\n# nothing\n"), &bad);
    CHECK(s != 0);
    CHECK_EQ(nn_score_rules(s), 0);
    CHECK_EQ(nn_score_of(s, "g", &o), 0);
    nn_score_free(s);

    /* the rule limit */
    {
        size_t n = (NN_SCORE_RULES + 1) * 20, k = 0;
        char *t = (char *)malloc(n);
        for (i = 0; i <= NN_SCORE_RULES; i++) {
            snprintf(big, sizeof(big), "kill subject: a%u\n", (unsigned)i);
            memcpy(t + k, big, strlen(big));
            k += strlen(big);
        }
        CHECK(nn_score_parse(t, k, &bad) == 0);
        CHECK_EQ(bad, 0);                 /* not a bad line: too many rules */
        free(t);
    }
    TEST_DONE();
}
