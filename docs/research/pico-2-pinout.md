# Raspberry Pi Pico 2 Pinout

**Summary**: The official Raspberry Pi pinout poster for Pico 1 and Pico 2: physical pin number, GPIO name and the UART, I2C, SPI and ADC functions of every header pin. It settles the pins the [Pico 2 datasheet](pico-2.md) only showed as images, and it confirms all seven items of our Pico wiring.

**Sources**: [pico-2-pinout](sources.md#pico-2-pinout)

**Last updated**: 2026-10-11

---

All entries are **spec** values read from the poster. The poster is one page of vector graphics with the labels drawn as outlines, so text extraction returns only the title and legend. The pin table below was read by eye from a rendering of the page (scale 4), not from extracted text. The poster covers Pico 1 and Pico 2 together ("For Raspberry Pi Pico 1 and Raspberry Pi Pico 2"), so it shows one shared header layout. It prints no notes, warnings or electrical limits.

## Reading the poster

- Pin numbers 1 to 20 run down the left edge (USB at the top), 21 to 40 up the right edge.
- Each row lists the GPIO and its alternate functions. Colour legend as printed: red power, dark grey ground, purple UART, light green GPIO/PIO/PWM, dark green ADC, magenta SPI, blue I2C, pink system control, orange debugging.
- The legend marks each of UART, SPI and I2C with a light and a dark shade, "X / X (default)". The darker shade is the default pin for that bus. Which shade a given label has is a visual reading and is the least certain part of this page; the pin numbers and function names are clear.
- Not stated: the GPIO function select setting, drive strength, voltage limits, pull-ups. See [Pico 2](pico-2.md) for what the datasheet gives.

## Our wiring against the pinout

| Wiring item (Build Guide) | Result | What the poster shows |
|---------------------------|--------|-----------------------|
| Pin 1 = GP0 = UART0 TX | **Confirmed** | Pin 1 is GP0, with UART0 TX in the dark (default) shade, also I2C0 SDA and SPI0 RX |
| Pin 2 = GP1 = UART0 RX | **Confirmed** | Pin 2 is GP1, UART0 RX dark (default), also I2C0 SCL and SPI0 CSn |
| Pin 3 = GND | **Confirmed** | Pin 3 is GND |
| Pin 6 = GP4 = I2C0 SDA | **Confirmed** | Pin 6 is GP4, I2C0 SDA in the dark (default) shade; also UART1 TX and SPI0 RX |
| Pin 7 = GP5 = I2C0 SCL | **Confirmed** | Pin 7 is GP5, I2C0 SCL dark (default); also UART1 RX and SPI0 CSn |
| Pin 36 = 3V3(OUT) | **Confirmed** | Pin 36 is labelled 3V3(OUT) |
| Pin 38 = GND | **Confirmed** | Pin 38 is GND |

Pin 1 and 2 also carry I2C0 SDA and SCL as alternates, and pins 6 and 7 also carry UART1; our use of UART0 on pins 1/2 and I2C0 on pins 6/7 does not clash because each pin is used for one function only. The poster does not say which function is selected at power-up; that is set in software *(needs source: RP2350 datasheet)*.

## 40-pin table

Defaults (dark shade) in bold. "-" means no label on the poster.

| Pin | Name | Functions listed |
|-----|------|------------------|
| 1 | GP0 | **UART0 TX**, I2C0 SDA, SPI0 RX |
| 2 | GP1 | **UART0 RX**, I2C0 SCL, SPI0 CSn |
| 3 | GND | ground |
| 4 | GP2 | I2C1 SDA, SPI0 SCK |
| 5 | GP3 | I2C1 SCL, SPI0 TX |
| 6 | GP4 | UART1 TX, **I2C0 SDA**, SPI0 RX |
| 7 | GP5 | UART1 RX, **I2C0 SCL**, SPI0 CSn |
| 8 | GND | ground |
| 9 | GP6 | I2C1 SDA, SPI0 SCK |
| 10 | GP7 | I2C1 SCL, SPI0 TX |
| 11 | GP8 | UART1 TX, I2C0 SDA, SPI1 RX |
| 12 | GP9 | UART1 RX, I2C0 SCL, SPI1 CSn |
| 13 | GND | ground |
| 14 | GP10 | I2C1 SDA, SPI1 SCK |
| 15 | GP11 | I2C1 SCL, SPI1 TX |
| 16 | GP12 | UART0 TX, I2C0 SDA, SPI1 RX |
| 17 | GP13 | UART0 RX, I2C0 SCL, SPI1 CSn |
| 18 | GND | ground |
| 19 | GP14 | I2C1 SDA, SPI1 SCK |
| 20 | GP15 | I2C1 SCL, SPI1 TX |
| 21 | GP16 | SPI0 RX, I2C0 SDA, UART0 TX |
| 22 | GP17 | SPI0 CSn, I2C0 SCL, UART0 RX |
| 23 | GND | ground |
| 24 | GP18 | SPI0 SCK, I2C1 SDA |
| 25 | GP19 | SPI0 TX, I2C1 SCL |
| 26 | GP20 | I2C0 SDA |
| 27 | GP21 | I2C0 SCL |
| 28 | GND | ground |
| 29 | GP22 | - |
| 30 | RUN | system control (reset) |
| 31 | GP26 | ADC0, I2C1 SDA |
| 32 | GP27 | ADC1, I2C1 SCL |
| 33 | AGND | analogue ground (ground colour, ADC-section label) |
| 34 | GP28 | ADC2 |
| 35 | ADC_VREF | ADC reference |
| 36 | 3V3(OUT) | power |
| 37 | 3V3_EN | system control |
| 38 | GND | ground |
| 39 | VSYS | power |
| 40 | VBUS | power |

Shading was re-checked against the rendered poster by the lead: only GP0/GP1 (pins 1 and 2) show UART0 in the dark default shade; GP12/GP13 and GP16/GP17 show it in the light shade. Pins 4, 5, 9 and 10 are I2C1 in the lighter blue, and the default I2C0 shade appears on pins 6 and 7. The default-shade reading for the pins we do not use is less certain; our wiring uses pins 1, 2, 6 and 7 only.

Every pin that is not power or ground is a GPIO except RUN, 3V3_EN, ADC_VREF and AGND. Ground pins: 3, 8, 13, 18, 23, 28, 33 (AGND), 38. The pin numbers for VBUS (40), VSYS (39), 3V3_EN (37), 3V3 (36), ADC_VREF (35), AGND (33) and RUN (30) agree with the prose in [Pico 2 §3.1](sources.md#pico-2-datasheet).

## Other items on the poster

- The onboard LED is labelled as GP25, drawn at the top of the board.
- Three debug pads at the bottom edge are labelled SWCLK, GND and SWDIO (orange, debugging; the middle one is ground).
- Pin 35 (ADC_VREF) and pins 26/27 (GP20/GP21) are drawn with a long leader line to their only label.

Source for the whole page: [pico-2-pinout](sources.md#pico-2-pinout).

## Related pages

- [Pico 2](pico-2.md)
- [SSD1306 OLED driver](ssd1306.md)
- [MPU-6050](mpu-6050.md)
- [Raspberry Pi 4](raspberry-pi-4.md)
