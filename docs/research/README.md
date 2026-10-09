# Research Wiki

The project's shared knowledge base: what the standards, papers, datasheets and specs
say, and the reasoning behind each design decision. It follows Andrej Karpathy's
"LLM Wiki" pattern. People add sources and ask questions; an LLM (Claude Code) does the
bookkeeping: summaries, cross-links, index, log, and contradiction tracking.

This wiki covers **outside knowledge** (ML-KEM, Ascon, radios, MAVLink, power
measurement, related work). How *our code* is designed lives in
[../design_notes.md](../design_notes.md), and our own measured results go in
`docs/results.md`, not here.

## Where things live

| What | Where | In git? |
|------|-------|---------|
| Source PDFs (immutable) | Maintainer's OneDrive: `C:\Users\balta\OneDrive\PQC_SOURCES\raw\` | No (copyright, size). Backed up by OneDrive |
| Extracted text of sources (disposable) | Same drive: `PQC_SOURCES\extracted\` | No |
| Source registry: citation, URL/DOI, licence, filename | [sources.md](sources.md) | Yes |
| Wiki pages | `docs/research/*.md` | Yes |
| Decision pages | `docs/research/decisions/*.md` | Yes |
| Catalog of all pages | [index.md](index.md) | Yes |
| Append-only change record | [log.md](log.md) | Yes |
| Images we drew, or public-domain figures (e.g. NIST) | `docs/research/attachments/` | Yes |

Teammates don't have the PDFs. Every source in [sources.md](sources.md) has a URL or
DOI so anyone can fetch it, and pages cite sources by ID, not by file path.

## Page types

- **Topic pages** (`ml-kem.md`, `radio-links.md`, `mavlink.md`): one concept each,
  merged from every source that covers it.
- **Source summaries** (`src-<id>.md`): what one source says, and which topic pages it
  fed. Only for substantial sources (a standard, a key paper); a datasheet table can go
  straight into a topic page.
- **Decision pages** (`decisions/<slug>.md`): one per open decision in the repo
  README: the options, the evidence for each with citations, a recommendation, and the
  outcome.
- **Comparisons and answers** (`<a>-vs-<b>.md`): good answers to questions, filed back
  so they compound.

Page names are lowercase with hyphens.

## Page format

```markdown
# Page Title

**Summary**: One or two sentences describing this page.

**Sources**: [fips-203](sources.md#fips-203), [sp-800-232](sources.md#sp-800-232)

**Last updated**: 2026-10-08

---

Main content. Short paragraphs, clear headings, tables for numbers.

## Related pages

- [ML-KEM](ml-kem.md)
```

Decision pages add two lines after **Last updated**:

```markdown
**Status**: Open | Decided 2026-10-20 | Superseded by [x](x.md)

**Decision**: (one line once decided; "—" while open)
```

From `decisions/`, links go up a level: `[fips-203](../sources.md#fips-203)`.

## Links and citations

- **Use standard markdown links**, `[ML-KEM](ml-kem.md)`, not Obsidian `[[wikilinks]]`.
  They render on GitHub and in every IDE, and Obsidian follows them too.
- **Cite by source ID**, linking into the registry:
  `([fips-203 §7.2](sources.md#fips-203))`. When a paragraph draws entirely on one
  source, one citation at the end of it is enough; cite per claim when sources are mixed
  or disagree.
- **Contradictions stay visible.** If two sources disagree, keep both claims, each with
  its citation, and say that they conflict. Never silently overwrite the older claim.
- **No source, no claim.** Anything without a source is marked *(needs source)*.

## Numbers

This project is about measuring overheads, so numbers need extra care.

- Every number carries its **unit**, its **conditions** (platform, CPU, compiler flags,
  parameter set, payload size) and its **source**.
- Say what kind of number it is:
  - **spec**: defined by a standard, e.g. key sizes in FIPS 203
  - **reported**: measured by someone else, e.g. cycle counts in a paper
  - **estimate**: derived by us here. Show the arithmetic.
- **Our own measurements never go in the wiki.** They belong in `docs/results.md`,
  under the reporting rules in CONTRIBUTING.md: Release builds only, 30+ runs, medians
  and spread.

## Copyright

- Paraphrase; quote at most a sentence or two.
- Tables of numbers (sizes, rates, timings) with a citation are fine.
- Figures from copyrighted papers or datasheets are not copied into `attachments/`:
  describe them, or redraw the idea ourselves. NIST publications are US-government works
  in the public domain, so their figures can be used with a citation.
- Extracted text stays on the source drive and is never committed.

## Workflows

These are written for whoever runs them, usually Claude Code at the maintainer's
request.

### Ingest a source

1. Put the PDF in `PQC_SOURCES\raw\` with a descriptive name.
2. Add its entry to [sources.md](sources.md): ID, full citation, URL/DOI, licence,
   filename.
3. Extract it to `PQC_SOURCES\extracted\<same name>.md` (see "Extraction" below), and
   read the extracted text rather than the PDF.
4. **Discuss the key takeaways with the user before writing anything.** In particular:
   which open decisions it bears on, and which numbers matter to us.
5. Write a source summary page if the source is substantial.
6. Create or update topic pages:
   - Before creating a page, check [index.md](index.md) for an existing page on the same
     concept, and never create near-duplicates (`ml-kem-2.md`).
   - New information on an existing page is merged where it fits, not appended at the
     end.
   - When a source would create a page whose name collides with an existing one, ask
     before creating a second page.
7. Update every decision page the source bears on.
8. Cross-link: add links to related pages, and check older pages for plain-text
   mentions of the new pages that should now be links (first mention per page only;
   ask about ambiguous ones).
9. Update [index.md](index.md) and append an entry to [log.md](log.md) (format below).
10. Run `python3 scripts/wiki_lint.py` and fix everything it reports.

One source often touches 5 to 15 pages. That is normal.

### Answer a question

1. Read [index.md](index.md) first, then the relevant pages.
2. Answer with links to the pages, and through them, the sources.
3. If the wiki doesn't cover it, say so plainly before using outside knowledge, and
   label the outside knowledge *(needs source)*.
4. If the answer is worth keeping, offer to file it as a page.

### Record a decision

When the team decides an open question:
- set the decision page's **Status** and **Decision** lines
- tick the item in the repo `README.md` "Open decisions" list
- update `CONTRIBUTING.md` if the decision creates a rule
- log it in [log.md](log.md)

### Lint

Run `python3 scripts/wiki_lint.py`, after every ingest and before committing wiki
changes. It checks for:
- orphan pages
- duplicated metadata lines
- stale **Last updated** dates
- format compliance
- broken links (including Obsidian-style `[[...]]`)
- citations to unknown source IDs
- pages missing from the index

Then review the things that need judgment:
- contradictions between pages
- concepts mentioned on several pages that have no page of their own
- claims a newer source has superseded
- missed cross-links

### Log entry format

Each entry in [log.md](log.md) is a section headed `## YYYY-MM-DD — <Ingest|Answer|Decision|Lint>: <title>`,
newest first, listing:

- the source ID(s) involved, and the extraction backend used for each
- pages created and pages updated, as backticked paths relative to `docs/research/`
  (e.g. `` `decisions/radio-link.md` ``). The linter reads these to spot stale dates.
- collisions, merges, or contradictions found, and how they were resolved

Sections whose title contains `Lint`, `cross-link`, `format update` or `tooling` don't
count as content changes for the stale-date check.

## Extraction

The maintainer's Windows machine has the extraction tools (`extract-pdfs`, with a GPU).
From WSL:

```sh
S=/mnt/c/Users/balta/OneDrive/PQC_SOURCES
X=/mnt/c/Users/balta/.local/bin/extract-pdfs.exe
"$X" "$(wslpath -w "$S/raw/<name>.pdf")" "$(wslpath -w "$S/extracted/<name>.md")" \
     --backends docling pdfplumber markitdown pdfminer
```

- `docling` comes first because it handles multi-column papers and standards best, and
  `pdfplumber` is the strongest on tables (parameter sets, datasheet specs).
- If the output is empty, garbled, or has dropped the tables, retry with a different
  backend order, or read the PDF directly. Don't block an ingest on extraction.
- Web pages (RFCs, MAVLink docs) don't need extraction. Save them as `.md` or `.html`
  in `raw/` and read them directly.
