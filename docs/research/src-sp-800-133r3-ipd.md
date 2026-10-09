# SP 800-133 Rev. 3 (Draft): Key Generation

**Summary**: Source summary of NIST SP 800-133 Rev. 3 **initial public draft** (April
2026) on how cryptographic keys are generated, derived and combined. **Draft: confirm
claims against the final before relying on them.**

**Sources**: [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd)

**Last updated**: 2026-10-08

---

> **Draft.** This is an initial public draft (comments closed June 16, 2026). The
> current final is SP 800-133 Rev. 2 (2020) *(outside knowledge; needs source)*.
> Wording may change in the Rev. 3 final.

## What it is

NIST's recommendation on generating keys: from RBG output, for key pairs (including
KEM keys), for symmetric keys established by key agreement or KEMs, by derivation, and
by combining keys.

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| §2.1–2.3 | Every key comes, directly or indirectly, from an approved RBG; FIPS 140 modules; strength limits | [Key management](key-management.md), [Random bit generation](random-bit-generation.md) |
| §4.1.2, §4.2 | KEM key pairs; seed derivation and expansion (lists Ascon-XOF as an approved XOF for seed expansion) | [Key management](key-management.md) |
| §5.2–5.2.2 | KEM shared secrets as symmetric keys; approved KDMs | [Key management](key-management.md), [Key derivation](key-derivation.md) |
| §5.3 | Combining keys: concatenation, XOR, HMAC extraction | [Key management](key-management.md#generating-and-combining-keys) |
| §5.5 | Replacing keys: must be independent of the old key | [Key management](key-management.md#rekeying-not-key-update) |

## Extraction notes

docling failed silently on this file, so it was extracted with **pdfplumber**. Line
numbers are mixed into the text, and page numbers come from the table of contents. The
draft has a small internal inconsistency: §5 refers to combining keys as "Section 5.4",
but it is §5.3.

## Related pages

- [Key management](key-management.md)
