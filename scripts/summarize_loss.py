#!/usr/bin/env python3
"""Summarise a T08 packet-loss run: delivery and recovery per configuration and loss level.

Usage: summarize_loss.py RUNS_CSV [--min N] [--csv OUT]

Reads the per-run CSV written by run_loss_test.sh. One row is one run. A run delivered
everything if nothing is still waiting and the number of records stored on Pi B equals the
number the client accepted (T08: every accepted record is eventually delivered). A row with a
missing or non-numeric value counts as not delivered. Rejected records (queue full) are not
"accepted", but they are counted and shown, never hidden.

Percentiles use the nearest-rank method: the p-th percentile of n sorted values is the value
at rank ceil(p/100 * n). A group with fewer than --min (default 10) runs is marked
INSUFFICIENT and is not reportable as a T08 result. Exit status is 1 if any group is
insufficient or any run did not deliver everything.
"""
import argparse
import csv
import math
import statistics
import sys

# (column, label, unit). drain_ms is -1 when the queue did not drain; those rows are left out.
METRICS = [
    ("duration_ms", "run duration", "ms"),
    ("drain_ms", "drain time after the last record", "ms"),
    ("lost_links", "sessions lost", ""),
    ("connect_failures", "failed connection attempts", ""),
    ("tcp_retrans_segs", "TCP segments retransmitted", ""),
    ("peak", "peak records waiting", ""),
]


def percentile(sorted_values, p):
    """Nearest-rank percentile of an ascending list; p in (0, 100]."""
    if not sorted_values:
        raise ValueError("no values")
    if not 0 < p <= 100:
        raise ValueError("p must be in (0, 100]")
    rank = math.ceil(p / 100 * len(sorted_values))
    return sorted_values[rank - 1]


def read_rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def number(row, col):
    """The value of a column as a float, or None if it is missing or not a number."""
    try:
        return float(row[col])
    except (KeyError, TypeError, ValueError):
        return None


def delivered(row):
    """True if the run ended with every accepted record stored and nothing waiting."""
    accepted = number(row, "accepted")
    pending = number(row, "pending")
    acked = number(row, "acked")
    stored = number(row, "stored")
    if None in (accepted, pending, acked, stored):
        return False
    return pending == 0 and acked == accepted and stored == accepted


def summarise(rows, minimum):
    """Return {(config, loss_percent): {...}} over all rows."""
    groups = {}
    for r in rows:
        groups.setdefault((r["config"], r["loss_percent"]), []).append(r)
    out = {}
    for key in sorted(groups, key=lambda k: (k[0], float(k[1]))):
        rs = groups[key]
        entry = {
            "n": len(rs),
            "sufficient": len(rs) >= minimum,
            "delivered": sum(1 for r in rs if delivered(r)),
            "rejected": sum(number(r, "rejected") or 0 for r in rs),
            "metrics": {},
        }
        for col, _label, _unit in METRICS:
            vals = sorted(v for v in (number(r, col) for r in rs)
                          if v is not None and v >= 0)
            if vals:
                entry["metrics"][col] = {
                    "median": statistics.median(vals),
                    "p95": percentile(vals, 95),
                    "min": vals[0],
                    "max": vals[-1],
                }
        out[key] = entry
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("runs_csv")
    ap.add_argument("--min", type=int, default=10, dest="minimum")
    ap.add_argument("--csv", dest="csv_out")
    args = ap.parse_args(argv)

    summary = summarise(read_rows(args.runs_csv), args.minimum)
    if not summary:
        print("no runs", file=sys.stderr)
        return 1

    status = 0
    for (cfg, loss), e in summary.items():
        flag = "" if e["sufficient"] else f"  INSUFFICIENT (< {args.minimum}): not reportable"
        ok = e["delivered"] == e["n"]
        verdict = "all delivered" if ok else f"NOT ALL DELIVERED ({e['n'] - e['delivered']} runs)"
        print(f"== config {cfg}, loss {loss}%: runs={e['n']}  delivered {e['delivered']}/{e['n']}"
              f"  rejected={e['rejected']:.0f}  {verdict}{flag}")
        if not ok or not e["sufficient"]:
            status = 1
        for col, label, unit in METRICS:
            m = e["metrics"].get(col)
            if m is None:
                continue
            print(f"   {label:<36} median {m['median']:>10.1f}  p95 {m['p95']:>10.1f}  "
                  f"min {m['min']:>10.1f}  max {m['max']:>10.1f} {unit}")

    if args.csv_out:
        with open(args.csv_out, "x", newline="") as f:
            w = csv.writer(f)
            w.writerow(["config", "loss_percent", "runs", "delivered_runs", "rejected_total",
                        "metric", "median", "p95", "min", "max"])
            for (cfg, loss), e in summary.items():
                for col, _label, _unit in METRICS:
                    m = e["metrics"].get(col)
                    if m is not None:
                        w.writerow([cfg, loss, e["n"], e["delivered"], e["rejected"], col,
                                    m["median"], m["p95"], m["min"], m["max"]])
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
