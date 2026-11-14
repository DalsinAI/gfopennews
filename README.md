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
| Kill and score files | `score.h` | plain-text rules in `[group, group]` sections: `score N FIELD PATTERN`, `kill FIELD PATTERN`, `lines<`/`lines>`/`bytes<`/`bytes>` N; from, subject, msgid, references and xref matched whole with `*` and `?`, ignoring case; every matching rule adds its score (at most 100000 each), and only a kill rule kills; a bad line is reported by number. `gfopennews-cli --score FILE headers ...` adds a score column and leaves killed articles out |
| Binaries | `binaries.h` | yEnc 1.3 (=ybegin/=ypart/=yend, part and whole-file CRC, escapes carried across lines, names may hold anything), UUencode (pad-trimmed lines), base64; all streaming line by line to a write callback. Subjects: the last `(n/m)` or `[n/m]`, the file name quoted or before `yEnc`, never a path |
| Part file | `nn_pf_*` | assembles a file from parts in any order at their offsets (zero-filling ahead, since AmigaDOS can't seek past the end); the parts held are a gfopennetkit journal, so a restart fetches only what is missing; the whole-file CRC is joined from the part CRCs in offset order (no reading back), holes and overlaps are caught, then the file takes its final name (numbered when taken) |
| NZB | `nzb.h` | NZB 1.1 with our own small XML reader (elements, attributes, entities, comments, CDATA, DOCTYPE); segments sorted, duplicates and unsafe message-ids dropped; title and password meta |
| Queue | `queue.h` | the download queue as a gfopennetkit journal (`queue.db`): BODY by message-id over any number of connections and servers, pipelined; a 430/423 or damaged part tries the next server, and a segment fails only when every server said no; yEnc straight into the part file; a restart asks only for what is missing (and re-fetches parts whose data was lost); PAR2 volumes added paused; files missing segments keep their `.part` for PAR2 repair |
| PAR2 | `par2.h` | our own reader from the Parchive 2.0 specification (par2cmdline and libpar2 are GPL and were not used): packets checked by MD5 (damaged ones and other sets skipped, garbage between packets passed over), files verified slice by slice (CRC-32 then MD5), a download left as `<name>.part` read in place, the fewest recovery volumes picked for the damage, Reed-Solomon repair over GF(2^16) (Gauss-Jordan), files too long cut back. Our own MD5 (RFC 1321) |
| CLI | `src/cli.c` | `gfopennews-cli groups | headers GROUP [LAST] | article GROUP N | unread | mark GROUP LO[-HI] | nzb FILE | resume | post GROUPS | followup GROUP N | par2 verify|repair FILE.par2`; `post` and `followup` take the text on standard input, `--from` and (for post) `--subject`, check it as the reader window does (groups, subject, address, no line breaks in headers, not empty, 256 KB at most; a post before it connects), and print the server's 240 line; a followup gets "Re: ..." and the parent's References with its id; after a queue run with damage, the PAR2 index is read, just enough paused volumes fetched and the files repaired; with `--socks HOST:PORT`, `--server2 HOST:PORT` (backup), `--conns N`, `--out DIR`. The system calls are in `src/plat.h`: `plat_posix.c` for the host (no TLS, password from the variable `--pass-env` names, AUTHINFO only with `--allow-plain-auth`), `amiga/plat_amiga.c` for AmigaOS (bsdsocket, `--tls` through gfopentls on port 563 with the certificate checked, `--insecure` to skip the check, the password from the gfopenkeys entry `nntp/<server>/<user>`, Ctrl-C stops and `resume` carries on) |
| File names | `path.h` | directory and file parts that work for `dir/file` and `DH0:dir/file` alike |
| Reader models | `ui_model.h` | what the window's lists show, in host-testable C: subscribed groups with unread counts and find-as-you-type; a group's headers threaded by References (missing articles pruned, or kept as an empty row when they join answers; loops refused; a "Re:" thread joined to its subject's; no recursion, so a long chain can't run the stack out), folding, read marks and next unread; the queue's rows; follow-ups (References trimmed under 998 octets), checks before sending, quoting without the signature, and the POST text |
| Reader window | `ui_window.h` | three panes (groups, threads, the article), a Queue tab and a Post tab, all drawn through GFOpenLayout's UI (olui): no graphics or layers calls, and no pictures the program draws itself (an unread article is marked with a `*` column). The program owns the models and gets what was asked through `NNWinOps` |
| NewsView | `amiga/NewsView.c` | the window on the Amiga over a .newsrc and the overview cache, with no network: the window's lab check until the reader's own program joins it to NNTP |

## Building and testing

    cmake -S . -B build -DNETKIT_DIR=../gfopennetkit && cmake --build build && ctest --test-dir build

For the Amiga, `./build-cli-amiga.sh` (the os32-gcc16-open6 stove; NETKIT, TLS and KEYS name the gfopennetkit, gfopentls and gfopenkeys checkouts) builds `build-amiga/GFOpenNews`, which takes the same options and commands. Without gfopenkeys headers it builds with no keystore, and so no AUTHINFO.

`cli_fake_server` runs the CLI against gfopennetkit's fake NNTP server, directly and through its fake SOCKS5 proxy; no test needs the internet. The PAR2 tests use sets written by `tests/par2make.py`, a separate small PAR2 writer in Python, so the C reader is checked against code that does not share its mistakes (it was also checked by hand against par2cmdline: each repairs the other's sets).
With a gfopenlayout checkout next to this one (or `-DGFOPENLAYOUT_DIR=...`), the reader window and its test are built too: `test_ui_window` drives the window with olui's own pointer and key calls.

    ./build-amiga.sh        # STOVE=... NETKIT=... OL=...; makes build-amiga/NewsView

`build-amiga.sh` needs gfopennetkit's and gfopenlayout's Amiga builds first (`build-amiga.sh` and `build-ui-amiga.sh` in each).

`cli_fake_server` runs the CLI against gfopennetkit's fake NNTP server, directly and through its fake SOCKS5 proxy; no test needs the internet.
