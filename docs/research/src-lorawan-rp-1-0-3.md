# LoRaWAN 1.0.3 Regional Parameters

**Summary**: Source summary of the LoRa Alliance's LoRaWAN 1.0.3 Regional Parameters
(Rev. A, 2018): per-region data rates, payload limits and legal constraints, used here
mainly for the US 400 ms dwell and EU duty-cycle rules.

**Sources**: [lorawan-rp-1-0-3](sources.md#lorawan-rp-1-0-3)

**Last updated**: 2026-10-08

---

## What it is

The companion to the LoRaWAN 1.0.3 specification. It defines, per region, the channel
plans, data rates, maximum payload sizes, transmit power and timing. It also summarises
the regulatory limits (FCC for the US, ETSI for the EU). We use it as a guide to what
is legal and practical for LoRa, not because we plan to use LoRaWAN. It is copyrighted,
so the wiki paraphrases it.

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| §2.2 (EU863-870) | Data rates, Tables 8–9 payload sizes, < 1% duty cycle, no dwell limit | [LoRa](lora.md#legal-limits) |
| §2.3.2–2.3.3 (US902-928) | FCC summary; 400 ms uplink dwell; channel plan | [LoRa](lora.md#legal-limits), [SiK radios](sik-radios.md#regulation) |
| Tables 12, 15–16 | US data rates and maximum payload M/N | [LoRa](lora.md) |
| §2.2.9, §2.3.9 | Receive delays and ACK timeout | [LoRa](lora.md#what-this-means-for-ml-kem) |

## Extraction notes

Extracted with docling. The tables are clean.

## Related pages

- [LoRa](lora.md)
