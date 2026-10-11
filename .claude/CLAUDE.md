# CLAUDE.md — PQC-1: Quantum-Secure Drone Link (PQC-LINK)

This file tells Claude (and new team members) how this repo works and what rules to follow.
Read it fully before making changes.

## Project purpose

RSCL senior-design project. We build a physical wireless telemetry link and measure what
**hybrid post-quantum key establishment** costs on it.

Research question: what are the measured performance, communication and energy costs of
introducing hybrid post-quantum key establishment into a resource-constrained, physically
implemented wireless telemetry system, and how does the system behave under communication
failures?

**Authoritative scope: `docs/scope.md`** (advisor's decision, 2026-10-10). The RSCL PQC LINK
Build Guide V2 is the implementation and acceptance document and overrides the original
Canvas / Research Guide description (custom ML-KEM + Ascon). Background:
`docs/guide_comparison.md`; answers: `docs/advisor_questions.md`.

System:

```
MPU-6050 --I2C--> Pico 2 A --UART--> Pi A (persistent queue, TLS client)
                                       |  dedicated Wi-Fi AP, TLS 1.3
                                     Pi B (TLS server, dedup, ACK) --UART--> Pico 2 B --I2C--> OLED
```

| Config | Key exchange | Role |
|--------|--------------|------|
| A | TLS 1.3, X25519, verified identity | Mandatory benchmark baseline; never a fallback |
| B | TLS 1.3, `X25519MLKEM768`, verified identity | Mandatory final system |
| C | Pure ML-KEM-768 primitive via OpenSSL 3.5+ | Optional benchmark; not a TLS session, never report it as one |

A and B use the same application, hardware, wireless conditions and cipher suite; the key
exchange group is the only variable. The unsecured Wi-Fi socket is only the L4 diagnostic.
Hybrid key establishment is **not** PQ authentication (classical certificate), and reports
must say so. Educational Wi-Fi link; not flight-approved, space-qualified or NASA-endorsed.

## What this repo is now

The earlier custom stack (ML-KEM shim, Ascon, `kem.h`, custom handshake) was **archived**: it
is not in `main`, but is kept at tag `custom-stack-v1` and branch `archive/custom-stack`. Do
not re-add it, and do not add Ascon or a custom protocol to the mandatory system. Bringing it
back as a research extension needs a defined research question, an independent spec and the
advisor's approval.

## Gates and evidence

- Follow gates **L1–L10 in order** (README table). If a physical result is missing, do not
  advance; change one variable at a time and keep the failed output.
- Each gate: a dated PASS/FAIL log, exact code revision and board IDs, and a photo, trace or
  recording in `evidence/`. Instructor initials mark it DONE.
- **Never invent, estimate or hand-edit measurements.** Do not claim a gate passed, or a
  test T01–T10 passed, without the evidence file. Do not report numbers from Debug builds or
  single runs.
- No purchasing, PCB or connector orders, and no optional work, without advisor approval.
- Electrical safety: 3.3 V pins only, no load from a GPIO or Pi header, wire with power off.

## Language

- **C17 for all project code** on the Pis, `-Wall -Wextra -Werror -Wmissing-prototypes`.
  Every non-`static` function is declared in a header first.
- **The Pi services are written in C17** (team decision, 2026-10-10; confirm with the advisor,
  Q11). **Open:** Pico firmware language (C SDK or MicroPython); do not start it until decided.

## Dependencies

- **OpenSSL 3.5+** on every node doing cryptography, pinned to an exact version recorded in
  `evidence/bootstrap.log` and every test log. Verify with
  `openssl list -tls1_3 -tls-groups | grep -i X25519MLKEM768`.
- No git submodules. If third-party code is ever added: `third_party/`, pinned commit, never
  modified, version noted in `docs/results.md`.

## Build (existing C code)

Requires CMake 3.28+ and a C17 compiler. No internet is needed to configure.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Benchmarks use `Release` only. API docs: `cmake --build build --target docs` (optional;
  needs Doxygen).

## Repo layout

Files marked *(planned)* do not exist yet.

```
pqc-drone-link/
├── README.md                  # What the project is, gates, how to build
├── CONTRIBUTING.md            # Rules every change must follow
├── CMakeLists.txt             # Build system for the existing C code
├── src/                       # Code kept from the earlier work
│   ├── common/errors.h        # Negative error codes returned by all functions
│   ├── crypto/                # rng.h/.c: rng_bytes(), secure_wipe(), ct_memcmp()
│   └── metrics/               # timer.h/.c, logger.h/.c
├── pico/                      # (planned) Pico A sampler/UART sender, Pico B display
├── pi/                        # (planned) Pi A client + queue, Pi B server + dedup store
├── hardware/                  # (planned) schematic, pin_map.csv, pcb/, photo
├── evidence/                  # (planned) per-gate logs, captures, photos, final video
├── tests/                     # test_rng.c, test_metrics.c;
│                              # (planned) positive_and_attack_tests.csv, fault_injection.csv
├── experiments/configs/       # (planned) one file per experiment
├── scripts/                   # wiki_lint.py; (planned) run_demo.sh, netem_setup.sh, power_logger.py
├── expected_results.md        # (planned)
├── analysis/  results/        # (planned)
└── docs/
    ├── research/              # Research wiki (schema: docs/research/README.md)
    ├── scope.md               # Authoritative scope (advisor decision, 2026-10-10)
    ├── advisor_questions.md   # Questions sent to the advisor, with answers
    ├── guide_comparison.md    # Research Guide vs Build Guide
    ├── block_diagram.md       # System block diagram and wiring
    ├── telemetry_format.md    # Architecture and protocol: records, ACKs, dedup, queue, trust
    ├── responsibility_matrix.md  # Owner and backup per task
    ├── schedule.md            # L1-L10 mapped to the 28-week gates
    ├── bom.md                 # Parts, lab equipment, pending purchases
    ├── design_notes.md        # Why the code looks the way it does; status and progress log
    ├── threat_model.md        # (planned)
    └── results.md             # (planned) our own measurements
```

## Crypto rules (non-negotiable)

Code comments cite these by number, so keep the numbering. Rules 1–9 are the original rules;
those marked *archived* belonged to the custom stack and stay only so old comments and history
make sense. Rules 10–17 apply to the mandatory system.

### Original rules (1-9)

1. *(archived)* Never implement a cryptographic primitive. (Still true: we use OpenSSL; rule 10.)
2. *(archived)* All key exchanges go through `kem.h`.
3. **All randomness in our own code comes from `rng.c`** (OS CSPRNG, `getrandom()`). Never use
   `rand()` or any non-cryptographic source. TLS keys and nonces come from OpenSSL.
4. *(archived)* Never reuse a nonce with the same key; nonces built in `session.c`. (TLS 1.3
   handles nonces now.)
5. **Wipe secrets after use.** Call `secure_wipe()` on every secret buffer in our own code on
   every exit path, including errors. Plain `memset` can be optimized away.
6. **Constant-time comparisons only** for tags, MACs, and secrets (`ct_memcmp()`), never `memcmp`.
7. **Check every return code** from crypto functions, OpenSSL included. A failed verification or
   decrypt drops the connection or packet and increments a counter; it never falls back to
   plaintext or a weaker mode.
8. *(archived)* No downgrade of the custom handshake mode. (Replaced by rule 12.)
9. *(archived)* Session keys derived in `kdf.c` with domain-separation labels. (TLS 1.3's key
   schedule does this now.)

### Rules for the mandatory system (TLS)

10. **TLS only.** All Pi A ↔ Pi B traffic goes through an OpenSSL TLS 1.3 connection. No
    custom ML-KEM handshake, no second application-layer security protocol, no Ascon on TLS.
11. **Pin the key-exchange group.** Config B accepts only `X25519MLKEM768`; config A only
    `X25519`. Set the groups explicitly on both client and server, with TLS 1.3 as both
    minimum and maximum version.
12. **No downgrade and no fallback.** A hybrid-only peer refuses a classical-only offer (T04);
    config A is never used when configured for hybrid. Never retry a failed handshake with a
    weaker group or without verification.
13. **Always verify the peer.** The client verifies the chain against the pinned demo CA and
    checks the hostname; failure is fatal (T03). Never set verify-none outside a clearly named
    test that expects the failure.
14. **Same everything but the group.** A and B share application, hardware, network
    conditions, certificate policy and cipher suite.
15. **ACK only after a durable commit.** Pi A writes and flushes a record before sending; Pi B
    stores `node`+`seq` and the record durably before ACKing; Pi A deletes only after the ACK.
16. **Exactly-once effects.** The OLED unique-record count changes only on a new
    `node`+`seq` insert; a duplicate is ACKed again with no other effect (T05).
17. **Keys and secrets.** Private keys and the demo CA key never go in git; test certificates
    come from a script and are short-lived. Wipe secrets in our own code; never log keys or
    session secrets.

## Protocol rules

- `docs/telemetry_format.md` is the source of truth for the UART frame, record, ACK, dedup,
  queue and trust configuration. Update it **before** code that depends on it, same commit.
- `seq` is 64-bit, per node, starts at 1, never reset or reused. Every wire/file value has a
  documented size, byte order and unit. Link and queue sizes are config values.

## Memory and timing rules

- Every buffer copy uses an explicit length checked against the destination size (C and
  Pico firmware).
- Never log, print, or write files **inside** a timed region; record timestamps in memory,
  then write after the timed region ends.

## Measurement and logging

- Keep the **TLS handshake benchmark (T10)** and **application telemetry** measurements
  separate, in separate files. Never mix them.
- Handshake benchmark: 100+ *successful* handshakes per config; median and p95 latency, bytes
  on the wire, CPU and memory (method written down). Failed attempts go in their own file.
- Application runs record queue capacity, sample rate, duration and accepted-record count,
  and report overflow or rejection as numbers.
- Monotonic timestamps. Power is read externally and matched to runs by run ID and
  timestamp; the instrument is still open (Q5).
- Loss tests at 0, 10 and 30% emulated; record the method (`tc netem` or the AP; Q10).
- `logger.c` CSV columns: `run_id, mode, timestamp_ns, endpoint, event,
  bytes, detail`. `logger_event()` is safe in timed regions; `logger_flush()` is not. Kept for Pi-side
  instrumentation; output must match the required evidence CSVs (Q15).

## Experiments and reproducibility

- Every experiment is a config file in `experiments/configs/` (config A/B, loss, repetitions,
  queue capacity, sample rate, duration, seed). Never change parameters by editing code.
- Runs record git commit hash, OpenSSL version, configuration hashes, board IDs and date.
- Another student must be able to run the demo from a clean environment with no undocumented
  steps; a teammate who did not write the part audits it.

## Testing

Every change keeps `ctest` passing for the code that exists. Required for the mandatory system:

- T01–T10 from the Build Guide in `tests/positive_and_attack_tests.csv` (and
  `tests/fault_injection.csv`).
- Restart of Pi A and Pi B with records queued: nothing lost, nothing counted twice.
- Truncated, oversized, out-of-range, replayed and out-of-order frames on the UART and the
  application protocol are rejected without a crash.
- Wrong certificate, wrong hostname and classical-only peer all fail closed.

New features need an adversarial test. Tests are plain C programs with a `CHECK(cond)` macro
that records a failure and keeps going; one file per module, `tests/test_<module>.c`,
registered in `CMakeLists.txt`.

## Research wiki

Outside knowledge goes in `docs/research/` (schema in its README.md).

- Cite sources by ID, linking into `sources.md`. Never commit source PDFs or extracted text.
- Every number carries unit, conditions, source, and whether it is spec, reported, or ours.
- Our own measurements go in `docs/results.md`, never in the wiki.
- Open decisions are argued on their page in `docs/research/decisions/`. When decided, update
  the page, tick it in README.md, and add any resulting rule here and in CONTRIBUTING.md.
- Standard markdown links, not `[[wikilinks]]`; run `python3 scripts/wiki_lint.py` after edits.

## Code style

- `snake_case`; module prefix on public functions. C functions return `int`: `0` on success,
  negative codes from `src/common/errors.h`; the one exception is `secure_wipe()` (`void`).
- Error code values are fixed once written; add new codes in the module's range of 10.
- `secure_wipe()` and `ct_memcmp()` live in `src/crypto/rng.h`. Includes relative to `src/`.
- Keep functions short; state machines are explicit. Comment **why**.
- `clang-format -i` (repo `.clang-format`) on changed C/C++ files; every file ends with a newline.

### Documentation (Doxygen)

`third_party/` is never documented or scanned.

- Every source and header starts with a `/** @file <name> @brief ... */` block.
- Every public function, struct, enum, typedef and macro in a header has `/** ... */` with
  `@brief`; functions document each parameter (`@param[in]`, `@param[out]`, `@param[in,out]`)
  and `@return` listing the `errors.h` codes. Struct fields and enum values use `/**< ... */`.
- API docs live in the **header**; `.c` files explain *why* with plain `/* */` comments;
  static helpers get a short `/** @brief */`.
- Security-relevant contracts get `@warning` or `@note`. Use `@` commands, not `\`.
- New code adds no Doxygen warnings.

## Claims

Hybrid key establishment is not PQ authentication. Do not write or help write that the
system is flight-approved, space-qualified, NASA-endorsed, CCSDS BPv7 DTN, secure boot, or
end-to-end post-quantum authenticated, or that a gate or test passed without evidence.

## Open decisions (update as the team decides)

Decided by the advisor 2026-10-10: Wi-Fi AP link; TLS 1.3 with no custom handshake and no
Ascon; hybrid `X25519MLKEM768` is config B; pinned classical certificate, no ML-DSA; Pi 4B +
Pico 2 platform.

- [ ] Pico firmware language (C SDK or MicroPython). Pi services are decided: C17 (team,
      2026-10-10; confirm with the advisor, Q11)
- [ ] Current/power instrument: DMM only or INA219/INA260; per-handshake energy needed? (Q5)
- [ ] Loss emulation: `tc netem` on the Pis or at the AP (Q10)
- [ ] Persistent store on each Pi (SQLite or append-only file with fsync)
- [ ] Queue capacity, overflow policy, test durations (proposed in `docs/telemetry_format.md`)
- [ ] TLS cipher suite shared by A and B
- [ ] Confirm: no rekey, no command channel, one-way server authentication (Q6, Q8)
- [ ] Reuse the existing timer and logger for evidence CSVs? (Q15)

Decision pages: `docs/research/decisions/`. Earlier questions Q1–Q18:
`docs/research/open-questions.md` (many now moot; kept for the research extension).

## Plan

The Build Guide's 28-week gates G0–G8, mapped to L1–L10 in `docs/schedule.md`, replace the
old four-week plan. Hardware first: no crypto work until L1–L4 are signed.

## Notes for Claude

- Prefer small, reviewable changes. Explain security-relevant changes in the commit message.
- If a request would violate a rule above, stop and flag it instead of working around it.
- When unsure about protocol behavior, check `docs/telemetry_format.md` first; if it doesn't
  answer the question, ask rather than guess. Mark guesses as *(proposed)*.
- Do not report or summarize performance numbers from Debug builds or single runs.
- Do not restore the archived custom stack, or start Ascon or FPGA work, without the advisor's approval.
- This file is personal (`.claude/` is gitignored). The team's copy of the rules is
  `CONTRIBUTING.md`, and the layout, build steps, plan and open decisions are in `README.md`.
  When a rule, layout entry or decision changes here, make the same change there, and keep
  crypto rule numbers identical (code comments cite them).
- Research wiki: `docs/research/`. Source PDFs and extracted text live on the Windows side at
  `/mnt/c/Users/balta/OneDrive/PQC_SOURCES/` (never in git). When answering a question about
  standards, papers, radios or MAVLink, read `docs/research/index.md` first and cite wiki
  pages; say when the wiki doesn't cover it. The Build Guide and Research Guide are not in
  the wiki yet. Use `/ingest` to add a source and `/wiki-lint` to audit.
- `docs/design_notes.md` records *why* the code is designed the way it is, plus status and
  follow-ups. Read it before changing a finished module; update it when a module is finished.

## Before committing

1. `clang-format -i` on changed C files; every file ends with a newline.
2. `cmake --build build` with no warnings, and `ctest --test-dir build` all passing.
3. `cmake --build build --target docs` adds no new Doxygen warnings (if Doxygen is installed).
4. New files are listed in `CMakeLists.txt` (code) and in the repo layout above.
5. If a module was finished, `docs/design_notes.md` is updated.
6. If the research wiki changed, `python3 scripts/wiki_lint.py` reports no issues.
7. No keys, private certificates, source PDFs or invented numbers are staged.
