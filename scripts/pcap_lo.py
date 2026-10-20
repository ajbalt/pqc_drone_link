#!/usr/bin/env python3
"""Record the loopback interface to a .pcap file, and count its TCP traffic by port.

Usage:
  pcap_lo.py record --out FILE [--iface lo]     (stop it with SIGTERM or SIGINT)
  pcap_lo.py summary FILE --port N

`record` needs a raw packet socket, which an ordinary user does not have. Inside a private user
and network namespace (`unshare -Urn`, see scripts/run_loss_test.sh) it works without root, and
sees only that namespace's traffic. The file is a classic pcap (Ethernet link type) that
Wireshark and tcpdump open.

On the loopback interface every packet is seen twice, as outgoing and as received. Only the
received copy is written, so each packet appears once.

`summary` counts, for the TCP packets to or from port N, per direction (c2s = to port N,
s2c = from port N): packets, bytes on the wire (the whole Ethernet frame), IP bytes, TCP payload
bytes, and how many packets carried no payload (ACK, SYN, FIN), plus retransmissions (a segment
whose bytes were already sent). The payload bytes are the TLS byte stream; they equal what
tls_proxy.py counts for the same connection, which the end-to-end test checks.
"""
import argparse
import signal
import socket
import struct
import sys
import time

PCAP_MAGIC = 0xA1B2C3D4
LINKTYPE_ETHERNET = 1
ETH_P_ALL = 3
PACKET_HOST = 0  # the received copy of a packet, as opposed to PACKET_OUTGOING (4)


def pcap_header(snaplen=65535):
    return struct.pack("<IHHiIII", PCAP_MAGIC, 2, 4, 0, 0, snaplen, LINKTYPE_ETHERNET)


def pcap_record(frame, ts):
    sec = int(ts)
    return struct.pack("<IIII", sec, int((ts - sec) * 1_000_000), len(frame), len(frame)) + frame


def read_pcap(path):
    """Yield (timestamp, frame bytes) from a classic little-endian pcap file."""
    with open(path, "rb") as f:
        head = f.read(24)
        if len(head) < 24:
            raise ValueError("not a pcap file: too short")
        magic, _vmaj, _vmin, _zone, _sig, _snap, link = struct.unpack("<IHHiIII", head)
        if magic != PCAP_MAGIC or link != LINKTYPE_ETHERNET:
            raise ValueError("not a little-endian Ethernet pcap file")
        while True:
            rec = f.read(16)
            if not rec:
                return
            if len(rec) < 16:
                raise ValueError("pcap file ends inside a packet header")
            sec, usec, caplen, _orig = struct.unpack("<IIII", rec)
            frame = f.read(caplen)
            if len(frame) < caplen:
                raise ValueError("pcap file ends inside a packet")
            yield sec + usec / 1_000_000, frame


def parse_tcp(frame):
    """Return (src_port, dst_port, seq, flags, payload_len, ip_len) for an IPv4/TCP frame.

    Returns None for anything else (other protocols, fragments, truncated frames).
    """
    if len(frame) < 14 + 20 or frame[12:14] != b"\x08\x00":
        return None
    ip = frame[14:]
    ihl = (ip[0] & 0x0F) * 4
    if ip[0] >> 4 != 4 or ihl < 20 or ip[9] != 6:
        return None
    total = struct.unpack(">H", ip[2:4])[0]
    if total < ihl + 20 or total > len(ip):
        return None
    if struct.unpack(">H", ip[6:8])[0] & 0x3FFF:  # more-fragments flag or a fragment offset
        return None
    tcp = ip[ihl:total]
    src, dst, seq = struct.unpack(">HHI", tcp[:8])
    off = (tcp[12] >> 4) * 4
    if off < 20 or off > len(tcp):
        return None
    return src, dst, seq, tcp[13], len(tcp) - off, total


def summarise(frames, port):
    """Count the TCP packets to or from `port`. `frames` yields Ethernet frames."""
    out = {d: {"packets": 0, "frame_bytes": 0, "ip_bytes": 0, "payload_bytes": 0,
               "no_payload": 0, "retransmissions": 0} for d in ("c2s", "s2c")}
    highest = {}
    for frame in frames:
        t = parse_tcp(frame)
        if t is None:
            continue
        src, dst, seq, flags, plen, ip_len = t
        if dst == port:
            d = "c2s"
        elif src == port:
            d = "s2c"
        else:
            continue
        row = out[d]
        row["packets"] += 1
        row["frame_bytes"] += len(frame)
        row["ip_bytes"] += ip_len
        row["payload_bytes"] += plen
        if plen == 0:
            row["no_payload"] += 1
        else:
            key = (d, src, dst)
            end = seq + plen
            if key in highest and end <= highest[key]:
                row["retransmissions"] += 1
            highest[key] = max(highest.get(key, 0), end)
    return out


def record(path, iface):
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
    sock.bind((iface, 0))
    sock.settimeout(0.2)
    stop = []
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.append(1))
    with open(path, "xb") as f:
        f.write(pcap_header())
        f.flush()
        print("recording", flush=True)
        n = 0
        while not stop:
            try:
                frame, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if addr[2] != PACKET_HOST:
                continue
            f.write(pcap_record(frame, time.time()))
            n += 1
        # Drain what is already queued so the last packets are not lost.
        sock.settimeout(0.05)
        while True:
            try:
                frame, addr = sock.recvfrom(65535)
            except socket.timeout:
                break
            if addr[2] == PACKET_HOST:
                f.write(pcap_record(frame, time.time()))
                n += 1
    print(f"packets={n}", flush=True)
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record")
    r.add_argument("--out", required=True)
    r.add_argument("--iface", default="lo")
    s = sub.add_parser("summary")
    s.add_argument("file")
    s.add_argument("--port", type=int, required=True)
    args = ap.parse_args(argv)
    if args.cmd == "record":
        return record(args.out, args.iface)
    stats = summarise((f for _ts, f in read_pcap(args.file)), args.port)
    for d in ("c2s", "s2c"):
        print(f"dir={d} " + " ".join(f"{k}={v}" for k, v in stats[d].items()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
