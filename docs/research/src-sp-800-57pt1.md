# SP 800-57 Part 1 Rev. 5 (Key Management)

**Summary**: Source summary of NIST SP 800-57 Part 1 Rev. 5 (May 2020), the general
key-management recommendation: security strengths, cryptoperiods, key states, re-keying,
and key-establishment assurances.

**Sources**: [sp-800-57pt1](sources.md#sp-800-57pt1)

**Last updated**: 2026-10-08

---

## What it is

NIST's broad guide to managing keys over their lifetime. It predates the post-quantum
standards, so it does not cover ML-KEM, ML-DSA or the security categories, but its
general rules still apply.

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| §5.3, Table 1 | Cryptoperiods by key type; factors that shorten them; disposal | [Key management](key-management.md#cryptoperiods), [Rekeying](rekeying.md) |
| §5.6, Tables 2–4 | Comparable strengths; weakest-link rule; strengths acceptable by year | [Key management](key-management.md#security-strength) |
| §8.1.5.2–8.1.5.3 | Assurances required before using key-agreement output | [Key management](key-management.md#before-keys-are-used) |
| §8.2.3 | Re-keying vs key update (key update disallowed) | [Key management](key-management.md#rekeying-not-key-update) |

## Extraction notes

Extracted with docling (645 KB, the largest source so far); no formulas lost. Only the
sections above were read in detail.

## Related pages

- [Key management](key-management.md)
