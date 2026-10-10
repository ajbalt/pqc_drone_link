# Course Guide Comparison

Two course documents describe this project differently. This page records both, where
they conflict, and what we have decided to do until the advisor answers
([advisor_questions.md](advisor_questions.md)).

Source documents (course materials, not stored in git):

- **Research Guide** — *RSCL Undergraduate Research and Competition Guide, Fall 2026
  (Expanded)*, page 8, "PQC-1: Quantum-Secure Drone Link". This is the original project
  and the basis of the current repo.
- **Build Guide** — *RSCL PQC LINK ECE Build Guide V2*, "LINK / PURPLE" pages. Designed by
  Dr. Mohamed El-Hadedy. Describes a physical prototype and is dated October 2026.

## What the Build Guide requires (LINK)

**Data path.** MPU-6050 IMU on Pico A (I2C) → Pico A to Pi A over UART (115200 8N1,
3.3 V) → Pi A to Pi B over live Wi-Fi with TLS 1.3 restricted to `X25519MLKEM768` →
Pi B to Pico B over UART → SSD1306 OLED. Sample rate 10 Hz.

**Security contract.**

- Do not write a custom ML-KEM handshake. Use OpenSSL 3.5+.
- Verify a pinned demo CA/cert and hostname. A classical cert is not PQ authentication.
- Both peers fail closed if only classical X25519 is offered (no downgrade).
- Pi A persists a record before sending. Pi B commits `node+seq` durably before ACKing.
- Receiver deduplicates, so the OLED count changes once per unique record.
- Claims boundary: educational Wi-Fi link, not flight-approved, not CCSDS BPv7 DTN,
  not fully PQ-authenticated.

**Gates (do in order; stop if a result is missing).**

| Gate | What | Evidence file |
|------|------|---------------|
| L1 | Power and network; UART pins measured at 3.3 V | `evidence/L1_power_network.txt` |
| L2 | IMU on Pico A, 10 Hz samples change when tilted | `evidence/L2_imu_motion.csv` |
| L3 | 100 UART records match; OLED shows test count | `evidence/L3_uart_scope.png` |
| L4 | Plain Wi-Fi socket diagnostic, isolated network only | `evidence/L4_plain_diagnostic.mp4` |
| L5 | OpenSSL 3.5+ on both Pis; hybrid group shown by `s_client` | `evidence/L5_tls_handshake.log` |
| L6 | TLS app sockets, hybrid only, cert and hostname verified | `tests/tls_identity_results.txt` |
| L7 | Record format `{node, seq64, timestamp, IMU payload}`; persistent queue | `docs/telemetry_format.md` |
| L8 | Durable ACK and dedup | `evidence/L8_replay.csv` |
| L9 | 60 s outage, reconnect, replay | `evidence/L9_outage.csv` |
| L10 | 0/10/30% loss; 100+ handshakes hybrid vs X25519 | `evidence/link_benchmark.csv` |

Each gate needs a dated PASS/FAIL log, the code revision and board IDs, and a photo,
logic trace or recording. The instructor initials it.

**Faculty tests.**

| Test | What must happen |
|------|------------------|
| T01 | Tilting the rover sensor changes the relay OLED sample |
| T02 | Log shows negotiated `X25519MLKEM768` |
| T03 | Untrusted cert or wrong hostname: TLS fails closed |
| T04 | Classical-only offer to a hybrid-only client: handshake fails |
| T05 | Replayed application seq: one delivery count only |
| T06 | 60 s Wi-Fi interruption: queue grows, reconnect drains it |
| T07 | Restart Pi A during outage: persisted records remain |
| T08 | 10% and 30% emulated loss: all accepted records eventually delivered |
| T09 | Tampered TLS ciphertext: integrity validation rejects it |
| T10 | 100 hybrid vs 100 classical handshakes: latency, memory, byte overhead CSV |

