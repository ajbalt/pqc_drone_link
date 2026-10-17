#!/usr/bin/env bash
# Run the T10 handshake benchmark for configurations A and B, interleaved, from an
# experiment config, and write the raw CSVs, an environment record and a summary.
#
# Usage: run_handshake_bench.sh <openssl-prefix> <build-dir> <config-file> <out-dir>
#   openssl-prefix  the pinned OpenSSL (see docs/build_openssl.md), used for certificates
#   build-dir       a RELEASE build containing tls_server and bench_handshake
#   config-file     e.g. experiments/configs/t10_handshake_loopback.conf
#   out-dir         where results go (created if needed); existing results are never overwritten
#
# Runs both servers on this machine over loopback. That is a bring-up measurement on a
# development machine: label it as such (docs/results.md). Pi runs use the same programs
# with the servers on Pi B and evidence/ as the output directory.
# Demo certificates are made in a temporary directory and deleted: keys never reach out-dir.
set -eu
P=${1:?openssl prefix}; B=${2:?build dir}; CONF=${3:?config file}; OUT=${4:?out dir}
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)

conf() { # key -> value of the first "key=value" line
  sed -n "s/^$1=\(.*\)$/\1/p" "$CONF" | head -1
}
NAME=$(conf name); REPS=$(conf repetitions); WARM=$(conf warmup); MAXA=$(conf max_attempts)
TMO=$(conf timeout_ms); INTV=$(conf interval_ms); HOST=$(conf hostname)
for v in NAME REPS WARM MAXA TMO INTV HOST; do
  [ -n "${!v}" ] || { echo "config $CONF is missing a value for $v" >&2; exit 2; }
done
[ "$(conf loss_percent)" = 0 ] || { echo "this runner does not apply packet loss yet" >&2; exit 2; }
for f in "$B/tls_server" "$B/bench_handshake"; do
  [ -x "$f" ] || { echo "missing $f: build first" >&2; exit 2; }
done
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$B/CMakeCache.txt" || {
  echo "$B is not a Release build; Debug numbers are never reported" >&2; exit 2; }

RUN="${NAME}_$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
CERTS=$(mktemp -d)
cleanup() { kill $(jobs -p) 2>/dev/null || true; rm -rf "$CERTS"; }
trap cleanup EXIT
export LD_LIBRARY_PATH="$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}"

bash "$here/make_demo_certs.sh" "$P/bin/openssl" "$CERTS" > /dev/null

start_server() { # config -> port
  local cfg=$1 log="$CERTS/server_$1.log" port=""
  "$B/tls_server" --config "$cfg" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
                  --timeout-ms "$TMO" > "$log" 2>/dev/null &
  for _ in $(seq 1 50); do
    port=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$log")
    [ -n "$port" ] && break
    sleep 0.1
  done
  [ -n "$port" ] || { echo "server $cfg did not start" >&2; exit 1; }
  echo "$port"
}
PA=$(start_server A); PB=$(start_server B)

sensors() { # label: CPU temperature and frequency, "NA" where the machine has none
  local t="NA" f="NA"
  [ -r /sys/class/thermal/thermal_zone0/temp ] && t=$(cat /sys/class/thermal/thermal_zone0/temp)
  [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq ] && f=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
  echo "$1_cpu_temp_millic=$t"; echo "$1_cpu_freq_khz=$f"
}

META="$OUT/$RUN.meta.txt"
{
  echo "run=$RUN"
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git_commit=$(git -C "$root" rev-parse HEAD)"
  echo "git_dirty=$(git -C "$root" status --porcelain | grep -q . && echo yes || echo no)"
  echo "config_file=$CONF"
  echo "config_sha256=$(sha256sum "$CONF" | cut -d' ' -f1)"
  echo "host=$(uname -a)"
  echo "cpu_model=$(sed -n 's/^model name[^:]*: //p' /proc/cpuinfo | head -1)"
  echo "loss_method=$(conf loss_method)"
  echo "platform_note=client and both servers on one machine, loopback; not Pi data"
  sensors before
  echo "--- openssl version -a"
  "$P/bin/openssl" version -a
} > "$META"

"$B/bench_handshake" --host 127.0.0.1 --ca "$CERTS/ca.crt" --hostname "$HOST" --run-id "$RUN" \
    --port-a "$PA" --port-b "$PB" --successes "$REPS" --warmup "$WARM" --max-attempts "$MAXA" \
    --timeout-ms "$TMO" --interval-ms "$INTV" \
    --out "$OUT/$RUN.ok.csv" --fail-out "$OUT/$RUN.fail.csv" | tee "$OUT/$RUN.bench.log"
status=${PIPESTATUS[0]}
{ echo "--- after"; sensors after; } >> "$META"

python3 -I "$here/summarize_handshakes.py" "$OUT/$RUN.ok.csv" "$OUT/$RUN.fail.csv" \
    --min "$REPS" --csv "$OUT/$RUN.summary.csv" | tee "$OUT/$RUN.summary.txt" || status=1
echo "results: $OUT/$RUN.*"
exit "$status"
