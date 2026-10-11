# Responsibility Matrix

Deliverable 3 for the advisor review. The Build Guide requires **one accountable owner and
one backup for every workstream**, and says no one gets credit for watching teammates build
the hardware.

**Status: PROPOSED draft.** Names are blank and must be filled in by the team. The Build
Guide splits the work among four students (A–D); if the team is a different size, merge
or split the columns and keep one primary and one backup per row.

## Workstreams

| Student | Workstream (from the Build Guide) | Name | Backup (proposed) |
|---------|-----------------------------------|------|-------------------|
| A | Rover Pico A: IMU, I2C driver, UART framing, electrical carrier | | C |
| B | Pi A: hybrid TLS client and persistent queue | | C |
| C | Pi B: TLS server, ACK and dedup store, Pico B OLED, wiring | | B |
| D | Access point, outage and loss testing, traces, benchmark and summary data | | A |

## Tasks

P = primary, B = backup. Fill in the name from the table above.

| Area | Task | Gate | P | B |
|------|------|------|---|---|
| Hardware | Wiring diagram, KiCad schematic, pin map, photos | L1 | A | C |
| Hardware | Carrier/interface PCBs (after approval) | G5 | A | C |
| Hardware | 3.3 V rail and UART checks with a DMM | L1, L3 | A | D |
| Hardware | Logic-analyzer and scope captures of both UARTs | L3 | D | A |
| Firmware | MPU-6050 driver, 10 Hz sampling, UART framing (Pico A) | L2, L3 | A | C |
| Firmware | UART receive and OLED display (Pico B) | L3 | C | A |
| Software | OpenSSL 3.5+ install, pinning, version evidence on both Pis | L5 | B | C |
| Software | Certificate script, pinned CA, hostname check | L5, L6 | B | C |
| Software | TLS client with forced group, verification, no fallback | L6 | B | C |
| Software | TLS server with forced group and logging | L6 | C | B |
| Software | Persistent queue, replay on reconnect (Pi A) | L7, L9 | B | C |
| Software | Durable store, dedup, ACK (Pi B) | L8 | C | B |
| Integration | End-to-end tilt-to-OLED demonstration | L4, L6 | C | A |
| Integration | Restart and power-cycle behaviour | L7–L9 | B | C |
| Measurement | Access point set up, outage and loss emulation | L9, L10 | D | B |
| Measurement | 100+ handshake benchmark, configurations A and B | L10 | D | B |
| Measurement | Current/power, RSSI, goodput, queue and drain data | L9, L10 | D | A |
| Tests | T01–T10 run and recorded in `tests/*.csv` | L10 | D | each owner |
| Tests | Adversarial tests for UART frames and the application protocol | L3, L8 | B | C |
| Docs | Telemetry and protocol document ([telemetry_format.md](telemetry_format.md)) | L7 | B | C |
| Docs | Evidence folder, three-file gate packs, instructor initials | all | D | A |
| Docs | README, run script, expected results, dependency versions | G7 | C | D |
| Docs | Reproducibility audit by someone **not** responsible for the part | G7 | rotating | |
| Docs | Final report, claims wording, 3-minute video | G8 | D | B |

## Rules

- Every row has exactly one primary. A backup must be able to take over without
  re-learning the work, which means they have reviewed the primary's evidence.
- The G7 audit is done by a teammate who did not write that component.
- If a project is more than two weeks behind, the Build Guide says to drop optional work
  (dashboard, FPGA, publication) at once and keep the physical interface, the secure
  gate and the mandatory negative tests.
- The existing software (timer, logger, error codes, frozen custom stack) was written
  before the scope change. Decide who owns it, and whether it is kept or archived.
