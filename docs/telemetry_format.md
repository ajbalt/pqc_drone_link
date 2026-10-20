# Architecture and Telemetry Protocol

Deliverable 2 for the advisor review, and the file required by gate L7. It defines the
telemetry fields, sequence numbers, acknowledgments, deduplication, queue capacity and
trust configuration for the mandatory system ([scope.md](scope.md)).

**Status: PROPOSED draft.** Values marked *(proposed)* are our suggestions for the team to
confirm with the advisor. Nothing here is implemented or measured yet. Per
[CONTRIBUTING.md](../CONTRIBUTING.md), update this file before the code that depends on it.

## 1. Data flow

1. Pico A reads the MPU-6050 at 10 Hz and sends one UART frame per sample to Pi A.
2. Pi A checks the frame, assigns `node` and `seq`, and **writes the record to its
   persistent queue and flushes it to disk** before anything is sent.
3. Pi A sends the record over TLS 1.3 to Pi B.
4. Pi B checks the record, **writes it to its durable store**, and only then sends an ACK.
5. Pi A deletes the record from its queue after the ACK.
6. Pi B sends the unique-record count and latest sample to Pico B, which shows them on the
   OLED.

If the connection fails at any point, Pi A keeps sampling and queueing. On reconnect it
replays every un-ACKed record in sequence order.

## 2. Pico A → Pi A UART frame

UART0, 115200 baud, 8N1, 3.3 V. All multi-byte fields are **big-endian** (matching the
MPU-6050 register order).

| Offset | Size | Field | Meaning |
|--------|------|-------|---------|
| 0 | 2 | `sync` | `0xA5 0x5A`; the receiver re-syncs on this after any error |
| 2 | 1 | `len` | Payload length in bytes, 1–32 *(proposed max)* |
| 3 | `len` | payload | See below |
| 3+`len` | 2 | `crc16` | CRC-16/CCITT-FALSE over `len` and the payload |

Sample payload (`len` = 16):

| Size | Field | Unit and range |
|------|-------|----------------|
| 4 | `pico_ms` | Milliseconds since Pico A booted, wraps at 2^32 |
| 2 | `ax` | Signed raw LSB. At ±2 g full scale, 16384 LSB per g |
| 2 | `ay` | as above |
| 2 | `az` | as above |
| 2 | `gx` | Signed raw LSB. At ±250 °/s full scale, 131 LSB per °/s |
| 2 | `gy` | as above |
| 2 | `gz` | as above |

The sensor range settings are fixed in the firmware and written in the evidence log. The
16384 and 131 figures are the MPU-6050 datasheet values for those ranges
([MPU-6050](research/mpu-6050.md), datasheet Revision 3.4). Absolute accuracy is limited (about ±3% sensitivity
tolerance), which is fine because the demo shows relative motion.

The receiver drops a frame with a bad CRC, a bad length or a wrong payload size, counts it
(`uart_bad_frames`), and re-syncs. It never crashes or blocks on a malformed frame.

**Shared encoder and parser** *(proposed, `src/proto/uart_frame.h`)*. The Pico firmware and Pi A use
the same C file, so both ends agree by construction. The CRC is CRC-16/CCITT-FALSE: polynomial
`0x1021`, initial value `0xFFFF`, no bit reflection, no final XOR, computed over the `len` byte and
the payload; the check value of the ASCII string `123456789` is `0x29B1`. Example (one sample:
`pico_ms` = `0x01020304`, `ax`..`gz` = 1, -1, 32767, -32768, 256, -256), 21 bytes:

```
a5 5a 10 01 02 03 04 00 01 ff ff 7f ff 80 00 01 00 ff 00 e0 ba
```

The parser keeps a window of at most 37 bytes (the longest frame) starting at the first byte it has
not yet judged, and looks at the window, not at a state:

1. A first byte that is not `0xA5`, or a first byte `0xA5` followed by a byte that is not `0x5A`, is
   noise: drop one byte and count it (`noise_bytes`).
