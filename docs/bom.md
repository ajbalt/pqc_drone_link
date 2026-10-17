# Bill of Materials

Deliverable 5 for the advisor review. The Build Guide requires, for every item: quantity,
make/model, supplier URL, quoted USD, in-stock status, maximum voltage and current,
assigned team member and date, plus an approved equivalent. **Nothing is bought before the
instructor signs the BOM and the current quotations.** Prices in the Build Guide are
budget allowances, not quotes, so none are copied here.

**Phase 1 (2026-10-16): no purchases.** The advisor asked us to use existing computers
first. Nothing here is requested until Phase 2,. Pico W and Pico 2 are
both acceptable per the advisor, so use whichever is available.

**Status: DRAFT.** The part list comes from the Build Guide ("LINK: Purchase or reserve
these exact parts"). Everything marked TBD must be checked against what RSCL has in stock
and filled in. No prices are invented.

## Core parts (from the Build Guide)

| # | Item | Qty | Role | Ownership | MPN / model | Supplier URL | Quote (USD) | Max V / I | Stock | Needs approval |
|---|------|-----|------|-----------|-------------|--------------|-------------|-----------|-------|----------------|
| 1 | Raspberry Pi 4 Model B, 4 GB | 2 | Rover Pi A (TLS client, queue); relay Pi B (TLS server) | TBD (prefer RSCL stock) | TBD | TBD | TBD | 5 V, official PSU rating | TBD | Yes if not in stock |
| 2 | Official Pi power supply | 2 | Power for Pi A and B | TBD | TBD | TBD | TBD | TBD | TBD | |
| 3 | microSD card | 2 | OS and persistent queue/store; one per Pi | TBD | TBD | TBD | TBD | n/a | TBD | |
| 4 | Raspberry Pi Pico 2 | 2 | Pico A (sensor), Pico B (display) | TBD | TBD | TBD | TBD | 3.3 V GPIO | TBD | |
| 5 | MPU-6050 (or equivalent 3.3 V) I2C IMU breakout | 1 | Rover motion sensor | TBD | TBD | TBD | TBD | 3.3 V; confirm breakout pull-ups and voltage | TBD | |
| 6 | SSD1306 (or equivalent) 128x64 I2C OLED, 3.3 V | 1 | Relay display | TBD | TBD | TBD | TBD | 3.3 V; confirm module voltage compatibility | TBD | |
| 7 | 2.4 GHz Wi-Fi access point / router | 1 | Dedicated lab AP with controlled outage | TBD | TBD | TBD | TBD | TBD | TBD | |
| 8 | Bench pushbutton and LED | 1 | Mark sensor events, count delivered records | TBD | TBD | TBD | TBD | 3.3 V | TBD | |
| 9 | USB/Pi power supplies | 4 | Stable power for each node (see note) | TBD | TBD | TBD | TBD | TBD | TBD | |
| 10 | Carrier/interface PCBs | 2 | Label UART, I2C, power, display and probe points | To make | KiCad design | TBD | TBD | 3.3 V | n/a | **Yes, before ordering PCBs or connectors** |
| 11 | Logic analyzer (lab-owned) | 1 | Record UART frames | RSCL | TBD | n/a | n/a | 3.3 V probes | TBD | No |
| 12 | Digital multimeter (lab-owned) | 1 | Volts and current | RSCL | TBD | n/a | n/a | TBD | TBD | No |

Note on item 9: the Build Guide's gate L1 powers each Pico from its associated Pi's USB,
while its parts list has four supplies. Ask which is intended before buying.

## Small parts the Build Guide implies but does not list

Confirm each with the lab, and ask before buying.

| Item | Why | Ownership |
|------|-----|-----------|
| Breadboards and jumper wires | Bring-up before the PCB exists | TBD |
| USB cables (Pi to Pico, power) | Pico power and programming | TBD |
| Pull-up resistors, if the breakouts lack them | I2C | TBD |
| Header pins and connectors | Probe and test points | TBD (**approval required** for connector orders) |
| Test leads and probe clips | Scope and logic-analyzer attachment | RSCL |
| Ethernet or USB console cable per Pi | Local console so SSH is never the only management path during outage tests | TBD |
| Photo and label supplies | Header photos and labelled supplies are required evidence | TBD |

## Pending a decision

| Item | Depends on | Notes |
|------|-----------|-------|
| INA219 or INA260 current sensor | Advisor Q5 | Only if a DMM is not enough for current and power. Do not buy until answered |
| Second AP or a managed switch | Advisor Q10 | Only if loss is shaped at the AP |
| A more constrained endpoint | Advisor Q13 | Not requested; do not budget |
| FPGA board | Optional extension | Not before L1–L10 are approved |

## Lab equipment check

The team must record, with dates, which lab-owned instruments it can use and when: DMM,
2-channel scope or 8-channel logic analyzer, a PC for decoding, and any bench supply. Add
asset IDs here once known.

| Instrument | Asset ID | Booked for | Notes |
|------------|----------|-----------|-------|
| DMM | TBD | | |
| Logic analyzer / scope | TBD | | |

## Sign-off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Team lead | | | |
| Instructor | | | |

For each purchase: assigned team member, date, and the approved equivalent, if any.
