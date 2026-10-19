#!/usr/bin/env python3
"""Summarise a T10 handshake benchmark: median, p95 and spread per configuration.

Usage: summarize_handshakes.py OK_CSV [FAIL_CSV] [--min N] [--csv OUT]
                               [--server A=FILE] [--server B=FILE] [--server-warmup N]

Reads the raw CSV written by bench_handshake. Warm-up rows are excluded. Every number
printed comes from the raw file, so anyone can rerun this and get the same table.
Percentiles use the nearest-rank method: the p-th percentile of n sorted values is the
value at rank ceil(p/100 * n).

A configuration with fewer than --min (default 100) measured successes is marked
INSUFFICIENT and is not reportable as a T10 result. Failed attempts are only counted.

--server CFG=FILE adds the server's side of the cost from the CSV written by
`tls_server --handshake-csv`: CPU time of the handshake, the heap the open connection
holds, the handshake bytes, and the process peak RSS. The server does not know which
handshakes were warm-up, so rows with ok=0 are counted as failures and the first
--server-warmup (default 5) successful rows are dropped, matching the client's warm-up.
If the server's measured count differs from the client's, the summary says so and exits 1.
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


# Server-side columns, from tls_server --handshake-csv. Times are stored in ns, shown in ms.
SERVER_METRICS = [
    ("server_cpu_ns", "server CPU time, handshake", 1e6, "ms"),
    ("heap_delta_bytes", "server heap held by the connection", 1, "B"),
    ("bytes_rx", "handshake bytes received (server)", 1, "B"),
    ("bytes_tx", "handshake bytes sent (server)", 1, "B"),
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


def summarise_server(rows, warmup):
    """Return the server-side summary of one configuration's rows, or None if none remain.

    Rows with ok=0 are counted as failures. The first `warmup` successful rows are dropped.
    The peak RSS is the largest value in any row: it only ever grows, so that is the peak of
    the whole run, warm-up included.
    """
    ok_rows = [r for r in rows if r["ok"] == "1"]
    measured = ok_rows[warmup:]
    if not measured:
        return None
    entry = {"n": len(measured), "failed": len(rows) - len(ok_rows),
             "peak_rss_kb": max(int(r["rss_peak_kb"]) for r in rows), "metrics": {}}
    for col, _label, div, _unit in SERVER_METRICS:
        vals = sorted(float(r[col]) / div for r in measured)
        entry["metrics"][col] = {
            "median": statistics.median(vals),
            "p95": percentile(vals, 95),
            "min": vals[0],
            "max": vals[-1],
            "mean": statistics.fmean(vals),
        }
    return entry


def parse_server_args(items):
    """['A=file', 'B=file'] -> {'A': 'file', 'B': 'file'}; raises ValueError if malformed."""
    out = {}
    for item in items:
        cfg, sep, path = item.partition("=")
        if not sep or not cfg or not path or cfg in out:
            raise ValueError(f"bad --server value: {item!r}")
        out[cfg] = path
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
    ap.add_argument("--server", action="append", default=[], metavar="CFG=FILE")
    ap.add_argument("--server-warmup", type=int, default=5, dest="server_warmup")
    args = ap.parse_args(argv)

    summary = summarise(read_rows(args.ok_csv), args.minimum)
    servers = {}
    for cfg, path in parse_server_args(args.server).items():
        servers[cfg] = summarise_server(read_rows(path), args.server_warmup)
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
        if cfg in servers:
            srv = servers[cfg]
            if srv is None:
                print("   server side: no measured rows")
                status = 1
                continue
            print(f"   -- server side: n={srv['n']}  failed={srv['failed']}")
            if srv["n"] != e["n"]:
                print(f"   WARNING: server measured {srv['n']} handshakes, client {e['n']}: "
                      "the two sides do not describe the same handshakes")
                status = 1
            for col, label, _div, unit in SERVER_METRICS:
                m = srv["metrics"][col]
                print(f"   {label:<36} median {m['median']:>10.3f}  p95 {m['p95']:>10.3f}  "
                      f"min {m['min']:>10.3f}  max {m['max']:>10.3f} {unit}")
            print(f"   {'server peak RSS of the whole run':<36} {srv['peak_rss_kb']:>10d} kB")

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
                srv = servers.get(cfg)
                if srv is not None:
                    for col, _label, _div, unit in SERVER_METRICS:
                        m = srv["metrics"][col]
                        w.writerow([cfg, srv["n"], srv["failed"], col if col.startswith("server_")
                                    else "server_" + col, unit, m["median"], m["p95"],
                                    m["min"], m["max"], m["mean"]])
                    rss = float(srv["peak_rss_kb"])
                    w.writerow([cfg, srv["n"], srv["failed"], "server_peak_rss_kb", "kB",
                                rss, rss, rss, rss, rss])
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
