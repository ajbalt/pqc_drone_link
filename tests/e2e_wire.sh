#!/usr/bin/env bash
# What actually crosses the wire (T02 at the packet level, "verified encryption") and T09 over a
# real TCP connection. A TLS-aware proxy sits between the real tls_client and tls_server, records
# both byte streams and, in some cases, damages one TLS record.
#
# Checked, for configurations A and B:
#   - the ClientHello and ServerHello show the pinned group, TLS 1.3 and the one cipher suite;
#     everything after them is an encrypted record
#   - none of the client's queued records, and none of the server's ACKs, appear in the
#     captured bytes, while the records ARE in the server's store (so the search can find them)
# Checked for tampering (configuration B):
#   - a flipped bit in a RECORD, in an ACK, in the server's first encrypted handshake record,
#     and in the client's Finished: TLS rejects it, nothing damaged is stored, and the client
#     still gets every record delivered exactly once on a new connection (or, for a damaged
#     handshake the client sees, stops: fail closed)
# Usage: e2e_wire.sh <tls_server> <tls_client> <tls_proxy.py> <cert-dir>
set -u
SERVER=${1:?tls_server}; CLIENT=${2:?tls_client}; PROXY=${3:?tls_proxy.py}; CERTS=${4:?cert dir}
WORK=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

# settle PID...: give each process 10 s to finish by itself, then stop it. A case that goes wrong
# (a server waiting for a connection that never comes) must end as a failed check, not a hang.
settle() {
  for p in "$@"; do
    for _ in $(seq 1 100); do kill -0 "$p" 2>/dev/null || break; sleep 0.1; done
    kill "$p" 2>/dev/null; wait "$p" 2>/dev/null
  done
}

# start_server LOG CONFIG MAX_CONNS STORE: sets SPORT and SPID.
start_server() {
  local log=$1 cfg=$2 conns=$3 store=$4
  : > "$log"
  "$SERVER" --config "$cfg" --port 0 --cert "$CERTS/srv.crt" --key "$CERTS/srv.key" \
            --store "$store" --max-conns "$conns" --timeout-ms 3000 > "$log" 2>/dev/null &
  SPID=$!; SPORT=""
  for _ in $(seq 1 50); do
    SPORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$log")
    [ -n "$SPORT" ] && return
    kill -0 $SPID 2>/dev/null || return
    sleep 0.1
  done
}

# start_proxy PREFIX UPSTREAM_PORT MAX_CONNS [proxy args...]: sets XPORT and XPID.
start_proxy() {
  local prefix=$1 up=$2 conns=$3; shift 3
  python3 -I "$PROXY" --upstream-port "$up" --capture "$prefix" --max-conns "$conns" "$@" \
          > "$prefix.out" 2>&1 &
  XPID=$!; XPORT=""
  for _ in $(seq 1 50); do
    XPORT=$(sed -n 's/^listening port=\([0-9]*\).*/\1/p' "$prefix.out")
    [ -n "$XPORT" ] && return
    sleep 0.1
  done
}

# run_client LOG CONFIG PORT QUEUE [extra args...]: sets CRC. Records come from the queue.
run_client() {
  local log=$1 cfg=$2 port=$3 queue=$4; shift 4
  timeout 30 "$CLIENT" --config "$cfg" --host 127.0.0.1 --port "$port" --ca "$CERTS/ca.crt" \
          --queue "$queue" --timeout-ms 3000 --rate-hz 0 --backoff-ms 50 --backoff-max-ms 200 \
          "$@" > "$log" 2>&1
  CRC=$?
}

# A port nothing listens on, so a client can queue records without delivering them.
start_server "$WORK/probe.log" B 1 "$WORK/probe.dat"; FREEPORT=$SPORT
kill $SPID 2>/dev/null; wait $SPID 2>/dev/null

# fill_queue QUEUE COUNT: COUNT records made and left in the queue file.
fill_queue() { run_client "$WORK/fill.log" B "$FREEPORT" "$1" --count "$2" --deadline-ms 300; }

# queued_messages QUEUE COUNT OUT: the RECORD messages in the queue file (36 bytes each, after
# the 20-byte header, one 40-byte entry per record: src/queue/queue.h), then the ACKs the
# server will send for them (12 bytes each) in OUT.acks.
queued_messages() {
  python3 -I - "$1" "$2" "$3" <<'PY'
import sys
q = open(sys.argv[1], "rb").read()
n = int(sys.argv[2])
msgs = b"".join(q[20 + 40 * i:20 + 40 * i + 36] for i in range(n))
assert len(msgs) == 36 * n
open(sys.argv[3], "wb").write(msgs)
acks = b""
for i in range(n):
    seq = int.from_bytes(msgs[36 * i + 4:36 * i + 12], "big")
    acks += b"\x02\x00\x09\x01" + seq.to_bytes(8, "big")
open(sys.argv[3] + ".acks", "wb").write(acks)
PY
}

