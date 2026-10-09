#!/usr/bin/env python3
"""
wiki_lint.py: health checks for the research wiki in docs/research/.

Adapted from the maintainer's personal LLM-wiki linter, switched from Obsidian
[[wikilinks]] to standard markdown links so pages render on GitHub and in any IDE.

Checks:
  1. Orphan pages        no inbound link from any other page (index.md counts)
  2. Duplicate metadata  more than one **Sources** / **Last updated** line
  3. Stale dates         **Last updated** older than the latest content entry
                         in log.md that names the page
  4. Format compliance   H1, Summary, Sources, Last updated, --- separator;
                         decision pages also need Status and Decision
  5. Broken links        relative links to missing files, and any [[wikilink]]
  6. Unknown sources     links to sources.md#<id> where <id> isn't registered
  7. Index coverage      pages not listed in index.md

Usage:   python3 scripts/wiki_lint.py
Exit:    0 = clean, 1 = issues found, 2 = wiki not found
Stdlib only.
"""

import datetime
import re
import sys
from pathlib import Path

WIKI = Path(__file__).resolve().parent.parent / "docs" / "research"
STRUCTURAL = {"README.md", "index.md", "log.md", "sources.md"}

# Log sections whose title contains one of these are metadata-only and must not
# advance a page's expected Last updated date.
LOG_SKIP_KEYWORDS = ("lint", "cross-link", "format update", "tooling")

FENCED_RE = re.compile(r"```.*?```", re.DOTALL)
INLINE_RE = re.compile(r"`[^`\n]+`")
LINK_RE = re.compile(r"!?\[[^\]\n]*\]\(([^)\s]+)\)")
WIKILINK_RE = re.compile(r"\[\[[^\]\n]+\]\]")


def rel(path: Path) -> str:
    """Path relative to the wiki root, with forward slashes."""
    return path.relative_to(WIKI).as_posix()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def prose(text: str) -> str:
    """Text with code removed, so example links in code blocks are ignored."""
    return INLINE_RE.sub("", FENCED_RE.sub("", text))


def wiki_pages() -> list[Path]:
    """Every content page (not structural files, not attachments)."""
    return sorted(
        p for p in WIKI.rglob("*.md")
        if rel(p) not in STRUCTURAL and "attachments" not in p.parts
    )


def all_md() -> list[Path]:
    return sorted(p for p in WIKI.rglob("*.md") if "attachments" not in p.parts)


def links(path: Path) -> list[tuple[Path, str, str]]:
    """Local links in a file as (resolved target, anchor, raw target)."""
    out = []
    for m in LINK_RE.finditer(prose(read(path))):
        raw = m.group(1)
        if re.match(r"^[a-z]+:", raw) or raw.startswith("#"):
            continue  # external URL, or anchor within the same page
        target, _, anchor = raw.partition("#")
        out.append(((path.parent / target).resolve(), anchor, raw))
    return out


def source_ids() -> set[str]:
    src = WIKI / "sources.md"
    if not src.exists():
        return set()
    return set(re.findall(r"^### ([a-z0-9][a-z0-9.-]*)\s*$", prose(read(src)), re.M))


# 1. Orphans ------------------------------------------------------------------

def check_orphans(pages):
    inbound = {p.resolve(): 0 for p in pages}
    for f in all_md():
        for target, _, _ in links(f):
            if target in inbound and target != f.resolve():
                inbound[target] += 1
    return [rel(p) for p in pages if inbound[p.resolve()] == 0]


# 2. Duplicate metadata -------------------------------------------------------

def check_duplicate_metadata(pages):
    issues = []
    for p in pages:
        t = read(p)
        n_src = len(re.findall(r"^\*\*Sources\*\*:", t, re.M))
        n_lu = len(re.findall(r"^\*\*Last updated\*\*:", t, re.M))
        if n_src > 1 or n_lu > 1:
            issues.append(f"{rel(p)}  (Sources x{n_src}, Last updated x{n_lu})")
    return issues


# 3. Stale dates --------------------------------------------------------------

