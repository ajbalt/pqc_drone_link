#!/usr/bin/env python3
"""Tests for scripts/pcap_lo.py: the pcap format and the per-port TCP counts."""
import importlib.util
import os
import struct
import sys
import tempfile

sys.dont_write_bytecode = True  # do not leave __pycache__ next to the script

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "pcap_lo", os.path.join(HERE, "..", "scripts", "pcap_lo.py"))
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)

failures = 0


def check(cond, what):
    global failures
    if not cond:
        print(f"CHECK failed: {what}", file=sys.stderr)
        failures += 1


def frame(src, dst, seq, payload=b"", flags=0x10, options=b"", eth_pad=b"", proto=6,
          ethertype=b"\x08\x00", frag=0):
    """An Ethernet + IPv4 + TCP frame with the given ports, sequence number and payload."""
    tcp_len = 20 + len(options)
    tcp = struct.pack(">HHIIBBHHH", src, dst, seq, 0, (tcp_len // 4) << 4, flags, 65535, 0, 0)
    tcp += options + payload
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(tcp), 1, frag, 64, proto, 0,
                     bytes([127, 0, 0, 1]), bytes([127, 0, 0, 1]))
    return b"\x00" * 12 + ethertype + ip + tcp + eth_pad


# --- parse_tcp -----------------------------------------------------------------------------
t = P.parse_tcp(frame(5000, 6000, 77, b"abcde"))
check(t == (5000, 6000, 77, 0x10, 5, 20 + 20 + 5), "ports, seq, flags, payload and IP length")
t = P.parse_tcp(frame(5000, 6000, 1, b"xy", options=b"\x01\x01\x01\x01"))
check(t[4] == 2, "TCP options are skipped to find the payload")
t = P.parse_tcp(frame(5000, 6000, 1, b"xy", eth_pad=b"\x00" * 10))
check(t[4] == 2, "padding after the IP packet is not payload")
check(P.parse_tcp(frame(5000, 6000, 1, proto=17)) is None, "UDP is ignored")
check(P.parse_tcp(frame(5000, 6000, 1, ethertype=b"\x86\xdd")) is None, "IPv6 is ignored")
check(P.parse_tcp(frame(5000, 6000, 1, frag=0x2000)) is None, "fragments are ignored")
check(P.parse_tcp(frame(5000, 6000, 1)[:30]) is None, "a truncated frame is ignored")
check(P.parse_tcp(b"") is None and P.parse_tcp(b"\x00" * 14) is None, "empty and tiny frames")
bad = bytearray(frame(5000, 6000, 1, b"abc"))
bad[14 + 2:14 + 4] = struct.pack(">H", 9999)  # IP length larger than the frame
check(P.parse_tcp(bytes(bad)) is None, "an IP length beyond the frame is refused")

# --- summarise ---------------------------------------------------------------------------
pkts = [
    frame(40000, 443, 100, b"", flags=0x02),               # SYN
    frame(443, 40000, 900, b"", flags=0x12),               # SYN-ACK
    frame(40000, 443, 101, b"", flags=0x10),               # ACK
    frame(40000, 443, 101, b"A" * 100),                    # data
    frame(443, 40000, 901, b"B" * 300),                    # data back
    frame(40000, 443, 201, b"", flags=0x10),               # pure ACK
    frame(40000, 443, 101, b"A" * 100),                    # the same 100 bytes again
    frame(40000, 443, 201, b"C" * 50),                     # new data
    frame(5555, 6666, 1, b"D" * 70),                       # another connection: not counted
]
s = P.summarise(pkts, 443)
check(s["c2s"]["packets"] == 6 and s["s2c"]["packets"] == 2, "packets per direction")
check(s["c2s"]["payload_bytes"] == 250 and s["s2c"]["payload_bytes"] == 300, "payload bytes")
check(s["c2s"]["no_payload"] == 3 and s["s2c"]["no_payload"] == 1, "packets without payload")
check(s["c2s"]["retransmissions"] == 1 and s["s2c"]["retransmissions"] == 0, "one retransmission")
c2s_frames = [pkts[i] for i in (0, 2, 3, 5, 6, 7)]
check(s["c2s"]["frame_bytes"] == sum(len(f) for f in c2s_frames) and
      s["c2s"]["ip_bytes"] == sum(len(f) - 14 for f in c2s_frames),
      "frame bytes include the Ethernet header, IP bytes do not")
check(P.summarise(pkts, 9999)["c2s"]["packets"] == 0, "a port with no traffic gives zeros")

# --- the file format ---------------------------------------------------------------------
with tempfile.TemporaryDirectory() as d:
    path = os.path.join(d, "x.pcap")
    with open(path, "wb") as f:
        f.write(P.pcap_header())
        for i, fr in enumerate(pkts):
            f.write(P.pcap_record(fr, 1000.5 + i))
    got = list(P.read_pcap(path))
    check([fr for _t, fr in got] == pkts, "read_pcap returns the frames written")
    check(abs(got[0][0] - 1000.5) < 1e-5 and abs(got[2][0] - 1002.5) < 1e-5, "timestamps")
    with open(path, "rb") as f:
        head = f.read(24)
    check(head[:4] == b"\xd4\xc3\xb2\xa1" and struct.unpack("<I", head[20:24])[0] == 1,
          "magic number and Ethernet link type, as Wireshark expects")
    for name, content in (("short", b"abc"),
                          ("magic", b"\x00" * 24),
                          ("cut_hdr", P.pcap_header() + b"\x01" * 7),
                          ("cut_pkt", P.pcap_header() + P.pcap_record(pkts[0], 1.0)[:-5])):
        p = os.path.join(d, name)
        open(p, "wb").write(content)
        try:
            list(P.read_pcap(p))
            check(False, f"read_pcap should reject {name}")
        except ValueError:
            pass

if failures:
    print(f"{failures} check(s) failed", file=sys.stderr)
    sys.exit(1)
print("all checks passed")
