---
name: precommit
description: Run the PQC drone link before-commit checklist (format, final newlines, warning-free build, ctest, Doxygen, CMake/README listings, design notes, wiki lint) and report what to fix. Use when the user says /precommit, asks "is this ready to commit", or before suggesting a commit.
---

# Pre-commit check

Runs the "Before committing" checklist from CONTRIBUTING.md against uncommitted
changes, or against a range if the user names one (e.g. "check the last commit").

## Steps

1. **Run the mechanical checks:**
   ```sh
   bash .claude/skills/precommit/check.sh          # uncommitted changes
   bash .claude/skills/precommit/check.sh HEAD~1   # also include the last commit
   ```
   If the tree is clean, ask whether to check the last commit instead, and use
   `HEAD~1` if so.

2. **Do the checks that need judgment.** Only do these for files that changed.
   - **Rules.** If `CONTRIBUTING.md` changed, check that README.md, `docs/` and code
     comments don't contradict it, and that crypto rule numbers weren't renumbered
     (code comments cite them).
   - **Design notes.** If a module looks finished (it has a source file and a passing
     test), check that `docs/design_notes.md` has all of these:
     - a status row for it marked Done
     - a section on its design choices
     - a progress-log entry dated today
     - any new follow-ups
   - **Error codes.** If `src/common/errors.h` changed, confirm no existing value was
     renumbered (`git diff` on that file).
   - **Commit grouping.** Check whether a test file changed without its CMake
     registration, or a CMake change references a file that's still untracked.
     Either way, the commit would be broken for teammates.

3. **Report.** Give a short list with the FAILs first, then the WARNs, each with the
   exact command or edit that fixes it. End with a suggested list of files to commit,
   plus a one-line commit message.

## Rules

- **Don't modify files** unless the user asks. The user types code themselves to
  learn. Reporting `clang-format -i <files>` is better than running it silently.
  When asked to fix, mechanical fixes (formatting, newlines) are fine.
- **Never commit or push.** The user does that.
- **Never report a pass that the script didn't print.** If the script couldn't run,
  for example because `build/` is missing, say so.
