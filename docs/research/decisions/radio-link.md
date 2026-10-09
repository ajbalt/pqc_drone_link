# Radio Link

**Summary**: Which link the drone and ground station use for experiments: a SiK
915 MHz telemetry radio, LoRa, or UDP only (with netem emulating a real radio).

**Sources**: [fips-203](../sources.md#fips-203), [sp-800-232](../sources.md#sp-800-232), [sx1276-datasheet](../sources.md#sx1276-datasheet), [rfd900-datasheet](../sources.md#rfd900-datasheet), [lorawan-rp-1-0-3](../sources.md#lorawan-rp-1-0-3)

**Last updated**: 2026-10-08

**Status**: Open

**Decision**: —

---

## Why it matters

The radio sets the payload size per packet and the data rate. Those decide how many
fragments an ML-KEM key and ciphertext need, and how long a handshake takes on air.
That is the core of the research question. Fragmentation (`fragment.c`) is due in
week 2, so this decision shapes the week 2 design.

## Options

| Option | Payload per packet | Rate | Main constraint | Details |
|--------|--------------------|------|-----------------|---------|
| SiK 915 MHz (e.g. RFD900) | Our own framing over a byte stream; real air packet size *(needs source: SiK firmware)* | 4–250 kbps air; serial 57600 baud by default | The serial port; the radio hides its own ECC, resends and injected packets | [SiK radios](../sik-radios.md) |
| LoRa (SX127x), raw | 1–255 B | ≈ 0.3–37.5 kbps | US: 400 ms per packet; EU: 1% duty cycle; half-duplex | [LoRa](../lora.md) |
| LoRaWAN | 11–242 B application payload (US) | Same as LoRa | Same, plus MAC overhead and 1–2 s receive windows | [LoRa](../lora.md#what-this-means-for-ml-kem) |
| UDP + netem only | Any (`link_mtu` config) | Any | Realism: which real radio does it emulate? | — |

([rfd900-datasheet §2, §6](../sources.md#rfd900-datasheet),
[sx1276-datasheet §4.1.1.7](../sources.md#sx1276-datasheet),
[lorawan-rp-1-0-3 §2.3.3, Tables 15–16](../sources.md#lorawan-rp-1-0-3))

## Evidence

### Bytes to carry

**Key-exchange payload per handshake** (bytes, before our headers, fragmentation or
retransmissions). These are **estimates** from
[fips-203 Table 3](../sources.md#fips-203); see [ML-KEM](../ml-kem.md#sizes).

| Mode | Responder → initiator | Initiator → responder | Total |
|------|----------------------|-----------------------|-------|
| `mlkem512`  | 800 (`ek`)  | 768 (`c`)  | 1568 |
| `mlkem768`  | 1184 (`ek`) | 1088 (`c`) | 2272 |
| `mlkem1024` | 1568 (`ek`) | 1568 (`c`) | 3136 |
| `x25519`    | *(needs source: RFC 7748)* | *(needs source)* | — |

**Per-packet overhead in the secured modes.** Ascon-AEAD128 adds exactly a 16-byte tag
to each packet ([sp-800-232 §4.1](../sources.md#sp-800-232)). The nonce is rebuilt from
header fields and never sent. So a secured packet costs our header + 16 bytes more
than the same payload in `none` mode (**estimate**). See
[Ascon](../ascon.md#ascon-aead128-parameters).

### Handshake time on each link

ML-KEM-768, 8-byte project header per fragment, airtime only. All **estimates**; full
assumptions are on the linked pages.

| Link | Packets | Handshake airtime |
|------|---------|-------------------|
| RFD900, 64 kbps air, 57600-baud serial | ≈ 10 | ≈ 0.3 s on air, ≈ 0.4 s through the serial port |
| RFD900, 64 kbps with Golay ECC on | ≈ 10 | ≈ 0.6 s |
| LoRa SF7/500 kHz | 10 | ≈ 0.9 s |
| LoRa SF7/125 kHz | 10 | ≈ 3.7 s |
| LoRa SF10/125 kHz (longest legal US range) | 142 | ≈ 53 s |
| LoRaWAN US DR0 | 758 | ≈ 4.7 min |
| LoRaWAN EU DR0, 1% duty cycle | 54 | ≥ 2 h wall clock |
| LoRa SF11/SF12, US | — | **Not possible**: one packet exceeds 400 ms |

Sources: [SiK radios](../sik-radios.md#the-serial-port-may-be-the-bottleneck),
[LoRa](../lora.md#what-this-means-for-ml-kem).

**Finding.** On fast links (SiK, LoRa SF7) ML-KEM-768 costs a few seconds or less. On
the slow, long-range LoRa settings it costs a minute to hours, or is not legal at all.
X25519 fits in one packet each way everywhere. Being able to measure that contrast
rigorously is an argument for emulating several link profiles rather than picking one
radio.

### Tag truncation as a lever

Shorter Ascon tags save bytes on a slow link, but:
- they must be at least 32 bits
- below 64 bits they need a risk analysis
- below 64 bits, every decryption failure should force a rekey
- security drops to the tag length

([sp-800-232 §4.2.1, R4, R5](../sources.md#sp-800-232)). Whether to study this is
[open question Q3](../open-questions.md#q3-is-tag-truncation-in-scope). It matters most
on the smallest LoRa payloads.

### Things that affect measurement validity

- **SiK hides overhead.** ECC (about 3× bytes), opportunistic resends and injected
  RADIO_STATUS packets are invisible to `logger.c`
  ([rfd900-datasheet Fig. 8.3](../sources.md#rfd900-datasheet)). They must be pinned
  per experiment ([Q18](../open-questions.md#q18-which-radio-settings-must-every-experiment-pin)).
- **LoRa overhead is computable.** The preamble, header, CRC and FEC can be calculated
  per fragment ([sx1276-datasheet §4.1.1.7](../sources.md#sx1276-datasheet)), so the
  logger could record true airtime ([Q17](../open-questions.md#q17-should-the-logger-record-computed-airtime-and-radio-overhead)).
- **An 8-byte fragment header is costly on small payloads.** On LoRaWAN US DR0 it
  leaves 3 data bytes per packet ([Q16](../open-questions.md#q16-fragment-header-size-and-minimum-link_mtu)).

## Recommendation (tentative)

Keep **UDP + netem as the primary platform** (as planned for development), but
configure it as **three link profiles taken from these sources**, rather than arbitrary
numbers:

1. **SiK-like:** ≈ 250 B framing, 64 or 128 kbps, capped by 57600-baud serial.
2. **LoRa SF7/125-like:** 255 B maximum payload, ≈ 5.5 kbps, half-duplex, ≈ 400 ms per
   full packet.
3. **LoRa SF10 stress:** 24 B maximum payload, ≈ 1 kbps. This shows where ML-KEM stops
   being practical.

If hardware is bought, validate on a SiK radio, the drone-standard choice, with its
settings pinned. If LoRa is used, use raw LoRa, not LoRaWAN.

Still needed before deciding:
- the hardware and region ([Q14](../open-questions.md#q14-which-radio-hardware-and-which-region))
- the SiK firmware's real packet size and TDM behaviour
- FCC Part 15.247
- MAVLink message sizes for the telemetry load

## Related pages

- [LoRa](../lora.md)
- [SiK radios](../sik-radios.md)
- [ML-KEM](../ml-kem.md)
- [Ascon](../ascon.md)
- [Research wiki index](../index.md)
