#!/usr/bin/env bash
# Smoke test of bench_handshake against real tls_server processes (A and B), plus
# its failure paths. Checks the CSV files, not the speed.
# Usage: e2e_bench.sh <tls_server> <bench_handshake> <summarize.py> <cert-dir>
set -u
SERVER=${1:?tls_server}; BENCH=${2:?bench_handshake}; SUMM=${3:?summarize}; CERTS=${4:?cert dir}
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

start_server() { # config -> prints port
  local cfg=$1 log="$WORK/server_$1.log" port=""
  "$SERVER" --config "$cfg" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
            --store "$WORK/store_$1.dat" --timeout-ms 3000 > "$log" 2>/dev/null &
  for _ in $(seq 1 50); do
    port=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$log")
    [ -n "$port" ] && break
    sleep 0.1
  done
  echo "$port"
}
PA=$(start_server A); PB=$(start_server B)
check "both servers started" '[ -n "$PA" ] && [ -n "$PB" ]'

# 1. Interleaved A and B: 5 measured + 2 warm-up per configuration.
timeout 60 "$BENCH" --host 127.0.0.1 --ca "$CERTS/ca.crt" --run-id smoke \
        --port-a "$PA" --port-b "$PB" --successes 5 --warmup 2 \
        --out "$WORK/ok.csv" --fail-out "$WORK/fail.csv" --allow-debug > "$WORK/bench.log" 2>&1
check "harness exits 0 when targets are reached" '[ $? -eq 0 ]'
check "ok.csv: 14 handshakes (2 configs x (5 + 2))" '[ "$(tail -n +2 "$WORK/ok.csv" | wc -l)" -eq 14 ]'
check "ok.csv: 4 warm-up rows" '[ "$(tail -n +2 "$WORK/ok.csv" | awk -F, "\$4==1" | wc -l)" -eq 4 ]'
check "fail.csv has only the header" '[ "$(wc -l < "$WORK/fail.csv")" -eq 1 ]'
check "config A rows report x25519" 'tail -n +2 "$WORK/ok.csv" | awk -F, "\$2==\"A\"{print \$12}" | sort -u | grep -qix x25519'
check "config B rows report X25519MLKEM768" '[ "$(tail -n +2 "$WORK/ok.csv" | awk -F, "\$2==\"B\"{print \$12}" | sort -u)" = X25519MLKEM768 ]'
check "one cipher suite in every row" '[ "$(tail -n +2 "$WORK/ok.csv" | cut -d, -f13 | sort -u)" = TLS_AES_128_GCM_SHA256 ]'
check "attempts alternate A,B" '[ "$(tail -n +2 "$WORK/ok.csv" | head -4 | cut -d, -f2 | tr -d "\n")" = ABAB ]'
check "hybrid sends more bytes than classical" '
  a=$(tail -n +2 "$WORK/ok.csv" | awk -F, "\$2==\"A\"{print \$9; exit}")
  b=$(tail -n +2 "$WORK/ok.csv" | awk -F, "\$2==\"B\"{print \$9; exit}")
  [ "$b" -gt $((a + 1000)) ]'
check "latencies are positive" '! tail -n +2 "$WORK/ok.csv" | awk -F, "\$6<=0 || \$7<=0 || \$7>\$6" | grep -q .'
check "summary runs and reports both configs" 'python3 -I "$SUMM" "$WORK/ok.csv" "$WORK/fail.csv" --min 5 | grep -c "^== config" | grep -qx 2'
check "summary marks too few rows INSUFFICIENT" '! python3 -I "$SUMM" "$WORK/ok.csv" --min 100 > /dev/null'

# 2. Existing output files are refused, not overwritten.
"$BENCH" --host 127.0.0.1 --ca "$CERTS/ca.crt" --run-id smoke --port-a "$PA" \
         --out "$WORK/ok.csv" --fail-out "$WORK/fail2.csv" --allow-debug > /dev/null 2>&1
check "refuses to overwrite an existing result" '[ $? -ne 0 ]'

# 3. A server that is not there: failures go to fail.csv, exit code is non-zero.
"$BENCH" --host 127.0.0.1 --ca "$CERTS/ca.crt" --run-id broken --port-a 1 \
         --max-attempts 4 --warmup 0 --successes 1 \
         --out "$WORK/ok3.csv" --fail-out "$WORK/fail3.csv" --allow-debug > /dev/null 2>&1
check "exits non-zero when it cannot reach the target" '[ $? -ne 0 ]'
check "failed attempts are in fail.csv, not ok.csv" '[ "$(tail -n +2 "$WORK/fail3.csv" | wc -l)" -eq 4 ] && [ "$(wc -l < "$WORK/ok3.csv")" -eq 1 ]'

# 4. Bad usage.
"$BENCH" --host 127.0.0.1 > /dev/null 2>&1
check "bad usage exits 2" '[ $? -eq 2 ]'

[ $fail -eq 0 ] && echo "all bench smoke checks passed"
exit $fail
