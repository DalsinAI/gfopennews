/* .newsrc tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/newsrc.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

static const char *fmt(NNGroupRc *g)
{
    static char b[256];
    nn_rc_format(g, b, sizeof(b));
    return b;
}

int main(int argc, char **argv)
{
    NNNewsrc rc, rc2;
    NNGroupRc *g;
    char path[512];
    FILE *f;

    nn_rc_init(&rc);
    CHECK_EQ(nn_rc_parse(&rc, S(
        "comp.sys.amiga.misc: 1-41237,41240,41300-41310\n"
        "alt.binaries.test! \r\n"
        "# a comment\n"
        "\n"
        "no separator here\n"
        "de.comp.sys.amiga:1-5,3-9, 20\n"
        "comp.sys.amiga.misc: 1-2\n"        /* listed twice: first one kept */
        "bad.range: 5-3\n"
        "tail.noeol: 7")), NK_OK);
    CHECK_EQ(rc.n, 5);
    CHECK_EQ(rc.bad_lines, 2);
    g = nn_rc_group(&rc, "comp.sys.amiga.misc", 0);
    CHECK(g && g->subscribed && g->n == 3);
    CHECK(strcmp(fmt(g), "comp.sys.amiga.misc: 1-41237,41240,41300-41310") == 0);
    CHECK(nn_rc_is_read(g, 1) && nn_rc_is_read(g, 41237) && !nn_rc_is_read(g, 41238));
    CHECK(nn_rc_is_read(g, 41240) && nn_rc_is_read(g, 41305) && !nn_rc_is_read(g, 41311));
    CHECK_EQ(nn_rc_unread(g, 41230, 41320), 2 + 59 + 10);
    g = nn_rc_group(&rc, "alt.binaries.test", 0);
    CHECK(g && !g->subscribed && g->n == 0);
    CHECK(strcmp(fmt(g), "alt.binaries.test!") == 0);
    g = nn_rc_group(&rc, "de.comp.sys.amiga", 0);
    CHECK(strcmp(fmt(g), "de.comp.sys.amiga: 1-9,20") == 0);     /* overlaps joined */
    g = nn_rc_group(&rc, "bad.range", 0);
    CHECK(g && g->n == 0);                 /* the bad part is left out */
    g = nn_rc_group(&rc, "tail.noeol", 0);
    CHECK(g && nn_rc_is_read(g, 7));

    /* marking read joins neighbours, unread splits */
    g = nn_rc_group(&rc, "comp.sys.amiga.misc", 0);
    nn_rc_mark(g, 41238, 41239, 1);
    CHECK(strcmp(fmt(g), "comp.sys.amiga.misc: 1-41240,41300-41310") == 0);
    nn_rc_mark(g, 41241, 41299, 1);
    CHECK(strcmp(fmt(g), "comp.sys.amiga.misc: 1-41310") == 0);
    nn_rc_mark(g, 100, 200, 0);
    CHECK(strcmp(fmt(g), "comp.sys.amiga.misc: 1-99,201-41310") == 0);
    nn_rc_mark(g, 1, 99, 0);
    CHECK(strcmp(fmt(g), "comp.sys.amiga.misc: 201-41310") == 0);
    nn_rc_mark(g, 50, 60, 1);
    CHECK(strcmp(fmt(g), "comp.sys.amiga.misc: 50-60,201-41310") == 0);
    nn_rc_mark(g, 0xfffffff0u, 0xffffffffu, 1);
    CHECK(nn_rc_is_read(g, 0xffffffffu));
    CHECK_EQ(nn_rc_mark(g, 5, 4, 1), NK_EARG);
    CHECK_EQ(nn_rc_unread(g, 1, 10), 10);
    CHECK_EQ(nn_rc_unread(0, 0, 10), 10);
    CHECK_EQ(nn_rc_unread(g, 10, 5), 0);
    /* names that would break the format are refused */
    CHECK(nn_rc_group(&rc, "bad:name", 1) == 0);
    CHECK(nn_rc_group(&rc, "", 1) == 0);
    {
        char tiny[8];
        CHECK_EQ(nn_rc_format(g, tiny, sizeof(tiny)), strlen(fmt(g)));
        CHECK_EQ(strlen(tiny), 7);
    }

    /* save and load back; the old file is replaced whole */
    snprintf(path, sizeof(path), "%s/test.newsrc", argc > 1 ? argv[1] : ".");
    f = fopen(path, "wb");
    fputs("old.junk: 1\n", f);
    fclose(f);
    CHECK_EQ(nn_rc_save(&rc, path), NK_OK);
    nn_rc_init(&rc2);
    CHECK_EQ(nn_rc_load(&rc2, path), NK_OK);
    CHECK_EQ(rc2.n, rc.n);
    CHECK(nn_rc_group(&rc2, "old.junk", 0) == 0);
    g = nn_rc_group(&rc2, "comp.sys.amiga.misc", 0);
    CHECK(g && nn_rc_is_read(g, 55) && !nn_rc_is_read(g, 61));
    CHECK_EQ(rc2.bad_lines, 0);
    nn_rc_free(&rc2);
    {
        char p2[600];
        snprintf(p2, sizeof(p2), "%s.new", path);
        CHECK((f = fopen(p2, "rb")) == 0);
        snprintf(p2, sizeof(p2), "%s.old", path);
        CHECK((f = fopen(p2, "rb")) == 0);
    }
    remove(path);
    nn_rc_init(&rc2);
    CHECK_EQ(nn_rc_load(&rc2, path), NK_OK);   /* missing file: empty */
    CHECK_EQ(rc2.n, 0);
    nn_rc_free(&rc2);
    nn_rc_free(&rc);
    TEST_DONE();
}
