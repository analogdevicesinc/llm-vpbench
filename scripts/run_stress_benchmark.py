#!/usr/bin/env python3
"""
LLM-VPBench Stress Benchmark Runner
Precompiles all models, then runs stress tests N times for statistical analysis.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor, as_completed


def run_cmd(cmd, env=None, timeout=300):
    result = subprocess.run(
        cmd, shell=True, capture_output=True, text=True, env=env, timeout=timeout
    )
    return result.returncode, result.stdout, result.stderr


def precompile(benchmark_dir, submission_dir, systemc_home, build_dir):
    """Compile once, return executable path or None on failure."""
    build_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["SYSTEMC_HOME"] = str(systemc_home)

    rc, out, err = run_cmd(
        f"cmake -S {benchmark_dir} -B {build_dir} "
        f"-DSUBMISSION_DIR={submission_dir} -DCMAKE_BUILD_TYPE=Release",
        env=env
    )
    if rc != 0:
        return None, f"CMAKE FAIL: {err[-500:]}"

    rc, out, err = run_cmd(f"make -C {build_dir} -j{os.cpu_count()}", env=env)
    if rc != 0:
        return None, f"MAKE FAIL: {err[-500:]}"

    for f in build_dir.iterdir():
        if f.is_file() and os.access(f, os.X_OK) and not f.suffix:
            return f, ""
    return None, "No executable found"


def run_single_stress(exe_path, run_id):
    """Run one stress iteration, return parsed metrics."""
    env = os.environ.copy()
    try:
        rc, out, err = run_cmd(
            f"/usr/bin/time -v {exe_path} --stress",
            env=env, timeout=120
        )
    except subprocess.TimeoutExpired:
        return {"run_id": run_id, "status": "TIMEOUT"}

    stress_line = None
    for line in out.splitlines():
        if "[STRESS]" in line and "benchmark=" in line:
            stress_line = line.strip()
            break

    if not stress_line:
        return {"run_id": run_id, "status": "NO_STRESS_OUTPUT"}

    rss = None
    wall_from_time = None
    for line in err.splitlines():
        m = re.search(r"Maximum resident set size.*?(\d+)", line)
        if m:
            rss = int(m.group(1))
        m = re.search(r"wall clock.*?(\d+):(\d+\.\d+)", line)
        if m:
            wall_from_time = int(m.group(1)) * 60 + float(m.group(2))

    metrics = {"run_id": run_id, "status": "OK", "rss_kb": rss, "total_wall_s": wall_from_time}

    m = re.search(r"transactions=(\d+)", stress_line)
    if m:
        metrics["transactions"] = int(m.group(1))
    m = re.search(r"wall_time_ms=([\d.e+]+)", stress_line)
    if m:
        metrics["stress_wall_ms"] = float(m.group(1))
    m = re.search(r"throughput_txn_per_s=([\d.e+]+)", stress_line)
    if m:
        metrics["throughput"] = float(m.group(1))

    return metrics


def main():
    parser = argparse.ArgumentParser(description="Stress Benchmark Runner (N iterations)")
    parser.add_argument("--benchmarks-dir", required=True)
    parser.add_argument("--submissions-dir", required=True)
    parser.add_argument("--systemc-home", default=os.environ.get("SYSTEMC_HOME", ""))
    parser.add_argument("--build-cache-dir", default="/tmp/vpbench_stress_builds")
    parser.add_argument("--output-dir", default="stress_results")
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--parallel-runs", type=int, default=1,
                        help="Parallel stress runs (keep 1 for accurate timing)")
    parser.add_argument("--include-golden", action="store_true")
    args = parser.parse_args()

    if not args.systemc_home:
        sys.exit("ERROR: Set SYSTEMC_HOME or pass --systemc-home")

    benchmarks_dir = Path(args.benchmarks_dir).resolve()
    submissions_dir = Path(args.submissions_dir).resolve()
    build_cache = Path(args.build_cache_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    systemc_home = Path(args.systemc_home).resolve()

    benchmarks = [d for d in sorted(benchmarks_dir.iterdir())
                  if d.is_dir() and (d / "CMakeLists.txt").exists()]
    models = [d for d in sorted(submissions_dir.iterdir()) if d.is_dir()]

    # Build task list: (benchmark_name, model_name, benchmark_dir, submission_dir)
    tasks = []
    for bench in benchmarks:
        bench_name = bench.name
        if args.include_golden:
            golden_dir = bench / "golden"
            if golden_dir.exists():
                tasks.append((bench_name, "golden", bench, golden_dir))
        for model in models:
            sub = model / bench_name
            if sub.exists() and list(sub.glob("*.h")):
                tasks.append((bench_name, model.name, bench, sub))

    # Phase 1: Precompile all
    print(f"{'═' * 70}")
    print(f"  Phase 1: Precompiling {len(tasks)} configurations")
    print(f"{'═' * 70}\n")

    executables = {}
    for bench_name, model_name, bench_dir, sub_dir in tasks:
        key = f"{bench_name}/{model_name}"
        bdir = build_cache / f"{bench_name}_{model_name}"
        exe, err = precompile(bench_dir, sub_dir, systemc_home, bdir)
        if exe:
            executables[key] = exe
            print(f"  ✓ {key:<45} {exe.name}")
        else:
            print(f"  ✗ {key:<45} {err[:60]}")

    print(f"\n  Compiled: {len(executables)}/{len(tasks)}\n")

    # Phase 2: Run stress tests N times
    print(f"{'═' * 70}")
    print(f"  Phase 2: Running {args.iterations} iterations per model")
    print(f"{'═' * 70}\n")

    all_results = {}

    total_models = len(executables)
    model_idx = 0
    for key, exe in sorted(executables.items()):
        model_idx += 1
        print(f"\n  [{model_idx}/{total_models}] {key}", flush=True)
        runs = []

        for i in range(args.iterations):
            result = run_single_stress(str(exe), i)
            runs.append(result)
            ok_so_far = sum(1 for r in runs if r["status"] == "OK")
            status_char = "✓" if result["status"] == "OK" else "✗"
            wall = f"{result.get('stress_wall_ms', 0):.1f}ms" if result.get("stress_wall_ms") else result["status"]
            print(f"    run {i+1:>2}/{args.iterations} {status_char} {wall}  [{ok_so_far}/{i+1} ok]", flush=True)

        print(f"    ── done: {ok_so_far}/{args.iterations} successful", flush=True)

        ok_runs = [r for r in runs if r["status"] == "OK"]
        if ok_runs:
            walls = [r["stress_wall_ms"] for r in ok_runs if "stress_wall_ms" in r]
            thrus = [r["throughput"] for r in ok_runs if "throughput" in r]
            rsses = [r["rss_kb"] for r in ok_runs if r.get("rss_kb")]

            summary = {
                "key": key,
                "n_ok": len(ok_runs),
                "n_total": len(runs),
                "wall_ms_mean": sum(walls) / len(walls) if walls else None,
                "wall_ms_min": min(walls) if walls else None,
                "wall_ms_max": max(walls) if walls else None,
                "wall_ms_stddev": (sum((w - sum(walls)/len(walls))**2 for w in walls) / len(walls))**0.5 if len(walls) > 1 else 0,
                "throughput_mean": sum(thrus) / len(thrus) if thrus else None,
                "throughput_min": min(thrus) if thrus else None,
                "throughput_max": max(thrus) if thrus else None,
                "rss_kb_mean": sum(rsses) / len(rsses) if rsses else None,
                "transactions": ok_runs[0].get("transactions") if ok_runs else None,
            }
            print(f" wall={summary['wall_ms_mean']:.2f}±{summary['wall_ms_stddev']:.2f}ms "
                  f"throughput={summary['throughput_mean']:.2e} txn/s "
                  f"RSS={summary['rss_kb_mean']:.0f}KB")
        else:
            summary = {"key": key, "n_ok": 0, "n_total": len(runs), "status": "ALL_FAILED"}
            print(f" ALL FAILED ({runs[0].get('status', '?')})")

        all_results[key] = {"summary": summary, "runs": runs}

        # Write per-model results
        bench_name, model_name = key.split("/")
        result_file = output_dir / f"{bench_name}_{model_name}.json"
        result_file.write_text(json.dumps(all_results[key], indent=2, default=str))

    # Phase 3: Summary table
    print(f"\n{'═' * 90}")
    print(f"  {'Benchmark':<20} {'Model':<16} {'N':<4} {'Wall(ms)':<18} {'Throughput(Mtxn/s)':<22} {'RSS(KB)'}")
    print(f"{'─' * 90}")

    for key in sorted(all_results.keys()):
        s = all_results[key]["summary"]
        parts = key.split("/")
        bench, model = parts[0], parts[1]
        if s.get("n_ok", 0) > 0:
            wall_str = f"{s['wall_ms_mean']:.2f} ± {s['wall_ms_stddev']:.2f}"
            thru_str = f"{s['throughput_mean']/1e6:.2f}"
            rss_str = f"{s['rss_kb_mean']:.0f}"
            print(f"  {bench:<20} {model:<16} {s['n_ok']:<4} {wall_str:<18} {thru_str:<22} {rss_str}")
        else:
            print(f"  {bench:<20} {model:<16} {s['n_total']:<4} {'FAILED':<18}")

    print(f"{'═' * 90}")

    # Write combined summary
    summary_file = output_dir / "stress_summary.json"
    summary_data = {k: v["summary"] for k, v in all_results.items()}
    summary_file.write_text(json.dumps(summary_data, indent=2, default=str))
    print(f"\n  Results written to: {output_dir}/")
    print(f"  Summary: {summary_file}")


if __name__ == "__main__":
    main()
