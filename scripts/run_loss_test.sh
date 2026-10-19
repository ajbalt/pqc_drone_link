#!/usr/bin/env bash
# Run the T08 packet-loss test for configurations A and B, interleaved, at each loss level of
# an experiment config, and write a per-run CSV, an environment record and a summary.
#
# Usage: run_loss_test.sh <openssl-prefix> <build-dir> <config-file> <out-dir> [--allow-debug]
#   openssl-prefix  the pinned OpenSSL (see docs/build_openssl.md), used for certificates
#   build-dir       a RELEASE build containing tls_server and tls_client
#   config-file     e.g. experiments/configs/t08_loss_loopback.conf
#   out-dir         where results go (created if needed); existing results are never overwritten
#   --allow-debug   accept a non-Release build (only for the smoke test; never report those numbers)
#
# Each run starts a server and the client inside a fresh, unprivileged user and network
# namespace (unshare -Urn) and applies `tc netem` loss to that namespace's loopback, so no root
# is needed and runs cannot disturb each other or the machine. Loss applies to every packet in
# both directions. That is a bring-up measurement on a development machine: label it as such
# (docs/results.md). Pi runs use the same programs with netem on the Pis or the access point
# (advisor Q10) and evidence/ as the output directory.
# Demo certificates are made in a temporary directory and deleted: keys never reach out-dir.
# Exit status: 0 if every run delivered everything and enough runs were made; 1 if not;
# 77 if netem cannot be used here (nothing was run).
set -eu
export PATH="$PATH:/usr/sbin:/sbin"

conf() { # key -> value of the first "key=value" line
  sed -n "s/^$1=\(.*\)$/\1/p" "$CONF" | head -1
}
kv() { # key, line -> value of "key=value" in the line, empty if absent
  sed -n "s/.* $1=\([^ ]*\).*/\1/p" <<< "$2" | head -1
}

# Inner mode: runs inside the namespace for one (configuration, loss) pair.
inner_run() {
  ip link set lo up
  ip link set lo mtu "$MTU"
  local args=""
  [ "$R_LOSS" = 0 ] || args="loss ${R_LOSS}%"
  [ "$DELAY" = 0 ] || args="$args delay ${DELAY}ms"
  # shellcheck disable=SC2086
  [ -z "$args" ] || tc qdisc add dev lo root netem $args
  tc qdisc show dev lo > "$R_DIR/qdisc.txt"

  "$B/tls_server" --config "$R_CFG" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
                  --store "$R_DIR/store.dat" --timeout-ms "$SRV_TMO" \
                  > "$R_DIR/server.log" 2>/dev/null &
  local spid=$! port=""
  for _ in $(seq 1 50); do
    port=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$R_DIR/server.log")
    [ -n "$port" ] && break
    sleep 0.1
  done
  [ -n "$port" ] || { echo "server did not start" >&2; kill "$spid" 2>/dev/null || true; exit 1; }

  local rc=0
  timeout $((DEADLINE + 15)) "$B/tls_client" --config "$R_CFG" --host 127.0.0.1 --port "$port" \
          --ca "$CERTS/ca.crt" --hostname "$HOST" --queue "$R_DIR/queue.dat" \
          --count "$RECORDS" --rate-hz "$RATE" --queue-max "$QCAP" --window "$WINDOW" \
          --timeout-ms "$TMO" --backoff-ms "$BO" --backoff-max-ms "$BOMAX" \
          --deadline-ms $((DEADLINE * 1000)) > "$R_DIR/client.log" 2>&1 || rc=$?
  echo "$rc" > "$R_DIR/client.rc"
  kill "$spid" 2>/dev/null || true
  wait "$spid" 2>/dev/null || true
  # The namespace is new, so its TCP counters hold exactly this run: proof the loss acted.
  awk '/^Tcp:/ { if (!h) { for (i = 1; i <= NF; i++) if ($i == "RetransSegs") c = i; h = 1 }
                 else print $c }' /proc/net/snmp > "$R_DIR/retrans"
}

if [ "${1:-}" = --inner ]; then
  inner_run
  exit 0
fi

P=${1:?openssl prefix}; B=${2:?build dir}; CONF=${3:?config file}; OUT=${4:?out dir}
ALLOW_DEBUG=no
[ "${5:-}" != --allow-debug ] || ALLOW_DEBUG=yes
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)

NAME=$(conf name); CONFIGS=$(conf configs); LOSSES=$(conf loss_percent_list)
DELAY=$(conf delay_ms); MTU=$(conf mtu); REPS=$(conf repetitions); MINRUNS=$(conf min_runs)
RECORDS=$(conf records); RATE=$(conf rate_hz); QCAP=$(conf queue_capacity)
WINDOW=$(conf window); TMO=$(conf timeout_ms); SRV_TMO=$(conf server_timeout_ms)
BO=$(conf backoff_ms); BOMAX=$(conf backoff_max_ms); DEADLINE=$(conf run_deadline_s)
HOST=$(conf hostname)
for v in NAME CONFIGS LOSSES DELAY MTU REPS MINRUNS RECORDS RATE QCAP WINDOW TMO SRV_TMO BO \
         BOMAX DEADLINE HOST; do
  [ -n "${!v}" ] || { echo "config $CONF is missing a value for $v" >&2; exit 2; }
