# PQC-1: Quantum-Secure Drone Link (PQC-LINK)

RSCL senior-design project (Cal Poly Pomona). We build a physical wireless telemetry link
and measure what hybrid post-quantum key establishment costs on it.

**Research question:** what are the measured performance, communication and energy
costs of introducing hybrid post-quantum key establishment into a resource-constrained,
physically implemented wireless telemetry system, and how does the system behave under
communication failures?

**Scope.** The advisor's decision of 2026-10-10 is final: the RSCL PQC LINK Build Guide V2
is the implementation and acceptance document, and it replaces the earlier custom
ML-KEM + Ascon design. Details: [docs/scope.md](docs/scope.md). Background:
[docs/guide_comparison.md](docs/guide_comparison.md).

## The system

```
MPU-6050 --I2C--> Pico 2 A --UART--> Pi A (persistent queue, TLS client)
                                       |  dedicated Wi-Fi AP, TLS 1.3
                                     Pi B (TLS server, dedup, ACK) --UART--> Pico 2 B --I2C--> OLED
```

- A real IMU is read by Pico A and sent to Pi A, which queues each record on disk before
  sending it over TLS 1.3 to Pi B. Pi B stores the record, ACKs it, and shows the unique
  record count on the OLED through Pico B.
- Block diagram and wiring: [docs/block_diagram.md](docs/block_diagram.md).
- Records, ACKs, deduplication, queue and trust settings:
  [docs/telemetry_format.md](docs/telemetry_format.md).

| Config | Key exchange | Role |
|--------|--------------|------|
| A | TLS 1.3, X25519, verified identity | Mandatory benchmark baseline. Never a fallback. |
| B | TLS 1.3, `X25519MLKEM768`, verified identity | Mandatory final system |
| C | Pure ML-KEM-768 primitive via OpenSSL 3.5+ | Optional benchmark. Not a TLS session. |

A and B use the same application, hardware, wireless conditions and cipher suite. Hybrid
key establishment is **not** post-quantum authentication: the certificate is classical,
and every report says so. This is an educational Wi-Fi link, not flight-approved.

> **Before changing anything, read [CONTRIBUTING.md](CONTRIBUTING.md).** It holds the
> rules every change must follow. [docs/design_notes.md](docs/design_notes.md) explains
> *why* the code is designed the way it is, and tracks status. The research wiki in
> [docs/research/](docs/research/index.md) holds what the standards, papers and
> datasheets say.

## Status of the code in this repo

`src/` holds the earlier custom stack (error codes, `kem.h`, RNG helpers, timer, CSV
logger) built before the scope changed. It is **frozen**: it still builds and its tests
pass, and the timer and logger may be reused for measurement, but the custom handshake,
Ascon and ML-KEM shim are not part of the mandatory system. The Pico firmware, the Pi
services, the hardware files and the evidence folder do not exist yet; they are created
as gates L1–L10 are reached. Whether to archive the frozen code is still to be decided.

## Gates L1–L10

Work follows these in order; a gate is done only when the instructor has initialed its
evidence (a PASS/FAIL log, the code revision and board IDs, and a photo or trace).

| Gate | What |
|------|------|
| L1 | Power and network: both Pis boot and join the lab Wi-Fi; UART pins at 3.3 V |
| L2 | IMU on Pico A, 10 Hz samples that change when tilted |
| L3 | UART: 100 records match; OLED shows the test count |
| L4 | Plain Wi-Fi socket diagnostic, isolated network only |
| L5 | OpenSSL 3.5+ on both Pis; hybrid group shown by `s_client` |
| L6 | TLS app sockets: hybrid only, certificate and hostname verified |
| L7 | Record format and persistent queue on Pi A |
| L8 | Durable ACK and deduplication on Pi B |
| L9 | 60 s outage, reconnect, replay |
| L10 | 0/10/30% loss; 100+ handshakes, hybrid vs X25519 |

Schedule and owners: [docs/schedule.md](docs/schedule.md),
[docs/responsibility_matrix.md](docs/responsibility_matrix.md). Parts:
[docs/bom.md](docs/bom.md). The ten mandatory tests (T01–T10) are listed in
[docs/scope.md](docs/scope.md).

