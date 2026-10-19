#!/usr/bin/env python3
"""Tests for scripts/summarize_loss.py: delivery verdicts, grouping and percentiles."""
import contextlib
import csv
import importlib.util
import io
import os
import sys
import tempfile

sys.dont_write_bytecode = True  # do not leave __pycache__ next to the script

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "summarize_loss", os.path.join(HERE, "..", "scripts", "summarize_loss.py"))
S = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(S)

failures = 0


def check(cond, what):
    global failures
    if not cond:
        print(f"CHECK failed: {what}", file=sys.stderr)
        failures += 1


def row(cfg="A", loss="10", accepted=100, acked=None, pending=0, stored=None,
        rejected=0, drain=50.0, duration=1000.0, lost=0):
    """One run row; by default everything accepted was delivered."""
    return {"run_id": "t", "config": cfg, "loss_percent": str(loss),
            "accepted": str(accepted),
            "acked": str(accepted if acked is None else acked),
            "pending": str(pending),
            "stored": str(accepted if stored is None else stored),
            "rejected": str(rejected), "duration_ms": str(duration),
            "drain_ms": str(drain), "lost_links": str(lost), "connect_failures": "0",
            "tcp_retrans_segs": "5", "peak": "3"}


# The delivery verdict (T08: every accepted record is eventually delivered).
check(S.delivered(row()), "everything accepted, ACKed and stored is delivered")
check(not S.delivered(row(pending=2, acked=98, stored=98)), "records still waiting")
check(not S.delivered(row(stored=99)), "one record missing on Pi B")
check(not S.delivered(row(acked=99)), "one record never ACKed")
check(S.delivered(row(rejected=5)), "rejected records are not accepted ones")
bad = row()
bad["stored"] = "NA"
check(not S.delivered(bad), "a missing value counts as not delivered")
del bad["pending"]
check(not S.delivered(bad), "a missing column counts as not delivered")

# Nearest-rank percentiles on 1..100.
v = list(range(1, 101))
check(S.percentile(v, 95) == 95, "p95 of 1..100 is 95")
check(S.percentile(v, 50) == 50, "p50 of 1..100 is 50")
check(S.percentile([7], 95) == 7, "single value")
for badp in (0, -1, 101):
    try:
        S.percentile(v, badp)
        check(False, f"percentile({badp}) should raise")
    except ValueError:
        pass
try:
    S.percentile([], 50)
    check(False, "empty list should raise")
except ValueError:
    pass

# Grouping, ordering by numeric loss, and the metrics.
rows = [row("A", 30, drain=d, duration=d * 10) for d in (10, 20, 30, 40)]
rows += [row("A", 10), row("A", 0), row("B", 10, pending=1, acked=99, stored=99)]
out = S.summarise(rows, 4)
check(list(out) == [("A", "0"), ("A", "10"), ("A", "30"), ("B", "10")],
      "groups sorted by config then numeric loss")
a30 = out[("A", "30")]
check(a30["n"] == 4 and a30["sufficient"] and a30["delivered"] == 4, "A at 30%")
check(a30["metrics"]["drain_ms"]["median"] == 25.0, "median of 10,20,30,40")
check(a30["metrics"]["drain_ms"]["p95"] == 40.0, "p95 of four values is the largest")
check(a30["metrics"]["duration_ms"]["max"] == 400.0, "duration from rows")
check(not out[("A", "10")]["sufficient"], "one run is below --min 4")
check(out[("B", "10")]["delivered"] == 0, "B at 10% did not deliver")

# drain_ms of -1 (queue never drained) is left out of the drain statistics.
rows = [row(drain=-1, pending=3, acked=97, stored=97), row(drain=8.0)]
m = S.summarise(rows, 1)[("A", "10")]["metrics"]["drain_ms"]
check(m["min"] == 8.0 and m["max"] == 8.0, "drain -1 excluded")

# Rejected records are summed.
rows = [row(rejected=4), row(rejected=6)]
check(S.summarise(rows, 1)[("A", "10")]["rejected"] == 10, "rejected summed")


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


# Through the CSV reader and the CLI: exit 0 only if sufficient and all delivered.
with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()):
    good = os.path.join(d, "good.csv")
    write_csv(good, [row() for _ in range(10)])
    check(S.main([good, "--min", "10"]) == 0, "10 delivered runs pass --min 10")
    check(S.main([good, "--min", "11"]) == 1, "10 runs fail --min 11")
    out_csv = os.path.join(d, "summary.csv")
    check(S.main([good, "--min", "10", "--csv", out_csv]) == 0, "writes the summary csv")
    try:
        S.main([good, "--min", "10", "--csv", out_csv])
        check(False, "must refuse to overwrite the summary")
    except FileExistsError:
        pass
    lost = os.path.join(d, "lost.csv")
    write_csv(lost, [row() for _ in range(9)] + [row(stored=99)])
    check(S.main([lost, "--min", "10"]) == 1, "one undelivered run fails the summary")
    empty = os.path.join(d, "empty.csv")
    write_csv(empty, [row()])
    with open(empty, "w") as f:
        f.write("config,loss_percent\n")
    check(S.main([empty]) == 1, "no runs is an error")

if failures:
    print(f"{failures} check(s) failed", file=sys.stderr)
    sys.exit(1)
print("all checks passed")
