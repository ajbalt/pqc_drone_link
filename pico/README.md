# Pico firmware

Not started (Phase 2: after the computer-only link is verified and the advisor has reviewed it). Language decided: C SDK. Either a Pico W or a
Pico 2 is acceptable per the advisor (2026-10-16); use what is available and record the model. This folder will hold the firmware for the two Raspberry Pi Pico 2 boards:

| Board | Job | Reads |
|-------|-----|-------|
| Pico A (rover) | Read the MPU-6050 at 10 Hz over I2C0 and send one UART frame per sample to Pi A | [MPU-6050 registers](../docs/research/mpu-6050-registers.md), frame format in [telemetry_format.md](../docs/telemetry_format.md) section 2 |
| Pico B (relay) | Receive display updates from Pi B over UART and show the unique-record count and latest sample on the OLED | [SSD1306](../docs/research/ssd1306.md) |

## Before writing anything

- **The firmware is C with the Raspberry Pi Pico C/C++ SDK** (team decision, 2026-10-19; recorded in
  [../CONTRIBUTING.md](../CONTRIBUTING.md)). No MicroPython. A Pico 2 or a Pico W only does I/O.
- Gate L2 (IMU) and L3 (UART and OLED) come first and need real hardware. Do not write the TLS side first.
- Wiring is in [../hardware/pin_map.csv](../hardware/pin_map.csv). UART0 on GP0/GP1 at 115200 8N1, I2C0 on
  GP4/GP5, 3.3 V logic. The IMU starts asleep and must be woken before reading it.

## Rules that apply here

- Every buffer copy has an explicit length checked against the destination.
- A bad UART frame (CRC, length, size) is dropped, counted and re-synced, never trusted.
- Nothing in firmware prints or logs inside a timed region.
- Pico firmware does **no** cryptography. All TLS runs on the Pis.
- **Share the frame code, do not copy it.** Pico A builds the UART frame with `src/proto/uart_frame.c`
  (`uart_sample_pack()`, then `uart_frame_encode()`): add that file to the SDK target and the repo's
  `src/` directory to its include path. Pico B can use the same parser for display messages from Pi B.
  The file depends only on `common/errors.h`, so it needs nothing else from the repo.
