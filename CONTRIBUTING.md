# Contributing

The rules every change to this repo must follow. Read this fully before writing code.
For what the project is and how to build it, see [README.md](README.md). For *why* the
existing code is designed the way it is, see [docs/design_notes.md](docs/design_notes.md).

If a change would break one of these rules, stop and raise it with the team instead of
working around it.

## Language and compiler flags

- **C17 for all project code.**
- **One exception:** `src/crypto/mlkem_shim.cpp` is C++20, because the ML-KEM library is
  a C++20 header-only library. It exists only to expose ML-KEM through a plain C header
  (`mlkem_shim.h`). No other C++ files may be added.
- The shim only converts C pointers to the library's fixed-size spans and calls
  keygen/encapsulate/decapsulate. No protocol logic, no logging, no allocation, no
  exceptions. It is compiled with `-fno-exceptions -fno-rtti`.
- Project C code is compiled with `-Wall -Wextra -Werror -Wmissing-prototypes`.
  Every non-`static` function must be declared in a header first; helpers that aren't
  in a header must be `static`. This turns a header/`.c` name mismatch into a compile
  error on the exact line instead of a linker "undefined reference".

## Dependencies

All crypto libraries are **git submodules** under `third_party/`, pinned to exact
commits.

| Submodule             | Repo                                 | How it's built |
|-----------------------|--------------------------------------|----------------|
| `third_party/ml-kem`  | https://github.com/itzmeanjan/ml-kem | `add_subdirectory()`; linked via the `ml-kem` target |
| `third_party/ascon-c` | https://github.com/ascon/ascon-c     | Its CMakeLists is **not** used (it is a test harness). We compile one implementation folder ourselves into the `ascon` target |

- **Never modify files inside `third_party/`.** Wrap libraries in `src/crypto/` instead.
- **Do not manage ml-kem's dependencies.** ml-kem's own CMakeLists fetches `sha3`,
  `randomshake`, and `subtle` at configure time. Do not vendor, replace, or re-pin them.
- **Never `add_subdirectory()` ascon-c.**
- We use ascon-c's combined folder `crypto_aead_hash/asconaeadxof128/<impl>`, which
  provides both Ascon-AEAD128 and Ascon-XOF128. Do not also compile the separate AEAD or
  XOF folders (risk of duplicate symbols).
- The Ascon implementation is selected with the CMake cache variable `ASCON_IMPL`
  (default `ref`). Every experiment log records which implementation was used.
- **Updating a library is a deliberate change:** bump the submodule in its own commit,
  rerun all known-answer tests, and note the change in `docs/results.md`, since it can
  shift benchmark numbers. Updating ml-kem also updates its fetched dependencies.

## Crypto rules (non-negotiable)

Code comments refer to these by number ("crypto rule 7"), so keep the numbering.

1. **Never implement a cryptographic primitive.** ML-KEM and Ascon come from
   `third_party/` only. Our code only wires them together.
2. **All key exchanges go through `kem.h`.** Application and protocol code must never
   call a library or the shim directly, or branch on which KEM is in use. Mode is
   selected by config at runtime. `kem_mlkem.c` is the only file that includes
   `mlkem_shim.h`, and `aead_ascon.c` / `kdf.c` are the only files that call ascon-c.
3. **All randomness comes from `rng.c`**, which uses the OS CSPRNG (`getrandom()`).
   ML-KEM seeds (`d`, `z`, `m`) are generated there and passed into the shim. Never use
   `rand()` or any non-cryptographic source.
4. **Never reuse a nonce with the same key.** Nonces are built in `session.c` only:
   key epoch + direction bit (drone→ground / ground→drone) + 64-bit packet counter.
   The counter never resets without a rekey. If the counter would overflow, force a
   rekey.
5. **Wipe secrets after use.** Call `secure_wipe()` on every secret key, seed, shared
   secret, and session key on every exit path, including errors. Plain `memset` can be
   optimized away.
6. **Constant-time comparisons only** for tags, MACs, and secrets (`ct_memcmp()`), never
   `memcmp`.
7. **Check every return code** from crypto functions, including ML-KEM encapsulation,
   which can fail on a malformed public key. A failed decrypt drops the packet and
   increments a counter; it never falls back to plaintext or a weaker mode.
8. **No downgrade.** A session configured for `mlkem768` must never negotiate anything
   else.
9. Session keys are derived in `kdf.c` only, with distinct domain-separation labels per
   key and direction. Never use a KEM shared secret directly as an Ascon key.

## Protocol rules

- `docs/protocol_spec.md` is the source of truth for packet formats and the handshake.
  Update the spec **before** changing handshake or packet code, in the same commit.
- Link MTU is a config value (`link_mtu`), never hard-coded. ML-KEM keys and ciphertexts
  are larger than one radio packet, so `fragment.c` must handle loss, duplication, and
  reordering of fragments, with a bounded retransmit timeout.
- Every encrypted packet header (packet type, sequence number, epoch) is passed to Ascon
  as associated data, so it is authenticated even though it is not encrypted.
- The receiver maintains a replay window and rejects old or duplicate sequence numbers.

## Memory and timing rules

- No `malloc`/`free` in the handshake or packet path. Use fixed-size buffers sized from
  the largest supported parameter set (ML-KEM-1024).
- Every buffer copy uses an explicit length that is checked against the destination
  size.
- Never log, print, or write files **inside** a timed region. Record timestamps into
  memory, then log after the timed region ends.

## Measurement and logging

- All events are logged as CSV by `logger.c`, one row per event, with these columns:
  `run_id, mode, timestamp_ns, endpoint, event, bytes, detail`
