# Implementation Schedule

Deliverable 4 for the advisor review: gates L1–L10 mapped onto the Build Guide's 28-week
faculty gates (G0–G8).

**Status: PROPOSED draft.** The Build Guide defines L1–L10 and G0–G8 separately and does
not say which L falls in which G, so the mapping below is our proposal. Weeks are relative
to team kickoff, which was not given: **kickoff date: TBD.** Fill in dates once it is set.

## Phase 1 / Phase 2 (advisor, 2026-10-16)

Phase 1 runs the complete link on existing computers, with no purchases. Gates and
weeks that need Pi 4B, Pico, sensor, OLED or instruments are **deferred to Phase 2**
(Pico W migration, after the advisor reviews Phase 1). The mapping below is the full
plan; the software parts of L5–L10 come first. Dates need re-planning once the Phase 1
computers are chosen.

## Mapping

| Weeks | Faculty gate | Build gates | What must be true (evidence) |
|-------|--------------|-------------|------------------------------|
| W1–2 | G0 | Before L1 | BOM quoted and approved; Pis and Picos powered; 3.3 V rails and wiring drawings signed ([bom.md](bom.md), [block_diagram.md](block_diagram.md)) |
| W3–5 | G1 | L1, L2, L3, L4 | The physical pipeline works **without PQC**: IMU to Pico A to Pi A, relay to Pico B and OLED, plain Wi-Fi socket on an isolated network. UART captured |
| W6–8 | G2 | L5, start L6 | OpenSSL 3.5+ pinned; hybrid group shown by `s_client`; positive and negative TLS tests |
| W9–11 | G3 | L6, L8 (replay) | TLS gates the real link; identity, downgrade and replay failures in a running test suite |
| W12–14 | G4 | L1–L6 done | First-semester public live demo: secure tilt-to-OLED over hybrid TLS, plus one failure test (for example wrong certificate) |
| W15–18 | G5 | L7, L8, L9 | Persistent queue, durable ACK and dedup, 60 s outage recovery, restart recovery, cabling and PCB integration |
| W19–22 | G6 | L10 | 100+ handshake benchmark for A and B, 0/10/30% loss runs, instrument traces, all T01–T10 recorded |
| W23–25 | G7 | Freeze | Code freeze; a teammate who did not write each part reproduces it from a clean setup |
| W26–28 | G8 | Final | Instructor red-team demo, final report, design files, test suite, 3-minute video |

The instructor must see the hardware output before a checkpoint counts as DONE.

## Task detail

| Gate | Task | Output file | Owner |
|------|------|-------------|-------|
| L1 | Boot both Pis, join the lab Wi-Fi, measure UART pins at 3.3 V | `evidence/L1_power_network.txt` | A, D |
| L2 | MPU-6050 on Pico A, 10 Hz stream, values change when tilted | `evidence/L2_imu_motion.csv` | A |
| L3 | 100 UART records match; OLED shows the count; scope capture | `evidence/L3_uart_scope.png` | A, C |
| L4 | Plain socket telemetry, isolated network, record the delay | `evidence/L4_plain_diagnostic.mp4` | C |
| L5 | OpenSSL 3.5+ on both Pis; certificate identity checked | `evidence/L5_tls_handshake.log` | B |
| L6 | Forced-group TLS app sockets; impostor certificate and hostname fail | `tests/tls_identity_results.txt` | B, C |
| L7 | Record format; persistent queue; restart keeps un-ACKed records | `docs/telemetry_format.md` | B |
| L8 | Durable store and ACK; repeated record counted once; survives reboot | `evidence/L8_replay.csv` | C |
| L9 | 60 s outage, reconnect, re-TLS, replay; drain interval measured | `evidence/L9_outage.csv` | D, B |
| L10 | 0/10/30% loss; 100+ handshakes A vs B; CPU, memory, bytes | `evidence/link_benchmark.csv` | D |

Each gate needs three files: a dated PASS/FAIL log, the exact code revision and board
IDs, and a photo, trace or recording. If a result is missing, do not advance. Change one
variable at a time and keep the failed output.

## Deliverables before the next technical review

| # | Deliverable | File | State |
|---|-------------|------|-------|
| 1 | System block diagram | [block_diagram.md](block_diagram.md) | Draft |
| 2 | Architecture and protocol document | [telemetry_format.md](telemetry_format.md) | Draft |
| 3 | Responsibility matrix | [responsibility_matrix.md](responsibility_matrix.md) | Draft, names needed |
| 4 | Implementation schedule | this page | Draft, kickoff date needed |
| 5 | Bill of materials | [bom.md](bom.md) | Draft, quotes needed |

## Risks and watch items

- **OpenSSL version.** Hybrid groups need 3.5 or newer; Raspberry Pi OS may ship an older
  version. Build or install a pinned version early (L5) and record it. This blocks G2.
- **Hardware first.** L5 and later must not start before L1–L4 are signed. The existing
  software modules do not count toward these gates. *Exception (advisor, 2026-10-16):* Phase 1
  builds and tests the software parts of L5–L10 on computers first. That work is development, not
  gate evidence: each gate still needs its Pi evidence and the instructor's initials.
- **Lead times.** Parts and PCBs need approval before purchase. Order the minimum set in W1–2.
- **SD-card wear and sync cost.** Flushing every record to microSD costs time and wear;
  measure it in L7 before choosing the store.
- **Wi-Fi interference.** Use a dedicated AP and record RSSI and bit rate with each run.
- **Falling behind.** If more than two weeks behind, drop optional work and keep the
  physical interface, the secure gate and the negative tests.
- **Optional work** (Config C, Ascon, FPGA) starts only after L1–L10 are done and
  approved. Config C may be done earlier only if it never delays A and B.
