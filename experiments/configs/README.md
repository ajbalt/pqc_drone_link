# Experiment configs

One file per experiment. **Never change an experiment by editing code**; change or add a config.
No runner exists yet (`scripts/run_demo.sh` and the experiment scripts are planned), so the file format below is a
proposal *(proposed)*: plain `key=value` lines.

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

## Recorded with every run (not part of the config)

Git commit hash, OpenSSL version, configuration hash, board IDs, date, clock source and offset, CPU temperature and
frequency (the Pi 4 throttles at 85 degrees C), and the loss method actually applied. See
[../../CONTRIBUTING.md](../../CONTRIBUTING.md) and [../../evidence/README.md](../../evidence/README.md).
