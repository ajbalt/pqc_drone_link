#!/usr/bin/env python3
"""Turn the raw two-machine T10 files into the data files of Progress Report 3.

Usage: python3 -I docs/report/prepare_data3.py RESULTS_DIR OUT_DIR [SESSION_ID ...]
       default sessions: t10_lan_s1 t10_lan_s2b t10_lan_s3b

For every session it needs, in RESULTS_DIR:
  <id>.ok.csv  <id>.fail.csv        client rows (bench_handshake)
  <id>.server_A.csv  <id>.server_B.csv   server rows (tls_server --handshake-csv)
  <id>.client_meta.txt  <id>.server_meta.txt
and writes into OUT_DIR:
  numbers3.tex          LaTeX macros for every number the report text uses
  sess_table.tex        rows of the per-session medians table
  box_tls.tex, box_cpu_client.tex, box_cpu_server.tex   generated \\addplot lines
  scatter_<K>_<C>.csv   attempt, total_ms, tls_ms for session K (One, Two, Three), config C
  slow_table.tex        rows of the table of slow connects

Nothing here estimates or edits a measurement; every figure comes from the raw files, and the
script can be rerun by anyone. It refuses a session that is not from a Release build, has
failures, or whose client and server counts differ. Standard library only. Existing output
files are overwritten because they are derived data, not results.
"""
import csv
import math
import os
import statistics
import sys

KEYS = ["One", "Two", "Three"]
SLOW_MS = 100.0   # a connect-to-end time above this is listed as "slow"


def pct(v, p):
    """Nearest-rank percentile of an ascending list; p in (0, 100]."""
    return v[math.ceil(p / 100 * len(v)) - 1]


