#!/usr/bin/env python3
"""Makes a small PAR2 recovery set for the tests, written separately from the
C code (from the Parchive 2.0 specification) so the two check each other.
Slow pure Python: only for small test files.

    par2make.py OUTBASE SLICE NREC FILE...

writes OUTBASE.par2 (no recovery data) and OUTBASE.volNN+MM.par2 volumes
holding 1, 2, 4, ... recovery blocks. MIT licence, (c) 2026 AmiCradle Team.
"""
import hashlib
import os
import struct
import sys
import zlib

POLY = 0x1100B
EXP = [0] * (65535 * 2)
LOG = [0] * 65536
x = 1
for i in range(65535):
    EXP[i] = x
    LOG[x] = i
    x <<= 1
    if x & 0x10000:
        x ^= POLY
for i in range(65535, 65535 * 2):
    EXP[i] = EXP[i - 65535]


def gmul(a, b):
    return 0 if a == 0 or b == 0 else EXP[LOG[a] + LOG[b]]


def gpow(a, e):
    return 1 if e == 0 else (0 if a == 0 else EXP[(LOG[a] * e) % 65535])


def consts(n):
    out, k = [], 0
    while len(out) < n:
        k += 1
        if k % 3 and k % 5 and k % 17 and k % 257:
            out.append(EXP[k % 65535])
    return out


def packet(set_id, ptype, body):
    assert len(ptype) == 16 and len(body) % 4 == 0
    length = 64 + len(body)
    rest = set_id + ptype + body
    return b"PAR2\0PKT" + struct.pack("<Q", length) + hashlib.md5(rest).digest() + rest


def pad4(b):
    return b + b"\0" * (-len(b) % 4)


def make(outbase, slice_size, nrec, paths):
    files = []
    for p in paths:
        data = open(p, "rb").read()
        name = os.path.basename(p).encode()
        md5_16k = hashlib.md5(data[:16384]).digest()
        fid = hashlib.md5(md5_16k + struct.pack("<Q", len(data)) + name).digest()
        files.append({"id": fid, "data": data, "name": name, "md5_16k": md5_16k})
    files.sort(key=lambda f: f["id"])
    main_body = struct.pack("<QI", slice_size, len(files)) + b"".join(f["id"] for f in files)
    set_id = hashlib.md5(main_body).digest()
    common = [packet(set_id, b"PAR 2.0\0Main\0\0\0\0", main_body)]
    slices = []
    for f in files:
        d = f["data"]
        common.append(packet(set_id, b"PAR 2.0\0FileDesc",
                             f["id"] + hashlib.md5(d).digest() + f["md5_16k"]
                             + struct.pack("<Q", len(d)) + pad4(f["name"])))
        ifsc = f["id"]
        for o in range(0, len(d), slice_size):
            s = d[o:o + slice_size].ljust(slice_size, b"\0")
            slices.append(s)
            ifsc += hashlib.md5(s).digest() + struct.pack("<I", zlib.crc32(s))
        common.append(packet(set_id, b"PAR 2.0\0IFSC\0\0\0\0", ifsc))
    creator = packet(set_id, b"PAR 2.0\0Creator\0", pad4(b"GFOpenNews tests"))
    with open(outbase + ".par2", "wb") as o:
        o.write(b"".join(common) + creator)
    c = consts(len(slices))
    words = [struct.unpack("<%dH" % (slice_size // 2), s) for s in slices]

    def recovery(e):
        acc = [0] * (slice_size // 2)
        for i, w in enumerate(words):
            m = gpow(c[i], e)
            for j, v in enumerate(w):
                if v:
                    acc[j] ^= gmul(m, v)
        return struct.pack("<%dH" % len(acc), *acc)

    e, count = 0, 1
    while e < nrec:
        n = min(count, nrec - e)
        name = "%s.vol%02d+%02d.par2" % (outbase, e, n)
        with open(name, "wb") as o:
            for k in range(e, e + n):
                o.write(packet(set_id, b"PAR 2.0\0RecvSlic", struct.pack("<I", k) + recovery(k)))
            o.write(b"".join(common) + creator)
        e += n
        count *= 2


if __name__ == "__main__":
    make(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:])
