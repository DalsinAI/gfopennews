/* GFOpenNews reader window on GFOpenLayout. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennews/ui_window.h"

static const char markup[] =
    "<column id=main>"
    " <tabs id=tabs>"
    "  <tab title=News id=tnews>"
    "   <row class=panes>"
    "    <column class=left>"
    "     <input id=find placeholder='Find a group'>"
    "     <vlist id=groups columns='200'></vlist>"
    "    </column>"
    "    <column class=right>"
    "     <row class=bar>"
    "      <button id=next>Next unread</button>"
    "      <button id=new>New article</button>"
    "      <button id=followup>Follow up</button>"
    "     </row>"
    "     <vlist id=threads columns='16 320 160 140'></vlist>"
    "     <textarea id=article readonly></textarea>"
    "    </column>"
    "   </row>"
    "  </tab>"
    "  <tab title=Queue id=tqueue>"
    "   <vlist id=queue columns='300 90 50'></vlist>"
    "   <row class=bar>"
    "    <button id=qpause>Pause</button>"
    "    <button id=qresume>Resume</button>"
    "    <button id=qremove>Remove</button>"
    "   </row>"
    "  </tab>"
    "  <tab title=Post id=tpost>"
    "   <grid columns=2>"
    "    <label for=pgroups>Newsgroups</label><input id=pgroups>"
    "    <label for=psubject>Subject</label><input id=psubject>"
    "   </grid>"
    "   <textarea id=pbody></textarea>"
    "   <label id=pwhy></label>"
    "   <row class=send>"
    "    <button id=send>Send</button>"
    "    <button id=cancel>Cancel</button>"
    "   </row>"
    "  </tab>"
    " </tabs>"
    " <label id=status></label>"
    "</column>";

static const char css[] =
    "#main{flex:1}"
    "#tabs{flex:1}"
    "tab{flex:1}"
    ".panes{flex:1;gap:6px}"
    ".left{width:200px;gap:4px}"
    ".right{flex:1;gap:4px}"
    ".bar{gap:4px}"
    "#groups{flex:1}"
    "#threads{flex:1}"
    "#article{flex:1;font-family:monospace}"
    "#queue{flex:1}"
    "#pgroups,#psubject{flex:1}"
    "#pbody{flex:1;font-family:monospace}"
    ".send{justify-content:end;gap:4px}"
    "#status{height:18px}";

struct NNWin {
    olui        *ui;
    NNWinOps     ops;
    void        *user;
    olui_el     *tabs, *find, *groups, *threads, *article, *queue, *status;
    olui_el     *pgroups, *psubject, *pbody, *pwhy;
    NNUiGroups  *g;
    NNUiThreads *t;
    NNUiQFile   *q;
    size_t       nq;
    char        *qnames;          /* the queue's names, one block */
    char         group[256];
    uint32_t     number;          /* the article chosen */
    char        *text;            /* its text */
    char         from[256];
    char         refs[1024];      /* the follow-up being written: References */
    char         cell[512];
};

static void set_text(NNWin *w, olui_el *el, const char *s) { olui_set_text(w->ui, el, s ? s : ""); }

/* ---- the lists' cells ---- */

static const char *group_cell(void *userdata, int row, int column, int *image)
{
    NNWin *w = (NNWin *)userdata;
    (void)column; (void)image;
    if (!w->g) return NULL;
    nn_ug_label(w->g, (size_t)row, w->cell, sizeof(w->cell));
    return w->cell;
}

static const char *thread_cell(void *userdata, int row, int column, int *image)
{
    NNWin *w = (NNWin *)userdata;
    const NNUiRow *r = w->t ? nn_ut_row(w->t, (size_t)row) : NULL;
    if (!r) return NULL;
    (void)image;
    switch (column) {
    case 0: return r->number && !r->read ? "*" : "";      /* unread: a mark of text, nothing drawn by hand */
    case 1:
        nn_ut_label(w->t, (size_t)row, w->cell, sizeof(w->cell));
        return w->cell;
    case 2: return r->from;
    case 3: return r->date;
    default: return NULL;
    }
}

