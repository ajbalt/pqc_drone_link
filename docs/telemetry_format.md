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
16384 and 131 figures are the MPU-6050 datasheet values for those ranges; confirm them
against the datasheet on file before reporting physical units.

The receiver drops a frame with a bad CRC, a bad length or a wrong payload size, counts it
(`uart_bad_frames`), and re-syncs. It never crashes or blocks on a malformed frame.
Pico A has no queue: if Pi A is down, samples are lost at the UART and counted as
`uart_gap` from `pico_ms` gaps.

## 3. The record

Pi A builds the record from the frame. This is what is stored, sent and deduplicated.

| Size | Field | Meaning |
|------|-------|---------|
| 1 | `node` | Sender ID. `1` = rover A. Fixed by configuration |
| 8 | `seq` | Unsigned 64-bit, per node, **starts at 1, +1 per record**, never reused or reset |
| 8 | `ts_ms` | Pi A wall clock when the frame arrived, milliseconds since the Unix epoch |
| 4 | `pico_ms` | From the frame |
| 12 | `imu` | `ax ay az gx gy gz`, six signed 16-bit values |

Total 33 bytes. `seq` is assigned **after** the record is durable. On restart Pi A
continues from the highest `seq` in its store, so it never reuses a number. `ts_ms` is
only as good as Pi A's clock; the Pis have no real-time clock, so keep them
time-synchronized (for example with `chrony`) and say so in the evidence. Latency is
measured with a clock reference both nodes share, or with a logic analyzer on the two UARTs.

## 4. Messages over TLS

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

## 6. Deduplication and the OLED count

- Pi B's store has a unique key on (`node`, `seq`). A new key inserts the record and
  returns "new"; an existing key returns "duplicate".
- Only a **new** insert changes the OLED unique-record count and the latest-sample display.
  The count is `SELECT COUNT(*)`, not a running variable, so a reboot cannot change it.
- The ACK is sent only after the insert has been flushed to disk, and Pi B pushes the
  current count to Pico B at startup, so the display recovers after a reboot.
- A replayed record from an attacker with a valid TLS session is deduplicated the same way
  (test T05). A replay on the wire is rejected by TLS itself.
- Gaps in `seq` are allowed (for example from queue overflow), and are reported, not hidden.

## 7. Persistent queue and capacity

| Parameter | Value | Why |
|-----------|-------|-----|
| Sample rate | 10 Hz | Build Guide gate L2 |
| Queue capacity | 10,000 records *(proposed)* | About 1,000 s of data, over 16 times a 60 s outage (600 records). About 330 kB of payload, so far below any microSD limit |
| Overflow policy | Reject **new** records and count them *(proposed)* | Never discard a record already promised durable. The count is reported as a number |
| Store | Open: SQLite in WAL mode with full sync, or an append-only file with `fsync` | Must survive a power cut mid-write. Decide in L7 |

Each reliability test records: queue capacity, sample rate, test duration, number of
records accepted by Pi A, number delivered and ACKed, peak queue depth, overflow count and
drain time. Overflow is reported, never hidden.

Test durations *(proposed)*: 60 s outage runs repeated 10 times; loss runs at 0, 10 and 30%
of 10 minutes each, repeated 10 times. Confirm counts with the advisor.

## 8. Trust configuration

| Setting | Configuration A (classical) | Configuration B (hybrid) |
|---------|-----------------------------|--------------------------|
| TLS version | 1.3 only (min = max) | 1.3 only (min = max) |
| Key-exchange group | `X25519` only | `X25519MLKEM768` only |
| Cipher suite | One suite, identical in both *(proposed: `TLS_AES_128_GCM_SHA256`)* | same |
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

Handshake numbers never come from a run that was also carrying telemetry unless the report
says so.

## 11. Open points

- Persistent store choice. The Pi services are written in C (team decision, 2026-10-10;
  the advisor has not objected). Pico firmware language is still open.
- Final queue size, overflow policy, in-flight window and timeouts above.
- The shared cipher suite for A and B.
- Whether Pi B authenticates Pi A (mutual TLS). The Build Guide asks for server
  verification only.
- The IMU ranges, and whether gyro values stay in the record or only acceleration does.
  The Build Guide's tests need only acceleration.
