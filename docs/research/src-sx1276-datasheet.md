# SX1276 Datasheet (LoRa Transceiver)

**Summary**: Source summary of Semtech's SX1276/77/78/79 datasheet (Rev. 7, 2020), the
LoRa radio chip: what it specifies and which pages it fed.

**Sources**: [sx1276-datasheet](sources.md#sx1276-datasheet)

**Last updated**: 2026-10-08

---

## What it is

The manufacturer's datasheet for the most common LoRa transceiver family. It covers
both the LoRa and FSK modems, the register map and electrical characteristics. It is
copyrighted, so the wiki paraphrases it.

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| §4.1.1.2–4.1.1.5, Tables 12–15 | SF, CR, BW, symbol rate, example bit rates | [LoRa](lora.md) |
| §4.1.1.6–4.1.1.7 | Packet structure; the time-on-air formula; 1–255-byte payload | [LoRa](lora.md), [Radio link](decisions/radio-link.md) |
| §4.1.1.8 | Frequency hopping within a packet | [LoRa](lora.md) |
| §4.1.2.3, §4.1.3.1 | 256-byte FIFO; half-duplex modes | [LoRa](lora.md) |
| Table 10 | TX/RX current | [LoRa](lora.md#energy) |

## Extraction notes

Extracted with docling. The time-on-air formulas (§4.1.1.5, §4.1.1.7) were not
decoded. They were recovered from the PDF's text layer and verified against the
LoRaWAN payload tables, and by an independent recomputation.

## Related pages

- [LoRa](lora.md)
