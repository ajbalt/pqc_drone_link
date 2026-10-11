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

`src/` holds only the helpers kept from the earlier work: error codes, RNG and secret-wiping
helpers, a monotonic timer and a CSV event logger, with tests. The earlier custom ML-KEM +
Ascon stack was **archived**: it is no longer in `main`, but is preserved at tag
`custom-stack-v1` and branch `archive/custom-stack` (restore with
`git checkout custom-stack-v1`). The Pico firmware, the Pi services (C17), the hardware
files and the evidence folder do not exist yet; they are created as gates L1–L10 are reached.

## Gates L1–L10

Work follows ten gates in order, from power and wiring (L1) through hybrid TLS (L5–L6),
persistence and outage recovery (L7–L9), to the 0/10/30% loss and 100+ handshake comparison
(L10). A gate is done only when the instructor has initialed its evidence. The gates, evidence
files, schedule and owners are in [docs/schedule.md](docs/schedule.md) and
[docs/responsibility_matrix.md](docs/responsibility_matrix.md). The ten mandatory tests are in
[docs/scope.md](docs/scope.md). Parts: [docs/bom.md](docs/bom.md).

## Getting the code

```sh
git clone https://github.com/ajbalt/pqc_drone_link.git
```

There are no submodules any more. If you have an older clone, run
`git submodule deinit -f --all` before pulling, then delete `third_party/`.

## Building and testing the existing code

Requires CMake 3.28+ and a C17 compiler. No internet is needed to configure.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Rerun the `cmake -B build ...` configure step only after editing `CMakeLists.txt` or
  changing a cache option.
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
├── CMakeLists.txt             # Build system (C17 only)
├── src/                       # Code kept from the earlier work
│   ├── common/
│   │   └── errors.h           # Negative error codes returned by all functions
│   ├── crypto/
│   │   ├── rng.h              # rng_bytes(), secure_wipe(), ct_memcmp()
│   │   └── rng.c              # OS CSPRNG (getrandom())
│   └── metrics/
│       ├── timer.h / timer.c  # Monotonic wall time and process CPU time
│       └── logger.h / logger.c # Buffers events in memory, writes CSV logs
├── pico/                      # (planned) Pico A IMU + UART sender, Pico B OLED + UART receiver
├── pi/                        # (planned) Pi A client + queue, Pi B server + dedup store
├── hardware/                  # (planned) board_schematic.pdf, pin_map.csv, pcb/, actual_photo.jpg
├── evidence/                  # (planned) per-gate logs, captures, bench photos, final video
├── tests/
│   ├── test_rng.c             # rng_bytes(), secure_wipe(), ct_memcmp()
│   ├── test_metrics.c         # timer.c and logger.c
│   ├── positive_and_attack_tests.csv  # (planned) T01-T10 results
│   └── fault_injection.csv    # (planned) outage, restart and loss results
├── experiments/
│   └── configs/               # (planned) one file per experiment (config A/B, loss, duration)
├── scripts/
│   ├── run_demo.sh            # (planned) assemble-and-run for another student
│   ├── make_demo_certs.sh     # Demo CA and server certificate (backdated 1 day, 30 days)
│   ├── tls_group_matrix.sh    # Config A/B, T03 and T04 cases on loopback (bring-up check)
│   ├── tls_handshake_bytes.sh # Handshake bytes for A and B through a counting proxy
│   ├── proxy_count.py         # Helper: counts TCP payload bytes per direction
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
    └── results.md             # Our own measurements (preliminary dev-machine results so far)
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

Smaller questions from the earlier research (Q1–Q18) are in
[docs/research/open-questions.md](docs/research/open-questions.md). Many were about the
archived custom stack and are now moot; they are kept for the later research extension.
