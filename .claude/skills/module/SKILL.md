---
name: module
description: Learn-by-typing workflow for building the next PQC drone link module. Claude drafts and verifies the code privately, shows it in chat for the user to type in, then reviews what they typed against the draft. Use when the user says /module, asks "give me the code for X", or says they've built/typed a module and want it checked.
---

# Module workflow

The user types every new module in themselves, to learn both the project and C.
Claude's job is to:
1. hand over code that is already known to compile and pass its tests
2. explain it
3. check what the user typed

**Never write a new module's files into the repo.** Small fixes are allowed when the
user asks for them ("fix the errors", "make both changes").

Arguments: the module name (e.g. `udp`, `messages`, `kem_mlkem`), or `review <module>`.
With no argument, pick the next step from the status table in `docs/design_notes.md`
and confirm it with the user.

## Phase 1: draft (when the user asks for a module)

1. **Read before designing:**
   - The rules in `CONTRIBUTING.md`. `.claude/CLAUDE.md` has the same rules.
   - In `docs/design_notes.md`: conventions, the follow-ups that name this module,
     and the sections for modules this one depends on.
   - `docs/protocol_spec.md`, for anything in `src/protocol/` or packet formats. The
     spec must be written before the code. If the spec doesn't answer a question,
     ask; don't guess.
   - The headers this module will call.

2. **Stop and ask if an open decision blocks the design.** The open decisions are in
   `README.md`; examples are handshake authentication, KDF choice, and packet header
   contents. Use AskUserQuestion and put a recommendation first.

3. **Draft in `.claude/drafts/<module>/`,** mirroring repo paths
   (`.claude/drafts/<module>/src/...`, `.claude/drafts/<module>/tests/test_<module>.c`).
   `.claude/` is gitignored, so drafts survive across sessions and never reach the
   team. Overwrite any older draft of the same module.
   - Every public function goes in a header with full Doxygen: `@file`, `@brief`,
     directional `@param`, `@return` listing the `errors.h` codes, and
     `@warning`/`@note` for security contracts.
   - Static helpers get a `/** @brief */`, and `.c` comments explain *why*.
   - The test uses the project's `CHECK` macro pattern (copy it from
     `tests/test_kem.c`), with adversarial cases: NULL, wrong length, full buffer,
     unknown value, replayed/late/truncated input where relevant.
   - Cite rules as "CONTRIBUTING.md crypto rule N", never CLAUDE.md.

4. **Verify before showing anything.** Run this from the repo root under **bash**.
   The default shell is zsh, which doesn't split `$FLAGS` into separate words, so
   wrap it in `bash <<'EOF' ... EOF`:
   ```sh
   D=.claude/drafts/<module>
   FLAGS="-std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wmissing-prototypes"
   INC="-I$D/src -Isrc"     # draft headers win over the repo's
   SRCS="$D/src/<dir>/<module>.c <repo .c files it depends on> $D/tests/test_<module>.c"
   for O in -O0 -O2; do gcc $FLAGS $O $INC $SRCS -o $D/t && $D/t || echo FAIL; done
   gcc $FLAGS -g -fsanitize=address,undefined $INC $SRCS -o $D/ts && $D/ts
   clang-format -i $D/src/*/* $D/tests/*.c && clang-format --dry-run $D/src/*/* $D/tests/*.c
   ```
   - Every run must print "all checks passed", with no warnings and no sanitizer
     reports. Fix the draft until it does.
   - Check that files end with a newline: clang-format does not add one.
   - Delete the `t`/`ts` binaries afterwards.
   - For C++ (only `mlkem_shim.cpp`), use `g++ -std=c++20 -fno-exceptions -fno-rtti`
     and link against the ml-kem headers in `third_party/` and `build/_deps`.

5. **Present it in chat, in this order:**
   1. One line saying it was verified, and how: flags, -O0/-O2, sanitizers.
   2. **Why this module exists / how it fits.** A short paragraph, plus a small ASCII
      diagram if it involves a message flow or data layout.
   3. **Design choices the user may want to change.** Only real choices, each with
      the trade-off.
   4. Files that aren't in the README repo layout, and why they're needed.
   5. Each file in full, in a fenced block headed with its path.
   6. The exact `CMakeLists.txt` lines to add, plus the build and test commands.
   7. **What to notice while typing it in.** Short bullets on the C techniques and
      security reasoning: the things worth learning, not a repeat of the comments.
   8. "Tell me when it's built and I'll check it."

## Phase 2: review (when the user says it's built or typed)

1. Diff the user's files against the draft, ignoring whitespace:
   `diff -w .claude/drafts/<module>/<path> <path>`. Report every difference:
   - say whether it's a typo, a harmless variation, or an improvement
   - for each typo, give the line number and the fix

   Explain *why* a mistake breaks things. For example, a header/definition name
   mismatch gives a linker "undefined reference".
2. Run `bash .claude/skills/precommit/check.sh` and report its FAIL and WARN lines.
3. If the user reports a build error, find the cause and explain it as a lesson:
   what the compiler or linker was telling them. Then add it to the
   "Debugging lessons" table in `docs/design_notes.md`.
4. Fix things only when the user asks. Otherwise, list the edits for them to make.

## Phase 3: wrap-up (once the build and all tests pass)

1. Update `docs/design_notes.md`:
   - status row → Done, and the next step → Next
   - a section on the design choices and the "what to notice" points
   - new follow-ups that later modules must honour; tick off any this module
     completed
   - a progress-log entry dated today
2. Add new files to the repo layout in `README.md` and in `.claude/CLAUDE.md`.
3. If the module introduced a new rule or convention, add it to both
   `CONTRIBUTING.md` and `.claude/CLAUDE.md`, with the same wording.
4. Suggest the commit file list and message, and the next module. Don't commit.
