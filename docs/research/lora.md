# LoRa

**Summary**: The LoRa radio (Semtech SX127x) and its legal limits: payload size,
spreading factors, the time-on-air formula, the US 400 ms dwell and EU 1% duty-cycle
rules, and what they mean for carrying an ML-KEM handshake.

**Sources**: [sx1276-datasheet](sources.md#sx1276-datasheet), [lorawan-rp-1-0-3](sources.md#lorawan-rp-1-0-3)

**Last updated**: 2026-10-08

---

## The physical layer

| Item | Value | Source |
|------|-------|--------|
| Max payload per packet | 1–255 bytes | [sx1276-datasheet §4.1.1.7](sources.md#sx1276-datasheet) |
| FIFO buffer | 256 bytes, shared by TX and RX | [sx1276-datasheet §4.1.2.3](sources.md#sx1276-datasheet) |
| Spreading factor (SF) | 6–12 (SF6 needs implicit header) | [sx1276-datasheet Table 13, §4.1.1.2](sources.md#sx1276-datasheet) |
| Bandwidth (BW) | 7.8 to 500 kHz (125 / 250 / 500 kHz are typical) | [sx1276-datasheet Table 15](sources.md#sx1276-datasheet) |
| Coding rate (CR) | 4/5, 4/6, 4/7, 4/8 (1.25–2× overhead) | [sx1276-datasheet Table 14](sources.md#sx1276-datasheet) |
| Header | Explicit (length, CR and CRC flag, always sent at CR 4/8) or implicit | [sx1276-datasheet §4.1.1.6](sources.md#sx1276-datasheet) |
| Payload CRC | Optional, 16 bits | [sx1276-datasheet §4.1.1.6](sources.md#sx1276-datasheet) |
| Preamble | 12 symbols by default; LoRaWAN uses 8 | [sx1276-datasheet §4.1.1.6](sources.md#sx1276-datasheet), [lorawan-rp-1-0-3 §2.3.1](sources.md#lorawan-rp-1-0-3) |
| Modes | Half-duplex, one channel: both directions share the airtime | [sx1276-datasheet §4.1.3.1](sources.md#sx1276-datasheet) |
| Bit rate | About 0.018–37.5 kbps (e.g. SF12/125 kHz ≈ 293 bps; SF7/125 ≈ 5.5 kbps) | [sx1276-datasheet Table 1, Table 12](sources.md#sx1276-datasheet) |

All values are **spec**, except the bit rates, which are nominal.

**Consequence:** even ML-KEM-512 needs several packets per direction. The ML-KEM-768
encapsulation key (1184 B) needs at least 5 at the maximum payload.

## Time on air

The symbol time is Ts = 2^SF / BW, so each step up in SF roughly doubles airtime
([sx1276-datasheet §4.1.1.5](sources.md#sx1276-datasheet)). One packet's airtime
([sx1276-datasheet §4.1.1.7](sources.md#sx1276-datasheet)):

```
T_preamble = (n_preamble + 4.25) · Ts
n_payload  = 8 + max( ceil( (8·PL − 4·SF + 28 + 16·CRC − 20·IH) / (4·(SF − 2·DE)) ) · (CR + 4), 0 )
T_packet   = T_preamble + n_payload · Ts
```

- PL = payload bytes; CRC = 1 if on; IH = 1 for implicit header.
- DE = 1 when low-data-rate optimisation is on. It is required when Ts > 16 ms, which
  at 125 kHz means SF11 and SF12.
- CR = 1–4 for 4/5–4/8.

**Extraction note:** docling dropped these formulas, so they were recovered from the
PDF's text layer. Two cross-checks agree:
- The formula reproduces the LoRaWAN US915 maximum-payload table exactly (below).
- An independent recomputation gives 399.6 ms for SF7/125 at 255 B.

The datasheet itself is slightly inconsistent: it gives "+4" for the preamble in
§4.1.1.6 but "+4.25" in the formula.

Because airtime is fully computable, `logger.c` could record a computed on-air time per
fragment (see [Q17](open-questions.md#q17-should-the-logger-record-computed-airtime-and-radio-overhead)).

## Legal limits

| Region | Rule | Source |
|--------|------|--------|
| US 902–928 MHz (FCC, frequency hopping) | ≤ 400 ms per transmission per channel, ≥ 50 channels of ≤ 250 kHz, ≤ 30 dBm conducted | [lorawan-rp-1-0-3 §2.3.2–2.3.3](sources.md#lorawan-rp-1-0-3) |
| US, ≥ 500 kHz channels (DTS mode) | Power-density limit (about 26 dBm); hybrid mode keeps the 400 ms dwell | [lorawan-rp-1-0-3 §2.3.2](sources.md#lorawan-rp-1-0-3) |
| EU 863–870 MHz | No dwell limit, but < 1% duty cycle on the default channels | [lorawan-rp-1-0-3 §2.2.2–2.2.3, Table 3](sources.md#lorawan-rp-1-0-3) |

The LoRaWAN document summarises the FCC rules as of March 2017. The FCC text itself
(Part 15.247) has not been ingested yet. Long packets in the US band need frequency
hopping during a packet, which the host microcontroller must manage
([sx1276-datasheet §4.1.1.8](sources.md#sx1276-datasheet)).

**Maximum payload under the 400 ms dwell** (US915 uplink, 125 kHz, CR 4/5, explicit
header, CRC on, 8-symbol preamble):

| SF | Largest PHY payload ≤ 400 ms | LoRaWAN US data rate | LoRaWAN max app payload N |
|----|------------------------------|----------------------|---------------------------|
| 10 | 24 B | DR0 | 11 B |
| 9  | 66 B | DR1 | 53 B |
| 8  | 138 B | DR2 | 125 B |
| 7  | 255 B | DR3 | 242 B |
| 11, 12 | **none**: 1 byte already takes > 400 ms | not used for US uplinks | — |

These are **estimates** from the formula, matched against
[lorawan-rp-1-0-3 Tables 12, 15–16](sources.md#lorawan-rp-1-0-3). The LoRaWAN MAC
overhead between PHY payload and N is about 5–13 bytes *(needs source: LoRaWAN 1.0.3
spec)*.

## What this means for ML-KEM

Handshake airtime for ML-KEM-768 (1184 B `ek` + 1088 B `c`), assuming an 8-byte project
header per fragment. Airtime only: no turnaround, acknowledgements or retransmissions.
All **estimates**:

| Link | Data per fragment | Packets (both directions) | Airtime |
|------|-------------------|---------------------------|---------|
| Raw LoRa SF7/500 kHz | 247 B | 10 | ≈ 0.9 s |
| Raw LoRa SF8/500 kHz | 247 B | 10 | ≈ 1.6 s |
| Raw LoRa SF7/125 kHz | 247 B | 10 | ≈ 3.7 s |
| Raw LoRa SF10/125 kHz (longest legal US range) | 16 B | 142 | ≈ 53 s |
| LoRaWAN US DR0 (SF10/125, N = 11) | 3 B | 758 | ≈ 4.7 min |
| LoRaWAN EU DR0 (SF12/125), 1% duty cycle | 43 B | 54 | ≈ 2.5 min airtime → **≥ 2 h** wall clock |
| X25519, any of the above | — | 2 | one packet each way *(key size: needs source, RFC 7748)* |

**The settings chosen for long range are exactly where ML-KEM-768 stops being
practical.** At the fast settings it costs a few seconds. That contrast is a central
result for `docs/results.md`.

**LoRaWAN vs raw LoRa.** LoRaWAN adds its own MAC layer and security, and 1–2 s receive
windows with a 2 ± 1 s ACK timeout
([lorawan-rp-1-0-3 §2.3.9](sources.md#lorawan-rp-1-0-3)). Its tiny payload at DR0
leaves 3 bytes after our header. Raw LoRa with our own framing fits a point-to-point
link better ([Q15](open-questions.md#q15-raw-lora-or-lorawan)).

## Energy

TX current is about 28 mA at 13 dBm and 90 mA at 17 dBm (PA_BOOST). RX current is
about 10–14 mA, depending on band and bandwidth (**typical**,
[sx1276-datasheet Table 10](sources.md#sx1276-datasheet)). With the airtimes above, this
gives a first estimate of the radio energy per handshake.

## Related pages

- [Radio link](decisions/radio-link.md)
- [SiK radios](sik-radios.md)
- [SX1276 summary](src-sx1276-datasheet.md)
- [LoRaWAN regional parameters summary](src-lorawan-rp-1-0-3.md)
