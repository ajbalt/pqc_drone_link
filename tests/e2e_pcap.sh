#!/usr/bin/env bash
# A real packet capture of a TLS run, with TCP/IP headers, for configurations A and B: a .pcap
# of the loopback interface recorded inside a private user and network namespace (no root),
# checked against the byte counts of the TLS-aware proxy that carried the same connection.
#
# Checked:
#   - the capture is a valid pcap, and its TCP payload bytes on the client-to-proxy connection
#     equal the proxy's own stream counts in both directions (two independent captures agree,
#     and no packet was counted twice)
#   - the proxy-to-server connection carried the same bytes
#   - headers are counted: every packet costs at least the 54 bytes of Ethernet + IP + TCP
#   - no plaintext RECORD message of the client appears anywhere in the raw capture
#   - no retransmissions on a clean loopback
# Prints the handshake byte totals with and without headers (T10 "bytes on the wire").
# Exit status 77 if a private namespace with a raw packet socket cannot be made here.
# Usage: e2e_pcap.sh <tls_server> <tls_client> <tls_proxy.py> <pcap_lo.py> <cert-dir>
set -u
if [ "${1:-}" != --inner ]; then
  unshare -Urn true 2>/dev/null || { echo "SKIP: no unprivileged network namespace"; exit 77; }
  exec unshare -Urn bash "$0" --inner "$@"
fi
shift
SERVER=${1:?tls_server}; CLIENT=${2:?tls_client}; PROXY=${3:?tls_proxy.py}
PCAP=${4:?pcap_lo.py}; CERTS=${5:?cert dir}
export PATH="$PATH:/usr/sbin:/sbin"
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

ip link set lo up && ip link set lo mtu 1500 || { echo "SKIP: cannot configure lo"; exit 77; }

# settle PID...: give each process 10 s to finish by itself, then stop it. A case that goes wrong
# (a server waiting for a connection that never comes) must end as a failed check, not a hang.
settle() {
  for p in "$@"; do
    for _ in $(seq 1 100); do kill -0 "$p" 2>/dev/null || break; sleep 0.1; done
    kill "$p" 2>/dev/null; wait "$p" 2>/dev/null
  done
}

start_server() { # LOG CONFIG MAX_CONNS STORE -> SPORT SPID
  : > "$1"
  "$SERVER" --config "$2" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
            --store "$4" --max-conns "$3" --timeout-ms 3000 > "$1" 2>/dev/null &
  SPID=$!; SPORT=""
  for _ in $(seq 1 50); do
    SPORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$1")
    [ -n "$SPORT" ] && return
    sleep 0.1
  done
}
start_proxy() { # PREFIX UPSTREAM_PORT -> XPORT XPID
  python3 -I "$PROXY" --upstream-port "$2" --capture "$1" --max-conns 1 > "$1.out" 2>&1 &
  XPID=$!; XPORT=""
  for _ in $(seq 1 50); do
    XPORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$1.out")
    [ -n "$XPORT" ] && return
    sleep 0.1
  done
}
run_client() { # LOG CONFIG PORT QUEUE [args...] -> CRC
  local log=$1 cfg=$2 port=$3 queue=$4; shift 4
  timeout 30 "$CLIENT" --config "$cfg" --host 127.0.0.1 --port "$port" --ca "$CERTS/ca.crt" \
          --queue "$queue" --timeout-ms 3000 --rate-hz 0 --backoff-ms 50 "$@" > "$log" 2>&1
  CRC=$?
}
field() { grep "^dir=$2 " "$1" | tr ' ' '\n' | sed -n "s/^$3=//p" | head -1; }

# A port nothing listens on, to queue records without delivering them.
start_server "$WORK/probe.log" B 1 "$WORK/probe.dat"; DEAD=$SPORT
kill $SPID 2>/dev/null; wait $SPID 2>/dev/null

for cfg in A B; do
  d="$WORK/$cfg"; mkdir "$d"
  run_client "$d/fill.log" $cfg "$DEAD" "$d/q.dat" --count 5 --deadline-ms 300
  python3 -I - "$d/q.dat" "$d/msgs.bin" <<'PY'
