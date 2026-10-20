#!/usr/bin/env python3
"""A TLS-aware TCP proxy for T09 and the on-the-wire checks: it records what crosses, describes
it, and can damage one record.

Usage: tls_proxy.py --upstream-port N [--listen-port N] [--upstream-host H] [--capture PREFIX]
                    [--tamper DIR:NTH] [--tamper-conn K] [--max-conns N]

Accepts connections on 127.0.0.1:--listen-port (0 = pick a free one; the chosen port is printed
as "listening port=N") and forwards each to the upstream server, one TLS record at a time.

For every connection and direction it writes the bytes as the sender produced them to
PREFIX.conn<K>.<dir>.bin (dir = c2s or s2c), and prints one summary line when the connection
ends (also appended to PREFIX.summary):

  conn=1 dir=c2s bytes=1795 records=20:1,22:1,23:3 malformed=0 tampered=0 hello_groups=11ec ...

records= counts TLS records by content type (20 change_cipher_spec, 21 alert, 22 handshake,
23 application data; in TLS 1.3 every encrypted record, including the encrypted handshake and
the close_notify alert, has type 23). The ClientHello and ServerHello are not encrypted, so
the proxy reads from them the groups offered and chosen, the TLS versions, and the cipher
suites: that is the on-the-wire evidence of the pinned configuration (docs/telemetry_format.md
section 8).

--tamper c2s:2 flips one bit in the middle of the 2nd type-23 record sent client-to-server
(s2c: server-to-client) on connection --tamper-conn (default 1). TLS must refuse the damaged
record: the AEAD tag no longer matches. Everything else is forwarded unchanged. Records that
cannot be parsed (a bad header) switch that direction to plain forwarding and are flagged
malformed=1.

This is a bring-up and test tool, not part of the product. It sees only the TLS byte stream, not
TCP/IP headers (see pcap_lo.py for those).
"""
import argparse
import socket
import sys
import threading

# TLS record content types (RFC 8446 section 5.1).
CCS, ALERT, HANDSHAKE, APPDATA = 20, 21, 22, 23
# Largest record body the protocol allows: 2^14 plus 256 for encryption overhead.
MAX_BODY = 16384 + 256
# HelloRetryRequest is a ServerHello whose random is this fixed value (RFC 8446 section 4.1.3).
HRR_RANDOM = bytes.fromhex(
    "cf21ad74e59a6111be1d8c021e65b891c2a211167abb8c5e079e09e2c8a8339c")

EXT_SUPPORTED_GROUPS, EXT_KEY_SHARE, EXT_SUPPORTED_VERSIONS = 0x000A, 0x0033, 0x002B


class RecordParser:
    """Cuts a TCP byte stream into TLS records, however the bytes were split up."""

    def __init__(self):
        self.buf = bytearray()
        self.malformed = False

    def feed(self, data):
        """Add bytes; return the complete records found as (type, version, body, raw) tuples.

        After a record header that cannot be right, no more records are returned and
        `malformed` is set; the caller should forward the bytes as they are.
        """
        self.buf += data
        out = []
        while not self.malformed and len(self.buf) >= 5:
            ctype = self.buf[0]
            version = (self.buf[1] << 8) | self.buf[2]
            length = (self.buf[3] << 8) | self.buf[4]
            if ctype not in (CCS, ALERT, HANDSHAKE, APPDATA) or self.buf[1] != 3 \
                    or length > MAX_BODY:
                self.malformed = True
                break
            if len(self.buf) < 5 + length:
                break
            raw = bytes(self.buf[:5 + length])
            out.append((ctype, version, raw[5:], raw))
            del self.buf[:5 + length]
        return out

    def leftover(self):
        """Bytes held back (a partial record, or everything after a bad header)."""
        return bytes(self.buf)


def _u16(b, off):
    return (b[off] << 8) | b[off + 1]


def _extensions(b, off):
    """Parse the extensions block at b[off:] (2-byte total length first) -> {type: data}."""
    exts = {}
    if off + 2 > len(b):
        return exts
    end = off + 2 + _u16(b, off)
    off += 2
    if end > len(b):
        end = len(b)
    while off + 4 <= end:
        etype, elen = _u16(b, off), _u16(b, off + 2)
        off += 4
        if off + elen > end:
            break
        exts[etype] = bytes(b[off:off + elen])
        off += elen
    return exts


