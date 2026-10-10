# Advisor Questions

Questions sent to Dr. El-Hadedy about the scope of PQC-LINK. They exist because two
course documents describe different designs; see
[guide_comparison.md](guide_comparison.md) for the full comparison.

- **Sent:** 2026-10-10
- **Status:** Pending. Fill in each **Answer** below as replies arrive, with the date and
  who answered, then follow "When answers arrive" at the end.
- **Blocking:** Q1–Q5. Until they are answered, no handshake, protocol or packet code
  is written (see the "On hold" note in [design_notes.md](design_notes.md)).

## A. Which design governs

| # | Question | Answer |
|---|----------|--------|
| 1 | Should we replace our custom ML-KEM + Ascon link with the Build Guide's OpenSSL TLS design? Or do you want both, with TLS as the physical demo and the custom link as a research comparison? | |
| 2 | Is Ascon-AEAD128 still required anywhere? The Build Guide never mentions it, but the original project defined it as the lightweight traffic protection. | |
| 3 | Should we measure only the hybrid group (`X25519MLKEM768`), or also pure ML-KEM-768 (`MLKEM768`, available in OpenSSL 3.5)? Pure ML-KEM is what the original research question ("replace classical with ML-KEM") asks about. | |
| 4 | For the T10 comparison, is "classical" X25519 over TLS 1.3? Do you also want an unsecured baseline (no crypto) in the final results, or only as the L4 diagnostic? | |

## B. Metrics and measurement

| # | Question | Answer |
|---|----------|--------|
| 5 | The original lists "energy per handshake". A multimeter can't resolve a single handshake. May we add a current sensor (INA219/INA260) on Pi A, or is DMM supply current enough? | |
| 6 | Is rekeying required? The original measures rekey time, and the Build Guide never mentions it. TLS 1.3 KeyUpdate doesn't repeat the key exchange, so we would define "rekey" as a fresh handshake (reconnect). Is that acceptable? | |
| 7 | Should the final write-up still answer "when is the PQC overhead acceptable?" (the original definition of done)? | |

## C. Scope and technical details

| # | Question | Answer |
|---|----------|--------|
| 8 | Is there any command traffic from Pi B to Pi A beyond ACKs? The original says "telemetry/command link". | |
| 9 | Do you want Wi-Fi only, or should SiK/LoRa radio remain a stretch goal? | |
| 10 | Should loss (0/10/30%) be emulated with `tc netem` on the Pis, or shaped at the access point? Since TCP hides most loss from the application, should handshake latency under loss be reported at the TLS level, with the 60 s outage and replay covered by the queue tests? | |
| 11 | May Pi services be written in Python (`ssl` module on OpenSSL 3.5+), or must they be C? Our repo currently mandates C17. | |
| 12 | Authentication is a pinned classical demo certificate under the claims boundary. Should ML-DSA appear anywhere (for example a sidebar comparison), or is it explicitly out of scope? | |
| 13 | The Picos only handle sensor and display I/O, and all cryptography runs on Pi 4s. Does that satisfy "constrained robot", or should we also characterize a more constrained endpoint? | |

## D. Process

| # | Question | Answer |
|---|----------|--------|
| 14 | Given the 28-week plan, can we treat the original 4-week milestones (baseline, ML-KEM, AEAD, rekey/loss) as superseded by gates G0–G8? | |
| 15 | May we reuse our existing measurement code (timer and CSV logger) for the evidence files, as long as the output matches the required CSV names? | |

## What each answer changes

| Q | If the answer is... | We change... |
|---|---------------------|--------------|
| 1 | TLS replaces custom | Archive the custom stack (`kem*`, shim, `handshake`, `fragment`, `rekey`, Ascon, submodules). Rewrite CLAUDE.md, CONTRIBUTING.md and README (purpose, modes, layout, crypto rules, plan). |
| 1 | Both | Keep the custom stack as a research branch. Add the TLS prototype alongside it. |
| 1 | Custom stays | Ignore the Build Guide's TLS requirement. Keep the current plan. |
| 2 | Ascon dropped | Remove the `ascon` target and submodule. Rules 4 and 9 and the KDF decision page are moot. |
| 3 | Pure ML-KEM wanted | Add `MLKEM768` as a third TLS group to the L10 benchmark. |
| 4 | Unsecured baseline wanted | Keep a plaintext run as a third row in `link_benchmark.csv`. |
| 5 | Sensor needed | Settle the part, wire it, write `power_logger.py`. Add it to the BOM. |
| 6 | Rekey required | Define it in the spec. Add a rekey test and a `rekey_time` column. |
| 10 | netem on the Pis | Write `scripts/netem_setup.sh` for the Pi interfaces. Record the method in each config. |
| 11 | Python allowed | Drop the C17-only rule for `pi/`. Pico firmware stays C or MicroPython. |
| 12 | ML-DSA out of scope | Close [handshake-auth](research/decisions/handshake-auth.md) as "pinned classical cert". |
| 9 | Wi-Fi only | Close [radio-link](research/decisions/radio-link.md) as Wi-Fi. |

## When answers arrive

1. Fill in the **Answer** column above, with the date and who answered.
2. Update the decision pages in `docs/research/decisions/` and `open-questions.md`, and
   tick the matching items under "Open decisions" in README.md.
3. If any rule changes, update CLAUDE.md and CONTRIBUTING.md together, keeping crypto
   rule numbers identical.
4. Update the "On hold" note and the status table in [design_notes.md](design_notes.md),
   and add a progress log row.
5. If the Build Guide governs, ingest it with `/ingest` and add it to the research wiki
   sources (it is not in the wiki yet).
