# Shared helpers for the binaries tests: yEnc posting into the fake
# servers' spools and NZB writing. MIT licence, (c) 2026 AmiCradle Team.
import os
import zlib


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


def nzb_file(subject, segs):
    s = '<file poster="t" date="0" subject="%s"><groups><group>alt.binaries.test</group></groups><segments>' % subject
    for n, by, mid in segs:
        s += '<segment bytes="%d" number="%d">%s</segment>' % (by, n, mid)
    return s + "</segments></file>"