done
for f in "$B/tls_server" "$B/tls_client"; do
  [ -x "$f" ] || { echo "missing $f: build first" >&2; exit 2; }
done
if [ "$ALLOW_DEBUG" = no ]; then
  grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$B/CMakeCache.txt" || {
    echo "$B is not a Release build; Debug numbers are never reported" >&2; exit 2; }
fi

# Can this machine apply netem inside a private namespace? If not, say so and stop.
PROBE=$(unshare -Urn bash -c \
  'ip link set lo up && ip link set lo mtu 1500 && tc qdisc add dev lo root netem loss 10% \
   && tc qdisc show dev lo' 2>&1) || {
  echo "cannot apply tc netem in an unprivileged namespace here: $PROBE" >&2; exit 77; }

RUN="${NAME}_$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
WORKROOT=$(mktemp -d)
CERTS="$WORKROOT/certs"
mkdir "$CERTS"
cleanup() { kill $(jobs -p) 2>/dev/null || true; rm -rf "$WORKROOT"; }
trap cleanup EXIT
export LD_LIBRARY_PATH="$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}"
bash "$here/make_demo_certs.sh" "$P/bin/openssl" "$CERTS" > /dev/null

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
  echo "platform_note=client and server on one machine, loopback; not Pi data"
  echo "netem_probe=$PROBE"
  echo "--- openssl version -a"
  "$P/bin/openssl" version -a
} > "$META"

CSV="$OUT/$RUN.runs.csv"
echo "run_id,config,loss_percent,repetition,netem,records,rate_hz,queue_capacity,window,timeout_ms,generated,accepted,rejected,acked,pending,peak,lost_links,connect_failures,duration_ms,drain_ms,stored,tcp_retrans_segs,client_rc" > "$CSV"

export B CONF CERTS MTU DELAY SRV_TMO DEADLINE RECORDS RATE QCAP WINDOW TMO BO BOMAX HOST
total=$(( REPS * $(tr ',' '\n' <<< "$LOSSES" | wc -l) * $(tr ',' '\n' <<< "$CONFIGS" | wc -l) ))
n=0; status=0
# Repetition outermost, then loss, then configuration (A and B side by side): slow drift in
# load or temperature hits both configurations equally.
for rep in $(seq 1 "$REPS"); do
  for loss in ${LOSSES//,/ }; do
    for cfg in ${CONFIGS//,/ }; do
      n=$((n + 1))
      export R_CFG=$cfg R_LOSS=$loss R_DIR="$WORKROOT/run_$n"
      mkdir "$R_DIR"
      unshare -Urn bash "$0" --inner || { echo "run $n failed to start" >&2; status=1; }
      line=$(grep '^done ' "$R_DIR/client.log" 2>/dev/null | tail -1 || true)
      stored=NA
      if [ -f "$R_DIR/store.dat" ]; then
        # 8-byte header, then one 40-byte entry per stored record (src/store/store.h).
        stored=$(( ($(stat -c %s "$R_DIR/store.dat") - 8) / 40 ))
      fi
      get() { v=$(kv "$1" "$line"); echo "${v:-NA}"; }
      netem="loss=${loss}%;delay_ms=$DELAY;mtu=$MTU"
      echo "${RUN}_$n,$cfg,$loss,$rep,$netem,$RECORDS,$RATE,$QCAP,$WINDOW,$TMO,$(get generated),$(get accepted),$(get rejected),$(get acked),$(get pending),$(get peak),$(get lost_links),$(get connect_failures),$(get duration_ms),$(get drain_ms),$stored,$(cat "$R_DIR/retrans" 2>/dev/null || echo NA),$(cat "$R_DIR/client.rc" 2>/dev/null || echo NA)" >> "$CSV"
      echo "run $n/$total config=$cfg loss=${loss}% rep=$rep: $(get acked)/$(get accepted) ACKed, stored=$stored, lost_links=$(get lost_links)"
      { echo "--- run $n ($cfg, loss $loss%)"; cat "$R_DIR/qdisc.txt" 2>/dev/null; } >> "$META"
    done
  done
done

python3 -I "$here/summarize_loss.py" "$CSV" --min "$MINRUNS" --csv "$OUT/$RUN.summary.csv" \
    | tee "$OUT/$RUN.summary.txt" || status=1
echo "results: $OUT/$RUN.*"
exit "$status"
