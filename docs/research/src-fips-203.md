# FIPS 203 (ML-KEM Standard)

**Summary**: Source summary of FIPS 203, NIST's ML-KEM standard (August 2024): what it
specifies, which sections matter to this project, and which wiki pages it fed.

**Sources**: [fips-203](sources.md#fips-203)

**Last updated**: 2026-10-08

---

## What it is

FIPS 203 is NIST's standard for ML-KEM, a module-lattice key-encapsulation
mechanism derived from CRYSTALS-Kyber. It was published and took effect on
August 13, 2024. It specifies three algorithms (KeyGen, Encaps, Decaps), three
parameter sets (ML-KEM-512, -768 and -1024), and the requirements a conforming
implementation must meet. NIST will re-evaluate it every five years
([fips-203 Abstract, Announcement ¶13](sources.md#fips-203)).

It defers to **SP 800-227** for how to *use* a KEM securely in an application, and to
**SP 800-108 / SP 800-56C** for key derivation
([fips-203 §1.1, §3.3](sources.md#fips-203)).

## Sections that matter to us

| Section | Content | Used in |
|---------|---------|---------|
| §3.1–3.2 | KEM model, MLWE assumption, FO transform, decapsulation failure rates (Table 1) | [ML-KEM](ml-kem.md) |
| §3.3 | Implementation requirements: K-PKE never used alone, internal functions for testing only, approved use of the shared secret, randomness, input checking, destroying intermediate values, no floating point | [ML-KEM](ml-kem.md), [KDF construction](decisions/kdf-construction.md), [Hybrid mode](decisions/hybrid-mode.md) |
| §6 | Internal (seed-taking) algorithms; implicit rejection in Decaps | [ML-KEM](ml-kem.md) |
| §7 | KeyGen / Encaps / Decaps, key-pair checks, encapsulation-key and decapsulation input checks | [ML-KEM](ml-kem.md) |
| §8 | Parameter sets (Table 2), sizes (Table 3), security categories, ML-KEM-768 as recommended default | [ML-KEM](ml-kem.md), [Radio link](decisions/radio-link.md) |
| App. C | Differences from Kyber: fixed 256-bit shared secret, different FO variant, no hashing of `m`, explicit input checks | [ML-KEM](ml-kem.md) |

§2, §4 and §5 (notation, NTT, sampling, K-PKE internals) are needed to *implement*
ML-KEM. We don't implement primitives (crypto rule 1), so they were only skimmed.

## Takeaways for this project

1. Our `kem.h` maximum sizes match the ML-KEM-1024 row of Table 3 exactly. An ML-KEM-768
   handshake carries 2272 bytes of key-exchange payload (estimate from Table 3).
2. Implicit rejection means tampering with the ciphertext is silent at the KEM layer.
   The handshake needs key confirmation.
3. The required input checks are covered by our exact-length checks plus the library's
   modulus check.
4. Passing seeds from `rng.c` into the shim is consistent with the standard, provided
   `src/crypto/` is treated as the cryptographic module.
5. Using the shared secret directly is allowed, but any derivation must follow
   SP 800-108/56C. That bears on the KDF decision.
6. Combining KEMs needs care (SP 800-227). That bears on the hybrid-mode decision.

## Extraction notes

Extracted with docling. The prose and both size tables came out cleanly. Several
formulas came out as `<!-- formula-not-decoded -->` (mostly §3.2 and §4.1). None were
needed, because the size formulas are restated in the algorithm headers of §6 and
cross-check against Table 3.

## Related pages

- [ML-KEM](ml-kem.md)
- [SP 800-227 summary](src-sp-800-227.md): how to use a KEM (FIPS 203 defers to it)
- [SP 800-56C summary](src-sp-800-56c.md): key derivation (FIPS 203 §3.3 points here)
