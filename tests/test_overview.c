/* Overview cache tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/overview.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

static uint32_t seen[16];
static int nseen;
static int take(void *u, const NNOver *ov)
{
    (void)u;
    if (nseen < 16)
        seen[nseen] = ov->number;
    nseen++;
    return nseen >= 100;
}

int main(int argc, char **argv)
{
    char path[512], big[3000];
    NNOvCache *c;
    int e;
    const char *dir = argc > 1 ? argv[1] : ".";

    CHECK_EQ(nn_ov_path("GFOpenNews:cache/srv", "comp.sys.amiga.misc", path, sizeof(path)),
             strlen("GFOpenNews:cache/srv/comp.sys.amiga.misc.ov"));
    CHECK(strcmp(path, "GFOpenNews:cache/srv/comp.sys.amiga.misc.ov") == 0);
    nn_ov_path("RAM:", "../evil/x:y", path, sizeof(path));
    CHECK(strcmp(path, "RAM:_._evil_x_y.ov") == 0);       /* no way out of the cache */
    CHECK_EQ(nn_ov_path("d", "g", path, 3), 6);           /* too small: length only */

    nn_ov_path(dir, "test.group", path, sizeof(path));
    remove(path);
    c = nn_ov_open(path, &e);
    CHECK(c && e == 0);
    CHECK_EQ(nn_ov_high(c), 0);
    CHECK_EQ(nn_ov_add(c, S("1\ta\tf\td\t<1@x>\t\t10\t1")), 1);
    CHECK_EQ(nn_ov_add(c, S("3\tc\tf\td\t<3@x>\t<1@x>\t10\t1")), 1);
    CHECK_EQ(nn_ov_add(c, S("2\told\tf\td\t<2@x>\t\t10\t1")), 0);    /* below high */
    CHECK_EQ(nn_ov_add(c, S("junk")), 0);
    CHECK_EQ(nn_ov_add(c, S("4\tx\ny\tf\td\tm\tr\t1\t1")), 0);       /* LF inside */
    /* a long subject line */
    memset(big, 's', sizeof(big));
    memcpy(big, "4\t", 2);
    memcpy(big + 2000, "\tf\td\t<4@x>\t\t10\t1", 17);
    CHECK_EQ(nn_ov_add(c, big, 2017), 1);
    CHECK_EQ(nn_ov_high(c), 4);
    nn_ov_close(c);

    /* reopening finds the high mark, even after a long line */
    c = nn_ov_open(path, &e);
    CHECK_EQ(nn_ov_high(c), 4);
    CHECK_EQ(nn_ov_count(c), 3);
    CHECK_EQ(nn_ov_add(c, S("4\tdup\tf\td\tm\tr\t1\t1")), 0);
    CHECK_EQ(nn_ov_add(c, S("10\tten\tf\td\t<10@x>\t\t1\t1")), 1);
    nn_ov_close(c);

    nseen = 0;
    CHECK_EQ(nn_ov_read(path, 2, 9, take, 0), 0);
    CHECK(nseen == 2 && seen[0] == 3 && seen[1] == 4);
    nseen = 0;
    nn_ov_read(path, 0, 0xffffffffu, take, 0);
    CHECK(nseen == 4 && seen[3] == 10);
    nseen = 0;
    remove(path);
    CHECK_EQ(nn_ov_read(path, 0, 10, take, 0), 0);   /* no cache yet: nothing */
    CHECK_EQ(nseen, 0);
    TEST_DONE();
}