# found FILE PATTERNS ITEM_LEN COUNT [START END]: how many of the COUNT items of ITEM_LEN bytes
# in PATTERNS (or only bytes START:END of each) occur anywhere in FILE.
found() {
  python3 -I - "$@" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
pats = open(sys.argv[2], "rb").read()
size, n = int(sys.argv[3]), int(sys.argv[4])
lo, hi = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (0, size)
print(sum(1 for i in range(n) if pats[size * i + lo:size * i + hi] in data))
PY
}

# occurrences FILE PATTERN_FILE: how many times the whole pattern occurs in FILE.
occurrences() {
  python3 -I - "$1" "$2" <<'PY'
import sys
print(open(sys.argv[1], "rb").read().count(open(sys.argv[2], "rb").read()))
PY
}

# sumfield SUMMARY CONN DIR KEY: a value from the proxy's summary lines.
sumfield() { grep "^conn=$2 dir=$3 " "$1" | sed -n "s/.* $4=\([^ ]*\).*/\1/p"; }

# stored STORE: records in a store file (8-byte header, 40 bytes per record).
stored() { echo $(( ($(stat -c %s "$1") - 8) / 40 )); }

# ---------------------------------------------------------------------------------------
# What the wire shows, for A and B.
for cfg in A B; do
  if [ $cfg = A ]; then grp=001d; else grp=11ec; fi
  d="$WORK/clean_$cfg"; mkdir "$d"
  fill_queue "$d/q.dat" 5
  queued_messages "$d/q.dat" 5 "$d/msgs.bin"
  start_server "$d/server.log" $cfg 1 "$d/store.dat"
  start_proxy "$d/cap" "$SPORT" 1
  run_client "$d/client.log" $cfg "$XPORT" "$d/q.dat" --count 0
  settle $SPID $XPID
  S="$d/cap.summary"

  check "$cfg: delivered 5 of 5 through the proxy" \
        '[ $CRC -eq 0 ] && grep -q "acked=5 pending=0 " "$d/client.log"'
  check "$cfg: the records are in the server store (so the search below can find them)" \
        '[ "$(found "$d/store.dat" "$d/msgs.bin" 36 5)" -eq 5 ]'
  for dir in c2s s2c; do
    cap="$d/cap.conn1.$dir.bin"
    check "$cfg $dir: none of the 5 plaintext RECORD messages is on the wire" \
          '[ "$(found "$cap" "$d/msgs.bin" 36 5)" -eq 0 ]'
    check "$cfg $dir: none of the 5 (seq, ts_ms) fields is on the wire" \
          '[ "$(found "$cap" "$d/msgs.bin" 36 5 4 20)" -eq 0 ]'
    check "$cfg $dir: none of the 5 plaintext ACK messages is on the wire" \
          '[ "$(found "$cap" "$d/msgs.bin.acks" 12 5)" -eq 0 ]'
    check "$cfg $dir: records parse cleanly, only types 20, 22, 23, no plaintext alert" \
          '[ "$(sumfield "$S" 1 $dir malformed)" = 0 ] && ! sumfield "$S" 1 $dir records | grep -q "21:"'
  done
  check "$cfg: the server certificate is not readable on the wire" \
        '[ "$(found "$d/cap.conn1.s2c.bin" <(printf relay.test) 10 1)" -eq 0 ]'
  check "$cfg: exactly one clear handshake record each way (ClientHello, ServerHello)" \
        'sumfield "$S" 1 c2s records | grep -q "22:1" && sumfield "$S" 1 s2c records | grep -q "22:1"'
  check "$cfg: ClientHello offers only the pinned group $grp, with a key share for it" \
        '[ "$(sumfield "$S" 1 c2s hello_groups)" = "$grp" ] && [ "$(sumfield "$S" 1 c2s hello_keyshare)" = "$grp" ]'
  check "$cfg: ClientHello offers TLS 1.3 only and the one cipher suite 1301" \
        '[ "$(sumfield "$S" 1 c2s hello_versions)" = 0304 ] && [ "$(sumfield "$S" 1 c2s hello_ciphers)" = 1301 ]'
  check "$cfg: ServerHello chooses $grp, TLS 1.3, suite 1301, no retry" \
        '[ "$(sumfield "$S" 1 s2c server_keyshare)" = "$grp" ] && [ "$(sumfield "$S" 1 s2c server_version)" = 0304 ] && [ "$(sumfield "$S" 1 s2c server_cipher)" = 1301 ] && [ "$(sumfield "$S" 1 s2c hrr)" = 0 ]'
done

# ---------------------------------------------------------------------------------------
# Tampering, configuration B. First learn from a clean one-record run how many encrypted
# (type 23) records each side sends: the client sends Finished, the RECORD, close_notify; the
# server sends its handshake records, the ACK, close_notify. So the RECORD is the second to
# last type-23 record from the client, and the ACK the second to last from the server.
d="$WORK/probe_run"; mkdir "$d"
fill_queue "$d/q.dat" 1
start_server "$d/server.log" B 1 "$d/store.dat"
start_proxy "$d/cap" "$SPORT" 1
run_client "$d/client.log" B "$XPORT" "$d/q.dat" --count 0
settle $SPID $XPID
n23() { sumfield "$d/cap.summary" 1 "$1" records | tr ',' '\n' | sed -n 's/^23://p'; }
C2S23=$(n23 c2s); S2C23=$(n23 s2c)
check "learned the record counts of a clean run (client $C2S23, server $S2C23)" \
      '[ "${C2S23:-0}" -ge 3 ] && [ "${S2C23:-0}" -ge 3 ]'
