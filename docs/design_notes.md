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
| 7    | Application wire format (RECORD, ACK) | `src/proto/wire.h/.c`           | `tests/test_wire.c` | Done         |
| 8    | TLS link wrapper (OpenSSL, pinned group and suite) | `src/tls/tls_link.h/.c` | `tests/test_tls_link.c` | Done |
| 9    | TCP helpers, TLS server and client programs | `src/net/tcp.h/.c`, `pi/tls_server.c`, `pi/tls_client.c` | `tests/test_tcp.c`, `tests/e2e_tls_link.sh` | Done |
| 10   | T10 handshake benchmark harness | `experiments/bench_handshake.c`, `scripts/run_handshake_bench.sh`, `scripts/summarize_handshakes.py` | `tests/e2e_bench.sh`, `tests/test_summarize.py` | Done (client side) |
| 11   | Durable store: append-only file, dedup by node+seq | `src/store/store.h/.c` | `tests/test_store.c` | Done |
| 12   | Use the store in `tls_server` (ACK after commit, count) | `pi/tls_server.c` | `tests/e2e_tls_link.sh` | Done |
| 13   | Persistent client queue: append-only file, ACK entries, compaction, seq owner | `src/queue/queue.h/.c`, `src/common/fileio.h/.c` | `tests/test_queue.c`, `tests/test_fileio.c` | Done |
| 14   | Client uses the queue: window of 32, ACK timeout, reconnect backoff, outage and restart tests | `pi/tls_client.c` | `tests/e2e_tls_link.sh` (17 cases) | Done |
| 15   | Packet-loss runs (T08): `tc netem` in a private namespace, per-run CSV, summary; handshake timeouts are retried | `scripts/run_loss_test.sh`, `scripts/summarize_loss.py`, `experiments/configs/t08_loss_loopback.conf`, `src/tls/tls_link.c`, `pi/tls_client.c` | `tests/e2e_loss.sh`, `tests/test_summarize_loss.py`, `tests/test_tls_link.c` | Done |
| 16   | Server-side CPU and memory in the T10 benchmark: `tls_server --handshake-csv`, `mem` module, `--server` in the summary | `src/metrics/mem.h/.c`, `pi/tls_server.c`, `scripts/summarize_handshakes.py`, `scripts/run_handshake_bench.sh` | `tests/test_metrics.c`, `tests/test_summarize.py`, `tests/e2e_bench.sh` (25 checks) | Done |
| 17   | Full-size runs on a quiet machine (T10 100+ handshakes, T08 10 x 10 min); then packet captures for byte totals with headers | (not started) | — | Next |
| 3-6  | Key exchange interface, packet formats, UDP transport, baseline apps | (removed from `main`) | — | Archived at tag `custom-stack-v1` |

Phase 1 (computers only, advisor 2026-10-16) is implemented through step 12 below on `main`.
The Pico firmware (Phase 2) and the hardware gates in [schedule.md](schedule.md) are not
started.

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

## wire.h: RECORD and ACK messages

**What it is.** Builds and checks the application messages that travel inside the TLS stream
(`docs/telemetry_format.md` sections 3 and 4). No I/O and no OpenSSL, so the layout is tested
on its own against a fixed byte vector.

**Design choices**

- **`wire_parse_header()` is separate from the decoders.** It checks the type, the maximum
  payload (64) and the exact length for the type before any payload is read, so a length is
  never trusted unchecked.
- **`seq` 0 is refused on encode and decode** (spec section 4).
- **Decoders zero their output on failure**, so an ignored return code cannot leak half-filled data.
- **Error codes `-80..-82`** (`PQC_ERR_WIRE_*`), separate from the archived handshake codes.
- **Phase 1 payload:** `seq` counts up from 1, the six `imu` values are zero (decided 2026-10-16).

**What to notice.** Big-endian bytes are written with shifts, not struct casts, so the code does
not depend on CPU byte order. Negative `int16_t` values round-trip through `uint16_t` (defined
wrap-around); the test vector includes -1 and -32768.

---

## tls_link.h: the TLS 1.3 wrapper

**What it is.** One place that enforces the fixed settings of the experiment: TLS 1.3 only,
exactly one group (X25519 for A, X25519MLKEM768 for B) on both sides, one cipher suite
(`TLS_AES_128_GCM_SHA256`), no tickets, and a client that trusts only the pinned CA and checks
the hostname (crypto rules 10-14).

**Design choices**

- **Opaque `tls_ctx` and `tls_conn`**, so users of the header do not need OpenSSL headers.
- **Sockets stay with the caller.** The module takes a connected `fd`; timeouts
  (`SO_RCVTIMEO`) show up as `PQC_ERR_TIMEOUT`, and the caller must ignore `SIGPIPE`.
