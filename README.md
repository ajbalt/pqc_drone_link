# PQC-1: Quantum-Secure Drone Link (PQC-LINK)

RSCL senior-design project (Cal Poly Pomona). We build a physical wireless telemetry link
and measure what hybrid post-quantum key establishment costs on it.

**Research question:** what are the measured performance, communication and energy
costs of introducing hybrid post-quantum key establishment into a resource-constrained,
physically implemented wireless telemetry system, and how does the system behave under
communication failures?

**Scope.** The advisor's decision of 2026-10-10 is final: the RSCL PQC LINK Build Guide V2
is the implementation and acceptance document, and it replaces the earlier custom
ML-KEM + Ascon design. **Phase 1 (advisor, 2026-10-16):** build and measure the complete link on existing
computers first, no purchases; Pico W migration is investigated afterwards. Details:
[docs/scope.md](docs/scope.md). Background:
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

**Phase 1 (computers only, per the advisor on 2026-10-16) is working on `main` (merged from `pc-link`, PR #2):** a
TLS 1.3 client and server (C17, OpenSSL 3.5.9) that run configurations A and B, the wire format,
TCP helpers, and the T10 handshake benchmark harness. Mandatory failure cases (T03, T04, T09) are
covered by automated tests on a development machine. This is **not** gate evidence: that needs the
Pis. The server commits every record to a durable, deduplicated store before ACKing it
(`src/store/`; replay T05, restart and torn-write cases run end to end on one machine). Still
missing: the persistent queue on the client, outage and loss tests, server-side benchmark costs, and everything on the
Pico, sensor and display side (Phase 2). Details and next steps: [docs/design_notes.md](docs/design_notes.md).

`src/` also keeps the helpers from the earlier work: error codes, RNG and secret-wiping helpers, a
monotonic timer and a CSV event logger, with tests. The earlier custom ML-KEM + Ascon stack was
**archived**: it is not in `main`, but is preserved at tag `custom-stack-v1` and branch
`archive/custom-stack` (restore with `git checkout custom-stack-v1`). The `evidence/` folder
exists with its rules and a gate-record template; the Pico firmware and hardware files are
Phase 2 and do not exist yet.

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

Requires CMake 3.28+, a C17 compiler and OpenSSL 3.5+ (build it first with
[docs/build_openssl.md](docs/build_openssl.md)). No internet is needed to configure.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DOPENSSL_ROOT_DIR=$HOME/opt/openssl-3.5.9
cmake --build build
ctest --test-dir build --output-on-failure
```

- Rerun the `cmake -B build ...` configure step only after editing `CMakeLists.txt` or
  changing a cache option.
- The TLS link needs OpenSSL 3.5+. With only the system OpenSSL, configure stops with
  "Could NOT find OpenSSL"; to build everything else, add `-DPQC_BUILD_TLS=OFF`.
- Benchmarks and experiments must use `Release` builds. Debug numbers are never reported.

### Running the Phase 1 link by hand

After building, make demo certificates with the same OpenSSL (keys stay out of git):

```sh
P=$HOME/opt/openssl-3.5.9
LD_LIBRARY_PATH=$P/lib scripts/make_demo_certs.sh $P/bin/openssl /tmp/demo_certs
build/tls_server --config B --port 5000 --cert /tmp/demo_certs/srv.crt --key /tmp/demo_certs/srv.key \
                 --store /tmp/records.dat &
build/tls_client --config B --host 127.0.0.1 --port 5000 --ca /tmp/demo_certs/ca.crt --count 10
```

`--config A` pins X25519 and `--config B` pins `X25519MLKEM768`; both sides must use the
same one. The server writes each record to `--store` and `fsync()`s it before the ACK; run the
client again and the same seqs are ACKed as duplicates without changing the count. Delete
`/tmp/records.dat` to start from zero.

### Running the handshake benchmark (T10)

Needs a **Release** build. It starts a configuration-A and a configuration-B server on this machine,
alternates handshakes between them, and writes the raw CSVs, an environment record and a summary:

```sh
scripts/run_handshake_bench.sh $HOME/opt/openssl-3.5.9 build \
    experiments/configs/t10_handshake_loopback.conf results/
```

Results from a development machine are not gate evidence; record them in
[docs/results.md](docs/results.md) with the platform. Do not commit raw logs from `results/`. To tidy it, run `scripts/clean_results.sh --keep 3`
(a dry run; add `--apply` to delete old runs).

### Requirements for the mandatory system

- Raspberry Pi 4B (4 GB) x2 with a 64-bit OS, Raspberry Pi Pico 2 x2, MPU-6050, an SSD1306
  128x64 I2C OLED, a dedicated 2.4 GHz Wi-Fi AP. Full list: [docs/bom.md](docs/bom.md).
- **OpenSSL 3.5 or newer** on every node; build steps in
  [docs/build_openssl.md](docs/build_openssl.md). Check with
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
│   ├── proto/
│   │   ├── wire.h             # RECORD and ACK messages (encode, header check, decode)
│   │   └── wire.c
│   ├── net/
│   │   ├── tcp.h              # Blocking IPv4 TCP helpers (listen, accept, connect, timeouts)
│   │   └── tcp.c
│   ├── store/
│   │   ├── store.h            # Pi B durable store: append-only file + fsync, dedup by node+seq
│   │   └── store.c
│   ├── tls/
│   │   ├── tls_link.h         # TLS 1.3 client/server: pinned group and suite, verified peer
│   │   └── tls_link.c
│   ├── crypto/
│   │   ├── rng.h              # rng_bytes(), secure_wipe(), ct_memcmp()
│   │   └── rng.c              # OS CSPRNG (getrandom())
│   └── metrics/
│       ├── timer.h / timer.c  # Monotonic wall time and process CPU time
│       └── logger.h / logger.c # Buffers events in memory, writes CSV logs
├── pico/                      # Pico firmware (Phase 2, not started); README lists the jobs and prerequisites
├── pi/                        # Services in C17; README lists the programs and what is still to come
│   ├── tls_server.c           # Phase 1 TLS server: stores each RECORD durably, then ACKs it
│   └── tls_client.c           # Phase 1 TLS client: sends numbered RECORDs, checks each ACK
├── hardware/                  # Wiring and boards
│   ├── README.md              # Rules and file states
│   └── pin_map.csv            # Draft pin map by physical header pin (not yet verified on the bench)
│                              # (planned) board_schematic.pdf, pcb/, actual_photo.jpg
├── evidence/                  # Per-gate records and captures; see evidence/README.md
│   ├── README.md              # Rules, naming and what each gate needs
│   └── GATE_RECORD_TEMPLATE.md # Copy for each gate: setup, expected vs observed, sign-off
├── tests/
│   ├── test_rng.c             # rng_bytes(), secure_wipe(), ct_memcmp()
│   ├── test_metrics.c         # timer.c and logger.c
│   ├── test_wire.c            # wire.c: byte vector, round trips, bad input
│   ├── test_store.c           # store.c: file layout, dedup, restart, full, torn tail, corruption
│   ├── test_tls_link.c        # tls_link.c: A, B, T02, T03, T04, T09 over a socketpair
│   ├── test_tcp.c             # tcp.c on loopback
│   ├── e2e_tls_link.sh        # tls_server + tls_client: A, B, T03, T04, T05, restart, torn/foreign/full store
│   ├── e2e_bench.sh           # bench_handshake smoke test and failure paths
│   ├── test_summarize.py      # summarize_handshakes.py: percentiles, warm-up exclusion
│   ├── positive_and_attack_tests.csv  # T01-T10 results (header only so far)
│   └── fault_injection.csv    # Outage, restart and loss results (header only so far)
├── experiments/
│   ├── bench_handshake.c      # T10 client: many timed handshakes, A and B interleaved
│   └── configs/               # One file per experiment; README lists the fields
│       └── t10_handshake_loopback.conf  # Handshake benchmark on one machine
├── scripts/
│   ├── run_demo.sh            # (planned) assemble-and-run for another student
│   ├── make_demo_certs.sh     # Demo CA and server certificate (backdated 1 day, 30 days)
│   ├── tls_group_matrix.sh    # Config A/B, T03 and T04 cases on loopback (bring-up check)
│   ├── tls_handshake_bytes.sh # Handshake bytes for A and B through a counting proxy
│   ├── proxy_count.py         # Helper: counts TCP payload bytes per direction
│   ├── run_handshake_bench.sh # Runs the T10 benchmark from a config file; writes CSVs, meta, summary
│   ├── clean_results.sh       # Deletes old runs from results/ (dry run unless --apply)
│   ├── summarize_handshakes.py # Median, p95, spread per configuration from the raw CSV
│   ├── netem_setup.sh         # (planned) packet-loss emulation
│   ├── power_logger.py        # (planned) current/power sensor logging
│   └── wiki_lint.py           # Health checks for the research wiki
├── expected_results.md        # (planned) what a correct run looks like
├── analysis/                  # (planned) CSV logs -> tables and charts
├── results/                   # Raw benchmark output from run_handshake_bench.sh (git-ignored, not evidence)
└── docs/
    ├── research/              # Research wiki: sources, topics, decision pages
    ├── scope.md               # Authoritative project scope (advisor decision, 2026-10-10)
    ├── advisor_questions.md   # Questions sent to the advisor, with answers
    ├── guide_comparison.md    # Research Guide vs Build Guide: conflicts and resolution
    ├── block_diagram.md       # System block diagram and wiring summary
    ├── telemetry_format.md    # Architecture and protocol: records, ACKs, dedup, queue, trust
    ├── responsibility_matrix.md # Owner and backup for every task
    ├── schedule.md            # L1-L10 mapped to the 28-week gates
    ├── build_openssl.md       # How to build the pinned OpenSSL 3.5.9 on a team machine
    ├── bom.md                 # Parts, lab equipment, and purchases awaiting approval
    ├── design_notes.md        # Why the code looks the way it does; status and progress log
    ├── threat_model.md        # (planned) What attacks we defend against, and which we don't
    ├── report/                # LaTeX progress report: diagrams and plots from the raw benchmark CSVs
    │   ├── progress_report.tex  # Build with latexmk -pdf; every number comes from data/numbers.tex
    │   ├── prepare_data.py      # Turns results/*.ok.csv into the plot data (standard library only)
    │   └── data/                # Generated plot data; rerun prepare_data.py to refresh
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
- [x] Drone-side hardware: Raspberry Pi 4B plus Pico 2 (per the Build Guide). Superseded for
      now by the 2026-10-16 decision below
- [x] Pico W or Pico 2 for Phase 2: either is fine, use what is available (2026-10-16)
- [x] TLS cipher suite shared by configurations A and B: `TLS_AES_128_GCM_SHA256`
      ([docs/telemetry_format.md](docs/telemetry_format.md) section 8, 2026-10-16)
- [x] Phase 1 test payload: the spec RECORD with a counter and zeroed IMU values; no IMU or OLED
      simulation (2026-10-16)
- [x] Phase 1 runs on existing computers, no purchases; Pico W migration only after
      verified encryption, decryption, communication and measurements (2026-10-16)

Still open:

- [ ] Which computers run Phase 1 for the final measurements (so far one development machine)
- [ ] Server-side CPU and memory in the T10 benchmark (the harness measures the client only)
- [ ] Pico firmware language (C SDK or MicroPython). The Pi services are decided: C17
      (team decision 2026-10-10, still to be confirmed with the advisor, Q11)
- [ ] Current and power instrument: DMM only, or INA219/INA260; per-handshake energy needed?
      (advisor Q5)
- [ ] Loss emulation: `tc netem` on the Pis or at the access point (advisor Q10)
- [x] Persistent store: an append-only file with `fsync`, not SQLite (team decision 2026-10-17;
      [docs/telemetry_format.md](docs/telemetry_format.md) sections 6 and 7). Pi B's store is
      `src/store/`; the client queue will follow the same rules
- [x] Queue capacity, overflow policy and test durations: 10,000 un-ACKed records, reject new
      and count (2026-10-19; still *(proposed)* until the advisor confirms,
      [docs/telemetry_format.md](docs/telemetry_format.md) section 7)
- [ ] Confirm: no rekey, no command channel, one-way server authentication (advisor Q6, Q8)
- [ ] Reuse the existing logger for evidence CSVs? (advisor Q15). The timer is used; the benchmark
      writes its own per-attempt CSV because the logger's fixed columns do not fit

Smaller questions from the earlier research (Q1–Q18) are in
[docs/research/open-questions.md](docs/research/open-questions.md). Many were about the
archived custom stack and are now moot; they are kept for the later research extension.
