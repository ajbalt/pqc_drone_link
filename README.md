# PQC-1: Quantum-Secure Drone Link

We measure the mission-level cost of replacing classical key establishment with
**ML-KEM** (FIPS 203) on a constrained drone telemetry/command link, with traffic
protected by **Ascon-AEAD128** (NIST SP 800-232).

**Research question:** what latency, radio, energy, and compute penalties appear when a
constrained robot replaces classical key establishment with ML-KEM?

The deliverable is a **reproducible comparison** of three modes over the same link:

| Mode       | Key establishment | Traffic protection |
|------------|-------------------|--------------------|
| `none`     | none              | none (plaintext)   |
| `x25519`   | X25519 ECDH       | Ascon-AEAD128      |
| `mlkem768` | ML-KEM-768        | Ascon-AEAD128      |

ML-KEM-512 and ML-KEM-1024 are secondary comparisons. The **only** variable between the
secured modes is the key exchange. Results are always presented with network and mission
context (packet loss, link rate, rekey timing), never as bare algorithm benchmarks.

> **Before changing any code, read [CONTRIBUTING.md](CONTRIBUTING.md).** It holds the
> rules every change must follow (crypto, memory, logging, testing, code style).
> [docs/design_notes.md](docs/design_notes.md) explains *why* the code is designed the
> way it is, and tracks current status.

## Getting the code

The crypto libraries are git submodules, so clone with `--recurse-submodules`:

```sh
git clone --recurse-submodules https://github.com/ajbalt/pqc_drone_link.git
# or, in an existing clone:
git submodule update --init --recursive
```

## Building and testing

