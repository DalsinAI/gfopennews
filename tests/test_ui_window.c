/* Reader window tests: the window on GFOpenLayout's UI, driven with olui's
 * own input calls (the pointer and keys as the Amiga glue gives them).
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/ui_window.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

/* about 0.54 em a character, as GFOpenLayout's own tests measure */
static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * style->font_size * 7 / 13;
    m->ascent = style->font_size * 10 / 13;
    m->descent = style->font_size * 3 / 13;
    return 1;
}

/* what the program saw */
static struct {
    char group[64];
    uint32_t article;
    int articles, posts, pauses, resumes, removes;
    NNPost post;
    char body[512];
    uint32_t qid;
} seen;

static NNUiThreads *threads;
static NNWin *win;
static NNNewsrc rc;

static NNOver ov(uint32_t n, const char *subj, const char *id, const char *refs)
{
    NNOver o;
    memset(&o, 0, sizeof(o));
    o.number = n; o.subject = subj; o.from = "ann@example.org"; o.date = "13 Nov 2026";
    o.msgid = id; o.references = refs; o.xref = "";
    return o;
}

static void open_group(void *user, const char *group)
{
    NNOver o[4];
    int i;
    (void)user;
    snprintf(seen.group, sizeof(seen.group), "%s", group);
    nn_ut_free(threads);
    threads = nn_ut_new();
    o[0] = ov(10, "Amiga 1200 PSU", "<a@x>", "");
    o[1] = ov(11, "Re: Amiga 1200 PSU", "<b@x>", "<a@x>");
    o[2] = ov(12, "Bus error on boot", "<c@x>", "");
    o[3] = ov(13, "Re: Bus error on boot", "<d@x>", "<c@x>");
    for (i = 0; i < 4; i++) nn_ut_add(threads, &o[i]);
    nn_ut_build(threads, 0);
    nn_ut_set_read(threads, nn_rc_group(&rc, group, 1));
    nn_win_threads(win, threads);
}

static void open_article(void *user, uint32_t number)
{
    (void)user;
    seen.article = number;
    seen.articles++;
    nn_rc_mark(nn_rc_group(&rc, seen.group, 1), number, number, 1);
    nn_ut_set_read(threads, nn_rc_group(&rc, seen.group, 0));
    nn_win_article(win, "Mine is dead.\n\n-- \nAnn\n");
    nn_win_refresh(win);
}

static void post(void *user, const NNPost *p, const char *body)
{
    (void)user;
    seen.posts++;
    seen.post = *p;
    snprintf(seen.body, sizeof(seen.body), "%s", body);
}

static void queue_pause(void *user, uint32_t id, int pause)
{
    (void)user;
    seen.qid = id;
    if (pause) seen.pauses++; else seen.resumes++;
}

static void queue_remove(void *user, uint32_t id) { (void)user; seen.qid = id; seen.removes++; }

/* text drawn, to see what the lists show */
typedef struct { char text[64][96]; int images[64]; int n, nimg; } drawn;

static void draw_op(void *userdata, const olui_draw_op *op)
{
    drawn *d = (drawn *)userdata;
    if (op->kind == OLUI_DRAW_TEXT && d->n < 64) {
        size_t k = op->length < 95 ? op->length : 95;
        memcpy(d->text[d->n], op->text, k); d->text[d->n][k] = 0; d->n++;
    }
    if (op->kind == OLUI_DRAW_IMAGE && d->nimg < 64) d->images[d->nimg++] = op->image;
}

static int was_drawn(olui *ui, const char *text)
{
    static drawn d;
    int i;
    memset(&d, 0, sizeof(d));
    olui_draw(ui, NULL, draw_op, &d);
    for (i = 0; i < d.n; i++) if (!strcmp(d.text[i], text)) return 1;
    return 0;
}

static int texts_drawn(olui *ui, const char *text)
{
    static drawn d;
    int i, n = 0;
    memset(&d, 0, sizeof(d));
    olui_draw(ui, NULL, draw_op, &d);
    for (i = 0; i < d.n; i++) n += !strcmp(d.text[i], text);
    return n;
}

static int images_drawn(olui *ui)
{
    static drawn d;
    memset(&d, 0, sizeof(d));
    olui_draw(ui, NULL, draw_op, &d);
    return d.nimg;
}

static void click(olui *ui, const char *id)
{
    olui_rect r;
    CHECK(olui_box(ui, olui_find(ui, id), &r));
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_DOWN);
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_UP);
}

/* a list's row: 18 px each (16 px of line, 1 above and below), from 1 px in */
static void click_row(olui *ui, const char *id, int row, int what)
{
    olui_rect r;
    CHECK(olui_box(ui, olui_find(ui, id), &r));
    olui_mouse(ui, r.x + 20, r.y + 1 + row * 18 + 9, what);
    if (what == OLUI_MOUSE_DOWN) olui_mouse(ui, r.x + 20, r.y + 1 + row * 18 + 9, OLUI_MOUSE_UP);
}

