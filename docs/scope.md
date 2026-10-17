# Project Scope (authoritative)

Final scope for PQC-1 as set by the advisor's email of 2026-10-10. It overrides the
original Canvas / Research Guide description wherever they conflict. The RSCL PQC LINK
Build Guide V2 is the implementation and acceptance document. Background on the
conflict: [guide_comparison.md](guide_comparison.md). Answers to our questions:
[advisor_questions.md](advisor_questions.md).

## Phase 1 direction (advisor, 2026-10-16)

The advisor asked us to implement and test the complete PQC secure link **on existing
computers first**, with no hardware purchases at this stage. Once there is a working
implementation with verified encryption, decryption, communication and performance
measurements, the migration to Raspberry Pi Pico W boards will be investigated.

- **Phase 1 (now):** the link runs between computers we already have. Same software scope
  as below: TLS 1.3, configurations A and B, persistent queue, ACK, dedup. Sensor and
  display are simulated in software (proposed). No purchases.
- **Phase 2 (after the advisor reviews Phase 1 results):** investigate the Pico W
  migration. Not started; needs the advisor's go-ahead.
- Phase 1 numbers are **computer-to-computer** results. Label every result with its
  platform and never present it as Pi 4B or Pico data.
- Gates L1–L4 and the T01 hardware test, and anything needing the Pi 4B, Pico, MPU-6050,
  OLED, scope or power instruments, are **deferred to Phase 2**, not failed. Electrical
  measurements (current, power, UART timing) are deferred with them.
- Open question for the advisor: the email says Pico **W**, while the Build Guide and
  our BOM use Pico **2** (no Wi-Fi). Do not change the BOM until this is clarified.

The architecture, configurations and mandatory tests below describe the full system and
are unchanged.

## Research question

What are the measured performance, communication and energy costs of introducing hybrid
post-quantum key establishment into a resource-constrained, physically implemented
wireless telemetry system, and how does the system behave under communication failures?

## Architecture (one system)

```
MPU-6050 --I2C--> Pico 2 A --UART--> Pi A (queue, TLS client)
                                       |  Wi-Fi AP, TLS 1.3, X25519MLKEM768
                                     Pi B (TLS server, dedup, ACK) --UART--> Pico 2 B --I2C--> OLED
```

- Pi A keeps a persistent telemetry queue and is the TLS client. It verifies the server
  certificate and hostname, and refuses a classical-only key exchange.
- Pi B is the TLS server. It validates records, keeps persistent dedup state, ACKs only
  after a durable commit, and forwards accepted records to Pico B.
- Pico B shows the sensor data and the unique-record count on the OLED.
- No custom ML-KEM handshake and no second application-layer security protocol.

## Configurations

| Config | Key exchange | Status |
|--------|--------------|--------|
| A | TLS 1.3, X25519, verified identity | Mandatory benchmarking baseline. Never a fallback in hybrid-only operation. |
| B | TLS 1.3, `X25519MLKEM768`, verified identity | Mandatory final system |
| C | Pure ML-KEM-768 primitive via OpenSSL 3.5+ | Optional. Keygen, encaps, decaps latency, sizes, CPU, memory. Not a TLS session; never report it as one. Must not delay A and B. |

A and B use the same application, hardware, wireless conditions and TLS cipher suite.
The unsecured Wi-Fi socket exists only for the L4 diagnostic on an isolated network. It
is not in the T10 comparison.

## Measurements

- **Electrical:** Pi and Pico supply voltage, system current and power, UART timing
  (scope or logic analyzer), sensor sample rate.
- **Cryptographic (T10):** 100+ successful handshakes per configuration, with median and
  p95 latency, handshake byte overhead, CPU utilization, memory (method documented),
  verified certificate and negotiated group. Failed attempts are recorded separately.
- **Communication:** goodput, sensor-to-display latency, behaviour at 0/10/30% emulated
  loss, queue occupancy during outages, queue drain time after reconnect.
- The handshake benchmark and the application telemetry latency are **separate**
  measurements. Do not mix them.
- For each reliability experiment, specify queue capacity, sample rate, duration and
  accepted-record count. Report any overflow or rejection; do not hide it.

## Mandatory tests

From the Build Guide. Mark PASS only from evidence, and record each result in
`tests/positive_and_attack_tests.csv` or `tests/fault_injection.csv`.

| Test | What must happen |
|------|------------------|
| T01 | Tilting the rover sensor changes the relay OLED sample |
| T02 | Log shows negotiated `X25519MLKEM768` |
| T03 | Untrusted cert or wrong hostname: TLS fails closed |
| T04 | Classical-only offer to a hybrid-only peer: handshake fails, no silent downgrade |
| T05 | Replayed application seq: one delivery count only |
| T06 | 60 s Wi-Fi interruption: queue keeps growing, reconnect drains it |
| T07 | Restart Pi A during an outage: persisted records remain for retry |
| T08 | 10% and 30% emulated loss: all accepted records eventually delivered |
| T09 | Tampered TLS ciphertext: integrity check rejects it |
| T10 | 100 hybrid vs 100 classical handshakes: latency, memory and byte-overhead CSV |

## Order of work

Gates L1–L10 (details and evidence files in [schedule.md](schedule.md)):
L1–L4 hardware, IMU, UART, OLED and plain diagnostic. L5–L6 OpenSSL and hybrid TLS.
L7–L9 persistent queue, ACKs, dedup, outage recovery. L10 loss tests and final
comparison. If a gate fails, fix it before moving on. Until Phase 2, only the software
gates are worked, on computers; see "Phase 1 direction" above. **No** Ascon, custom protocol or
FPGA work until L1–L10 are complete and approved. **No** purchasing or optional
architecture work without approval.

## Claims the report must get right

- Hybrid key establishment does not imply post-quantum authentication. The baseline uses
  a classical certificate, and the report must distinguish the two.
- Educational Wi-Fi link only. Not flight-approved, not space-qualified, not NASA
  endorsed, not CCSDS BPv7 DTN, and not fully PQ-authenticated end to end.

## Deliverables due before the next technical review

1. System block diagram: all components, I2C, UART, Wi-Fi, TLS, persistent storage, OLED.
2. Architecture and protocol document: telemetry fields, sequence numbers, ACKs, dedup,
   queue capacity, trust configuration (this becomes `docs/telemetry_format.md`).
3. Responsibility matrix: a primary owner and a backup for every hardware, software,
   integration and measurement task.
4. Implementation schedule mapped to L1–L10.
5. Bill of materials: existing lab equipment, missing parts, items needing purchase approval.

## Future research extension (not required)

The original ML-KEM + Ascon-AEAD128 design stays available for later. It needs a defined
research question, an independent protocol specification, and the advisor's approval.
