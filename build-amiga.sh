#!/bin/sh
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
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
NETKIT=${NETKIT:-"$HERE/../gfopennetkit"}
OL=${OL:-"$HERE/../gfopenlayout"}
OUT=${OUT:-"$HERE/build-amiga"}
NETKIT_LIB=${NETKIT_LIB:-"$NETKIT/build-amiga/lib/libgfopennetkit.a"}
OLUI_LIB=${OLUI_LIB:-"$OL/build-amiga/ui/libolui.a"}
CFLAGS="-m68040 -O2 -fomit-frame-pointer -noixemul -Wall -Wextra -Werror -std=gnu99
        -I$HERE/include -I$NETKIT/include -I$OL/include"
mkdir -p "$OUT"
"$CC" $CFLAGS -s -o "$OUT/NewsView" "$HERE/amiga/NewsView.c" \
    "$HERE/src/ui_window.c" "$HERE/src/ui_model.c" "$HERE/src/newsrc.c" "$HERE/src/overview.c" "$HERE/src/nntp.c" \
    "$OLUI_LIB" "$NETKIT_LIB"
echo "$OUT/NewsView ($(wc -c < "$OUT/NewsView") bytes)"
sha256sum "$OUT/NewsView"
