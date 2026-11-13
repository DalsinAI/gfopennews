# gfopennews

GFOpenNews: Usenet (NNTP) newsreader for GFOS, with binaries (yEnc, UU, NZB, PAR2)

Part of GFOS (AmiCradle). Design: amicradle-teams `designs/` (GFOPENNEWS-20261113.md, GFOPENIRC-20261113.md, and the Team 2 answers of 13 Nov 2026).

Rules: commits are authored and committed by SacredTrees; draw through GFOpenLayout; third-party code keeps its own licence and goes in `third_party/` with its notice (BSD-3-Clause libtorrent is allowed; no GPL, LGPL or MPL code).

## What is here (step 2 of the design)

| Part | Header | Notes |
| --- | --- | --- |
| NNTP core | `nntp.h` | RFC 3977 and AUTHINFO (RFC 4643) over a gfopennetkit connection; non-blocking and pipelined, replies matched in order; blocks dot-unstuffed and streamed line by line (an article never has to fit in memory); an over-long block line is dropped and reported; a lost link answers every waiting command; POST bodies dot-stuffed |
| Reply parsers | `nn_parse_group`, `nn_parse_active`, `nn_parse_over` | GROUP, LIST ACTIVE, OVER/XOVER with the Xref extra field |
| .newsrc | `newsrc.h` | the classic `group: ranges` / `group! ranges` format, order kept, overlapping ranges joined, unreadable lines skipped and counted, marking read/unread, unread counts, saved aside and swapped in (AmigaDOS `Rename()` won't replace) |
| Overview cache | `overview.h` | OVER lines per group in `<cache>/<group>.ov` (group names can't climb out of the cache), only numbers above the cached high mark appended, read back in windows |
| CLI | `src/cli.c` | `gfopennews-cli groups | headers GROUP [LAST] | article GROUP N | unread | mark GROUP LO[-HI]`, with `--socks HOST:PORT`. Host build; the Amiga CLI (ReadArgs, bsdsocket, gfopentls, password from gfopenkeys) comes next |

## Building and testing

    cmake -S . -B build -DNETKIT_DIR=../gfopennetkit && cmake --build build && ctest --test-dir build

`cli_fake_server` runs the CLI against gfopennetkit's fake NNTP server, directly and through its fake SOCKS5 proxy; no test needs the internet.
