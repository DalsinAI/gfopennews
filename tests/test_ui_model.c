/* Reader UI model tests: groups, threads, the queue panel, posting.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/ui_model.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

static NNOver ov(uint32_t n, const char *subj, const char *id, const char *refs)
{
    NNOver o;
    memset(&o, 0, sizeof(o));
    o.number = n; o.subject = subj; o.from = "a@b.example"; o.date = "13 Nov 2026";
    o.msgid = id; o.references = refs; o.bytes = 1000 + n; o.lines = 10; o.xref = "";
    return o;
}

static const char *label(NNUiThreads *t, size_t row)
{
    static char b[256];
    nn_ut_label(t, row, b, sizeof(b));
    return b;
}

static void add(NNUiThreads *t, uint32_t n, const char *subj, const char *id, const char *refs)
{
    NNOver o = ov(n, subj, id, refs);
    CHECK_EQ(nn_ut_add(t, &o), NK_OK);
}

static void groups(void)
{
    NNNewsrc rc;
    NNActive act[3];
    NNUiGroups *g = nn_ug_new();
    char b[64];
    nn_rc_init(&rc);
    CHECK_EQ(nn_rc_parse(&rc, S("comp.sys.amiga.misc: 1-90\nalt.test! 1-5\nde.comp.sys.amiga: \nComp.Lang.C: 1-3\n")), NK_OK);
    memset(act, 0, sizeof(act));
    strcpy(act[0].name, "comp.sys.amiga.misc"); act[0].low = 1; act[0].high = 100;
    strcpy(act[1].name, "de.comp.sys.amiga"); act[1].low = 10; act[1].high = 19;
    strcpy(act[2].name, "alt.test"); act[2].low = 1; act[2].high = 50;
    CHECK_EQ(nn_ug_set(g, &rc, act, 3), NK_OK);
    /* subscribed only, in .newsrc order; a group the server doesn't list shows 0 */
    CHECK_EQ(nn_ug_rows(g), 3);
    CHECK(!strcmp(nn_ug_row(g, 0)->name, "comp.sys.amiga.misc") && nn_ug_row(g, 0)->unread == 10);
    CHECK(!strcmp(nn_ug_row(g, 1)->name, "de.comp.sys.amiga") && nn_ug_row(g, 1)->unread == 10);
    CHECK(!strcmp(nn_ug_row(g, 2)->name, "Comp.Lang.C") && nn_ug_row(g, 2)->unread == 0);
    nn_ug_label(g, 0, b, sizeof(b)); CHECK(!strcmp(b, "comp.sys.amiga.misc (10)"));
    nn_ug_label(g, 2, b, sizeof(b)); CHECK(!strcmp(b, "Comp.Lang.C"));
    CHECK_EQ(nn_ug_label(g, 9, b, sizeof(b)), 0);
    /* find as you type, any case; the filter stays across a new list */
    CHECK_EQ(nn_ug_filter(g, "COMP.S"), 2);
    CHECK_EQ(nn_ug_find(g, "de.comp.sys.amiga"), 1);
    CHECK_EQ(nn_ug_find(g, "Comp.Lang.C"), -1);
    CHECK_EQ(nn_ug_set(g, &rc, act, 3), NK_OK);
    CHECK_EQ(nn_ug_rows(g), 2);
    CHECK_EQ(nn_ug_filter(g, ""), 3);
    CHECK_EQ(nn_ug_filter(g, NULL), 3);
    CHECK(nn_ug_row(g, 3) == NULL);
    /* reading brings the count down */
    nn_rc_mark(nn_rc_group(&rc, "de.comp.sys.amiga", 0), 10, 14, 1);
    nn_ug_recount(g, &rc);
    CHECK_EQ(nn_ug_row(g, 1)->unread, 5);
    nn_ug_free(g);
    nn_rc_free(&rc);
}

