/* External-unpack hook tests: the command line built from a template.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennews/unpack.h"
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

int main(void)
{
    char o[256], small[12];

    CHECK_EQ(nn_unpack_command("unrar x %d/%n.rar %d", "/news/out", "film", NN_SHELL_POSIX, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "unrar x '/news/out'/'film'.rar '/news/out'"));
    CHECK_EQ(nn_unpack_command("lha x %n.lha %d", "DH1:News/Out", "demo", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "lha x \"demo\".lha \"DH1:News/Out\""));

    /* quotes inside the values cannot end the quoting */
    CHECK_EQ(nn_unpack_command("x %n", "", "it's; rm -rf ~", NN_SHELL_POSIX, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "x 'it'\\''s; rm -rf ~'"));
    /* AmigaDOS: a quote or a star in a value is refused, never escaped */
    CHECK_EQ(nn_unpack_command("x %n", "", "a\"b", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %n", "", "a*b", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %d", "DH1:a\" Delete SYS:#? ALL \"", "", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %n", "", "*N*E[0m", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    /* what a release name holds otherwise passes as it is */
    CHECK_EQ(nn_unpack_command("x %n", "", "Some.Show-S01E02 (2026) [x264]; & | > #?", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "x \"Some.Show-S01E02 (2026) [x264]; & | > #?\""));
    CHECK_EQ(nn_unpack_command("x %d", 0, 0, NN_SHELL_POSIX, o, sizeof(o)), NK_OK);   /* NULL as "" */
    CHECK(!strcmp(o, "x ''"));

    /* %% and the template left as written */
    CHECK_EQ(nn_unpack_command("echo 100%% > %d/log", "o", "", NN_SHELL_POSIX, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "echo 100% > 'o'/log"));

    /* refused: unknown %, a % at the end, control characters (no second command) */
    CHECK_EQ(nn_unpack_command("x %s", "o", "", NN_SHELL_POSIX, o, sizeof(o)), NK_EARG);
    CHECK_EQ(o[0], 0);
    CHECK_EQ(nn_unpack_command("x %", "o", "", NN_SHELL_POSIX, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %n", "o", "a\nreboot", NN_SHELL_POSIX, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %d", "o\r", "", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    /* the AmigaDOS Shell runs `...` and fills in $name inside double quotes: refused there */
    o[0] = 'z';
    CHECK_EQ(nn_unpack_command("x %n", "", "x`Delete SYS:#? ALL`", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    CHECK(o[0] == 0);
    CHECK_EQ(nn_unpack_command("x %n", "", "a$b", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %d", "${Kickstart}", "", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %n", "", "a\"b*c`d$e", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_EARG);
    /* in the POSIX single quotes both are plain text */
    CHECK_EQ(nn_unpack_command("x %n", "", "a`b$c", NN_SHELL_POSIX, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "x 'a`b$c'"));
    /* the template is the user's own: a $ there is theirs to use */
    CHECK_EQ(nn_unpack_command("x $DIR %n", "", "a", NN_SHELL_AMIGADOS, o, sizeof(o)), NK_OK);
    CHECK(!strcmp(o, "x $DIR \"a\""));
    CHECK_EQ(nn_unpack_command("x\ny", "o", "", NN_SHELL_POSIX, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command("x %n", "o", "a\x7f", NN_SHELL_POSIX, o, sizeof(o)), NK_EARG);
    CHECK_EQ(nn_unpack_command(0, "o", "", NN_SHELL_POSIX, o, sizeof(o)), NK_EARG);

    /* too long: nothing half-built */
    CHECK_EQ(nn_unpack_command("unrar x %d", "/a/long/directory", "", NN_SHELL_POSIX, small, sizeof(small)), NK_ETOOLONG);
    CHECK_EQ(small[0], 0);
    CHECK_EQ(nn_unpack_command("0123456789a", "", "", NN_SHELL_POSIX, small, sizeof(small)), NK_OK);  /* exactly fits */
    CHECK_EQ(nn_unpack_command("0123456789ab", "", "", NN_SHELL_POSIX, small, sizeof(small)), NK_ETOOLONG);

    /* the NZB's name */
    nn_unpack_name("/home/k/Big Film.NZB", o, sizeof(o));
    CHECK(!strcmp(o, "Big Film"));
    nn_unpack_name("DH0:News/demo.nzb", o, sizeof(o));
    CHECK(!strcmp(o, "demo"));
    nn_unpack_name("RAM:x.nzb.txt", o, sizeof(o));
    CHECK(!strcmp(o, "x.nzb.txt"));
    nn_unpack_name("", o, sizeof(o));
    CHECK(!strcmp(o, ""));
    nn_unpack_name("averyveryverylongname.nzb", small, sizeof(small));
    CHECK(!strcmp(small, "averyveryve"));
    TEST_DONE();
}