- **The negotiated version, group and suite are checked again after the handshake**, and a
  client re-checks that a verified peer certificate exists. Two independent checks, so removing
  one by mistake does not open a hole.
- **Error mapping:** verification failure `PQC_ERR_HS_AUTH` (T03); other handshake failure such
  as no shared group `PQC_ERR_TLS_HANDSHAKE` (T04); tampered record `PQC_ERR_IO` (T09);
  clean close `PQC_ERR_TLS_CLOSED`. New codes `-90..-93`.
- **`PQC_BUILD_TLS` CMake option, default ON,** with `find_package(OpenSSL 3.5 REQUIRED)`; use
  `-DOPENSSL_ROOT_DIR=$HOME/opt/openssl-3.5.9`. The tests make their certificates at test time.

**What to notice.** `SSL_get_error(ssl, 0)` after a failed `_ex` call; `tls_read_exact` loops
because TLS returns whatever has arrived; every call clears OpenSSL's error queue first so
`tls_last_error()` is current. Group names compare case-insensitively (`x25519` vs
`X25519MLKEM768`).

**Follow-ups.** T09 is
covered here by raw bytes on a socketpair; repeat it through a proxy on the real link.

---

## tcp.h, tls_server and tls_client: the Phase 1 link

**What it is.** `tcp.c` holds the socket code both programs share. `tls_server` and `tls_client`
(in `pi/`) use `tls_link` and `wire` to send numbered RECORDs and check each ACK on one machine.

**Design choices**

- **ACK after a durable commit (step 12, 2026-10-18).** Each RECORD goes through `store_insert()`
  before its ACK (rule 15); a duplicate is ACKed again and counted as a duplicate, not as new
  (rule 16, T05). `--store` is required, so there is no mode that ACKs without storing. The store
  is opened before the server listens, and its count and truncated bytes are printed at start.
- **How a connection ends is an `enum conn_end`:** closed, protocol error, store full (the record is
  not ACKed and the connection closes so the client knows at once), or store failure (the server
  exits, so a restart reads back what is really on disk). `--store-max` defaults to 100,000
  *(proposed)*.
- **The benchmark servers get a store too** (the server requires one); the benchmark sends no
  RECORDs, so the store is never written while timing.
- **Stop-and-wait (superseded in step 14).** The first client had one record in flight; see "tls_client: queue, window, reconnect" below.
- **IPv4 literals for `--host` and `--bind`.** The certificate name is checked by TLS
  (`--hostname`), not by DNS.
- **`--port 0` and `--max-conns N`** let the end-to-end script start a server on a free port and
  have it stop by itself.
- **The server closes the connection** on an unknown type, bad length, `seq` 0 or unknown node.
- **No retry and no fallback in the client (superseded in step 14).** Now only a failed TCP connect or a lost session is retried; a failed TLS handshake still ends the run.

**What to notice.** The server checks a header's length before reading the payload; `TCP_NODELAY`
keeps 12-36 byte messages from being batched; the client frees everything through one `goto done`
path; the end-to-end script checks that a failing case never prints `connected` on the server, so a
test cannot pass for the wrong reason. In step 12: decode, `store_insert()`, then ACK is the order
rule 15 needs; a lost ACK is harmless only because the resend is deduplicated; resources are freed
in reverse order on every early exit; the foreign-file case checks the file is byte-for-byte
unchanged (`cmp`), not only the exit code.

**Follow-ups.** Add the
persistent queue and the window of 32 on the client (T06-T08); the benchmark harness (T10) and the
loss emulation (`tc netem`) are separate modules; T09 here is raw bytes on a socketpair, repeat it
through a proxy on the real link.

---

## The T10 handshake benchmark harness

**What it is.** `bench_handshake` makes many full TLS handshakes against two servers (A and B),
alternating between them, and writes one CSV row per attempt. `summarize_handshakes.py` turns the
raw CSV into median, p95 and spread. `run_handshake_bench.sh` reads an experiment config, starts
both servers, runs the harness and records the environment (commit, OpenSSL, config hash, CPU
temperature and frequency where available).

**Design choices**

- **Interleaved A and B**, so drift in load or temperature affects both equally (rule 14).
- **Warm-up excluded;** failures go to a separate file and are never in the statistics.
- **Nothing is written inside a timed region.** Rows are kept in memory and written at the end.
- **Bytes from OpenSSL's socket counters** (`tls_conn_bytes`): exact for the TLS stream, without
  TCP/IP headers. Cross-check: B minus A is +1176 B sent and +1088 B received, exactly the ML-KEM-768
  key and ciphertext sizes.