static void threads(void)
{
    NNUiThreads *t = nn_ut_new();
    NNNewsrc rc;
    long r;
    add(t, 10, "Amiga 1200 PSU", "<a@x>", "");
    add(t, 12, "Re: Amiga 1200 PSU", "<b@x>", "<a@x>");
    add(t, 11, "Bus error on boot", "<c@x>", "");
    add(t, 14, "Re: Amiga 1200 PSU", "<d@x>", "<a@x> <b@x>");
    add(t, 13, "RE: Re: Amiga 1200 PSU", "<e@x>", "<a@x>");
    add(t, 15, "Re: Bus error on boot", "<f@x>", "<c@x> <gone@x>");   /* parent not on the server, one answer */
    add(t, 16, "Re[2]: Bus error on boot", "<g@x>", "<c@x> <gone@x>"); /* now <gone@x> joins two */
    add(t, 17, "Re: Lost thread", "<h@x>", "<lost@x>");               /* missing root, one answer: h is the root */
    add(t, 18, "Re: Lost thread", "<i@x>", "<lost2@x>");
    add(t, 19, "Re: Lost thread", "<j@x>", "<lost2@x>");              /* missing root, two answers: kept */
    add(t, 20, "Re: Amiga 1200 PSU", "<k@x>", "");                    /* no references: joins by subject */
    add(t, 21, "Re: Amiga 1200 PSU", "<k@x>", "");                    /* same id again: skipped */
    add(t, 22, "No id", "", "");
    CHECK_EQ(nn_ut_build(t, 0), NK_OK);
    CHECK_EQ(nn_ut_rows(t), 13);
    CHECK(!strcmp(label(t, 0), "- Amiga 1200 PSU"));
    CHECK(!strcmp(label(t, 1), "  - Re: Amiga 1200 PSU") && nn_ut_row(t, 1)->number == 12);
    CHECK(!strcmp(label(t, 2), "    Re: Amiga 1200 PSU") && nn_ut_row(t, 2)->number == 14);
    CHECK(nn_ut_row(t, 3)->number == 13 && nn_ut_row(t, 3)->depth == 1);
    CHECK(nn_ut_row(t, 4)->number == 20 && nn_ut_row(t, 4)->depth == 1);
    CHECK(!strcmp(label(t, 5), "- Bus error on boot"));
    /* <gone@x> is missing but not a thread's first: its answers move up to 11 */
    CHECK(nn_ut_row(t, 6)->number == 15 && nn_ut_row(t, 6)->depth == 1);
    CHECK(nn_ut_row(t, 7)->number == 16 && nn_ut_row(t, 7)->depth == 1);
    CHECK(nn_ut_row(t, 8)->number == 17 && nn_ut_row(t, 8)->depth == 0 && !nn_ut_row(t, 8)->children);
    /* a missing first article with two answers stays, as an empty row */
    CHECK(nn_ut_row(t, 9)->number == 0 && nn_ut_row(t, 9)->children == 2);
    CHECK(!strcmp(label(t, 9), "- (not on the server)"));
    CHECK(!strcmp(nn_ut_row(t, 9)->subject, "Re: Lost thread"));
    CHECK(nn_ut_row(t, 10)->number == 18 && nn_ut_row(t, 11)->number == 19);
    /* the article without a message-id is a thread of its own, last by number */
    CHECK(nn_ut_row(t, 12)->number == 22 && !strcmp(label(t, 12), "No id"));
    CHECK(nn_ut_row(t, 13) == NULL);
    CHECK_EQ(nn_ut_show(t, 22), 12);
    CHECK_EQ(nn_ut_show(t, 0), -1);
    /* built again after more headers came: the same threads, the new one in place */
    add(t, 23, "Re: Bus error on boot", "<l@x>", "<c@x>");
    CHECK_EQ(nn_ut_build(t, 0), NK_OK);
    CHECK_EQ(nn_ut_rows(t), 14);
    CHECK(nn_ut_row(t, 8)->number == 23 && nn_ut_row(t, 8)->depth == 1);
    nn_ut_free(t);

    /* folded, read state and next unread */
    t = nn_ut_new();
    add(t, 1, "One", "<1@x>", "");
    add(t, 2, "Re: One", "<2@x>", "<1@x>");
    add(t, 3, "Re: One", "<3@x>", "<1@x> <2@x>");
    add(t, 4, "Two", "<4@x>", "");
    add(t, 5, "Re: Two", "<5@x>", "<4@x>");
    CHECK_EQ(nn_ut_build(t, 1), NK_OK);
    CHECK_EQ(nn_ut_rows(t), 2);
    CHECK(!strcmp(label(t, 0), "+ One") && !strcmp(label(t, 1), "+ Two"));
    nn_rc_init(&rc);
    CHECK_EQ(nn_rc_parse(&rc, S("g: 1-2,4\n")), NK_OK);
    nn_ut_set_read(t, &rc.g[0]);
    CHECK(nn_ut_row(t, 0)->read && nn_ut_row(t, 0)->unread_below == 1);
    CHECK(nn_ut_row(t, 1)->read && nn_ut_row(t, 1)->unread_below == 1);
    r = nn_ut_next_unread(t, -1);
    CHECK_EQ(r, 2);                     /* article 3, its thread opened down to it */
    CHECK_EQ(nn_ut_row(t, r)->number, 3);
    CHECK_EQ(nn_ut_rows(t), 4);
    CHECK(!strcmp(label(t, 0), "- One") && !strcmp(label(t, 1), "  - Re: One"));
    r = nn_ut_next_unread(t, r);
    CHECK_EQ(nn_ut_row(t, r)->number, 5);
    CHECK_EQ(nn_ut_next_unread(t, r), -1);
    CHECK_EQ(nn_ut_open(t, 0, 0), 1);
    CHECK_EQ(nn_ut_open(t, 0, 0), 0);    /* already folded */
    CHECK_EQ(nn_ut_rows(t), 3);
    CHECK_EQ(nn_ut_open(t, 2, 1), 0);    /* no answers to open */
    CHECK_EQ(nn_ut_open(t, 99, 1), 0);
    CHECK_EQ(nn_ut_show(t, 2), 1);
    CHECK_EQ(nn_ut_show(t, 99), -1);
    nn_ut_set_read(t, NULL);
    CHECK(!nn_ut_row(t, 0)->read && nn_ut_row(t, 0)->unread_below == 2);
    nn_rc_free(&rc);
    nn_ut_free(t);

    /* a loop in References doesn't hang or lose articles */
    t = nn_ut_new();
    add(t, 1, "A", "<a@x>", "<b@x>");
    add(t, 2, "B", "<b@x>", "<a@x>");
    CHECK_EQ(nn_ut_build(t, 0), NK_OK);
    CHECK_EQ(nn_ut_rows(t), 2);
    nn_ut_free(t);

    /* a long chain of answers: no recursion, depth counted */
    t = nn_ut_new();
    {
        static char ids[3000][16], refs[3000][24];
        int i;
        for (i = 0; i < 3000; i++) {
            sprintf(ids[i], "<%d@x>", i);
            if (i) sprintf(refs[i], "<%d@x>", i - 1); else refs[i][0] = 0;
            add(t, (uint32_t)(i + 1), i ? "Re: Chain" : "Chain", ids[i], refs[i]);
        }
    }
    CHECK_EQ(nn_ut_build(t, 0), NK_OK);
    CHECK_EQ(nn_ut_rows(t), 3000);
    CHECK_EQ(nn_ut_row(t, 2999)->depth, 2999);
    CHECK(strlen(label(t, 2999)) == 40 + strlen("Re: Chain"));   /* indent stops at 20 levels */
    nn_ut_free(t);

    CHECK(!strcmp(nn_base_subject("Re: RE:re:  Hello"), "Hello"));
    CHECK(!strcmp(nn_base_subject("Re[3]: Hi"), "Hi"));
    CHECK(!strcmp(nn_base_subject("Reply all"), "Reply all"));
    CHECK(!strcmp(nn_base_subject(NULL), ""));
}

