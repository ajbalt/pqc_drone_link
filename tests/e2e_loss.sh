#!/usr/bin/env bash
# Smoke test of the T08 runner: a tiny matrix (A and B, 0% and 10% loss, one run each) through
# the real tls_server and tls_client inside private network namespaces. Checks the CSV, the
# netem record and the summary, not the speed. Skips (exit 77) where netem cannot be used.
# Usage: e2e_loss.sh <run_loss_test.sh> <openssl-prefix> <build-dir>
set -u
RUNNER=${1:?run_loss_test.sh}; PREFIX=${2:?openssl prefix}; BUILD=${3:?build dir}
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

cat > "$WORK/smoke.conf" <<'CONF'
name=t08_smoke
configs=A,B
cipher_suite=TLS_AES_128_GCM_SHA256
hostname=relay.test
loss_percent_list=0,10
delay_ms=0
mtu=1500
loss_method=tc netem on lo inside an unprivileged user and network namespace (unshare -Urn)
repetitions=1
min_runs=1
records=20
rate_hz=100
queue_capacity=1000
window=32
timeout_ms=2000
server_timeout_ms=5000
backoff_ms=50
backoff_max_ms=200
run_deadline_s=60
CONF

timeout 300 bash "$RUNNER" "$PREFIX" "$BUILD" "$WORK/smoke.conf" "$WORK/out" --allow-debug \
        > "$WORK/run.log" 2>&1
rc=$?
if [ $rc -eq 77 ]; then echo "SKIP: netem cannot be used here"; cat "$WORK/run.log"; exit 77; fi
CSV=$(ls "$WORK"/out/*.runs.csv 2>/dev/null | head -1)
check "runner exits 0 (every run delivered everything)" '[ $rc -eq 0 ]'
check "runs.csv exists with 4 rows (2 configs x 2 loss levels)" \
      '[ -n "$CSV" ] && [ "$(tail -n +2 "$CSV" | wc -l)" -eq 4 ]'
check "runs alternate A,B within each loss level" \
      '[ "$(tail -n +2 "$CSV" | cut -d, -f2 | tr -d "\n")" = ABAB ]'
check "every run: 20 accepted, 20 ACKed, none waiting, 20 stored" \
      '[ "$(tail -n +2 "$CSV" | awk -F, "\$12==20 && \$14==20 && \$15==0 && \$21==20" | wc -l)" -eq 4 ]'
check "every run exited 0" '[ "$(tail -n +2 "$CSV" | awk -F, "\$23==0" | wc -l)" -eq 4 ]'
check "netem column records the loss level" \
      'tail -n +2 "$CSV" | cut -d, -f5 | sort -u | tr "\n" " " | grep -q "loss=0%;delay_ms=0;mtu=1500 loss=10%;delay_ms=0;mtu=1500"'
check "no TCP retransmissions at 0% loss" \
      '[ "$(tail -n +2 "$CSV" | awk -F, "\$3==0 && \$22!=0" | wc -l)" -eq 0 ]'
check "TCP retransmissions happen at 10% loss (the loss acted)" \
      '[ "$(tail -n +2 "$CSV" | awk -F, "\$3==10 {s+=\$22} END {print s+0}")" -gt 0 ]'
check "meta lists the netem qdisc of each 10% run" \
      '[ "$(grep -c "^qdisc netem.* loss 10%" "$WORK"/out/*.meta.txt)" -eq 2 ]'
check "summary prints both configurations" \
      'grep -q "== config A, loss 10%" "$WORK"/out/*.summary.txt && grep -q "== config B, loss 10%" "$WORK"/out/*.summary.txt'
check "summary csv written" '[ -s "$WORK"/out/*.summary.csv ]'

[ $fail -eq 0 ] && echo "all loss-runner checks passed"
exit $fail