- **Memory = heap held by the open connection** (`mallinfo2`), plus the process peak RSS once per run.
  The transient peak inside the handshake is not captured.
- **Own CSV writer, not `logger.c`**, because the per-attempt columns do not fit the logger (advisor
  question Q15 is still open).
- **Debug builds are refused** (no `NDEBUG`), and existing result files are never overwritten.

**What to notice.** `wait_for_close()` keeps the previous connection's cleanup out of the next
attempt's latency; `fopen(..., "wx")` makes "never overwrite" an OS guarantee; percentiles are
nearest-rank and unit-tested on `1..100`.

**Follow-ups.** Server-side CPU and memory are measured since step 16; take packet
captures for byte totals with headers on the real link; run on the Pis with board IDs and put the
output in `evidence/`; add packet-loss runs once `tc netem` is set up; decide Q15 (logger reuse).

---

## store.h: the durable record store (Pi B)

**What it is.** The server's commit step (crypto rules 15 and 16, spec section 6). `store_insert()`
appends a record to one file and `fsync()`s it before returning, so `PQC_OK` means "safe to ACK".
A record whose `node`+`seq` is already stored changes nothing and returns `is_new = 0` (T05). On
open, the whole file is read back to rebuild the dedup index, so the count after a restart is
what is on disk. Store chosen 2026-10-17: append-only file, not SQLite (README open decision).

**File layout.** An 8-byte header `PQCSTOR1` (the last byte is the format version), then fixed
40-byte entries: the 36-byte RECORD message exactly as `wire_encode_record()` writes it, plus a
big-endian CRC-32 (IEEE, as zlib) of those 36 bytes. Fixed in a byte vector in `test_store.c`.

**Design choices**

- **Append-only file over SQLite:** no new dependency to install and pin on every machine, and
  every durability step is our own short, tested code. SQLite stays the fallback if this ever
  proves too slow or too fragile.
- **One `fsync()` per record.** Simplest and safest; its cost on a microSD card must be measured
  in L7 before anything is batched (schedule risk "SD-card wear and sync cost").
- **The index changes only after the fsync succeeds,** so memory never claims a record a crash
  could lose. A new file's directory is synced too, or its name could be lost.
- **Only the last entry may be torn.** A short or bad-CRC last entry is cut off on open (it was never
  ACKed, so the client resends it) and the cut size is reported by `store_truncated_bytes()`.
  Damage before the last entry, a repeated entry or a foreign file gives `PQC_ERR_STORE_CORRUPT`
  and leaves the file untouched.
- **Duplicates are checked before capacity,** so a resend after a lost ACK is still ACKed when the
  store is full; a new record when full gives `PQC_ERR_STORE_FULL` and must not be ACKed.
- **One failed write or fsync disables the store** until it is reopened: Linux can drop the unwritten
  pages and clear the error, so a retried fsync could "succeed" with nothing on disk.
- **The whole index is allocated at open** (`max_records` × 16 B), so an insert never allocates.
- **Error codes `-100..-101`** (`PQC_ERR_STORE_*`); read and write failures use `PQC_ERR_IO`.
- **No file lock:** one process per store file is a documented `@warning`, not enforced.

**What to notice.** `write()` only reaches the kernel's memory, `fsync()` reaches the disk;
`pread`/`pwrite` take an explicit offset and can be short or interrupted (`EINTR`), so both are
looped; the CRC catches accidents, not attackers, and is not a secret (so `!=`, not `ct_memcmp`);
binary search uses `lo + (hi - lo) / 2`; overlapping shifts need `memmove`; the opaque
`struct store` stops callers from bypassing the fsync-then-index order; outputs are cleared first
so an ignored return code shows nothing rather than garbage; tests simulate a power cut by
appending half an entry between close and open.

**Follow-ups.** Done in step 12: used in `tls_server` (`--store`, `--store-max`; ACK only after `PQC_OK`; log the
count and truncated bytes at startup) and test T05 end to end; measure the per-record fsync cost on
the real store medium; the client queue (step 13) needs the same torn-tail rules.

---

## queue.h: the persistent client queue (Pi A)

**What it is.** Pi A's half of crypto rule 15 (spec sections 1, 3 and 7). `queue_append()` gives a
record the next `seq`, writes it and `fsync()`s it; only then may the client send it. It stays until
`queue_ack()` for that `seq`. The queue owns the sequence numbers. Module only: the client does not
use it yet (step 14).

