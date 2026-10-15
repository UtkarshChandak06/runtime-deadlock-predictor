#!/usr/bin/env python3
"""Reproducible baseline/preload measurements; standard library only."""
import argparse, csv, datetime, json, os, platform, statistics, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "build" / "libdeadlock_monitor.so"
BIN = ROOT / "build" / "bench_mutex"
CASES = [("single", 1)] + [(kind, count) for kind in ("independent", "shared", "ordered") for count in (1, 2, 4, 8)]

def execute(kind, threads, iterations, mode):
    env = os.environ.copy()
    if mode == "monitored": env["LD_PRELOAD"] = str(LIB)
    else: env.pop("LD_PRELOAD", None)
    p = subprocess.run([str(BIN), kind, str(threads), str(iterations), mode], cwd=ROOT,
        env=env, text=True, capture_output=True, timeout=20)
    if p.returncode or p.stderr:
        raise RuntimeError(f"{kind}/{threads}/{mode}: exit={p.returncode}, stderr={p.stderr!r}")
    record = json.loads(p.stdout)
    expected_completed = iterations * threads
    expected_operations = expected_completed * (6 if kind == "ordered" else 2)
    if (record["monitor_loaded"] != (mode == "monitored") or
            record["benchmark"] != kind or record["threads"] != threads or
            record["iterations"] != iterations or
            record["completed_iterations"] != expected_completed or
            record["counter_value"] != expected_completed or
            record["operations"] != expected_operations):
        raise RuntimeError(f"invalid benchmark record: {record}")
    return record

def median(values): return statistics.median(values)
def cv(values):
    return statistics.pstdev(values) / statistics.mean(values) * 100 if len(values) > 1 else 0

FIELDS = ["benchmark","mode","threads","iterations","run","sequence","phase","elapsed_seconds","operations","completed_iterations","counter_value","monitor_loaded","monitor_library","graph_nodes","graph_edges","graph_dropped","tracker_held","tracker_dropped","log_dropped","log_errors"]

def append_record(target, record):
    new = not target.exists()
    with target.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        if new: writer.writeheader()
        writer.writerow(record)
        f.flush()
        os.fsync(f.fileno())

