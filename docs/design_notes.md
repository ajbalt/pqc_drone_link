# Design Notes

A running record of what has been built, the design choices behind it, and
the reasoning a new team member needs to understand the code. Add a section
each time a module is finished.

The rules themselves live in `CONTRIBUTING.md`; message formats live in
`docs/telemetry_format.md`. This file explains *why* the code
looks the way it does.

## Status

> **Scope changed (2026-10-10).** The advisor confirmed the Build Guide governs:
> OpenSSL TLS 1.3 with `X25519MLKEM768`, compared with classical X25519 TLS. The custom
> ML-KEM + Ascon work was **archived** the same day: it is preserved at tag
> `custom-stack-v1` and branch `archive/custom-stack`, and removed from `main`. Steps 0-2
> (errors, timer and logger, RNG helpers) remain in use. See [scope.md](scope.md) and
> [guide_comparison.md](guide_comparison.md).

| Step | Module                         | Files                                   | Tested by           | State        |
|------|--------------------------------|-----------------------------------------|---------------------|--------------|
| 0    | Error codes                    | `src/common/errors.h`                   | (used by all tests) | Done         |
| 1    | Timing and event logging       | `src/metrics/timer.h/.c`, `logger.h/.c` | `tests/test_metrics.c` | Done      |
| 2    | Randomness and secret handling | `src/crypto/rng.h`, `rng.c`             | `tests/test_rng.c`  | Done         |
| 3-6  | Key exchange interface, packet formats, UDP transport, baseline apps | (removed from `main`) | — | Archived at tag `custom-stack-v1` |

Pi services, Pico firmware and the evidence folder are not started; they follow the gates
in [schedule.md](schedule.md).

### Files added beyond the original layout

| File                    | Why it exists                                               |
|-------------------------|-------------------------------------------------------------|
| `src/crypto/rng.h`      | Lets other files call `rng_bytes`, `secure_wipe`, `ct_memcmp` |
| `src/metrics/timer.h`   | Public interface to `timer.c`                               |
| `src/metrics/logger.h`  | Public interface to `logger.c`                              |
| `tests/test_rng.c`      | rng.c tests                                                 |
| `tests/test_metrics.c`  | timer.c and logger.c tests                                  |

These are listed in the repo layout in README.md.

---

## Conventions used everywhere

- **Return codes.** Every function returns `int`: `0` (`PQC_OK`) on success,
  a negative code from `errors.h` on failure. The only exception is
  `secure_wipe()`, which returns `void` (see below).
- **Warnings.** Project C code builds with
  `-Wall -Wextra -Werror -Wmissing-prototypes`. The last one requires every
  non-`static` function to be declared in a header first, so a typo between
  a header and its `.c` file (e.g. `timer_cput_ns` vs `timer_cpu_ns`) fails
  at compile time on the exact line, instead of as a confusing
  "undefined reference" from the linker. Helpers that aren't in a header
  must be `static`.
- **Includes** are relative to `src/`: `#include "crypto/rng.h"`,
  `#include "common/errors.h"`.
- **Doxygen.** API docs go in the header; `.c` files explain *why* with
  plain `/* */` comments. Static helpers and static arrays get a short
  `/** @brief */` so Doxygen doesn't warn about them.
