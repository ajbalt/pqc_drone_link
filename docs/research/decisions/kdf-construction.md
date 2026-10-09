# KDF Construction

**Summary**: How `kdf.c` turns the key-exchange shared secret into per-direction
Ascon-AEAD128 session keys: Ascon-XOF128 (already in our Ascon build) or an approved
construction such as HKDF-SHA256.

**Sources**: [fips-203](../sources.md#fips-203), [sp-800-56c](../sources.md#sp-800-56c), [sp-800-232](../sources.md#sp-800-232), [sp-800-227](../sources.md#sp-800-227), [sp-800-133r3-ipd](../sources.md#sp-800-133r3-ipd)

**Last updated**: 2026-10-08

**Status**: Open

**Decision**: —

---

## Why it matters

Every secured mode derives its session keys here, so the choice affects:
- code size and dependencies: whether we need SHA-256 alongside Ascon
- handshake CPU time and energy, which are measured metrics
- whether the design can claim to follow NIST guidance for key derivation

It must be the same for `x25519` and `mlkem768`. The key exchange is the only variable
allowed to differ between modes.

## Requirements

- If keys are derived from an ML-KEM shared secret, the derivation **shall** follow
  SP 800-108 or SP 800-56C ([fips-203 §3.3](../sources.md#fips-203)). A KEM secret may
  serve as the input to an SP 800-108, 800-56C or 800-133 method
  ([sp-800-227 §4.3](../sources.md#sp-800-227)).
- One extraction may feed several expansions (our per-direction keys), provided:
  - the same KDF mode and PRF are used for all of them
  - their FixedInfo values (our direction labels) are pairwise distinct
  - no key is output until all of them have been computed

  ([sp-800-56c §5.3](../sources.md#sp-800-56c))
- Project rule (CONTRIBUTING.md crypto rule 9): keys are derived only in `kdf.c`, with
  distinct labels per key and direction, and the shared secret is never used directly
  as an Ascon key.

## Options

| Option | NIST status | Available to us | Notes |
|--------|-------------|-----------------|-------|
| **Ascon-XOF128** (or CXOF128) | **Not approved** for key derivation. SP 800-56C lists only SHA-2/SHA-3, HMAC, KMAC and AES-CMAC ([sp-800-56c §4.1, §7](../sources.md#sp-800-56c)); SP 800-232 leaves the XOFs' approved uses to "other NIST publications" ([sp-800-232 §6](../sources.md#sp-800-232)) | Yes, already compiled in | No new dependency; smallest code. Results must say "non-approved KDF" |
| **HKDF-SHA256** | **Approved**: corresponds to the SP 800-56C two-step method ([sp-800-56c §5.1](../sources.md#sp-800-56c)) | No: needs SHA-256 + HMAC ([Q2](../open-questions.md#q2-where-would-sha-256--sha-3-code-come-from)) | Common in other protocols; §5.3 multi-expansion fits our per-direction keys |
| **One-step SHA3-256** or **KMAC128** | **Approved** one-step options ([sp-800-56c §4.1](../sources.md#sp-800-56c)) | No usable C code: SHA-3 exists only as ml-kem's C++ dependency ([Q2](../open-questions.md#q2-where-would-sha-256--sha-3-code-come-from)) | SHA-3 also matches NIST's hybrid-combiner example ([sp-800-227 §4.6.3](../sources.md#sp-800-227)), which would help if hybrid mode goes ahead |

## Evidence summary

- Ascon-XOF128 has **no approved key-derivation status** under any source we have
  ingested.
- HKDF-SHA256, a one-step SHA3-256 KDF and KMAC128 are all approved, and all meet our
  128-bit target ([sp-800-56c §7](../sources.md#sp-800-56c)).
- The real trade-off is **"approved" vs "no new dependency"**. That depends on
  [Q1](../open-questions.md#q1-how-nist-compliant-must-the-project-be) (how compliant
  we must be) and [Q2](../open-questions.md#q2-where-would-sha-256--sha-3-code-come-from)
  (where hash code would come from).
- [Q4](../open-questions.md#q4-does-sp-800-133-cover-our-kdf-derived-ascon-keys)
  (answered from a draft): Ascon keys derived from an ML-KEM secret are covered by
  SP 800-133, but only through an approved KDF. That rules out Ascon-XOF128 for a
  "follows NIST guidance" claim ([sp-800-133r3-ipd §5.2](../sources.md#sp-800-133r3-ipd)).

## Recommendation

*(pending Q1 and Q2)*

- If the project must follow NIST guidance: **HKDF-SHA256**, using one extraction and
  two expansions labelled by direction (and a third for a key-confirmation key, if Q5
  chooses explicit confirmation).
- If "uses NIST primitives" is enough: **Ascon-XOF128** (ideally CXOF128, with the
  label in the customization string), documented as a non-approved KDF.

## Related pages

- [Key derivation](../key-derivation.md)
- [Handshake authentication](handshake-auth.md)
- [Hybrid mode](hybrid-mode.md)
- [Open questions](../open-questions.md)
