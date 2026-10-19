#!/usr/bin/env bash
# End-to-end test of tls_server and tls_client as real processes over TCP on
# loopback: configurations A and B, the cases that must fail closed, the
# durable store (T05 replay, server restart, torn write, corrupt and full
# store) and the persistent client queue (outage T06, client restart T07,
# queue full L9, lost link, window).
# Usage: e2e_tls_link.sh <tls_server> <tls_client> <cert-dir> <other-cert-dir>
set -u
SERVER=${1:?tls_server}; CLIENT=${2:?tls_client}; CERTS=${3:?cert dir}; OTHER=${4:?other cert dir}
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0

# start_server LOG CONFIG MAX_CONNS STORE [extra args...]: sets PORT and SPID.
# PORT is empty if the server did not print "listening". A later --port in
# the extra args wins over the --port 0 here.
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

# start_client LOG CONFIG CA_DIR HOSTNAME QUEUE [extra args...]: sets CPID.
# Records are made all at once unless the extra args say otherwise (the last
# value of an option wins), and the retry waits are short so tests are quick.
start_client() {
  local log=$1 cfg=$2 ca=$3 host=$4 queue=$5; shift 5
  timeout 20 "$CLIENT" --config "$cfg" --host 127.0.0.1 --port "$PORT" --ca "$ca/ca.crt" \
          --hostname "$host" --queue "$queue" --timeout-ms 3000 --rate-hz 0 \
          --backoff-ms 50 --backoff-max-ms 200 "$@" > "$log" 2>&1 &
  CPID=$!
}

# run_client LOG CONFIG CA_DIR HOSTNAME QUEUE [extra args...]: sets CRC to the
# client's exit code.
run_client() {
  start_client "$@"
  wait $CPID; CRC=$?
}

# fill_queue LOG QUEUE COUNT: make COUNT records with no server listening, so
# they stay in the queue file. The client gives up after its deadline (exit 1).
fill_queue() {
  PORT=$FREEPORT run_client "$1" B "$CERTS" relay.test "$2" --count "$3" --deadline-ms 300
}

# result NAME OK [logs...]: print PASS or FAIL, and the logs on failure.
result() {
  local name=$1 ok=$2; shift 2
  if [ "$ok" -eq 1 ]; then echo "PASS $name"; else echo "FAIL $name"; cat "$@"; fail=1; fi
}

# A port with no server on it, for the outage cases.
start_server "$WORK/probe.log" B 1 "$WORK/probe.dat"
FREEPORT=$PORT
kill $SPID 2>/dev/null; wait $SPID 2>/dev/null

