# Raspberry Pi Pico 2

**Summary**: What the Raspberry Pi Pico 2 datasheet (release 5, build date 03/07/2026) says about the board we use as the sensor endpoint: power limits, the 3.3 V logic level, which pins are stated in the text, current consumption, programming and reset. The datasheet does **not** give the GPIO function table, I2C speeds, drive currents or absolute maximums; those are marked as gaps.

**Sources**: [pico-2-datasheet](sources.md#pico-2-datasheet)

**Last updated**: 2026-10-11

---

All numbers below are **spec** values from the datasheet unless marked otherwise. "Sec." is the datasheet section. The extracted text does not contain the pinout drawings (Figure 2, Figure 4 are images), so every pin-to-function mapping that only appears in those figures is marked *(needs source)* here and has to be checked by eye against the PDF.

## The board and the chip

| Item | Value | Source |
|------|-------|--------|
| Chip | RP2350A, with 4 MB on-board flash | [pico-2-datasheet §1](sources.md#pico-2-datasheet) |
| Cores | Dual Arm Cortex-M33 or dual Hazard3 (RISC-V), up to 150 MHz | §1 |
| SRAM | 520 kB | §1 |
| Peripherals | 2 x UART, 2 x I2C, 2 x SPI, 16 x PWM channels, 3 PIO blocks | §1 |
| Hardware security | SHA-256 acceleration and a hardware TRNG are listed for RP2350 | §2 |
| GPIO | 26 multi-function GPIO on the headers (the chip has 30) | §1, §5.2 |
| IO voltage | Fixed at 3.3 V on Pico 2 (chip supports 1.8 to 3.3 V) | §1, §5.2 |
| Size | 51 x 21 mm, 40 pins on a 2.54 mm pitch, breadboard compatible | §3 |
| Operating temperature | -20 to 85 C including self-heating; recommended maximum ambient 70 C | §3.3 |

Compared with the original Pico (RP2040): 150 versus 133 MHz, 520 versus 264 kB SRAM, 4 versus 2 MB flash (§2). This is the Pico 2 datasheet, not the RP2040 one.

The board datasheet defers chip details (peripheral registers, I2C and UART limits, pad electricals) to the RP2350 datasheet; those are on the [RP2350](rp2350.md) page.

## Pins stated in the text

Section 3.1 states these pin numbers in prose:

| Physical pin | Name | What the text says |
|--------------|------|--------------------|
| 40 | VBUS | Micro-USB 5 V input, nominally 5 V, or 0 V if USB is absent |
| 39 | VSYS | Main system input, 1.8 to 5.5 V; feeds the on-board 3.3 V supply |
| 37 | 3V3_EN | Enable of the 3.3 V supply, pulled up to VSYS by 100 kohm; shorting it low turns off the 3.3 V and the chip |
| 36 | 3V3 | 3.3 V output for external circuits |
| 35 | ADC_VREF | ADC reference |
| 33 | AGND | Analogue ground for GPIO26 to 29 |
| 30 | RUN | Chip enable with about 50 kohm internal pull-up; shorting it low resets the chip |

Not stated in the text *(needs source: Figure 2 and Figure 4 of the datasheet)*: the physical pin of GP0, GP1, GP4 and GP5; which pins are ground; the default and alternate functions (UART0, I2C0, I2C1, SPI) of each GPIO. Pin numbers other than the seven above, including pin 38, appear only in the figures.

Internal GPIOs (§3.1): GPIO23 controls the supply power-save mode, GPIO24 senses VBUS, GPIO25 drives the LED, GPIO29 measures VSYS/3. None of these is on our wiring.

## Power

| Item | Value and conditions | Source |
|------|----------------------|--------|
| VBUS | 5 V +/- 10 percent (so 4.5 to 5.5 V, our arithmetic) | §3.3 |
| VSYS | 1.8 V min, 5.5 V max | §3.3 |
| 3V3 output | Maximum depends on chip load and VSYS; datasheet recommends keeping the load under 300 mA | §3.1 |
| USB to VSYS | Through a Schottky diode, so VSYS is VBUS minus the diode drop; VBUS and VSYS may be shorted if USB is the only source | §5.4, §5.5 |
| Second supply | Feed VSYS through its own Schottky diode or a P-channel MOSFET, never straight in | §5.5 |
| USB host mode | Must be powered with 5 V on VBUS | §5.5 |

Typical VBUS (5 V) current, with the USB cable connected and GPIOs unconnected except GPIO0 and GPIO1 on a Debug Probe (§4.1). The datasheet says these are typical indications, not guaranteed maxima.

| Use case | VBUS current | Power |
|----------|--------------|-------|
| USB boot mode, idle | 5.47 mA | 27.4 mW |
| USB boot mode, peak during boot | 14.26 mA | 71.3 mW |
| hello_serial (constant serial output) | 12.43 mA | 62.1 mW |
| CoreMark, single core | 9.38 mA | 46.9 mW |
| Low-power states P1.0 to P1.7 | 0.148 to 0.233 mA | 0.74 to 1.16 mW |
| RUN held low | 0.388 mA | 1.9 mW |

These are Pico-board numbers with no IMU, no display and no radio; they are not our measurements. Our own power data goes in [results.md](../results.md). The datasheet says nothing about the current a Raspberry Pi 4 USB port can supply *(needs source)*; see [Raspberry Pi 4](raspberry-pi-4.md).

## GPIO and electrical limits

- **Logic level**: GPIO is powered from the on-board 3.3 V rail and is fixed at 3.3 V (§5.2).
- **5 V tolerance**: this board datasheet does **not** state it. The chip datasheet does: GPIO0 to 25 tolerate inputs up to 5.5 V only while IOVDD is powered at 3.3 V, and GPIO26 to 29 do not ([RP2350](rp2350.md)). This datasheet only says the ADC-capable GPIO26 to 29 must not exceed 3.3 V plus about 300 mV, and that GPIO0 to 25 do not have this restriction when the chip is unpowered (§5.2). We still run 3.3 V logic everywhere.
- **Drive current per pin, absolute maximum ratings, input thresholds, internal pull-up values on GPIO**: not in this board datasheet; see [RP2350](rp2350.md) (input high at least 2.0 V and input low at most 0.8 V at 3.3 V I/O; internal pull-up 32 to 86 kohm).
- **SWD**: the chip has internal pull-ups on SWDIO and SWCLK, nominally 60 kohm each (§5.8).

## Programming, reset, debug

- **BOOTSEL**: power the board off, hold the BOOTSEL button, then connect USB. It appears as a mass storage drive; copying a `.uf2` file onto it writes the flash and restarts (§5.1). The boot code is in ROM and cannot be overwritten by accident.
- **Reset**: short RUN (pin 30) low (§3.1).
- **SWD**: a 3-pin header on the lower edge of the board (§5.8). Getting started steps are in a separate document, not this one.
- **Software**: the C/C++ SDK and the MicroPython SDK are both mentioned as supported (§1 tip); this datasheet does not help choose between them.

## Errata

The datasheet has no errata section. Its release history lists only typo fixes and a toolchain change (Appendix D). Chip-level errata are in the RP2350 datasheet: none for I2C or UART, and one GPIO leakage erratum on early silicon ([RP2350](rp2350.md)). This board datasheet says nothing about I2C or UART behaviour, so use the chip datasheet for those.

## How this bears on our wiring

The pin numbers and UART0/I2C0 functions that this datasheet shows only in images are settled by the
[Pico 2 pinout](pico-2-pinout.md) poster, which confirms all seven items below. The table records what the
*datasheet text* alone states.

Checked against the Build Guide wiring: Pico A pin 6 = GP4 = I2C0 SDA, pin 7 = GP5 = I2C0 SCL, pin 36 = 3V3(OUT) to the IMU, pin 38 = GND, pin 1 = GP0 UART0 TX, pin 2 = GP1 UART0 RX, pin 3 = GND.

| Wiring item | Datasheet result |
|-------------|------------------|
| Pin 36 = 3V3(OUT) | **Matches** (§3.1). Fine for an IMU well under 300 mA |
| 3.3 V logic on UART and I2C | **Matches**: IO is fixed at 3.3 V (§5.2), the same level as the Pi 4 header *(Pi side: see its page)* |
| Pin 38 = GND, pin 3 = GND | **Not stated in the text**; shown only in the pinout figure |
| Pin 1 = GP0, pin 2 = GP1 | **Not stated in the text**. The datasheet does say GPIO0 and GPIO1 carry a serial console to the Debug Probe in its current tests (§4.1.2), which suggests they are UART pins, but the UART0 assignment itself is not given here. The pinout poster and the chip datasheet both confirm GP0/GP1 as UART0 TX/RX ([Pico 2 pinout](pico-2-pinout.md), [RP2350](rp2350.md)) |
| Pin 6 = GP4, pin 7 = GP5, I2C0 SDA and SCL | **Not stated in the text**; confirmed by the pinout poster and the chip datasheet ([Pico 2 pinout](pico-2-pinout.md), [RP2350](rp2350.md)) |
| 115200 8N1 on UART0, I2C speed | **Not stated here**; 8N1 is supported and 115200 is far below the UART ceiling, and I2C runs at 100 kHz or 400 kHz ([RP2350](rp2350.md)) |
| Powering each Pico from the Pi's USB port | **Supported** in principle: micro-USB VBUS powers the board (§5.5), 5 V +/- 10 percent. Typical draw of the bare board is about 12 mA at 5 V when running a serial demo (§4.1.2) |
| Pico power from a Pi USB port while a Pi UART is also wired | Datasheet silent on any ground-loop or back-power issue *(needs source)* |

Practical consequences for a student: use the USB cable as the only power source, never feed 5 V into 3V3 or a GPIO, and do not connect 5 V signals (including a Pi pin that happens to be 5 V) to any GPIO, because tolerance is not documented here.

## Related pages

- [Pico 2 pinout](pico-2-pinout.md)
- [SSD1306 OLED driver](ssd1306.md)
- [RP2350](rp2350.md)
- [Raspberry Pi 4](raspberry-pi-4.md)
- [MPU-6050](mpu-6050.md)
- [OpenSSL TLS 1.3 and the hybrid group](openssl-tls13-hybrid.md)
