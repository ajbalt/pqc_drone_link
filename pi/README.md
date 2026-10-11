# Pi services

Not started. This folder will hold the C17 programs that run on the two Raspberry Pi 4 boards:

| Node | Program | What it does |
|------|---------|--------------|
| Pi A | client and queue | Reads UART frames from Pico A, writes each record to a persistent queue, sends it over TLS 1.3, deletes it only after the ACK |
| Pi B | server and store | Accepts TLS connections, stores each `node`+`seq` durably, ACKs after the commit, deduplicates, and sends the unique-record count to Pico B |

The record, ACK, deduplication, queue and trust settings are defined in
[../docs/telemetry_format.md](../docs/telemetry_format.md). That file is the source of truth: update it before
the code.

## Before writing anything

- Language: **C17** (team decision; advisor confirmation pending, Q11).
- Gates L1 to L4 (hardware and the plain Wi-Fi diagnostic) come first. L5 and L6 (OpenSSL and hybrid TLS) need
  OpenSSL 3.5 or newer and a verified `X25519MLKEM768` group on both Pis. See
  [OpenSSL TLS 1.3 and the hybrid group](../docs/research/openssl-tls13-hybrid.md) and
  [certificates and the Pi clock](../docs/research/certificates-and-time.md).
- Add `find_package(OpenSSL 3.5 REQUIRED)` to `CMakeLists.txt` when the first service is added, so an older
  OpenSSL fails the build.
- Reusable code already in the repo: `src/common/errors.h`, `src/crypto/rng.*`, `src/metrics/timer.*` and
  `logger.*`. Whether the logger writes the evidence CSVs is advisor question Q15.

## Rules that apply here

CONTRIBUTING.md crypto rules 10 to 17: TLS only, pinned group, no downgrade, always verify the peer, same
everything but the group for A and B, ACK only after a durable commit, exactly-once effects, no keys in git.
