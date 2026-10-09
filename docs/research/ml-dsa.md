# ML-DSA

**Summary**: NIST's post-quantum signature scheme (FIPS 204), the candidate for
signature-based handshake authentication: sizes, security categories, signing modes,
randomness and implementation rules, and what it would add to our handshake.

**Sources**: [fips-204](sources.md#fips-204), [sp-800-227](sources.md#sp-800-227)

**Last updated**: 2026-10-08

---

## Sizes and categories

All values are **spec** ([fips-204 Table 1, Table 2](sources.md#fips-204)).

| Parameter set | Public key | Private key | Signature | Security category |
|---------------|-----------:|------------:|----------:|:-----------------:|
| ML-DSA-44 | 1312 B | 2560 B | 2420 B | 2 |
| ML-DSA-65 | 1952 B | 4032 B | 3309 B | 3 |
| ML-DSA-87 | 2592 B | 4896 B | 4627 B | 5 |

ML-DSA-65 is the category-3 match for ML-KEM-768. A private key can be stored as just
its 32-byte seed, which must then be protected like the key itself
([fips-204 §3.6.3, §4](sources.md#fips-204)).

## What it would cost our handshake

Extra on-air bytes if the handshake is signed, compared with the 2272-byte ML-KEM-768
payload. A pre-shared key adds 0. All **estimates** from Table 2:

| Option | Extra bytes | Handshake total | × ML-KEM-768 alone |
|--------|------------:|----------------:|-------------------:|
| ML-DSA-44, verification key pre-installed | 2420 | 4692 | 2.1× |
| ML-DSA-44, key sent | 3732 | 6004 | 2.6× |
| ML-DSA-65, key pre-installed | 3309 | 5581 | 2.5× |
| ML-DSA-65, key sent | 5261 | 7533 | 3.3× |
| Mutual (both sign), ML-DSA-65, keys pre-installed | 6618 | 8890 | 3.9× |

Every rekey pays these bytes again, because rekeys run a fresh handshake (see
[Rekeying](rekeying.md)). On slow links, signatures would dominate the measured cost
and hide the ML-KEM vs X25519 difference we are trying to measure. See
[Handshake authentication](decisions/handshake-auth.md).

## Signing

- **Hedged (the default):** each signature mixes 32 fresh random bytes (`rnd`) with a
  secret seed ([fips-204 §3.4, §5.2](sources.md#fips-204)).
- **Deterministic** (`rnd` all zeros) is allowed. It **should not** be used where
  side-channel or fault attacks are a concern and can't otherwise be mitigated
  ([fips-204 §3.4](sources.md#fips-204)). A drone that could be captured is arguably
  such a platform.
- **Context string:** an optional string of up to 255 bytes, bound into the signature.
  It could carry domain separation, such as a protocol name and version
  ([fips-204 §5.2](sources.md#fips-204)).
- **Pure vs pre-hash:** "pure" ML-DSA is preferred. The pre-hash variant (HashML-DSA) is
  for very large messages, and our transcript is about 2.3 KB. One key pair should not
  be used for both ([fips-204 §5.4](sources.md#fips-204)).
- **Variable signing time.** Signing loops a random number of times: on average 4.25,
  5.1 and 3.85 times for -44, -65 and -87. Any loop cap must be at least 814
  ([fips-204 Table 1, App. C](sources.md#fips-204)). Signing time varies from run to run,
  which widens the spread of handshake-latency measurements.

## Randomness

- The key-generation seed (32 bytes) **shall** come from an approved RBG of at least
  192-bit strength for ML-DSA-65; for ML-DSA-44, 128 bits is required and 192
  recommended.
- `rnd` **should** come from an approved RBG, though even a weak RBG is better than
  deterministic signing.

([fips-204 §3.6.1](sources.md#fips-204)). This is the same deviation as for ML-KEM; see
[Random bit generation](random-bit-generation.md).

## Implementation rules

- Verification **shall** return false for a wrong-length signature or public key, or a
  badly encoded hint ([fips-204 §3.6.2, §6.3](sources.md#fips-204)). This matches crypto
  rule 7.
- Intermediate values **shall** be destroyed as soon as they are no longer needed
  ([fips-204 §3.6.3](sources.md#fips-204)). This matches crypto rule 5.
- No floating-point arithmetic ([fips-204 §3.6.4](sources.md#fips-204)).
- Verification needs only the public key, message, signature and context. But the
  public key must be **bound to an identity** (a certificate, or other assurance of
  identity and of possession), which FIPS 204 leaves to other documents
  ([fips-204 §1.1, §3.5](sources.md#fips-204)).
- A signing key pair **shall not** be used for any other purpose
  ([fips-204 Announcement ¶8](sources.md#fips-204)), so a PSK must never be derived from
  it.

## Using it in our handshake

FIPS 204 gives no key-establishment guidance. SP 800-227's signed-handshake example has
the **encapsulator** sign the transcript
([sp-800-227 §5.2.3](sources.md#sp-800-227)), and in our flow the encapsulator is the
drone. For the ground station to sign a transcript that includes the ciphertext, the
handshake needs a third message. Adding ML-DSA would also need a new library, which
runs into the one-C++-file rule ([Q12](open-questions.md#q12-where-would-an-ml-dsa-library-come-from)).

## Related pages

- [Handshake authentication](decisions/handshake-auth.md)
- [FIPS 204 summary](src-fips-204.md)
- [ML-KEM](ml-kem.md)
