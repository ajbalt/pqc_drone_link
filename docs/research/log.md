# Research Wiki Log

Append-only record of every change to the research wiki, newest first. The format is
described in [README.md](README.md#log-entry-format).

## 2026-10-08 — Ingest: 11 sources (radio, authentication, randomness and keys, MAVLink)

- **Sources**: `sx1276-datasheet`, `rfd900-datasheet`, `lorawan-rp-1-0-3`, `fips-204`,
  `expressvpn-pq-wireguard-guide`, `sp-800-90a`, `sp-800-90b`, `sp-800-90c`,
  `sp-800-133r3-ipd`, `sp-800-57pt1`, `mavlink2-guide`.
- **Extraction**:
  - All PDFs were extracted one at a time with docling.
  - SP 800-133r3 needed **pdfplumber** (docling failed silently).
  - The SX1276 time-on-air formulas were not decoded. A subagent recovered them from
    the PDF text layer, and they were verified against the LoRaWAN payload tables and
    an independent recomputation.
  - `mavlink2-guide` is markdown and was read directly.
- **Analysis**: three subagents analysed the radio, authentication and RBG/key groups in
  parallel and reported back. Their key claims were spot-checked against the extracted
  text. Pages were written after the user approved the combined takeaways.
- **Created**: `lora.md`, `sik-radios.md`, `ml-dsa.md`, `random-bit-generation.md`,
  `key-management.md`, `mavlink.md`, `src-sx1276-datasheet.md`,
  `src-rfd900-datasheet.md`, `src-lorawan-rp-1-0-3.md`, `src-fips-204.md`,
  `src-expressvpn-pq-wireguard-guide.md`, `src-sp-800-90.md` (one page for 90A/B/C),
  `src-sp-800-133r3-ipd.md`, `src-sp-800-57pt1.md`, `src-mavlink2-guide.md`.
- **Updated**:
  - `decisions/radio-link.md`: rewritten with handshake-time estimates per link and a
    tentative recommendation (UDP + netem with SiK-like, LoRa SF7 and LoRa SF10
    profiles).
  - `decisions/handshake-auth.md`: rewritten with ML-DSA sizes, the signer-role issue,
    a static-KEM option and a tentative PSK recommendation.
  - `decisions/kdf-construction.md`, `decisions/hybrid-mode.md`.
  - `open-questions.md`: Q4 and Q7 answered; new evidence for Q1, Q5 and Q10; Q11–Q18
    added.
  - `rekeying.md`, `ml-kem.md`, `ascon.md`, `key-derivation.md`, `kem-usage.md`,
    `index.md`, `sources.md`.
- **Collisions / contradictions**:
  - The ExpressVPN guide is not the academic PQ-WireGuard paper and contains a factual
    error. The user chose to keep it with a low-trust label.
  - SP 800-133r3 (draft) §5.3 is stricter than SP 800-227 §4.6.2 on combining keys.
    Both are recorded.
  - SP 800-57's strength table literally rates a ~253-bit curve at 112 bits (Q10).
- **Flagged for the team** (not changed during the ingest):
  - Add a rule that epoch n+1 keys are never derived from epoch n keys.
  - Pin and record radio settings in experiment configs (Q18).
  - Fragment header size (Q16).

## 2026-10-08 — Ingest: SP 800-227, SP 800-232, SP 800-56C Rev. 2

- **Sources**: `sp-800-227`, `sp-800-232` and `sp-800-56c`, all extracted with docling by a
  subagent, which analysed them and reported back; the pages were then written after
  the user reviewed the takeaways.
  - SP 800-227: the key-combiner and KC-key-split formulas weren't decoded; the prose
    covers both.
  - SP 800-232: undecoded formulas are only in the pseudocode.
  - SP 800-56C: clean.
  - Key claims were spot-checked against the extracted text.
- **Created**: `src-sp-800-227.md`, `src-sp-800-232.md`, `src-sp-800-56c.md`, `ascon.md`,
  `key-derivation.md`, `kem-usage.md`, `rekeying.md`, `open-questions.md`,
  `decisions/handshake-auth.md`.
- **Updated**:
  - `decisions/kdf-construction.md`: main evidence; Ascon-XOF128 not approved; HKDF and
    SHA3/KMAC approved.
  - `decisions/hybrid-mode.md`: approved combiners, the naive-combiner pitfall, X-Wing.
  - `decisions/radio-link.md`: 16-byte tag overhead; truncation trade-off.
  - `ml-kem.md`: key confirmation now sourced; single-use ephemeral keys.
  - `src-fips-203.md`, `index.md`, `sources.md`.
- **Collisions / contradictions**: no page collisions. One tension was recorded rather
  than resolved: "Ascon-XOF128 KDF" (README option) vs not approved by SP 800-56C /
  SP 800-232 §6. It is open question Q1/Q2.
- **Questions recorded**: Q1–Q10 in `open-questions.md`, for the team to answer later.

## 2026-10-08 — Ingest: FIPS 203 (ML-KEM standard)

- **Source**: `fips-203` (`raw/NIST.FIPS.203.pdf`), extracted with docling. The prose and
  Tables 1–3 were clean; some §3.2/§4.1 formulas weren't decoded, and none were needed.
- **Created**: `ml-kem.md`, `src-fips-203.md`, `decisions/kdf-construction.md`,
  `decisions/hybrid-mode.md`.
- **Updated**: `decisions/radio-link.md` (handshake payload per mode), `index.md`,
  `sources.md`.
- **Collisions / contradictions**: none. Checked our `KEM_MAX_*` constants in
  `src/crypto/kem.h` against Table 3: they match ML-KEM-1024.
- **Flagged for the team** (not changed during the ingest): the handshake needs key
  confirmation because of implicit rejection; CONTRIBUTING crypto rule 3 should name
  `src/crypto/` as the module boundary; whether `getrandom()` is an approved RBG goes
  in the threat model.

## 2026-10-08 — Tooling: wiki created

Set up the research wiki, adapted from the maintainer's personal LLM wiki.

- **Created**: `README.md` (schema), `index.md`, `sources.md`, `log.md`,
  `decisions/radio-link.md` (first decision page, no sources yet).
- **Linter**: `scripts/wiki_lint.py`.
- **Source drive**: `C:\Users\balta\OneDrive\PQC_SOURCES\` (`raw/`, `extracted/`).

