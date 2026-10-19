#!/usr/bin/env bash
# End-to-end test of tls_server and tls_client as real processes over TCP on
# loopback: configurations A and B, the cases that must fail closed, and the
# durable store (T05 replay, server restart, torn write, corrupt and full store).
# Usage: e2e_tls_link.sh <tls_server> <tls_client> <cert-dir> <other-cert-dir>
set -u
SERVER=${1:?tls_server}; CLIENT=${2:?tls_client}; CERTS=${3:?cert dir}; OTHER=${4:?other cert dir}
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0

# start_server LOG CONFIG MAX_CONNS STORE [extra args...]: sets PORT and SPID.
# PORT is empty if the server did not print "listening".
start_server() {
  local log=$1 cfg=$2 conns=$3 store=$4; shift 4
  : > "$log"
  "$SERVER" --config "$cfg" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
            --store "$store" --max-conns "$conns" --timeout-ms 3000 "$@" > "$log" 2>/dev/null &
  SPID=$!
  PORT=""
  for _ in $(seq 1 50); do
    PORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$log")
    [ -n "$PORT" ] && return
    kill -0 $SPID 2>/dev/null || return   # the server exited before listening
    sleep 0.1
  done
}

# run_client LOG CONFIG CA_DIR HOSTNAME COUNT: sets CRC to the client's exit code.
run_client() {
  timeout 15 "$CLIENT" --config "$2" --host 127.0.0.1 --port "$PORT" --ca "$3/ca.crt" \
          --hostname "$4" --count "$5" --timeout-ms 3000 > "$1" 2>&1
  CRC=$?
}

# result NAME OK [logs...]: print PASS or FAIL, and the logs on failure.
result() {
  local name=$1 ok=$2; shift 2
  if [ "$ok" -eq 1 ]; then echo "PASS $name"; else echo "FAIL $name"; cat "$@"; fail=1; fi
}

# run_case NAME SERVER_CONFIG CLIENT_CONFIG CLIENT_CA_DIR HOSTNAME EXPECT(ok|fail) [GROUP]
run_case() {
  local name=$1 sc=$2 cc=$3 cadir=$4 host=$5 expect=$6 group=${7:-}
  local slog="$WORK/server.log" clog="$WORK/client.log" store="$WORK/case.dat"
  rm -f "$store"
  start_server "$slog" "$sc" 1 "$store"
  if [ -z "$PORT" ]; then result "$name: server did not start" 0 "$slog"; kill $SPID 2>/dev/null; return; fi
  run_client "$clog" "$cc" "$cadir" "$host" 5
  wait $SPID 2>/dev/null

  local ok=1
  if [ "$expect" = ok ]; then
    [ $CRC -eq 0 ] || ok=0
    grep -q "done sent=5 acked=5" "$clog" || ok=0
    grep -q "conn_done records=5 protocol_error=0 new=5 duplicates=0 count=5" "$slog" || ok=0
    grep -qi "connected group=$group " "$clog" || ok=0
    grep -qi "connected group=$group " "$slog" || ok=0
  else
    [ $CRC -ne 0 ] || ok=0
    grep -q "acked=5" "$clog" && ok=0
    grep -q "connected group=" "$slog" && ok=0
  fi
  result "$name (client rc=$CRC)" $ok "$clog" "$slog"
}

run_case "A: X25519 both sides"          A A "$CERTS" relay.test ok   x25519
run_case "B: hybrid both sides"          B B "$CERTS" relay.test ok   X25519MLKEM768
run_case "T04: client hybrid, server A"  A B "$CERTS" relay.test fail
run_case "T04: client A, server hybrid"  B A "$CERTS" relay.test fail
run_case "T03: wrong hostname"           B B "$CERTS" wrong.test fail
run_case "T03: untrusted CA"             B B "$OTHER" relay.test fail

# --- Durable store, configuration B -----------------------------------------
STORE="$WORK/records.dat"
SLOG="$WORK/store_server.log"
C1="$WORK/c1.log"; C2="$WORK/c2.log"

# T05: the same 5 records sent twice (as after lost ACKs or a reconnect) are
# ACKed both times but stored and counted once.
start_server "$SLOG" B 2 "$STORE"
run_client "$C1" B "$CERTS" relay.test 5; rc1=$CRC
run_client "$C2" B "$CERTS" relay.test 5; rc2=$CRC
wait $SPID 2>/dev/null
ok=1
[ $rc1 -eq 0 ] && [ $rc2 -eq 0 ] || ok=0
grep -q "done sent=5 acked=5" "$C2" || ok=0
grep -q "store path=$STORE count=0 truncated_bytes=0" "$SLOG" || ok=0
grep -q "conn_done records=5 protocol_error=0 new=5 duplicates=0 count=5" "$SLOG" || ok=0
grep -q "conn_done records=5 protocol_error=0 new=0 duplicates=5 count=5" "$SLOG" || ok=0
result "T05: replayed seqs ACKed again, counted once" $ok "$C1" "$C2" "$SLOG"

# Restart (L8): a new server process reads the count back from disk, and the
# old records are still recognised.
start_server "$SLOG" B 1 "$STORE"
run_client "$C1" B "$CERTS" relay.test 7
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "store path=$STORE count=5 truncated_bytes=0" "$SLOG" || ok=0
grep -q "conn_done records=7 protocol_error=0 new=2 duplicates=5 count=7" "$SLOG" || ok=0
result "restart: count survives, old seqs still duplicates" $ok "$C1" "$SLOG"

# Torn write: half an entry at the end, as a power cut mid-write leaves. It
# is cut off at start and logged; nothing that was ACKed is lost.
printf '\001\000\041torn-entry' >> "$STORE"
start_server "$SLOG" B 1 "$STORE"
run_client "$C1" B "$CERTS" relay.test 7
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "store path=$STORE count=7 truncated_bytes=13" "$SLOG" || ok=0
grep -q "conn_done records=7 protocol_error=0 new=0 duplicates=7 count=7" "$SLOG" || ok=0
result "torn last entry cut off, count unchanged" $ok "$C1" "$SLOG"

# A file that is not a store: the server refuses to start and leaves it alone.
printf 'not a store file\n' > "$WORK/foreign.dat"
cp "$WORK/foreign.dat" "$WORK/foreign.orig"
start_server "$SLOG" B 1 "$WORK/foreign.dat"
wait $SPID 2>/dev/null; src=$?
ok=1
[ -z "$PORT" ] || ok=0
[ $src -ne 0 ] || ok=0
cmp -s "$WORK/foreign.dat" "$WORK/foreign.orig" || ok=0
result "foreign store file: no start, file untouched (rc=$src)" $ok "$SLOG"

# Full store: records past --store-max are not ACKed, and the client is told.
rm -f "$WORK/full.dat"
start_server "$SLOG" B 1 "$WORK/full.dat" --store-max 3
run_client "$C1" B "$CERTS" relay.test 5
wait $SPID 2>/dev/null
ok=1
[ $CRC -ne 0 ] || ok=0
grep -q "done sent=5 acked=3" "$C1" || ok=0
grep -q "store_full node=1 seq=4" "$SLOG" || ok=0
grep -q "conn_done records=3 protocol_error=0 new=3 duplicates=0 count=3" "$SLOG" || ok=0
result "full store: seq 4 rejected, not ACKed" $ok "$C1" "$SLOG"

[ $fail -eq 0 ] && echo "all e2e cases passed"
exit $fail
