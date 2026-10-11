# Results

Our own measurements. The wiki never holds these (research wiki rules). Nothing here is gate
evidence: gate evidence needs the Pis, recorded board IDs and the instructor's initials, and
lives in `evidence/`.

## Preliminary: OpenSSL 3.5.9 behaviour on a development machine (2026-10-11)

**Purpose:** check, before touching the Pis, that the OpenSSL policy we plan to use behaves as
the manual says, and get the handshake message sizes. **Not** Pi data, **not** timing data.

**Conditions**

| Item | Value |
|------|-------|
| Machine | x86_64 under WSL2 (not a Raspberry Pi) |
| OpenSSL | 3.5.9, built from tag `openssl-3.5.9` (commit `45e844fa2a14ec92d146bd8f5778ac130b6625fb`) with `no-docs no-tests` |
| Network | loopback only, no loss, no delay |
| Certificate | ECDSA P-256, `CN=relay.test` with SAN `DNS:relay.test`, from a demo CA |
| Cipher suite | server default, `TLS_AES_256_GCM_SHA384` |
| Tools | `scripts/make_demo_certs.sh`, `scripts/tls_group_matrix.sh`, `scripts/tls_handshake_bytes.sh` |
| Repeats | the pass/fail cases once each; byte totals 3 runs each |

**Group and identity cases** (TLS 1.3 only, `s_client`/`s_server`, `-groups` set on both sides)

| Case | Result |
|------|--------|
| A: client X25519, server X25519 | Connected, key `X25519`, hostname verified |
| B: client hybrid, server hybrid | Connected, group `X25519MLKEM768`, hostname verified |
| T04: client hybrid, server X25519 only | **Failed**: TLS alert 40 (`handshake_failure`) |
| T04, reverse: client X25519, server hybrid only | **Failed**: alert 40 |
| Default client against a hybrid-only server | Connected with `X25519MLKEM768` |
| Default client against an X25519-only server | Connected with `X25519` (a default client falls back) |
| T03: wrong hostname | **Failed**: `hostname mismatch` (verify error 62) |

So pinning the group on **both** sides produces the downgrade rejection we want, and a client
that leaves the group list at its default does not (it connects with whatever the server
offers). That supports crypto rule 11.

**Handshake sizes** (TLS payload bytes through a counting proxy; tickets off; no TCP/IP headers,
no ACK-only packets)

| | Config A (X25519) | Config B (`X25519MLKEM768`) | Difference |
|---|---|---|---|
| ClientHello message | 212 B (0xD4) | 1,388 B (0x56C) | +1,176 B |
| ServerHello message | 122 B (0x7A) | 1,210 B (0x4BA) | +1,088 B |
| Client to server, whole handshake | 321 B | 1,497 B | +1,176 B |
| Server to client, whole handshake | 952 to 954 B | 2,040 to 2,042 B | about +1,088 B |

The server-to-client total varies by up to 2 B between runs (the ECDSA signature length varies).
The ServerHello difference equals the ML-KEM-768 ciphertext (1,088 B). The ClientHello difference is 8 B
less than the 1,184 B I expected from the encapsulation-key size; the cause is not yet explained.

**Certificate validity against the clock** (`openssl verify -attime`, certificate valid 7 days
from issue)

| Verification time | Result |
|-------------------|--------|
| 1 day before issue | Rejected: error 9, certificate is not yet valid |
| Shortly after issue | OK |
| 8 days after issue | Rejected: error 10, certificate has expired |

A device whose clock is wrong therefore cannot verify the demo certificate. See
[certificates and time](research/certificates-and-time.md).

## Reading these numbers

- Message sizes are deterministic apart from a couple of bytes; they are not statistics.
- A Pi run still has to be done: `evidence/L5_tls_handshake.log` and the first T04 evidence must
  come from the Pis with board IDs and the pinned OpenSSL version.
- No latency, CPU, memory or power was measured here.
