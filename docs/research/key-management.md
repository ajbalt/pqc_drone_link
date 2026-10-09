# Key Management

**Summary**: NIST key-management guidance that applies to our session keys: comparable
security strengths and the weakest-link rule, cryptoperiods (rekey intervals), how keys
may be generated and combined, rekeying vs key update, and disposal.

**Sources**: [sp-800-57pt1](sources.md#sp-800-57pt1), [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd), [sp-800-227](sources.md#sp-800-227)

**Last updated**: 2026-10-08

---

## Security strength

Comparable strengths ([sp-800-57pt1 §5.6.1.1, Table 2](sources.md#sp-800-57pt1)):

| Strength (bits) | Symmetric | Finite-field DH (L / N) | RSA (k) | Elliptic curve (base-point order, bits) |
|-----------------|-----------|-------------------------|---------|-----------------------------------------|
| ≤ 80 | 2TDEA | 1024 / 160 | 1024 | 160–223 |
| 112 | 3TDEA | 2048 / 224 | 2048 | 224–255 |
| 128 | AES-128 | 3072 / 256 | 3072 | 256–383 |
| 192 | AES-192 | 7680 / 384 | 7680 | 384–511 |
| 256 | AES-256 | 15360 / 512 | 15360 | 512+ |

- 112 bits is acceptable through 2030, then disallowed for applying protection; 128 bits
  and above are acceptable ([sp-800-57pt1 §5.6.3, Table 4](sources.md#sp-800-57pt1)).
- SP 800-57 predates the post-quantum standards and does not cover ML-KEM or the
  security categories 1/3/5 ([sp-800-57pt1 §5.6.1](sources.md#sp-800-57pt1)).
- **X25519.** Curve25519's base-point order is about 2^252 *(outside knowledge, needs
  source)*. Read literally, the table above would put it in the 112-bit row, while
  common practice treats X25519 as 128-bit. Citing X25519 as a 128-bit baseline needs
  SP 800-186 or SP 800-56A ([Q10](open-questions.md#q10-does-x25519-count-as-an-approved-sp-800-56a-scheme)).

**Weakest link.** Protection is only as strong as the weakest algorithm, key or process
involved ([sp-800-57pt1 §5.6.2](sources.md#sp-800-57pt1)). A key is also capped by the
strength of the RBG that produced it
([sp-800-133r3-ipd §2.3.3](sources.md#sp-800-133r3-ipd)). **So our session security is
128 bits, set by Ascon-AEAD128, even with ML-KEM-768.** SP 800-227 explicitly
accommodates this ([sp-800-227 App. D](sources.md#sp-800-227)). The results should say
"128-bit session security".

## Cryptoperiods

Suggested lifetimes ([sp-800-57pt1 §5.3.6, Table 1](sources.md#sp-800-57pt1)); these are
rough order-of-magnitude guidance:

| Key type | Use period | Ours |
|----------|------------|------|
| Symmetric data-encryption key, high-volume link encryption | About **a day to a week** | Ascon session keys |
| Symmetric data-encryption key, smaller volumes | Up to 2 years | — |
| Ephemeral key-agreement keys (private and public) | **One key-agreement transaction** | ML-KEM / X25519 handshake keys |
| Static key-agreement keys | 1–2 years | Static KEM keys, if used for authentication |
| Symmetric key-derivation (master) key | About 1 year | A long-term PSK |

Shorter periods are justified by data volume, nonce limits, the threat level, and
quantum computers ([sp-800-57pt1 §5.3.1](sources.md#sp-800-57pt1)). A drone mission is
far shorter than a day. **NIST guidance doesn't constrain our rekey interval**, so it is
a free experimental variable. See [Rekeying](rekeying.md).

## Generating and combining keys

From the SP 800-133 Rev. 3 *initial public draft*. Confirm against the final before
treating it as normative ([Q4](open-questions.md#q4-does-sp-800-133-cover-our-kdf-derived-ascon-keys)).

- **Every key must come, directly or indirectly, from an approved RBG.** Keys derived
  during key agreement, or with a KDF, count as indirectly generated
  ([sp-800-133r3-ipd §2.1](sources.md#sp-800-133r3-ipd)).
- **A shared secret from an approved KEM is itself a symmetric key**, and needs no
  further derivation. Keys derived from it must use an **approved** key-derivation
  method built on approved hashes, HMAC, AES-CMAC or KMAC
  ([sp-800-133r3-ipd §5.2–5.2.2](sources.md#sp-800-133r3-ipd)).
- **Ascon-XOF is listed as an approved XOF only for seed expansion** in key-pair
  generation, not as a KDF ([sp-800-133r3-ipd §4.2.2](sources.md#sp-800-133r3-ipd)).
  This doesn't change the [KDF construction](decisions/kdf-construction.md) evidence.
- **Combining keys** is allowed in three ways: concatenation, XOR, or HMAC extraction.
  But **every** component must be independent and come from an approved method
  ([sp-800-133r3-ipd §5.3](sources.md#sp-800-133r3-ipd)). That is stricter than
  SP 800-227's hybrid combiner, which needs only one approved input; it affects
  [Hybrid mode](decisions/hybrid-mode.md) and a PSK.

## Rekeying, not key update

- **Re-keying** makes an independent new key through fresh key establishment.
- **Key update** derives the new key from the old one, and is **disallowed**
  ([sp-800-57pt1 §8.2.3.1–8.2.3.2](sources.md#sp-800-57pt1),
  [sp-800-133r3-ipd §5.5](sources.md#sp-800-133r3-ipd)).

**For us: epoch n+1 keys must never be derived from epoch n keys.** Every rekey runs a
fresh key exchange (already the plan in [Rekeying](rekeying.md)). This should become an
explicit rule.

## Before keys are used

Key-agreement output shall not be used to protect and send data until the parties have
assurance of:
- the correct identifiers
- the keys being bound to the right entities
- correctly derived keys, for example by key confirmation

([sp-800-57pt1 §8.1.5.2.3](sources.md#sp-800-57pt1)). Shared secrets are not used
directly as keying material ([sp-800-57pt1 §8.1.5.3.3](sources.md#sp-800-57pt1)). This
favours an explicit key-confirmation step
([Q5](open-questions.md#q5-implicit-or-explicit-key-confirmation)). It is written for
SP 800-56A/B key agreement, but applies by analogy.

## Disposal

Shared secrets are destroyed as soon as they are no longer needed for derivation;
RBG seeds and intermediate results immediately after use
([sp-800-57pt1 §5.3.7](sources.md#sp-800-57pt1)). This backs crypto rule 5.

## Related pages

- [Rekeying](rekeying.md)
- [Key derivation](key-derivation.md)
- [Random bit generation](random-bit-generation.md)
- [SP 800-57 summary](src-sp-800-57pt1.md)
- [SP 800-133r3 summary](src-sp-800-133r3-ipd.md)
