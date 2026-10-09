# Random Bit Generation

**Summary**: What NIST means by an "approved RBG" (SP 800-90A/B/C), why Linux
`getrandom()` is not one as deployed, how to state that deviation, and what a
conforming `rng.c` backend would need.

**Sources**: [sp-800-90a](sources.md#sp-800-90a), [sp-800-90b](sources.md#sp-800-90b), [sp-800-90c](sources.md#sp-800-90c), [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd), [fips-203](sources.md#fips-203), [fips-204](sources.md#fips-204), [sp-800-232](sources.md#sp-800-232)

**Last updated**: 2026-10-08

---

## Why it matters

Every standard we use requires randomness from an "approved RBG" (random bit
generator) of sufficient strength:

| Use | Required strength | Source |
|-----|-------------------|--------|
| ML-KEM-768 seeds `d`, `z`, `m` | ≥ 192 bits | [fips-203 §3.3, Table 2](sources.md#fips-203) |
| ML-DSA-65 key-generation seed | ≥ 192 bits | [fips-204 §3.6.1](sources.md#fips-204) |
| Ascon-AEAD128 keys | ≥ 128 bits | [sp-800-232 R1](sources.md#sp-800-232) |

Our `rng.c` uses Linux `getrandom()` (CONTRIBUTING.md crypto rule 3).

## What an "approved RBG" is

Three documents together define it:

1. **SP 800-90A: the DRBG mechanism.** One of Hash_DRBG, HMAC_DRBG or CTR_DRBG,
   instantiated at 112, 128, 192 or 256 bits. Output strength is capped by the
   instantiated strength ([sp-800-90a §8.4, §10](sources.md#sp-800-90a)).
2. **SP 800-90B: the entropy source.** A noise source, optional conditioning, and
   start-up and continuous health tests. It must be validated by an accredited lab and
   CMVP ([sp-800-90b §2.2, §3, §4.3–4.4](sources.md#sp-800-90b)).
3. **SP 800-90C: the construction.** How 1 and 2 are put together: RBG1, RBG2 (physical
   or non-physical entropy), RBG3 (full entropy) or RBGC (a tree of DRBGs)
   ([sp-800-90c §2.2, Table 1](sources.md#sp-800-90c)).

Plus **a FIPS 140-validated module**: keys are to be generated inside a validated module
that contains its RBG ([sp-800-133r3-ipd §2.2](sources.md#sp-800-133r3-ipd); a draft).

**Operating-system RBGs** are anticipated by the RBGC construction, which explicitly
lets an OS RBG be designed and validated independently of the hardware
([sp-800-90c §7](sources.md#sp-800-90c)). Its root must be seeded from a validated
RBG2, RBG3 or full-entropy source, and every component must be validated
([sp-800-90c §7.1.2.1, §7.3.1](sources.md#sp-800-90c)). **None of these documents says
that any particular OS RNG qualifies.** None mentions Linux or `getrandom()`.

## Key numbers

All **spec**.

| Item | Value | Source |
|------|-------|--------|
| DRBG strengths | 112, 128, 192, 256 bits | [sp-800-90a §8.4](sources.md#sp-800-90a) |
| Entropy to instantiate at strength s | ≥ 3s/2 bits (288 bits for s = 192) | [sp-800-90c §2.6, §5.3](sources.md#sp-800-90c) |
| Entropy to reseed | ≥ s bits | [sp-800-90c §5.3](sources.md#sp-800-90c) |
| Hash/HMAC/CTR_DRBG (AES) reseed interval | 2^48 requests | [sp-800-90a Tables 2–3](sources.md#sp-800-90a) |
| Max per request | 2^19 bits (64 KiB) | [sp-800-90a Tables 2–3](sources.md#sp-800-90a) |
| RBG2 / RBG3(XOR) forced reseed | after 2^17 output bits | [sp-800-90c §5.2.3, §6.4.2](sources.md#sp-800-90c) |
| Output limit per RBG | 2^64 bits | [sp-800-90c §2.1](sources.md#sp-800-90c) |

SP 800-90C also asks implementations to make generate calls atomic and to request only
the bits needed right now, rather than storing random bits for later
([sp-800-90c §2.7](sources.md#sp-800-90c)). Our `rng.c` already generates `d`, `z` and
`m` just in time.

## Where `getrandom()` stands

**Not an approved RBG as deployed:** it is not validated against SP 800-90A/B/C, and
does not run inside a FIPS 140-validated module. Mainline Linux uses a
ChaCha20/BLAKE2s design rather than an SP 800-90A DRBG *(outside knowledge, needs
source)*. Some vendor kernels and FIPS-validated modules add an SP 800-90A DRBG and a
validated entropy source *(outside knowledge, needs source)*.

**Proposed statement** for `docs/threat_model.md`
([Q7](open-questions.md#q7-how-do-we-state-the-getrandom-deviation)):

> All randomness (ML-KEM seeds `d`, `z`, `m`; X25519 private keys; any PSK we
> generate) comes from Linux `getrandom()`. This RNG is not validated against
> SP 800-90A/B/C and does not run inside a FIPS 140-validated module, so it is not an
> "approved RBG" as FIPS 203 §3.3, SP 800-227 and SP 800-232 R1 require. We assume its
> output is computationally indistinguishable from random at ≥ 192-bit strength once the
> kernel pool is initialised. This is a conformance limitation, not a known weakness.
> It is identical across all secured modes, so it does not bias the comparison. A
> conforming deployment would replace `rng.c`'s backend with a validated RBG (SP 800-90C
> RBG2 or RBGC) at ≥ 192 bits.

## A conforming backend

`rng.c` is already the only source of randomness, so a conforming backend would be a
one-file change: call a FIPS-validated library's SP 800-90A DRBG, instantiated at
≥ 192 bits from a validated entropy source. Whether that is in scope depends on
[Q1](open-questions.md#q1-how-nist-compliant-must-the-project-be).

## Related pages

- [ML-KEM](ml-kem.md#randomness-and-the-internal-functions)
- [Key management](key-management.md)
- [SP 800-90 summary](src-sp-800-90.md)