static void queue(void)
{
    NNUiQFile f;
    char b[64];
    memset(&f, 0, sizeof(f));
    f.nsegs = 200; f.done = 199;
    CHECK_EQ(nn_uq_percent(&f), 99);
    f.done = 200; CHECK_EQ(nn_uq_percent(&f), 100);
    f.done = 1; CHECK_EQ(nn_uq_percent(&f), 0);
    f.nsegs = 0; CHECK_EQ(nn_uq_percent(&f), 0);
    f.state = NN_UQ_DONE; CHECK_EQ(nn_uq_percent(&f), 100);
    nn_uq_state(&f, b, sizeof(b)); CHECK(!strcmp(b, "Done"));
    f.state = NN_UQ_INCOMPLETE; f.failed = 3;
    nn_uq_state(&f, b, sizeof(b)); CHECK(!strcmp(b, "Incomplete (3 missing)"));
    f.state = NN_UQ_PAUSED; nn_uq_state(&f, b, sizeof(b)); CHECK(!strcmp(b, "Paused"));
    f.state = NN_UQ_BADCRC; nn_uq_state(&f, b, sizeof(b)); CHECK(!strcmp(b, "Damaged"));
    f.state = NN_UQ_QUEUED; nn_uq_state(&f, b, sizeof(b)); CHECK(!strcmp(b, "Queued"));
    nn_uq_size(512, b, sizeof(b)); CHECK(!strcmp(b, "512 bytes"));
    nn_uq_size(12698, b, sizeof(b)); CHECK(!strcmp(b, "12.4 KB"));
    nn_uq_size(3u * 1024 * 1024, b, sizeof(b)); CHECK(!strcmp(b, "3.0 MB"));
    nn_uq_size((uint64_t)1288490189, b, sizeof(b)); CHECK(!strcmp(b, "1.2 GB"));
    nn_uq_size((uint64_t)5 << 40, b, sizeof(b)); CHECK(!strcmp(b, "5.0 TB"));
}

