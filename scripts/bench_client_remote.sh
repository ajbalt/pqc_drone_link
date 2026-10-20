#!/usr/bin/env bash
# T10 across two machines, client side: runs bench_handshake against the servers started by
# bench_server_remote.sh and writes the raw CSVs and an environment record.
#
# Usage: bench_client_remote.sh <openssl-prefix> <build-dir> <config-file> <out-dir> \
#                               <server-ip> <ca.crt> <run-id>
# The server's CSVs are copied here afterwards; then run summarize_handshakes.py (the
# command is printed at the end).
set -eu
P=${1:?openssl prefix}; B=${2:?build dir}; CONF=${3:?config file}; OUT=${4:?out dir}
IP=${5:?server ip}; CA=${6:?ca.crt}; RUN=${7:?run id}
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)
PORT_A=4433; PORT_B=4434

conf() { sed -n "s/^$1=\(.*\)$/\1/p" "$CONF" | head -1; }
REPS=$(conf repetitions); WARM=$(conf warmup); MAXA=$(conf max_attempts)
TMO=$(conf timeout_ms); INTV=$(conf interval_ms); HOST=$(conf hostname)
for v in REPS WARM MAXA TMO INTV HOST; do
  [ -n "${!v}" ] || { echo "config $CONF is missing a value for $v" >&2; exit 2; }
done
[ "$(conf loss_percent)" = 0 ] || { echo "this runner does not apply packet loss" >&2; exit 2; }
[ -x "$B/bench_handshake" ] || { echo "missing $B/bench_handshake: build first" >&2; exit 2; }
[ -r "$CA" ] || { echo "missing $CA" >&2; exit 2; }
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$B/CMakeCache.txt" || {
  echo "$B is not a Release build; Debug numbers are never reported" >&2; exit 2; }
mkdir -p "$OUT"
export LD_LIBRARY_PATH="$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}"

sensors() {
  local t="NA" f="NA"
  [ -r /sys/class/thermal/thermal_zone0/temp ] && t=$(cat /sys/class/thermal/thermal_zone0/temp)
  [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq ] && f=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
  echo "$1_cpu_temp_millic=$t"; echo "$1_cpu_freq_khz=$f"
}

META="$OUT/$RUN.client_meta.txt"
{
  echo "run=$RUN"
  echo "role=client"
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git_commit=$(git -C "$root" rev-parse HEAD)"
  echo "git_dirty=$(git -C "$root" status --porcelain | grep -q . && echo yes || echo no)"
  echo "config_file=$CONF"
  echo "config_sha256=$(sha256sum "$CONF" | cut -d' ' -f1)"
  echo "host=$(uname -a)"
  echo "cpu_model=$(sed -n 's/^model name[^:]*: //p' /proc/cpuinfo | head -1)"
  echo "cpu_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo NA)"
  echo "server_ip=$IP"
  echo "ping_rtt=$(ping -c 5 -q "$IP" 2>/dev/null | tail -1 || echo NA)"
  echo "platform_note=client and server on two machines over a LAN; label as dev machines (docs/results.md)"
  sensors before
  echo "--- openssl version -a"
  "$P/bin/openssl" version -a
} > "$META"

"$B/bench_handshake" --host "$IP" --ca "$CA" --hostname "$HOST" --run-id "$RUN" \
    --port-a "$PORT_A" --port-b "$PORT_B" --successes "$REPS" --warmup "$WARM" \
    --max-attempts "$MAXA" --timeout-ms "$TMO" --interval-ms "$INTV" \
    --out "$OUT/$RUN.ok.csv" --fail-out "$OUT/$RUN.fail.csv" | tee "$OUT/$RUN.bench.log"
status=${PIPESTATUS[0]}
{ echo "--- after"; sensors after; } >> "$META"

echo "client done (status $status). Stop the server (Ctrl-C), copy its $RUN.server_*.csv and"
echo "$RUN.server_meta.txt into $OUT, then run:"
echo "  python3 -I $here/summarize_handshakes.py $OUT/$RUN.ok.csv $OUT/$RUN.fail.csv \\"
echo "    --min $REPS --csv $OUT/$RUN.summary.csv --server A=$OUT/$RUN.server_A.csv \\"
echo "    --server B=$OUT/$RUN.server_B.csv --server-warmup $WARM | tee $OUT/$RUN.summary.txt"
exit "$status"