2. A length of 0, above 32, or (when the receiver expects one size, 16 for samples) not that size is a
   **bad length**: drop the first byte, count it (`bad_length`), and look again.
3. A complete frame whose CRC does not match is a **bad CRC**: drop the first byte, count it
   (`bad_crc`), and look again.
4. A complete frame with a matching CRC is delivered (`frames_ok`) and removed.

Dropping only the first byte, then looking again, means the bytes after a damaged frame are searched
for the next real frame. A lost byte, an extra byte or a false `0xA5 0x5A` therefore costs only the
damaged frame, not the frames after it. `uart_bad_frames` is `bad_length` + `bad_crc`. The CRC
catches accidental corruption on the wire; it is not security (TLS protects the Wi-Fi link, and the
UART is a cable between two boards we own).

Pico A has no queue: if Pi A is down, samples are lost at the UART and counted as
`uart_gap` from `pico_ms` gaps.

**Pi A's sample source** *(proposed, `src/uart/sample_source.h`)*. Pi A opens the serial device
(115200 8N1, raw: no echo, no newline translation), feeds the bytes to the parser above and turns each
good frame into the start of a record: `pico_ms` and the six `imu` values from the frame, `ts_ms` from
Pi A's wall clock when the frame is read (late by however long Pi A was busy, since the kernel buffers
the bytes until Pi A reads them), and `node` and `seq` left for the queue to fill in.

Missing samples are counted from `pico_ms` with unsigned 32-bit subtraction, so the wrap at 2^32 is not
a gap. With `delta` = this sample's `pico_ms` minus the previous one's, and the nominal `period`
(100 ms at 10 Hz, a config value):

| `delta` | Meaning | Counted as |
|---------|---------|------------|
| at least `max_gap` (10 s *(proposed)*) | The Pico restarted, or stalled for longer than that | `resets`; no samples counted as lost |
| below `period` / 2 (including 0) | Two samples closer together than they should be | `early` |
| at least 1.5 x `period` | One or more samples never arrived | `gaps` + 1, and `samples_lost` += round(`delta` / `period`) - 1 |
| otherwise | On time, with jitter | nothing |

The first sample after Pi A starts never counts a gap (there is nothing to compare with), and the state
is kept in memory only. A frame that arrives damaged is counted as a bad frame by the parser, not as a
gap, but the sample it carried is missing, so the next good sample shows it as a gap: `samples_lost`
includes samples lost to bad frames.

## 3. The record

Pi A builds the record from the frame. This is what is stored, sent and deduplicated.

| Size | Field | Meaning |
|------|-------|---------|
| 1 | `node` | Sender ID. `1` = rover A. Fixed by configuration |
| 8 | `seq` | Unsigned 64-bit, per node, **starts at 1, +1 per record**, never reused or reset |
| 8 | `ts_ms` | Pi A wall clock when the frame arrived, milliseconds since the Unix epoch |
| 4 | `pico_ms` | From the frame |
| 12 | `imu` | `ax ay az gx gy gz`, six signed 16-bit values |

Total 33 bytes. The queue assigns `seq` when the record is appended, and it is part of the
same `fsync`ed write. A record rejected because the queue is full still uses up its `seq`, so
the loss shows as a gap. On restart Pi A continues from the larger of the queue header's
`next_seq` floor and the last record in the file, so it never reuses a number that was ACKed. `ts_ms` is
only as good as Pi A's clock; the Pis have no real-time clock, so keep them
time-synchronized (for example with `chrony`) and say so in the evidence. Latency is
measured with a clock reference both nodes share, or with a logic analyzer on the two UARTs.

## 4. Messages over TLS

**Phase 1 (2026-10-16):** no IMU exists yet, so the client sends the same 33-byte RECORD with
`seq` counting up from 1, `ts_ms` and `pico_ms` set from the clocks, and the six `imu` values
set to zero. The framing and fields do not change in Phase 2.

