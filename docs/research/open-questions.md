# Open Questions

**Summary**: Questions raised while ingesting sources that the team still has to
answer. Each one lists what it affects, the options, the evidence so far, and when it
must be settled. Answered questions stay here, with the answer and date.

**Sources**: [fips-203](sources.md#fips-203), [fips-204](sources.md#fips-204), [sp-800-227](sources.md#sp-800-227), [sp-800-232](sources.md#sp-800-232), [sp-800-56c](sources.md#sp-800-56c), [sp-800-57pt1](sources.md#sp-800-57pt1), [sp-800-90c](sources.md#sp-800-90c), [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd), [sx1276-datasheet](sources.md#sx1276-datasheet), [rfd900-datasheet](sources.md#rfd900-datasheet), [lorawan-rp-1-0-3](sources.md#lorawan-rp-1-0-3)

**Last updated**: 2026-10-08

---

## How to use this page

- When a question is answered, fill in **Answer** with the date and who decided, and
  change **Status** to Answered.
- Update the decision or topic pages it affects.
- If the answer creates a rule, add the rule to CONTRIBUTING.md.
- Bigger choices with several options and evidence get their own page in
  `decisions/`. This page is for the smaller questions, and for questions that cut
  across several decisions.

| # | Question | Needed by | Status |
|---|----------|-----------|--------|
| Q1 | How "NIST-compliant" must the project be? | Week 2 (KDF) | Open |
| Q2 | Where would SHA-256 / SHA-3 code come from? | Week 2 (KDF), if an approved KDF is chosen | Open |
| Q3 | Is tag truncation in scope? | Week 2 (packet format) | Open |
| Q4 | Does SP 800-133 cover our KDF-derived Ascon keys? | Before the KDF decision is final | Answered (draft source) |
| Q5 | Implicit or explicit key confirmation? | Week 2 (handshake spec) | Open |
| Q6 | Should too many `decrypt_fail` events force a rekey? | Week 4 (`rekey.c`) | Open |
| Q7 | How do we state the `getrandom()` deviation? | Threat model; before results | Answered (wording) |
| Q8 | Should crypto rule 3 name `src/crypto/` as the module boundary? | Any time | Open |
| Q9 | What byte order do the nonce and KDF fields use? | Week 2 (protocol spec) | Open |
| Q10 | Does X25519 count as an approved SP 800-56A scheme? | Hybrid mode; citing X25519 as 128-bit | Open |
| Q11 | Mutual or one-way authentication? | Week 2 (handshake spec) | Open |
| Q12 | Where would an ML-DSA library come from? | Only if ML-DSA is tested | Open |
| Q13 | One PSK per drone or per fleet? | Week 2 (if PSK chosen) | Open |
| Q14 | Which radio hardware, and which region? | Radio decision | Open |
| Q15 | Raw LoRa or LoRaWAN? | Only if LoRa is used | Open |
| Q16 | Fragment header size and minimum `link_mtu` | Week 2 (`fragment.c`, protocol spec) | Open |
| Q17 | Should the logger record computed airtime and radio overhead? | Week 1–2 (`logger.c`) | Open |
| Q18 | Which radio settings must every experiment pin? | Week 4 (experiment configs) | Open |

---

## Q1. How "NIST-compliant" must the project be?

**Status**: Open · **Answer**: —

Should the write-up claim only that we *use NIST-standardized primitives* (ML-KEM,
Ascon), or that the *protocol follows NIST guidance* throughout?

- **Why it matters:** this decides whether three known deviations are acceptable if
  stated, or must be removed:
  - an Ascon-XOF128 KDF is not approved
    ([sp-800-232 §6](sources.md#sp-800-232), [sp-800-56c §4.1, §7](sources.md#sp-800-56c))
  - `getrandom()` is not shown to be an SP 800-90 RBG (Q7)
  - Ascon can't be used for explicit key confirmation
    ([sp-800-227 App. C.1](sources.md#sp-800-227)) (Q5)
  - keys are to be generated inside a FIPS 140-validated module
    ([sp-800-133r3-ipd §2.2](sources.md#sp-800-133r3-ipd)), which a research prototype
    on a Raspberry Pi is not
- **Options:**
  - **(a) "Uses NIST primitives":** deviations are allowed if documented in
    `threat_model.md` and the results.
  - **(b) "Follows NIST guidance":** requires an approved KDF (and so Q2), an approved
    MAC for any explicit key confirmation, and an RBG argument.
- **Affects:** [KDF construction](decisions/kdf-construction.md),
  [Hybrid mode](decisions/hybrid-mode.md), Q2, Q5, Q7.

## Q2. Where would SHA-256 / SHA-3 code come from?

**Status**: Open · **Answer**: —

If an approved KDF wins (HKDF-SHA256, a one-step SHA3-256 KDF, or KMAC), we need a hash
implementation we can call from C.

- **What we have:** SHA-3 exists only as ml-kem's fetched, header-only C++ dependency.
  Using it would need a second C++ shim and would mean managing ml-kem's dependencies,
  both forbidden by CONTRIBUTING.md. There is no SHA-256, HMAC or KMAC (see
  [Key derivation](key-derivation.md#what-we-have-available)).
- **Options:**
  - **(a)** Add a small C SHA-256/HMAC library as a new pinned submodule in
    `third_party/` (which library is TBD), wrapped in `src/crypto/`.
  - **(b)** Add a C SHA-3/KMAC library as a submodule.
  - **(c)** Stay with Ascon-XOF128 and accept "non-approved KDF" (depends on Q1).
- **Rules that apply:** a new dependency must be a pinned submodule, never modified,
  with known-answer tests run through our wrapper (CONTRIBUTING.md).

## Q3. Is tag truncation in scope?

**Status**: Open · **Answer**: —

Should the radio experiments include shorter Ascon tags (for example 64 or 32 bits) to
save bytes, or always use the full 16-byte tag?

- **Evidence:**
  - tags may be truncated to no less than 32 bits
  - under 64 bits needs a written risk analysis, and the tag length is fixed for the
    key's lifetime
  - under 64 bits, every decryption failure should force a rekey
  - security drops to λ bits

  ([sp-800-232 §4.2.1, R4, R5, §4.4.1](sources.md#sp-800-232))
- **Options:**
  - **(a)** Full tags only. Simplest, and the comparison stays clean.
  - **(b)** Add truncation as an experiment variable on slow links, with the R4 risk
    analysis written up and the R5 rekey rule implemented.
- **Affects:** [Radio link](decisions/radio-link.md), `messages.h`, Q6.

## Q4. Does SP 800-133 cover our KDF-derived Ascon keys?

**Status**: Answered (draft source) · **Answer**: 2026-10-08, from the SP 800-133 Rev. 3
initial public draft. Confirm against the final.

SP 800-232 R1 says Ascon keys **shall** be generated per SP 800-133 with an approved
RBG ([sp-800-232 §4.3](sources.md#sp-800-232)). Our Ascon keys come out of a KDF.

- **Answer: covered, but only if both the KEM and the KDF are approved.**
  - Keys derived during key agreement, or with a KDF, count as indirectly RBG-generated
    ([sp-800-133r3-ipd §2.1](sources.md#sp-800-133r3-ipd)).
  - An approved KEM's shared secret is itself a symmetric key, and keys derived from it
    must use an approved KDM built on hashes, HMAC, AES-CMAC or KMAC
    ([sp-800-133r3-ipd §5.2–5.2.2](sources.md#sp-800-133r3-ipd)).
- **Consequences:**
  - An Ascon-XOF128 KDF is not approved. The draft names Ascon-XOF only for seed
    expansion in key-pair generation (§4.2.2).
  - X25519-derived keys qualify only if X25519 is an approved scheme (Q10).
  - The RBG at the root inherits Q7.
- **Feeds:** [KDF construction](decisions/kdf-construction.md) and Q1.
- **To do:** get SP 800-133 Rev. 2 (current final), or the Rev. 3 final when it is
  published, to confirm.

## Q5. Implicit or explicit key confirmation?

**Status**: Open · **Answer**: —

Because of ML-KEM's implicit rejection, a tampered ciphertext is silent until keys are
used ([fips-203 §6.3](sources.md#fips-203)). How should our handshake confirm both sides
hold the same key?

- **Options:**
  - **(a) Implicit:** the first Ascon-AEAD packet that verifies counts as confirmation
    ([sp-800-227 §4.4](sources.md#sp-800-227)). There are no extra messages. NIST names
    this as an example, but makes "no statement as to the adequacy" of methods other
    than the explicit one. A tampered handshake then shows up as `decrypt_fail` instead
    of `handshake_fail`.
  - **(b) Explicit:** a MAC over the SP 800-227 MAC_Data format, with a dedicated KC key
    ([sp-800-227 §4.4.1–4.4.2](sources.md#sp-800-227)). This adds one or two messages
    and needs an approved MAC (HMAC, AES-CMAC or KMAC), so it depends on Q2.
  - **(c) Explicit, with an Ascon-AEAD "finished" message:** an encrypted confirmation
    message under the new key, carrying a transcript hash. It is a form of (a) with its
    own message, giving a clean `handshake_done` / `handshake_fail` boundary for
    measurements.
- **New evidence:** key-agreement output "shall not" be used to protect and send data
  until there is assurance of correct identifiers, of keys bound to the right entities,
  and of correctly derived keys (for example by key confirmation)
  ([sp-800-57pt1 §8.1.5.2.3](sources.md#sp-800-57pt1)). It is written for SP 800-56A/B
  key agreement, but favours option (b) or (c) over (a). Choosing (a) would mean
  documenting it as a deviation.
- **Also:** in any option, the transcript (identifiers, ciphertext, encapsulation key)
  should go into the KDF's FixedInfo, following the MAC_Data layout
  ([sp-800-227 §4.4.1](sources.md#sp-800-227)).
- **Affects:** `docs/protocol_spec.md`, `handshake.c`, what `handshake_done` means in
  the logs, and the handshake-latency metric. See [KEM usage](kem-usage.md#key-confirmation).

## Q6. Should too many `decrypt_fail` events force a rekey?

**Status**: Open · **Answer**: —

SP 800-232 R7 says the key **should** be updated when the decryption-failure limit is
reached: 2^96 for full tags, 1 for tags under 64 bits
([sp-800-232 §4.3](sources.md#sp-800-232)). Today, CONTRIBUTING.md forces a rekey only
on counter overflow.

- **Options:**
  - **(a)** Add the R5 limit as a rekey trigger. It only matters in practice if Q3 says
    yes to truncation.
  - **(b)** Also add a lower, configurable threshold, as an attack signal.
  - **(c)** Counter overflow and the configured interval only.
- **Affects:** `rekey.c`, CONTRIBUTING.md crypto rule 4, [Rekeying](rekeying.md).

## Q7. How do we state the `getrandom()` deviation?

**Status**: Answered (wording) · **Answer**: 2026-10-08. `getrandom()` is not an approved
RBG as deployed. Use the deviation statement in
[Random bit generation](random-bit-generation.md#where-getrandom-stands) in
`docs/threat_model.md`. Whether to replace the backend depends on Q1.

FIPS 203, SP 800-227 and SP 800-232 all require randomness from an approved RBG
(SP 800-90A/B/C) ([fips-203 §3.3](sources.md#fips-203),
[sp-800-227 §3.1](sources.md#sp-800-227), [sp-800-232 R1](sources.md#sp-800-232)). - An approved RBG needs an SP 800-90A DRBG, SP 800-90B validated entropy sources, a
  validated SP 800-90C construction, and a FIPS 140 module
  ([sp-800-90a §10](sources.md#sp-800-90a), [sp-800-90b §3](sources.md#sp-800-90b),
  [sp-800-90c §2.2](sources.md#sp-800-90c),
  [sp-800-133r3-ipd §2.2](sources.md#sp-800-133r3-ipd)).
- SP 800-90C's RBGC construction allows for OS RBGs in principle
  ([sp-800-90c §7](sources.md#sp-800-90c)). None of the documents mentions Linux or
  `getrandom()`, so none approves it.

## Q8. Should crypto rule 3 name `src/crypto/` as the module boundary?

**Status**: Open · **Answer**: —

FIPS 203 says the seed-taking internal functions should not be exposed to
applications, and that seeds must be generated by the cryptographic module
([fips-203 §3.3, §6](sources.md#fips-203)). Our library only offers seed-taking
functions, so `rng.c` passes seeds into the shim. That is consistent if `src/crypto/` is
the module.

- **Proposed:** add one sentence to CONTRIBUTING.md crypto rule 3, saying that
  `src/crypto/` is the cryptographic module boundary, and that no seed or internal KEM
  function is reachable from outside it. See
  [ML-KEM](ml-kem.md#randomness-and-the-internal-functions).

## Q9. What byte order do the nonce and KDF fields use?

**Status**: Open · **Answer**: —

Ascon is little-endian ([sp-800-232 App. A](sources.md#sp-800-232)), while the
SP 800-56C counter and length fields are big-endian
([sp-800-56c §4.1](sources.md#sp-800-56c)).

- **To do:** `docs/protocol_spec.md` must define the exact byte layout of the 16-byte
  nonce (epoch, direction, counter) and of every FixedInfo field. `test_nonce.c` must
  check it.

## Q10. Does X25519 count as an approved SP 800-56A scheme?

**Status**: Open · **Answer**: —

For a hybrid combiner to be approved, at least one input secret must come from an
approved scheme ([sp-800-227 §4.6.2](sources.md#sp-800-227)). ML-KEM satisfies that on
its own, so this only affects how the write-up describes X25519 *(needs source: SP
800-56A Rev. 3 and/or SP 800-186)*.

- **Checked, not covered:** SP 800-57 Pt 1, SP 800-90A/B/C and SP 800-133r3 don't
  mention X25519 or Curve25519.
- **New wrinkle:** read literally, SP 800-57's strength table puts a ~253-bit-order curve
  in the 112-bit row ([sp-800-57pt1 Table 2](sources.md#sp-800-57pt1); the curve order
  is outside knowledge), while X25519 is commonly treated as 128-bit. Calling the
  `x25519` baseline "128-bit" needs SP 800-186.

## Q11. Mutual or one-way authentication?

**Status**: Open · **Answer**: —

Must both ends authenticate each other, or only the ground station to the drone? A
command link arguably needs both: the drone must trust commands, and the ground station
must trust telemetry.

- **Why it matters:** one signature authenticates only its signer, so mutual ML-DSA
  doubles the signature bytes (to +4840 or +6618 B). A PSK authenticates both sides at
  no cost ([ML-DSA](ml-dsa.md#what-it-would-cost-our-handshake)).
- **Affects:** [Handshake authentication](decisions/handshake-auth.md), the protocol
  spec.

## Q12. Where would an ML-DSA library come from?

**Status**: Open · **Answer**: —

Only relevant if ML-DSA is tested. A C++ header-only library like our ml-kem would need
a second C++ shim, which CONTRIBUTING.md forbids. A C library would be a new pinned
submodule with KATs run through our wrapper.

- **Options:**
  - **(a)** Relax the one-C++-file rule for an ML-DSA shim.
  - **(b)** Use a C library as a new submodule.
  - **(c)** Don't test ML-DSA.

## Q13. One PSK per drone or per fleet?

**Status**: Open · **Answer**: —

If the PSK option is chosen: is there one PSK per drone, or one shared by all drones?
How is it installed, stored, and rotated? What is the threat model for a captured
drone?

- **Why it matters:** with a fleet-wide PSK, one captured drone exposes every link to
  active attack. Per-drone PSKs limit the damage, but need provisioning. A long-term PSK
  is a key-derivation key with a suggested cryptoperiod of about a year
  ([sp-800-57pt1 Table 1](sources.md#sp-800-57pt1)).
- **Affects:** [Handshake authentication](decisions/handshake-auth.md),
  `docs/threat_model.md`.

## Q14. Which radio hardware, and which region?

**Status**: Open · **Answer**: —

Which radio does the team have or plan to buy: the original RFD900 (2013 datasheet), an
RFD900x/ux, or a 3DR/Holybro SiK radio, and which firmware? Do experiments run under US
rules (400 ms dwell) or EU rules (1% duty cycle)?

- **Why it matters:** the LoRa conclusions differ hugely by region, and the 2013
  datasheet may not match current hardware
  ([LoRa](lora.md#legal-limits), [SiK radios](sik-radios.md)).

## Q15. Raw LoRa or LoRaWAN?

**Status**: Open · **Answer**: —

If LoRa is used: raw LoRa point-to-point with our own framing, or LoRaWAN?

- **Evidence:** LoRaWAN adds MAC overhead, its own security, and 1–2 s receive windows,
  and its US DR0 payload leaves 3 bytes per fragment after our header
  ([lorawan-rp-1-0-3 §2.3.9, Tables 15–16](sources.md#lorawan-rp-1-0-3)).
- **Suggested:** raw LoRa.

## Q16. Fragment header size and minimum link_mtu

**Status**: Open · **Answer**: —

Should the protocol spec define a compact fragment header (2–4 bytes) for small-payload
links? Or a minimum `link_mtu` below which `mlkem*` modes are reported as infeasible?

- **Why it matters:** with an 8-byte header and an 11-byte payload (LoRaWAN US DR0),
  73% of every packet is our header ([LoRa](lora.md#what-this-means-for-ml-kem)).
- **Affects:** `docs/protocol_spec.md`, `fragment.c`, `messages.h`.

## Q17. Should the logger record computed airtime and radio overhead?

**Status**: Open · **Answer**: —

CONTRIBUTING.md says byte counts include *all* on-air overhead, but radios add bytes our
logger can't see:
- LoRa's preamble, header, CRC and FEC, which can be computed
  ([sx1276-datasheet §4.1.1.7](sources.md#sx1276-datasheet))
- SiK's ECC, resends and injected packets, which cannot
  ([rfd900-datasheet Fig. 8.3](sources.md#rfd900-datasheet))

- **Options:**
  - **(a)** Log a computed airtime and on-air byte count per fragment, where possible,
    and document what can't be seen.
  - **(b)** Count only our bytes, and state the limitation.

## Q18. Which radio settings must every experiment pin?

**Status**: Open · **Answer**: —

SiK radios can retransmit, add error correction, inject packets and throttle themselves
([rfd900-datasheet Fig. 8.3, §6](sources.md#rfd900-datasheet)). Which settings must
every experiment config fix and record?

- **Proposed minimum:** ECC, MAVLINK, OP_RESEND, AIR_SPEED, SERIAL_SPEED, DUTY_CYCLE,
  NUM_CHANNELS and TX power. For LoRa: SF, BW, CR, preamble, header mode and CRC.
- **Affects:** `experiments/configs/`, `run_experiment.sh`.

## Related pages

- [KDF construction](decisions/kdf-construction.md)
- [Handshake authentication](decisions/handshake-auth.md)
- [Hybrid mode](decisions/hybrid-mode.md)
- [Radio link](decisions/radio-link.md)
