# Experiment configs

One file per experiment. **Never change an experiment by editing code**; change or add a config.
The handshake benchmark runner (`scripts/run_handshake_bench.sh`) reads this format, plain `key=value` lines, from
`t10_handshake_loopback.conf`, and the packet-loss runner (`scripts/run_loss_test.sh`) from `t08_loss_loopback.conf`.
The other experiment runners (`scripts/run_demo.sh`, outage runs) are still planned, so fields they need are *(proposed)*.

## Fields

| Field | Meaning |
|-------|---------|
| `config` | `A` (X25519) or `B` (`X25519MLKEM768`); never `DEFAULT` |
| `group` | Exact group string set on client and server |
| `cipher_suite` | The TLS 1.3 suite used for both A and B |
| `tls_min_max_version` | `1.3` for both |
| `loss_percent` | Emulated packet loss: 0, 10 or 30 |
| `loss_method` | `netem` on the Pis or the access point (advisor Q10), with the model and parameters |
| `repetitions` | Number of runs; at least 100 successful handshakes for T10 |
| `queue_capacity` | Records |
| `sample_rate_hz` | 10 unless told otherwise |
| `duration_s` | Test length |
| `outage_s` | Wi-Fi outage length, 60 for T06 |
| `seed` | Random seed, where one is used |
| `name` | Label for the run; the runner adds a UTC timestamp |
| `configs` | Which configurations run, e.g. `A,B` (interleaved) |
| `group_A`, `group_B` | Expected group strings, recorded for the reader; the code fixes them |
| `hostname` | Name the server certificate must match |
| `warmup` | Successes per configuration run first and excluded from statistics |
| `max_attempts` | Hard cap on attempts so a broken setup cannot loop forever |
| `timeout_ms` | Connect, send and receive timeout |
| `interval_ms` | Pause between attempts (0 = none) |
| `loss_percent_list` | T08: loss levels to run, e.g. `0,10,30` |
| `delay_ms`, `mtu` | T08: extra one-way delay (0 = none) and the interface MTU (1500 so messages split into packets as on Ethernet or Wi-Fi) |
| `repetitions`, `min_runs` | T08: runs per configuration and loss level, and the number needed before a result is reportable (10) |
| `records`, `rate_hz` | T08: records made per run and how fast |
| `queue_capacity`, `window` | T08: queue size and records sent before their ACKs are read |
| `server_timeout_ms` | T08: the server's read timeout |
| `backoff_ms`, `backoff_max_ms` | T08: first and longest wait between connection attempts |
| `run_deadline_s` | T08: a run not finished by then is reported as undelivered |

## Recorded with every run (not part of the config)

Git commit hash, OpenSSL version, configuration hash, board IDs, date, clock source and offset, CPU temperature and
frequency (the Pi 4 throttles at 85 degrees C), and the loss method actually applied. See
[../../CONTRIBUTING.md](../../CONTRIBUTING.md) and [../../evidence/README.md](../../evidence/README.md).
