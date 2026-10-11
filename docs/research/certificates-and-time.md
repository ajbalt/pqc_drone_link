# Certificates and the Pi Clock

**Summary**: Which OpenSSL the Raspberry Pi OS provides, and why certificate verification depends
on the clock of a Raspberry Pi 4 that has no real-time clock, with what we propose to do about it.

**Sources**: [openssl-3-5-9](sources.md#openssl-3-5-9), [debian-openssl-tracker](sources.md#debian-openssl-tracker), [rpi-os-trixie-news](sources.md#rpi-os-trixie-news), [rpi-docs-rtc](sources.md#rpi-docs-rtc), [arch-arm-pi4-clock-forum](sources.md#arch-arm-pi4-clock-forum)

**Last updated**: 2026-10-11

---

## Which OpenSSL will the Pis have?

| Fact | Value | Type and source |
|------|-------|-----------------|
| Debian 12 "bookworm" OpenSSL | 3.0.x (3.0.22 in the security suite at reading time) | reported, [debian-openssl-tracker](sources.md#debian-openssl-tracker) |
| Debian 13 "trixie" OpenSSL | 3.5.7 (`3.5.7-1~deb13u3` in the security suite at reading time) | reported, same |
| Raspberry Pi OS base | Debian 13 "trixie" since October 2025; Linux 6.12 kernel | reported, [rpi-os-trixie-news](sources.md#rpi-os-trixie-news) |

So a Pi running Raspberry Pi OS based on trixie should already have OpenSSL 3.5, which includes
`X25519MLKEM768`, and **no build from source is needed**. A bookworm-based image has 3.0 and does
not. This is inferred from the package tracker, so it **must be checked on each Pi**:

```sh
openssl version
openssl list -tls1_3 -tls-groups | grep -i X25519MLKEM768
```

Trade-offs *(proposed)*:

| Option | For | Against |
|--------|-----|---------|
| Distribution OpenSSL 3.5 (trixie) | Security updates, nothing to build, `libssl-dev` for C code | The version moves with updates, so hold it (`apt-mark hold`) and record `dpkg -s openssl libssl3` in every test log |
| Build 3.5.9 into `/opt` | Exact pin, same on dev machine and Pi | Build time on a Pi 4 unknown; we then own the security updates |

The gate L5 text requires a pinned version; either option satisfies it if the exact version is
recorded. Distribution builds may carry patches, so the group check above is the real test.

## The Pi 4 has no real-time clock

- The Raspberry Pi documentation says only that the **Raspberry Pi 5** includes an RTC
  ([rpi-docs-rtc](sources.md#rpi-docs-rtc)); it names no other model. Users report that a Pi 4 shows
  "RTC time: n/a" and boots with a stale time unless a service restores it
  ([arch-arm-pi4-clock-forum](sources.md#arch-arm-pi4-clock-forum), forum posts, **low trust**, and
  for Arch Linux ARM rather than Raspberry Pi OS). *(Check with `timedatectl` on our Pis.)*
- Common remedies mentioned there: `fake-hwclock` (restores the last saved time at boot),
  `systemd-timesyncd` or `chrony` (network time), or an RTC module such as a DS3231.
- **Our lab network is a dedicated AP and may have no internet**, so public NTP may be unreachable.
  Then the Pis need a local time source: for example Pi B (or the AP) acting as an NTP server that Pi
  A follows.

## What a wrong clock does to TLS

Certificates carry a not-before and a not-after date, and OpenSSL checks them. On our dev machine,
with a 7-day certificate ([results.md](../results.md)):

| Clock at verification | OpenSSL result |
|-----------------------|----------------|
| Before the not-before date | error 9, "certificate is not yet valid" |
| Inside the validity period | OK |
| After the not-after date | error 10, "certificate has expired" |

A Pi that boots with an old clock rejects a freshly made certificate. That would look like a
failure of T03 or L6 but is a clock problem. It also corrupts `ts_ms` in records and any latency that
compares timestamps from the two Pis.

## Proposals *(proposed)*

1. Never disable the time check to make connections work (crypto rule 13). Fix the clock.
2. Make certificates with a not-before **one day in the past** and a validity of 30 days. OpenSSL
   3.5.9's `openssl x509` supports `-not_before` and `-not_after` (`apps/x509.c`).
   `scripts/make_demo_certs.sh` does this. A 30-day validity still counts as short-lived for rule 17.
3. Give both Pis the same time source and record the offset (for example from `chronyc tracking`)
   in each test log. Decide whether the AP, Pi B or an RTC module provides it.
4. Have Pi A refuse to start the TLS client until the clock is plausible (for example not earlier
   than the build date), and log it, so a clock fault is not mistaken for a TLS fault.
5. Record the clock source and offset in `evidence/L1_power_network.txt`.

## Open

- Which Raspberry Pi OS version and image the lab will use (trixie or bookworm).
- Whether the lab network has internet access.
- Whether `systemd-timesyncd` or `chrony` is the default on the chosen image.

## Related pages

- [OpenSSL TLS 1.3 and the hybrid group](openssl-tls13-hybrid.md)
- [Handshake authentication](decisions/handshake-auth.md)
- [Telemetry format](../telemetry_format.md)