The application sends length-prefixed messages inside the TLS stream. Big-endian.

| Size | Field |
|------|-------|
| 1 | `type`: `0x01` RECORD, `0x02` ACK |
| 2 | `length` of the payload |
| `length` | payload |

- **RECORD** payload is the 33-byte record above. Any other length is a protocol error.
- **ACK** payload is `node` (1 byte) and `seq` (8 bytes), 9 bytes. It acknowledges exactly
  that record, and means "committed durably".
- An unknown `type`, a wrong `length`, a `node` the server does not know, or a `seq` of
  `0` closes the connection, is counted, and is logged. Pi B never trusts a length before
  checking it against the maximum (64 bytes *(proposed)*).

## 5. Acknowledgment and replay

- Pi A keeps up to **32** records in flight *(proposed)*, and resends a record if no ACK
  arrives within a timeout (2 s *(proposed)*). Both are config values.
- On reconnect, Pi A resends all un-ACKed records, oldest first, with exponential backoff
  between connection attempts (1 s up to 10 s *(proposed)*).
- If an ACK is lost, Pi A resends the record. Pi B sees a `node`+`seq` it already has,
  sends the ACK again, and changes nothing else.
- **How the Phase 1 client does it** *(proposed, `pi/tls_client.c`)*: it sends the oldest
  `min(waiting, window)` records in order, then reads that many ACKs. Pi B ACKs in order, so each
  ACK must be for the oldest waiting record; any other ACK, a bad message, a send or receive
  error, or no ACK within the timeout closes the connection and counts a lost link. Resending on a
  TCP stream that has stopped answering achieves nothing, so a timeout means reconnect, and
  everything not yet ACKed is resent from the oldest record. A record leaves the queue only
  through `queue_ack()` for a verified ACK (rule 15).
- **Only a bad link is retried.** A failed TCP connect, a TLS handshake that times out or hits a
  socket error (`PQC_ERR_TIMEOUT`, `PQC_ERR_IO`: packet loss, a stalled server) and a lost session
  are retried with backoff. Every other handshake failure (an alert, wrong CA, wrong hostname, group
  or policy mismatch) ends the run: no retry loop is allowed to hide a T03 or T04 failure or to look
  like a downgrade attempt (rules 12 and 13), and nothing is ever retried with weaker settings. The
  queue file survives, so a later run drains it. A peer that accepts TCP but never answers TLS is
  retried until `--deadline-ms`.
- Backoff starts at the initial value, doubles after each failed attempt up to the maximum, and
  returns to the initial value once a connection has had an ACK. Window, ACK timeout, initial and
  maximum backoff are command-line values (`--window`, `--timeout-ms`, `--backoff-ms`,
  `--backoff-max-ms`), never constants.
- **Phase 1 record source.** The client generates records at `--rate-hz` (default 10;
  0 = all `--count` records at once) with zero IMU values. `--count 0` generates none and only
  drains what the queue file already holds (the restart test, T07). Ticks missed while the client
  waits for a slow ACK or connect are made up afterwards, so `ts_ms` can be late by up to the
  timeout; this does not exist once real samples arrive from the Pico.
- The client prints one summary line per run: records generated, accepted, rejected, ACKed and
  still waiting, peak queue depth, lost links, queue capacity, rate, duration and drain time
  (section 7). Exit status 0 only if nothing is waiting and nothing was rejected.

## 6. Deduplication and the OLED count

- Pi B's store has a unique key on (`node`, `seq`). A new key inserts the record and
  returns "new"; an existing key returns "duplicate".
- Only a **new** insert changes the OLED unique-record count and the latest-sample display.
  The count is the number of records in the store, rebuilt from the file at every start,
  not a saved variable, so a reboot cannot change it.
