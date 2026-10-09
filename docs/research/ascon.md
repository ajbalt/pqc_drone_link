# Ascon

**Summary**: Ascon-AEAD128, the authenticated cipher that protects every packet in the
secured modes: its sizes, the nonce and tag rules, per-key limits, byte order, and the
companion hash and XOF functions.

**Sources**: [sp-800-232](sources.md#sp-800-232), [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd)

**Last updated**: 2026-10-08

---

## Ascon-AEAD128 parameters

All values are **spec** ([sp-800-232 §4.1, Table 3](sources.md#sp-800-232)).

| Item | Size |
|------|------|
| Key | 128 bits (16 bytes) |
| Nonce | 128 bits (16 bytes) |
| Tag | 128 bits (16 bytes), may be truncated (see below) |
| Ciphertext | Same length as the plaintext, plus the tag |
| Associated data | Any length, including empty |
| Permutation state | 320 bits |
| Rate / capacity | 128 / 192 bits |
| Rounds | 12 at initialization and finalization, 8 per data block |
| Security | 128 bits (single key, nonces never repeated) |

It is based on ASCON-128a, with a new IV and **little-endian** byte order
([sp-800-232 §1, App. A](sources.md#sp-800-232)).

**Our build.** The pinned ascon-c commit (`446347f`, "update references to NIST SP
800-232 to final version") builds 16-byte keys, nonces and tags in
`crypto_aead_hash/asconaeadxof128/ref/api.h`. That matches the table above.

**On-air cost.** Encryption adds exactly the tag: 16 bytes per packet
([sp-800-232 §4.1](sources.md#sp-800-232)). The nonce doesn't have to be sent, because
our receiver can rebuild it from the epoch, direction and sequence number already in the
header (CONTRIBUTING.md crypto rule 4). Per-packet overhead is therefore our header plus
16 bytes (**estimate**).

## Nonces

- Nonces **shall** be distinct for every encryption under a given key (R3,
  [sp-800-232 §4.3](sources.md#sp-800-232)). Our nonce (key epoch + direction bit +
  64-bit packet counter, CONTRIBUTING.md crypto rule 4) fits in the 128-bit nonce.
- If a nonce and associated-data pair is reused, confidentiality can be lost. The
  design keeps limited integrity if any pair is repeated at most 2^8 times
  ([sp-800-232 §4.4.3, Tables 7–8](sources.md#sp-800-232)). This is a safety margin,
  not something to rely on.
- With separate keys per direction, reusing the same nonce under *different* keys is
  allowed. Security in the multi-key setting is 128 − log2(u) bits for u keys
  ([sp-800-232 §4.4.2](sources.md#sp-800-232)).
- An optional "nonce masking" mode uses a 256-bit key to restore full multi-key
  security ([sp-800-232 §4.2.2](sources.md#sp-800-232)). With only a few keys per
  mission, we don't need it.
- **Byte order:** Ascon is little-endian ([sp-800-232 App. A](sources.md#sp-800-232)).
  `docs/protocol_spec.md` must fix how the epoch, direction and counter are laid out in
  the 16 nonce bytes.

## Associated data

Associated data is authenticated but not encrypted. The integrity guarantee covers the
whole tuple (nonce, associated data, ciphertext, tag)
([sp-800-232 Table 2, §4.4.1](sources.md#sp-800-232)). That is exactly what our
"packet header as associated data" rule relies on.

## Tag truncation

- A truncated tag is the leftmost λ bits. λ **shall** be at least 32, a value below 64
  **shall** only be chosen after a careful risk analysis, and λ must stay fixed for the
  key's lifetime (R4, [sp-800-232 §4.2.1, §4.3](sources.md#sp-800-232)).
- Truncation lowers both confidentiality and integrity strength to λ bits
  ([sp-800-232 §4.4.1](sources.md#sp-800-232)).
- With λ < 64, the number of allowed decryption failures per key **should** be 1. In
  practice that means a rekey after a single bad packet, unless a risk analysis
  justifies more (R5, [sp-800-232 §4.3](sources.md#sp-800-232)).

So truncation saves bytes on a slow radio, but costs security and causes more rekeys.
Whether to study it is an [open question](open-questions.md).

## Per-key limits

| Limit | Value | Requirement |
|-------|-------|-------------|
| Data per key, encryption and decryption combined, including nonces | 2^54 bytes (2^50 blocks) | R6 **shall** |
| Decryption failures per key, tag 64 ≤ λ ≤ 128 | 2^(λ−32), i.e. 2^96 for a full tag | R5 **shall** |
| Decryption failures per key, tag 32 ≤ λ < 64 | 1 (relaxable after a risk analysis) | R5 **should** |
| Key update | Required at the data limit; recommended at the failure limit | R7 |

([sp-800-232 §4.3](sources.md#sp-800-232)). See [Rekeying](rekeying.md).

## Key generation

AEAD keys **shall** be generated following SP 800-133, using an approved random bit
generator of at least 128-bit strength, and **shall not** be used for other purposes
(R1, [sp-800-232 §4.3](sources.md#sp-800-232)). Our keys come out of a KDF instead.
The SP 800-133 Rev. 3 draft covers keys derived from an approved KEM's shared secret,
but only through an **approved** KDF, which Ascon-XOF128 is not
([sp-800-133r3-ipd §2.1, §5.2](sources.md#sp-800-133r3-ipd); a draft). See
[Q4](open-questions.md#q4-does-sp-800-133-cover-our-kdf-derived-ascon-keys). The RBG at
the root inherits [Q7](open-questions.md#q7-how-do-we-state-the-getrandom-deviation).

## Hash and XOF functions

([sp-800-232 §5, Table 9](sources.md#sp-800-232))

| Function | Output | Strength | Notes |
|----------|--------|----------|-------|
| Ascon-Hash256 | 256 bits | 128 bits (collision, preimage) | Approved hash function |
| Ascon-XOF128 | Any length L | collision min(L/2, 128), preimage min(L, 128) | Approved XOF; approved uses "specified in other NIST publications" |
| Ascon-CXOF128 | Any length L | As XOF128 | Adds a customization string of up to 2048 bits (256 bytes) |

- Hash and XOFs use rate 64 / capacity 256 bits
  ([sp-800-232 §5.1–5.2](sources.md#sp-800-232)).
- XOF128 outputs of different lengths from the same input share a prefix. CXOF128 with
  the length in the customization string is the suggested fix
  ([sp-800-232 §5](sources.md#sp-800-232)). This matters if keys of different lengths
  are ever derived from the same input.
- XOFs are **not** approved as hash functions, and HMAC built on any Ascon function is
  not approved ([sp-800-232 §6](sources.md#sp-800-232)). See
  [Key derivation](key-derivation.md).

## Known-answer tests

ascon-c ships `LWC_AEAD_KAT_128_128.txt` and `LWC_XOF_KAT_128_512.txt` in the
`asconaeadxof128` folder; our tests use those (CONTRIBUTING.md). SP 800-232 also points
to NIST's ACVP test vectors ([sp-800-232 §6](sources.md#sp-800-232)), which could be
added later.

## Related pages

- [SP 800-232 summary](src-sp-800-232.md)
- [Key derivation](key-derivation.md)
- [Rekeying](rekeying.md)
- [Radio link](decisions/radio-link.md)
