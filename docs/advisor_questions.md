# Advisor Questions

Questions sent to Dr. El-Hadedy about the scope of PQC-LINK. They exist because two
course documents describe different designs; see
[guide_comparison.md](guide_comparison.md) for the full comparison.

- **Sent:** 2026-10-10
- **Status:** Answered 2026-10-10 by email from the advisor. The Build Guide is the
  authoritative scope and the TLS design replaces the custom stack. Q1–Q4, Q7, Q9, Q12
  and Q14 are settled. Q5 and Q10 are partly settled. Q6, Q8, Q11, Q13 and Q15 were not
  addressed (see "Still open" below).
- The full email is not stored in git. The decisions are in [scope.md](scope.md).

## A. Which design governs

| # | Question | Answer |
|---|----------|--------|
| 1 | Should we replace our custom ML-KEM + Ascon link with the Build Guide's OpenSSL TLS design? Or do you want both, with TLS as the physical demo and the custom link as a research comparison? | **TLS replaces custom.** The Build Guide is the authoritative implementation and acceptance document. No custom ML-KEM handshake or second application-layer security protocol in the mandatory system. (Email, 2026-10-10) |
| 2 | Is Ascon-AEAD128 still required anywhere? The Build Guide never mentions it, but the original project defined it as the lightweight traffic protection. | **Not required.** Ascon is a possible research extension only after L1–L10 pass. It needs a defined research question, an independently specified protocol, and the advisor's approval. Do not layer it on TLS. |
| 3 | Should we measure only the hybrid group (`X25519MLKEM768`), or also pure ML-KEM-768 (`MLKEM768`, available in OpenSSL 3.5)? Pure ML-KEM is what the original research question ("replace classical with ML-KEM") asks about. | **Optional Configuration C.** Pure ML-KEM-768 *primitive* benchmarking with OpenSSL 3.5+: keygen, encaps and decaps latency, public-key and ciphertext sizes, CPU and memory. It is not a TLS session and must not be reported as one. It must not delay A and B. |
| 4 | For the T10 comparison, is "classical" X25519 over TLS 1.3? Do you also want an unsecured baseline (no crypto) in the final results, or only as the L4 diagnostic? | **Mandatory T10 = classical X25519 TLS vs hybrid `X25519MLKEM768` TLS**, 100+ successful handshakes each, failures recorded separately. Same hardware, network, cert policy, conditions and cipher suite. The unsecured link is for L4 diagnostics only and is not part of T10. |

## B. Metrics and measurement

| # | Question | Answer |
|---|----------|--------|
| 5 | The original lists "energy per handshake". A multimeter can't resolve a single handshake. May we add a current sensor (INA219/INA260) on Pi A, or is DMM supply current enough? | **Partly answered.** Required: Pi and Pico supply voltage, system current and power. The instrument (DMM vs INA219/INA260) is not specified, and per-handshake energy is not mentioned. Follow-up needed. |
| 6 | Is rekeying required? The original measures rekey time, and the Build Guide never mentions it. TLS 1.3 KeyUpdate doesn't repeat the key exchange, so we would define "rekey" as a fresh handshake (reconnect). Is that acceptable? | **Not addressed.** The mandatory design has no rekey. Treat it as out of scope until told otherwise. |
| 7 | Should the final write-up still answer "when is the PQC overhead acceptable?" (the original definition of done)? | **Replaced.** The research question is now the measured performance, communication and energy cost of hybrid PQ key establishment in a physically implemented telemetry system, and its behaviour under communication failures. The report must separate hybrid key establishment from PQ authentication. |

## C. Scope and technical details

| # | Question | Answer |
|---|----------|--------|
| 8 | Is there any command traffic from Pi B to Pi A beyond ACKs? The original says "telemetry/command link". | **Not addressed.** The Build Guide design is records one way plus ACKs. Assume no command channel. |
| 9 | Do you want Wi-Fi only, or should SiK/LoRa radio remain a stretch goal? | **Wi-Fi only.** Dedicated access point. SiK/LoRa is not mentioned, so it is out of scope. |
| 10 | Should loss (0/10/30%) be emulated with `tc netem` on the Pis, or shaped at the access point? Since TCP hides most loss from the application, should handshake latency under loss be reported at the TLS level, with the 60 s outage and replay covered by the queue tests? | **Partly answered.** Loss is emulated at 0%, 10% and 30%. The method (`tc netem` vs the AP) is not stated. The TLS handshake benchmark and application telemetry latency must be measured separately. Queue capacity, sample rate, test duration and accepted-record count must be specified, and overflow reported. |
| 11 | May Pi services be written in Python (`ssl` module on OpenSSL 3.5+), or must they be C? Our repo currently mandates C17. | **Not addressed.** Follow-up needed. |
| 12 | Authentication is a pinned classical demo certificate under the claims boundary. Should ML-DSA appear anywhere (for example a sidebar comparison), or is it explicitly out of scope? | **Certificate authentication per the Build Guide** (pinned demo cert, hostname verified). ML-DSA is not mentioned. The report must say hybrid key establishment is not PQ authentication. |
| 13 | The Picos only handle sensor and display I/O, and all cryptography runs on Pi 4s. Does that satisfy "constrained robot", or should we also characterize a more constrained endpoint? | **Not addressed.** The Build Guide platform stands (Pi 4B for crypto, Pico 2 for I/O). |

## D. Process

| # | Question | Answer |
|---|----------|--------|
| 14 | Given the 28-week plan, can we treat the original 4-week milestones (baseline, ML-KEM, AEAD, rekey/loss) as superseded by gates G0–G8? | **Yes.** L1–L10 in order is mandatory. No Ascon, custom protocol or FPGA work until L1–L10 are completed and approved. |
| 15 | May we reuse our existing measurement code (timer and CSV logger) for the evidence files, as long as the output matches the required CSV names? | **Not addressed.** Follow-up needed. |

## Still open

Ask these at the next review (the advisor said implementation questions can go to the
meetings):

- **Q5** Which instrument for current and power, and is per-handshake energy needed?
- **Q10** How is loss emulated (`tc netem` on the Pis or at the AP)?
- **Q11** The team decided to write the Pi services in C. Confirm this is acceptable (Python was the alternative).
- **Q6** Confirm there is no rekey requirement.
- **Q8** Confirm there is no command channel.
- **Q13** and **Q15** Confirm the Pi 4B platform and that we may reuse the timer/logger.
- **New:** queue capacity, sample rate (the Build Guide says 10 Hz), test duration and
  the accepted-record target must be specified by us (advisor item 6). They are not
  set yet.

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

## When answers arrive (done for the first reply)

1. Fill in the **Answer** column above, with the date and who answered.
2. Update the decision pages in `docs/research/decisions/` and `open-questions.md`, and
   tick the matching items under "Open decisions" in README.md.
3. If any rule changes, update CLAUDE.md and CONTRIBUTING.md together, keeping crypto
   rule numbers identical.
4. Update the "On hold" note and the status table in [design_notes.md](design_notes.md),
   and add a progress log row.
5. If the Build Guide governs, ingest it with `/ingest` and add it to the research wiki
   sources (it is not in the wiki yet).