- The ACK is sent only after the insert has been flushed to disk, and Pi B pushes the
  current count to Pico B at startup, so the display recovers after a reboot.
- A replayed record from an attacker with a valid TLS session is deduplicated the same way
  (test T05). A replay on the wire is rejected by TLS itself.
- Gaps in `seq` are allowed (for example from queue overflow), and are reported, not hidden.
- **How (decided 2026-10-17, `src/store/store.h`):** an append-only file. An 8-byte header
  `PQCSTOR1`, then one 40-byte entry per unique record: the 36-byte RECORD message of section 4
  (header included, big-endian) followed by a big-endian CRC-32 (IEEE 802.3, as zlib) of those 36
  bytes. Each entry is `fsync()`ed before the ACK. On start the file is read back to rebuild the
  dedup index and the count. A short or bad-CRC **last** entry is a write cut short by a crash and
  is cut off (it was never ACKed). Damage before the last entry, a repeated `node`+`seq`, or a file
  without the header stops the server and the file is left untouched.
- When the store is full, a duplicate is still ACKed again; a new record is rejected, counted and
  not ACKed.

## 7. Persistent queue and capacity

| Parameter | Value | Why |
|-----------|-------|-----|
| Sample rate | 10 Hz | Build Guide gate L2 |
| Queue capacity | 10,000 **un-ACKed** records, set with `--queue-max` *(proposed)* | About 1,000 s of data, over 16 times a 60 s outage (600 records). 40 bytes per entry (the 36-byte RECORD plus the CRC-32), so 400 kB at capacity, far below any microSD limit. The limit counts records not yet ACKed, not entries in the file, because ACKed entries stay in the file until it is compacted |
| Overflow policy | Reject **new** records, count them, and still consume their `seq` *(proposed)* | Never discard a record already promised durable. The `seq` gap and the count are the visible record of the overflow (section 6 allows gaps) |
| Store | Append-only file with `fsync` per record, CRC-32 per entry (decided 2026-10-17, section 6) | Survives a power cut mid-write: only the last entry can be torn, and it was never ACKed. Per-record fsync cost on microSD still to be measured in L7 |

Each reliability test records: queue capacity, sample rate, test duration, number of
records accepted by Pi A, number delivered and ACKed, peak queue depth, overflow count and
drain time. Overflow is reported, never hidden.

Test durations *(proposed)*: 60 s outage runs repeated 10 times; loss runs at 0, 10 and 30%
of 10 minutes each, repeated 10 times. Confirm counts with the advisor.

How the T08 loss runs are done *(proposed, `scripts/run_loss_test.sh`)*: `tc netem` drops packets
at the given percentage on the interface, in both directions, so a request and its answer can each
be lost. A run is delivered if nothing is still waiting and the records stored on Pi B equal the
records the client accepted. Every run is reported, delivered or not, with its lost sessions, failed
connection attempts, TCP retransmissions, peak queue depth and drain time; a group of fewer than 10
runs is marked INSUFFICIENT. A and B alternate within each loss level. The loss method is recorded
with each run (advisor Q10). On a development machine it is `netem` on the loopback interface inside
a private network namespace with the MTU set to 1500, so messages are split into packets as on
Ethernet or Wi-Fi; on the Pis it is `netem` on the interface or the access point.

Queue-full test (L9) *(proposed)*: run with `--queue-max 100` and the server down for 30 s, so
the result is deterministic: 300 records generated, 100 queued, 200 rejected and counted, with
the `seq` gaps reported. Run the full 10,000 capacity once as a separate check, since filling
it takes about 17 minutes at 10 Hz.

