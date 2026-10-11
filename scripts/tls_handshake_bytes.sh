#!/usr/bin/env bash
# Print TLS payload bytes per full handshake for configs A and B (tickets off), and the
# ClientHello/ServerHello lengths. Loopback bring-up helper, not gate evidence.
# Usage: tls_handshake_bytes.sh <openssl-prefix> <cert-dir> [repeats]
set -u
P=${1:?openssl prefix}; C=${2:?cert dir}; N=${3:-3}
here=$(cd "$(dirname "$0")" && pwd); cd "$C" || exit 2
export LD_LIBRARY_PATH=$P/lib64:$P/lib:${LD_LIBRARY_PATH:-}
O=$P/bin/openssl
for G in X25519 X25519MLKEM768; do
  echo "== $G"
  for i in $(seq "$N"); do
    sp=$((20000 + RANDOM % 2000)); pp=$((sp + 3000))
    $O s_server -accept $sp -tls1_3 -groups $G -cert srv.crt -key srv.key -www -no_ticket >/dev/null 2>&1 &
    s=$!; sleep 0.4
    python3 -I "$here/proxy_count.py" $pp $sp > cnt.txt & p=$!; sleep 0.3
    $O s_client -connect 127.0.0.1:$pp -tls1_3 -groups $G -CAfile ca.crt -verify_hostname relay.test \
      -msg </dev/null > msg.txt 2>&1
    wait $p; kill $s 2>/dev/null; wait $s 2>/dev/null
    echo "run $i: client->server / server->client payload bytes: $(cat cnt.txt)"
  done
  grep -E "^(>>>|<<<) TLS 1.3, Handshake \[length [0-9a-f]+\], (ClientHello|ServerHello)" msg.txt | sed 's/TLS 1.3, //'
done
rm -f cnt.txt msg.txt
