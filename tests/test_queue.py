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
import zlib

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


def yenc(data, line=128):
    o, col = bytearray(), 0
    for b in data:
        c = (b + 42) & 255
        if c in (0, 10, 13, 61) or (col == 0 and c in (46, 32, 9)):
            o += b"=" + bytes([(c + 64) & 255])
        else:
            o.append(c)
        col += 1
        if col >= line:
            o += b"\n"
            col = 0
    if col:
        o += b"\n"
    return bytes(o)


def post(name, data, nparts, where, tag):
    """Writes the parts to spools; where(part) -> list of spools."""
    size = len(data)
    step = (size + nparts - 1) // nparts
    crc = zlib.crc32(data) & 0xffffffff
    segs = []
    for p in range(1, nparts + 1):
        b, e = (p - 1) * step, min(p * step, size)
        chunk = data[b:e]
        body = b"some text first\n"
        if nparts > 1:
            body += b"=ybegin part=%d total=%d line=128 size=%d name=%s\n" % (p, nparts, size, name.encode())
            body += b"=ypart begin=%d end=%d\n" % (b + 1, e)
        else:
            body += b"=ybegin line=128 size=%d name=%s\n" % (size, name.encode())
        body += yenc(chunk)
        body += b"=yend size=%d part=%d pcrc32=%08x crc32=%08x\n" % (len(chunk), p, zlib.crc32(chunk) & 0xffffffff, crc)
        mid = "%s-%d@fake.test" % (tag, p)
        for sp in where(p):
            with open(os.path.join(sp, mid.replace("<", "").replace(">", "") + ".art"), "wb") as f:
                f.write(body)
        segs.append((p, len(body), mid))
    return segs


def rnd(n, seed):
    out, x = bytearray(), seed
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7fffffff
        out.append((x >> 16) & 255)
    return bytes(out)


a = rnd(50000, 1) + bytes(range(256)) * 4      # every byte value too
b = rnd(3000, 2)
c = rnd(5000, 3)
segs_a = post("amiga demo.lha", a, 5, lambda p: [spool2] if p == 3 else [spool1, spool2], "a")
segs_b = post("small.txt", b, 1, lambda p: [spool1], "b")
segs_c = post("broken.bin", c, 2, lambda p: [spool1] if p == 1 else [], "c")


def nzb_file(subject, segs):
    s = '<file poster="t" date="0" subject="%s"><groups><group>alt.binaries.test</group></groups><segments>' % subject
    for n, by, mid in segs:
        s += '<segment bytes="%d" number="%d">%s</segment>' % (by, n, mid)
    return s + "</segments></file>"


nzb = os.path.join(work, "test.nzb")
with open(nzb, "w") as f:
    f.write('<?xml version="1.0"?><nzb>' +
            nzb_file("&quot;amiga demo.lha&quot; yEnc (1/5)", segs_a) +
            nzb_file("&quot;small.txt&quot; yEnc (1/1)", segs_b) +
            nzb_file("&quot;broken.bin&quot; yEnc (1/2)", segs_c) + "</nzb>")

env = dict(os.environ, NEWS_PW="s3cret")
base = [cli, "--server", "127.0.0.1", "--port", p1, "--server2", "127.0.0.1:" + p2,
        "--user", "dale", "--pass-env", "NEWS_PW", "--out", out, "--conns", "2"]
r = subprocess.run(base + ["--limit", "3", "nzb", nzb], capture_output=True, text=True, env=env, timeout=60)
check("added 3 files" in r.stdout, "added: %r %r" % (r.stdout, r.stderr))
check(not os.path.exists(os.path.join(out, "amiga demo.lha")), "not finished after 3 segments")
check(os.path.exists(os.path.join(out, "queue.db")), "queue journal written")
r = subprocess.run(base + ["resume"], capture_output=True, text=True, env=env, timeout=60)
lines = {l.split("\t")[0]: l.split("\t") for l in r.stdout.splitlines()}
check(lines.get("amiga demo.lha", [None, None])[1] == "done", "demo done: %r %r" % (r.stdout, r.stderr))
check(lines.get("small.txt", [None, None])[1] == "done", "small done: %r" % r.stdout)
check(lines.get("broken.bin", [None, None])[1] == "incomplete", "broken incomplete: %r" % r.stdout)
check(r.returncode == 3, "exit status 3 when a file is incomplete: %d" % r.returncode)
with open(os.path.join(out, "amiga demo.lha"), "rb") as f:
    check(f.read() == a, "demo bytes")
with open(os.path.join(out, "small.txt"), "rb") as f:
    check(f.read() == b, "small bytes")
check(os.path.exists(os.path.join(out, "broken.bin.part")), "broken keeps its .part for repair")
check(not os.path.exists(os.path.join(out, "amiga demo.lha.parts")), "state removed when done")
# a third run has nothing left to fetch and says the same
r = subprocess.run(base + ["resume"], capture_output=True, text=True, env=env, timeout=60)
check("amiga demo.lha\tdone\t5/5" in r.stdout, "stable after resume: %r" % r.stdout)
print("queue tests: %d failed" % fails)
sys.exit(1 if fails else 0)