Deleting after an ACK *(proposed, `src/queue/queue.h`)*: the file stays append-only. Each ACK is
appended as its own 16-byte entry (the 12-byte ACK message and a CRC-32) and `fsync`ed; a record
is waiting if no ACK entry follows it. Entries are 40 bytes (records) or 16 bytes (ACKs), told
apart by the message type byte. The 20-byte header (`PQCQUEU1`, a `next_seq` floor, a CRC-32) is
what keeps a `seq` from being reused after compaction. Pi A compacts by writing `<path>.tmp` with
only the waiting records, `fsync`ing it and renaming it over the queue file, so a crash leaves
either the old or the new file whole. It compacts at every start when the file holds ACKed
records, and while running once as many records have been ACKed as the queue can hold. The same
torn-tail rule as the store applies (section 6). A crash between the server's commit and the
ACK entry is safe: the record is resent and Pi B deduplicates it.

## 8. Trust configuration

| Setting | Configuration A (classical) | Configuration B (hybrid) |
|---------|-----------------------------|--------------------------|
| TLS version | 1.3 only (min = max) | 1.3 only (min = max) |
| Key-exchange group | `X25519` only | `X25519MLKEM768` only |
| Cipher suite | One suite, identical in both: `TLS_AES_128_GCM_SHA256` (decided 2026-10-16) | same |
| Server certificate | Pinned demo CA, hostname `relay.test` *(from the Build Guide example)* | same |
| Peer verification | Required; failure is fatal | Required; failure is fatal |
| Client certificate | None (one-way authentication) *(open)* | None *(open)* |
| Session resumption and 0-RTT | **Off**, so every handshake is a full handshake | **Off** |
| Fallback | None | None. Never falls back to A |

- Both **client and server** set the group list, so either side refuses a mismatch (T04).
- Certificates are backdated one day and valid 30 days *(proposed)*, because a Pi with no real-time
  clock may boot with a wrong time; time checks stay on ([certificates and time](research/certificates-and-time.md)).
- The demo CA key stays off the Pis and out of git. The server key lives only on Pi B. Pi A
  holds only the CA certificate. Certificates are generated by a script and are short-lived.
- Authentication is classical (RSA or ECDSA). **Hybrid key establishment does not make the
  authentication post-quantum.** Say this in the report.
- OpenSSL 3.5's default group list is hybrid-first, so A must set `X25519` explicitly. Never use
  `DEFAULT`, and never put a `?` before the group in B, so a build without ML-KEM fails loudly.
  Details and the C calls: [OpenSSL TLS 1.3](research/openssl-tls13-hybrid.md) *(proposed)*.
- The server logs the negotiated group and the verification result for every connection
  (T02, T03).

## 9. Failure behaviour

| Event | Expected behaviour | Test |
|-------|--------------------|------|
| Wrong certificate or hostname | Handshake fails, nothing is sent | T03 |
| Classical-only client to hybrid-only server (and the reverse) | Handshake fails, no fallback | T04 |
| Same record sent twice | Stored once, counted once, ACKed twice | T05 |
| Wi-Fi off for 60 s | Queue grows at 10 records/s, reconnect drains it | T06 |
| Pi A restarted during an outage | All un-ACKed records still in the queue | T07 |
| 10% and 30% emulated loss | Every accepted record eventually delivered | T08 |
| Modified TLS ciphertext | TLS rejects the record, connection closes | T09 |
| Pi B restarted | Count and records unchanged; display recovers | L8 |
| Bad UART frame | Dropped, counted, re-synced | L3 |
| Queue full | New records rejected and counted | L9 |

## 10. Benchmarks are separate from application telemetry

- **TLS handshake benchmark (T10).** A script opens 100 or more *successful* new
  connections per configuration and records, per attempt: latency, handshake bytes on the
  wire, CPU time and peak memory, the negotiated group and the verification result.
  Failures go to a separate file. Latency is reported two ways *(proposed)*: from before
  `connect()` to the end of the handshake, and for the TLS part alone. Byte counts come
  from a packet capture on the Pi, not an estimate.
- **Application telemetry.** Goodput, sensor-to-display latency, queue occupancy and drain
  time come from the live record flow and are stored in other files.

