#!/usr/bin/env python3
"""Count TCP payload bytes in each direction of one connection (loopback bring-up helper).

Usage: proxy_count.py <listen-port> <upstream-port>
Accepts one connection on 127.0.0.1:<listen-port>, forwards it to <upstream-port>, and prints
"<client-to-server bytes> <server-to-client bytes>" when it closes. Counts payload only: no
TCP/IP headers, no ACK-only packets, no retransmissions.
"""
import socket
import sys
import threading

listen_port, upstream_port = int(sys.argv[1]), int(sys.argv[2])
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", listen_port))
srv.listen(1)
client, _ = srv.accept()
upstream = socket.create_connection(("127.0.0.1", upstream_port))
count = {"c2s": 0, "s2c": 0}


def pump(src, dst, key):
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            count[key] += len(data)
            dst.sendall(data)
    except OSError:
        pass
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


threads = [
    threading.Thread(target=pump, args=(client, upstream, "c2s")),
    threading.Thread(target=pump, args=(upstream, client, "s2c")),
]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(count["c2s"], count["s2c"])
