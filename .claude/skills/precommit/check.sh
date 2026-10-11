#!/usr/bin/env bash
# Mechanical part of the before-commit checklist (CONTRIBUTING.md).
# Prints one PASS/FAIL/WARN line per check; exits 1 if any check FAILs.
# Usage: check.sh [base-ref]   (default HEAD: checks uncommitted changes)
set -u

repo=$(git rev-parse --show-toplevel) || exit 2
cd "$repo" || exit 2
base=${1:-HEAD}
fail=0

pass() { printf 'PASS  %s\n' "$1"; }
warn() { printf 'WARN  %s\n' "$1"; }
bad()  { printf 'FAIL  %s\n' "$1"; fail=1; }
detail() { sed 's/^/        /'; }

# Changed = modified/added since base, plus untracked (not ignored) files.
changed=$( { git diff --name-only --diff-filter=ACMR "$base" --;
             git ls-files --others --exclude-standard; } | sort -u |
           grep -v '^third_party/' )
added=$( { git diff --name-only --diff-filter=A "$base" --;
           git ls-files --others --exclude-standard; } | sort -u |
         grep -v '^third_party/' )
code=$(printf '%s\n' "$changed" | grep -E '\.(c|h|cpp)$' || true)

if [ -z "$changed" ]; then
  echo "No changes since $base. Pass a base ref to check commits, e.g. check.sh HEAD~1"
fi
echo "Changed files: $(printf '%s\n' "$changed" | grep -c . )"

# 1. clang-format
if [ -n "$code" ]; then
  out=$(clang-format --dry-run $code 2>&1 | grep 'warning:' | cut -d: -f1 | sort -u)
  if [ -z "$out" ]; then pass "clang-format"
  else bad "clang-format: run clang-format -i on:"; printf '%s\n' "$out" | detail; fi
else pass "clang-format (no C/C++ changes)"; fi

# 2. final newline on changed text files
nonl=""
for f in $changed; do
  case "$f" in *.c|*.h|*.cpp|*.md|*.txt|*.sh|*.py|*.cmake|CMakeLists.txt) ;; *) continue ;; esac
  [ -s "$f" ] && [ -n "$(tail -c1 "$f")" ] && nonl="$nonl$f"$'\n'
done
if [ -z "$nonl" ]; then pass "final newlines"
else bad "missing final newline:"; printf '%s' "$nonl" | detail; fi

# 3. build with no warnings
if [ ! -d build ]; then
  bad "build/ missing: run cmake -B build -DCMAKE_BUILD_TYPE=Release"
else
  out=$(cmake --build build 2>&1); rc=$?
  warns=$(printf '%s\n' "$out" | grep -E 'warning:|error:' | grep -v '/third_party/\|/_deps/')
  if [ $rc -ne 0 ]; then bad "build failed:"; printf '%s\n' "$out" | grep -E 'error|undefined' | head -20 | detail
  elif [ -n "$warns" ]; then bad "build warnings:"; printf '%s\n' "$warns" | head -20 | detail
  else pass "build (no warnings)"; fi

  # 4. tests
  out=$(ctest --test-dir build --output-on-failure 2>&1); rc=$?
  summary=$(printf '%s\n' "$out" | grep 'tests passed')
  if [ $rc -eq 0 ]; then pass "ctest: $summary"
  else bad "ctest: $summary"; printf '%s\n' "$out" | grep -E 'Failed|CHECK failed' | head -20 | detail; fi

  # 5. Doxygen warnings in changed files (optional target)
  if cmake --build build --target help 2>/dev/null | grep -q '\bdocs\b'; then
    out=$(cmake --build build --target docs 2>&1)
    dw=""
    for f in $changed; do
      dw="$dw$(printf '%s\n' "$out" | grep "$repo/$f:" | grep -i warning)"
    done
    if [ -z "$dw" ]; then pass "Doxygen (no warnings in changed files)"
    else bad "Doxygen warnings:"; printf '%s\n' "$dw" | sed "s|$repo/||" | head -20 | detail; fi
  else warn "Doxygen not installed; docs check skipped"; fi
fi

# 6. new .c/.cpp files are built by CMake
missing=""
for f in $(printf '%s\n' "$changed" | grep -E '^(src|tests)/.*\.(c|cpp)$'); do
  grep -q "$f" CMakeLists.txt || missing="$missing$f"$'\n'
done
if [ -z "$missing" ]; then pass "C sources listed in CMakeLists.txt"
else bad "not in CMakeLists.txt (won't be built):"; printf '%s' "$missing" | detail; fi

# 7. new project files appear in the README layout
missing=""
for f in $(printf '%s\n' "$added" | grep -E '^(src|tests|docs|scripts|experiments|analysis)/' | grep -v '^docs/research/'); do
  grep -q "$(basename "$f")" README.md || missing="$missing$f"$'\n'
done
if [ -z "$missing" ]; then pass "new files in README layout"
else warn "new files not in README repo layout:"; printf '%s' "$missing" | detail; fi

# 8. shared files must not cite the personal CLAUDE.md
refs=$(grep -rn 'CLAUDE' src tests docs README.md CONTRIBUTING.md CMakeLists.txt 2>/dev/null)
if [ -z "$refs" ]; then pass "no CLAUDE.md references in shared files"
else bad "shared files cite CLAUDE.md (teammates don't have it); use CONTRIBUTING.md:"
     printf '%s\n' "$refs" | detail; fi

# 9. design notes reminder
if printf '%s\n' "$changed" | grep -qE '^src/' &&
   ! printf '%s\n' "$changed" | grep -qx 'docs/design_notes.md'; then
  warn "src/ changed but docs/design_notes.md didn't: update it if a module was finished"
else pass "design notes"; fi

# 10. research wiki lint (only when the wiki or its linter changed)
if printf '%s\n' "$changed" | grep -qE '^docs/research/|^scripts/wiki_lint\.py$'; then
  out=$(python3 scripts/wiki_lint.py 2>&1); rc=$?
  if [ $rc -eq 0 ]; then pass "research wiki lint"
  else bad "research wiki lint: run python3 scripts/wiki_lint.py"
       printf '%s\n' "$out" | grep -E '^\s+x ' | head -20 | detail; fi
fi

# 11. rules files changed? (sync is checked by Claude, not here)
if printf '%s\n' "$changed" | grep -qx 'CONTRIBUTING.md'; then
  warn "CONTRIBUTING.md changed: make sure .claude/CLAUDE.md matches"
fi

echo
[ $fail -eq 0 ] && echo "RESULT: ready to commit" || echo "RESULT: fix the FAIL items first"
exit $fail
