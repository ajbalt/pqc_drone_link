# Evidence

Proof that each gate (L1 to L10) and each test (T01 to T10) really happened on the physical system. A gate
or test counts as done only when its files are here and the instructor has initialed the gate record.
The gates and the evidence file each one must produce are listed in [../docs/schedule.md](../docs/schedule.md);
the tests are in [../docs/scope.md](../docs/scope.md).

## Rules

- **Never invent, estimate or hand-edit a measurement.** Raw captures and CSV files are saved as the
  instrument or script produced them. Put any edited or summarized version in a separate file and name the
  original.
- **Failures are evidence too.** If a gate fails, keep the failed output, record what you changed, and change
  one variable at a time before retrying. Do not overwrite a failed record; add a new dated one.
- **Do not move on if a result is missing.** The next gate does not start until the previous one is initialed.
- **Development-machine results are not gate evidence.** They go in [../docs/results.md](../docs/results.md).
  Gate evidence comes from the real boards, with board IDs.

## What each gate needs (three items)

1. **The gate record**: a dated PASS or FAIL log, from [GATE_RECORD_TEMPLATE.md](GATE_RECORD_TEMPLATE.md).
   Name it `L<n>_record.md` (for a retry, `L<n>_record_<date>.md`).
2. **The code revision and board IDs**: inside the gate record (git commit hash, OpenSSL version, which Pi
   and Pico by serial number or label).
3. **An observable result**: the named output file from the schedule (for example `L2_imu_motion.csv`,
   `L3_uart_scope.png`, `L5_tls_handshake.log`) plus a photo, logic-analyzer trace or recording.

## Naming and layout

All files sit flat in this folder, named as in the schedule:

| Kind | Name | Example |
|------|------|---------|
| Gate record | `L<n>_record.md` | `L1_record.md` |
| Gate output | as listed in the schedule | `L2_imu_motion.csv` |
| Test results | in `../tests/` | `positive_and_attack_tests.csv` |
| Bench photos and captures | `bench/` | `bench/pinout_pi_a.jpg` |
| Environment log | `bootstrap.log` | `openssl version -a` output on each node |
| Power and latency | `power.csv`, `latency.csv` | |

## Large files

Videos and other large captures are **not committed**: `.gitignore` excludes `evidence/**/*.mp4` and
`*.mov`. Store them in the team's shared drive, and put the location and the SHA-256 in the gate record:

```sh
sha256sum evidence/L4_plain_diagnostic.mp4
```

*(Proposed; the team still has to pick the shared drive. Photos and logic-analyzer screenshots are small and
are committed.)*

## What never goes here

- Private keys, the demo CA key, or any certificate private part (blocked by `.gitignore`).
- Passwords, Wi-Fi credentials or anything personal.
- Third-party documents such as datasheets or the course guides.
