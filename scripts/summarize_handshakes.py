#!/usr/bin/env python3
"""Summarise a T10 handshake benchmark: median, p95 and spread per configuration.

Usage: summarize_handshakes.py OK_CSV [FAIL_CSV] [--min N] [--csv OUT]

Reads the raw CSV written by bench_handshake. Warm-up rows are excluded. Every number
printed comes from the raw file, so anyone can rerun this and get the same table.
Percentiles use the nearest-rank method: the p-th percentile of n sorted values is the
value at rank ceil(p/100 * n).

A configuration with fewer than --min (default 100) measured successes is marked
INSUFFICIENT and is not reportable as a T10 result. Failed attempts are only counted.
"""
import argparse
import csv
import math
import statistics
import sys

# (column, label, divisor, unit). Times are stored in ns and shown in ms.
METRICS = [
    ("latency_total_ns", "latency, connect to handshake end", 1e6, "ms"),
    ("latency_tls_ns", "latency, TLS handshake alone", 1e6, "ms"),
    ("client_cpu_ns", "client CPU time", 1e6, "ms"),
    ("bytes_tx", "handshake bytes sent", 1, "B"),
    ("bytes_rx", "handshake bytes received", 1, "B"),
    ("heap_delta_bytes", "heap held by the open connection", 1, "B"),
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


def summarise(rows, minimum):
    """Return {config: {...}} for measured (non-warm-up) rows."""
    by_cfg = {}
    for r in rows:
        if r["warmup"] != "0":
            continue
        by_cfg.setdefault(r["config"], []).append(r)
    out = {}
    for cfg, rs in sorted(by_cfg.items()):
        entry = {
            "n": len(rs),
            "sufficient": len(rs) >= minimum,
            "groups": sorted({r["group"].lower() for r in rs}),
            "ciphers": sorted({r["cipher"] for r in rs}),
            "builds": sorted({r["build"] for r in rs}),
            "metrics": {},
        }
        for col, _label, div, _unit in METRICS:
            vals = sorted(float(r[col]) / div for r in rs)
            entry["metrics"][col] = {
                "median": statistics.median(vals),
                "p95": percentile(vals, 95),
                "min": vals[0],
                "max": vals[-1],
                "mean": statistics.fmean(vals),
            }
        out[cfg] = entry
    return out


def count_failures(path):
    counts = {}
    for r in read_rows(path):
        counts[r["config"]] = counts.get(r["config"], 0) + 1
    return counts


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ok_csv")
    ap.add_argument("fail_csv", nargs="?")
    ap.add_argument("--min", type=int, default=100, dest="minimum")
    ap.add_argument("--csv", dest="csv_out")
    args = ap.parse_args(argv)

    summary = summarise(read_rows(args.ok_csv), args.minimum)
    failures = count_failures(args.fail_csv) if args.fail_csv else {}
    if not summary:
        print("no measured rows", file=sys.stderr)
        return 1

    status = 0
    for cfg, e in summary.items():
        flag = "" if e["sufficient"] else f"  INSUFFICIENT (< {args.minimum}): not reportable"
        print(f"== config {cfg}: n={e['n']}  failures={failures.get(cfg, 0)}{flag}")
        print(f"   group={','.join(e['groups'])}  cipher={','.join(e['ciphers'])}  "
              f"build={','.join(e['builds'])}")
        if "debug" in e["builds"]:
            print("   WARNING: debug build: these numbers must never be reported")
        if not e["sufficient"]:
            status = 1
        for col, label, _div, unit in METRICS:
            m = e["metrics"][col]
            print(f"   {label:<36} median {m['median']:>10.3f}  p95 {m['p95']:>10.3f}  "
                  f"min {m['min']:>10.3f}  max {m['max']:>10.3f} {unit}")

    if args.csv_out:
        with open(args.csv_out, "x", newline="") as f:
            w = csv.writer(f)
            w.writerow(["config", "n", "failures", "metric", "unit", "median", "p95",
                        "min", "max", "mean"])
            for cfg, e in summary.items():
                for col, _label, _div, unit in METRICS:
                    m = e["metrics"][col]
                    w.writerow([cfg, e["n"], failures.get(cfg, 0), col, unit,
                                m["median"], m["p95"], m["min"], m["max"], m["mean"]])
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