static void posting(void)
{
    NNPost p;
    NNOver par = ov(7, "Re: Re: Kickstart 3.2", "<p7@x>", "<p1@x> <p5@x>");
    char why[128], out[1024];
    size_t n;
    static const char text[] = "Hello\r\n> earlier\n\nbye\n-- \nsig line\n";
    nn_post_followup(&p, "comp.sys.amiga.misc", &par, "me@home.example");
    CHECK(!strcmp(p.subject, "Re: Kickstart 3.2"));
    CHECK(!strcmp(p.references, "<p1@x> <p5@x> <p7@x>"));
    CHECK(!strcmp(p.newsgroups, "comp.sys.amiga.misc"));
    CHECK_EQ(nn_post_check(&p, "text", why, sizeof(why)), 1);
    CHECK(!strcmp(why, ""));
    CHECK_EQ(nn_post_check(&p, " \n\r\n", why, sizeof(why)), 0);
    CHECK(!strcmp(why, "The article is empty."));

    n = nn_post_quote("Ann", text, sizeof(text) - 1, out, sizeof(out));
    CHECK_STRN(out, n, "Ann wrote:\n> Hello\n>> earlier\n>\n> bye\n");
    CHECK_EQ(nn_post_quote("Ann", text, sizeof(text) - 1, out, 8), n);   /* cut short, length still told */
    CHECK(!strcmp(out, "Ann wro"));

    n = nn_post_format(&p, "line one\nline two", out, sizeof(out));
    CHECK_STRN(out, n, "From: me@home.example\r\nNewsgroups: comp.sys.amiga.misc\r\nSubject: Re: Kickstart 3.2\r\n"
                        "References: <p1@x> <p5@x> <p7@x>\r\nUser-Agent: GFOpenNews\r\n\r\nline one\r\nline two\r\n");
    nn_post_new(&p, "alt.test", "me@home.example");
    strcpy(p.subject, "Test");
    n = nn_post_format(&p, "x\r\n", out, sizeof(out));
    CHECK_STRN(out, n, "From: me@home.example\r\nNewsgroups: alt.test\r\nSubject: Test\r\nUser-Agent: GFOpenNews\r\n\r\nx\r\n");

    /* checks */
    strcpy(p.subject, "   ");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0); CHECK(!strcmp(why, "The subject is empty."));
    strcpy(p.subject, "Hi\r\nNewsgroups: alt.spam");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0); CHECK(!strcmp(why, "A header can't hold a line break."));
    strcpy(p.subject, "Hi");
    strcpy(p.newsgroups, "a.b, c.d,e.f, g.h, i.j, k.l");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0); CHECK(!strcmp(why, "Posted to 6 groups: 5 at most."));
    strcpy(p.newsgroups, "a.b,c.d,e.f,g.h,i.j");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 1);
    strcpy(p.newsgroups, "a..b");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0); CHECK(!strcmp(why, "\"a..b\" is not a group name."));
    strcpy(p.newsgroups, "Alt.Test");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0);
    strcpy(p.newsgroups, " , ");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0); CHECK(!strcmp(why, "Say which group to post to."));
    strcpy(p.newsgroups, "alt.test"); strcpy(p.from, "nobody");
    CHECK_EQ(nn_post_check(&p, "x", why, sizeof(why)), 0); CHECK(!strcmp(why, "Set your address (From) in the settings."));

    /* a References line too long loses its second oldest ids, keeps the first and the parent */
    {
        static char refs[2000];
        size_t i;
        NNOver deep;
        refs[0] = 0;
        for (i = 0; i < 60; i++) sprintf(refs + strlen(refs), "%s<id%02u.long.message.id@x>", i ? " " : "", (unsigned)i);
        deep = ov(9, "Deep", "<last@x>", refs);
        nn_post_followup(&p, "alt.test", &deep, "me@home.example");
        CHECK(strlen(p.references) <= NN_POST_REFS_MAX);
        CHECK(!strncmp(p.references, "<id00.long.message.id@x> ", 25));
        CHECK(strstr(p.references, "<id59.long.message.id@x> <last@x>") != NULL);
        CHECK(strstr(p.references, "<id01.") == NULL);
    }
}

int main(void)
{
    groups();
    threads();
    queue();
    posting();
    TEST_DONE();
}
