# MPU-6050 registers

**Summary**: The register-level facts a driver needs for the MPU-6050 on Pico A: wake-up, range and rate setting, the data registers and the temperature formula, plus a minimal bring-up sequence built only from the register map document.

**Sources**: [rm-mpu-6000a](sources.md#rm-mpu-6000a), [mpu-6050-datasheet](sources.md#mpu-6050-datasheet)

**Last updated**: 2026-10-11

---

All values are **spec** values from the InvenSense register map document, Revision 4.0 (9 Mar 2012), which calls itself preliminary. Section numbers are the document's own (§4.n); page numbers come from its contents list. Electrical and timing limits stay on [MPU-6050](mpu-6050.md).

## Identity and address

| Item | Value | Source |
|------|-------|--------|
| `WHO_AM_I` register | `0x75` (117), read only | [rm-mpu-6000a](sources.md#rm-mpu-6000a) §4.34, p. 46 |
| `WHO_AM_I` value | `0x68` (power-on value of bits 6:1 is `110100`; bits 7 and 0 read 0) | §4.34 |
| What it holds | The upper 6 bits of the 7-bit I2C address. The lowest address bit comes from the AD0 pin and is **not** reflected in the register | §4.34 |

The register map document never lists the two slave addresses itself. The `0x68` (AD0 low) and `0x69` (AD0 high) addresses are on [MPU-6050](mpu-6050.md) from the datasheet ([mpu-6050-datasheet](sources.md#mpu-6050-datasheet) §9.2), and agree with the `WHO_AM_I` text above. So the chip answers `0x68` from `WHO_AM_I` regardless of AD0.

## Reset state

The reset value is `0x00` for every register except `PWR_MGMT_1` (`0x40`) and `WHO_AM_I` (`0x68`) (§3, p. 8). The document also states that the device comes up in **sleep mode** at power-up (§4, p. 9), which matches `0x40` having the `SLEEP` bit set. The reset value for `PWR_MGMT_1` was corrected in revision 3.2 and again in 4.0 (§1, p. 4), so check you have Revision 4.0.

## Power management and clock

`PWR_MGMT_1` is at `0x6B` (107), read/write (§4.30, p. 41).

| Bit | Name | Meaning |
|-----|------|---------|
| 7 | `DEVICE_RESET` | 1 resets all registers to defaults; clears itself when done |
| 6 | `SLEEP` | 1 puts the chip in sleep mode. **Clear it to wake the chip** |
| 5 | `CYCLE` | With `SLEEP` = 0, cycles between sleep and single accelerometer samples |
| 3 | `TEMP_DIS` | 1 disables the temperature sensor |
| 2:0 | `CLKSEL` | Clock source (below) |

| `CLKSEL` | Clock source |
|----------|--------------|
| 0 | Internal 8 MHz oscillator (the power-up default) |
| 1, 2, 3 | PLL with X, Y or Z gyroscope reference |
| 4, 5 | PLL with external 32.768 kHz or 19.2 MHz reference |
| 6 | Reserved |
| 7 | Stops the clock, timing generator held in reset |

The document says it is **highly recommended** to select a gyroscope (or external) clock instead of the internal oscillator, for stability (§4.30). `PWR_MGMT_2` (`0x6C`, §4.31, p. 43) holds per-axis standby bits and the low-power wake rate; it resets to `0x00`, so all axes are active.

## Rate and filter

`SMPLRT_DIV` is at `0x19` (25) and `CONFIG` at `0x1A` (26), both read/write (§4.2 p. 11, §4.3 p. 13).

- Sample rate = gyroscope output rate / (1 + `SMPLRT_DIV`), `SMPLRT_DIV` an 8-bit value (§4.2). The sensor data registers, FIFO and motion detection all run at this sample rate.
- Gyroscope output rate is 8 kHz when the filter is disabled (`DLPF_CFG` = 0 or 7) and 1 kHz when it is enabled (1 to 6). The accelerometer output rate is 1 kHz, so above 1 kHz the same accelerometer sample repeats (§4.2).
- `CONFIG` bits 5:3 are `EXT_SYNC_SET` (FSYNC pin sampling; 0 = off) and bits 2:0 are `DLPF_CFG` (§4.3).

| `DLPF_CFG` | Accel bandwidth (Hz) | Gyro bandwidth (Hz) | Gyro rate (kHz) |
|-----------|----------------------|---------------------|-----------------|
| 0 | 260 | 256 | 8 |
| 1 | 184 | 188 | 1 |
| 2 | 94 | 98 | 1 |
| 3 | 44 | 42 | 1 |
| 4 | 21 | 20 | 1 |
| 5 | 10 | 10 | 1 |
| 6 | 5 | 5 | 1 |
| 7 | reserved | reserved | 8 |

**Estimate (ours):** with the filter off, the divider's maximum of 255 gives 8000 / 256 = 31.25 Hz, so a 10 Hz sample rate needs the filter **on** (`DLPF_CFG` 1 to 6). With it on, `SMPLRT_DIV` = 99 gives 1000 / 100 = 10 Hz. With the reset values (`SMPLRT_DIV` 0, `DLPF_CFG` 0) the sample rate is 8 kHz.

## Ranges

`GYRO_CONFIG` is at `0x1B` (27) and `ACCEL_CONFIG` at `0x1C` (28) (§4.4 p. 14, §4.5 p. 15). Bits 7:5 of each are the self-test enables (`XG_ST` etc. and `XA_ST` etc.). `FS_SEL` and `AFS_SEL` are the 2-bit field in bits 4:3 (register map, §3 p. 6).

| Select value | Gyro range (`FS_SEL`) | Gyro LSB per °/s | Accel range (`AFS_SEL`) | Accel LSB per g |
|--------------|-----------------------|------------------|-------------------------|-----------------|
| 0 | ±250 °/s | 131 | ±2 g | 16384 |
| 1 | ±500 °/s | 65.5 | ±4 g | 8192 |
| 2 | ±1000 °/s | 32.8 | ±8 g | 4096 |
| 3 | ±2000 °/s | 16.4 | ±16 g | 2048 |

Sensitivities are from §4.18 (p. 30) and §4.20 (p. 32). Writing `0x00` to both registers selects ±250 °/s and ±2 g with self-test off.

## Data registers

Registers `0x3B` to `0x48` (59 to 72), read only, hold the latest measurements (§4.18 to §4.20, pp. 30 to 32). Each value is a high byte (`_H`, lower address) followed by a low byte (`_L`), so the order is **big-endian** and matches our UART frame.

| Address | Content | Format |
|---------|---------|--------|
| `0x3B` to `0x40` | `ACCEL_XOUT`, `ACCEL_YOUT`, `ACCEL_ZOUT` (H then L each) | 16-bit two's complement |
| `0x41`, `0x42` | `TEMP_OUT` | 16-bit signed |
| `0x43` to `0x48` | `GYRO_XOUT`, `GYRO_YOUT`, `GYRO_ZOUT` | 16-bit two's complement |

So the order is ax, ay, az, temperature, gx, gy, gz: 14 contiguous bytes from `0x3B` to `0x48`. Our six-value frame skips the temperature word.

The values are written at the sample rate into an internal set, and copied to the readable set whenever the serial interface is idle. A **burst read** of these registers therefore returns values from one sampling instant. Single-byte reads must be matched to one instant by the host, using the Data Ready interrupt (§4.18). The document says nothing about I2C register auto-increment or the exact burst length *(needs source)*.

Temperature: degrees C = `TEMP_OUT` (signed) / 340 + 36.53 (§4.19, p. 31). The datasheet's 340 LSB/°C and offset are consistent with this ([MPU-6050](mpu-6050.md)).

## FIFO, interrupts, self-test (brief)

- **FIFO**: `FIFO_EN` `0x23` selects which data go in; `USER_CTRL` `0x6A` bit 6 enables it; `FIFO_COUNT_H/L` `0x72`/`0x73` (read H first); `FIFO_R_W` `0x74`. On overflow the oldest data are lost (§4.7, §4.29, §4.32, §4.33).
- **Interrupts**: `INT_ENABLE` `0x38` bit 0 `DATA_RDY_EN`; `INT_STATUS` `0x3A` bit 0 `DATA_RDY_INT`, cleared on read (§4.16, §4.17).
- **Self-test**: `SELF_TEST_X/Y/Z/A` at `0x0D` to `0x10` hold factory-trim codes (§4.1, p. 9). Gyro self-test is to be run at ±250 °/s and accelerometer self-test at ±8 g. Pass limits are in the datasheet, not here.

## Warnings in the document

- Revision 4.0 is marked preliminary information (§2).
- On the MPU-6050, always write 0 to `I2C_IF_DIS` (`USER_CTRL` bit 4) (§4.29).
- `SIGNAL_PATH_RESET` (`0x68`) does not clear the sensor registers; `USER_CTRL` `SIG_COND_RESET` does (§4.27, §4.29).

## Minimal bring-up sequence

Built only from the document. Each write is to the register at the I2C address chosen by AD0.

1. Wait for power-up and supply settling *(needs source: the register map gives no delay; see the datasheet start-up figures)*.
2. Read `WHO_AM_I` (`0x75`); expect `0x68`. Stop and report if it differs (§4.34).
3. Optionally write `0x80` to `PWR_MGMT_1` (`0x6B`) to reset registers, then wait for the bit to clear *(needs source: no reset delay stated; the bit is stated to self-clear)*.
4. Write `PWR_MGMT_1` (`0x6B`) with `SLEEP` = 0 and `CLKSEL` = 1, that is `0x01`, to wake the chip and use the X gyroscope PLL clock (§4.30). Derived from the bit table.
5. Write `GYRO_CONFIG` (`0x1B`) = `0x00` for ±250 °/s and `ACCEL_CONFIG` (`0x1C`) = `0x00` for ±2 g (§4.4, §4.5).
6. Write `CONFIG` (`0x1A`) with `DLPF_CFG` in 1 to 6 (for example `0x03`, 44 Hz accelerometer bandwidth), then `SMPLRT_DIV` (`0x19`) = 99 for a 10 Hz sample rate (estimate above). A faster rate just needs a smaller divider.
7. Wait for the first sample after waking *(needs source: no wake or settling time stated)*.
8. Every 100 ms, start at register `0x3B` and read 14 bytes (a repeated-start read with register address auto-increment) *(needs source for the I2C transaction form and auto-increment)*; combine each H and L pair big-endian into a signed 16-bit value.

## Related pages

- [MPU-6050](mpu-6050.md)
- [Pico 2](pico-2.md)
- [Telemetry format](../telemetry_format.md)
