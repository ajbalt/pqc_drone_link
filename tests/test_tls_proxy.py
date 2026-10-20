#!/usr/bin/env python3
"""Tests for scripts/tls_proxy.py: record cutting, hello parsing, tampering and the proxy itself."""
import importlib.util
import os
import random
import socket
import subprocess
import sys
import tempfile
import threading

sys.dont_write_bytecode = True  # do not leave __pycache__ next to the script

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "scripts", "tls_proxy.py")
SPEC = importlib.util.spec_from_file_location("tls_proxy", SCRIPT)
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)

failures = 0


def check(cond, what):
    global failures
    if not cond:
        print(f"CHECK failed: {what}", file=sys.stderr)
        failures += 1


def u16(v):
    return v.to_bytes(2, "big")


def ext(etype, data):
    return u16(etype) + u16(len(data)) + data


def record(ctype, body, version=0x0303):
    return bytes([ctype]) + u16(version) + u16(len(body)) + body


def hs(kind, body):
    return bytes([kind]) + len(body).to_bytes(3, "big") + body


def client_hello(groups, keyshare, versions=(0x0304,), ciphers=(0x1301,), session=b"\x01" * 32):
    sg = u16(2 * len(groups)) + b"".join(u16(g) for g in groups)
    ks = b"".join(u16(g) + u16(4) + b"\xaa" * 4 for g in keyshare)
    sv = bytes([2 * len(versions)]) + b"".join(u16(v) for v in versions)
    exts = (ext(0x000A, sg) + ext(0x0033, u16(len(ks)) + ks) + ext(0x002B, sv)
            + ext(0x0000, b"\x00\x00"))  # an extension we do not read
    body = (u16(0x0303) + b"\x07" * 32 + bytes([len(session)]) + session
            + u16(2 * len(ciphers)) + b"".join(u16(c) for c in ciphers) + b"\x01\x00"
            + u16(len(exts)) + exts)
    return hs(1, body)


def server_hello(group, cipher=0x1301, hrr=False):
    key = u16(group) if hrr else u16(group) + u16(4) + b"\xbb" * 4
    exts = ext(0x002B, u16(0x0304)) + ext(0x0033, key)
    rnd = P.HRR_RANDOM if hrr else b"\x09" * 32
    body = (u16(0x0303) + rnd + b"\x00" + u16(cipher) + b"\x00" + u16(len(exts)) + exts)
    return hs(2, body)


# --- RecordParser ------------------------------------------------------------------------
stream = (record(22, b"hello" * 3) + record(20, b"\x01") + record(23, b"x" * 100)
          + record(23, b""))
for cut in range(len(stream) + 1):  # every way to split the stream in two
    rp = P.RecordParser()
    got = rp.feed(stream[:cut]) + rp.feed(stream[cut:])
    check([r[0] for r in got] == [22, 20, 23, 23], f"records found with a split at {cut}")
    check(b"".join(r[3] for r in got) == stream and not rp.malformed, "records are lossless")
rp = P.RecordParser()
check(rp.feed(stream[:7]) == [] and rp.leftover() == stream[:7], "a partial record is held back")
for name, bad in (("unknown type", bytes([0x99, 3, 3, 0, 1, 0])),
                  ("version 4", bytes([23, 4, 3, 0, 1, 0])),
                  ("too long", bytes([23, 3, 3]) + u16(P.MAX_BODY + 1))):
    rp = P.RecordParser()
    good = record(23, b"ok")
    got = rp.feed(good + bad + b"more")
    check(len(got) == 1 and rp.malformed and rp.leftover() == bad + b"more",
          f"malformed header ({name}) keeps the bytes after it")
    check(rp.feed(b"later") == [] and rp.leftover().endswith(b"later"), "stays malformed")

# --- hello parsing -----------------------------------------------------------------------
ch = P.parse_hello(client_hello([0x11EC], [0x11EC]))
check(ch and ch["kind"] == "client" and ch["groups"] == [0x11EC] and ch["keyshare"] == [0x11EC],
      "client hello: hybrid group")
