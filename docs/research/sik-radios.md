# SiK Radios

**Summary**: SiK-firmware telemetry radios such as the RFD900, the common 900 MHz drone
link: air and serial rates, why they behave as a byte stream rather than a packet
radio, and the settings that would distort our measurements unless pinned.

**Sources**: [rfd900-datasheet](sources.md#rfd900-datasheet), [mavlink2-guide](sources.md#mavlink2-guide), [lorawan-rp-1-0-3](sources.md#lorawan-rp-1-0-3)

**Last updated**: 2026-10-08

---

## What they are

The RFD900 runs the open-source **SiK** firmware, which 3DR radios also use. It is
configured with AT commands or "S-registers"
([rfd900-datasheet §6](sources.md#rfd900-datasheet)). It works as a **transparent serial
link**: bytes written to its UART come out of the other radio's UART
([rfd900-datasheet §6](sources.md#rfd900-datasheet)).

So for us, `link_mtu` is a **framing choice we make over a byte stream**, not a radio
constant. `serial_radio.c` needs its own framing (a length prefix or delimiter). The
radio's real over-the-air packet size is not given in the datasheet *(needs source: SiK
firmware)*.

## Numbers (RFD900, 2013 datasheet)

| Item | Value | Source |
|------|-------|--------|
| Air data rates | 4, 8, 16, 19, 24, 32, 48, 64, 96, 128, 192, 250 kbps (§9 also lists 2) | [rfd900-datasheet §2, §9](sources.md#rfd900-datasheet) |
| Default air rate | "64, 128" (the two values appear to be for firmware v1 and v2; needs confirming) | [rfd900-datasheet Fig. 8.3](sources.md#rfd900-datasheet) |
| Serial rates | 2400–115200 baud; default 57600 8N1 | [rfd900-datasheet §2, §6](sources.md#rfd900-datasheet) |
| Band | 902–928 MHz, frequency hopping, up to 50 channels | [rfd900-datasheet §2](sources.md#rfd900-datasheet) |
| TX power | 0–30 dBm in 1 dB steps | [rfd900-datasheet §2, §3](sources.md#rfd900-datasheet) |
| Range | ≥ 40 km line of sight (about 40 km at 64 kbps; antenna dependent) | [rfd900-datasheet §1, §9](sources.md#rfd900-datasheet) (typical) |
| Current | About 1 A peak at maximum power; about 60 mA RX | [rfd900-datasheet §2](sources.md#rfd900-datasheet) (typical) |
| Max packet size, latency | Not covered | — |

The module is not certified as a stand-alone device
([rfd900-datasheet §1](sources.md#rfd900-datasheet)). The datasheet is from 2013;
current RFD900x/ux models may differ ([Q14](open-questions.md#q14-which-radio-hardware-and-which-region)).

## The serial port may be the bottleneck

At the default 57600 baud (8N1 sends 10 bits per byte, so 5760 B/s), the 2272-byte
ML-KEM-768 handshake plus framing takes **≈ 0.41 s just to cross the UART**. At 64 kbps,
the air needs only ≈ 0.29 s (**estimates**). At default settings the serial link, not
the radio, limits handshake time. Experiments should record the serial rate.

## Settings that distort measurements

The radio does things our logger cannot see
([rfd900-datasheet Fig. 8.3 footnotes, §6](sources.md#rfd900-datasheet)):

| S-register | Default | Effect on our measurements |
|------------|---------|----------------------------|
| ECC (S5) | on/off by version | Golay error correction sends **extra data twice the packet length**, so real on-air bytes are about 3× ours and throughput drops |
| MAVLINK (S6) | on/off by version | When it sees a MAVLink heartbeat, the radio **injects RADIO_STATUS packets** into the stream |
| OP_RESEND (S7) | on/off by version | Radio-level retransmission **hides packet loss** from our retransmit logic |
| DUTY_CYCLE (S11) | 100% | Limits transmit time; there is also automatic thermal throttling that is always on |
| AIR_SPEED (S2), SERIAL_SPEED | see above | Set the link rate |

Each experiment config should pin these and record them in the run log
([Q18](open-questions.md#q18-which-radio-settings-must-every-experiment-pin)). Otherwise
loss and byte counts change between runs for reasons outside our code. That breaks the
CONTRIBUTING.md rule that byte counts include *all* on-air overhead
([Q17](open-questions.md#q17-should-the-logger-record-computed-airtime-and-radio-overhead)).

The injected RADIO_STATUS packets are the same ones MAVLink's signing guide says to
accept unsigned, "for feedback from 3DR radios (which don't do signing)"
([mavlink2-guide](sources.md#mavlink2-guide)).

## Regulation

US frequency hopping in this band is limited to 400 ms per channel over at least 50
channels ([lorawan-rp-1-0-3 §2.3.2](sources.md#lorawan-rp-1-0-3), summarising FCC rules).
The RFD900's alternative default of 20 channels appears to conflict with that; it needs
checking against FCC Part 15.247 *(needs source)*.

## Related pages

- [Radio link](decisions/radio-link.md)
- [LoRa](lora.md)
- [MAVLink](mavlink.md)
- [RFD900 summary](src-rfd900-datasheet.md)