**File layout.** A 20-byte header (`PQCQUEU1`, a `next_seq` floor, CRC-32), then entries in the order
they happened: a 40-byte record entry (the RECORD message plus CRC-32, same as the store) or a 16-byte
ACK entry (the ACK message plus CRC-32). The type byte gives the length. A record is waiting if no ACK
entry follows it. Fixed in a byte vector in `test_queue.c`.

**Design choices**

- **ACK entries, not an `acked_through` cursor.** A cursor cannot express out-of-order ACKs and needs an
  in-place overwrite, which breaks the append-only rule. Cost: one more `fsync()` per ACK (about 20
  per second at 10 Hz); measure on microSD in L7.
- **The `next_seq` floor in the header.** Compaction drops ACKed records, so the file alone no longer
  shows the highest `seq` ever used. Without the floor a restart could reuse an ACKed `seq` and Pi B
  would dedup new data away. `test_no_seq_reuse` covers it.
- **A rejected record still uses up its `seq`** (full queue gives `PQC_ERR_QUEUE_FULL`), so the loss
  shows as a gap. The overflow count is in memory only (`queue_get_stats()`); the test harness must
  log it. After a restart the last rejected seqs may be reused, which is safe: they were never stored.
- **Compaction** writes `<path>.tmp`, `fsync()`s it, renames it over the queue file and syncs the
  directory. It runs on open when ACKed records are present, and while running once as many records
  have been ACKed as the capacity.
- **Same torn-tail rule as the store.** Only a short or bad-CRC last entry is cut off. A good CRC with
  a nonsense meaning (seq going backwards, another node, an ACK for a record that is not waiting) is
  `PQC_ERR_QUEUE_CORRUPT` even as the last entry.
- **A smaller `max_pending` than the file needs is `PQC_ERR_BAD_CONFIG`,** not silent loss.
- **ACKs in any order are accepted;** a second ACK writes nothing; an ACK for a `seq` never issued is
  `PQC_ERR_QUEUE_BAD_ACK`.
- **The ring is allocated at open** (`max_pending` x 48 B), so an append never allocates after the
  disk write. One failed write, fsync or compaction disables the queue until it is reopened.
- **Error codes `-110..-112`** (`PQC_ERR_QUEUE_*`).
- **`fileio` is new shared code** (read/write loops, directory sync, CRC-32, big-endian). `store.c`
  still has its own private copies.

**What to notice.** Disk first, memory second; `rename()` is the atomic step; a ring with a moving
head makes the usual ACK O(1); the CRC check separates a crash from damage; a seeded generator makes
the random model test reproducible. Sanitizers and three deliberate bugs (ignoring the floor,
compaction forgetting it, not writing the ACK) were used to check the tests can fail.

**Follow-ups.** Step 14 uses it from `tls_client`. Switch `store.c` to `fileio`. Measure the
per-append and per-ACK fsync cost on the real medium.

---

## tls_client: queue, window, reconnect (step 14)

**What it is.** Pi A's send loop (spec section 5). The client makes numbered RECORDs at `--rate-hz`,
puts each in the persistent queue (`queue_append()` assigns the `seq` and `fsync()`s), sends the oldest
`--window` waiting records, reads that many ACKs, and removes a record only through `queue_ack()` for
an ACK it has checked (rule 15). `--queue` is required, so there is no mode without a queue.
`--count 0` sends only what the queue file already holds (the restart case).

**Design choices**

- **A timeout means reconnect, not resend on the same connection.** TCP is reliable; a stream that has
  stopped answering is only fixed by a new one. Everything not yet ACKed goes again from the oldest
  record, and the server deduplicates (rule 16).
- **Batch send, then read, no `poll()`.** No change to `tls_link` was needed. A `poll()` loop would
  need `SSL_has_pending()`: ACKs already inside OpenSSL's buffer make the socket look idle. The cost:
  while the client waits for a slow ACK or connect, record ticks are missed and made up afterwards, so
  `ts_ms` can be late by up to `--timeout-ms`. This disappears once real samples arrive from the Pico.
- **Only a bad link is retried.** A failed TCP connect, a lost session, and (since step 15) a handshake
  that times out or hits a socket error are retried; every other handshake failure ends the run
  (rules 12 and 13): a retry loop must not hide T03 or T04, or look like a downgrade attempt. The queue
  survives, so a later run drains it. Step 14 first made *every* handshake failure fatal; the loss runs
  showed that was too coarse (see step 15).
- **ACKs must come in order, each for the oldest waiting record.** Anything else drops the connection
  (`bad_ack`). The server ACKs in order today; relax this if that changes.