static const char *queue_cell(void *userdata, int row, int column, int *image)
{
    NNWin *w = (NNWin *)userdata;
    const NNUiQFile *f;
    (void)image;
    if (row < 0 || (size_t)row >= w->nq) return NULL;
    f = &w->q[row];
    switch (column) {
    case 0: return f->name;
    case 1:
        if (f->state == NN_UQ_QUEUED) {
            char size[32];
            nn_uq_size(f->bytes, size, sizeof(size));
            snprintf(w->cell, sizeof(w->cell), "%d%% of %s", nn_uq_percent(f), size);
        } else nn_uq_state(f, w->cell, sizeof(w->cell));
        return w->cell;
    case 2: snprintf(w->cell, sizeof(w->cell), "%d%%", nn_uq_percent(f)); return w->cell;
    default: return NULL;
    }
}

/* ---- keeping the lists up to date ---- */

static long row_of(const NNWin *w, uint32_t number)
{
    size_t i;
    if (!w->t || !number) return -1;
    for (i = 0; i < nn_ut_rows(w->t); i++)
        if (nn_ut_row(w->t, i)->number == number) return (long)i;
    return -1;
}

/* olui keeps a list's selection by row, so after the rows change it is set
 * again from what the rows hold: only row (-1: none) stays selected. */
static void select_only(NNWin *w, olui_el *list, long row)
{
    int i;
    for (i = 0; i < olui_vlist_rows(list); i++)
        if (i != row && olui_vlist_selected(list, i)) olui_vlist_select(w->ui, list, i, 0);
    if (row >= 0) {
        olui_vlist_select(w->ui, list, (int)row, 1);
        olui_vlist_set_current(w->ui, list, (int)row);
    }
}

static void show_threads(NNWin *w)
{
    olui_vlist_set(w->ui, w->threads, w->t ? (int)nn_ut_rows(w->t) : 0, thread_cell, w);
    select_only(w, w->threads, row_of(w, w->number));
}

static void show_groups(NNWin *w)
{
    long r;
    olui_vlist_set(w->ui, w->groups, w->g ? (int)nn_ug_rows(w->g) : 0, group_cell, w);
    r = w->g && w->group[0] ? nn_ug_find(w->g, w->group) : -1;
    select_only(w, w->groups, r);
}

void nn_win_groups(NNWin *w, NNUiGroups *g) { w->g = g; show_groups(w); }
void nn_win_threads(NNWin *w, NNUiThreads *t) { w->t = t; show_threads(w); }
void nn_win_refresh(NNWin *w) { show_groups(w); show_threads(w); }

void nn_win_article(NNWin *w, const char *text)
{
    size_t n = strlen(text ? text : "");
    char *c = malloc(n + 1);
    if (c) { memcpy(c, text ? text : "", n + 1); free(w->text); w->text = c; }
    set_text(w, w->article, text);
    olui_set_scroll(w->ui, w->article, 0);
}

int nn_win_queue(NNWin *w, const NNUiQFile *files, size_t n)
{
    size_t i, k, bytes = 0;
    NNUiQFile *q;
    char *names, *p;
    uint32_t *chosen, cur = 0;
    size_t nchosen = 0;
    int curat = olui_vlist_current(w->queue);
    for (i = 0; i < n; i++) bytes += strlen(files[i].name ? files[i].name : "") + 1;
    q = malloc((n ? n : 1) * sizeof(*q));
    names = malloc(bytes ? bytes : 1);
    if (!q || !names) { free(q); free(names); return 0; }
    for (i = 0, p = names; i < n; i++) {
        size_t k = strlen(files[i].name ? files[i].name : "") + 1;
        q[i] = files[i];
        memcpy(p, files[i].name ? files[i].name : "", k);
        q[i].name = p;
        p += k;
    }
    /* the files chosen, by id: the rows may hold other files now */
    chosen = malloc((w->nq ? w->nq : 1) * sizeof(*chosen));
    if (!chosen) { free(q); free(names); return 0; }
    for (i = 0; i < w->nq; i++)
        if (olui_vlist_selected(w->queue, (int)i)) chosen[nchosen++] = w->q[i].id;
    if (curat >= 0 && (size_t)curat < w->nq) cur = w->q[curat].id;
    free(w->q); free(w->qnames);
    w->q = q; w->qnames = names; w->nq = n;
    olui_vlist_set(w->ui, w->queue, (int)n, queue_cell, w);
    for (i = 0; i < n; i++) {
        int sel = 0;
        for (k = 0; k < nchosen; k++) if (q[i].id == chosen[k]) sel = 1;
        if (sel != olui_vlist_selected(w->queue, (int)i)) olui_vlist_select(w->ui, w->queue, (int)i, sel);
        if (cur && q[i].id == cur) olui_vlist_set_current(w->ui, w->queue, (int)i);
    }
    free(chosen);
    return 1;
}