# run_case NAME SERVER_CONFIG CLIENT_CONFIG CLIENT_CA_DIR HOSTNAME EXPECT(ok|fail) [GROUP]
run_case() {
  local name=$1 sc=$2 cc=$3 cadir=$4 host=$5 expect=$6 group=${7:-}
  local slog="$WORK/server.log" clog="$WORK/client.log" store="$WORK/case.dat" q="$WORK/case.q"
  rm -f "$store" "$q"
  start_server "$slog" "$sc" 1 "$store"
  if [ -z "$PORT" ]; then result "$name: server did not start" 0 "$slog"; kill $SPID 2>/dev/null; return; fi
  run_client "$clog" "$cc" "$cadir" "$host" "$q" --count 5
  wait $SPID 2>/dev/null

  local ok=1
  if [ "$expect" = ok ]; then
    [ $CRC -eq 0 ] || ok=0
    grep -q "done generated=5 accepted=5 rejected=0 acked=5 pending=0 " "$clog" || ok=0
    grep -q "conn_done records=5 protocol_error=0 new=5 duplicates=0 count=5" "$slog" || ok=0
    grep -qi "connected group=$group " "$clog" || ok=0
    grep -qi "connected group=$group " "$slog" || ok=0
  else
    [ $CRC -ne 0 ] || ok=0
    grep -q "acked=5" "$clog" && ok=0
    grep -q "handshake_failed" "$clog" || ok=0
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

# T05: five records that Pi B already stored are sent again (as after a crash
# between Pi B's commit and Pi A's ACK entry): the queue file is copied before
# the first drain and put back for the second. Both are ACKed, stored once.
fill_queue "$C1" "$WORK/q5.dat" 5
cp "$WORK/q5.dat" "$WORK/q5.copy"
start_server "$SLOG" B 2 "$STORE"
run_client "$C1" B "$CERTS" relay.test "$WORK/q5.dat" --count 0; rc1=$CRC
cp "$WORK/q5.copy" "$WORK/q5.dat"
run_client "$C2" B "$CERTS" relay.test "$WORK/q5.dat" --count 0; rc2=$CRC
wait $SPID 2>/dev/null
ok=1
[ $rc1 -eq 0 ] && [ $rc2 -eq 0 ] || ok=0
grep -q "queue path=$WORK/q5.dat pending=5 next_seq=6" "$C2" || ok=0
grep -q "done generated=0 accepted=0 rejected=0 acked=5 pending=0 " "$C2" || ok=0
grep -q "store path=$STORE count=0 truncated_bytes=0" "$SLOG" || ok=0
grep -q "conn_done records=5 protocol_error=0 new=5 duplicates=0 count=5" "$SLOG" || ok=0
grep -q "conn_done records=5 protocol_error=0 new=0 duplicates=5 count=5" "$SLOG" || ok=0
result "T05: replayed seqs ACKed again, counted once" $ok "$C1" "$C2" "$SLOG"

# Restart (L8): a new server process reads the count back from disk. The
# queue still holds seqs 1-5 (restored copy) and makes 6 and 7.
cp "$WORK/q5.copy" "$WORK/q5.dat"
start_server "$SLOG" B 1 "$STORE"
run_client "$C1" B "$CERTS" relay.test "$WORK/q5.dat" --count 2
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "store path=$STORE count=5 truncated_bytes=0" "$SLOG" || ok=0
grep -q "conn_done records=7 protocol_error=0 new=2 duplicates=5 count=7" "$SLOG" || ok=0
result "restart: count survives, old seqs still duplicates" $ok "$C1" "$SLOG"

# Torn write: half an entry at the end, as a power cut mid-write leaves. It
# is cut off at start and logged; nothing that was ACKed is lost.
printf '\001\000\041torn-entry' >> "$STORE"
fill_queue "$C1" "$WORK/q7.dat" 7
start_server "$SLOG" B 1 "$STORE"
run_client "$C1" B "$CERTS" relay.test "$WORK/q7.dat" --count 0
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
# The client keeps seqs 4 and 5 queued and gives up at its deadline.
rm -f "$WORK/full.dat" "$WORK/full.q"
start_server "$SLOG" B 1 "$WORK/full.dat" --store-max 3
run_client "$C1" B "$CERTS" relay.test "$WORK/full.q" --count 5 --deadline-ms 1500
wait $SPID 2>/dev/null
ok=1
[ $CRC -ne 0 ] || ok=0
grep -q "acked=3 pending=2 " "$C1" || ok=0
grep -q "store_full node=1 seq=4" "$SLOG" || ok=0
grep -q "conn_done records=3 protocol_error=0 new=3 duplicates=0 count=3" "$SLOG" || ok=0
result "full store: seq 4 rejected, not ACKed, still queued" $ok "$C1" "$SLOG"

# --- Persistent client queue -------------------------------------------------

# Window of 1 (stop and wait) works too.
rm -f "$WORK/w1.dat" "$WORK/w1.q"
start_server "$SLOG" B 1 "$WORK/w1.dat"
run_client "$C1" B "$CERTS" relay.test "$WORK/w1.q" --count 20 --window 1
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "done generated=20 accepted=20 rejected=0 acked=20 pending=0 " "$C1" || ok=0
grep -q "conn_done records=20 protocol_error=0 new=20 duplicates=0 count=20" "$SLOG" || ok=0
result "window 1: 20 records ACKed one by one" $ok "$C1" "$SLOG"

# T06: the server is down while the client makes 50 records (1 s at 50 Hz).
# They wait in the queue; when the server comes up the client reconnects and
# drains them all.
rm -f "$WORK/t06.dat" "$WORK/t06.q"
PORT=$FREEPORT start_client "$C1" B "$CERTS" relay.test "$WORK/t06.q" \
      --count 50 --rate-hz 50 --queue-max 1000
sleep 1.3
start_server "$SLOG" B 1 "$WORK/t06.dat" --port "$FREEPORT"
wait $CPID; CRC=$?
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "connect_failed" "$C1" || ok=0
grep -q "done generated=50 accepted=50 rejected=0 acked=50 pending=0 peak=50 " "$C1" || ok=0
grep -q "conn_done records=50 protocol_error=0 new=50 duplicates=0 count=50" "$SLOG" || ok=0
result "T06: 50 records queued during an outage, all delivered" $ok "$C1" "$SLOG"

# T07: Pi A stops (here: its deadline) with 30 records queued and restarts.
# Nothing is lost, nothing is counted twice, and seq numbering carries on.
rm -f "$WORK/t07.dat" "$WORK/t07.q"
fill_queue "$C1" "$WORK/t07.q" 30
start_server "$SLOG" B 1 "$WORK/t07.dat"
run_client "$C2" B "$CERTS" relay.test "$WORK/t07.q" --count 0
wait $SPID 2>/dev/null
ok=1
grep -q "done generated=30 accepted=30 rejected=0 acked=0 pending=30 " "$C1" || ok=0
[ $CRC -eq 0 ] || ok=0
grep -q "queue path=$WORK/t07.q pending=30 next_seq=31" "$C2" || ok=0
grep -q "done generated=0 accepted=0 rejected=0 acked=30 pending=0 " "$C2" || ok=0
grep -q "conn_done records=30 protocol_error=0 new=30 duplicates=0 count=30" "$SLOG" || ok=0
start_server "$SLOG" B 1 "$WORK/t07.dat"
run_client "$C2" B "$CERTS" relay.test "$WORK/t07.q" --count 5
wait $SPID 2>/dev/null
[ $CRC -eq 0 ] || ok=0
grep -q "queue path=$WORK/t07.q pending=0 next_seq=31" "$C2" || ok=0
grep -q "conn_done records=5 protocol_error=0 new=5 duplicates=0 count=35" "$SLOG" || ok=0
result "T07: restart drains the queue, seq carries on (31..35)" $ok "$C1" "$C2" "$SLOG"

# T07, harder: the client is killed with SIGKILL while it is making records.
# Whatever the queue file holds is delivered exactly once after the restart.
rm -f "$WORK/kill.dat" "$WORK/kill.q"
PORT=$FREEPORT start_client "$C1" B "$CERTS" relay.test "$WORK/kill.q" \
      --count 100000 --rate-hz 200
sleep 0.6
pkill -9 -P $CPID; kill -9 $CPID 2>/dev/null; wait $CPID 2>/dev/null   # timeout, then the client
start_server "$SLOG" B 1 "$WORK/kill.dat"
run_client "$C2" B "$CERTS" relay.test "$WORK/kill.q" --count 0
wait $SPID 2>/dev/null
held=$(sed -n 's/^queue path=[^ ]* pending=\([0-9]*\) .*/\1/p' "$C2")
ok=1
[ $CRC -eq 0 ] || ok=0
[ "${held:-0}" -ge 10 ] || ok=0
grep -q "done generated=0 accepted=0 rejected=0 acked=$held pending=0 " "$C2" || ok=0
grep -q "conn_done records=$held protocol_error=0 new=$held duplicates=0 count=$held" "$SLOG" || ok=0
result "T07: SIGKILL with $held records queued, all delivered once" $ok "$C2" "$SLOG"

# L9: the queue holds 20 un-ACKed records; the 80 after that are rejected and
# counted, and their seqs are used up. The 20 queued ones are delivered later.
rm -f "$WORK/l9.dat" "$WORK/l9.q"
PORT=$FREEPORT run_client "$C1" B "$CERTS" relay.test "$WORK/l9.q" \
      --count 100 --queue-max 20 --deadline-ms 400
start_server "$SLOG" B 1 "$WORK/l9.dat"
run_client "$C2" B "$CERTS" relay.test "$WORK/l9.q" --count 0 --queue-max 20
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "done generated=100 accepted=20 rejected=80 acked=0 pending=20 peak=20 next_seq=101 " "$C1" || ok=0
grep -q "done generated=0 accepted=0 rejected=0 acked=20 pending=0 " "$C2" || ok=0
grep -q "conn_done records=20 protocol_error=0 new=20 duplicates=0 count=20" "$SLOG" || ok=0
result "L9: queue full rejects and counts 80, delivers the 20 queued" $ok "$C1" "$C2" "$SLOG"

# A lost link: the server is killed in the middle of a 1 s run and a new one
# starts on the same port and store. The client reconnects, resends what was
# not ACKed (the new server deduplicates it) and every record ends up stored.
rm -f "$WORK/link.dat" "$WORK/link.q"
start_server "$SLOG" B 5 "$WORK/link.dat" --port "$FREEPORT"
S1=$SPID
start_client "$C1" B "$CERTS" relay.test "$WORK/link.q" --count 40 --rate-hz 40
sleep 0.4
kill -9 $S1; wait $S1 2>/dev/null
sleep 0.2
start_server "$WORK/link2.log" B 1 "$WORK/link.dat" --port "$FREEPORT"
wait $CPID; CRC=$?
wait $SPID 2>/dev/null
ok=1
[ $CRC -eq 0 ] || ok=0
grep -q "done generated=40 accepted=40 rejected=0 acked=40 pending=0 " "$C1" || ok=0
grep -q "lost_links=0 " "$C1" && ok=0
grep -q "conn_done .* count=40" "$WORK/link2.log" || ok=0
result "lost link: server killed and restarted, 40 of 40 delivered" $ok "$C1" "$WORK/link2.log"

[ $fail -eq 0 ] && echo "all e2e cases passed"
exit $fail