def parse_hello(body):
    """Describe a ClientHello or ServerHello handshake message, or return None.

    `body` is the record body of a type-22 record that starts with the handshake header.
    Returns a dict: kind ('client'/'server'), groups (offered, client only), keyshare (groups
    that have a key share), versions, ciphers (offered list for a client, the one chosen for a
    server), hrr (True for a HelloRetryRequest). Returns None if the message is not a complete,
    well-formed hello.
    """
    if len(body) < 4 or body[0] not in (1, 2):
        return None
    hlen = (body[1] << 16) | (body[2] << 8) | body[3]
    if len(body) < 4 + hlen:
        return None  # the hello is spread over several records: not handled
    b = body[4:4 + hlen]
    try:
        off = 2 + 32  # legacy_version, random
        random = bytes(b[2:34])
        sid = b[off]
        off += 1 + sid
        info = {"kind": "client" if body[0] == 1 else "server"}
        if body[0] == 1:
            nbytes = _u16(b, off)
            suites = [_u16(b, off + 2 + i) for i in range(0, nbytes, 2)]
            off += 2 + nbytes
            off += 1 + b[off]  # compression methods
            info["ciphers"] = suites
        else:
            info["ciphers"] = [_u16(b, off)]
            off += 2 + 1  # cipher suite, compression method
            info["hrr"] = random == HRR_RANDOM
        exts = _extensions(b, off)
    except IndexError:
        return None
    info.setdefault("hrr", False)
    info["groups"] = []
    info["keyshare"] = []
    info["versions"] = []
    if EXT_SUPPORTED_GROUPS in exts:
        d = exts[EXT_SUPPORTED_GROUPS]
        info["groups"] = [_u16(d, 2 + i) for i in range(0, _u16(d, 0), 2)
                          if 2 + i + 2 <= len(d)]
    if EXT_SUPPORTED_VERSIONS in exts:
        d = exts[EXT_SUPPORTED_VERSIONS]
        if info["kind"] == "client":
            info["versions"] = [_u16(d, 1 + i) for i in range(0, d[0], 2)
                                if 1 + i + 2 <= len(d)]
        elif len(d) >= 2:
            info["versions"] = [_u16(d, 0)]
    if EXT_KEY_SHARE in exts:
        d = exts[EXT_KEY_SHARE]
        if info["kind"] == "client":
            off, end = 2, min(len(d), 2 + _u16(d, 0))
            while off + 4 <= end:
                info["keyshare"].append(_u16(d, off))
                off += 4 + _u16(d, off + 2)
        elif len(d) >= 2:
            info["keyshare"] = [_u16(d, 0)]
    return info


