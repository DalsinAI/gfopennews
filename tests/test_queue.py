#!/usr/bin/env python3
# End-to-end binaries test: makes two files, posts them as yEnc articles
# into the fake servers' spools (one part only on the backup server, one
# part nowhere for the second file), writes an NZB, then runs
# gfopennews-cli nzb with a stop after a few segments, resumes, and checks
# the files byte for byte. MIT licence, (c) 2026 AmiCradle Team.
import os
import shutil
import subprocess
import sys

from newsfixture import nzb_file, post, rnd

cli, work = sys.argv[1], sys.argv[2]
spool1 = os.environ["NK_NNTP_SPOOL"]
spool2 = os.environ["NK_NNTP2_SPOOL"]
p1, p2 = os.environ["NK_NNTP_PORT"], os.environ["NK_NNTP2_PORT"]
out = os.path.join(work, "q-out")
for d in (spool1, spool2, out):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        print("FAIL", what)
        fails += 1


a = rnd(50000, 1) + bytes(range(256)) * 4      # every byte value too
b = rnd(3000, 2)
c = rnd(5000, 3)
segs_a = post("amiga demo.lha", a, 5, lambda p: [spool2] if p == 3 else [spool1, spool2], "a")
segs_b = post("small.txt", b, 1, lambda p: [spool1], "b")
segs_c = post("broken.bin", c, 2, lambda p: [spool1] if p == 1 else [], "c")
# numbered 1, 2, 7 in the NZB: a gap in the numbering is not a missing part
d = rnd(4000, 4)
segs_d = [(n, by, mid) for n, (_, by, mid) in zip((1, 2, 7), post("gap.bin", d, 3, lambda p: [spool1], "d"))]


nzb = os.path.join(work, "test.nzb")
with open(nzb, "w") as f:
    f.write('<?xml version="1.0"?><nzb>' +
            nzb_file("&quot;amiga demo.lha&quot; yEnc (1/5)", segs_a) +
            nzb_file("&quot;small.txt&quot; yEnc (1/1)", segs_b) +
            nzb_file("&quot;broken.bin&quot; yEnc (1/2)", segs_c) +
            # a raw '>' inside the quoted subject (legal XML) must not end the tag
            nzb_file("2 > 1 &quot;gap.bin&quot; yEnc (1/3)", segs_d) + "</nzb>")

env = dict(os.environ, NEWS_PW="s3cret")
base = [cli, "--server", "127.0.0.1", "--port", p1, "--server2", "127.0.0.1:" + p2,
        "--allow-plain-auth", "--user", "dale", "--pass-env", "NEWS_PW", "--out", out, "--conns", "2"]
r = subprocess.run(base + ["--limit", "3", "nzb", nzb], capture_output=True, text=True, env=env, timeout=60)
check("added 4 files" in r.stdout, "added: %r %r" % (r.stdout, r.stderr))
check(not os.path.exists(os.path.join(out, "amiga demo.lha")), "not finished after 3 segments")
check(os.path.exists(os.path.join(out, "queue.db")), "queue journal written")
r = subprocess.run(base + ["resume"], capture_output=True, text=True, env=env, timeout=60)
lines = {l.split("\t")[0]: l.split("\t") for l in r.stdout.splitlines()}
check(lines.get("amiga demo.lha", [None, None])[1] == "done", "demo done: %r %r" % (r.stdout, r.stderr))
check(lines.get("small.txt", [None, None])[1] == "done", "small done: %r" % r.stdout)
check(lines.get("gap.bin", [None, None])[1] == "done", "gap in numbering done: %r" % r.stdout)
check(lines.get("broken.bin", [None, None])[1] == "incomplete", "broken incomplete: %r" % r.stdout)
check(r.returncode == 3, "exit status 3 when a file is incomplete: %d" % r.returncode)
with open(os.path.join(out, "amiga demo.lha"), "rb") as f:
    check(f.read() == a, "demo bytes")
with open(os.path.join(out, "small.txt"), "rb") as f:
    check(f.read() == b, "small bytes")
with open(os.path.join(out, "gap.bin"), "rb") as f:
    check(f.read() == d, "gap bytes")
check(os.path.exists(os.path.join(out, "broken.bin.part")), "broken keeps its .part for repair")
check(not os.path.exists(os.path.join(out, "amiga demo.lha.parts")), "state removed when done")
# a third run has nothing left to fetch and says the same
r = subprocess.run(base + ["resume"], capture_output=True, text=True, env=env, timeout=60)
check("amiga demo.lha\tdone\t5/5" in r.stdout, "stable after resume: %r" % r.stdout)
print("queue tests: %d failed" % fails)
sys.exit(1 if fails else 0)
