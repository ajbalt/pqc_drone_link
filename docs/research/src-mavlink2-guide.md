# MAVLink 2 Guide

**Summary**: Source summary of the "Guide to MAVLink2" programmer's document: MAVLink 2
features, the C and Python APIs, and message signing.

**Sources**: [mavlink2-guide](sources.md#mavlink2-guide)

**Last updated**: 2026-10-08

---

## What it is

A short programmer's guide to MAVLink 2. It covers generating C headers, interworking
with MAVLink 1, message extensions, and in most detail, how to enable and handle
message signing. It has no date, author or URL. It is believed to come from the
MAVLink/pymavlink repositories (see [sources](sources.md#mavlink2-guide)). It was read
directly as markdown.

## Sections used

| Section | Content | Used in |
|---------|---------|---------|
| Key features | 24-bit IDs, signing, extensions | [MAVLink](mavlink.md#what-mavlink-2-adds) |
| Handling message signing / SETUP_SIGNING / timestamps / link IDs | Key setup, timestamp rules, replay checks, link IDs | [MAVLink](mavlink.md#message-signing) |
| accept_unsigned_callback | Accepting unsigned RADIO_STATUS packets | [MAVLink](mavlink.md#message-signing), [SiK radios](sik-radios.md) |

**Not covered:** message sizes, stream rates, and the signature algorithm or its size.

## Related pages

- [MAVLink](mavlink.md)
