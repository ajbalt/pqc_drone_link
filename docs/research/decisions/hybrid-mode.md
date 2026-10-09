# Hybrid Mode

**Summary**: Whether to add a stretch-goal mode that combines X25519 and ML-KEM-768 in
one handshake, and if so, how to combine their shared secrets safely.

**Sources**: [fips-203](../sources.md#fips-203), [sp-800-227](../sources.md#sp-800-227), [sp-800-56c](../sources.md#sp-800-56c), [sp-800-133r3-ipd](../sources.md#sp-800-133r3-ipd)

**Last updated**: 2026-10-08

**Status**: Open

**Decision**: —

---

## Why it matters

A hybrid mode protects the link if *either* key exchange is broken, which is a common
migration strategy ([sp-800-227 §4.6](../sources.md#sp-800-227)). It would also add a
fourth comparison point (both costs together) to the results. But combining is easy to
get wrong, and it adds handshake bytes and CPU time.

## Requirements

- **The naive combiner is not safe.** KDF(K1, K2), over the two secrets alone, does not
  preserve IND-CCA security, whatever the KDF
  ([sp-800-227 §4.6.3](../sources.md#sp-800-227)). FIPS 203 gives the same warning
  ([fips-203 §3.3](../sources.md#fips-203)). Treated as a KEM, X25519-style
  Diffie-Hellman is not claimed to be IND-CCA
  ([sp-800-227 §5.1.1](../sources.md#sp-800-227)).
- **Approved combiners** apply an SP 800-56C key-derivation method to all the shared
  secrets. They are approved if at least one secret comes from an approved scheme;
  ML-KEM-768 satisfies that. The ciphertexts, encapsulation keys, parameter set and a
  domain separator may be included through FixedInfo
  ([sp-800-227 §4.6.2](../sources.md#sp-800-227),
  [sp-800-56c §2](../sources.md#sp-800-56c)).
- **NIST's CCA-preserving example** hashes both secrets, both ciphertexts and both
  encapsulation keys, plus a domain separator: H(K1, K2, c1, c2, ek1, ek2, domain_sep),
  with H an approved hash ([sp-800-227 §4.6.3](../sources.md#sp-800-227)). The domain
  separator should identify:
  - both components and their order
  - the parameter set
  - the combiner and the KDF

  Including the encapsulation keys is not needed for CCA security, but it binds the
  result to the parties.
- **Encoding:** combined inputs need an unambiguous encoding. Bare concatenation of
  variable-length fields can collide ([sp-800-227 §4.6.2](../sources.md#sp-800-227)).
- **Stricter draft rule:** the SP 800-133 Rev. 3 draft requires *every* combined key to
  come from an approved method ([sp-800-133r3-ipd §5.3](../sources.md#sp-800-133r3-ipd)).
  That is stricter than SP 800-227's "at least one". It ties into
  [Q10](../open-questions.md#q10-does-x25519-count-as-an-approved-sp-800-56a-scheme).
- **Downgrade:** composite schemes add downgrade risk
  ([sp-800-227 §4.6.3](../sources.md#sp-800-227)). Our no-downgrade rule (crypto rule
  8) must treat `hybrid` as its own mode, never falling back to either component.

## Options

| Option | Notes |
|--------|-------|
| No hybrid mode | Keeps the comparison to three modes; least work |
| X25519 + ML-KEM-768 with the NIST example combiner | Exposed through `kem.h` as one mode (crypto rule 2); combiner in `kdf.c`. Needs an approved hash, which ties to [KDF construction](kdf-construction.md) and [Q2](../open-questions.md#q2-where-would-sha-256--sha-3-code-come-from) |
| X-Wing | A published hybrid KEM built from exactly ML-KEM + X25519, which SP 800-227 names as an example ([sp-800-227 §4.6](../sources.md#sp-800-227)) *(design details need source: the X-Wing paper or IETF draft)* |

## Evidence summary

- Combining is allowed, but only with an approved combiner over secrets *and*
  ciphertexts.
- ML-KEM-768 alone makes the combiner approvable. Whether X25519 itself counts as an
  approved SP 800-56A scheme is
  [open question Q10](../open-questions.md#q10-does-x25519-count-as-an-approved-sp-800-56a-scheme).
- *Still needed:* the X-Wing specification, and the IETF hybrid TLS drafts, as design
  references.

## Recommendation

*(pending; this is a stretch goal, so it doesn't block weeks 1–4)*. If it goes ahead,
use X-Wing or the SP 800-227 §4.6.3 combiner, never KDF(K1, K2).

## Related pages

- [ML-KEM](../ml-kem.md)
- [Key derivation](../key-derivation.md)
- [KDF construction](kdf-construction.md)
