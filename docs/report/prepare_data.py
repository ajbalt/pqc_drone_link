#!/usr/bin/env python3
"""Turn raw T10 benchmark CSVs into the small data files the progress report plots.

Usage: python3 -I docs/report/prepare_data.py RESULTS_DIR OUT_DIR

Reads every *.ok.csv in RESULTS_DIR (written by bench_handshake) and the matching *.meta.txt,
drops warm-up rows, and writes into OUT_DIR:
  sessions.csv  one row per session and configuration: median and p95 per metric
  sessions_A.csv, sessions_B.csv   the same, split by configuration for the line plot
  box.csv       pooled box-plot summaries (nearest-rank percentiles)
  box_<metric>.tex   generated \\addplot lines for the box plots in the report
  bytes.csv     pooled median handshake bytes sent and received
  numbers.tex   LaTeX macros the report text uses, so no number is ever typed by hand

Nothing here estimates or edits a measurement: every figure comes from the raw files, and the
script can be rerun by anyone. Standard library only. Existing output files are overwritten
because they are derived data, not results.
"""
import csv
import glob
import math
import os
import statistics
import sys

METRICS = {  # column -> (label, divisor to the plotted unit)
    "latency_total_ns": ("latency_total_ms", 1e6),
    "latency_tls_ns": ("latency_tls_ms", 1e6),
    "client_cpu_ns": ("client_cpu_ms", 1e6),
    "bytes_tx": ("bytes_tx", 1),
    "bytes_rx": ("bytes_rx", 1),
    "heap_delta_bytes": ("heap_delta_bytes", 1),
}


def pct(sorted_vals, p):
    """Nearest-rank percentile, p in (0, 100]."""
    return sorted_vals[math.ceil(p / 100 * len(sorted_vals)) - 1]


def tex_escape(s):
    for a, b in (("\\", " "), ("&", r"\&"), ("%", r"\%"), ("_", r"\_"), ("#", r"\#"),
                 ("$", r"\$"), ("{", r"\{"), ("}", r"\}"), ("~", " "), ("^", " ")):
        s = s.replace(a, b)
    return s


def read_meta(path):
    meta = {}
    if not os.path.exists(path):
        return meta
    with open(path) as f:
        for line in f:
            if line.startswith("---"):
                break
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                meta.setdefault(k, v)
    return meta


