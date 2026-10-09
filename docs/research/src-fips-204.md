# FIPS 204 (ML-DSA Standard)

**Summary**: Source summary of FIPS 204, NIST's ML-DSA signature standard (August
2024): what it specifies and which pages it fed.

**Sources**: [fips-204](sources.md#fips-204)

**Last updated**: 2026-10-08

---

## What it is

NIST's standard for ML-DSA, a module-lattice signature scheme derived from
CRYSTALS-Dilithium. It specifies three parameter sets, hedged and deterministic
signing, a pre-hash variant, and implementation requirements. It is relevant to us only
as the signature option for [Handshake authentication](decisions/handshake-auth.md).

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| §3.4–3.6 | Hedged vs deterministic signing; randomness; input checks; destroying intermediates; no floating point | [ML-DSA](ml-dsa.md) |
| §4, Tables 1–2 | Parameter sets, categories, sizes, expected signing loops | [ML-DSA](ml-dsa.md#sizes-and-categories) |
| §5.2–5.4 | Signing API, context string, pure vs HashML-DSA | [ML-DSA](ml-dsa.md#signing) |
| App. C | Minimum signing-loop cap | [ML-DSA](ml-dsa.md#signing) |

§6–7 (internals, arithmetic) were skimmed, since we don't implement primitives.

## Extraction notes

Extracted with docling. Table 2 (sizes) is clean. None of the 16 undecoded formulas is
in a table we use.

## Related pages

- [ML-DSA](ml-dsa.md)
