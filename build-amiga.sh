#!/bin/sh
# GFOpenNews for AmigaOS 3.x: the Shell client build-amiga/GFOpenNews, with
# the os32 GCC 16 stove (amiga-gcc, libnix), 68040, no FPU. It runs
# gfopennetkit's build-amiga.sh first and links its library.
#   STOVE    the stove (default ~/AmiCradle/stoves/os32-gcc16-open6)
#   NETKIT   a gfopennetkit checkout (default ../gfopennetkit)
#   TLS      a gfopentls checkout for its headers (default ../gfopentls)
#   KEYS     a gfopenkeys checkout for its headers (default ../gfopenkeys);
#            without one the client is built with no keystore (no AUTHINFO)
# GFOpenNews for AmigaOS 3.x on the 68040 with the os32 GCC 16 stove (open6: its proto/socket.h is the one gfopennetkit needs):
# NewsView, the reader window over the .newsrc and overview cache.
#   STOVE    the stove (default ~/AmiCradle/stoves/os32-gcc16-open6)
#   NETKIT   a gfopennetkit checkout, its build-amiga.sh run (default ../gfopennetkit)
#   OL       a gfopenlayout checkout, its build-ui-amiga.sh run (default ../gfopenlayout)
#   OUT      the output directory (default build-amiga)
# MIT licence, (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
export STOVE
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
NETKIT=${NETKIT:-"$HERE/../gfopennetkit"}
TLS=${TLS:-"$HERE/../gfopentls"}
KEYS=${KEYS:-"$HERE/../gfopenkeys"}
OUT=${OUT:-"$HERE/build-amiga"}
CFLAGS="-m68040 -O2 -fomit-frame-pointer -noixemul -Wall -Wextra -Wno-unused-parameter -std=gnu99
        -I$HERE/include -I$NETKIT/include -I$TLS/include"
if [ -f "$KEYS/include/proto/gfopenkeys.h" ]; then
    CFLAGS="$CFLAGS -DHAVE_GFOPENKEYS -I$KEYS/include"
else
    echo "no gfopenkeys headers in $KEYS: building without the keystore (no AUTHINFO)"
fi
TLS="$TLS" OUT="$OUT/netkit" sh "$NETKIT/build-amiga.sh"
mkdir -p "$OUT/obj"
OBJS=""
for s in src/nntp.c src/newsrc.c src/overview.c src/decode.c src/partfile.c src/nzb.c src/queue.c src/md5.c src/par2.c src/path.c src/cli.c src/amiga/plat_amiga.c src/ui_window.c src/ui_model.c