void nn_win_status(NNWin *w, const char *text) { set_text(w, w->status, text); }

void nn_win_from(NNWin *w, const char *from) { snprintf(w->from, sizeof(w->from), "%s", from ? from : ""); }

const char *nn_win_group(const NNWin *w) { return w->group; }
uint32_t nn_win_article_number(const NNWin *w) { return w->number; }

/* ---- posting ---- */

static void compose(NNWin *w, const NNPost *p, const char *body)
{
    snprintf(w->refs, sizeof(w->refs), "%s", p->references);
    set_text(w, w->pgroups, p->newsgroups);
    set_text(w, w->psubject, p->subject);
    set_text(w, w->pbody, body);
    set_text(w, w->pwhy, "");
    olui_tabs_select(w->ui, w->tabs, NN_WIN_TAB_POST);
    olui_focus(w->ui, p->subject[0] ? w->pbody : w->psubject);
}

int nn_win_compose_new(NNWin *w)
{
    NNPost p;
    nn_post_new(&p, w->group, w->from);
    compose(w, &p, "");
    return 1;
}

int nn_win_compose_followup(NNWin *w)
{
    NNPost p;
    NNOver o;
    const NNUiRow *r;
    long row = row_of(w, w->number);
    char *quote;
    size_t n;
    if (row < 0 || !(r = nn_ut_row(w->t, (size_t)row))) return 0;
    memset(&o, 0, sizeof(o));
    o.number = r->number; o.subject = r->subject; o.from = r->from; o.date = r->date;
    o.msgid = r->msgid; o.references = r->references; o.xref = "";
    nn_post_followup(&p, w->group, &o, w->from);
    n = nn_post_quote(r->from, w->text ? w->text : "", w->text ? strlen(w->text) : 0, NULL, 0);
    if (!(quote = malloc(n + 1))) return 0;
    nn_post_quote(r->from, w->text ? w->text : "", w->text ? strlen(w->text) : 0, quote, n + 1);
    compose(w, &p, quote);
    free(quote);
    return 1;
}

static void send_post(NNWin *w)
{
    NNPost p;
    char why[256];
    const char *body = olui_get_text(w->pbody);
    nn_post_new(&p, olui_get_text(w->pgroups), w->from);
    snprintf(p.subject, sizeof(p.subject), "%s", olui_get_text(w->psubject));
    snprintf(p.references, sizeof(p.references), "%s", w->refs);
    if (!nn_post_check(&p, body, why, sizeof(why))) {
        set_text(w, w->pwhy, why);
        return;
    }
    if (w->ops.post) w->ops.post(w->user, &p, body);
    w->refs[0] = 0;
    set_text(w, w->pgroups, ""); set_text(w, w->psubject, ""); set_text(w, w->pbody, ""); set_text(w, w->pwhy, "");
    olui_tabs_select(w->ui, w->tabs, NN_WIN_TAB_NEWS);
}

static void cancel_post(NNWin *w)
{
    w->refs[0] = 0;
    set_text(w, w->pgroups, ""); set_text(w, w->psubject, ""); set_text(w, w->pbody, ""); set_text(w, w->pwhy, "");
    olui_tabs_select(w->ui, w->tabs, NN_WIN_TAB_NEWS);
}

/* ---- events ---- */

static void choose_article(NNWin *w, long row)
{
    const NNUiRow *r = w->t ? nn_ut_row(w->t, (size_t)row) : NULL;
    if (!r || !r->number || r->number == w->number) return;
    w->number = r->number;
    if (w->ops.open_article) w->ops.open_article(w->user, r->number);
}