check(ch["versions"] == [0x0304] and ch["ciphers"] == [0x1301], "client hello: version, suite")
ch = P.parse_hello(client_hello([0x001D, 0x0017], [0x001D], versions=(0x0304, 0x0303),
                                ciphers=(0x1301, 0x1302)))
check(ch["groups"] == [0x001D, 0x0017] and ch["keyshare"] == [0x001D], "client hello: two groups")
check(ch["versions"] == [0x0304, 0x0303] and ch["ciphers"] == [0x1301, 0x1302], "lists in order")
sh = P.parse_hello(server_hello(0x11EC))
check(sh and sh["kind"] == "server" and sh["keyshare"] == [0x11EC] and not sh["hrr"],
      "server hello: chosen group")
check(sh["ciphers"] == [0x1301] and sh["versions"] == [0x0304], "server hello: suite, version")
check(P.parse_hello(server_hello(0x11EC, hrr=True))["hrr"], "HelloRetryRequest is flagged")

whole = client_hello([0x11EC], [0x11EC])
for n in range(len(whole)):  # every truncation is refused or at least does not crash
    r = P.parse_hello(whole[:n])
    check(r is None, f"hello truncated to {n} bytes is not accepted")
check(P.parse_hello(b"") is None and P.parse_hello(b"\x05\x00\x00\x00") is None, "other types")
rng = random.Random(7)
for _ in range(3000):  # garbage must never raise
    body = bytes([rng.choice((1, 2))]) + bytes(rng.randrange(256) for _ in range(rng.randrange(80)))
    try:
        P.parse_hello(body)
    except Exception as err:  # noqa: BLE001
        check(False, f"parse_hello raised {err!r}")
        break

# --- tamper ------------------------------------------------------------------------------
rec = record(23, bytes(range(40)))
bad = P.tamper(rec)
diff = [i for i in range(len(rec)) if rec[i] != bad[i]]
check(len(bad) == len(rec) and bad[:5] == rec[:5], "header and length untouched")
check(diff == [5 + 20] and bin(rec[diff[0]] ^ bad[diff[0]]).count("1") == 1,
      "exactly one bit, in the middle of the body")
check(P.tamper(rec, 0)[5] != rec[5] and P.tamper(rec, 39)[44] != rec[44], "explicit offsets")
for args in ((record(23, b""),), (rec, 40), (rec, -1)):
    try:
        P.tamper(*args)
        check(False, f"tamper{args[1:]} should raise")
    except ValueError:
        pass

# --- argument parsing --------------------------------------------------------------------
check(P.parse_tamper("c2s:2") == ("c2s", 2) and P.parse_tamper("s2c:10") == ("s2c", 10),
      "parse --tamper")
for badspec in ("", "c2s", "c2s:0", "c2s:x", "up:1", "c2s:-1"):
    try:
        P.parse_tamper(badspec)
        check(False, f"parse_tamper({badspec!r}) should raise")
    except ValueError:
        pass

# --- the proxy itself, against a plain echo-less sink --------------------------------------


def run_proxy(extra, payload_c2s, payload_s2c):
    """Run the proxy once between a scripted client and server; return what each side got."""
    got = {"server": b"", "client": b""}
    ready = threading.Event()
    sink = socket.socket()
    sink.bind(("127.0.0.1", 0))
    sink.listen(1)

    def server():
        conn, _ = sink.accept()
        conn.sendall(payload_s2c)
        conn.shutdown(socket.SHUT_WR)
        while True:
            data = conn.recv(65536)
            if not data:
                break
            got["server"] += data
        conn.close()
        ready.set()

    t = threading.Thread(target=server)
    t.start()
    with tempfile.TemporaryDirectory() as d:
        prefix = os.path.join(d, "cap")
        proc = subprocess.Popen(
            [sys.executable, "-I", SCRIPT, "--upstream-port", str(sink.getsockname()[1]),
             "--capture", prefix, "--max-conns", "1"] + extra,
            stdout=subprocess.PIPE, text=True)
        port = int(proc.stdout.readline().strip().split("=")[1])
        c = socket.create_connection(("127.0.0.1", port))
        c.sendall(payload_c2s)
        c.shutdown(socket.SHUT_WR)
        while True:
            data = c.recv(65536)
            if not data:
                break
            got["client"] += data
        c.close()
        out = proc.communicate(timeout=10)[0]
        t.join()
        files = {n: open(os.path.join(d, n), "rb").read() for n in sorted(os.listdir(d))}
    sink.close()
    return got, out, files


