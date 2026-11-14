/* Host and AmigaOS file names. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/path.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static int dir_is(const char *path, const char *want, size_t want_at)
{
    char d[64];
    size_t at = 99;
    return nn_path_dir(path, d, sizeof(d), &at) == NK_OK && !strcmp(d, want) && at == want_at;
}

int main(void)
{
    char o[16];
    size_t at;
    CHECK(dir_is("film.par2", "", 0));
    CHECK(dir_is("out/film.par2", "out", 4));
    CHECK(dir_is("a/b/film.par2", "a/b", 4));
    CHECK(dir_is("/film.par2", "/", 1));
    CHECK(dir_is("DH0:film.par2", "DH0:", 4));
    CHECK(dir_is("DH0:News/film.par2", "DH0:News", 9));
    CHECK(dir_is("Work:", "Work:", 5));
    CHECK(dir_is("dir/", "dir", 4));
    CHECK_EQ(nn_path_dir("verylongdirectory/x", o, 8, &at), NK_EARG);
    CHECK_EQ(nn_path_dir("x", o, 0, &at), NK_EARG);

    CHECK_EQ(nn_path_join("", "f", o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "f"));
    CHECK_EQ(nn_path_join("out", "f", o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "out/f"));
    CHECK_EQ(nn_path_join("out/", "f", o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "out/f"));
    CHECK_EQ(nn_path_join("DH0:", "f", o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "DH0:f"));
    CHECK_EQ(nn_path_join("DH0:News", "f", o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "DH0:News/f"));
    CHECK_EQ(nn_path_join("directory", "longname", o, sizeof(o)), NK_EARG);
    TEST_DONE();
}
