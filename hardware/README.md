# Hardware

Schematic, pin map, PCB and photos for the physical prototype. **Phase 2:** hardware work waits until the
computer-only link is verified (advisor, 2026-10-16); nothing here needs to be bought or built yet. The Build Guide requires a KiCad schematic, a
carrier/interconnect PCB (or a justified alternative) and an exact cable pinout. See
[../docs/block_diagram.md](../docs/block_diagram.md) for the diagram and wiring summary.

| File | State | What it is |
|------|-------|------------|
| `pin_map.csv` | Draft | Every connection by physical header pin number. Taken from the Build Guide wiring and checked against the pinout sources in the wiki; **`verified_on_bench` stays `no` until someone checks the real cable with a meter and a teammate initials it** |
| `board_schematic.pdf` | Not started | KiCad schematic export |
| `pcb/` | Not started | KiCad project for the two carrier boards. Needs advisor approval before any order |
| `actual_photo.jpg` | Not started | Photo of the assembled prototype |

## Rules

- "Pin" always means the **physical header pin number**, not BCM or GP numbering. GP names are in the
  signal column.
- Wire with power off. Cross TX into RX. Share ground. No UART cable powers another device. Never power a load
  from a GPIO or the Pi header. See the electrical checklist in the Build Guide and gate L1.
- Record header photos before powering up, and put them in `../evidence/bench/`.
- Change `pin_map.csv` and the schematic together. A pin that appears in one but not the other is a bug.

## Sources for the pin map

Pico pins and functions: [Pico 2 pinout](../docs/research/pico-2-pinout.md). Pi header pins:
[Raspberry Pi 4](../docs/research/raspberry-pi-4.md). IMU supply range and I2C:
[MPU-6050](../docs/research/mpu-6050.md). OLED address and supply:
[SSD1306](../docs/research/ssd1306.md).
