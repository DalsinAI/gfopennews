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
| Binaries | `binaries.h` | yEnc 1.3 (=ybegin/=ypart/=yend, part and whole-file CRC, escapes carried across lines, names may hold anything), UUencode (pad-trimmed lines), base64; all streaming line by line to a write callback. Subjects: the last `(n/m)` or `[n/m]`, the file name quoted or before `yEnc`, never a path |
| Part file | `nn_pf_*` | assembles a file from parts in any order at their offsets (zero-filling ahead, since AmigaDOS can't seek past the end); the parts held are a gfopennetkit journal, so a restart fetches only what is missing; the whole-file CRC is joined from the part CRCs in offset order (no reading back), holes and overlaps are caught, then the file takes its final name (numbered when taken) |
| NZB | `nzb.h` | NZB 1.1 with our own small XML reader (elements, attributes, entities, comments, CDATA, DOCTYPE); segments sorted, duplicates and unsafe message-ids dropped; title and password meta |
| Queue | `queue.h` | the download queue as a gfopennetkit journal (`queue.db`): BODY by message-id over any number of connections and servers, pipelined; a 430/423 or damaged part tries the next server, and a segment fails only when every server said no; yEnc straight into the part file; a restart asks only for what is missing (and re-fetches parts whose data was lost); PAR2 volumes added paused; files missing segments keep their `.part` for PAR2 repair |
| CLI | `src/cli.c` | `gfopennews-cli groups | headers GROUP [LAST] | article GROUP N | unread | mark GROUP LO[-HI] | nzb FILE | resume`, with `--socks HOST:PORT`, `--server2 HOST:PORT` (backup), `--conns N`, `--out DIR`. Host build; the Amiga CLI (ReadArgs, bsdsocket, gfopentls, password from gfopenkeys) comes next |

## Building and testing

    cmake -S . -B build -DNETKIT_DIR=../gfopennetkit && cmake --build build && ctest --test-dir build

`cli_fake_server` runs the CLI against gfopennetkit's fake NNTP server, directly and through its fake SOCKS5 proxy; no test needs the internet.
