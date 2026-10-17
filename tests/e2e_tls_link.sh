#!/usr/bin/env bash
# End-to-end test of tls_server and tls_client as real processes over TCP on
# loopback: configurations A and B, and the cases that must fail closed.
# Usage: e2e_tls_link.sh <tls_server> <tls_client> <cert-dir> <other-cert-dir>
set -u
SERVER=${1:?tls_server}; CLIENT=${2:?tls_client}; CERTS=${3:?cert dir}; OTHER=${4:?other cert dir}
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0

# run_case NAME SERVER_CONFIG CLIENT_CONFIG CLIENT_CA_DIR HOSTNAME EXPECT(ok|fail) [GROUP]
run_case() {
  local name=$1 sc=$2 cc=$3 cadir=$4 host=$5 expect=$6 group=${7:-}
  local slog="$WORK/server.log" clog="$WORK/client.log"
  : > "$slog"
  "$SERVER" --config "$sc" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
            --max-conns 1 --timeout-ms 3000 > "$slog" 2>/dev/null &
  local spid=$!
  local port=""
  for _ in $(seq 1 50); do
    port=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$slog")
    [ -n "$port" ] && break
    sleep 0.1
  done
  if [ -z "$port" ]; then echo "FAIL $name: server did not start"; fail=1; kill $spid 2>/dev/null; return; fi

  timeout 15 "$CLIENT" --config "$cc" --host 127.0.0.1 --port "$port" --ca "$cadir/ca.crt" \
          --hostname "$host" --count 5 --timeout-ms 3000 > "$clog" 2>&1
  local crc=$?
  wait $spid 2>/dev/null

  local ok=1
  if [ "$expect" = ok ]; then
    [ $crc -eq 0 ] || ok=0
    grep -q "done sent=5 acked=5" "$clog" || ok=0
    grep -q "conn_done records=5 protocol_error=0" "$slog" || ok=0
    grep -qi "connected group=$group " "$clog" || ok=0
    grep -qi "connected group=$group " "$slog" || ok=0
  else
    [ $crc -ne 0 ] || ok=0
    grep -q "acked=5" "$clog" && ok=0
    grep -q "connected group=" "$slog" && ok=0
  fi
  if [ $ok -eq 1 ]; then echo "PASS $name"; else echo "FAIL $name (client rc=$crc)"; cat "$clog" "$slog"; fail=1; fi
}

run_case "A: X25519 both sides"          A A "$CERTS" relay.test ok   x25519
run_case "B: hybrid both sides"          B B "$CERTS" relay.test ok   X25519MLKEM768
run_case "T04: client hybrid, server A"  A B "$CERTS" relay.test fail
run_case "T04: client A, server hybrid"  B A "$CERTS" relay.test fail
run_case "T03: wrong hostname"           B B "$CERTS" wrong.test fail
run_case "T03: untrusted CA"             B B "$OTHER" relay.test fail

[ $fail -eq 0 ] && echo "all e2e cases passed"
exit $fail
