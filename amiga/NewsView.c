/* NewsView: the GFOpenNews reader window over what is already on disk, a
 * .newsrc and the overview cache, with no network. It is the window's check
 * on the Amiga (the lab steps are in the README) until the reader's own
 * program joins it to NNTP.
 *
 *     NewsView NEWSRC=ENVARC:GFOpenNews/news.example.newsrc CACHE=GFOpenNews:cache/news.example
 *
 * Articles chosen are marked read and the .newsrc saved on quit.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"
#include "gfopennetkit/netkit.h"
#include "gfopennews/newsrc.h"
#include "gfopennews/overview.h"
#include "gfopennews/ui_window.h"

static const char version[] = "$VER: NewsView 0.1 (13.11.2026) AmiCradle Team";

typedef struct App {
    NNWin       *win;
    NNNewsrc     rc;
    NNUiGroups  *groups;
    NNUiThreads *threads;
    const char  *cache;
    int          changed;
} App;

static int add_header(void *user, const NNOver *ov)
{
    App *a = (App *)user;
    return nn_ut_add(a->threads, ov) == NK_OK ? 0 : 1;
}

static void open_group(void *user, const char *group)
{
    App *a = (App *)user;
    char path[512], status[320];
    nn_ut_free(a->threads);
    if (!(a->threads = nn_ut_new())) return;
    nn_ov_path(a->cache, group, path, sizeof(path));
    nn_ov_read(path, 1, 0xffffffffUL, add_header, a);
    nn_ut_build(a->threads, 1);
    nn_ut_set_read(a->threads, nn_rc_group(&a->rc, group, 0));
    nn_win_threads(a->win, a->threads);
    nn_win_article(a->win, "");
    snprintf(status, sizeof(status), "%s: %lu threads in the cache", group, (unsigned long)nn_ut_rows(a->threads));
    nn_win_status(a->win, status);
}

static void open_article(void *user, uint32_t number)
{
    App *a = (App *)user;
    NNGroupRc *g = nn_rc_group(&a->rc, nn_win_group(a->win), 1);
    char text[160];
    if (g) { nn_rc_mark(g, number, number, 1); a->changed = 1; }
    nn_ut_set_read(a->threads, g);
    nn_ug_recount(a->groups, &a->rc);
    snprintf(text, sizeof(text), "Article %lu.\n\nNewsView shows the overview cache only: GFOpenNews fetches the text over NNTP.\n",
             (unsigned long)number);
    nn_win_article(a->win, text);
    nn_win_refresh(a->win);
}

static void post(void *user, const NNPost *p, const char *body)
{
    App *a = (App *)user;
    (void)p; (void)body;
    nn_win_status(a->win, "NewsView doesn't send: posting is GFOpenNews' (NNTP).");
}

/* The server's marks as the cache knows them: 1 up to the highest number held. */
static NNActive *marks(App *a, size_t *n)
{
    NNActive *act = (NNActive *)calloc(a->rc.n ? a->rc.n : 1, sizeof(*act));
    size_t i;
    *n = 0;
    if (!act) return NULL;
    for (i = 0; i < a->rc.n; i++) {
        char path[512];
        int err = 0;
        NNOvCache *c;
        if (!a->rc.g[i].subscribed) continue;
        nn_ov_path(a->cache, a->rc.g[i].name, path, sizeof(path));
        if (!(c = nn_ov_open(path, &err))) continue;
        snprintf(act[*n].name, sizeof(act[*n].name), "%s", a->rc.g[i].name);
        act[*n].low = 1;
        act[*n].high = nn_ov_high(c);
        act[*n].status = 'y';
        (*n)++;
        nn_ov_close(c);
    }
    return act;
}

int main(void)
{
    static const NNWinOps ops = { open_group, open_article, post, NULL, NULL };
    LONG args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rda = ReadArgs((STRPTR)"NEWSRC/A,CACHE/A,FROM/K,THEME/K", args, NULL);
    struct Window *w = NULL;
    olui *ui = NULL;
    olui_amiga *glue = NULL;     /* not "amiga": the compiler defines that */
    NNActive *act = NULL;
    size_t nact = 0;
    char err[128];
    App a;
    int done = 0, rc = RETURN_FAIL;
    (void)version;

    memset(&a, 0, sizeof(a));
    nn_rc_init(&a.rc);
    if (!rda) { PutStr((STRPTR)"NewsView: NEWSRC=file CACHE=directory [FROM=address] [THEME=file]\n"); return RETURN_FAIL; }
    a.cache = (const char *)args[1];
    if (nn_rc_load(&a.rc, (const char *)args[0]) != NK_OK) {
        Printf((STRPTR)"NewsView: can't read %s\n", (ULONG)args[0]);
        goto out;
    }
    w = OpenWindowTags(NULL, WA_Title, (ULONG)"GFOpenNews", WA_Width, 640, WA_Height, 440,
                       WA_MinWidth, 400, WA_MinHeight, 260, WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
                       WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                       WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE,
                       WA_IDCMP, olui_amiga_idcmp(), TAG_DONE);
    if (!w) { PutStr((STRPTR)"NewsView: no window\n"); goto out; }
    ui = olui_new();
    glue = ui ? olui_amiga_new(ui, w) : NULL;
    a.groups = nn_ug_new();
    if (!glue || !a.groups) { PutStr((STRPTR)"NewsView: out of memory\n"); goto out; }
    if (!olui_amiga_theme(glue, args[3] ? (const char *)args[3] : NULL))
        PutStr((STRPTR)"NewsView: no theme found, the built-in look\n");
    if (!(a.win = nn_win_new(ui, &ops, &a, err, sizeof(err)))) {
        Printf((STRPTR)"NewsView: %s\n", (ULONG)err);
        goto out;
    }
    nn_win_from(a.win, args[2] ? (const char *)args[2] : "");
    act = marks(&a, &nact);
    if (nn_ug_set(a.groups, &a.rc, act, nact) != NK_OK) { PutStr((STRPTR)"NewsView: out of memory\n"); goto out; }
    nn_win_groups(a.win, a.groups);
    nn_win_status(a.win, "Choose a group.");
    if (!olui_amiga_layout(glue)) { PutStr((STRPTR)"NewsView: out of memory\n"); goto out; }
    while (!done) {
        struct IntuiMessage *m;
        ULONG sig = Wait((1UL << w->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) break;
        while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            int r = olui_amiga_event(glue, m);
            ReplyMsg(&m->ExecMessage);
            if (r == OLUI_AMIGA_CLOSE) done = 1;
        }
        olui_amiga_draw(glue);
    }
    rc = RETURN_OK;
    if (a.changed && nn_rc_save(&a.rc, (const char *)args[0]) != NK_OK) {
        Printf((STRPTR)"NewsView: can't save %s\n", (ULONG)args[0]);
        rc = RETURN_WARN;
    }
out:
    nn_win_free(a.win);
    olui_amiga_free(glue);
    olui_free(ui);
    if (w) CloseWindow(w);
    nn_ut_free(a.threads);
    nn_ug_free(a.groups);
    free(act);
    nn_rc_free(&a.rc);
    FreeArgs(rda);
    return rc;
}