- **Formatting.** Run `clang-format -i` on every file before committing.
  Files must end with a newline (turn on "Ensure every saved file ends with a
  line break" in the editor).
- **Tests** are plain C programs registered with `add_test()`. Each uses a
  `CHECK(cond)` macro that records a failure and keeps going, so one run
  shows every failure. A test returns non-zero if any check failed.
- **Adversarial tests.** Each test covers bad input as well as the happy
  path: NULL pointers, wrong lengths, unknown values, full buffers.

---

## errors.h: error codes

**What it is.** One `enum pqc_err` with every error code in the project.

**Design choices**

- **Grouped in ranges of 10 by module** (`-10..-19` randomness,
  `-20..-29` key exchange, and so on), so a number in a CSV log tells you
  where it came from without looking it up.
- **Values are fixed; never renumber.** Codes get written into experiment
  logs, so changing one would break comparison with older runs. Add new
  codes in the right range instead.
- **`PQC_` prefix** avoids clashing with crypto libraries (OpenSSL defines
  `ERR_*` macros) if one is picked for X25519.
- **`PQC_ERR_AEAD_AUTH` is one code for every decrypt failure.** Different
  codes for "bad tag" vs "bad length" would tell an attacker which part of a
  forged packet was wrong.
- **`PQC_ERR_HS_MODE_MISMATCH` is always fatal.** That is how the no-downgrade
  rule (crypto rule 8) shows up in the code.

**Lesson learned.** An unterminated `/*` comment silently swallowed
`PQC_ERR_REPLAY` and `PQC_ERR_IO`, and the build still passed because no
file used them yet. The build passing does not prove a header is correct;
a test that uses every value does.

---

## kem.h and the key-exchange layer (archived)

Removed from `main` on 2026-10-10 with the rest of the custom ML-KEM + Ascon stack. The
code and the full design notes for it are at tag `custom-stack-v1`
(`git show custom-stack-v1:docs/design_notes.md`).

---

## rng.c: randomness, wiping, constant-time compare

**What it is.** The three primitives every crypto file needs:
`rng_bytes()`, `secure_wipe()` and `ct_memcmp()`. They share one file
because every crypto file needs all three; only `rng_bytes` is about
randomness.

**`rng_bytes()`**

- All randomness in the project comes from here (crypto rule 3), via the OS
  CSPRNG `getrandom()`.
- **It loops.** Linux only promises to fill up to 256 bytes per call; a
  larger request can come back short or be interrupted by a signal
  (`EINTR`). ML-KEM-1024 secret keys are 3168 bytes. The test requests 4096
  bytes to exercise the loop.
- **On failure it zeroes the buffer** rather than leaving it partly random,
  so a caller that ignores the error gets an obviously bad key. It never
  falls back to a weaker source.

**`secure_wipe()`**

- A plain `memset()` on a buffer that's about to go out of scope is a "dead
  store": the compiler sees nothing reads the zeros and may delete it,
  leaving the secret in memory. Writes through a `volatile` pointer must be
  performed, so they can't be removed.
- Portable C, so it also works on microcontrollers that lack
  `explicit_bzero()` or `memset_s()`.
- **Returns `void`**, unlike every other function: it can't fail, and it is
  called on error paths where a return code would have nowhere to go.

**`ct_memcmp()`**

- `memcmp()` stops at the first differing byte. Timing it tells an attacker
  how many leading bytes of a forged tag were right, so they can guess a
  tag one byte at a time. `ct_memcmp()` always reads every byte and ORs the
  differences together; `volatile` stops the compiler adding its own early
  exit.
- Returns only equal (0) or not equal (1). There is no ordering.
- **Fails closed:** a NULL pointer returns "not equal", so a tag check
  rejects the packet.
- `memcmp()` is still fine in test code, where timing doesn't matter.

---

## timer.c and logger.c: measurement

**timer.c**

- `timer_now_ns()` reads **`CLOCK_MONOTONIC`**. The normal clock jumps when
  NTP corrects it or someone changes the date, which could make a handshake
  appear to take negative time. The monotonic clock only moves forward.
- It is the same clock as Python's `time.monotonic_ns()` on Linux, so
  `scripts/power_logger.py` must use `time.monotonic_ns()` for its
  timestamps to line up with ours on the same machine.
- Timestamps are only comparable between readings on the **same machine**;
  drone and ground clocks are unrelated.
- `timer_cpu_ns()` reads process CPU time. CPU time divided by wall time
  over the same interval gives the required CPU-utilization metric.

**logger.c**

CSV columns, as required by CONTRIBUTING.md:
`run_id, mode, timestamp_ns, endpoint, event, bytes, detail`.

- **Two halves.** `logger_event()` only writes a record into an array in
  memory (no I/O, no allocation), so it's safe inside timed sections and on
  the packet path. `logger_flush()` does the slow file writing and is called
  afterwards. This is how the "never log inside a timed region" rule is met.
- **The caller provides the storage**, e.g.
  `static struct log_record records[10000];`, so there is no `malloc`
  anywhere. This also works on a microcontroller.
- **A full buffer drops events instead of flushing**, because flushing would
  do file I/O inside a timed section. Each drop is counted and written as a
  `log_dropped` row (detail = number lost), so analysis can tell the run's
  log is incomplete.
- **`detail` is a number, not text.** It holds an error code, sequence
  number, fragment index or epoch depending on the event (documented on
  each value of `enum log_event`). Numbers are fast to record and never need
  CSV escaping.
- **Never overwrites a file.** `logger_init()` opens with `fopen(path, "wx")`
  (C11 exclusive mode) and fails if the file exists, so a rerun can't
  silently replace earlier results.
- **Config strings are validated, not escaped.** A comma or newline in a
  `run_id` would shift every column after it and silently corrupt the
  analysis. They are rejected with `PQC_ERR_BAD_CONFIG` *before* the file is
  created, so bad config never leaves an empty log behind.
- **Event names are tied to the enum** with designated initializers
  (`[LOG_EV_PKT_TX] = "pkt_tx"`), and a `_Static_assert` fails the build if
  an event is added without a name.
- **`(unsigned)` casts** on enum range checks turn "negative or too big"
  into one comparison, and avoid a compiler warning about checking an enum
  for `< 0`.
- **Not thread-safe:** use one logger per thread.

---

## Debugging lessons

Mistakes we've hit so far, and how to recognise them next time.

| Symptom | Cause | How it's caught now |
|---------|-------|---------------------|
| Build passes, but an error code "doesn't exist" when first used | An unterminated `/*` comment in `errors.h` swallowed the next enum line | Tests that use every value; read compiler output for "undeclared" |
| Linker: `undefined reference to 'timer_cpu_ns'` | Typo between header and `.c` file (`timer_cput_ns`); each file compiled fine on its own | `-Wmissing-prototypes` now fails the build on the exact line |
| Doxygen describes a struct with the enum's text | Comment block copied and not edited | Read the generated docs (`cmake --build build --target docs`) |
| git diff shows "No newline at end of file" | Editor not adding a final newline | Editor setting + `clang-format -i` before committing |

**Reading an "undefined reference" error:** the compiler only checks that
a function is *declared*; the linker checks that it is *defined*
somewhere in the build. So "undefined reference" means either the
definition's name doesn't match the declaration, or the `.c` file holding
it isn't listed in `CMakeLists.txt`.

---

## Follow-ups that later modules must honour

These were promised by code written so far; whoever writes the module must
implement them.

- [ ] `scripts/power_logger.py` timestamps with `time.monotonic_ns()`.
- [ ] Pi services call `logger_flush()` only outside timed regions.
- [ ] Apps size the log buffer for a whole run (telemetry rate × run
      length × events per packet), and `analysis/parse_logs.py` flags any
      run that contains a `log_dropped` row.
- [x] Add the extra files above to the repo layout (README.md).

## Open decisions still pending

See "Open decisions" in README.md and [advisor_questions.md](advisor_questions.md).

---

## Progress log

| Date       | Work |
|------------|------|
| 2026-09-30 | Build skeleton, CMake, submodules, clang-format, Doxygen target; first draft of `errors.h` |
| 2026-10-01 | Doxygen working |
| 2026-10-07 | Fixed `errors.h` (two swallowed codes, truncated comments); `kem.h` / `kem.c` / `kem_none.c` + `test_kem`; `rng.c` + `test_rng`; first `ctest` tests |
| 2026-10-08 | `timer.c` and `logger.c` + `test_metrics`; added `-Wmissing-prototypes`; started this file. `ctest`: 3/3 passing |
| 2026-10-08 | Team docs: `README.md` + `CONTRIBUTING.md`. Research wiki in `docs/research/` (adapted from the maintainer's LLM wiki) with `scripts/wiki_lint.py`; first decision page: radio link |
| 2026-10-10 | Read the Build Guide (LINK) and Research Guide p.8; found a conflict between them. Sent questions to the advisor. Added `docs/advisor_questions.md` and `docs/guide_comparison.md`; paused protocol work pending answers |
| 2026-10-10 | Advisor replied: Build Guide governs, TLS replaces the custom stack, Ascon is an optional later extension. Added `docs/scope.md`; recorded answers in `docs/advisor_questions.md` |
| 2026-10-10 | Rewrote README, CONTRIBUTING and CLAUDE.md for the Build Guide scope (rules 1-9 kept for the frozen stack, new TLS rules 10-17). Drafted the five review deliverables: `block_diagram.md`, `telemetry_format.md`, `responsibility_matrix.md`, `schedule.md`, `bom.md`. Closed four wiki decision pages. No code changed |
| 2026-10-10 | Archived the custom stack: tag `custom-stack-v1` and branch `archive/custom-stack`; removed `kem*`, the ML-KEM shim, the empty protocol, transport and app stubs, `test_kem` and the two submodules from `main`. `CMakeLists.txt` now builds C17 only (rng, timer, logger). `errors.h`, `rng`, `timer` and `logger` kept. Pi services will be C17. `ctest` 2/2 passing |
| 2026-10-11 | Built OpenSSL 3.5.9 on a dev machine; checked T02/T03/T04 behaviour and handshake sizes; added `scripts/make_demo_certs.sh`, `tls_group_matrix.sh`, `tls_handshake_bytes.sh`, `proxy_count.py`, `docs/results.md`, and wiki pages on certificates and time and TCP under loss. Dev-machine results only, not gate evidence |
| 2026-10-11 | Added key, certificate and video patterns to `.gitignore` (rule 17); created `evidence/README.md` and `evidence/GATE_RECORD_TEMPLATE.md`; ingested the Pi 4, Pico 2, MPU-6050 datasheets, MPU register map, Pico 2 pinout, RP2350 and SSD1306 datasheets (wiki pages; subagents per document for the first four) |
| 2026-10-11 | Added stub folders with READMEs: `hardware/` (with a draft `pin_map.csv` of all 14 connections, unverified on the bench), `pico/`, `pi/`, `experiments/configs/`; header-only `tests/positive_and_attack_tests.csv` and `tests/fault_injection.csv`; ingested the SSD1306 datasheet |
