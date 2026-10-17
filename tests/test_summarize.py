#!/usr/bin/env python3
"""Tests for scripts/summarize_handshakes.py: percentiles and warm-up handling."""
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
    "summarize", os.path.join(HERE, "..", "scripts", "summarize_handshakes.py"))
S = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(S)

failures = 0


def check(cond, what):
    global failures
    if not cond:
        print(f"CHECK failed: {what}", file=sys.stderr)
        failures += 1


def row(cfg, lat_ms, warmup=0):
    ns = int(lat_ms * 1e6)
    return {"run_id": "t", "config": cfg, "attempt": "0", "warmup": str(warmup),
            "t_start_ns": "0", "latency_total_ns": str(ns), "latency_tls_ns": str(ns),
            "client_cpu_ns": str(ns), "bytes_tx": "100", "bytes_rx": "200",
            "heap_delta_bytes": "-5", "group": "X25519", "cipher": "C", "verify_ok": "1",
            "build": "release"}


# Nearest-rank percentiles on 1..100: p95 is the 95th value.
v = list(range(1, 101))
check(S.percentile(v, 95) == 95, "p95 of 1..100 is 95")
check(S.percentile(v, 50) == 50, "p50 of 1..100 is 50")
check(S.percentile(v, 100) == 100, "p100 is the maximum")
check(S.percentile([7], 95) == 7, "single value")
check(S.percentile([1, 2, 3, 4], 95) == 4, "p95 of 4 values is the largest")
for bad in (0, -1, 101):
    try:
        S.percentile(v, bad)
        check(False, f"percentile({bad}) should raise")
    except ValueError:
        pass
try:
    S.percentile([], 50)
    check(False, "empty list should raise")
except ValueError:
    pass

# Warm-up rows never reach the statistics; the other configuration is separate.
rows = [row("A", 1000, warmup=1)] + [row("A", i) for i in range(1, 101)]
rows += [row("B", 5)]
out = S.summarise(rows, 100)
check(out["A"]["n"] == 100, "warm-up row excluded from n")
check(out["A"]["sufficient"], "100 measured is sufficient")
check(out["A"]["metrics"]["latency_total_ns"]["median"] == 50.5, "median of 1..100")
check(out["A"]["metrics"]["latency_total_ns"]["p95"] == 95.0, "p95 from rows")
check(out["A"]["metrics"]["latency_total_ns"]["max"] == 100.0, "warm-up not in max")
check(out["A"]["metrics"]["heap_delta_bytes"]["median"] == -5.0, "negative heap delta")
check(out["B"]["n"] == 1 and not out["B"]["sufficient"], "B has too few")

# End to end through the CSV reader and the CLI (exit code 1 when insufficient).
with tempfile.TemporaryDirectory() as d:
    ok = os.path.join(d, "ok.csv")
    with open(ok, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(row("A", 1).keys()))
        w.writeheader()
        w.writerows([row("A", i) for i in range(1, 11)])
    with contextlib.redirect_stdout(io.StringIO()):  # keep the test output quiet
        check(S.main([ok, "--min", "10"]) == 0, "10 rows pass --min 10")
        check(S.main([ok, "--min", "11"]) == 1, "10 rows fail --min 11")
        out_csv = os.path.join(d, "summary.csv")
        check(S.main([ok, "--min", "10", "--csv", out_csv]) == 0, "writes summary csv")
        try:
            S.main([ok, "--min", "10", "--csv", out_csv])
            check(False, "must refuse to overwrite the summary")
        except FileExistsError:
            pass

if failures:
    print(f"{failures} check(s) failed", file=sys.stderr)
    sys.exit(1)
print("all checks passed")