def tamper(raw, which=None):
    """Return a copy of the record `raw` with one bit flipped in its body.

    `which` is the byte offset in the body (default: the middle). The 5-byte header is left
    alone so the damaged record still looks like a record.
    """
    body_len = len(raw) - 5
    if body_len < 1:
        raise ValueError("no body to damage")
    pos = 5 + (body_len // 2 if which is None else which)
    if not 5 <= pos < len(raw):
        raise ValueError("offset outside the record body")
    out = bytearray(raw)
    out[pos] ^= 0x01
    return bytes(out)


def _hex(values):
    return ",".join(f"{v:04x}" for v in values) if values else "-"


class Direction:
    """Everything the proxy learns about one direction of one connection."""

    def __init__(self, name):
        self.name = name
        self.parser = RecordParser()
        self.nbytes = 0
        self.counts = {}
        self.appdata_seen = 0
        self.hello = None
        self.first_seen = False
        self.tampered = False

    def summary(self, conn):
        recs = ",".join(f"{t}:{n}" for t, n in sorted(self.counts.items())) or "-"
        parts = [f"conn={conn}", f"dir={self.name}", f"bytes={self.nbytes}",
                 f"records={recs}", f"malformed={int(self.parser.malformed)}",
                 f"tampered={int(self.tampered)}"]
        h = self.hello
        if h and h["kind"] == "client":
            parts += [f"hello_groups={_hex(h['groups'])}",
                      f"hello_keyshare={_hex(h['keyshare'])}",
                      f"hello_versions={_hex(h['versions'])}",
                      f"hello_ciphers={_hex(h['ciphers'])}"]
        elif h:
            parts += [f"server_cipher={_hex(h['ciphers'])}",
                      f"server_keyshare={_hex(h['keyshare'])}",
                      f"server_version={_hex(h['versions'])}", f"hrr={int(h['hrr'])}"]
        return " ".join(parts)


def handle(client, upstream_addr, conn_no, args, log):
    """Forward one connection, recording and (once) damaging records as asked."""
    try:
        upstream = socket.create_connection(upstream_addr)
    except OSError:
        client.close()
        return
    dirs = {"c2s": Direction("c2s"), "s2c": Direction("s2c")}
    files = {}
    if args.capture:
        for name in dirs:
            files[name] = open(f"{args.capture}.conn{conn_no}.{name}.bin", "xb")
    tamper_dir, tamper_nth = args.tamper_spec or (None, 0)
    do_tamper = conn_no == args.tamper_conn

    def pump(src, dst, d):
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                d.nbytes += len(data)
                if d.name in files:
                    files[d.name].write(data)
                out = bytearray()
                for ctype, _ver, body, raw in d.parser.feed(data):
                    d.counts[ctype] = d.counts.get(ctype, 0) + 1
                    if not d.first_seen:
                        d.first_seen = True
                        if ctype == HANDSHAKE:
                            d.hello = parse_hello(body)
                    if ctype == APPDATA:
                        d.appdata_seen += 1
                        if do_tamper and d.name == tamper_dir and d.appdata_seen == tamper_nth:
                            raw = tamper(raw)
                            d.tampered = True
                    out += raw
                if d.parser.malformed:
                    out += d.parser.leftover()
                    d.parser.buf.clear()
                dst.sendall(bytes(out))
        except OSError:
            pass
        try:
            if d.parser.buf:  # a record cut short by the close: pass it on as it is
                dst.sendall(bytes(d.parser.buf))
            dst.shutdown(socket.SHUT_WR)
        except OSError:
            pass

    threads = [threading.Thread(target=pump, args=(client, upstream, dirs["c2s"])),
               threading.Thread(target=pump, args=(upstream, client, dirs["s2c"]))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    for f in files.values():
        f.close()
    client.close()
    upstream.close()
    with log["lock"]:
        for name in ("c2s", "s2c"):
            line = dirs[name].summary(conn_no)
            print(line, flush=True)
            if log["file"]:
                log["file"].write(line + "\n")
                log["file"].flush()


def parse_tamper(spec):
    """'c2s:2' -> ('c2s', 2). Raises ValueError for anything else."""
    direction, sep, nth = spec.partition(":")
    if not sep or direction not in ("c2s", "s2c") or not nth.isdigit() or int(nth) < 1:
        raise ValueError(f"bad --tamper value: {spec!r} (want c2s:N or s2c:N, N >= 1)")
    return direction, int(nth)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--upstream-port", type=int, required=True)
    ap.add_argument("--upstream-host", default="127.0.0.1")
    ap.add_argument("--listen-port", type=int, default=0)
    ap.add_argument("--capture", help="prefix for the per-connection stream files")
    ap.add_argument("--tamper", help="DIR:NTH, for example c2s:2")
    ap.add_argument("--tamper-conn", type=int, default=1)
    ap.add_argument("--max-conns", type=int, default=0, help="stop after N connections")
    args = ap.parse_args(argv)
    try:
        args.tamper_spec = parse_tamper(args.tamper) if args.tamper else None
    except ValueError as err:
        print(err, file=sys.stderr)
        return 2

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.listen_port))
    srv.listen(8)
    print(f"listening port={srv.getsockname()[1]}", flush=True)

    log = {"lock": threading.Lock(),
           "file": open(args.capture + ".summary", "x") if args.capture else None}
    handlers = []
    n = 0
    while args.max_conns == 0 or n < args.max_conns:
        client, _ = srv.accept()
        n += 1
        t = threading.Thread(target=handle, args=(
            client, (args.upstream_host, args.upstream_port), n, args, log))
        t.start()
        handlers.append(t)
    for t in handlers:
        t.join()
    if log["file"]:
        log["file"].close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