**How the harness measures it (Phase 1, `experiments/bench_handshake.c`).**
Configurations A and B are interleaved (A, B, A, B, ...) so drift hits both equally; the first
5 successes per configuration are warm-up and excluded; contexts are built once before timing, and
tickets and resumption are off so every attempt is a full handshake. Per attempt, on the client
side only: both latencies above; client CPU time over the connect-to-handshake span; handshake
bytes sent and received as counted by OpenSSL on the socket (the TLS byte stream, exact, without
TCP/IP headers; a packet capture is still needed for totals with headers); and `heap_delta_bytes`,
the live heap held by the open connection (`mallinfo2()`). The transient peak inside the handshake
is not captured per attempt; the process peak RSS is printed once per run. Percentiles are
nearest-rank. Successes go to `*.ok.csv`, failures to
`*.fail.csv`, and a run with fewer than 100 measured successes is marked INSUFFICIENT.

**Bytes on the wire and T09 over real TCP** *(proposed, `scripts/tls_proxy.py`, `scripts/pcap_lo.py`)*.
`tls_proxy.py` sits between the real client and server, forwards one TLS record at a time, records both
byte streams, and reads the unencrypted ClientHello and ServerHello: the groups offered and chosen, the
TLS versions, the cipher suite (so the pinned configuration is visible on the wire, T02), and whether a
HelloRetryRequest happened. With `--tamper c2s:N` or `s2c:N` it flips one bit in the Nth type-23 record of
that direction on the first connection (T09). `pcap_lo.py` records the loopback interface to a `.pcap`
that Wireshark opens, inside a private network namespace (no root), and counts per direction the packets,
the bytes with Ethernet, IP and TCP headers, the TCP payload bytes and retransmissions. The TCP payload
equals the TLS byte stream the proxy counts, which the end-to-end test checks. These are the T10 "bytes on
the wire" with headers; the handshake benchmark itself still counts the TLS stream only.

**Server side** *(proposed, `tls_server --handshake-csv`)*. The server writes one row per accepted
connection to its own file (`*.server_A.csv`, `*.server_B.csv`): CPU time of the server process over
`tls_accept()` (work, not waiting for the client), the live heap the open connection holds
(`mallinfo2()` before and after), handshake bytes received and sent (OpenSSL's own counters, so the
client's transmitted bytes must equal the server's received bytes and the reverse), and the process
peak RSS so far. A row is written and flushed after `tls_accept()` returns, never inside the timed
span, and an existing file is refused. The server cannot know which handshakes were warm-up, so the
summary counts rows that failed (`ok=0`), drops the first 5 successful rows per configuration, and
reports an error if its count differs from the client's. The peak RSS is the whole process, so it is
dominated by the server's baseline; the per-connection heap is the figure that separates A from B.
Client and server numbers describe the same handshakes but stay in separate files and are never mixed
with application telemetry.

Handshake numbers never come from a run that was also carrying telemetry unless the report
says so. Each benchmark run also records the Pi's CPU temperature and clock frequency, because the Pi 4
throttles to stay under 85 °C ([Raspberry Pi 4](research/raspberry-pi-4.md)) *(proposed)*.

## 11. Open points

- The Pi services are written in C (team decision, 2026-10-10; the advisor has not
  objected). The Pico firmware is C with the Pico SDK and does I/O only, no cryptography (team
  decision, 2026-10-19). The persistent store was decided on 2026-10-17 (section 6).
- Pi B's store capacity: `tls_server --store-max`, default 100,000 records *(proposed)*, about
  2.8 hours at 10 Hz and 1.6 MB of index memory. The queue capacity in section 7 is for Pi A.
- Final queue size, overflow policy, in-flight window and timeouts above.
- Whether Pi B authenticates Pi A (mutual TLS). The Build Guide asks for server
  verification only.
- The IMU ranges, and whether gyro values stay in the record or only acceleration does.
  The Build Guide's tests need only acceleration.
