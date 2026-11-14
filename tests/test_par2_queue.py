#!/usr/bin/env python3
# Queue + PAR2 end to end: an NZB with two data files (one segment on no
# server), the PAR2 index and four recovery volumes (1, 2, 4 and 8 blocks,
# queued paused). The run fetches the data, reads the index, fetches just
# the volumes that cover the damage (2 + 4 blocks for 6 bad slices),
# repairs, and leaves the other volumes paused. MIT licence,
# (c) 2026 AmiCradle Team.
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import par2make  # noqa: E402
from newsfixture import nzb_file, post, rnd  # noqa: E402

cli, work = sys.argv[1], sys.argv[2]
spool1 = os.environ["NK_NNTP_SPOOL"]
p1 = os.environ["NK_NNTP_PORT"]
src = os.path.join(work, "pq-src")
out = os.path.join(work, "pq-out")
for d in (spool1, src, out):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        print("FAIL", what)
        fails += 1


data = {"film.mkv": rnd(20000, 11), "notes.nfo": rnd(700, 12)}
for n, d in data.items():
    with open(os.path.join(src, n), "wb") as f:
        f.write(d)
par2make.make(os.path.join(src, "film"), 1024, 15, [os.path.join(src, n) for n in data])
vols = sorted(n for n in os.listdir(src) if ".vol" in n)
check(vols == ["film.vol00+01.par2", "film.vol01+02.par2", "film.vol03+04.par2",
               "film.vol07+08.par2"], "volumes made: %r" % vols)

nzb = ""
# film.mkv in 4 parts of 5000 bytes; part 2 (bytes 5000-9999, slices 4-9) is nowhere
segs = post("film.mkv", data["film.mkv"], 4, lambda p: [] if p == 2 else [spool1], "f")
nzb += nzb_file("&quot;film.mkv&quot; yEnc (1/4)", segs)
segs = post("notes.nfo", data["notes.nfo"], 1, lambda p: [spool1], "n")
nzb += nzb_file("&quot;notes.nfo&quot; yEnc (1/1)", segs)
for i, n in enumerate(["film.par2"] + vols):
    segs = post(n, open(os.path.join(src, n), "rb").read(), 1, lambda p: [spool1], "p%d" % i)
    nzb += nzb_file("&quot;%s&quot; yEnc (1/1)" % n, segs)
path = os.path.join(work, "pq.nzb")
with open(path, "w") as f:
    f.write('<?xml version="1.0"?><nzb>' + nzb + "</nzb>")

env = dict(os.environ, NEWS_PW="s3cret")
r = subprocess.run([cli, "--server", "127.0.0.1", "--port", p1, "--allow-plain-auth", "--user", "dale", "--pass-env", "NEWS_PW",
                    "--out", out, "--unpack", "ls %d > %d.list && echo %n > %d.name",
                    "nzb", path], capture_output=True, text=True, env=env, timeout=120)
lines = {l.split("\t")[0]: l.split("\t") for l in r.stdout.splitlines() if "\t" in l}
check("par2: 6 damaged slices, 0 recovery blocks in" in r.stdout, "damage found: %r" % r.stdout)
check("par2: repaired yes" in r.stdout, "repaired: %r %r" % (r.stdout, r.stderr))
check(r.returncode == 0, "exit 0 once repaired: %d" % r.returncode)
for n in ("film.vol01+02.par2", "film.vol03+04.par2"):
    check(lines.get(n, [0, ""])[1] == "done", "%s fetched: %r" % (n, r.stdout))
for n in ("film.vol00+01.par2", "film.vol07+08.par2"):
    check(lines.get(n, [0, ""])[1] == "paused", "%s left paused: %r" % (n, r.stdout))
for n, d in data.items():
    p = os.path.join(out, n)
    check(os.path.exists(p) and open(p, "rb").read() == d, "%s byte for byte" % n)

# the unpack hook ran once every file was done, with the directory and name quoted
check("unpack: done" in r.stdout, "unpack ran: %r" % r.stdout)
listing = open(out + ".list").read().split() if os.path.exists(out + ".list") else []
check("film.mkv" in listing and "notes.nfo" in listing, "unpack saw the files: %r" % listing)
check(os.path.exists(out + ".name") and open(out + ".name").read() == "pq\n", "unpack got the NZB name")
# a failing unpacker: exit 4, the files stay
r = subprocess.run([cli, "--server", "127.0.0.1", "--port", p1, "--allow-plain-auth", "--user", "dale", "--pass-env", "NEWS_PW",
                    "--out", out, "--unpack", "false", "resume"], capture_output=True, text=True, env=env, timeout=120)
check(r.returncode == 4 and "unpack: failed (1)" in r.stdout, "failing unpack: %d %r" % (r.returncode, r.stdout))
r = subprocess.run([cli, "--server", "127.0.0.1", "--port", p1, "--allow-plain-auth", "--user", "dale", "--pass-env", "NEWS_PW",
                    "--out", out, "--unpack", "x %s", "resume"], capture_output=True, text=True, env=env, timeout=120)
check(r.returncode == 4 and "can't be built" in r.stderr, "bad unpack template: %d %r" % (r.returncode, r.stderr))
print("ok" if not fails else "%d failures" % fails)
sys.exit(1 if fails else 0)
