# CLAUDE.md

Guidance for Claude Code in this repo. It does not repeat the project rules; those live in
one place each, so read them instead of relying on a copy here:

| Need | Read |
|------|------|
| What the project is, system, gates L1–L10, layout, build, open decisions | [README.md](../README.md) |
| Every rule (crypto, protocol, measurement, testing, style, before-commit) | [CONTRIBUTING.md](../CONTRIBUTING.md) |
| Final scope, configurations, mandatory tests T01–T10 | [docs/scope.md](../docs/scope.md) |
| Record, UART frame, ACK, dedup, queue, trust configuration | [docs/telemetry_format.md](../docs/telemetry_format.md) |
| Why existing code looks the way it does, status, progress log | [docs/design_notes.md](../docs/design_notes.md) |
| Standards, papers, datasheets | [docs/research/index.md](../docs/research/index.md) |

## Working rules for Claude

- **Scope.** The advisor's scope (`docs/scope.md`) is final: OpenSSL TLS 1.3 with
  `X25519MLKEM768`, compared with classical X25519. The custom ML-KEM + Ascon stack is
  archived at tag `custom-stack-v1`. Do not restore it, add Ascon, a custom handshake or
  FPGA work, or buy parts, without the advisor's approval.
- **Stop and flag** any request that would break a rule in CONTRIBUTING.md instead of
  working around it.
- **Evidence.** Never invent, estimate or hand-edit a measurement, and never say a gate
  L1–L10 or test T01–T10 passed without its file in `evidence/` or `tests/`. Do not report
  numbers from Debug builds or single runs.
- **Guesses.** If `docs/telemetry_format.md` doesn't answer a protocol question, ask. Mark
  any suggested value *(proposed)*.
- **Claims.** Hybrid key establishment is not PQ authentication, and this is an educational
  link, not flight-approved or NASA-endorsed. Keep reports and slides accurate on this.
- **Small changes.** Prefer small, reviewable commits and explain security-relevant changes
  in the message. Commit or push only when asked.
- **Docs follow code.** When a decision is made, update the page that argues it, tick the
  README "Open decisions" item, and add any resulting rule to CONTRIBUTING.md. Update
  `docs/design_notes.md` when a module is finished.
- **Research wiki.** Read `docs/research/index.md` first for questions about standards,
  papers, radios or MAVLink, cite wiki pages, and say when the wiki doesn't cover it. Source
  PDFs live outside git at `/mnt/c/Users/balta/OneDrive/PQC_SOURCES/`. The Build Guide and
  Research Guide are not in the wiki yet. Run `python3 scripts/wiki_lint.py` after wiki edits.
- **Format.** Run `clang-format -i` on changed C files and end every file with a newline.

## Skills (in `.claude/skills/`)

`/module` (learn-by-typing workflow: Claude drafts and verifies, the user types),
`/precommit` (before-commit checklist), `/ingest` (add a source to the wiki), `/wiki-lint`
(audit the wiki).