def log_content_dates():
    log = WIKI / "log.md"
    if not log.exists():
        return {}
    text = read(log)
    section_re = re.compile(r"^## (\d{4}-\d{2}-\d{2}) — (.+)$", re.M)
    page_re = re.compile(r"`([a-z0-9][a-z0-9/._-]*\.md)`")
    dates = {}
    heads = list(section_re.finditer(text))
    for i, m in enumerate(heads):
        if any(k in m.group(2).lower() for k in LOG_SKIP_KEYWORDS):
            continue
        try:
            d = datetime.date.fromisoformat(m.group(1))
        except ValueError:
            continue
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        for pm in page_re.finditer(text[m.end():end]):
            name = pm.group(1)
            if name not in STRUCTURAL and (name not in dates or d > dates[name]):
                dates[name] = d
    return dates


def check_stale_dates(pages, log_dates):
    issues = []
    date_re = re.compile(r"^\*\*Last updated\*\*:\s*(\d{4}-\d{2}-\d{2})", re.M)
    for p in pages:
        m = date_re.search(read(p))
        if not m:
            continue
        page_date = datetime.date.fromisoformat(m.group(1))
        expected = log_dates.get(rel(p))
        if expected and expected > page_date:
            issues.append(f"{rel(p)}  says {page_date}, log shows content "
                          f"updated {expected}")
    return issues


# 4. Format -------------------------------------------------------------------

def check_format(pages):
    issues = []
    for p in pages:
        lines = read(p).splitlines()
        head = "\n".join(lines[:16])
        probs = []
        if not lines or not lines[0].startswith("# "):
            probs.append("H1 title missing on line 1")
        for field in ("Summary", "Sources", "Last updated"):
            if f"**{field}**:" not in head:
                probs.append(f"**{field}**: missing near the top")
        if rel(p).startswith("decisions/"):
            for field in ("Status", "Decision"):
                if f"**{field}**:" not in head:
                    probs.append(f"**{field}**: missing (decision page)")
        if not any(ln.strip() == "---" for ln in lines[:20]):
            probs.append("--- separator missing after the metadata")
        if probs:
            issues.append((rel(p), probs))
    return issues


# 5 + 6. Broken links and unknown sources --------------------------------------

def check_links(ids):
    broken, unknown = [], []
    sources = (WIKI / "sources.md").resolve()
    for f in all_md():
        bad, unk = set(), set()
        for target, anchor, raw in links(f):
            if not target.exists():
                bad.add(raw)
            elif target == sources and anchor and anchor not in ids:
                unk.add(anchor)
        for m in WIKILINK_RE.finditer(prose(read(f))):
            bad.add(m.group(0) + " (use a markdown link)")
        if bad:
            broken.append((rel(f), sorted(bad)))
        if unk:
            unknown.append((rel(f), sorted(unk)))
    return broken, unknown


# 7. Index coverage -----------------------------------------------------------

def check_index(pages):
    index = WIKI / "index.md"
    if not index.exists():
        return ["index.md is missing"]
    listed = {t for t, _, _ in links(index)}
    return [rel(p) for p in pages if p.resolve() not in listed]


# main ------------------------------------------------------------------------

def report(n, title, items):
    print(f"\n[{n}] {title}  ({len(items)})")
    if not items:
        print("    ok  none")
    for it in items:
        if isinstance(it, tuple):
            for detail in it[1]:
                print(f"    x   {it[0]}: {detail}")
        else:
            print(f"    x   {it}")
    return len(items)


def main() -> int:
    if not WIKI.is_dir():
        print(f"wiki not found at {WIKI}")
        return 2
    pages = wiki_pages()
    ids = source_ids()
    print(f"wiki_lint: {len(pages)} pages, {len(ids)} sources\n" + "=" * 54)

    broken, unknown = check_links(ids)
    total = 0
    total += report(1, "Orphan pages", check_orphans(pages))
    total += report(2, "Duplicate metadata", check_duplicate_metadata(pages))
    total += report(3, "Stale Last updated",
                    check_stale_dates(pages, log_content_dates()))
    total += report(4, "Format issues", check_format(pages))
    total += report(5, "Broken links", broken)
    total += report(6, "Unknown source IDs", unknown)
    total += report(7, "Pages missing from index.md", check_index(pages))

    print("\n" + "=" * 54 + f"\nTotal issues: {total}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
