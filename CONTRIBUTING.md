# Contributing

The rules every change to this repo must follow. Read this fully before writing code.
For what the project is and how to build it, see [README.md](README.md). The final scope
is in [docs/scope.md](docs/scope.md). For *why* the existing code is designed the way it
is, see [docs/design_notes.md](docs/design_notes.md).

If a change would break one of these rules, stop and raise it with the team instead of
working around it.

## What this repo is now

The mandatory system is the advisor's scope of 2026-10-10: IMU, Pico, Pi, Wi-Fi,
**OpenSSL TLS 1.3**, persistent queue and OLED ([docs/scope.md](docs/scope.md)). The earlier
custom stack (ML-KEM shim, Ascon, `kem.h`, custom handshake) was **archived**: it is not in
`main`, but is kept at tag `custom-stack-v1` and branch `archive/custom-stack`. Do not
re-add it, and do not add Ascon or a custom protocol to the mandatory system. Bringing it
back as a research extension needs a defined research question, an independent protocol
spec and the advisor's approval.

## Gates and evidence

- Work follows the Build Guide gates **L1–L10 in order** (except Phase 1 below, where the
  software parts are built on computers first). If an expected physical result is missing, do
  not advance a gate. Change one variable at a time and keep the failed output.
- Each gate needs three files in `evidence/` (rules, naming and the record template are in
  `evidence/README.md`): a dated log showing PASS or FAIL, the exact
  code revision and board IDs, and a photo, logic trace or recording. The instructor
  initials it before the gate counts as DONE.
- **Never invent, estimate or hand-edit measurements.** Every number in a report comes
  from a raw CSV or capture in `evidence/` that someone else can re-run.
- No purchasing, PCB or connector orders, and no optional work (Ascon, custom protocol,
  FPGA, dashboards), without advisor approval.
- **Phase 1 (advisor, 2026-10-16):** the link is built and measured on existing computers
  first. Hardware gates are deferred, not failed. Every result records its platform
  (CPU, OS, OpenSSL version) and is never reported as Pi 4B or Pico data.
- Electrical safety: pins are 3.3 V only, no load powered from a GPIO or Pi header, wire
  with power off, and record header photos before powering up. See the Build Guide.

## Language and compiler flags

- **C17 for all project code** on the Pis, compiled with
  `-Wall -Wextra -Werror -Wmissing-prototypes`. Every non-`static` function must be
  declared in a header first; helpers that aren't in a header must be `static`.
- **The Pi services (Pi A client and queue, Pi B server and store) are written in C17**
  (team decision, 2026-10-10). Confirm with the advisor (Q11).
- **The Pico firmware is written in C with the Raspberry Pi Pico C/C++ SDK** (team decision,
  2026-10-19; MicroPython is not used). A Pico 2 or a Pico W only does sensor and display I/O
  (Pico A reads the IMU, Pico B drives the OLED) and **no cryptography**: they are not powerful
  enough to run TLS, so all TLS stays on the Pis. Firmware is built with `-Wall -Wextra -Werror`
  *(proposed)* and follows the same buffer, timing and bad-frame rules as the Pi code. Confirm the
  role with the advisor (see `docs/advisor_questions.md`, Q13).

- **Code shared with the Pico firmware** (today `src/proto/uart_frame.c`) must include only
  `<stdint.h>`, `<stddef.h>` and `common/errors.h`: no heap, no stdio, no OS calls, no OpenSSL. Pi
  code and firmware then compile the same file and cannot disagree about a wire format.

## Dependencies

- **OpenSSL 3.5 or newer** on every node that does cryptography, pinned to an exact
  version. Record `openssl version -a` in `evidence/bootstrap.log` and in every test
  log. Do not use a distribution default that lacks `X25519MLKEM768`.
- There are no git submodules. If third-party code is ever added, put it under
  `third_party/` pinned to an exact commit, never modify it, and note the version in
  `docs/results.md`.
- Dependency versions are listed in the README.

## Crypto rules (non-negotiable)

Code comments refer to these by number ("crypto rule 7"), so keep the numbering. Rules
1–9 are the original rules; the ones marked *archived* belonged to the custom stack and
are kept here only so old comments and history still make sense. Rules 10–17 apply to the
mandatory system.

### Original rules (1-9)

1. *(archived)* Never implement a cryptographic primitive. (Still true: we use OpenSSL.
   See rule 10.)
2. *(archived)* All key exchanges go through `kem.h`.
3. **All randomness in our own code comes from `rng.c`**, which uses the OS CSPRNG
   (`getrandom()`). Never use `rand()` or any non-cryptographic source. TLS keys and
   nonces come from OpenSSL.