import sys
q = open(sys.argv[1], "rb").read()
open(sys.argv[2], "wb").write(b"".join(q[20 + 40 * i:20 + 40 * i + 36] for i in range(5)))
PY
  start_server "$d/server.log" $cfg 1 "$d/store.dat"
  start_proxy "$d/px" "$SPORT"
  python3 -I "$PCAP" record --out "$d/cap.pcap" > "$d/rec.out" 2>&1 &
  RPID=$!
  for _ in $(seq 1 50); do grep -q recording "$d/rec.out" 2>/dev/null && break; sleep 0.1; done
  run_client "$d/client.log" $cfg "$XPORT" "$d/q.dat" --count 0
  settle $SPID $XPID
  sleep 0.3; kill $RPID 2>/dev/null; wait $RPID 2>/dev/null

  python3 -I "$PCAP" summary "$d/cap.pcap" --port "$XPORT" > "$d/front.txt" 2>&1   # client <-> proxy
  python3 -I "$PCAP" summary "$d/cap.pcap" --port "$SPORT" > "$d/back.txt" 2>&1    # proxy <-> server
  PS="$d/px.summary"
  PC2S=$(sed -n 's/^conn=1 dir=c2s bytes=\([0-9]*\).*/\1/p' "$PS")
  PS2C=$(sed -n 's/^conn=1 dir=s2c bytes=\([0-9]*\).*/\1/p' "$PS")

  check "$cfg: delivered 5 of 5" '[ $CRC -eq 0 ] && grep -q "acked=5 pending=0 " "$d/client.log"'
  check "$cfg: the pcap has the classic header (Wireshark opens it)" \
        '[ "$(head -c 4 "$d/cap.pcap" | xxd -p)" = d4c3b2a1 ]'
  check "$cfg: capture payload client->proxy = proxy count ($PC2S bytes)" \
        '[ "$(field "$d/front.txt" c2s payload_bytes)" = "$PC2S" ] && [ -n "$PC2S" ]'
  check "$cfg: capture payload proxy->client = proxy count ($PS2C bytes)" \
        '[ "$(field "$d/front.txt" s2c payload_bytes)" = "$PS2C" ] && [ -n "$PS2C" ]'
  check "$cfg: the proxy-to-server connection carried the same bytes" \
        '[ "$(field "$d/back.txt" c2s payload_bytes)" = "$PC2S" ] && [ "$(field "$d/back.txt" s2c payload_bytes)" = "$PS2C" ]'
  for dir in c2s s2c; do
    pk=$(field "$d/front.txt" $dir packets)
    over=$(( $(field "$d/front.txt" $dir frame_bytes) - $(field "$d/front.txt" $dir payload_bytes) ))
    check "$cfg $dir: $pk packets, headers cost $over bytes (at least 54 each)" \
          '[ "$over" -ge $(( 54 * pk )) ] && [ "$pk" -ge 3 ]'
  done
  check "$cfg: no retransmissions on a clean loopback" \
        '[ "$(field "$d/front.txt" c2s retransmissions)" = 0 ] && [ "$(field "$d/front.txt" s2c retransmissions)" = 0 ]'
  check "$cfg: no plaintext RECORD message anywhere in the raw capture" \
        '[ "$(python3 -I -c "
import sys
cap = open(sys.argv[1], \"rb\").read(); m = open(sys.argv[2], \"rb\").read()
print(sum(1 for i in range(5) if m[36*i:36*i+36] in cap))" "$d/cap.pcap" "$d/msgs.bin")" -eq 0 ]'
  check "$cfg: ...but the records are in the server store (the search can find them)" \
        '[ "$(python3 -I -c "
import sys
st = open(sys.argv[1], \"rb\").read(); m = open(sys.argv[2], \"rb\").read()
print(sum(1 for i in range(5) if m[36*i:36*i+36] in st))" "$d/store.dat" "$d/msgs.bin")" -eq 5 ]'
  echo "INFO $cfg: whole run (5 records) client->server $(field "$d/front.txt" c2s payload_bytes) payload / $(field "$d/front.txt" c2s frame_bytes) on the wire in $(field "$d/front.txt" c2s packets) packets; server->client $(field "$d/front.txt" s2c payload_bytes) / $(field "$d/front.txt" s2c frame_bytes) in $(field "$d/front.txt" s2c packets) packets"
done

[ $fail -eq 0 ] && echo "all pcap checks passed"
exit $fail