REC_N=$((C2S23 - 1)); ACK_N=$((S2C23 - 1))

# tamper_case NAME TAMPER_SPEC CONNS: queue 1 record, send it through a proxy that damages one
# record of the first connection; sets d (the case directory) and leaves the logs there.
tamper_case() {
  d="$WORK/$1"; mkdir "$d"
  fill_queue "$d/q.dat" 1
  queued_messages "$d/q.dat" 1 "$d/msgs.bin"
  start_server "$d/server.log" B "$3" "$d/store.dat"
  start_proxy "$d/cap" "$SPORT" "$3" --tamper "$2"
  run_client "$d/client.log" B "$XPORT" "$d/q.dat" --count 0 ${4:-}
  settle $SPID $XPID
}

# 1. A flipped bit in the RECORD the client sends.
tamper_case rec "c2s:$REC_N" 2
check "T09 RECORD: the proxy damaged the record on connection 1 only" \
      '[ "$(sumfield "$d/cap.summary" 1 c2s tampered)" = 1 ] && [ "$(sumfield "$d/cap.summary" 2 c2s tampered)" = 0 ]'
check "T09 RECORD: the server rejected it (protocol error), stored nothing, ACKed nothing" \
      'grep -q "conn_done records=0 protocol_error=1 new=0 duplicates=0 count=0" "$d/server.log"'
check "T09 RECORD: the client lost the link, reconnected and delivered the record" \
      '[ $CRC -eq 0 ] && grep -q "acked=1 pending=0 " "$d/client.log" && ! grep -q "lost_links=0 " "$d/client.log"'
check "T09 RECORD: the store holds the original record exactly once, never the damaged one" \
      '[ "$(stored "$d/store.dat")" -eq 1 ] && [ "$(occurrences "$d/store.dat" "$d/msgs.bin")" -eq 1 ]'
check "T09 RECORD: the second connection stored it as new" \
      'grep -q "conn_done records=1 protocol_error=0 new=1 duplicates=0 count=1" "$d/server.log"'

# 2. A flipped bit in the ACK the server sends.
tamper_case ack "s2c:$ACK_N" 2
check "T09 ACK: the proxy damaged the ACK on connection 1 only" \
      '[ "$(sumfield "$d/cap.summary" 1 s2c tampered)" = 1 ] && [ "$(sumfield "$d/cap.summary" 2 s2c tampered)" = 0 ]'
check "T09 ACK: the client refused it, lost the link and sent the record again" \
      '[ $CRC -eq 0 ] && grep -q "acked=1 pending=0 " "$d/client.log" && ! grep -q "lost_links=0 " "$d/client.log"'
# The first connection ends with protocol_error=1 or 0: after a damaged ACK the client's TLS
# object is in an error state and sends no close_notify, so the server sees an abrupt close.
check "T05+T09: the server stored the record once and recognised the resend as a duplicate" \
      'grep -q "conn_done records=1 protocol_error=[01] new=1 duplicates=0 count=1" "$d/server.log" && grep -q "conn_done records=1 protocol_error=0 new=0 duplicates=1 count=1" "$d/server.log" && [ "$(stored "$d/store.dat")" -eq 1 ]'

# 3. A flipped bit in the first encrypted handshake record the server sends: the client's
#    handshake must fail and the client must stop (fail closed, rules 12 and 13).
tamper_case hs_server "s2c:1" 1
check "T09 handshake (server to client): the client's handshake failed and it did not retry" \
      '[ $CRC -ne 0 ] && grep -q "handshake_failed" "$d/client.log" && ! grep -q "connected group=" "$d/client.log"'
check "T09 handshake (server to client): nothing was stored" \
      '[ "$(stored "$d/store.dat")" -eq 0 ] && ! grep -q "connected group=" "$d/server.log"'

# 4. A flipped bit in the client's Finished: the server must refuse the connection. The client
#    cannot tell at once, so it loses the link and delivers on the next connection.
tamper_case hs_client "c2s:1" 2
check "T09 handshake (client Finished): the server refused the first connection" \
      '[ "$(grep -c "handshake_failed" "$d/server.log")" -eq 1 ] && [ "$(sumfield "$d/cap.summary" 1 c2s tampered)" = 1 ]'
check "T09 handshake (client Finished): the record was delivered on the second connection" \
      '[ $CRC -eq 0 ] && grep -q "acked=1 pending=0 " "$d/client.log" && [ "$(stored "$d/store.dat")" -eq 1 ]'

[ $fail -eq 0 ] && echo "all wire checks passed"
exit $fail
