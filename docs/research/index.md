# Research Wiki Index

Catalog of every page in the research wiki, one line each. Read this first when
answering a question. How the wiki works: [README.md](README.md). Sources:
[sources.md](sources.md). Change history: [log.md](log.md).

## How to use this wiki now

The mandatory system (OpenSSL TLS 1.3, Wi-Fi, Pi 4B + Pico 2) is defined in
[scope](../scope.md). Most pages here were written for the earlier custom ML-KEM + Ascon
design over SiK or LoRa radios, and are kept as background for the possible research
extension.

| Relevance | Pages |
|-----------|-------|
| Useful for the mandatory system | [Raspberry Pi 4](raspberry-pi-4.md), [Pico 2](pico-2.md), [Pico 2 pinout](pico-2-pinout.md), [RP2350](rp2350.md), [MPU-6050](mpu-6050.md), [MPU-6050 registers](mpu-6050-registers.md), [SSD1306](ssd1306.md), [OpenSSL TLS 1.3](openssl-tls13-hybrid.md), [certificates and time](certificates-and-time.md), [TCP under loss](tcp-under-loss.md), [ML-KEM](ml-kem.md), [FIPS 203](src-fips-203.md), [key management](key-management.md), [random bit generation](random-bit-generation.md), [ML-DSA](ml-dsa.md) (why the certificate is not PQ authentication) |
| Background for the Ascon extension | [Ascon](ascon.md), [key derivation](key-derivation.md), [KEM usage](kem-usage.md), [rekeying](rekeying.md), SP 800-227, SP 800-232, SP 800-56C |
| Out of scope (radios and MAVLink) | [LoRa](lora.md), [SiK radios](sik-radios.md), [MAVLink](mavlink.md) |

Gaps: a tested SSD1306 initialization sequence, the Pi's UART setup in Raspberry Pi OS, the durable queue, measurement methods, and Wi-Fi. Ingest the
Build Guide first (`/ingest`).

## Decisions

One page per open decision in the repo README. The 2026-10-10 advisor decision closed all four; see [scope](../scope.md).

| Page | Status | Question |
|------|--------|----------|
| [Radio link](decisions/radio-link.md) | Decided: Wi-Fi AP | SiK 915 MHz, LoRa, or UDP-only? |
| [KDF construction](decisions/kdf-construction.md) | Superseded: TLS 1.3 | Ascon-XOF128 or HKDF-SHA256 for session keys? |
| [Hybrid mode](decisions/hybrid-mode.md) | Decided: config B | Add an X25519 + ML-KEM mode, and how to combine the secrets? |
| [Handshake authentication](decisions/handshake-auth.md) | Decided: pinned cert | Pre-shared key in the KDF, or ML-DSA signatures? |

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
- [MPU-6050 registers](mpu-6050-registers.md): register addresses and bits (wake-up, ranges, sample rate, data registers, temperature formula) and a minimal bring-up sequence
- [MPU-6050](mpu-6050.md): the rover IMU: 2.375 to 3.46 V supply, I2C 100 and 400 kHz, address 0x68 or 0x69, scale factors, sample rates
- [OpenSSL TLS 1.3 and the hybrid group](openssl-tls13-hybrid.md): 3.5.9 defaults, group-list syntax, config A and B settings, C API calls, benchmark tool limits
- [Certificates and the Pi clock](certificates-and-time.md): OpenSSL version on Raspberry Pi OS, no RTC on a Pi 4, validity errors, proposals
- [Open questions](open-questions.md): Q1–Q18, questions the team still has to answer
- [Random bit generation](random-bit-generation.md): what an approved RBG is (SP 800-90A/B/C), where `getrandom()` stands, and the deviation statement
- [TCP under loss](tcp-under-loss.md): RFC 6298 timers, dead-connection detection, expected handshake delay at 10 and 30% loss, `tc netem` models
- [SSD1306 OLED driver](ssd1306.md): the relay display controller: I2C address 0x3C or 0x3D, write format, 400 kHz limit, display memory, supply and power sequence, commands
- [Pico 2 pinout](pico-2-pinout.md): the official pinout poster: all 40 header pins with GPIO names and UART, I2C, SPI and ADC functions; confirms our Pico wiring (pins 1, 2, 3, 6, 7, 36, 38)
- [Pico 2](pico-2.md): Raspberry Pi Pico 2 datasheet facts: 3.3 V IO, power limits, typical current, BOOTSEL and reset, and which wiring facts the datasheet does not state
- [RP2350](rp2350.md): chip datasheet facts for the Pico 2: 5 V tolerance, input and output voltage levels, pull resistors, UART0 and I2C0 function table, I2C speeds and pull-ups, UART limits, errata
- [Raspberry Pi 4](raspberry-pi-4.md): UART and I2C pins, 3.3 V GPIO levels, power, thermal throttling, and what the datasheet does not say
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
- [OpenSSL 3.5.9](openssl-tls13-hybrid.md): read directly from the source tree; no separate summary page
- Pi 4, Pico 2 and MPU-6050 datasheets: read directly into the topic pages [Raspberry Pi 4](raspberry-pi-4.md), [Pico 2](pico-2.md) and [MPU-6050](mpu-6050.md); no separate summary pages
- [SX1276 datasheet](src-sx1276-datasheet.md): LoRa transceiver (Semtech, 2020)
- [RFD900 datasheet](src-rfd900-datasheet.md): SiK 900 MHz radio modem (RFDesign, 2013)
- [LoRaWAN 1.0.3 Regional Parameters](src-lorawan-rp-1-0-3.md): regional LoRa limits (LoRa Alliance, 2018)
- [MAVLink 2 guide](src-mavlink2-guide.md): MAVLink 2 features and message signing
- [ExpressVPN PQ WireGuard guide](src-expressvpn-pq-wireguard-guide.md): vendor white paper, **low trust**

## Comparisons and answers

*(none yet)*
