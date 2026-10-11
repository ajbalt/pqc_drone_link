---
name: wiki-lint
description: Health-check the PQC drone link research wiki (docs/research/). Runs scripts/wiki_lint.py, then the judgment checks (contradictions, missing concept pages, superseded claims, missed cross-links, stale decisions). Use when the user says /wiki-lint, "lint/audit the wiki", or after a batch of wiki edits.
---

# Research wiki lint

The schema is `docs/research/README.md`. Read it first.

## Step 1: automated checks

```sh
python3 scripts/wiki_lint.py
```

It runs eight checks: orphans, duplicate metadata, stale dates, format, broken links,
unknown source IDs, index coverage, and broken section anchors. Fix every issue it reports, then rerun it until
it exits 0. For a stale date, set **Last updated** to the date of the log entry that
changed the page; don't use today's date unless that is when the change happened.

## Step 2: judgment checks

Read the pages. Then report:

1. **Contradictions:** conflicting claims across pages, such as two different
   ML-KEM-768 ciphertext sizes. Both claims must stay visible, each with its own
   citation.
2. **Missing concept pages:** concepts mentioned on 3 or more pages that have no page
   of their own. Offer to create them.
3. **Superseded claims:** older claims a newer source has replaced, such as a draft
   standard later revised.
4. **Missed cross-links:** plain-text mentions of existing page titles that aren't
   links. Count the first mention per page only, and skip code, headings and metadata
   lines. Apply the unambiguous ones only after the user agrees; list the ambiguous ones.
5. **Stale decisions:** decision pages still marked Open that have enough evidence for
   a recommendation, and decided items whose README "Open decisions" checkbox isn't
   ticked (or the reverse).
6. **Unsourced claims:** sentences with numbers or factual claims that have neither a
   citation nor *(needs source)*.
7. **Wiki vs code:** numbers in the wiki that our code also hard-codes, such as the
   `KEM_MAX_*` sizes in `src/crypto/kem.h`. Flag any mismatch. Don't edit the code.

Report the findings as a numbered list, each with a suggested fix. If the user
approves edits, make them, then add a log entry titled `Lint: <what was fixed>`. The
`Lint` keyword keeps it from affecting the stale-date check.
