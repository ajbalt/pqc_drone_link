# SP 800-56C Rev. 2 (Key-Derivation Methods)

**Summary**: Source summary of NIST SP 800-56C Revision 2 (August 2020): the approved
ways to turn a key-establishment shared secret into keys. It contains the evidence
that decides our KDF choice.

**Sources**: [sp-800-56c](sources.md#sp-800-56c)

**Last updated**: 2026-10-08

---

## What it is

The recommendation FIPS 203 points to for deriving keys from a shared secret
([fips-203 §3.3](sources.md#fips-203)). It was written for secrets from SP 800-56A/B
schemes (Diffie-Hellman, RSA); SP 800-227 extends it to KEM shared secrets
([sp-800-227 §4.3](sources.md#sp-800-227)). It predates the Ascon standard.

## Sections that matter to us

| Section | Content | Used in |
|---------|---------|---------|
| §2, App. A.3 | Hybrid shared secret Z′ = Z ‖ T, where T is an extra agreed secret | [Key derivation](key-derivation.md), [Handshake authentication](decisions/handshake-auth.md) |
| §4.1 | One-step KDF: hash, HMAC or KMAC over counter ‖ Z ‖ FixedInfo | [Key derivation](key-derivation.md) |
| §5.1 | Two-step extract-then-expand (corresponds to HKDF) | [Key derivation](key-derivation.md), [KDF construction](decisions/kdf-construction.md) |
| §5.3 | One extraction, several expansions: one per key, with distinct FixedInfo | [Key derivation](key-derivation.md) |
| §7 | Selecting hash and MAC functions by security strength | [Key derivation](key-derivation.md) |
| §8.2, §8.4 | Choosing the salt; destroying local copies of secrets | [Key derivation](key-derivation.md) |

## Takeaways for this project

1. Approved KDF building blocks are SHA-2/SHA-3 hashes, HMAC, KMAC and AES-CMAC. Ascon
   is not among them.
2. HKDF-SHA256 corresponds to the approved two-step method, and §5.3 allows exactly our
   "one secret, several per-direction keys" pattern.
3. A pre-shared key has an approved way in: as the salt, or as the extra secret T.
4. Secrets and intermediates must be destroyed on every exit path (§8.4). This is our
   crypto rule 5.

## Extraction notes

Extracted with docling; no formulas lost, and Tables 1–5 are clean.

## Related pages

- [Key derivation](key-derivation.md)
