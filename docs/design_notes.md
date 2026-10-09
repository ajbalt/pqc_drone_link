# Design Notes

A running record of what has been built, the design choices behind it, and
the reasoning a new team member needs to understand the code. Add a section
each time a module is finished.

The rules themselves live in `.claude/CLAUDE.md`; packet formats and the
handshake live in `docs/protocol_spec.md`. This file explains *why* the code
looks the way it does.

## Status

| Step | Module                         | Files                                   | Tested by           | State        |
|------|--------------------------------|-----------------------------------------|---------------------|--------------|
| 0    | Error codes                    | `src/common/errors.h`                   | (used by all tests) | Done         |
| 3    | Key exchange interface         | `src/crypto/kem.h`, `kem.c`, `kem_none.c` | `tests/test_kem.c`  | Done         |
| 2    | Randomness and secret handling | `src/crypto/rng.h`, `rng.c`             | `tests/test_rng.c`  | Done         |
| 1    | Timing and event logging       | `src/metrics/timer.h/.c`, `logger.h/.c` | `tests/test_metrics.c` | In progress |
| 4    | Packet formats                 | `docs/protocol_spec.md`, `src/protocol/messages.h` | —         | Next         |
| 5    | UDP transport                  | `src/transport/udp.c`                   | —                   | Planned      |
| 6    | Baseline apps                  | `src/app/drone.c`, `ground_station.c`, `telemetry_gen.c` | — | Planned      |

Steps were done out of numeric order on purpose: `kem.h` came first because
its shape decides how cleanly ML-KEM and X25519 drop in during week 2.

### Files added beyond the CLAUDE.md layout

| File                    | Why it exists                                               |
|-------------------------|-------------------------------------------------------------|
| `src/crypto/kem.c`      | Mode lookup and length checks, in one place for every KEM   |
| `src/crypto/rng.h`      | Lets other files call `rng_bytes`, `secure_wipe`, `ct_memcmp` |
| `src/metrics/timer.h`   | Public interface to `timer.c`                               |
| `src/metrics/logger.h`  | Public interface to `logger.c`                              |
| `tests/test_kem.c`      | kem.h interface tests (`test_kem_vectors.c` is reserved for official KATs) |
| `tests/test_rng.c`      | rng.c tests                                                 |
| `tests/test_metrics.c`  | timer.c and logger.c tests                                  |

TODO: add these to the repo layout in CLAUDE.md.

---

## Conventions used everywhere

- **Return codes.** Every function returns `int`: `0` (`PQC_OK`) on success,
  a negative code from `errors.h` on failure. The only exception is
  `secure_wipe()`, which returns `void` (see below).
- **Includes** are relative to `src/`: `#include "crypto/kem.h"`,
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

## kem.h: the common key exchange interface

**What it is.** The single interface every key exchange mode implements.
Protocol and app code pick a mode once, from config, and then only call
through the table they get back.

```
Ground (responder)                         Drone (initiator)
keygen() -> pk, sk
                 ---- pk ---->
                                           encaps(pk) -> ct, ss
                 <---- ct ----
decaps(sk, ct) -> ss                       (both sides now hold the same ss)
```

**Design choices**

- **Every mode is a KEM** (keygen / encaps / decaps). X25519 fits: its
  "ciphertext" is the initiator's temporary public key. `none` fits with
  every size set to 0. This is what keeps the key exchange the *only*
  variable between modes: all modes run through the same handshake code.
- **A table of function pointers (`struct kem_ops`)**, looked up with
  `kem_get(mode)` or `kem_from_name("mlkem768")`. Protocol code never names
  a specific KEM and never branches on the mode (crypto rule 2).
- **The implementation tables are not in `kem.h`.** `kem_none_ops` etc. are
  declared `extern` inside `kem.c` only, so the lookup functions are the
  only way to reach them.
- **Wrappers check every length** (`kem_keygen`, `kem_encaps`,
  `kem_decaps`) before calling the implementation. One place to review
  instead of five, and every mode rejects bad input the same way. The
  implementations can assume correctly sized buffers.
- **Two kinds of length check:**
  - Our own output buffers only need to be *big enough* (`cap < len` fails,
    returning `PQC_ERR_BUFFER_TOO_SMALL`).
  - Data from the peer must be *exactly* the right size (`len != expected`
    fails). A short or long public key or ciphertext is a malformed message,
    returning `PQC_ERR_KEM_ENCAPS` / `PQC_ERR_KEM_DECAPS`.
  - A wrong length for our *own* secret key is a bug on our side, so it
    returns `PQC_ERR_INVALID_ARG`.
- **Fixed maximum sizes** (`KEM_MAX_PK_BYTES` etc.) taken from ML-KEM-1024,
  the largest parameter set. Callers allocate fixed buffers of these sizes;
  no `malloc` on the handshake path.
- **Mode numbers are fixed** (`KEM_MODE_NONE = 0` … `KEM_MODE_MLKEM1024 = 4`)
  because they will be sent in the handshake and written to logs.
- **Lookup failures clear the output pointer** (`*out = NULL`), so a caller
  that ignores the error can't keep using a table from an earlier call.

**The `none` mode**

- `ss_len = 0` is what stops `none` from ever producing an encryption key:
  `kdf.c` must reject a zero-length shared secret.
- The no-downgrade check in `handshake.c` stops a secured session from being
  talked into `none`.

**Handshake authentication is independent of this interface.** A pre-shared
key would be mixed in by `kdf.c`; signatures would be added in
`handshake.c`. Neither changes `kem.h`, so that open decision did not block
this step.

**Tests (`test_kem.c`)** loop over every mode number and skip modes that
aren't built yet, so ML-KEM and X25519 are tested automatically as soon as
they are added to the table in `kem.c`.

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

CSV columns, as required by CLAUDE.md:
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

## Follow-ups that later modules must honour

These were promised by code written so far; whoever writes the module must
implement them.

- [ ] `kdf.c` rejects a zero-length shared secret (keeps `none` from
      producing a key).
- [ ] `handshake.c` rejects any mode other than the configured one with
      `PQC_ERR_HS_MODE_MISMATCH` (no downgrade).
- [ ] Callers of `kem_keygen` / `kem_encaps` / `kem_decaps` call
      `secure_wipe()` on `sk` and `ss` on every exit path.
- [ ] Each KEM implementation wipes its own partial outputs if it fails.
- [ ] `scripts/power_logger.py` timestamps with `time.monotonic_ns()`.
- [ ] Apps call `logger_flush()` only outside timed regions.
- [ ] Add the extra files above to the CLAUDE.md layout.

## Open decisions still pending

See "Open decisions" in CLAUDE.md. None of them blocked the work so far.
The next one that matters is **handshake authentication** (pre-shared key
in the KDF vs ML-DSA signatures), which needs deciding before
`handshake.c` and `kdf.c`.