static void type(olui *ui, const char *s)
{
    while (*s) olui_key(ui, (unsigned char)*s++, 0);
}

int main(void)
{
    static const NNWinOps ops = { open_group, open_article, post, queue_pause, queue_remove };
    olui *ui = olui_new();
    char err[128];
    NNUiGroups *groups = nn_ug_new();
    NNActive act[2];
    olui_rect r;
    NNUiQFile q[2];

    olui_set_measure(ui, measure, NULL);
    win = nn_win_new(ui, &ops, NULL, err, sizeof(err));
    CHECK(win != NULL);
    if (!win) { fprintf(stderr, "%s\n", err); TEST_DONE(); }
    nn_win_from(win, "me@home.example");
    CHECK(olui_layout(ui, 800, 600));

    /* the panes share the window: groups on the left, the articles over the text */
    {
        olui_rect g, t, a;
        CHECK(olui_box(ui, olui_find(ui, "groups"), &g) && olui_box(ui, olui_find(ui, "threads"), &t)
              && olui_box(ui, olui_find(ui, "article"), &a));
        CHECK(g.w >= 190 && g.h > 300);
        CHECK(t.x > g.x + g.w && t.w > 400 && t.h > 150);
        CHECK(a.y >= t.y + t.h && a.h > 150 && a.x == t.x);
    }

    /* groups, with unread counts */
    nn_rc_init(&rc);
    CHECK_EQ(nn_rc_parse(&rc, S("comp.sys.amiga.misc: 1-9\nde.comp.sys.amiga: \nalt.test! \n")), NK_OK);
    memset(act, 0, sizeof(act));
    strcpy(act[0].name, "comp.sys.amiga.misc"); act[0].low = 1; act[0].high = 13;
    strcpy(act[1].name, "de.comp.sys.amiga"); act[1].low = 1; act[1].high = 3;
    CHECK_EQ(nn_ug_set(groups, &rc, act, 2), NK_OK);
    nn_win_groups(win, groups);
    CHECK_EQ(olui_vlist_rows(olui_find(ui, "groups")), 2);
    CHECK(was_drawn(ui, "comp.sys.amiga.misc (4)"));
    CHECK(was_drawn(ui, "de.comp.sys.amiga (3)"));

    /* typing in the find field narrows the list, and back */
    olui_focus(ui, olui_find(ui, "find"));
    type(ui, "DE.");
    CHECK_EQ(olui_vlist_rows(olui_find(ui, "groups")), 1);
    olui_key(ui, OLUI_KEY_BACKSPACE, 0); olui_key(ui, OLUI_KEY_BACKSPACE, 0); olui_key(ui, OLUI_KEY_BACKSPACE, 0);
    CHECK_EQ(olui_vlist_rows(olui_find(ui, "groups")), 2);

    /* choosing a group opens it once */
    click_row(ui, "groups", 0, OLUI_MOUSE_DOWN);
    CHECK(!strcmp(seen.group, "comp.sys.amiga.misc") && !strcmp(nn_win_group(win), "comp.sys.amiga.misc"));
    click_row(ui, "groups", 0, OLUI_MOUSE_DOWN);
    CHECK_EQ(olui_vlist_rows(olui_find(ui, "threads")), 4);
    CHECK(was_drawn(ui, "- Amiga 1200 PSU"));
    CHECK(was_drawn(ui, "  Re: Bus error on boot"));
    CHECK(was_drawn(ui, "ann@example.org"));
    CHECK_EQ(texts_drawn(ui, "*"), 4);

    /* choosing an article opens it, marks it read */
    click_row(ui, "threads", 1, OLUI_MOUSE_DOWN);
    CHECK_EQ(seen.article, 11);
    CHECK_EQ(nn_win_article_number(win), 11);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "article")), "Mine is dead.\n\n-- \nAnn\n"));
    CHECK_EQ(texts_drawn(ui, "*"), 3);
    CHECK_EQ(images_drawn(ui), 0);               /* nothing for the program to draw itself */
    CHECK(was_drawn(ui, "comp.sys.amiga.misc (4)"));    /* the program recounts groups; the window shows */
    /* the same row again: not opened twice */
    click_row(ui, "threads", 1, OLUI_MOUSE_DOWN);
    CHECK_EQ(seen.articles, 1);

    /* a double-click folds a thread and opens it again */
    click_row(ui, "threads", 0, OLUI_MOUSE_DOWN);
    CHECK_EQ(seen.article, 10);
    click_row(ui, "threads", 0, OLUI_MOUSE_DOUBLE);
    CHECK_EQ(olui_vlist_rows(olui_find(ui, "threads")), 3);
    CHECK(was_drawn(ui, "+ Amiga 1200 PSU"));
    click_row(ui, "threads", 0, OLUI_MOUSE_DOUBLE);
    CHECK_EQ(olui_vlist_rows(olui_find(ui, "threads")), 4);

    /* Next unread: 12, then 13, then none */
    click(ui, "next");
    CHECK_EQ(seen.article, 12);
    CHECK_EQ(olui_vlist_current(olui_find(ui, "threads")), 2);
    click(ui, "next");
    CHECK_EQ(seen.article, 13);
    click(ui, "next");
    CHECK_EQ(seen.article, 13);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "status")), "No more unread articles here."));

    /* a follow-up: the Post tab, subject, references and the text quoted, signature left out */
    click(ui, "followup");
    CHECK_EQ(olui_tabs_current(olui_find(ui, "tabs")), NN_WIN_TAB_POST);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "pgroups")), "comp.sys.amiga.misc"));
    CHECK(!strcmp(olui_get_text(olui_find(ui, "psubject")), "Re: Bus error on boot"));
    CHECK(!strcmp(olui_get_text(olui_find(ui, "pbody")), "ann@example.org wrote:\n> Mine is dead.\n>\n"));
    CHECK(olui_focused(ui) == olui_find(ui, "pbody"));
    /* a check that fails says why and sends nothing */
    olui_set_text(ui, olui_find(ui, "psubject"), " ");
    click(ui, "send");
    CHECK_EQ(seen.posts, 0);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "pwhy")), "The subject is empty."));
    olui_set_text(ui, olui_find(ui, "psubject"), "Re: Bus error on boot");
    olui_set_text(ui, olui_find(ui, "pbody"), "ann@example.org wrote:\n> Mine is dead.\n\nTry another PSU.\n");
    click(ui, "send");
    CHECK_EQ(seen.posts, 1);
    CHECK(!strcmp(seen.post.references, "<c@x> <d@x>"));
    CHECK(!strcmp(seen.post.from, "me@home.example"));
    CHECK(strstr(seen.body, "Try another PSU.") != NULL);
    CHECK_EQ(olui_tabs_current(olui_find(ui, "tabs")), NN_WIN_TAB_NEWS);

    /* a new article: no references, the group filled in; Cancel leaves it */
    click(ui, "new");
    CHECK_EQ(olui_tabs_current(olui_find(ui, "tabs")), NN_WIN_TAB_POST);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "pgroups")), "comp.sys.amiga.misc"));
    CHECK(!strcmp(olui_get_text(olui_find(ui, "psubject")), ""));
    CHECK(olui_focused(ui) == olui_find(ui, "psubject"));
    click(ui, "cancel");
    CHECK_EQ(olui_tabs_current(olui_find(ui, "tabs")), NN_WIN_TAB_NEWS);
    CHECK_EQ(seen.posts, 1);
    click(ui, "new");
    olui_set_text(ui, olui_find(ui, "psubject"), "Test");
    olui_set_text(ui, olui_find(ui, "pbody"), "x");
    click(ui, "send");
    CHECK_EQ(seen.posts, 2);
    CHECK(!strcmp(seen.post.references, "") && !strcmp(seen.post.subject, "Test"));

    /* the queue: rows with progress, Pause, Resume and Remove on the chosen files */
    memset(q, 0, sizeof(q));
    q[0].id = 7; q[0].name = "debian.iso"; q[0].nsegs = 400; q[0].done = 100; q[0].bytes = 300u * 1024 * 1024;
    q[1].id = 8; q[1].name = "debian.par2"; q[1].state = NN_UQ_PAUSED; q[1].nsegs = 4;
    CHECK(nn_win_queue(win, q, 2));
    q[0].name = "changed after";              /* copied */
    olui_tabs_select(ui, olui_find(ui, "tabs"), NN_WIN_TAB_QUEUE);
    CHECK(olui_layout(ui, 800, 600));
    CHECK(was_drawn(ui, "debian.iso") && was_drawn(ui, "25% of 300.0 MB") && was_drawn(ui, "Paused"));
    click_row(ui, "queue", 0, OLUI_MOUSE_DOWN);
    click(ui, "qpause");
    CHECK(seen.pauses == 1 && seen.qid == 7);
    click_row(ui, "queue", 1, OLUI_MOUSE_DOWN);
    click(ui, "qresume");
    CHECK(seen.resumes == 1 && seen.qid == 8);
    click(ui, "qremove");
    CHECK(seen.removes == 1 && seen.qid == 8);
    CHECK(olui_box(ui, olui_find(ui, "queue"), &r) && r.h > 300);

    nn_win_free(win);
    olui_free(ui);
    nn_ut_free(threads);
    nn_ug_free(groups);
    nn_rc_free(&rc);
    TEST_DONE();
}