Requires CMake 3.28+, a C17 compiler, and a C++20 compiler.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first configure needs internet (ml-kem deps)
cmake --build build
ctest --test-dir build --output-on-failure
```

- Rerun the `cmake -B build ...` configure step only after editing `CMakeLists.txt` or
  changing a cache option.
- To rebuild offline after the first configure (e.g. in the field), add
  `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`. Deleting `build/` requires internet again.
- Choose an optimized Ascon implementation with e.g. `-DASCON_IMPL=opt64`
  (default `ref`).
- Benchmarks and experiments must use `Release` builds. Debug numbers are never reported.
- Endpoints that run ML-KEM must be 64-bit (e.g. Raspberry Pi with a 64-bit OS).

## API documentation

Doxygen is optional and not part of the default build:

```sh
cmake --build build --target docs
# then open build/docs/html/index.html
```

The `docs` target only exists if Doxygen was installed when CMake was configured.

## Repo layout

```
pqc_drone_link/
├── README.md                  # This file: what the project is, how to build
├── CONTRIBUTING.md            # Rules every change must follow
├── CMakeLists.txt             # Build system (works on Pi and microcontrollers)
├── third_party/               # External crypto libraries (git submodules, pinned)
│   ├── ml-kem/                # itzmeanjan/ml-kem (C++20 header-only ML-KEM)
│   └── ascon-c/               # Ascon-AEAD128 + Ascon-XOF128 (asconaeadxof128)
│                              # X25519 library: TBD (see Open decisions)
├── src/
│   ├── common/
│   │   └── errors.h           # Negative error codes returned by all functions
│   ├── crypto/                # Crypto/software role
│   │   ├── mlkem_shim.h       # Plain C interface to ML-KEM
│   │   ├── mlkem_shim.cpp     # The only C++ file (C++20)
│   │   ├── rng.h              # rng_bytes(), secure_wipe(), ct_memcmp()
│   │   ├── rng.c              # OS CSPRNG (getrandom()); all randomness comes from here
│   │   ├── kem.h              # Common interface every key exchange implements
│   │   ├── kem.c              # Mode lookup + length checks for every KEM
│   │   ├── kem_mlkem.c        # ML-KEM-512/768/1024
│   │   ├── kem_x25519.c       # Classical baseline
│   │   ├── kem_none.c         # Unsecured baseline
│   │   ├── kdf.c              # Shared secret -> session key
│   │   └── aead_ascon.c       # Encrypt/decrypt packets
│   ├── protocol/              # Embedded networking role
│   │   ├── messages.h         # Packet formats and header layout
│   │   ├── handshake.c        # Handshake state machine
│   │   ├── fragment.c         # Split big ML-KEM keys across small packets
│   │   ├── session.c          # Nonce counters, replay window
│   │   └── rekey.c            # Mid-mission rekeying
│   ├── transport/
│   │   ├── udp.c              # For development and netem testing
│   │   └── serial_radio.c     # For real SiK/LoRa radios later
│   ├── metrics/               # Measurement/benchmarking role
│   │   ├── timer.h / timer.c  # Monotonic wall time and process CPU time
│   │   └── logger.h / logger.c # Buffers events in memory, writes CSV logs
│   └── app/
│       ├── drone.c            # Drone endpoint: sends telemetry
│       ├── ground_station.c   # Ground endpoint: sends commands
│       └── telemetry_gen.c    # Realistic fake telemetry at fixed rates
├── tests/                     # Attack/test role
│   ├── test_kem.c             # kem.h interface: lookup, length checks, every mode
│   ├── test_rng.c             # rng_bytes(), secure_wipe(), ct_memcmp()
│   ├── test_metrics.c         # timer.c and logger.c
│   ├── test_kem_vectors.c     # Known-answer tests against official vectors
│   ├── test_fragment.c        # Reassembly with lost/reordered fragments
│   ├── test_replay.c          # Replayed packets must be rejected
│   └── test_nonce.c           # Nonces never repeat, even across rekeys
├── experiments/
│   ├── configs/               # One file per experiment (KEM, loss rate, rekey interval)
│   └── run_experiment.sh      # Runs a config N times, saves logs
├── scripts/
│   ├── netem_setup.sh         # Simulate packet loss and delay
│   └── power_logger.py        # Reads the INA219/INA260 current sensor
├── analysis/
│   ├── parse_logs.py          # CSV logs -> tidy data
│   └── plots.ipynb            # Charts for the final comparison
├── results/                   # Raw logs (large ones gitignored or stored elsewhere)
└── docs/
    ├── design_notes.md        # Why the code looks the way it does; status and progress log
    ├── protocol_spec.md       # Exact handshake and packet formats
    ├── threat_model.md        # What attacks we defend against, and which we don't
    └── results.md             # Final write-up: when is the overhead acceptable?
```

Not every file exists yet; see the status table in
[docs/design_notes.md](docs/design_notes.md).

## Four-week plan

- **Week 1:** unsecured baseline link (`kem_none`), transport, telemetry generator,
  logging, submodules and build working on both endpoints.
- **Week 2:** ML-KEM (via the shim) and X25519 key establishment between two endpoints,
  fragmentation.
- **Week 3:** Ascon-AEAD128 protection of telemetry with derived session keys, nonces,
  replay window.
- **Week 4:** rekeying and packet-loss experiments, analysis, results write-up.

## Open decisions

Tick these off here as the team decides.

- [ ] Drone-side hardware (must be 64-bit if it runs ML-KEM, e.g. Raspberry Pi 4/5)
- [ ] Real radio: SiK 915 MHz, LoRa, or UDP-only
- [ ] X25519 library for the classical baseline
- [ ] Handshake authentication: pre-shared key mixed into KDF (prototype default) vs
      ML-DSA signatures
- [ ] KDF construction: Ascon-XOF128 (already in our Ascon build) vs HKDF-SHA256
- [ ] Optimized `ASCON_IMPL` for benchmarking on the chosen hardware
- [ ] Telemetry rates and message sizes to emulate (base on MAVLink)
- [ ] Hybrid mode (X25519 + ML-KEM) as a stretch goal?
- [ ] Packet header contents (packet type, sequence number, key epoch; mode?). Settle in
      `docs/protocol_spec.md` before writing `messages.h`.