- **Backoff** doubles from `--backoff-ms` to `--backoff-max-ms` and resets once a connection has had
  an ACK, so a server that accepts and then closes (store full) does not cause a reconnect storm.
- **Defaults** are the spec's proposed values: window 32, ACK timeout 2 s, backoff 1 s to 10 s, 10 Hz,
  queue 10,000. All are options, none are constants (CONTRIBUTING "Protocol rules").
- **Exit status 0 only if nothing is waiting and nothing was rejected.** The `done` line prints the
  section 7 evidence fields: generated, accepted, rejected, ACKed, pending, peak, lost links, capacity,
  rate, duration and drain time.
- **`--deadline-ms`** exists so a run against a dead server ends (the outage and full-store tests).

**The end-to-end script** (`tests/e2e_tls_link.sh`, 17 cases, one machine, not gate evidence) now has
T05 as a real replay (the queue file is copied before the first drain and put back for the second),
T06 (50 records queued while the server is down, all delivered), T07 (client restart; seq carries on
at 31, and a `SIGKILL` of the client mid-run), L9 (queue 20 of 100: 80 rejected and counted, 20
delivered), a lost link (server killed and restarted on the same port and store, 40 of 40 stored), and
window 1. A deliberately broken client that removed records without reading their ACKs failed 6 of the
17, so the cases can fail.

**What to notice.** The queue is the only copy of a record: the client asks `queue_get(i)` for each send
and `queue_get(0)` for each ACK, so a crash or reconnect cannot leave two copies disagreeing. The
`progressed` flag separates a healthy client that lost one connection from a dead server. In the shell
script, `timeout` is a separate process, so `kill -9` on it leaves the client running (use
`pkill -9 -P`); later options override earlier ones, which lets one helper set test defaults.

**Follow-ups.** The real T08 runs (step 15 built the runner). Replace the batch loop
with `poll()` if `ts_ms` lateness matters. Measure per-append and per-ACK `fsync` cost on the real
medium (L7). A fake server to test `bad_ack` and malformed ACK handling directly. Fill in the result
rows in `tests/positive_and_attack_tests.csv` only from runs on the real platform.

---

## T08 packet-loss runs (step 15)

**What it is.** A runner that measures whether every accepted record is delivered under emulated loss
(spec sections 7 and 9, T08). `scripts/run_loss_test.sh` reads `experiments/configs/t08_loss_loopback.conf`,
and for each repetition, loss level and configuration (A and B side by side) starts `tls_server` and
`tls_client` inside a fresh `unshare -Urn` user and network namespace, applies `tc netem loss N%` to that
namespace's loopback, and writes one CSV row per run. `scripts/summarize_loss.py` groups the rows by
(configuration, loss) and prints delivery, median, p95 and spread. Dev-machine only, not gate evidence.

**Design choices**

- **Unprivileged namespace, no root.** A new user and network namespace gives the script its own `lo` and
  `CAP_NET_ADMIN` inside it, so `netem` works without `sudo`. Runs cannot disturb each other or the
  machine, and the namespace's TCP counters hold exactly one run. The advisor's Q10 (netem on the Pis or
  at the access point) is still open for the real runs; the loss method is recorded with every run.
- **Loss applies to every packet in both directions,** so a request and its answer can each be lost.
- **MTU set to 1500 on `lo`.** Loopback's 64 kB MTU would hide that the hybrid handshake spans more
  packets than the classical one does on Ethernet or Wi-Fi.
- **A run is delivered only if nothing is waiting, ACKed equals accepted and stored equals accepted.**
  Stored comes from the server's store file size ((size - 8) / 40), not from server logs, which can be
  missed when a FIN is delayed. Every run is reported, delivered or not; a missing value counts as not
  delivered; a group with fewer than `min_runs` (10) runs is INSUFFICIENT.
- **Proof the loss acted:** `RetransSegs` from `/proc/net/snmp` is a column of every row.
- **`tls_link` now tells a bad link from a bad peer.** A handshake that times out or hits a socket error
  returns `PQC_ERR_TIMEOUT` or `PQC_ERR_IO`; alerts, failed verification and policy mismatches keep their
  codes. Without this the first lossy handshake would have ended the run, which contradicts T08.
- **Dev-sized defaults** (200 records at 20 Hz, 3 repetitions, about 3 minutes); the config's comment gives
  the spec's size (6000 records at 10 Hz, 10 repetitions).
- **Smoke test skips (exit 77) where netem cannot run.**

