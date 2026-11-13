#!/usr/bin/env python3
# Runs gfopennews-cli against the fake NNTP server (and through the fake
# SOCKS5 proxy). Started by fakeserv.py --run, which sets NK_NNTP_PORT and
# NK_SOCKS5_PORT. MIT licence, (c) 2026 AmiCradle Team.
import os
import subprocess
import sys

cli, work = sys.argv[1], sys.argv[2]
port = os.environ["NK_NNTP_PORT"]
socks = os.environ["NK_SOCKS5_PORT"]
env = dict(os.environ, NEWS_PW="s3cret", BAD_PW="nope")
cache = os.path.join(work, "cli-cache")
rc = os.path.join(work, "cli.newsrc")
os.makedirs(cache, exist_ok=True)
for f in os.listdir(cache):
    os.remove(os.path.join(cache, f))
if os.path.exists(rc):
    os.remove(rc)
fails = 0


def run(*args, ok=True):
    global fails
    base = [cli, "--server", "127.0.0.1", "--port", port, "--allow-plain-auth", "--user", "dale",
            "--pass-env", "NEWS_PW", "--cache", cache, "--newsrc", rc]
    p = subprocess.run(base + list(args), capture_output=True, text=True, env=env, timeout=30)
    if (p.returncode == 0) != ok:
        print("FAIL rc=%d %s\n%s%s" % (p.returncode, args, p.stdout, p.stderr))
        fails += 1
    return p.stdout


def check(cond, what):
    global fails
    if not cond:
        print("FAIL", what)
        fails += 1


out = run("groups")
check("comp.sys.amiga.misc\t3\t1\ty" in out, "groups lists the group: %r" % out)
out = run("headers", "comp.sys.amiga.misc")
check(out.splitlines() == ["1\tRe: A1200 trapdoor\ta@b.example",
                           "2\tKickstart question\tc@d.example",
                           "3\t..dots at the start\te@f.example"], "headers: %r" % out)
check(os.path.exists(os.path.join(cache, "comp.sys.amiga.misc.ov")), "cache written")
out = run("headers", "comp.sys.amiga.misc", "2")         # from the cache
check(out.splitlines()[0].startswith("2\t") and len(out.splitlines()) == 2, "last 2: %r" % out)
out = run("article", "comp.sys.amiga.misc", "3")
check(".leading dot line\n..two dots\nplain\n" in out, "article unstuffed: %r" % out)
check("Subject: ..dots at the start" in out, "article headers: %r" % out)
run("article", "comp.sys.amiga.misc", "9", ok=False)
out = run("mark", "comp.sys.amiga.misc", "1")
check(out.strip() == "comp.sys.amiga.misc: 1,3", "newsrc after reading 3 and marking 1: %r" % out)
out = run("unread")
check(out.strip() == "comp.sys.amiga.misc\t1 unread", "unread: %r" % out)
run("headers", "no.such.group", ok=False)
# wrong password is refused
p = subprocess.run([cli, "--server", "127.0.0.1", "--port", port, "--allow-plain-auth", "--user", "dale",
                    "--pass-env", "BAD_PW", "groups"], capture_output=True, text=True, env=env,
                   timeout=30)
check(p.returncode != 0 and "481" in p.stderr, "bad password refused: %r" % p.stderr)
# through the SOCKS5 proxy
p = subprocess.run([cli, "--server", "localhost", "--port", port, "--socks", "127.0.0.1:" + socks,
                    "--allow-plain-auth", "--user", "dale", "--pass-env", "NEWS_PW", "groups"],
                   capture_output=True, text=True, env=env, timeout=30)
check(p.returncode == 0 and "comp.sys.amiga.misc" in p.stdout, "via SOCKS5: %r" % (p.stdout + p.stderr))
# no TLS on the host build: AUTHINFO needs --allow-plain-auth
p = subprocess.run([cli, "--server", "127.0.0.1", "--port", port, "--user", "dale",
                    "--pass-env", "NEWS_PW", "groups"], capture_output=True, text=True, env=env, timeout=30)
check(p.returncode == 2 and "without TLS" in p.stderr, "plain auth refused: %d %r" % (p.returncode, p.stderr))
# --tls on a build without TLS stops before connecting
p = subprocess.run([cli, "--server", "127.0.0.1", "--port", port, "--tls", "--user", "dale",
                    "--pass-env", "NEWS_PW", "groups"], capture_output=True, text=True, env=env, timeout=30)
check(p.returncode == 1 and "no TLS" in p.stderr, "--tls refused: %d %r" % (p.returncode, p.stderr))
print("cli tests: %d failed" % fails)
sys.exit(1 if fails else 0)
