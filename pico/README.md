# Pico firmware

Not started. This folder will hold the firmware for the two Raspberry Pi Pico 2 boards:

| Board | Job | Reads |
|-------|-----|-------|
| Pico A (rover) | Read the MPU-6050 at 10 Hz over I2C0 and send one UART frame per sample to Pi A | [MPU-6050 registers](../docs/research/mpu-6050-registers.md), frame format in [telemetry_format.md](../docs/telemetry_format.md) section 2 |
| Pico B (relay) | Receive display updates from Pi B over UART and show the unique-record count and latest sample on the OLED | [SSD1306](../docs/research/ssd1306.md) |

## Before writing anything

- **The firmware language is undecided** (C SDK or MicroPython). Record the decision in CONTRIBUTING.md before
  starting. See the open decisions in [../README.md](../README.md).
- Gate L2 (IMU) and L3 (UART and OLED) come first and need real hardware. Do not write the TLS side first.
- Wiring is in [../hardware/pin_map.csv](../hardware/pin_map.csv). UART0 on GP0/GP1 at 115200 8N1, I2C0 on
  GP4/GP5, 3.3 V logic. The IMU starts asleep and must be woken before reading it.

## Rules that apply here

- Every buffer copy has an explicit length checked against the destination.
- A bad UART frame (CRC, length, size) is dropped, counted and re-synced, never trusted.
- Nothing in firmware prints or logs inside a timed region.
- Pico firmware does **no** cryptography. All TLS runs on the Pis.