**First look (one repetition per cell, dev machine, not a result).** All 6 runs delivered everything. At
30% loss the client lost 2 sessions per run and its queue took about 4 s to drain after the last record,
because the 2 s ACK timeout is shorter than TCP's retransmission backoff at that loss rate; at 10% no
sessions were lost. Do not quote these numbers: they are single runs.

**What to notice.** The runner re-executes itself with `--inner` so one file holds both halves; `awk`
finds the `RetransSegs` column by name from the header line; the first version of the smoke test's
"netem is in the meta file" check passed with netem switched off, because it matched the probe line, not
the per-run lines (a deliberately broken runner exposed it).

**Follow-ups.** Real runs: 10 repetitions of 10 minutes at 10 Hz, then on the Pis with the method Q10
settles. Look at whether a longer ACK timeout (or resending without reconnecting) shortens drain at 30%.
Time per message under loss (sensor-to-display latency) is a separate measurement.

---

## T10 server-side cost (step 16)

**What it is.** The benchmark client measured only its own side. `tls_server --handshake-csv FILE` now
writes one row per accepted connection (spec section 10, "Server side"): the CPU time of the server
process over `tls_accept()`, the live heap the open connection holds, the handshake bytes received and
sent, and the process peak RSS so far. `run_handshake_bench.sh` starts both servers with their own file
(`*.server_A.csv`, `*.server_B.csv`) and `summarize_handshakes.py --server A=... --server B=...` prints a
"server side" block under each configuration.

**Design choices**

- **The server logs itself, in its own file.** Client and server numbers stay in separate files
  (CONTRIBUTING "Measurement and logging"). Joining rows by attempt number was rejected: the server does
  not know the client's attempt numbers.
- **Measure, then log.** Two readings (CPU, heap) around `tls_accept()`; the row is written and flushed
  afterwards, so nothing is written inside the timed span and a server stopped with a signal loses no
  rows. CPU time counts work, not waiting, so the wait for the client's first message is not in it.
- **Warm-up is handled in the summarizer.** The server cannot know which handshakes were warm-up: rows
  with `ok=0` are counted as failures, the first `--server-warmup` successes are dropped, and the summary
  exits 1 if the server's count differs from the client's. A shift in alignment cannot pass silently.
- **Client and server must agree on the bytes:** the client's transmitted bytes equal the server's
  received bytes and the reverse, on every row (checked in `e2e_bench.sh`).
- **Peak RSS is the whole process,** about 8.5 MB, dominated by the server's baseline, so A and B look the
  same. The per-connection heap is the figure that separates them; the limit is written into the spec.
- **New `mem` module** (`mem_heap_in_use`, `mem_peak_rss_kb`). `bench_handshake` had private copies of the
  same two readings; it now uses the module. `fopen(..., "wx")` refuses an existing server file.
- **Not an automated check:** that hybrid costs the server more CPU. The difference is small and noisy,
  so a test on it would be flaky; the summary shows it instead.

**First look (one dev-machine run of 100 per configuration, not a result).** Server medians: CPU 0.36 ms
(A) vs 0.50 ms (B); heap held by the open connection 15.5 kB vs 21.6 kB; handshake bytes 273 received and
756 sent (A) vs 1449 and 1844 (B), matching the client's own counts.

**What to notice.** `volatile` in `test_mem` stops the compiler removing the allocation as dead code at
`-O2`; a mutation check (zeroing the CPU reading, swapping rx and tx) failed two checks each, so they can
fail; `mallinfo2` has two fields (`uordblks`, `hblkhd`) and the 16 MB test block exercises the mmap one.

**Follow-ups.** Take the full-size runs (100+ handshakes on a quiet machine, then on the Pis). Packet
captures for byte totals with headers. The transient peak memory inside a handshake is still not
captured per attempt. **Known flaky test:** `e2e_loss` failed once in a full `ctest` run and could not be
reproduced afterwards (25 direct runs and 30 repeated ctest runs passed; the failing check was not
captured). If it fails again, keep the output: the runner prints each run and the checks name what they
saw. Suspects: a 10% loss run that needs a SYN retransmission and runs close to a timeout, or machine load
during the full suite.

---

## Progress report (LaTeX)

