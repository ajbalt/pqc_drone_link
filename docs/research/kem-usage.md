# KEM Usage

**Summary**: The rules for using a KEM such as ML-KEM safely inside our handshake:
single-use ephemeral keys, the need for authentication, key confirmation, input
checking and failure handling.

**Sources**: [sp-800-227](sources.md#sp-800-227), [fips-203](sources.md#fips-203), [fips-204](sources.md#fips-204), [sp-800-57pt1](sources.md#sp-800-57pt1)

**Last updated**: 2026-10-08

---

## Ephemeral keys are single-use

If an application uses an ephemeral key pair, it **shall** be used for exactly one key
establishment and destroyed as soon as possible afterwards (RS6,
[sp-800-227 §4.2](sources.md#sp-800-227)).

For us: every handshake *and every mid-mission rekey* runs a fresh ML-KEM KeyGen on
the responder. That KeyGen belongs inside the measured rekey time. See
[Rekeying](rekeying.md).

## A KEM alone is not authenticated

The key exchange needs an application-appropriate form of authentication or integrity
(RM5, [sp-800-227 §4.2](sources.md#sp-800-227)). Typically this means extra elements
such as signatures or certificates ([sp-800-227 §4.1](sources.md#sp-800-227)).
Otherwise an active attacker can simply run the KEM with each side. If signatures are
used, see [ML-DSA](ml-dsa.md); a signing key must itself be bound to an identity
([fips-204 §3.5](sources.md#fips-204)). How we provide it
is the [Handshake authentication](decisions/handshake-auth.md) decision.

If a *static* (long-term) encapsulation key is used, the encapsulator must have
assurance of who owns it (RM3, [sp-800-227 §4.2](sources.md#sp-800-227)). Our
handshake uses ephemeral keys, so this applies only if that changes.

## Key confirmation

Key confirmation proves to one side that the other derived the same key. SP 800-227
says it **should** be used, but it is not required
([sp-800-227 §4.4](sources.md#sp-800-227)). It matters for ML-KEM in particular,
because implicit rejection makes a tampered ciphertext silent (see
[ML-KEM](ml-kem.md#implicit-rejection-decapsulation-never-reports-tampering)).

There are two ways to get it:

| | Implicit: the first AEAD packet | Explicit: a MAC over MAC_Data |
|---|---|---|
| How | A packet that decrypts and verifies under the new key proves the peer has it ([sp-800-227 §4.4](sources.md#sp-800-227)) | The provider sends a MAC tag over MAC_Data = KC_Step_Label ‖ ID_P ‖ ID_R ‖ Eph_P ‖ Eph_R ‖ Extra_P ‖ Extra_R ([sp-800-227 §4.4.1](sources.md#sp-800-227)) |
| NIST status | Named as an example, but "this recommendation makes no statement as to the adequacy of other methods" | The specified method |
| MAC | Ascon-AEAD128 (already in our build) | Must be an approved MAC: HMAC, AES-CMAC or KMAC (AES-GMAC also allowed) ([sp-800-227 App. C.1](sources.md#sp-800-227)). Not Ascon |
| Cost | No extra messages | An extra message (or two, for both directions), plus a MAC we don't have |

Details of the explicit form ([sp-800-227 §4.4.1–4.4.2](sources.md#sp-800-227)):
- KC_Step_Label is 6 bytes: "KC_1_E", "KC_2_E", "KC_1_D" or "KC_2_D" (first or second
  message; encapsulator or decapsulator as provider).
- Eph is the ciphertext (encapsulator) and the ephemeral encapsulation key
  (decapsulator).
- The KC key is a separate slice of the derived keying material, used only for
  confirmation and then destroyed (RS9).
- The minimum MAC tag is 64 bits ([sp-800-227 App. C.1](sources.md#sp-800-227)).

Even if we choose implicit confirmation, the MAC_Data layout is a ready-made template
for binding the transcript (identifiers, ciphertext, encapsulation key) into the KDF's
FixedInfo. Which approach to use is an [open question](open-questions.md).

## Input checking and failures

- Inputs must be checked as the KEM's standard specifies. An encapsulation key that is
  stored unmodifiably and already checked need not be checked again
  ([sp-800-227 §3.2](sources.md#sp-800-227)). For ML-KEM, see
  [ML-KEM input checking](ml-kem.md#input-checking).
- Failure information should not leak outside the crypto module, and constant-time
  side-channel protections are recommended. Embedded devices may lack enough
  side-channel protection ([sp-800-227 §3.3, §4.2](sources.md#sp-800-227)).
- Intermediate values must be destroyed before KeyGen, Encaps or Decaps returns
  ([sp-800-227 §3.2](sources.md#sp-800-227), [fips-203 §3.3](sources.md#fips-203)).
  This is our crypto rule 5.

## Security strength

The symmetric algorithms should be at least as strong as the KEM's target
([sp-800-227 §4.2](sources.md#sp-800-227)). Using ML-KEM-768 inside a protocol whose
symmetric parts are 128-bit, like our Ascon-AEAD128, is explicitly accommodated
([sp-800-227 App. D](sources.md#sp-800-227)). Overall session security is then
128 bits, by the weakest-link rule
([sp-800-57pt1 §5.6.2](sources.md#sp-800-57pt1)); see
[Key management](key-management.md#security-strength).

## Related pages

- [ML-KEM](ml-kem.md)
- [Handshake authentication](decisions/handshake-auth.md)
- [Rekeying](rekeying.md)
- [SP 800-227 summary](src-sp-800-227.md)
