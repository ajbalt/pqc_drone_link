# Course Guide Comparison

Two course documents describe this project differently. This page records both, where
they conflict, and what we have decided to do until the advisor answers
([advisor_questions.md](advisor_questions.md)).

Source documents (course materials, not stored in git):

- **Research Guide** — *RSCL Undergraduate Research and Competition Guide, Fall 2026
  (Expanded)*, page 8, "PQC-1: Quantum-Secure Drone Link". This is the original project
  and the basis of the current repo.
- **Build Guide** — *RSCL PQC LINK ECE Build Guide V2*, "LINK / PURPLE" pages. Designed by
  Dr. Mohamed El-Hadedy. Describes a physical prototype and is dated October 2026.

What the Build Guide requires is in [scope.md](scope.md) (system, configurations,
measurements, tests T01-T10) and [schedule.md](schedule.md) (gates L1-L10).

## Where the two guides conflict

| Topic | Research Guide (original) | Build Guide (LINK) |
|-------|---------------------------|--------------------|
| Key establishment | Integrate ML-KEM between two endpoints | TLS 1.3 `X25519MLKEM768` via OpenSSL; no custom ML-KEM handshake |
| Traffic protection | ASCON-AEAD with the established key | TLS record layer; Ascon never mentioned |
| PQC type | Pure ML-KEM replaces classical | Hybrid X25519+ML-KEM |
| Platform | Constrained robot, drone, radio | Two Pi 4B over Wi-Fi; Picos only do sensor and display I/O |
| Traffic | Telemetry and command | One-way IMU to OLED with ACKs |
| Baseline | Unsecured baseline benchmarked in week 1 | L4 plain socket is a diagnostic; T10 compares hybrid vs classical |
| Energy | Energy per handshake, power logged | DMM supply current, `evidence/power.csv` |
| Rekeying | Rekey time measured | Not mentioned; recovery is reconnect plus replay |
| Definition of done | Reproducible comparison with an explanation of when overhead is acceptable | Evidence-based PASS on gates and tests, 100-point rubric |
| Timeline | 4 weeks | 28 weeks, G0–G8 |
| Roles | Crypto, networking, measurement, attack/test | One owner per node (Pico A, Pi A, Pi B, AP and measurement) |

Where they agree: loss testing, handshake latency and byte counts, negative tests,
reproducibility, and no bare algorithm benchmarks.

## Resolution (2026-10-10)

The advisor replied: the **Build Guide governs**. The custom ML-KEM + Ascon design is
retained only as a possible research extension after L1-L10. See [scope.md](scope.md) for
the final scope and [advisor_questions.md](advisor_questions.md) for each answer.

- The custom stack (`kem*`, `mlkem_shim`, `handshake`, `fragment`, `rekey`, Ascon, the
  `ml-kem` and `ascon-c` submodules) is not part of the mandatory system. It was archived
  on 2026-10-10 (tag `custom-stack-v1`, branch `archive/custom-stack`) and removed from `main`.
- CLAUDE.md, CONTRIBUTING.md and README were rewritten for the new scope on 2026-10-10.
- Work now: the five review deliverables ([schedule.md](schedule.md)), then L1-L4 hardware
  bring-up, then OpenSSL 3.5 (L5).