`docs/report/progress_report.tex` is a short advisor-facing report with the architecture, security
design, wire format, verification table and benchmark plots. Plot data comes from
`prepare_data.py`, which reads the raw `results/*.ok.csv` files and writes `docs/report/data/`,
including `numbers.tex`, the macros the text uses, so no measurement is typed by hand. It refuses
to run on non-Release rows. All results in it are labelled preliminary (development computer,
loopback, not gate evidence). Rebuild after new benchmark runs: run `prepare_data.py`, then
`latexmk -pdf progress_report.tex` (MiKTeX on Windows works; add `--enable-installer` for packages).

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
| Compiler: `no previous prototype for 'wire_encoder_record'` | Name in `.c` differs from the header (`encoder` vs `encode`) | `-Wmissing-prototypes` stops on the definition's line |
| Compiler: `redefinition of 'wire_decode_record'` | A function pasted twice into the same file | Read the error's line numbers; delete one copy |
| Config A handshake rejected as a policy violation while B works | OpenSSL names the classical group x25519 in lowercase but the hybrid X25519MLKEM768 | Compare group names with strcasecmp; the test checks both |
| Doxygen: `Compound tls_conn is not documented` | Opaque struct defined in the `.c` file has no `/** @brief */` | Run the docs target before showing a draft |
| Tests fail with 'cannot open' or every TLS case fails | Cert script run without `LD_LIBRARY_PATH` for the private OpenSSL, so no certificates were made | CMake runs the script with `LD_LIBRARY_PATH` set |
| Tests pass locally but a fresh build fails with "server did not start" | CTest properties are `FIXTURES_SETUP` and `FIXTURES_REQUIRED` (plural); singular names are silently ignored, so only definition order made it work | Test from an empty build directory |
| Linker in `tls_client`: undefined reference to `store_count` | The new server code was typed into `pi/tls_client.c`; the client target does not link `pqc_store`. The e2e script still passed because it tested the old, unsaved `tls_server.c` | Check the file name in the editor tab; `git status` must list the files you meant to change; count the PASS lines, not just the exit code |
| `bash: tests/e2e_bench.sh: No such file or directory` | File saved as `tests/ e2e_bench.sh` (leading space) | `ls -b tests` shows the escaped name |
| `Permission denied` running a script | Typed-in files get mode 0644 | `chmod +x`, as `make_demo_certs.sh` has in git |
| CLion marks `O_DIRECTORY` / `O_CLOEXEC` undefined, but `cmake --build build` is clean | They are POSIX.1-2008 names, hidden under strict `-std=c17` unless `_POSIX_C_SOURCE=200809L` is set. CLion's own configure had failed ("Could NOT find OpenSSL"), so its analyzer never got the project's defines | Add `-DOPENSSL_ROOT_DIR=$HOME/opt/openssl-3.5.9` to the CLion CMake profile and reload; trust the terminal build |
| git diff shows "No newline at end of file" | Editor not adding a final newline | Editor setting + `clang-format -i` before committing |
| Last end-to-end case fails and a server log shows `kill: No such process` | A block of the script was pasted twice, so two `start_server` calls ran; the first server kept the port, `SPID` pointed at the second (which could not bind), and `kill`/`wait` hit the wrong process | `diff -w` the typed script against the draft; a second `start_server` in one case is the sign; count the PASS lines (17) |
| `kill -9` in a test leaves the program running | `timeout` is its own process; the signal reached `timeout`, not the client | `pkill -9 -P <timeout pid>` first, then `kill -9` the timeout |
| `precommit` fails with "missing final newline" on a file that `diff -w` calls identical | `diff -w` ignores a missing last newline | Editor setting; `precommit` catches it |
| A smoke test passes with the thing it tests switched off | The check matched a line that a different part of the output also produces (the netem probe line in the meta file), so it never looked at the per-run lines | Break the code on purpose and see the test fail (mutation check); assert on something only the real effect produces (TCP retransmissions, exactly one qdisc line per lossy run) |

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
- [x] `tls_server` sends an ACK only after `store_insert()` returns `PQC_OK`, and changes the
      unique-record count only when `is_new` is 1 (crypto rules 15 and 16).
- [x] After `PQC_ERR_IO` from the store, the server stops ACKing and reopens the store (or exits).
      It exits (step 12).
