# Handshake Authentication

**Summary**: How the drone and ground station know they are talking to each other and
not to an attacker in the middle: a pre-shared key mixed into the KDF (the prototype
default), ML-DSA signatures, or authentication through static KEM keys.

**Sources**: [sp-800-227](../sources.md#sp-800-227), [sp-800-56c](../sources.md#sp-800-56c), [fips-204](../sources.md#fips-204), [sp-800-133r3-ipd](../sources.md#sp-800-133r3-ipd), [expressvpn-pq-wireguard-guide](../sources.md#expressvpn-pq-wireguard-guide)

**Last updated**: 2026-10-08

**Status**: Open

**Decision**: —

---

## Why it matters

A KEM on its own gives no authentication. The key exchange **must** have an
application-appropriate form of authentication or integrity (RM5,
[sp-800-227 §4.2](../sources.md#sp-800-227)). Without it, an attacker can run one
handshake with the drone and another with the ground station, and relay between them.

The choice also changes what goes on air. On a slow radio, signatures can cost more
than ML-KEM itself. It must be the same for `x25519` and `mlkem768`, so that the key
exchange stays the only variable.

## Options

| Option | How | Extra bytes per handshake | For | Against |
|--------|-----|---------------------------|-----|---------|
| **Pre-shared key (PSK) in the KDF** | Both ends hold a secret installed before the mission. It enters key derivation as the salt, or as T in Z′ = Z ‖ T ([sp-800-56c §4.1, App. A.3](../sources.md#sp-800-56c)) | **0** | Cheapest; authenticates both sides at once; no new dependency; identical in every mode | Keys must be provisioned and protected on the drone; a captured drone's PSK may expose others unless each drone has its own ([Q13](../open-questions.md#q13-one-psk-per-drone-or-per-fleet)) |
| **ML-DSA signatures** | Sign the transcript ([sp-800-227 §5.2.3](../sources.md#sp-800-227)) | +2420 to +5261 for one signer; about double for mutual (see [ML-DSA](../ml-dsa.md#what-it-would-cost-our-handshake)) | Standard pattern; per-device identity; no shared secret to leak | 2–4× the handshake bytes, paid again on every rekey; variable signing time; needs a new library ([Q12](../open-questions.md#q12-where-would-an-ml-dsa-library-come-from)); shrinks the visible ML-KEM vs X25519 difference |
| **Static KEM keys** | Each side also holds a long-term ML-KEM key; authentication comes from the peer proving it can decapsulate ([sp-800-227 §5.2.4–5.2.5](../sources.md#sp-800-227)) | About +1088 per authenticated side (one extra ciphertext) | Smaller than any signature; PQ authentication | More complex handshake; static keys need trusted distribution (RM3) |

([sp-800-227 §4.2, §5.2](../sources.md#sp-800-227), [fips-204 Table 2](../sources.md#fips-204))

## Evidence

- **Authentication is required:** RM5 ([sp-800-227 §4.2](../sources.md#sp-800-227)).
- **A PSK has an approved route into the KDF**, as salt or as T
  ([sp-800-56c App. A.3](../sources.md#sp-800-56c)). The draft SP 800-133r3 is stricter
  about *combining keys*: each component must come from an approved method
  ([sp-800-133r3-ipd §5.3](../sources.md#sp-800-133r3-ipd)). A PSK we generate with
  `getrandom()` inherits the [Q7](../open-questions.md#q7-how-do-we-state-the-getrandom-deviation)
  deviation.
- **Who signs.** In SP 800-227's example the *encapsulator* signs, which in our flow is
  the drone. For the ground station to sign a transcript that includes the ciphertext,
  the handshake needs a third message, adding a round trip. Alternatively, it signs
  only its encapsulation key in message 1.
- **One signature authenticates one side.** A command link needs both directions
  trusted ([Q11](../open-questions.md#q11-mutual-or-one-way-authentication)), so
  mutual signatures double the bytes. A PSK covers both at once.
- **Key confirmation interacts with this.** With a PSK, a verified confirmation proves
  both key agreement *and* PSK possession; see
  [Q5](../open-questions.md#q5-implicit-or-explicit-key-confirmation).
- **Industry practice (low-trust source).** One VPN vendor uses a PSK delivered over a
  post-quantum TLS channel as its post-quantum measure
  ([expressvpn-pq-wireguard-guide](../sources.md#expressvpn-pq-wireguard-guide); see the
  [trust warning](../src-expressvpn-pq-wireguard-guide.md)). It is not a design reference.
- *Still needed:* a reviewed protocol that combines a PSK with a KEM inside the
  handshake (WireGuard whitepaper; Hülsing et al. 2021), and ML-DSA CPU and energy cost
  on the target hardware.

## Recommendation (tentative)

**PSK mixed into the KDF** for the main comparison:
- it costs 0 bytes, against +106–332% for ML-DSA
- it gives mutual authentication for free
- it needs no new dependency
- it is identical across all modes, so the key exchange stays the only variable

ML-DSA, or static-KEM authentication, could be a secondary experiment. Settle
[Q11](../open-questions.md#q11-mutual-or-one-way-authentication) (mutual?) and
[Q13](../open-questions.md#q13-one-psk-per-drone-or-per-fleet) (PSK scope) before
deciding.

## Related pages

- [ML-DSA](../ml-dsa.md)
- [KEM usage](../kem-usage.md)
- [Key derivation](../key-derivation.md)
- [KDF construction](kdf-construction.md)
