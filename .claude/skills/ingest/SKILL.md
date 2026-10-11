---
name: ingest
description: Ingest a source (PDF, RFC, web page, datasheet) into the PQC drone link research wiki in docs/research/. Registers it, extracts it on the Windows side, discusses takeaways, updates topic and decision pages, then runs the linter. Use when the user says /ingest, "add this paper/datasheet to the wiki", or drops a file into PQC_SOURCES/raw.
---

# Ingest a source into the research wiki

The schema is `docs/research/README.md`. Read it first every time; it overrides
anything here. This skill only adds the machine-specific steps.

**Arguments:** a filename in the source drive's `raw/` folder, a URL, or nothing. With
nothing, list the files in `raw/` that have no entry in `docs/research/sources.md`, and
ask which one to ingest.

## Paths

| | WSL path | Windows path |
|---|---|---|
| Sources | `/mnt/c/Users/balta/OneDrive/PQC_SOURCES/raw/` | `C:\Users\balta\OneDrive\PQC_SOURCES\raw\` |
| Extracted | `/mnt/c/Users/balta/OneDrive/PQC_SOURCES/extracted/` | `...\PQC_SOURCES\extracted\` |
| Extractor | `/mnt/c/Users/balta/.local/bin/extract-pdfs.exe` | (Windows exe, runs from WSL) |

The extractor is a Windows program, so pass it **Windows paths**: `$(wslpath -w ...)`.

## Steps

1. **Get the source.**
   - If it's a URL to a web page or RFC, save it into `raw/` as `.md` or `.html`.
   - If it's a PDF URL, ask the user to download it into `raw/`, so OneDrive backs up
     the real file.
   - Never modify files in `raw/`.
2. **Register it** in `docs/research/sources.md`, alphabetically by ID: title, authors or
   org, year, URL/DOI, licence, filename. Choose a short, stable ID (`fips-203`,
   `sp-800-232`, `rfc-7748`, `sik-radio-docs`), because IDs never change once pages
   cite them. NIST and other US-government publications are public domain; assume
   anything else is copyrighted.
3. **Extract it** (PDFs only), using the commands in the "Extraction" section of
   `docs/research/README.md`. Check the output size against the page count. If tables
   came out garbled, rerun with `--backends pdfplumber docling`. A font-scrambled or
   math-image PDF is rare for these sources; if it happens, the maintainer's personal
   wiki at `/mnt/c/Users/balta/OneDrive/LLM_WIKI/CLAUDE.md` documents the
   `ocrmypdf --force-ocr` and Nougat fixes. Use them only if needed.
4. **Read the extracted text** (not the PDF) in full. For a long standard, read
   the sections that matter to us properly and skim the rest.
5. **Discuss before writing anything.** Give the user:
   - The 3 to 6 takeaways that matter to *this project*: numbers, constraints, and
     anything that changes a design choice or a CONTRIBUTING.md rule.
   - Which decision pages it bears on, and whether it moves any of them toward a
     recommendation.
   - Which pages you plan to create and update.

   Wait for the go-ahead.
6. **Write the pages**, following the README's page format, links-and-citations,
   numbers, and copyright rules. Merge into existing topic pages rather than creating
   near-duplicates. Ask about name collisions.
7. **Cross-link.** For each new page, search the other pages for plain-text mentions
   of its title and link the first one on each page. Don't link inside code, headings
   or the metadata lines. Report any ambiguous matches instead of guessing.
8. **Update `index.md`**, and the Status column for any decisions touched.
9. **Add a log entry at the top of `log.md`**, in the format the README describes:
   - the source ID and the extraction backend actually used
   - pages created and updated, as backticked paths relative to `docs/research/`
   - collisions and contradictions, and how they were resolved
10. **Lint:** run `python3 scripts/wiki_lint.py` and fix everything it reports.
11. **Report:**
    - pages created and updated
    - decisions affected
    - anything that should now change in code or rules (e.g. a new size constant, an
      MTU assumption)
    - the files to commit

## Rules

- **Never commit** the PDFs or the extracted text. Only `docs/research/` and the
  linter go in git.
- **Our own measurements never go in the wiki;** they belong in `docs/results.md`.
- **If a source contradicts our code or CONTRIBUTING.md** (for example, a size
  constant in `kem.h`), flag it to the user. Don't change code from inside an ingest.
