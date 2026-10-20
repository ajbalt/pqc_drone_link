#!/usr/bin/env bash
# Record the wire for the advisor package: a real .pcap and the TLS-aware proxy's summary of a
# 5-record run for configurations A and B, plus one run where the proxy damages the client's
# RECORD (T09). Everything is kept in <out-dir> (created; an existing one is refused).
#
# Usage: capture_demo.sh <openssl-prefix> <build-dir> <out-dir>
#   openssl-prefix  the pinned OpenSSL, used for demo certificates (deleted afterwards)
#   build-dir       a build containing tls_server and tls_client
#   out-dir         receives, per case NAME in {A, B, B_tamper}:
#                     wire_NAME.pcap         the loopback capture (opens in Wireshark)
#                     wire_NAME.front.txt    packet and byte counts, client <-> proxy
#                     wire_NAME.back.txt     packet and byte counts, proxy <-> server
#                     wire_NAME.proxy.txt    the proxy's summary: records, hello groups, suite
#                     wire_NAME.client.txt   the client's output
#                     wire_NAME.server.txt   the server's output
#
# It runs inside a private user and network namespace (no root), with the loopback MTU set to
# 1500. Development-computer capture, not Pi data and not gate evidence (docs/results.md).
# Exit status 77 if such a namespace cannot be made here.
set -eu
export PATH="$PATH:/usr/sbin:/sbin"
here=$(cd "$(dirname "$0")" && pwd)
if [ "${1:-}" != --inner ]; then
  P=${1:?openssl prefix}; B=${2:?build dir}; OUT=${3:?out dir}
  unshare -Urn true 2>/dev/null || { echo "no unprivileged network namespace here" >&2; exit 77; }
  [ -x "$B/tls_server" ] && [ -x "$B/tls_client" ] || { echo "build first: $B" >&2; exit 2; }
  mkdir "$OUT"
  CERTS=$(mktemp -d); trap 'rm -rf "$CERTS"' EXIT
  export LD_LIBRARY_PATH="$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}"
  bash "$here/make_demo_certs.sh" "$P/bin/openssl" "$CERTS" > /dev/null
  OUT=$(cd "$OUT" && pwd) CERTS=$CERTS B=$(cd "$B" && pwd) exec unshare -Urn bash "$0" --inner
fi

ip link set lo up && ip link set lo mtu 1500
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$WORK" "$CERTS"' EXIT

settle() {
  for p in "$@"; do
    for _ in $(seq 1 100); do kill -0 "$p" 2>/dev/null || break; sleep 0.1; done
    kill "$p" 2>/dev/null || true; wait "$p" 2>/dev/null || true
  done
}
start_server() { # LOG CONFIG MAX_CONNS STORE -> SPORT SPID
  : > "$1"
  "$B/tls_server" --config "$2" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
                  --store "$4" --max-conns "$3" --timeout-ms 3000 > "$1" 2>/dev/null &
  SPID=$!; SPORT=""
  for _ in $(seq 1 50); do
    SPORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$1"); [ -n "$SPORT" ] && return; sleep 0.1
  done
}
run_client() { # LOG CONFIG PORT QUEUE [args...]
  local log=$1 cfg=$2 port=$3 queue=$4; shift 4
  timeout 30 "$B/tls_client" --config "$cfg" --host 127.0.0.1 --port "$port" --ca "$CERTS/ca.crt" \
          --queue "$queue" --timeout-ms 3000 --rate-hz 0 --backoff-ms 50 "$@" > "$log" 2>&1 || true
}

start_server "$WORK/probe.log" B 1 "$WORK/probe.dat"; DEAD=$SPORT
kill $SPID 2>/dev/null || true; wait $SPID 2>/dev/null || true

# demo NAME CONFIG [tamper-direction-and-index]
demo() {
  local name=$1 cfg=$2 tamper=${3:-} conns=1
  [ -z "$tamper" ] || conns=2
  run_client "$WORK/$name.fill" $cfg "$DEAD" "$WORK/$name.q" --count 5 --deadline-ms 300
  start_server "$WORK/$name.server" $cfg $conns "$WORK/$name.store"
  python3 -I "$here/tls_proxy.py" --upstream-port "$SPORT" --capture "$WORK/$name.px" \
          --max-conns $conns ${tamper:+--tamper $tamper} > "$WORK/$name.px.out" 2>&1 &
  XPID=$!
  for _ in $(seq 1 50); do
    XPORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$WORK/$name.px.out"); [ -n "$XPORT" ] && break; sleep 0.1
  done
  python3 -I "$here/pcap_lo.py" record --out "$OUT/wire_$name.pcap" > "$WORK/$name.rec" 2>&1 &
  RPID=$!
  for _ in $(seq 1 50); do grep -q recording "$WORK/$name.rec" && break; sleep 0.1; done
  run_client "$OUT/wire_$name.client.txt" $cfg "$XPORT" "$WORK/$name.q" --count 0
  settle $SPID $XPID
  sleep 0.3; kill $RPID 2>/dev/null || true; wait $RPID 2>/dev/null || true
  python3 -I "$here/pcap_lo.py" summary "$OUT/wire_$name.pcap" --port "$XPORT" > "$OUT/wire_$name.front.txt"
  python3 -I "$here/pcap_lo.py" summary "$OUT/wire_$name.pcap" --port "$SPORT" > "$OUT/wire_$name.back.txt"
  cp "$WORK/$name.px.summary" "$OUT/wire_$name.proxy.txt"
  cp "$WORK/$name.server" "$OUT/wire_$name.server.txt"
}

demo A A
demo B B
# The client sends Finished, the RECORD, close_notify: the RECORD is the second type-23 record.
demo B_tamper B c2s:2
echo "kept in $OUT:"; ls "$OUT"
