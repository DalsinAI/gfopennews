/* The queue journal after a crash: half-added files, bad states, removal
 * and the segment cap. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include "gfopennews/queue.h"
#include "gfopennews/nzb.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static void put(NKJournal *j, const char *r)
{
    nk_journal_append(j, r, strlen(r));
}

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";   /* a directory of its own */
    char db[600], dp[600], sp[600];
    NKJournal *j;
    NNQueue *q;
    NNQFileInfo fi;
    NNNzb nzb;
    NNNzbFile nf;
    int e;

    snprintf(db, sizeof(db), "%s/queue.db", dir);
    snprintf(dp, sizeof(dp), "%s/qj-out-c.bin.part", dir);
    snprintf(sp, sizeof(sp), "%s/qj-out-c.bin.parts", dir);
    remove(db); remove(dp); remove(sp);
    j = nk_journal_open(db, 0, 0, 0, &e);
    CHECK(j != 0);
    if (!j)
        return 1;
    /* 1: cut off after one of its three S records */
    put(j, "F 1 3 30 a.bin");
    put(j, "S 1 1 10 a1@x");
    /* 2: whole, with a completion state that is not one */
    put(j, "F 2 2 20 b.bin");
    put(j, "S 2 1 10 b1@x");
    put(j, "S 2 2 10 b2@x");
    put(j, "C 2 9 junk");
    put(j, "C 2 1 junk");          /* PAUSED is not a completion state either */
    /* 3: whole, size known: its part file is opened */
    put(j, "F 3 1 10 qj-out-c.bin");
    put(j, "S 3 1 10 c1@x");
    put(j, "Z 3 10");
    nk_journal_sync(j);
    nk_journal_close(j);

    q = nn_q_open(dir, dir, 1, &e);
    CHECK(q != 0);
    if (!q)
        return 1;
    CHECK_EQ(nn_q_dropped(q), 1);
    CHECK_EQ(nn_q_files(q), 2);
    CHECK(nn_q_info(q, 0, &fi) && fi.id == 2 && fi.state == NN_QF_QUEUED);
    CHECK(nn_q_info(q, 1, &fi) && fi.id == 3);
    CHECK(exists(dp) && exists(sp));
    /* removing a file drops its half-built download */
    CHECK_EQ(nn_q_remove(q, 3), NK_OK);
    CHECK(!exists(dp) && !exists(sp));
    /* a file with more segments than the cap is refused */
    memset(&nzb, 0, sizeof(nzb));
    memset(&nf, 0, sizeof(nf));
    nf.subject = (char *)"\"huge.bin\" yEnc (1/1)";
    nf.nsegs = (size_t)NN_Q_MAXSEGS + 1;
    nzb.files = &nf;
    nzb.nfiles = 1;
    CHECK_EQ(nn_q_add_nzb(q, &nzb), 0);
    CHECK_EQ(nn_q_files(q), 1);
    nn_q_close(q);

    /* reopened: the drop and the removal held */
    q = nn_q_open(dir, dir, 1, &e);
    CHECK(q != 0);
    if (q) {
        CHECK_EQ(nn_q_dropped(q), 0);
        CHECK_EQ(nn_q_files(q), 1);
        CHECK(nn_q_info(q, 0, &fi) && fi.id == 2);
        nn_q_close(q);
    }
    TEST_DONE();
}
