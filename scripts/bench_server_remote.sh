#!/usr/bin/env bash
# T10 across two machines, server side: runs the A and B servers on this machine, writes
# the server's handshake CSVs and an environment record, and waits until Ctrl-C.
#
# Usage: bench_server_remote.sh <openssl-prefix> <build-dir> <config-file> <out-dir> \
#                               <bind-ip> <run-id> <cert-dir>
#   cert-dir  holds srv.crt and srv.key (scripts/make_demo_certs.sh); only ca.crt leaves it
#   run-id    use the SAME id on the client (bench_client_remote.sh); results never overwrite
# Ports: A on 4433, B on 4434. Start this first, then run the client.
set -eu
P=${1:?openssl prefix}; B=${2:?build dir}; CONF=${3:?config file}; OUT=${4:?out dir}
IP=${5:?bind ip}; RUN=${6:?run id}; CERTS=${7:?cert dir}
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)
PORT_A=4433; PORT_B=4434

conf() { sed -n "s/^$1=\(.*\)$/\1/p" "$CONF" | head -1; }
TMO=$(conf timeout_ms)
[ -n "$TMO" ] || { echo "config $CONF is missing timeout_ms" >&2; exit 2; }
[ "$(conf loss_percent)" = 0 ] || { echo "this runner does not apply packet loss" >&2; exit 2; }
[ -x "$B/tls_server" ] || { echo "missing $B/tls_server: build first" >&2; exit 2; }
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$B/CMakeCache.txt" || {
  echo "$B is not a Release build; Debug numbers are never reported" >&2; exit 2; }
for f in srv.crt srv.key; do [ -r "$CERTS/$f" ] || { echo "missing $CERTS/$f" >&2; exit 2; }; done
for c in A B; do
  [ ! -e "$OUT/$RUN.server_$c.csv" ] || { echo "$OUT/$RUN.server_$c.csv exists; use a new run id" >&2; exit 2; }
done
mkdir -p "$OUT"
STORES=$(mktemp -d)
cleanup() { kill $(jobs -p) 2>/dev/null || true; rm -rf "$STORES"; }
trap cleanup EXIT
export LD_LIBRARY_PATH="$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}"

META="$OUT/$RUN.server_meta.txt"
{
  echo "run=$RUN"
  echo "role=server"
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git_commit=$(git -C "$root" rev-parse HEAD)"
  echo "git_dirty=$(git -C "$root" status --porcelain | grep -q . && echo yes || echo no)"
  echo "config_sha256=$(sha256sum "$CONF" | cut -d' ' -f1)"
  echo "host=$(uname -a)"
  echo "cpu_model=$(sed -n 's/^model name[^:]*: //p' /proc/cpuinfo | head -1)"
  echo "cpu_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo NA)"
  echo "bind_ip=$IP"
  echo "--- openssl version -a"
  "$P/bin/openssl" version -a
} > "$META"

for c in A B; do
  port=$PORT_A; [ "$c" = B ] && port=$PORT_B
  "$B/tls_server" --config "$c" --port "$port" --bind "$IP" --cert "$CERTS/srv.crt" \
      --key "$CERTS/srv.key" --store "$STORES/store_$c.dat" --timeout-ms "$TMO" \
      --handshake-csv "$OUT/$RUN.server_$c.csv" > "$STORES/server_$c.log" 2>&1 &
done
sleep 1
for c in A B; do
  grep -q '^listening' "$STORES/server_$c.log" || {
    echo "server $c did not start:" >&2; cat "$STORES/server_$c.log" >&2; exit 1; }
done
echo "servers ready on $IP (A:$PORT_A, B:$PORT_B), run id $RUN"
echo "start the client now; press Ctrl-C here when it has finished"
wait
