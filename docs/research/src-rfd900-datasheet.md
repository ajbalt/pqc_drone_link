# RFD900 Datasheet

**Summary**: Source summary of the RFDesign RFD900 radio modem datasheet (2013), a SiK
900 MHz telemetry radio: what it specifies and which pages it fed.

**Sources**: [rfd900-datasheet](sources.md#rfd900-datasheet)

**Last updated**: 2026-10-08

---

## What it is

The manufacturer's product specification, configuration guide and modem-tools manual
for the original RFD900. It is from 2013; newer RFD900x/ux models may differ. It is
copyrighted, so the wiki paraphrases it.

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| §1–3 | Band, power, range, EIRP limits, certification note | [SiK radios](sik-radios.md) |
| §6 | Transparent serial link, default serial settings, thermal throttling | [SiK radios](sik-radios.md) |
| §8, Fig. 8.3 | S-register table: air speed, ECC, MAVLink framing, opportunistic resend, duty cycle, channels | [SiK radios](sik-radios.md#settings-that-distort-measurements) |
| §9 | Air data rate vs range | [SiK radios](sik-radios.md) |

**Not covered:** maximum packet size, latency, and the details of the TDM link
protocol. These need the SiK firmware source.

## Extraction notes

Extracted with docling. Small source inconsistencies: §9 lists a 2 kbps rate that §2
doesn't, and the sensitivity is printed without a minus sign.

## Related pages

- [SiK radios](sik-radios.md)
