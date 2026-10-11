# Raspberry Pi 4 Model B

**Summary**: The parts of the Raspberry Pi 4 Model B datasheet that matter to our link: the UART and I2C
pins, GPIO logic levels, power, thermal throttling, and what the datasheet does not say. It is the
hardware for Pi A (TLS client) and Pi B (TLS server).

**Sources**: [pi-4-datasheet](sources.md#pi-4-datasheet), [rpi-docs-rtc](sources.md#rpi-docs-rtc)

**Last updated**: 2026-10-11

---

All values are **spec** values from the datasheet (Release 1.1, March 2024) unless marked otherwise.

## Pins we use

The 40-pin header exposes 28 GPIOs. Each pin has up to six alternate functions
([pi-4-datasheet](sources.md#pi-4-datasheet), Table 5).

| Header pin | GPIO | Default pull | ALT0 | ALT5 | Our use |
|-----------|------|--------------|------|------|---------|
| 8 | 14 | Low | `TXD0` | `TXD1` | UART to the Pico, Pi TX |
| 10 | 15 | Low | `RXD0` | `RXD1` | UART from the Pico, Pi RX |
| 6 | (ground) | n/a | n/a | n/a | Common ground |

- **Header pin to GPIO mapping** (8 = GPIO14, 10 = GPIO15) is the standard 40-pin layout; the datasheet
  shows it in Figure 3 (a picture, not extracted). The Build Guide uses the same pins.
- **Two UARTs share these pins.** ALT0 is `TXD0`/`RXD0`; ALT5 is `TXD1`/`RXD1`. The datasheet does not say
  which controller is which, so *(needs source)*: which of the two is the full UART and which is the mini UART,
  and how Raspberry Pi OS maps `/dev/serial0`. Use the one the OS documentation recommends and record it.
- **More UARTs and I2C buses exist** if a pin is ever needed elsewhere: up to 6 UARTs and 6 I2C buses on the
  BCM2711, with extra ones on other GPIOs (for example `TXD2`/`RXD2` on GPIO0/1 at ALT4)
  ([pi-4-datasheet](sources.md#pi-4-datasheet) §2.2, Table 5).
- The only I2C use on the Pi side would be I2C1 on GPIO2/3 at ALT0; the Build Guide puts both I2C devices on the
  Picos, so the Pis need none.

## GPIO electrical levels

At a 3.3 V I/O supply (`VDD IO`), from Table 3:

| Parameter | Min | Max | Unit |
|-----------|-----|-----|------|
| Input low voltage | 0 | 0.8 | V |
| Input high voltage | 2.0 | `VDD IO` | V |
| Output low voltage (at -2 mA) | n/a | 0.4 | V |
| Output high voltage (at +2 mA) | `VDD IO` - 0.4 | n/a | V |
| Output drive current (high or low) | 7 | n/a | mA |
| Input leakage current (at 85 °C) | n/a | 10 | µA |
| Pull-up and pull-down resistor | 18 (47 typical) | 73 | kΩ |

- **The Pi and a 3.3 V Pico are level-compatible**: a Pico output of 3.3 V is above the Pi's 2.0 V input-high
  minimum, and the Pi's output-high minimum is about 2.9 V (3.3 V minus 0.4 V, our arithmetic). The RP2350 input-high minimum
  is 2.0 V and input-low maximum 0.8 V at 3.3 V I/O, so levels are compatible in both directions
  ([RP2350](rp2350.md)).
- **The datasheet does not say GPIO is or is not 5 V tolerant.** The only absolute maximum is the 5 V input
  (-0.5 V to 6.0 V). Treat all GPIO as 3.3 V only, as the Build Guide says.
- Rise and fall times are listed as "TBD" in the datasheet (Table 4), so there is no spec value for UART edge speed.

## Power

- The board needs a good USB-C supply: **5 V at 3 A**, or 2.5 A if attached USB devices draw under 500 mA
  (§4.1).
- Total downstream USB current is limited to about **1.1 A** across the four sockets (§5.3). A Pico powered from
  a Pi USB port is well inside this: the bare Pico 2 draws about 12 mA at 5 V in a typical serial demo
  ([Pico 2](pico-2.md)), a typical value rather than a guaranteed maximum.
- Our own current and power measurements go in [results.md](../results.md), not here.

## Thermals and benchmarking

- Recommended ambient temperature: **0 to 50 °C** (§5.6).
- The Pi lowers clock speed and voltage when idle and raises them under load, and an internal governor throttles
  both so the CPU stays under **85 °C** (§5.6). Sustained load may need extra cooling.
- **Consequence for T10 (handshake benchmark):** handshake time depends on the CPU clock, which depends on load,
  temperature and governor. Record the CPU temperature and frequency with every run, and either fix the governor
  or report the spread. *(Proposed; not yet a rule.)*

## Other facts

| Item | Value | Section |
|------|-------|---------|
| CPU | Quad-core 64-bit Arm Cortex-A72 at 1.5 GHz | §2.1 |
| RAM options | 1, 2, 4 GB LPDDR4 (the datasheet text also mentions 8 GB) | §1, §2.1 |
| Wireless | 802.11 b/g/n/ac, Bluetooth 5.0 | §2.2 |
| Ethernet | 1 gigabit port | §2.2 |
| Availability | guaranteed until at least January 2031 | §6 |

The Build Guide specifies 4 GB boards. The 64-bit Cortex-A72 matters for our crypto: OpenSSL's ML-KEM is plain
C, so the speed we measure is the A72's, with no vendor-specific code path
([OpenSSL TLS 1.3 and the hybrid group](openssl-tls13-hybrid.md)).

## What the datasheet does not say

- **No real-time clock.** It lists no RTC. Absence of mention is not proof, and the Raspberry Pi
  documentation names only the Pi 5 as having one ([rpi-docs-rtc](sources.md#rpi-docs-rtc)). Check with
  `timedatectl` on our boards; see [Certificates and the Pi clock](certificates-and-time.md).
- No Wi-Fi throughput or 2.4 GHz-only mode, so link-rate numbers must come from our own measurements.
- No idle or load current figures for the board; these come from the instrument (Q5).

## Related pages

- [Pico 2](pico-2.md)
- [MPU-6050](mpu-6050.md)
- [Certificates and the Pi clock](certificates-and-time.md)
- [OpenSSL TLS 1.3 and the hybrid group](openssl-tls13-hybrid.md)