## Getting the code

The earlier crypto libraries are git submodules, so clone with `--recurse-submodules`:

```sh
git clone --recurse-submodules https://github.com/ajbalt/pqc_drone_link.git
# or, in an existing clone:
git submodule update --init --recursive
```

## Building and testing the existing code

Requires CMake 3.28+, a C17 compiler, and a C++20 compiler.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first configure needs internet (ml-kem deps)
cmake --build build
ctest --test-dir build --output-on-failure
```

- Rerun the `cmake -B build ...` configure step only after editing `CMakeLists.txt` or
  changing a cache option.
- To rebuild offline after the first configure, add `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.
  Deleting `build/` requires internet again.
- Benchmarks and experiments must use `Release` builds. Debug numbers are never reported.

### Requirements for the mandatory system

- Raspberry Pi 4B (4 GB) x2 with a 64-bit OS, Raspberry Pi Pico 2 x2, MPU-6050, an SSD1306
  128x64 I2C OLED, a dedicated 2.4 GHz Wi-Fi AP. Full list: [docs/bom.md](docs/bom.md).
- **OpenSSL 3.5 or newer** on both Pis. Check with
  `openssl list -tls1_3 -tls-groups | grep -i X25519MLKEM768`.
- A DMM and a logic analyzer or scope (lab-owned).
- Exact dependency versions and configuration hashes are recorded with each result.

## API documentation

Doxygen is optional and not part of the default build:

```sh
cmake --build build --target docs
# then open build/docs/html/index.html
```

The `docs` target only exists if Doxygen was installed when CMake was configured.

## Repo layout

Files marked *(planned)* do not exist yet.

```
pqc_drone_link/
├── README.md                  # This file: what the project is, how to build
├── CONTRIBUTING.md            # Rules every change must follow
├── CMakeLists.txt             # Build system for the existing C code
├── third_party/               # Frozen: ml-kem and ascon-c submodules (not in the mandatory system)
├── src/                       # Frozen custom stack; timer and logger reusable
│   ├── common/
│   │   └── errors.h           # Negative error codes returned by all functions
│   ├── crypto/
│   │   ├── mlkem_shim.h       # Plain C interface to ML-KEM
│   │   ├── mlkem_shim.cpp     # The only C++ file (C++20)
│   │   ├── rng.h              # rng_bytes(), secure_wipe(), ct_memcmp()
│   │   ├── rng.c              # OS CSPRNG (getrandom())
│   │   ├── kem.h              # Common interface every key exchange implements
│   │   ├── kem.c              # Mode lookup + length checks for every KEM
│   │   ├── kem_mlkem.c        # (planned, on hold) ML-KEM-512/768/1024
│   │   ├── kem_x25519.c       # (planned, on hold) Classical baseline
│   │   ├── kem_none.c         # Unsecured baseline
│   │   ├── kdf.c              # (planned, on hold) Shared secret -> session key
│   │   └── aead_ascon.c       # (planned, on hold) Encrypt/decrypt packets
│   ├── protocol/              # (on hold) custom handshake, fragmentation, session, rekey
│   ├── transport/
│   │   ├── udp.c              # Development transport
│   │   └── serial_radio.c     # (on hold)
│   ├── metrics/
│   │   ├── timer.h / timer.c  # Monotonic wall time and process CPU time
│   │   └── logger.h / logger.c # Buffers events in memory, writes CSV logs
│   └── app/                   # Baseline apps from the earlier plan (on hold)
├── pico/                      # (planned) Pico A IMU + UART sender, Pico B OLED + UART receiver
├── pi/                        # (planned) Pi A client + queue, Pi B server + dedup store
├── hardware/                  # (planned) board_schematic.pdf, pin_map.csv, pcb/, actual_photo.jpg
├── evidence/                  # (planned) per-gate logs, captures, bench photos, final video
├── tests/
│   ├── test_kem.c             # kem.h interface: lookup, length checks, every mode
│   ├── test_rng.c             # rng_bytes(), secure_wipe(), ct_memcmp()
│   ├── test_metrics.c         # timer.c and logger.c
│   ├── positive_and_attack_tests.csv  # (planned) T01-T10 results
│   └── fault_injection.csv    # (planned) outage, restart and loss results
├── experiments/
│   └── configs/               # (planned) one file per experiment (config A/B, loss, duration)
├── scripts/
│   ├── run_demo.sh            # (planned) assemble-and-run for another student
│   ├── netem_setup.sh         # (planned) packet-loss emulation
│   ├── power_logger.py        # (planned) current/power sensor logging
│   └── wiki_lint.py           # Health checks for the research wiki
├── expected_results.md        # (planned) what a correct run looks like
├── analysis/                  # (planned) CSV logs -> tables and charts
├── results/                   # (planned) raw logs (large ones stored elsewhere)
└── docs/
    ├── research/              # Research wiki: sources, topics, decision pages
    ├── scope.md               # Authoritative project scope (advisor decision, 2026-10-10)
    ├── advisor_questions.md   # Questions sent to the advisor, with answers
    ├── guide_comparison.md    # Research Guide vs Build Guide: conflicts and resolution
    ├── block_diagram.md       # System block diagram and wiring summary
    ├── telemetry_format.md    # Architecture and protocol: records, ACKs, dedup, queue, trust
    ├── responsibility_matrix.md # Owner and backup for every task
    ├── schedule.md            # L1-L10 mapped to the 28-week gates
    ├── bom.md                 # Parts, lab equipment, and purchases awaiting approval
    ├── design_notes.md        # Why the code looks the way it does; status and progress log
    ├── threat_model.md        # (planned) What attacks we defend against, and which we don't
    └── results.md             # (planned) Final write-up of our own measurements
```