- Events: `handshake_start`, `handshake_done`, `handshake_fail`, `frag_tx`, `frag_rx`,
  `frag_retx`, `pkt_tx`, `pkt_rx`, `pkt_drop`, `decrypt_fail`, `rekey_start`,
  `rekey_done`, and `log_dropped` (written by the logger itself when its buffer was
  full; analysis must treat that run's log as incomplete).
- `detail` is a number whose meaning depends on the event (error code, sequence number,
  fragment index, or epoch), documented on each value of `enum log_event` in `logger.h`.
- `logger_event()` only records into a caller-provided array (no I/O, no allocation) and
  is safe inside timed regions; `logger_flush()` writes the file and is only called
  outside them. The logger never overwrites an existing log file.
- Byte counts include **all** on-air overhead: headers, fragments, retransmissions, AEAD
  tags.
- Timestamps come from `CLOCK_MONOTONIC` (`timer_now_ns()`). Power is measured
  externally by `scripts/power_logger.py`, which must use Python's `time.monotonic_ns()`
  so its samples line up with our logs on the same machine; runs are matched by
  `run_id` and timestamp.
- Required metrics: handshake latency, bytes per session, CPU utilization, energy per
  handshake, rekey time, and all of these as a function of packet loss.

## Experiments and reproducibility

- Every experiment is a config file in `experiments/configs/` (mode, loss rate, delay,
  link rate, MTU, rekey interval, repetitions, random seed). Never change parameters by
  editing code.
- Run with `experiments/run_experiment.sh <config>`. It records the git commit hash,
  **submodule commit hashes** (`git submodule status`), `ASCON_IMPL`, and hardware info
  alongside the logs.
- Report medians and spread over many repetitions (default 30+), never single runs, and
  never numbers from Debug builds.
- Packet loss for development is simulated with `scripts/netem_setup.sh` (Linux
  `tc netem`).

## Testing

Every change must keep `ctest` passing. Required coverage:

- Known-answer tests on each target platform: ML-KEM against the vectors in
  `third_party/ml-kem/kats/`, and Ascon against the `LWC_*_KAT_*.txt` files in the
  ascon-c algorithm folder. These run through our wrappers, not just the libraries' own
  tests.
- Handshake completes for every mode and parameter set.
- Fragment reassembly under loss, duplication, and reordering.
- Replayed, reordered, and tampered packets are rejected.
- Nonces never repeat, including across rekeys and counter boundaries.
- Rekey mid-stream with no lost or duplicated application data.

New features need an **adversarial test** (what happens when an attacker sends it wrong,
twice, late, or truncated), not just a happy-path test.

Conventions:

- One file per module, `tests/test_<module>.c`, registered in `CMakeLists.txt` with
  `add_executable` + `add_test`. `test_kem_vectors.c` is reserved for the official KATs.
- Tests are plain C programs with a `CHECK(cond)` macro that records a failure and keeps
  going, so one run reports every failure; `main` returns non-zero if any check failed.
- Tests that cover every KEM loop over all mode numbers and skip modes not yet built, so
  new modes are tested as soon as they are added to the table in `kem.c`.

## Code style

- `snake_case` for functions and variables; module prefix on public functions
  (`session_encrypt`, `frag_reassemble`).
- Functions return `int`: `0` on success, negative error codes from
  `src/common/errors.h`. The one deliberate exception is `secure_wipe()`, which returns
  `void`: it cannot fail and is called on error paths where a code would have nowhere
  to go.
- Error code values are fixed once written (they appear in logs). Add new codes in the
  module's range of 10; never renumber.
- `secure_wipe()` and `ct_memcmp()` live in `src/crypto/rng.h` with `rng_bytes()`.
- Includes are relative to `src/`: `#include "crypto/kem.h"`.
- Keep functions short; the handshake is an explicit state machine, not nested
  conditionals.
- Comment **why**, especially for any security-relevant decision.
- Format with the repo's `.clang-format` (`clang-format -i <files>`), and make sure every
  file ends with a newline.

### Documentation (Doxygen)

`third_party/` is never documented or scanned.

- Every source and header file starts with a `/** @file <name> @brief ... */` block.
  Without it, Doxygen ignores everything in the file.
- Every public function, struct, enum, typedef, and macro in a header has a
  `/** ... */` comment with `@brief`. Functions also document each parameter with
  direction (`@param[in]`, `@param[out]`, `@param[in,out]`) and `@return`, listing which
  `errors.h` codes can be returned.
- Struct fields and enum values use trailing `/**< ... */` comments.
- API docs live in the **header**, not repeated in the `.c` file. Comments in `.c` files
  explain *why* with plain `/* */` comments; static helpers and static arrays get a
  short `/** @brief */`.
- Note security-relevant contracts explicitly with `@warning` or `@note` (e.g. "caller
  must `secure_wipe()` the output").
- Use `@` commands (`@brief`, `@param`), not `\` commands.

Example:

```c
/**
 * @brief Fill a buffer with cryptographically secure random bytes.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Number of bytes to write.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p buf is NULL,
 *         PQC_ERR_RNG_FAIL if getrandom() fails.
 */
int rng_bytes(uint8_t *buf, size_t len);
```

## Commits

- Prefer small, reviewable commits. Explain security-relevant changes in the commit
  message.
- When a module is finished, update [docs/design_notes.md](docs/design_notes.md): its
  status row, a section on the design choices, and the progress log.

### Before committing

1. `clang-format -i` on changed C/C++ files; every file ends with a newline.
2. `cmake --build build` with no warnings, and `ctest --test-dir build` all passing.
3. `cmake --build build --target docs` adds no new Doxygen warnings (if Doxygen is
   installed).
4. New files are listed in `CMakeLists.txt` and in the repo layout in README.md.
5. If a module was finished, `docs/design_notes.md` is updated.
