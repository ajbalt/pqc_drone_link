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

## Preliminary: T10 handshake cost between two machines (2026-10-20)

**Purpose:** first T10 run across a real network, with both ends' costs measured. **Not** Pi data,
**not** gate evidence (`evidence/` needs the Pis, board IDs and the instructor's initials). A development
setup: two ordinary computers, one of them a WSL2 virtual machine.

**Conditions**

| Item | Value |
|------|-------|
| Server | native Linux, Intel Core i7-6700HQ, CPU governor `powersave`, OpenSSL 3.5.9 |
| Client | Intel Core Ultra 9 285H under **WSL2** (a virtual machine), OpenSSL 3.5.9 |
| Network | one LAN; `ping` round trip 0.65 to 1.13 ms (averages 0.81 to 0.85 ms per session), no loss or delay added |
| Build | Release |
| Commits | session 1 `8af42959c866ec803b9607842140f8dfbd73eacd`; sessions 2b and 3b `aa14646abe837a41cd458583e74c916d359e695f` (adds a port check to the server script; the measured programs are the same), tree clean |
| Configuration | `experiments/configs/t10_handshake_lan.conf`: A (X25519) and B (X25519MLKEM768) interleaved, `TLS_AES_128_GCM_SHA256`, 100 measured handshakes per configuration after 5 warm-up, full handshake per connection |
| Sessions | 3 valid sessions (`t10_lan_s1`, `t10_lan_s2b`, `t10_lan_s3b`), n=100 per configuration on **both** sides, no failures. Two earlier attempts (`s2`, `s3`) were invalid and are not used (see below) |
| How to repeat | [two_machine_setup.md](two_machine_setup.md), section 5 |

**Medians per session** (ms; the client's and server's own measurements; B/A is the ratio of the two medians)

| Session | TLS handshake A | TLS handshake B | B/A | Client CPU B/A | Server CPU A | Server CPU B | Server CPU B/A |
|---------|-----------------|-----------------|-----|----------------|--------------|--------------|----------------|
| s1  | 0.971 | 1.203 | 1.24 | 1.23 | 0.379 | 0.479 | 1.26 |
| s2b | 1.142 | 1.305 | 1.14 | 1.17 | 0.479 | 0.587 | 1.22 |
| s3b | 1.080 | 1.220 | 1.13 | 1.17 | 0.377 | 0.465 | 1.23 |

The 95th percentile of the TLS handshake was 1.47 to 1.66 ms for A and 1.70 to 1.89 ms for B. The
full per-session tables (median, p95, min, max, heap, bytes) are the `t10_lan_*.summary.txt` files.

**Deterministic quantities** (identical in all three sessions)

| | Config A | Config B | Difference |
|---|---|---|---|
| Client sends, handshake | 273 B | 1,449 B | +1,176 B |
| Client receives, handshake | 757 B (p95 758) | 1,845 B (p95 1,846) | +1,088 B |
| Heap held by the open connection, client | 19,520 B | 27,408 B | +7.9 KB |
| Heap held by the open connection, server | 15,520 B | 21,584 B | +6.1 KB |

The client's bytes equal the server's bytes in the opposite direction in every session. The differences
(+1,176 B and +1,088 B) equal those measured on loopback in the first section (a different cipher suite
there, so the totals differ).

**What the numbers say, and what they do not**

- Config B costs about 0.14 to 0.23 ms more per handshake than A, and about 1.2 times the CPU time on
  each side. The B/A ratios are stable across sessions (within a few percent); the **absolute times
  moved by about 25 percent between sessions** (the server runs `powersave`, the client is a virtual
  machine), so quote ratios and differences, not one absolute time.
- Three sessions on one pair of machines show the result repeats here; they do not make it a
  statistical claim about other hardware.
- Latency from `connect()` to the end of the handshake includes the TCP connect over the network;
  the TLS-only latency excludes it.
- **A slow connect in B only.** In every session, 2 of the 100 measured B handshakes took 1.01 to
  1.03 s from `connect()` to the end of the handshake, while the TLS part of those same attempts was
  normal (1.5 to 2.4 ms). A had none. A second is about the initial TCP retransmission timeout, so a
  connection attempt to the B server's port was lost and retried. The medians and p95 do not change
  (2 percent is below 5 percent), but a mean or maximum of B would. **The cause was not investigated**
  (not captured; candidates are the network path or the firewall). This is not evidence that B is
  slower; do not report it as a cost of the hybrid group.

**Invalid attempts, kept and not used:** `s2` (its server CSVs held 149 and 148 handshakes where the
client made 105, and were last written about 8 minutes after the client finished, so another client
connected to servers that were still running; the files do not say which one) and `s3` (a client run
found no server: 500 failed connects in 3.7 s, no successes; the later server run recorded no
handshakes). Moved to `invalid/` with a note. The server script now refuses to start if its ports
are in use, which cannot prevent a forgotten server but makes it visible.

**Limits**

- Development machines, one a virtual machine; not the Pis. No Pi clock, power or radio effects.
- A wired LAN with a round trip of under 1 ms, no loss. T08 (loss) was not run across the network.
- Certificates: ECDSA P-256 demo CA; authentication is classical, so this measures key
  establishment only (hybrid key establishment is not post-quantum authentication).
- One pair of machines, three sessions, interleaved A and B.

## Reading these numbers

- Message sizes are deterministic apart from a couple of bytes; they are not statistics.
- A Pi run still has to be done: `evidence/L5_tls_handshake.log` and the first T04 evidence must
  come from the Pis with board IDs and the pinned OpenSSL version.
- No latency, CPU, memory or power was measured here.