def read(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def kv(path):
    out = {}
    for line in open(path):
        if line.startswith("---"):
            break
        if "=" in line:
            k, v = line.rstrip("\n").split("=", 1)
            out.setdefault(k, v)
    return out


def tex(s):
    for a, b in (("\\", " "), ("&", r"\&"), ("%", r"\%"), ("_", r"\_"), ("#", r"\#"),
                 ("$", r"\$"), ("{", r"\{"), ("}", r"\}"), ("~", " "), ("^", " ")):
        s = s.replace(a, b)
    return s


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    res, out = sys.argv[1], sys.argv[2]
    ids = sys.argv[3:] or ["t10_lan_s1", "t10_lan_s2b", "t10_lan_s3b"]
    os.makedirs(out, exist_ok=True)
    m = {}                      # macro name -> text
    box = {"tls": [], "cc": [], "sc": []}
    sess_rows, slow_rows = [], []
    commits = {}
    dates = set()
    for n, (k, rid) in enumerate(zip(KEYS, ids)):
        p = os.path.join(res, rid)
        ok, fail = read(p + ".ok.csv"), read(p + ".fail.csv")
        if {r["build"] for r in ok} != {"release"}:
            raise SystemExit(f"{rid}: not a release build")
        cm, sm = kv(p + ".client_meta.txt"), kv(p + ".server_meta.txt")
        commits[k] = (cm["git_commit"], cm["git_dirty"], sm["git_commit"], sm["git_dirty"])
        v = {}                  # per config: lists
        for c in "AB":
            mine = [r for r in ok if r["config"] == c]
            warm = sum(1 for r in mine if r["warmup"] == "1")
            meas = [r for r in mine if r["warmup"] == "0"]
            srows = read(f"{p}.server_{c}.csv")
            sfail = [r for r in srows if r["ok"] != "1"]
            sgood = [r for r in srows if r["ok"] == "1"][warm:]
            if any(r["config"] == c for r in fail) or sfail or len(meas) != len(sgood):
                raise SystemExit(f"{rid} config {c}: failures or client/server counts differ "
                                 f"({len(meas)} vs {len(sgood)})")
            if not all(a["bytes_tx"] == b["bytes_rx"] and a["bytes_rx"] == b["bytes_tx"]
                       for a, b in zip(meas, sgood)):
                raise SystemExit(f"{rid} config {c}: client and server byte counts disagree")
            v[c] = dict(rows=meas,
                        tls=sorted(float(r["latency_tls_ns"]) / 1e6 for r in meas),
                        tot=sorted(float(r["latency_total_ns"]) / 1e6 for r in meas),
                        cc=sorted(float(r["client_cpu_ns"]) / 1e6 for r in meas),
                        sc=sorted(float(r["server_cpu_ns"]) / 1e6 for r in sgood),
                        ch=int(statistics.median(float(r["heap_delta_bytes"]) for r in meas)),
                        sh=int(statistics.median(float(r["heap_delta_bytes"]) for r in sgood)),
                        ctx=int(statistics.median(int(r["bytes_tx"]) for r in meas)),
                        crx=int(statistics.median(int(r["bytes_rx"]) for r in meas)))
            m[f"N{k}{c}"] = str(len(meas))
            pos = 3.2 * n + (1 if c == "A" else 1.9)
            for tag in ("tls", "cc", "sc"):
                box[tag].append((pos, c, v[c][tag]))
            with open(os.path.join(out, f"scatter_{k}_{c}.csv"), "w") as f:
                f.write("attempt,total_ms,tls_ms\n")
                for r in meas:
                    f.write(f"{r['attempt']},{float(r['latency_total_ns'])/1e6:.4f},"
                            f"{float(r['latency_tls_ns'])/1e6:.4f}\n")
            for r in meas:
                if float(r["latency_total_ns"]) / 1e6 > SLOW_MS:
                    slow_rows.append((k, c, r["attempt"], float(r["latency_total_ns"]) / 1e6,
                                      float(r["latency_tls_ns"]) / 1e6))
            m[f"Slow{k}{c}"] = str(sum(1 for t in v[c]["tot"] if t > SLOW_MS))
            for name, key in (("TlsMed", "tls"), ("TlsPnf", "tls"), ("TotMed", "tot"),
                              ("CCpuMed", "cc"), ("SCpuMed", "sc")):
                val = statistics.median(v[c][key]) if name.endswith("Med") else pct(v[c][key], 95)
                m[f"{name}{k}{c}"] = f"{val:.3f}"
        a, b = v["A"], v["B"]
        for name, key in (("Tls", "tls"), ("CCpu", "cc"), ("SCpu", "sc")):
            ra, rb = statistics.median(a[key]), statistics.median(b[key])
            m[f"{name}Ratio{k}"] = f"{rb / ra:.2f}"
            m[f"{name}Diff{k}"] = f"{rb - ra:.3f}"
        sess_rows.append(" & ".join([
            k and f"{n + 1}", tex(rid.replace("t10_lan_", "")),
            m[f"TlsMed{k}A"], m[f"TlsMed{k}B"], m[f"TlsRatio{k}"],
            m[f"SCpuMed{k}A"], m[f"SCpuMed{k}B"], m[f"SCpuRatio{k}"]]))
        m[f"Rtt{k}"] = tex(cm.get("ping_rtt", "NA").split("=")[-1].strip())
        m[f"Date{k}"] = tex(cm["date_utc"][:10])
        dates.add(cm["date_utc"][:10])
        if n == 0:
            for c in "AB":
                for key in ("ch", "sh", "ctx", "crx"):
                    m[f"{key.capitalize()}{c}"] = f"{v[c][key]:,}".replace(",", "{,}")
            m["CliCpuModel"] = tex(cm["cpu_model"])
            m["SrvCpuModel"] = tex(sm["cpu_model"])
            m["SrvGov"] = tex(sm["cpu_governor"])
            m["CliGov"] = tex(cm["cpu_governor"])
            m["NWarm"] = str(sum(1 for r in ok if r["config"] == "A" and r["warmup"] == "1"))
        else:   # the deterministic quantities must be the same in every session
            for c in "AB":
                same = (f"{v[c]['ch']:,}".replace(",", "{,}") == m[f"Ch{c}"]
                        and f"{v[c]['ctx']:,}".replace(",", "{,}") == m[f"Ctx{c}"])
                m[f"Same{k}{c}"] = "yes" if same else "NO"
    for tag, fn in (("tls", "box_tls.tex"), ("cc", "box_cpu_client.tex"),
                    ("sc", "box_cpu_server.tex")):
        lines = ["% Generated by prepare_data3.py. Do not edit."]
        for pos, c, vals in box[tag]:
            st = [pct(vals, 5), pct(vals, 25), statistics.median(vals), pct(vals, 75),
                  pct(vals, 95)]
            lines.append(
                f"\\addplot+[boxplot prepared={{draw position={pos:.1f}, lower whisker={st[0]:.4f}, "
                f"lower quartile={st[1]:.4f}, median={st[2]:.4f}, upper quartile={st[3]:.4f}, "
                f"upper whisker={st[4]:.4f}}}, solid, fill=color{c}!25, draw=color{c}!80!black] "
                f"coordinates {{}};")
        open(os.path.join(out, fn), "w").write("\n".join(lines) + "\n")
    names = dict(zip(KEYS, ["s1", "s2b", "s3b"]))
    open(os.path.join(out, "sess_table.tex"), "w").write(
        "\\\\\n".join(sess_rows) + "\n")
    srows = [f"{n + 1} & {c} & {a} & {t:.0f} & {tl:.1f}" for (k, c, a, t, tl) in slow_rows
             for n in [KEYS.index(k)]]
    open(os.path.join(out, "slow_table.tex"), "w").write("\\\\\n".join(srows) + "\n")
    m["Dates"] = ", ".join(sorted(dates))
    m["NSlow"] = str(len(slow_rows))
    m["SlowMin"] = f"{min(r[3] for r in slow_rows):.0f}" if slow_rows else "0"
    m["SlowMax"] = f"{max(r[3] for r in slow_rows):.0f}" if slow_rows else "0"
    m["SlowTlsMax"] = f"{max(r[4] for r in slow_rows):.1f}" if slow_rows else "0"
    m["SlowOnlyB"] = "yes" if all(r[1] == "B" for r in slow_rows) else "NO"
    for k, (cc, cd, sc, sd) in commits.items():
        m[f"Commit{k}"] = cc[:7]
        m[f"Dirty{k}"] = cd
        m[f"SameCommit{k}"] = "yes" if cc == sc and cd == sd == "no" == cd else "NO"
    with open(os.path.join(out, "numbers3.tex"), "w") as f:
        f.write("% Generated by prepare_data3.py from the raw files. Do not edit.\n")
        for name, val in m.items():
            f.write(f"\\newcommand{{\\{name.replace('_', '')}}}{{{val}}}\n")
    print(f"wrote {out}: {len(ids)} sessions, {len(m)} macros, {len(slow_rows)} slow connects")


if __name__ == "__main__":
    main()