4. *(archived)* Never reuse a nonce with the same key; nonces built in `session.c`.
   (TLS 1.3 handles nonces now.)
5. **Wipe secrets after use.** Call `secure_wipe()` on every secret buffer in our own code
   on every exit path, including errors. Plain `memset` can be optimized away.
6. **Constant-time comparisons only** for tags, MACs, and secrets (`ct_memcmp()`), never
   `memcmp`.
7. **Check every return code** from crypto functions, OpenSSL included. A failed
   verification or decrypt drops the connection or packet and increments a counter; it
   never falls back to plaintext or a weaker mode.
8. *(archived)* No downgrade of the custom handshake mode. (Replaced by rule 12.)
9. *(archived)* Session keys derived in `kdf.c` with domain-separation labels.
   (TLS 1.3's key schedule does this now.)

### Rules for the mandatory system (TLS)

10. **TLS only.** All traffic between Pi A and Pi B goes through a TLS 1.3 connection from
    OpenSSL. No custom ML-KEM handshake, no second application-layer security protocol,
    and no Ascon on top of TLS.
11. **Pin the key-exchange group.** Configuration B accepts only `X25519MLKEM768`.
    Configuration A accepts only `X25519`. Set the groups explicitly on both client and
    server, and set TLS 1.3 as both the minimum and the maximum version.
12. **No downgrade and no fallback.** A hybrid-only peer must refuse a classical-only
    offer (test T04), and Configuration A must never be used when the system is configured
    for hybrid. Never retry a failed handshake with a weaker group, or without
    verification. A client may retry a failed TCP connect, a lost
    session, or a handshake that timed out or hit a socket error (`PQC_ERR_TIMEOUT`, `PQC_ERR_IO`), with
    backoff and the same pinned settings. Any other handshake failure ends the run: no retry loop may
    hide a T03 or T04 failure.
13. **Always verify the peer.** The client verifies the certificate chain against the
    pinned demo CA and checks the hostname, and treats failure as fatal (test T03). Never
    set verify-none, and never turn on an "insecure" flag outside a clearly named test
    that expects the failure.
14. **Same everything but the group.** Configurations A and B use the same application,
    hardware, network conditions, certificate policy and TLS cipher suite. The key-exchange
    group is the only variable.
15. **ACK only after a durable commit.** Pi A writes a record to its persistent queue and
    flushes it before sending. Pi B stores `node`+`seq` and the record durably before
    sending the ACK. Pi A removes a record only after its ACK.
16. **Exactly-once effects.** The OLED unique-record count changes only when Pi B inserts
    a new `node`+`seq`. A duplicate is ACKed again with no other effect (test T05).
17. **Keys and secrets.** Private keys and the demo CA key never go in git. Test
    certificates are generated by a script and are short-lived. Wipe secret buffers in our
    own code (`secure_wipe()` in C), and never log keys or session secrets.

## Protocol rules

- [docs/telemetry_format.md](docs/telemetry_format.md) is the source of truth for the
  UART frame, the record, the ACK, deduplication, the queue and the trust configuration.
  Update it **before** changing code that depends on it, in the same commit.
- Sequence numbers are 64-bit, per node, start at 1, and never reset or reuse a value.
- Every value on a wire or in a file has a documented size, byte order and unit.
- Link and queue sizes are config values, never hard-coded.
- Durable files (Pi B's store, Pi A's queue) are append-only. On start, only a torn or bad-CRC
  **last** entry may be cut off; any other damage stops the program and the file is left as it
  is. Never delete or rewrite a damaged store or queue file to make an error go away.

## Memory and timing rules

- Every buffer copy uses an explicit length that is checked against the destination
  size, in C and in Pico firmware.
- Never log, print or write files **inside** a timed region. Record timestamps into
  memory, then write them after the timed region ends.

## Measurement and logging

- Keep these measurements separate, in separate files: **TLS handshake benchmark**
  (T10) and **application telemetry** (latency, goodput, queue). Never mix them.
- Handshake benchmark: 100 or more *successful* handshakes per configuration, with
  median and p95 latency, bytes on the wire, CPU and memory (with the method written
  down). Failed attempts are recorded in their own file and are not in the statistics.
- Application runs record queue capacity, sample rate, duration and accepted-record
  count, and report any overflow or rejection as a number.
- Timestamps use a monotonic clock. Power is read externally and matched to a run by a
  run ID and timestamp. The instrument and logging method are decided with the advisor
  (Q5) and recorded in the evidence folder.
- Packet-loss tests use controlled emulation at 0, 10 and 30%. The emulation method
  (`tc netem` on the Pis or the access point) is recorded with each run (Q10).
- Report medians and spread over many repetitions, never single runs, and never numbers
  from Debug builds.
- `logger.c` writes CSV with columns
  `run_id, mode, timestamp_ns, endpoint, event, bytes, detail` (`mode` holds the
  configuration name). The logger and timer are kept for the Pi-side instrumentation; the
  output must match the required evidence CSV files (Q15).

## Experiments and reproducibility

- Every experiment is a config file under `experiments/configs/` (configuration A or B,
  loss rate, repetitions, queue capacity, sample rate, duration, seed). Never change
  parameters by editing code.
- `scripts/run_demo.sh` and the experiment runner record: git commit hash, OpenSSL
  version, configuration hashes, board IDs and the date, next to every result.
- Another student must be able to assemble and run the demo from a clean environment
  with no undocumented steps. This is checked by a teammate who did not write the part.

## Testing

Every change must keep `ctest` passing for the code that exists. Required coverage for the
mandatory system:

- T01–T10 from the Build Guide, each with a result row in
  `tests/positive_and_attack_tests.csv` (and `tests/fault_injection.csv` for failures
  and recovery).
- Restart of Pi A and Pi B with records in the queue: nothing lost, nothing counted twice.
- Truncated, oversized, out-of-range, replayed and out-of-order frames on both the UART
  and the application protocol are rejected without a crash.
- A wrong certificate, a wrong hostname and a classical-only peer all fail closed.

New features need an **adversarial test** (what happens when an attacker sends it wrong,
twice, late or truncated), not just a happy-path test.

Conventions:

- One file per module, `tests/test_<module>.c` for C code, registered in `CMakeLists.txt`
  with `add_executable` + `add_test`. Tests are plain programs with a `CHECK(cond)` macro
  that records a failure and keeps going, so one run reports every failure.

## Research wiki

Outside knowledge (standards, papers, datasheets, radio and MAVLink specs) goes in the
research wiki, [docs/research/](docs/research/README.md). Its README holds the full rules;
the essentials are:

- Cite sources by ID, linking into [sources.md](docs/research/sources.md). Never commit
  source PDFs or their extracted text (copyright); every source has a URL or DOI instead.
- Every number carries its unit, its conditions and its source, and says whether it is
  a spec value, reported by others, or our own estimate.
- Our own measurements go in `docs/results.md`, never in the wiki.
- Open decisions are argued on their page in `docs/research/decisions/`. When one is
  decided, update that page, tick it in README.md, and add any resulting rule here.
- Use standard markdown links, not `[[wikilinks]]`, and run `python3 scripts/wiki_lint.py`
  after editing the wiki.

## Claims

Hybrid key establishment is **not** post-quantum authentication: the baseline uses a
classical certificate, and every report says so. This is an educational Wi-Fi link, not
flight-approved, space-qualified, NASA-endorsed, CCSDS BPv7 DTN or secure boot. Do not
write otherwise in a report, slide or video.

## Code style

- `snake_case` for functions and variables; module prefix on public functions
  (`queue_append`, `uart_frame_parse`).
- C functions return `int`: `0` on success, negative error codes from
  `src/common/errors.h`. The one deliberate exception is `secure_wipe()`, which returns
  `void`: it cannot fail and is called on error paths where a code would have nowhere
  to go.
- Error code values are fixed once written (they appear in logs). Add new codes in the
  module's range of 10; never renumber.
- `secure_wipe()` and `ct_memcmp()` live in `src/crypto/rng.h` with `rng_bytes()`.
- Includes are relative to `src/`: `#include "common/errors.h"`.
- Keep functions short; state machines are explicit, not nested conditionals.
- Comment **why**, especially for any security-relevant decision.
- Format C with the repo's `.clang-format` (`clang-format -i <files>`), and make sure
  every file ends with a newline.

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

1. `clang-format -i` on changed C files; every file ends with a newline.
2. `cmake --build build` with no warnings, and `ctest --test-dir build` all passing.
3. `cmake --build build --target docs` adds no new Doxygen warnings (if Doxygen is
   installed).
4. New files are listed in `CMakeLists.txt` (code) and in the repo layout in README.md.
5. If a module was finished, `docs/design_notes.md` is updated.
6. If the research wiki changed, `python3 scripts/wiki_lint.py` reports no issues.
7. No keys, private certificates, source PDFs or invented numbers are staged.
