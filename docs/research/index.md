# Research Wiki Index

Catalog of every page in the research wiki, one line each. Read this first when
answering a question. How the wiki works: [README.md](README.md). Sources:
[sources.md](sources.md). Change history: [log.md](log.md).

## Decisions

One page per open decision in the repo README.

| Page | Status | Question |
|------|--------|----------|
| [Radio link](decisions/radio-link.md) | Open | SiK 915 MHz, LoRa, or UDP-only? |
| [KDF construction](decisions/kdf-construction.md) | Open | Ascon-XOF128 or HKDF-SHA256 for session keys? |
| [Hybrid mode](decisions/hybrid-mode.md) | Open | Add an X25519 + ML-KEM mode, and how to combine the secrets? |
| [Handshake authentication](decisions/handshake-auth.md) | Open | Pre-shared key in the KDF, or ML-DSA signatures? |

Smaller and cross-cutting questions (Q1–Q18) are tracked in
[Open questions](open-questions.md).

## Topics

- [Ascon](ascon.md): Ascon-AEAD128 sizes, nonce and tag rules, truncation, per-key limits, byte order, hash and XOFs
- [KEM usage](kem-usage.md): single-use ephemeral keys, the authentication requirement, key confirmation, failure handling
- [Key derivation](key-derivation.md): approved KDF constructions, inputs (secret, salt, FixedInfo, PSK), where Ascon stands
- [Key management](key-management.md): security strengths and the weakest-link rule, cryptoperiods, generating and combining keys, rekey vs key update, disposal
- [LoRa](lora.md): SX127x PHY, time-on-air formula, 255 B limit, US 400 ms dwell and EU 1% duty cycle, ML-KEM handshake airtime
- [MAVLink](mavlink.md): MAVLink 2 message signing (a PSK, timestamps against replay, link IDs) and what is still needed for telemetry modelling
- [ML-DSA](ml-dsa.md): signature sizes and categories, signing modes, randomness, and what signatures would add to our handshake
- [ML-KEM](ml-kem.md): parameter sets, sizes, input checks, implicit rejection, randomness and shared-secret rules
- [Open questions](open-questions.md): Q1–Q18, questions the team still has to answer
- [Random bit generation](random-bit-generation.md): what an approved RBG is (SP 800-90A/B/C), where `getrandom()` stands, and the deviation statement
- [Rekeying](rekeying.md): every rekey trigger with a basis in the standards, and what a rekey must do
- [SiK radios](sik-radios.md): RFD900/SiK rates, the serial bottleneck, a byte stream not a packet radio, settings that distort measurements

## Source summaries

- [FIPS 203](src-fips-203.md): the ML-KEM standard (NIST, 2024)
- [SP 800-227](src-sp-800-227.md): recommendations for using KEMs (NIST, 2025)
- [SP 800-232](src-sp-800-232.md): the Ascon standard (NIST, 2025)
- [SP 800-56C Rev. 2](src-sp-800-56c.md): key-derivation methods (NIST, 2020)
- [FIPS 204](src-fips-204.md): the ML-DSA signature standard (NIST, 2024)
- [SP 800-90A/B/C](src-sp-800-90.md): random bit generation (NIST, 2015–2025)
- [SP 800-133 Rev. 3 (draft)](src-sp-800-133r3-ipd.md): key generation (NIST, 2026, initial public draft)
- [SP 800-57 Part 1 Rev. 5](src-sp-800-57pt1.md): key management (NIST, 2020)
- [SX1276 datasheet](src-sx1276-datasheet.md): LoRa transceiver (Semtech, 2020)
- [RFD900 datasheet](src-rfd900-datasheet.md): SiK 900 MHz radio modem (RFDesign, 2013)
- [LoRaWAN 1.0.3 Regional Parameters](src-lorawan-rp-1-0-3.md): regional LoRa limits (LoRa Alliance, 2018)
- [MAVLink 2 guide](src-mavlink2-guide.md): MAVLink 2 features and message signing
- [ExpressVPN PQ WireGuard guide](src-expressvpn-pq-wireguard-guide.md): vendor white paper, **low trust**

## Comparisons and answers

*(none yet)*