def svg(rows, target):
    width, height, left, bottom = 1200, 520, 70, 80
    maximum = max(max(r["baseline_median_seconds"], r["monitored_median_seconds"]) for r in rows)
    scale = (height - bottom - 40) / maximum
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}"><style>text{{font:12px sans-serif}}.b{{fill:#4e79a7}}.m{{fill:#e15759}}</style>', '<text x="70" y="25">Mutex workload median elapsed time (actual measurements)</text>']
    step = (width - left - 20) / len(rows)
    for i, row in enumerate(rows):
        x = left + i * step
        for offset, key, cls in ((0, "baseline_median_seconds", "b"), (18, "monitored_median_seconds", "m")):
            h = row[key] * scale
            parts.append(f'<rect class="{cls}" x="{x+offset:.1f}" y="{height-bottom-h:.1f}" width="16" height="{h:.1f}"/>')
        parts.append(f'<text transform="translate({x:.1f},{height-bottom+15}) rotate(55)">{row["benchmark"]}/{row["threads"]}</text>')
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-20}" y2="{height-bottom}" stroke="black"/><text x="{left}" y="{height-20}" fill="#4e79a7">blue baseline</text><text x="{left+150}" y="{height-20}" fill="#e15759">red monitored</text></svg>')
    target.write_text("".join(parts))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--target-seconds", type=float, default=.10)
    ap.add_argument("--max-iterations", type=int, default=25_600_000)
    ap.add_argument("--results-dir", type=Path)
    args = ap.parse_args()
    if args.runs < 5 or args.target_seconds <= 0: raise SystemExit("runs must be >=5; target must be positive")
    stamp = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=5, minutes=30))).strftime("%Y%m%dT%H%M%S%z")
    out = args.results_dir or ROOT / "benchmarks" / "results" / stamp
    if out.exists(): raise SystemExit(f"refusing to overwrite {out}")
    out.mkdir(parents=True)
    env = {"status": "running", "platform": platform.platform(), "python": platform.python_version(), "cwd": str(ROOT), "runs": args.runs, "target_seconds": args.target_seconds, "max_iterations": args.max_iterations,
           "command": [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]}
    (out / "environment.json").write_text(json.dumps(env, indent=2))
    for command, key in [(["uname", "-a"], "uname"), (["lscpu"], "lscpu"), (["gcc", "--version"], "gcc")]:
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            env.update(status="failed", error=f"environment command failed: {command!r}", stderr=result.stderr)
            (out / "environment.json").write_text(json.dumps(env, indent=2))
            raise RuntimeError(env["error"])
        env[key] = result.stdout
    (out / "environment.json").write_text(json.dumps(env, indent=2))
    calibration, raw, calibration_status = [], [], {}
    try:
        for kind, threads in CASES:
            iterations = 50_000
            while True:
                b, m = execute(kind, threads, iterations, "baseline"), execute(kind, threads, iterations, "monitored")
                records = [{**x, "mode": mode, "run": "", "sequence": "", "phase": "calibration"} for x, mode in ((b,"baseline"),(m,"monitored"))]
                for record in records:
                    calibration.append(record); append_record(out / "calibration.csv", record)
                reached = min(b["elapsed_seconds"], m["elapsed_seconds"]) >= args.target_seconds
                if reached or iterations >= args.max_iterations: break
                iterations = min(iterations * 2, args.max_iterations)
            calibration_status[(kind, threads)] = {"capped": not reached, "baseline_seconds": b["elapsed_seconds"], "monitored_seconds": m["elapsed_seconds"]}
            for run in range(1, args.runs + 1):
                modes = ("baseline", "monitored") if run % 2 else ("monitored", "baseline")
                for sequence, mode in enumerate(modes, 1):
                    record = {**execute(kind, threads, iterations, mode), "mode": mode, "run": run, "sequence": sequence, "phase": "measurement"}
                    raw.append(record); append_record(out / "raw.csv", record)
    except Exception as exc:
        env.update(status="failed", error=repr(exc), calibration_records=len(calibration), measurement_records=len(raw))
        (out / "environment.json").write_text(json.dumps(env, indent=2))
        raise
    summary=[]
    for kind, threads in CASES:
        group = [r for r in raw if r["benchmark"] == kind and r["threads"] == threads]
        base = [r["elapsed_seconds"] for r in group if r["mode"] == "baseline"]
        mon = [r["elapsed_seconds"] for r in group if r["mode"] == "monitored"]
        bm, mm = median(base), median(mon)
        ops = group[0]["operations"]
        status = calibration_status[(kind, threads)]
        short = bm < args.target_seconds
        summary.append({"benchmark":kind,"threads":threads,"iterations":group[0]["iterations"],"operations":ops,
            "baseline_median_seconds":bm,"monitored_median_seconds":mm,"absolute_difference_seconds":mm-bm,
            "overhead_percent":(mm-bm)/bm*100,"baseline_operations_per_second":ops/bm,"monitored_operations_per_second":ops/mm,
            "baseline_min_seconds":min(base),"baseline_max_seconds":max(base),"monitored_min_seconds":min(mon),"monitored_max_seconds":max(mon),
            "baseline_cv_percent":cv(base),"monitored_cv_percent":cv(mon),
            "calibration_capped":int(status["capped"]),"baseline_below_target":int(short),
            "warning_count":int(status["capped"]) + int(short)})
    with (out / "summary.csv").open("w", newline="") as f:
        w=csv.DictWriter(f, fieldnames=list(summary[0])); w.writeheader(); w.writerows(summary)
    svg(summary, out / "median.svg")
    env.update(status="complete", calibration_records=len(calibration), measurement_records=len(raw),
               capped_cases=sum(x["capped"] for x in calibration_status.values()),
               short_baseline_cases=sum(r["baseline_below_target"] for r in summary))
    (out / "environment.json").write_text(json.dumps(env, indent=2))
    print(out)

if __name__ == "__main__": main()
