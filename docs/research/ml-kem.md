# ML-KEM

**Summary**: The post-quantum key-encapsulation mechanism we compare against X25519:
parameter sets, key and ciphertext sizes, the input checks and randomness rules a
conforming implementation must follow, and what "implicit rejection" means for our
handshake.

**Sources**: [fips-203](sources.md#fips-203), [sp-800-227](sources.md#sp-800-227)

**Last updated**: 2026-10-08

---

## What it is

ML-KEM is NIST's standardized key-encapsulation mechanism (KEM), derived from the
round-three CRYSTALS-Kyber submission. Its security rests on the Module Learning With
Errors (MLWE) problem, and it is believed to resist attackers with a large quantum
computer ([fips-203 §1.1, §3.2](sources.md#fips-203)).

A KEM has three algorithms ([fips-203 §3.1](sources.md#fips-203)):

| Algorithm | Run by | Randomness | Output |
|-----------|--------|------------|--------|
| KeyGen | Responder (our ground station) | Yes | encapsulation key `ek` (public), decapsulation key `dk` (secret) |
| Encaps(`ek`) | Initiator (our drone) | Yes | shared secret `K`, ciphertext `c` |
| Decaps(`dk`, `c`) | Responder | No (deterministic) | shared secret `K'` |

This is the same shape as our `kem.h` interface (keygen / encaps / decaps); see
[design notes](../design_notes.md).

ML-KEM is built by applying a Fujisaki-Okamoto transform to an internal public-key
encryption scheme, K-PKE. That transform is what gives ML-KEM its IND-CCA2 security;
K-PKE on its own is not IND-CCA2 and must never be used stand-alone
([fips-203 §3.2, §3.3](sources.md#fips-203)).

## Parameter sets

All three sets use `n = 256` and `q = 3329`
([fips-203 Table 2](sources.md#fips-203)). Values are **spec**.

| Parameter set | k | η1 | η2 | du | dv | Security category | Required RBG strength |
|---------------|---|----|----|----|----|-------------------|-----------------------|
| ML-KEM-512    | 2 | 3  | 2  | 10 | 4  | 1 | 128 bits |
| ML-KEM-768    | 3 | 2  | 2  | 10 | 4  | 3 | 192 bits |
| ML-KEM-1024   | 4 | 2  | 2  | 11 | 5  | 5 | 256 bits |

([fips-203 §3.2, §8, Table 2](sources.md#fips-203))

NIST recommends **ML-KEM-768 as the default** parameter set, as a large security margin
at reasonable cost ([fips-203 §8](sources.md#fips-203)). That matches the project's
choice of `mlkem768` as the primary mode.

## Sizes

All sizes in bytes; **spec** values ([fips-203 Table 3](sources.md#fips-203)).

| Parameter set | Encapsulation key `ek` | Decapsulation key `dk` | Ciphertext `c` | Shared secret `K` |
|---------------|------|------|------|----|
| ML-KEM-512    | 800  | 1632 | 768  | 32 |
| ML-KEM-768    | 1184 | 2400 | 1088 | 32 |
| ML-KEM-1024   | 1568 | 3168 | 1568 | 32 |

They follow from the formulas in the algorithm definitions: `ek` = 384k + 32,
`dk` = 768k + 96, `c` = 32(du·k + dv), `K` = 32
([fips-203 §6.1–6.2](sources.md#fips-203)). For example, ML-KEM-768 (k = 3, du = 10,
dv = 4) gives `ek` = 1184 and `c` = 32 × 34 = 1088.

**What goes on air.** Only `ek` (responder → initiator) and `c` (initiator →
responder) cross the link; `dk` and `K` never do. For ML-KEM-768 that is
1184 + 1088 = **2272 bytes** of key-exchange payload per handshake (**estimate**,
derived from the table, before any of our headers, fragmentation or retransmissions).
How many radio packets that becomes is the question in
[Radio link](decisions/radio-link.md).

**Our code.** The `KEM_MAX_PK_BYTES` / `KEM_MAX_SK_BYTES` / `KEM_MAX_CT_BYTES` /
`KEM_MAX_SS_BYTES` constants in [kem.h](../../src/crypto/kem.h) are 1568 / 3168 /
1568 / 32, which matches the ML-KEM-1024 row: the largest supported set, as
CONTRIBUTING.md requires.

## Implicit rejection: decapsulation never reports tampering

If the ciphertext has been altered, Decaps does **not** return an error. It re-encrypts
what it decrypted, compares the result with the received ciphertext, and if they differ
it outputs a pseudorandom value derived from the ciphertext and a secret value `z`
stored in `dk`, instead of the real key. Whether that rejection happened is secret
intermediate data, and the standard forbids returning it in any form
([fips-203 §6.3, Algorithm 18](sources.md#fips-203)).

So the two sides simply end up with different keys. For our protocol this means:

- A tampered ciphertext is invisible at the KEM layer. `PQC_ERR_KEM_DECAPS` can only
  come from our own input checks (wrong length), never from tampering.
- Without an extra step, the mismatch would surface only when the first data packet
  fails AEAD verification (`decrypt_fail`).
- To detect it during the handshake, the protocol needs **key confirmation**: proof
  that each side holds the same key. SP 800-227 recommends it, and accepts a successful
  AEAD exchange as one form of it ([sp-800-227 §4.4](sources.md#sp-800-227)). The
  options are on [KEM usage](kem-usage.md#key-confirmation), and the choice is
  [open question Q5](open-questions.md#q5-implicit-or-explicit-key-confirmation).

Natural decapsulation failures (`K' ≠ K` with no tampering) have probability 2^-138.8
(ML-KEM-512), 2^-164.8 (ML-KEM-768) and 2^-174.8 (ML-KEM-1024) (**spec**,
[fips-203 §3.2, Table 1](sources.md#fips-203)). They are negligible, so no retry
logic is needed for them.

## Input checking

Encaps and Decaps must only run on checked inputs
([fips-203 §3.3, §7.2, §7.3](sources.md#fips-203)):

| Input | Check | Required when | Where we do it |
|-------|-------|---------------|----------------|
| `ek` (from the peer) | Type check: exactly 384k + 32 bytes | Before Encaps (may be assured "by other means") | `kem_encaps()` exact-length check |
| `ek` (from the peer) | Modulus check: every encoded value decodes to an integer in [0, q−1] | Same | The ml-kem library: its `encapsulate()` returns `false` on a malformed key, which becomes `PQC_ERR_KEM_ENCAPS` |
| `c` (from the peer) | Type check: exactly 32(du·k + dv) bytes | **Every** Decaps | `kem_decaps()` exact-length check |
| `dk` (our own) | Type check (768k + 96 bytes) and hash check of the embedded `ek` | Before Decaps (may be assured "by other means") | Length check in `kem_decaps()`; we generate `dk` ourselves |

The standard also requires that the parameter set used in Encaps or Decaps matches the
one the inputs belong to ([fips-203 §7](sources.md#fips-203)). In our design, the exact
length checks enforce this, together with the no-downgrade rule (crypto rule 8).

## Randomness and the internal functions

- An ephemeral key pair is used for **one** key establishment only, then destroyed
  (RS6, [sp-800-227 §4.2](sources.md#sp-800-227)). Every handshake and every rekey runs
  a fresh KeyGen; see [Rekeying](rekeying.md).
- KeyGen draws two 32-byte seeds `d` and `z`; Encaps draws a 32-byte `m`. Fresh
  randomness is required on every call, from an approved RBG (SP 800-90A/B/C) with at
  least the strength in the parameter table above
  ([fips-203 §3.3, §7.1, §7.2](sources.md#fips-203)).
- The deterministic "internal" functions that take those seeds as input are meant for
  testing. They "should not" be exposed to applications, and the seeds "shall" be
  generated by the cryptographic module ([fips-203 §3.3, §6](sources.md#fips-203)).
- **How we fit this:** our library only offers the seed-taking functions, so crypto rule
  3 has `rng.c` generate `d`, `z` and `m` and pass them to the shim. This stays within
  the standard as long as `src/crypto/` is treated as the cryptographic module, and
  nothing outside it can supply seeds. Crypto rule 2 (only `kem_mlkem.c` includes
  `mlkem_shim.h`) already ensures that.
- Linux `getrandom()` is **not** an approved RBG as deployed. An approved RBG needs a
  validated SP 800-90A/B/C construction in a FIPS 140 module, instantiated with at least
  288 bits of entropy for 192-bit strength. This is a conformance limitation, stated in
  the threat model. See [Random bit generation](random-bit-generation.md) and
  [Q7](open-questions.md#q7-how-do-we-state-the-getrandom-deviation).
- The seed pair (`d`, `z`), 64 bytes, may be stored instead of the 2400-byte `dk` and
  re-expanded later. It must then be protected like `dk`
  ([fips-203 §3.3, §7.1](sources.md#fips-203)).

## Using the shared secret

- `K` is always 32 bytes (256 bits) and **may** be used directly as a symmetric key. If
  further keys are derived from it, the derivation **shall** follow SP 800-108 or
  SP 800-56C ([fips-203 §3.3, App. C.1](sources.md#fips-203)).
- Our crypto rule 9 (never use `K` directly as an Ascon key; derive keys per direction
  in `kdf.c`) is stricter than the standard, and we need derivation anyway to get
  separate keys per direction. Which derivation counts as approved is the question in
  [KDF construction](decisions/kdf-construction.md).
- A combined KEM (for example X25519 + ML-KEM) "might not" keep IND-CCA2 security, and
  implementers should assess any such combination. SP 800-227 has the guidance
  ([fips-203 §3.3](sources.md#fips-203)). See [Hybrid mode](decisions/hybrid-mode.md).

## Other implementation requirements

- **Destroy intermediate values:** after KeyGen, Encaps or Decaps, only the designated
  outputs may remain in memory ([fips-203 §3.3](sources.md#fips-203)). This matches our
  crypto rule 5 (`secure_wipe()` on every exit path). Inside the library, it is the
  library's responsibility.
- **No floating-point arithmetic** ([fips-203 §3.3](sources.md#fips-203)). This
  applies to the library, not to our code.
- **Conformance by input/output behaviour:** any mathematically equivalent procedure
  is allowed ([fips-203 §3.3, Announcement ¶7](sources.md#fips-203)). That is why our
  known-answer tests (`test_kem_vectors.c`) check outputs, not internals.

## Related pages

- [FIPS 203 summary](src-fips-203.md)
- [Radio link](decisions/radio-link.md)
- [KDF construction](decisions/kdf-construction.md)
- [Hybrid mode](decisions/hybrid-mode.md)
- [KEM usage](kem-usage.md)
- [Key derivation](key-derivation.md)
