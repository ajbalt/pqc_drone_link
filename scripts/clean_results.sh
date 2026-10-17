#!/usr/bin/env bash
# Tidy results/ by deleting old benchmark runs from run_handshake_bench.sh.
#
# Usage: clean_results.sh [--keep N] [--apply] [results-dir]
#   --keep N   keep the newest N runs of each configuration (default 3)
#   --apply    actually delete; without it this only lists what would go (dry run)
#   results-dir  defaults to results/ in the repository
#
# A run is every file named <name>_<UTC timestamp>.*, so a run is kept or deleted whole.
# results/ is git-ignored raw output, not gate evidence (evidence/ is never touched). The
# report plots (docs/report/data) are built from results/*.ok.csv by prepare_data.py: keep
# the runs you still need for them.
set -eu
KEEP=3; APPLY=no; DIR=""
while [ $# -gt 0 ]; do
  case $1 in
    --keep) KEEP=${2:?--keep needs a number}; shift 2 ;;
    --apply) APPLY=yes; shift ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) DIR=$1; shift ;;
  esac
done
case $KEEP in ''|*[!0-9]*) echo "--keep must be a whole number" >&2; exit 2 ;; esac
root=$(cd "$(dirname "$0")/.." && pwd)
DIR=${DIR:-$root/results}
[ -d "$DIR" ] || { echo "no such directory: $DIR" >&2; exit 2; }

# One run id per line, e.g. t10_handshake_loopback_20261017T074809Z, oldest first.
runs=$(find "$DIR" -maxdepth 1 -type f -name '*_[0-9]*T[0-9]*Z.*' -printf '%f\n' \
         | sed -n 's/^\(.*_[0-9]\{8\}T[0-9]\{6\}Z\)\..*$/\1/p' | sort -u)
[ -n "$runs" ] || { echo "no runs in $DIR"; exit 0; }

doomed=""
for name in $(echo "$runs" | sed 's/_[0-9]\{8\}T[0-9]\{6\}Z$//' | sort -u); do
  mine=$(echo "$runs" | grep -x "${name}_[0-9]\{8\}T[0-9]\{6\}Z")   # sorted by timestamp
  total=$(echo "$mine" | wc -l)
  [ "$total" -gt "$KEEP" ] || { echo "$name: $total run(s), nothing to delete"; continue; }
  old=$(echo "$mine" | head -n $((total - KEEP)))
  echo "$name: keeping newest $KEEP of $total"
  doomed="$doomed$old
"
done

n=0; bytes=0
for run in $doomed; do
  for f in "$DIR/$run".*; do
    [ -e "$f" ] || continue
    bytes=$((bytes + $(stat -c %s "$f"))); n=$((n + 1))
    if [ "$APPLY" = yes ]; then rm -- "$f"; else echo "would delete ${f#"$DIR"/}"; fi
  done
done
if [ "$APPLY" = yes ]; then echo "deleted $n file(s), $bytes bytes"
else echo "dry run: $n file(s), $bytes bytes would go; re-run with --apply to delete"; fi