## Plan

The Build Guide's 28-week schedule (gates G0–G8) replaces the old four-week plan. It is
mapped to L1–L10 in [docs/schedule.md](docs/schedule.md). In short: BOM and bring-up first
(G0–G1), OpenSSL and hybrid TLS with negative tests (G2–G3), a first live demo (G4),
persistence and recovery (G5), 100+ run benchmarking (G6), code freeze and audit
(G7), and the instructor red-team demo with report and video (G8).

## Open decisions

Tick these off here as the team decides, and record each on its page.

Decided by the advisor on 2026-10-10 ([docs/advisor_questions.md](docs/advisor_questions.md)):

- [x] Transport and radio: dedicated Wi-Fi access point
      ([decision page](docs/research/decisions/radio-link.md))
- [x] Handshake and traffic protection: TLS 1.3, no custom handshake, no Ascon
      ([KDF page](docs/research/decisions/kdf-construction.md))
- [x] Hybrid mode: `X25519MLKEM768` is the mandatory configuration B
      ([decision page](docs/research/decisions/hybrid-mode.md))
- [x] Authentication: pinned classical demo certificate with hostname check; ML-DSA is
      not in scope ([decision page](docs/research/decisions/handshake-auth.md))
- [x] Drone-side hardware: Raspberry Pi 4B plus Pico 2 (per the Build Guide)

Still open:

- [ ] Pico firmware language (C SDK or MicroPython). The Pi services are decided: C17
      (team decision 2026-10-10, still to be confirmed with the advisor, Q11)
- [ ] Current and power instrument: DMM only, or INA219/INA260; per-handshake energy needed?
      (advisor Q5)
- [ ] Loss emulation: `tc netem` on the Pis or at the access point (advisor Q10)
- [ ] Persistent store on each Pi (SQLite, or an append-only file with fsync)
- [ ] Queue capacity, overflow policy and test durations (proposed in
      [docs/telemetry_format.md](docs/telemetry_format.md))
- [ ] TLS cipher suite shared by configurations A and B
- [ ] Confirm: no rekey, no command channel, one-way server authentication (advisor Q6, Q8)
- [ ] Reuse the existing timer and logger for evidence CSVs? (advisor Q15)
- [ ] Archive or keep the frozen custom stack (team decision, pending)

Smaller questions from the earlier research (Q1–Q18) are in
[docs/research/open-questions.md](docs/research/open-questions.md). Many were about the
custom stack and are now moot; they are kept for the later research extension.