**Measurements.** Scope of both Pico UARTs, DMM supply current on both Pis and the
sensor/display rails, Wi-Fi RSSI and bit rate, 100-handshake p50/p95, protocol byte
overhead, CPU/RAM, queue depth during outage, unique delivery ratio, OLED update delay,
queue drain time.

**Deliverables.** `hardware/` (schematic, pin map, PCB, photo), `ground/`, `pi/`, `pico/`,
README with dependency versions and configuration hashes, `tests/*.csv`,
`scripts/run_demo.sh`, `expected_results.md`, `evidence/*`.

**Schedule.** 28 weeks, gates G0–G8 (G1 hardware without PQC, G2 positive and negative
PQC, G4 first public demo, G6 100+ run benchmarking, G7 code freeze, G8 instructor
red-team demo).

**Automatic failures.** Unsafe voltage or load, a TLS client that accepts a wrong
identity, no physical demonstration or invented measurements, and unproven claims of NASA
endorsement, space qualification, secure boot or end-to-end PQ signatures.

## Where the two guides conflict

| Topic | Research Guide (original) | Build Guide (LINK) |
|-------|---------------------------|--------------------|
| Key establishment | Integrate ML-KEM between two endpoints | TLS 1.3 `X25519MLKEM768` via OpenSSL; no custom ML-KEM handshake |
| Traffic protection | ASCON-AEAD with the established key | TLS record layer; Ascon never mentioned |
| PQC type | Pure ML-KEM replaces classical | Hybrid X25519+ML-KEM |
| Platform | Constrained robot, drone, radio | Two Pi 4B over Wi-Fi; Picos only do sensor and display I/O |
| Traffic | Telemetry and command | One-way IMU to OLED with ACKs |
| Baseline | Unsecured baseline benchmarked in week 1 | L4 plain socket is a diagnostic; T10 compares hybrid vs classical |
| Energy | Energy per handshake, power logged | DMM supply current, `evidence/power.csv` |
| Rekeying | Rekey time measured | Not mentioned; recovery is reconnect plus replay |
| Definition of done | Reproducible comparison with an explanation of when overhead is acceptable | Evidence-based PASS on gates and tests, 100-point rubric |
| Timeline | 4 weeks | 28 weeks, G0–G8 |
| Roles | Crypto, networking, measurement, attack/test | One owner per node (Pico A, Pi A, Pi B, AP and measurement) |

Where they agree: loss testing, handshake latency and byte counts, negative tests,
reproducibility, and no bare algorithm benchmarks.

## Impact on this repo (if the Build Guide governs)

| Area | Effect |
|------|--------|
| Custom handshake (`kem*`, `mlkem_shim`, `handshake`, `fragment`, `rekey`) | Not needed. Archive. |
| Ascon, `kdf.c`, `session.c` nonces and replay window | Replaced by TLS. Crypto rules 1, 2, 4, 5 and 9 largely stop applying. |
| Submodules (`ml-kem`, `ascon-c`) | Not needed. Replaced by a pinned OpenSSL 3.5+. |
| `timer`, `logger`, `errors.h`, `rng` | Reusable for instrumentation. |
| Research wiki | Reusable background. Ingest the Build Guide as a source. |
| New work | Pico firmware, UART framing, durable queue, ACK store and dedup, TLS client and server, outage and loss harness, evidence tree. |
| No-downgrade rule (rule 8) | Maps directly to test T04. |

## Interim decision (2026-10-10)

The questions are sent and unanswered. Until they are answered:

- **Do not** write handshake, fragment, rekey, session, KDF or packet-format code, and do
  not add the X25519 or ML-KEM wrappers. Any of it may be thrown away.
- **Safe to do:** keep the existing modules and tests passing, work on hardware bring-up
  that both guides need (L1–L3: IMU driver, UART framing, OLED, pin map, schematic), and
  OpenSSL 3.5 setup (L5).
- CLAUDE.md, CONTRIBUTING.md and README still describe the original design. They are
  intentionally unchanged until the advisor answers.
