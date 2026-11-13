/* GFOpenNews reader window, drawn through GFOpenLayout (gfopenlayout_ui.h):
 * three panes (groups, threads, the article), a Queue tab for the binaries
 * downloads and a Post tab for new articles and follow-ups. It never calls
 * graphics or layers: olui lays it out and the Amiga glue draws it.
 *
 * The window shows the models (ui_model.h) the program owns and tells the
 * program what was asked through NNWinOps. Plain C99: the host tests drive
 * it with olui's own input calls.
 *
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNEWS_UI_WINDOW_H
#define GFOPENNEWS_UI_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include "gfopenlayout_ui.h"
#include "gfopennews/ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tabs, from 0 */
enum { NN_WIN_TAB_NEWS = 0, NN_WIN_TAB_QUEUE, NN_WIN_TAB_POST };

typedef struct NNWinOps {
    void (*open_group)(void *user, const char *group);
    void (*open_article)(void *user, uint32_t number);
    /* Send was pressed and nn_post_check passed. */
    void (*post)(void *user, const NNPost *p, const char *body);
    void (*queue_pause)(void *user, uint32_t id, int pause);
    void (*queue_remove)(void *user, uint32_t id);
} NNWinOps;

typedef struct NNWin NNWin;

/* Loads the window's contents into ui (a new olui; the theme CSS goes in
 * first, as for any window). NULL with why in err. */
NNWin *nn_win_new(olui *ui, const NNWinOps *ops, void *user, char *err, size_t cap);
void   nn_win_free(NNWin *w);

/* The models shown; the program keeps them and calls nn_win_refresh after
 * changing them (new headers, articles marked read). */
void   nn_win_groups(NNWin *w, NNUiGroups *g);
void   nn_win_threads(NNWin *w, NNUiThreads *t);
void   nn_win_refresh(NNWin *w);
/* The article's text, shown as it is (headers the program chose, body). */
void   nn_win_article(NNWin *w, const char *text);
/* The queue's files (copied). */
int    nn_win_queue(NNWin *w, const NNUiQFile *files, size_t n);
/* The line under the panes: connection, counts, errors. */
void   nn_win_status(NNWin *w, const char *text);
/* The address posts are sent from (From:). */
void   nn_win_from(NNWin *w, const char *from);

/* What is chosen: the group ("" for none) and the article (0: none). */
const char *nn_win_group(const NNWin *w);
uint32_t    nn_win_article_number(const NNWin *w);

/* The Post tab: a new article in the chosen group, or a follow-up to the
 * chosen article with its text quoted. 0 when there is nothing to answer. */
int    nn_win_compose_new(NNWin *w);
int    nn_win_compose_followup(NNWin *w);

#ifdef __cplusplus
}
#endif
#endif
