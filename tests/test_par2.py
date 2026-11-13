#!/usr/bin/env python3
# PAR2 verify and repair through gfopennews-cli, on sets written by
# par2make.py (an independent writer): damaged slices, a missing file, a
# download left as <name>.part, a file too long, a damaged .par2 packet,
# and too few recovery blocks. MIT licence, (c) 2026 AmiCradle Team.
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import par2make  # noqa: E402
from newsfixture import rnd  # noqa: E402

cli, work = sys.argv[1], os.path.join(sys.argv[2], "par2-test")
fails = 0


def check(cond, what):
    global fails
    if not cond:
        print("FAIL", what)
        fails += 1


def fresh(nrec):
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    files = {"a.bin": rnd(9000, 7), "b b.dat": rnd(2600, 8), "c.txt": b"hello PAR2\n"}
    for n, d in files.items():
        with open(os.path.join(work, n), "wb") as f:
            f.write(d)
    par2make.make(os.path.join(work, "set"), 512, nrec, [os.path.join(work, n) for n in files])
    return files


def run(*args):
    return subprocess.run([cli, "par2"] + list(args) + [os.path.join(work, "set.par2")],
                          capture_output=True, text=True, timeout=60)


def same(files):
    for n, d in files.items():
        p = os.path.join(work, n)
        if not os.path.exists(p) or open(p, "rb").read() != d:
            return False
    return True


# all good
files = fresh(7)
r = run("verify")
check(r.returncode == 0 and "damaged slices 0, recovery blocks 7" in r.stdout, "clean: %r" % r.stdout)

# damage across a slice boundary, a missing file, a shortened file kept as .part
with open(os.path.join(work, "a.bin"), "r+b") as f:
    f.seek(1000)
    f.write(b"\0" * 100)
os.remove(os.path.join(work, "c.txt"))
os.rename(os.path.join(work, "b b.dat"), os.path.join(work, "b b.dat.part"))
with open(os.path.join(work, "b b.dat.part"), "r+b") as f:
    f.truncate(1500)
open(os.path.join(work, "b b.dat.parts"), "w").close()
r = run("verify")
check(r.returncode == 3, "verify finds damage: %d %r" % (r.returncode, r.stdout))
check("a.bin\tdamaged\t2/18" in r.stdout, "a.bin two slices: %r" % r.stdout)
check("c.txt\tmissing\t1/1" in r.stdout, "c.txt missing: %r" % r.stdout)
check("b b.dat\tdamaged\t4/6" in r.stdout, "b b.dat read from .part: %r" % r.stdout)
r = run("repair")
check(r.returncode == 0 and "repaired: yes" in r.stdout, "repair: %d %r %r" % (r.returncode, r.stdout, r.stderr))
check(same(files), "files rebuilt byte for byte")
check(not os.path.exists(os.path.join(work, "b b.dat.part")), ".part took its name")
check(not os.path.exists(os.path.join(work, "b b.dat.parts")), ".parts list removed")
# the damaged originals are kept as <name>.1; no .repair is left behind
k = os.path.join(work, "a.bin.1")
check(os.path.exists(k) and open(k, "rb").read()[1000:1100] == b"\0" * 100, "a.bin.1 is the damaged original")
check(not any(n.endswith(".repair") for n in os.listdir(work)), "no .repair left: %r" % os.listdir(work))

# too few blocks: says how many more
files = fresh(2)
for n in ("a.bin", "c.txt"):
    os.remove(os.path.join(work, n))
r = run("repair")
check(r.returncode == 3 and "need 17 more recovery blocks" in r.stdout, "too few: %r" % r.stdout)

# a file too long is cut back; data otherwise good
files = fresh(1)
with open(os.path.join(work, "c.txt"), "ab") as f:
    f.write(b"trailing junk")
r = run("verify")
check(r.returncode == 3 and "c.txt\ttoo long" in r.stdout, "too long: %r" % r.stdout)
r = run("repair")
check(r.returncode == 0 and same(files), "cut back: %r" % r.stdout)

# a damaged recovery packet in one volume is skipped, the rest still repair
files = fresh(3)
vol = os.path.join(work, "set.vol01+02.par2")
with open(vol, "r+b") as f:
    f.seek(200)
    f.write(b"\xff\xff\xff\xff")
with open(os.path.join(work, "a.bin"), "r+b") as f:
    f.seek(5000)
    f.write(b"x")
r = run("repair")
check(r.returncode == 0 and "recovery blocks 2" in r.stdout and same(files),
      "damaged volume: %d %r" % (r.returncode, r.stdout))

print("ok" if not fails else "%d failures" % fails)
sys.exit(1 if fails else 0)