- [x] The client queue file never deletes or repairs damage before its last entry (step 13).
- [ ] `store.c` still has private copies of the helpers now in `src/common/fileio.c`; switch it over.
- [x] `tls_client` calls `queue_ack()` only for a verified ACK for the oldest waiting record it sent, and resends in `queue_get()` order after a reconnect (step 14).
- [ ] `tls_client` assumes Pi B ACKs in order; revisit if the server changes.
- [ ] `tls_client` makes up missed record ticks after a slow ACK or connect, so `ts_ms` can be late; fix or document when the Pico supplies samples.

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
| 2026-10-16 | Advisor replied: implement and test the complete link on existing computers, no purchases; Pico W migration after verified encryption, decryption, communication and measurements. Recorded as Phase 1 / Phase 2 in `docs/scope.md`, README, `schedule.md`, `bom.md`, CONTRIBUTING. Follow-up: Pico W or Pico 2 both fine; IMU and OLED need not be simulated in Phase 1. Hardware gates deferred. No code changed |
| 2026-10-16 | `wire` module (RECORD/ACK encode, header check, decode) verified, typed in and tested. Cipher suite decided: `TLS_AES_128_GCM_SHA256`. Next: `tls_link` |
| 2026-10-16 | `tls_link` module (pinned TLS 1.3 client/server wrapper) verified, typed in and tested: A, B, T02, T03, T04, T09 and timeout over a socketpair. `ctest` 6/6. Next: `tls_server` and `tls_client` programs |
| 2026-10-17 | `tcp`, `tls_server` and `tls_client` typed in and tested. First end-to-end run on one machine: A and B succeed, T03 and T04 fail closed, as real processes. `ctest` 8/8. Commit step is a stub. Next: durable store, dedup and ACK-after-commit |
| 2026-10-17 | T10 handshake benchmark harness typed in and tested: `bench_handshake`, `run_handshake_bench.sh`, `summarize_handshakes.py`, `tls_conn_bytes`. Fixed the CTest fixture property names (plural). First full run on this machine produced CSVs, meta and summary in `results/` (dev machine, not evidence). `ctest` 10/10 |
| 2026-10-17 | Store decided: append-only file with fsync (not SQLite). `store` module (`src/store/`) verified, typed in and tested: dedup by node+seq, restart, capacity, torn tail, bad CRC, foreign file. New error codes -100, -101. Spec sections 6, 7 and 11 updated. `ctest` 11/11. Not yet used by `tls_server`. Next: use the store in `tls_server` |
| 2026-10-18 | Step 12: `tls_server` commits every RECORD to the store before its ACK (`--store` required, `--store-max`), logs new and duplicate counts per connection, exits on a store failure. `e2e_tls_link.sh` adds T05 (replay counted once), restart, torn tail, foreign store file and full store, all on one machine (not gate evidence). Benchmark scripts pass a store. `ctest` 11/11, 11 e2e cases. Next: persistent queue on the client |
| 2026-10-19 | Queue capacity and overflow proposed (10,000 un-ACKed records, reject new, still use the `seq`); spec section 7 updated. Step 13: `queue` module (`src/queue/`) and shared `fileio` (`src/common/`) verified, typed in and tested: layout, ACK entries, out-of-order ACKs, overflow, compaction, restart, torn tail, corruption, seq never reused, random model. New error codes -110 to -112. `ctest` 13/13. Next: use the queue in `tls_client` (step 14) |
| 2026-10-19 | Step 14: `tls_client` uses the queue: `--queue`, `--rate-hz`, `--window`, `--backoff-ms`, `--deadline-ms`; reconnect with backoff after a lost session or failed TCP connect, handshake failure still fatal; ACKs checked against the oldest waiting record. `e2e_tls_link.sh` rewritten to 17 cases (T05 replay, T06 outage, T07 restart and SIGKILL, L9 overflow, lost link, window 1), all on one machine (not gate evidence). `ctest` 13/13. Next: loss emulation (T08) and server-side benchmark costs |
| 2026-10-19 | Step 15: packet-loss runner for T08. `scripts/run_loss_test.sh` runs A and B at each loss level inside `unshare -Urn` namespaces with `tc netem` (no root); `summarize_loss.py` and `t08_loss_loopback.conf`. `tls_link` returns `PQC_ERR_TIMEOUT`/`PQC_ERR_IO` for a handshake that times out or hits a socket error, and `tls_client` retries those (other handshake failures stay fatal). `e2e_loss.sh` and `test_summarize_loss.py` added; `ctest` 15/15. One dev-machine repetition per cell delivered every record at 0, 10 and 30% (not a result). Next: server-side CPU and memory in the T10 benchmark (step 16) |
| 2026-10-19 | Step 16: server-side T10 cost. `tls_server --handshake-csv` (CPU over `tls_accept()`, heap held by the open connection, bytes, peak RSS), new `src/metrics/mem` module (also used by `bench_handshake`), `--server` and `--server-warmup` in `summarize_handshakes.py`, `run_handshake_bench.sh` passes the files. `e2e_bench.sh` grows to 25 checks incl. client/server byte agreement; mutation-checked. `ctest` 15/15. One 100-handshake dev-machine run: server CPU 0.36 ms (A) vs 0.50 ms (B). `e2e_loss` failed once in a full run and did not reproduce (see step 16 follow-ups). Next: full-size runs on a quiet machine, then packet captures |