def main(results_dir, out_dir):
    files = sorted(glob.glob(os.path.join(results_dir, "*.ok.csv")))
    if not files:
        print("no *.ok.csv files in", results_dir, file=sys.stderr)
        return 1
    os.makedirs(out_dir, exist_ok=True)

    per_session = []   # (session index, config, rows)
    pooled = {"A": [], "B": []}
    builds, groups, metas = set(), {}, []
    for i, path in enumerate(files, start=1):
        with open(path, newline="") as f:
            rows = [r for r in csv.DictReader(f) if r["warmup"] == "0"]
        metas.append(read_meta(path.replace(".ok.csv", ".meta.txt")))
        for cfg in ("A", "B"):
            rs = [r for r in rows if r["config"] == cfg]
            per_session.append((i, cfg, rs))
            pooled[cfg].extend(rs)
            for r in rs:
                builds.add(r["build"])
                groups.setdefault(cfg, set()).add(r["group"].lower())
    if builds != {"release"}:
        print("refusing: not all rows come from a release build:", builds, file=sys.stderr)
        return 1

    def col(rs, c):
        d = METRICS[c][1]
        return sorted(float(r[c]) / d for r in rs)

    # sessions.csv
    with open(os.path.join(out_dir, "sessions.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["session", "config", "n", "med_total_ms", "p95_total_ms", "med_tls_ms",
                    "med_cpu_ms", "bytes_tx", "bytes_rx", "med_heap_bytes"])
        for i, cfg, rs in per_session:
            t = col(rs, "latency_total_ns")
            w.writerow([i, cfg, len(rs), f"{statistics.median(t):.4f}", f"{pct(t, 95):.4f}",
                        f"{statistics.median(col(rs, 'latency_tls_ns')):.4f}",
                        f"{statistics.median(col(rs, 'client_cpu_ns')):.4f}",
                        f"{statistics.median(col(rs, 'bytes_tx')):.0f}",
                        f"{statistics.median(col(rs, 'bytes_rx')):.0f}",
                        f"{statistics.median(col(rs, 'heap_delta_bytes')):.0f}"])

    # per-configuration session files for the line plot
    for cfg in ("A", "B"):
        with open(os.path.join(out_dir, f"sessions_{cfg}.csv"), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["session", "med_total_ms", "p95_total_ms", "med_cpu_ms"])
            for i, c2, rs in per_session:
                if c2 != cfg:
                    continue
                t = col(rs, "latency_total_ns")
                w.writerow([i, f"{statistics.median(t):.4f}", f"{pct(t, 95):.4f}",
                            f"{statistics.median(col(rs, 'client_cpu_ns')):.4f}"])

    # box.csv and generated plot lines: whiskers are p5 and p95, box is p25 to p75
    with open(os.path.join(out_dir, "box.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["metric", "config", "n", "lw", "lq", "med", "uq", "uw"])
        for c in ("latency_total_ns", "latency_tls_ns", "client_cpu_ns"):
            tex_lines = ["% Generated by prepare_data.py. Do not edit."]
            for pos, cfg in ((1, "A"), (2, "B")):
                v = col(pooled[cfg], c)
                stats = [pct(v, 5), pct(v, 25), statistics.median(v), pct(v, 75), pct(v, 95)]
                w.writerow([METRICS[c][0], cfg, len(v)] + [f"{x:.4f}" for x in stats])
                tex_lines.append(
                    f"\\addplot+[boxplot prepared={{draw position={pos}, "
                    f"lower whisker={stats[0]:.4f}, lower quartile={stats[1]:.4f}, "
                    f"median={stats[2]:.4f}, upper quartile={stats[3]:.4f}, "
                    f"upper whisker={stats[4]:.4f}}}, fill=color{cfg}!25, draw=color{cfg}!80!black] "
                    f"coordinates {{}};")
            with open(os.path.join(out_dir, f"box_{METRICS[c][0]}.tex"), "w") as g:
                g.write("\n".join(tex_lines) + "\n")

    # bytes.csv
    med = {}
    with open(os.path.join(out_dir, "bytes.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["config", "bytes_tx", "bytes_rx"])
        for cfg in ("A", "B"):
            tx = statistics.median(col(pooled[cfg], "bytes_tx"))
            rx = statistics.median(col(pooled[cfg], "bytes_rx"))
            med[cfg] = {"tx": tx, "rx": rx}
            w.writerow([cfg, f"{tx:.0f}", f"{rx:.0f}"])

    # numbers.tex
    def m(c, cfg):
        return statistics.median(col(pooled[cfg], c))

    def q(c, cfg, p):
        return pct(col(pooled[cfg], c), p)

    sess_meds = {cfg: [statistics.median(col(rs, "latency_total_ns"))
                       for i, c2, rs in per_session if c2 == cfg] for cfg in ("A", "B")}
    ratios = [b / a for a, b in zip(sess_meds["A"], sess_meds["B"])]
    first = metas[0] if metas else {}
    commits = sorted({m.get("git_commit", "unknown")[:7] for m in metas})
    dirty = "yes" if any(m.get("git_dirty") == "yes" for m in metas) else "no"
    macros = {
        "NSessions": str(len(files)),
        "NPerConfig": str(len(pooled["A"])),
        "NPerSession": str(len(per_session[0][2])),
        "MedTotA": f"{m('latency_total_ns', 'A'):.3f}",
        "MedTotB": f"{m('latency_total_ns', 'B'):.3f}",
        "PNineFiveTotA": f"{q('latency_total_ns', 'A', 95):.3f}",
        "PNineFiveTotB": f"{q('latency_total_ns', 'B', 95):.3f}",
        "MedTlsA": f"{m('latency_tls_ns', 'A'):.3f}",
        "MedTlsB": f"{m('latency_tls_ns', 'B'):.3f}",
        "MedCpuA": f"{m('client_cpu_ns', 'A'):.3f}",
        "MedCpuB": f"{m('client_cpu_ns', 'B'):.3f}",
        "MedHeapA": f"{m('heap_delta_bytes', 'A') / 1024:.1f}",
        "MedHeapB": f"{m('heap_delta_bytes', 'B') / 1024:.1f}",
        "BytesTxA": f"{med['A']['tx']:.0f}",
        "BytesTxB": f"{med['B']['tx']:.0f}",
        "BytesRxA": f"{med['A']['rx']:.0f}",
        "BytesRxB": f"{med['B']['rx']:.0f}",
        "DeltaTx": f"{med['B']['tx'] - med['A']['tx']:.0f}",
        "DeltaRx": f"{med['B']['rx'] - med['A']['rx']:.0f}",
        "RatioMin": f"{min(ratios):.2f}",
        "RatioMax": f"{max(ratios):.2f}",
        "RatioMedian": f"{statistics.median(ratios):.2f}",
        "GroupA": tex_escape(",".join(sorted(groups["A"]))),
        "GroupB": tex_escape(",".join(sorted(groups["B"]))),
        "GitCommits": tex_escape(", ".join(commits)),
        "GitDirty": dirty,
        "CpuModel": tex_escape(first.get("cpu_model", "unknown")),
        "RunDate": tex_escape(first.get("date_utc", "unknown")[:10]),
    }
    with open(os.path.join(out_dir, "numbers.tex"), "w") as f:
        f.write("% Generated by prepare_data.py from the raw benchmark CSVs. Do not edit.\n")
        for k, v in macros.items():
            f.write(f"\\newcommand{{\\{k}}}{{{v}}}\n")
    print(f"wrote the data files to {out_dir} from {len(files)} sessions")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