static void next_unread(NNWin *w)
{
    long row;
    if (!w->t) return;
    row = nn_ut_next_unread(w->t, row_of(w, w->number));
    if (row < 0) { nn_win_status(w, "No more unread articles here."); return; }
    olui_vlist_set(w->ui, w->threads, (int)nn_ut_rows(w->t), thread_cell, w);
    select_only(w, w->threads, row);
    choose_article(w, row);
}

static void queue_action(NNWin *w, int what)
{
    int i;
    for (i = 0; i < (int)w->nq; i++) {
        if (!olui_vlist_selected(w->queue, i)) continue;
        if (what == 2) { if (w->ops.queue_remove) w->ops.queue_remove(w->user, w->q[i].id); }
        else if (w->ops.queue_pause) w->ops.queue_pause(w->user, w->q[i].id, what == 0);
    }
}

static void on_event(olui *ui, const olui_event *ev, void *userdata)
{
    NNWin *w = (NNWin *)userdata;
    const char *id = ev->id ? ev->id : "";
    (void)ui;
    if (ev->type == OLUI_CHANGE && !strcmp(id, "find")) {
        if (w->g) { nn_ug_filter(w->g, ev->value); show_groups(w); }
    } else if (ev->type == OLUI_SELECT && !strcmp(id, "groups")) {
        const NNUiGroup *g = w->g ? nn_ug_row(w->g, (size_t)ev->index) : NULL;
        if (g && strcmp(g->name, w->group)) {
            snprintf(w->group, sizeof(w->group), "%s", g->name);
            w->number = 0;
            if (w->ops.open_group) w->ops.open_group(w->user, g->name);
        }
    } else if (ev->type == OLUI_SELECT && !strcmp(id, "threads")) {
        choose_article(w, ev->index);
    } else if (ev->type == OLUI_ACTIVATE && !strcmp(id, "threads")) {
        const NNUiRow *r = w->t ? nn_ut_row(w->t, (size_t)ev->index) : NULL;
        if (r && r->children) {
            nn_ut_open(w->t, (size_t)ev->index, !r->open);
            show_threads(w);           /* the chosen article stays chosen where it moved to */
            olui_vlist_set_current(w->ui, w->threads, ev->index);
        }
    } else if (ev->type == OLUI_CLICK) {
        if (!strcmp(id, "next")) next_unread(w);
        else if (!strcmp(id, "new")) nn_win_compose_new(w);
        else if (!strcmp(id, "followup")) {
            if (!nn_win_compose_followup(w)) nn_win_status(w, "Choose an article to follow up.");
        }
        else if (!strcmp(id, "qpause")) queue_action(w, 0);
        else if (!strcmp(id, "qresume")) queue_action(w, 1);
        else if (!strcmp(id, "qremove")) queue_action(w, 2);
        else if (!strcmp(id, "send")) send_post(w);
        else if (!strcmp(id, "cancel")) cancel_post(w);
    }
}

NNWin *nn_win_new(olui *ui, const NNWinOps *ops, void *user, char *err, size_t cap)
{
    NNWin *w = calloc(1, sizeof(*w));
    if (!w) { snprintf(err, cap, "out of memory"); return NULL; }
    w->ui = ui; w->user = user;
    if (ops) w->ops = *ops;
    if (!olui_load(ui, markup, css, err, cap) || !olui_on(ui, NULL, 0, on_event, w)) { free(w); return NULL; }
    w->tabs = olui_find(ui, "tabs");      w->find = olui_find(ui, "find");
    w->groups = olui_find(ui, "groups");  w->threads = olui_find(ui, "threads");
    w->article = olui_find(ui, "article"); w->queue = olui_find(ui, "queue");
    w->status = olui_find(ui, "status");  w->pgroups = olui_find(ui, "pgroups");
    w->psubject = olui_find(ui, "psubject"); w->pbody = olui_find(ui, "pbody");
    w->pwhy = olui_find(ui, "pwhy");
    olui_vlist_set(ui, w->groups, 0, group_cell, w);
    olui_vlist_set(ui, w->threads, 0, thread_cell, w);
    olui_vlist_set(ui, w->queue, 0, queue_cell, w);
    return w;
}

void nn_win_free(NNWin *w)
{
    if (!w) return;
    free(w->q); free(w->qnames); free(w->text);
    free(w);
}