c2s = client_hello([0x11EC], [0x11EC]) and (
    record(22, client_hello([0x11EC], [0x11EC]), 0x0301) + record(23, b"A" * 53)
    + record(23, b"B" * 53) + record(23, b"C" * 19))
s2c = record(22, server_hello(0x11EC)) + record(23, b"D" * 29) + record(23, b"E" * 19)

got, out, files = run_proxy([], c2s, s2c)
check(got["server"] == c2s and got["client"] == s2c, "untouched traffic is forwarded exactly")
check(files["cap.conn1.c2s.bin"] == c2s and files["cap.conn1.s2c.bin"] == s2c,
      "capture files hold the streams")
check("dir=c2s" in out and "records=22:1,23:3" in out and "hello_groups=11ec" in out
      and "hello_keyshare=11ec" in out and "hello_ciphers=1301" in out, "c2s summary")
check("dir=s2c" in out and "server_keyshare=11ec" in out and "server_version=0304" in out
      and "hrr=0" in out and "tampered=0" in out, "s2c summary")
check(files["cap.summary"].decode().strip() == out.strip(), "summary file matches the output")

got, out, files = run_proxy(["--tamper", "c2s:2"], c2s, s2c)
sent, recv = c2s, got["server"]
changed = [i for i in range(len(sent)) if sent[i] != recv[i]]
second = len(record(22, client_hello([0x11EC], [0x11EC]))) + len(record(23, b"A" * 53))
check(len(recv) == len(sent) and len(changed) == 1, "tamper c2s:2 changes exactly one byte")
check(changed and second <= changed[0] < second + 5 + 53, "...inside the 2nd type-23 record")
check(got["client"] == s2c and files["cap.conn1.c2s.bin"] == c2s,
      "the other direction and the capture are untouched")
check("dir=c2s" in out and "tampered=1" in out.split("dir=s2c")[0], "summary says tampered")

got, out, files = run_proxy(["--tamper", "s2c:1", "--tamper-conn", "2"], c2s, s2c)
check(got["client"] == s2c and got["server"] == c2s, "--tamper-conn 2 leaves connection 1 alone")

# Bytes that are not TLS at all pass through unchanged and are flagged.
junk = b"GET / HTTP/1.0\r\n\r\n" * 3
got, out, _ = run_proxy([], junk, b"HTTP/1.0 200 OK\r\n\r\n")
check(got["server"] == junk and got["client"] == b"HTTP/1.0 200 OK\r\n\r\n",
      "non-TLS bytes are forwarded as they are")
check(out.count("malformed=1") == 2, "...and flagged malformed in both directions")

# A record cut short by the close is still passed on.
cut = record(23, b"Z" * 40)[:20]
got, out, _ = run_proxy([], cut, s2c)
check(got["server"] == cut, "a partial record at the end is forwarded")

rc = subprocess.run([sys.executable, "-I", SCRIPT, "--upstream-port", "1", "--tamper", "bad"],
                    capture_output=True).returncode
check(rc == 2, "a bad --tamper exits 2")

if failures:
    print(f"{failures} check(s) failed", file=sys.stderr)
    sys.exit(1)
print("all checks passed")
