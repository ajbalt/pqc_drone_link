#!/usr/bin/env bash
# Run the group/identity matrix on loopback: configs A and B, the T04 downgrade cases in both
# directions, default groups, and the T03 wrong hostname. Prints one line per case.
# Usage: tls_group_matrix.sh <openssl-prefix> <cert-dir>   (cert-dir from make_demo_certs.sh)
# This is a bring-up check on one machine, not gate evidence (see docs/results.md).
set -u
P=${1:?openssl prefix}; cd "${2:?cert dir}" || exit 2
export LD_LIBRARY_PATH=$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}
O=$P/bin/openssl; port=$((19000 + RANDOM % 2000))
run() { # name server_groups client_groups [extra s_client args]
  local name=$1 sg=$2 cg=$3; shift 3; port=$((port + 1))
  $O s_server -accept $port -tls1_3 ${sg:+-groups $sg} -cert srv.crt -key srv.key -www >/dev/null 2>&1 &
  local sp=$!; sleep 0.5
  local out rc
  out=$(timeout 5 $O s_client -connect 127.0.0.1:$port -tls1_3 ${cg:+-groups $cg} -CAfile ca.crt \
        -verify_hostname relay.test -verify_return_error -brief "$@" </dev/null 2>&1); rc=$?
  kill $sp 2>/dev/null; wait $sp 2>/dev/null
  local key; key=$(printf '%s\n' "$out" | grep -E "Peer Temp Key|Negotiated TLS1.3 group" | head -1 | sed 's/.*: //')
  local err; err=$(printf '%s\n' "$out" | grep -iE "alert number|hostname mismatch" | head -1 | sed 's/^[^ ]* *//')
  printf '%-36s rc=%-2s key=%-22s %s\n' "$name" "$rc" "${key:--}" "$err"
}
run "A  client X25519 / server X25519"      X25519 X25519
run "B  client hybrid / server hybrid"      X25519MLKEM768 X25519MLKEM768
run "T04 client hybrid / server X25519"     X25519 X25519MLKEM768
run "T04 client X25519 / server hybrid"     X25519MLKEM768 X25519
run "default client / server hybrid-only"   X25519MLKEM768 ""
run "default client / server X25519-only"   X25519 ""
run "T03 wrong hostname"                    X25519MLKEM768 X25519MLKEM768 -verify_hostname wrong.test
