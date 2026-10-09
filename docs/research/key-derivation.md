# Key Derivation

**Summary**: How a key-exchange shared secret may be turned into session keys under NIST
guidance: the approved constructions, how the inputs (secret, salt, labels) work, how
our per-direction keys and a pre-shared key would map onto them, and where Ascon stands.

**Sources**: [sp-800-56c](sources.md#sp-800-56c), [sp-800-227](sources.md#sp-800-227), [sp-800-232](sources.md#sp-800-232), [fips-203](sources.md#fips-203), [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd)

**Last updated**: 2026-10-08

---

## When derivation is needed

A KEM outputs a key that is ready to use. Shorter keys may be made by truncating it or
splitting it into non-overlapping pieces. Longer or multiple keys need an approved
key-derivation method (KDM) from SP 800-108, SP 800-56C or SP 800-133
([sp-800-227 §4.3](sources.md#sp-800-227), [fips-203 §3.3](sources.md#fips-203)).

We always derive (CONTRIBUTING.md crypto rule 9), because we need two 128-bit Ascon
keys, one per direction, with distinct labels. Splitting the 32-byte secret in half
would technically give two 16-byte keys, but it would skip the labels and any binding
to the handshake transcript.

## The approved constructions

### One-step ([sp-800-56c §4.1](sources.md#sp-800-56c))

`K(i) = H(counter ‖ Z ‖ FixedInfo)`. The counter is a 4-byte big-endian integer starting
at 1, and H is one of:

| Option | H |
|--------|---|
| 1 | An approved hash: SHA-2 (FIPS 180) or SHA-3 (FIPS 202) |
| 2 | HMAC with an approved hash, keyed by a salt |
| 3 | KMAC128 or KMAC256, keyed by a salt, customization string "KDF" |

### Two-step: extract then expand ([sp-800-56c §5.1](sources.md#sp-800-56c))

1. **Extract:** HMAC-hash or AES-CMAC, keyed by the salt, over Z. This gives a
   key-derivation key.
2. **Expand:** an SP 800-108 PRF-based KDF, using the same HMAC-hash (or AES-128-CMAC
   after a CMAC extract).

SP 800-56C notes that RFC 5869 **HKDF** is a version of this procedure, using HMAC for
both steps ([sp-800-56c §5.1](sources.md#sp-800-56c)). KMAC is not used in the two-step
method ([sp-800-56c §8.3](sources.md#sp-800-56c)).

### Several keys from one secret ([sp-800-56c §5.3](sources.md#sp-800-56c))

One extraction may feed several expansions, which is exactly our pattern of one key per
direction. The conditions:
- every expansion uses the same KDF mode and PRF
- the FixedInfo values are pairwise distinct (our direction labels)
- no derived key is output until all of them have been computed successfully

## Inputs

| Input | Role | Our use |
|-------|------|---------|
| Z | The shared secret: a KEM's shared secret takes its place ([sp-800-227 §4.3](sources.md#sp-800-227)) | ML-KEM or X25519 output |
| Salt | MAC key for HMAC/KMAC/extract. May be secret or public; the default is all zeros ([sp-800-56c §4.1, §8.2](sources.md#sp-800-56c)) | A possible slot for the pre-shared key |
| FixedInfo | Context: Label (key purpose), Context (identifiers, transcript), L (output length) ([sp-800-56c §5.1](sources.md#sp-800-56c)) | Direction label, mode, epoch, handshake transcript |
| T | An auxiliary shared secret appended as Z′ = Z ‖ T ([sp-800-56c §2, App. A.3](sources.md#sp-800-56c)) | Another possible slot for the pre-shared key |

**Combining keys** (concatenation, XOR or HMAC extraction) is stricter in the
SP 800-133 Rev. 3 draft: every component must be independent and come from an approved
method ([sp-800-133r3-ipd §5.3](sources.md#sp-800-133r3-ipd)). See
[Key management](key-management.md#generating-and-combining-keys).

**Mixing in a pre-shared key.** SP 800-56C explicitly allows an extra agreed secret as
T in Z′ = Z ‖ T, as the salt, or inside FixedInfo
([sp-800-56c App. A.3](sources.md#sp-800-56c)). This is the approved form of the
"PSK mixed into the KDF" option in [Handshake authentication](decisions/handshake-auth.md).

## Strength and handling rules

- The hash output length must be at least the targeted security strength. KMAC128 and
  AES-CMAC support at most 128 bits ([sp-800-56c §7](sources.md#sp-800-56c)). For our
  128-bit target, SHA-256, SHA3-256, KMAC128 and AES-128-CMAC all qualify.
- Derived material must be computed in full before any of it is output
  ([sp-800-56c §4.1, §5.3](sources.md#sp-800-56c)).
- The key-derivation key and local copies of Z must be destroyed after use, on every
  exit path ([sp-800-56c §5.1, §8.4](sources.md#sp-800-56c)). This is our crypto rule 5.
- **Byte order:** the one-step counter and the encoded length L are big-endian
  ([sp-800-56c §4.1](sources.md#sp-800-56c)), while Ascon is little-endian
  ([sp-800-232 App. A](sources.md#sp-800-232)). The protocol spec must pin each field.

## Where Ascon stands

- SP 800-56C only lists SHA-2/SHA-3-based hashes, HMAC, KMAC and AES-CMAC
  ([sp-800-56c §4.1, §5.1, §7](sources.md#sp-800-56c)). It predates Ascon.
- SP 800-232 approves Ascon-XOF128 and -CXOF128 as XOFs, but says their approved uses
  "will be specified in other NIST publications", and that HMAC over any Ascon function
  is not approved ([sp-800-232 §6](sources.md#sp-800-232)).
- The SP 800-133 Rev. 3 draft lists Ascon-XOF as an approved XOF, but **only for seed
  expansion** in key-pair generation. Its list of approved key-derivation methods still
  contains only hash, HMAC, AES-CMAC and KMAC constructions
  ([sp-800-133r3-ipd §4.2.2, §5.2](sources.md#sp-800-133r3-ipd)).
- **So, as of these sources, an Ascon-based KDF is not an approved construction.** It
  may well be secure, but results would have to say "non-approved KDF". See
  [KDF construction](decisions/kdf-construction.md).

## What we have available

| Primitive | In our build? |
|-----------|---------------|
| Ascon-XOF128 / CXOF128 / Hash256 | Yes: ascon-c `asconaeadxof128` |
| SHA3-256, SHAKE | Only as ml-kem's fetched C++ header-only dependency. We may not manage it, and calling it from C would need a second C++ shim (CONTRIBUTING.md forbids both) |
| SHA-256, HMAC, KMAC | No |

## Related pages

- [KDF construction](decisions/kdf-construction.md)
- [Hybrid mode](decisions/hybrid-mode.md)
- [Handshake authentication](decisions/handshake-auth.md)
- [Ascon](ascon.md)
- [SP 800-56C summary](src-sp-800-56c.md)
