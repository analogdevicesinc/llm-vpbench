#!/usr/bin/env python3
"""LLM-VPBench - Functional + Non-Functional Evaluation"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run_cmd(cmd, env=None, timeout=300):
    result = subprocess.run(
        cmd, shell=True, capture_output=True, text=True, env=env, timeout=timeout
    )
    return result.returncode, result.stdout, result.stderr


class BenchmarkRunner:
    def __init__(self, benchmark_dir, submission_dir, systemc_home):
        self.benchmark_dir = Path(benchmark_dir).resolve()
        self.submission_dir = Path(submission_dir).resolve()
        self.systemc_home = Path(systemc_home).resolve()
        self.build_dir = None
        self.executable = None
        self.config = self._load_config()

    def _load_config(self):
        cfg_path = self.benchmark_dir / "benchmark.json"
        if cfg_path.exists():
            return json.loads(cfg_path.read_text())
        return {}

    def build(self, sanitizer=False):
        self.build_dir = Path(tempfile.mkdtemp())
        env = os.environ.copy()
        env["SYSTEMC_HOME"] = str(self.systemc_home)

        cmake_flags = [
            f"-S {self.benchmark_dir}",
            f"-B {self.build_dir}",
            f"-DSUBMISSION_DIR={self.submission_dir}",
        ]
        if sanitizer:
            cmake_flags.append('-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"')
            cmake_flags.append('-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"')
            cmake_flags.append("-DCMAKE_BUILD_TYPE=Debug")
        else:
            cmake_flags.append("-DCMAKE_BUILD_TYPE=Release")

        rc, out, err = run_cmd(f"cmake {' '.join(cmake_flags)}", env=env)
        if rc != 0:
            return False, out + err

        rc, out, err = run_cmd(f"make -C {self.build_dir} -j{os.cpu_count()}", env=env)
        if rc != 0:
            return False, out + err

        for f in self.build_dir.iterdir():
            if f.is_file() and os.access(f, os.X_OK) and not f.suffix:
                self.executable = f
                break

        if not self.executable:
            return False, "No executable found after build"
        return True, ""

    def run_functional(self):
        if not self.executable:
            return None
        env = os.environ.copy()
        env["SYSTEMC_HOME"] = str(self.systemc_home)
        start = time.time()
        rc, out, err = run_cmd(str(self.executable), env=env, timeout=120)
        elapsed = time.time() - start

        passed_checks = []
        failed_checks = []
        for line in out.splitlines():
            m = re.search(r"\[(PASS|FAIL)\]\s+(?:step\s+\d+\s+\|)?\s*(\S+)", line)
            if m:
                status, checkpoint = m.group(1), m.group(2)
                if status == "PASS":
                    passed_checks.append(checkpoint)
                else:
                    failed_checks.append(checkpoint)

        total_passed = len(passed_checks)
        total_failed = len(failed_checks)
        final_match = re.search(r"FINAL:\s+(\d+)\s+passed,\s+(\d+)\s+failed,\s+(\d+)\s+total", out)
        if final_match:
            total_passed = int(final_match.group(1))
            total_failed = int(final_match.group(2))

        categories = {}
        for line in out.splitlines():
            m = re.match(r"└─\s+(\S+):\s+(\d+)\s+passed,\s+(\d+)\s+failed", line)
            if m:
                cat_id, cp, cf = m.group(1), int(m.group(2)), int(m.group(3))
                categories[cat_id] = {"passed": cp, "failed": cf, "total": cp + cf}

        return {
            "exit_code": rc,
            "passed": total_passed,
            "failed": total_failed,
            "total": total_passed + total_failed,
            "pass_rate": total_passed / max(total_passed + total_failed, 1),
            "wall_time_s": round(elapsed, 3),
            "categories": categories,
            "passed_checks": passed_checks,
            "failed_checks": failed_checks,
            "stdout": out,
        }

    def run_sanitizer(self):
        old_build = self.build_dir
        old_exe = self.executable
        self.build_dir = None
        self.executable = None

        ok, err = self.build(sanitizer=True)
        if not ok:
            self.build_dir = old_build
            self.executable = old_exe
            return {"pass": False, "error": "ASan build failed: " + err[:500]}

        env = os.environ.copy()
        env["SYSTEMC_HOME"] = str(self.systemc_home)
        rc, out, err_out = run_cmd(str(self.executable), env=env, timeout=120)

        combined = out + err_out
        asan_errors = []
        for line in combined.splitlines():
            if "ERROR: AddressSanitizer" in line or "ERROR: UndefinedBehaviorSanitizer" in line:
                asan_errors.append(line.strip())
            elif "runtime error:" in line:
                asan_errors.append(line.strip())

        if self.build_dir and self.build_dir.exists():
            shutil.rmtree(self.build_dir, ignore_errors=True)
        self.build_dir = old_build
        self.executable = old_exe

        return {
            "pass": len(asan_errors) == 0 and rc == 0,
            "errors": asan_errors[:10],
        }

    def run_performance(self):
        if not self.executable:
            return None
        env = os.environ.copy()
        env["SYSTEMC_HOME"] = str(self.systemc_home)
        time_cmd = f"/usr/bin/time -v {self.executable}"
        rc, out, err = run_cmd(time_cmd, env=env, timeout=120)

        wall_time = None
        max_rss_kb = None
        sim_time_ns = None
        for line in err.splitlines():
            m = re.search(r"wall clock.*?(\d+):(\d+\.\d+)", line)
            if m:
                wall_time = int(m.group(1)) * 60 + float(m.group(2))
            m = re.search(r"Maximum resident set size.*?(\d+)", line)
            if m:
                max_rss_kb = int(m.group(1))

        for line in out.splitlines():
            m = re.search(r"Simulation stopped.*?at\s+(\d+)\s*(ps|ns|us|ms|s)", line)
            if m:
                val, unit = int(m.group(1)), m.group(2)
                multipliers = {"ps": 0.001, "ns": 1, "us": 1000, "ms": 1e6, "s": 1e9}
                sim_time_ns = val * multipliers.get(unit, 1)
                break
            m = re.search(r"sc_time_stamp\(\)\s*=\s*(\d+)\s*(ps|ns|us|ms|s)", line)
            if m:
                val, unit = int(m.group(1)), m.group(2)
                multipliers = {"ps": 0.001, "ns": 1, "us": 1000, "ms": 1e6, "s": 1e9}
                sim_time_ns = val * multipliers.get(unit, 1)
                break

        return {"wall_time_s": wall_time, "max_rss_kb": max_rss_kb, "sim_time_ns": sim_time_ns}

    def run_code_quality(self):
        results = {}
        headers = list(self.submission_dir.glob("*.h"))
        if not headers:
            return {"error": "No .h files found"}

        all_lines = []
        for h in headers:
            all_lines.extend(h.read_text().splitlines())
        all_text = "\n".join(all_lines)

        total_lines = len(all_lines)
        code_lines = sum(1 for l in all_lines if l.strip())
        comment_lines = 0
        in_block = False
        for l in all_lines:
            stripped = l.strip()
            if in_block:
                comment_lines += 1
                if "*/" in stripped:
                    in_block = False
            elif stripped.startswith("//"):
                comment_lines += 1
            elif "/*" in stripped:
                comment_lines += 1
                if "*/" not in stripped:
                    in_block = True

        results["loc"] = code_lines
        results["total_lines"] = total_lines
        results["num_files"] = len(headers)
        results["comment_density"] = round(comment_lines / max(total_lines, 1) * 100, 1)

        # Cyclomatic complexity via lizard (parse CSV: NLOC,CCN,token,PARAM,length,...)
        rc, out, _ = run_cmd(f"lizard {' '.join(str(h) for h in headers)} --csv")
        if rc == 0 and out.strip():
            max_ccn = 0
            avg_ccn = 0
            func_count = 0
            for line in out.strip().splitlines():
                parts = line.split(",")
                if len(parts) >= 2:
                    try:
                        ccn = int(parts[1])
                        max_ccn = max(max_ccn, ccn)
                        avg_ccn += ccn
                        func_count += 1
                    except (ValueError, IndexError):
                        pass
            results["max_cyclomatic_complexity"] = max_ccn
            results["avg_cyclomatic_complexity"] = round(avg_ccn / max(func_count, 1), 1)
            results["function_count"] = func_count
        else:
            results["max_cyclomatic_complexity"] = "N/A"

        # Magic numbers: count ALL bare integer literals that are not 0 or 1,
        # not in #define/#include lines, not hex (0x...), not in string literals
        magic_count = 0
        for line in all_lines:
            stripped = line.strip()
            if stripped.startswith("#") or stripped.startswith("//"):
                continue
            # Remove string literals and hex
            cleaned = re.sub(r'"[^"]*"', '', stripped)
            cleaned = re.sub(r"'[^']*'", '', cleaned)
            cleaned = re.sub(r'0[xX][0-9a-fA-F]+[ULul]*', '', cleaned)
            # Find all remaining integer literals
            for m in re.finditer(r'(?<![0-9a-zA-Z_])(\d+)[ULul]*(?![0-9a-zA-Z_])', cleaned):
                num = m.group(1)
                if num not in ("0", "1"):
                    magic_count += 1
        results["magic_numbers"] = magic_count

        # Naming conventions: check function/method names + member variables
        # SystemC convention: snake_case
        func_names = re.findall(r'(?:void|bool|int|unsigned|uint\d+_t|int\d+_t|auto|sc_dt::\w+|tlm::\w+|std::\w+)\s+(\w+)\s*\(', all_text)
        class_names = re.findall(r'SC_MODULE\((\w+)\)', all_text)
        member_vars = re.findall(r'(?:sc_in|sc_out|sc_signal|sc_event|tlm_utils::\w+)\s*<[^>]*>\s+(\w+)', all_text)
        all_ids = func_names + member_vars
        # Exclude common overrides and operators
        all_ids = [i for i in all_ids if not i.startswith("operator") and i not in ("main", "sc_main")]

        if all_ids:
            snake = sum(1 for i in all_ids if re.match(r'^[a-z_][a-z0-9_]*$', i))
            results["naming_conventions"] = round(snake / len(all_ids) * 100)
        else:
            results["naming_conventions"] = 100

        # Null pointer safety: stored as static check; combined with sanitizer at report time
        uses_data_ptr = bool(re.search(r'get_data_ptr\(\)', all_text))
        if uses_data_ptr:
            null_check_patterns = [
                r'(?:data_ptr|ptr|data|bptr|p)\s*==\s*nullptr',
                r'nullptr\s*==\s*(?:data_ptr|ptr|data)',
                r'if\s*\(\s*!(?:data_ptr|ptr|bptr|p)\s*\)',
                r'get_data_ptr\(\)\s*==\s*nullptr',
                r'!\s*trans\.get_data_ptr\(\)',
            ]
            results["_has_null_guard"] = any(re.search(p, all_text) for p in null_check_patterns)
        else:
            results["_has_null_guard"] = True
        results["null_pointer_safety"] = None

        results["has_include_guards"] = all(
            re.search(r"#pragma once|#ifndef\s+\w+_H", h.read_text()) for h in headers
        )

        return results

    def compute_register_coverage(self, functional_result):
        if not self.config:
            return None
        spec_compliance = self.config.get("spec_compliance", {})
        reg_test_map = spec_compliance.get("register_test_map", {})
        if not reg_test_map:
            return None

        passed_set = set(functional_result.get("passed_checks", []))
        total_regs = len(reg_test_map)
        covered = 0
        for reg, tests in reg_test_map.items():
            # Try exact match first, then substring match
            if any(t in passed_set for t in tests):
                covered += 1
            else:
                # Try matching by suffix (e.g., CP-RST-01 might be FN-RST-01 in testbench)
                for t in tests:
                    # Extract the key part after the prefix (e.g., RST-01 from CP-RST-01)
                    parts = t.split("-", 1)
                    if len(parts) > 1:
                        suffix = parts[1]  # e.g., "RST-01" from "CP-RST-01"
                        if any(suffix in cp for cp in passed_set):
                            covered += 1
                            break
        return {"covered": covered, "total": total_regs}

    def compute_functional_breakdown(self, functional_result):
        passed = functional_result.get("passed_checks", [])
        failed = functional_result.get("failed_checks", [])

        groups = {}
        for cp in passed:
            prefix = self._get_group(cp)
            if prefix not in groups:
                groups[prefix] = {"passed": 0, "failed": 0}
            groups[prefix]["passed"] += 1
        for cp in failed:
            prefix = self._get_group(cp)
            if prefix not in groups:
                groups[prefix] = {"passed": 0, "failed": 0}
            groups[prefix]["failed"] += 1

        return groups

    def _get_group(self, checkpoint):
        m = re.match(r"(?:FN|EC|IR|TLM)-([A-Z0-9]+)", checkpoint)
        if m:
            return m.group(1)
        m = re.match(r"([A-Z][A-Z0-9]+)-", checkpoint)
        if m:
            return m.group(1)
        m = re.match(r"CP-[\d.]+-(\w+)", checkpoint)
        if m:
            return m.group(1)
        return "Other"

    def cleanup(self):
        if self.build_dir and self.build_dir.exists():
            shutil.rmtree(self.build_dir, ignore_errors=True)


def format_fraction(passed, total):
    pct = passed / max(total, 1) * 100
    return f"{passed}/{total} ({pct:.1f}%)"


def print_report(report, benchmark_name):
    func = report.get("functional")
    quality = report.get("code_quality", {})
    san = report.get("sanitizer", {})
    perf = report.get("performance", {})
    reg_cov = report.get("register_coverage")
    breakdown = report.get("functional_breakdown", {})

    total_tests = func["total"] if func else 0
    cats = func.get("categories", {}) if func else {}
    num_test_cases = len(cats)

    print(f"\n{'═' * 60}")
    print(f"  {benchmark_name} ({num_test_cases} test cases, {total_tests} assertions)")
    print(f"{'═' * 60}")

    if func:
        print(f"\n  Functional Correctness")
        print(f"  {'─' * 40}")
        if breakdown:
            for group, counts in sorted(breakdown.items()):
                t = counts["passed"] + counts["failed"]
                print(f"    {group} ({t})".ljust(32) + format_fraction(counts["passed"], t))
        print(f"    {'Total'.ljust(26)} {format_fraction(func['passed'], func['total'])}")

    print(f"\n  Code Quality & Spec Compliance")
    print(f"  {'─' * 40}")
    if reg_cov:
        print(f"    {'Register coverage'.ljust(26)} {format_fraction(reg_cov['covered'], reg_cov['total'])}")
    ccn = quality.get("max_cyclomatic_complexity", "N/A")
    print(f"    {'Max cyclomatic complexity'.ljust(26)} {ccn}")
    print(f"    {'Comment density'.ljust(26)} {quality.get('comment_density', '?')}%")
    print(f"    {'Magic numbers (bare literals)'.ljust(30)} {quality.get('magic_numbers', '?')}")
    print(f"    {'Naming conventions'.ljust(26)} {quality.get('naming_conventions', '?')}%")

    print(f"\n  Security")
    print(f"  {'─' * 40}")
    print(f"    {'Null pointer safety'.ljust(26)} {quality.get('null_pointer_safety', 'N/A')}")
    san_status = "PASS" if san.get("pass") else "FAIL"
    print(f"    {'Sanitizer clean build'.ljust(26)} {san_status}")

    if perf:
        print(f"\n  Performance")
        print(f"  {'─' * 40}")
        print(f"    {'Wall time'.ljust(26)} {perf.get('wall_time_s', '?')}s")
        print(f"    {'Max RSS'.ljust(26)} {perf.get('max_rss_kb', '?')} KB")
        sim_ns = perf.get('sim_time_ns')
        if sim_ns is not None:
            print(f"    {'Simulation time'.ljust(26)} {sim_ns} ns")

    print(f"\n{'═' * 60}\n")


def main():
    parser = argparse.ArgumentParser(description="LLM-VPBench Evaluation")
    parser.add_argument("--benchmark", required=True, help="Benchmark directory")
    parser.add_argument("--submission", required=True, help="Submission directory with .h files")
    parser.add_argument("--systemc-home", default=os.environ.get("SYSTEMC_HOME", ""),
                        help="SystemC install path")
    parser.add_argument("--output", default=None, help="Output JSON report path")
    parser.add_argument("--functional-only", action="store_true")
    parser.add_argument("--non-functional-only", action="store_true")
    args = parser.parse_args()

    if not args.systemc_home:
        sys.exit("ERROR: Set SYSTEMC_HOME or pass --systemc-home")

    runner = BenchmarkRunner(args.benchmark, args.submission, args.systemc_home)
    benchmark_name = runner.config.get("name", runner.benchmark_dir.name)
    report = {"benchmark": benchmark_name, "submission": str(runner.submission_dir)}

    try:
        if not args.non_functional_only:
            print("══ Building ══")
            ok, err = runner.build()
            report["compilation"] = {"pass": ok}
            if not ok:
                print(f"BUILD FAILED:\n{err}")
                report["compilation"]["error"] = err[:2000]
                if args.output:
                    Path(args.output).write_text(json.dumps(report, indent=2))
                sys.exit(1)
            print("Build OK\n")

            print("══ Running Functional Tests ══\n")
            func = runner.run_functional()
            report["functional"] = {k: v for k, v in func.items() if k != "stdout"}
            report["functional_breakdown"] = runner.compute_functional_breakdown(func)
            report["register_coverage"] = runner.compute_register_coverage(func)

        if not args.functional_only:
            print("══ Code Quality ══")
            quality = runner.run_code_quality()
            report["code_quality"] = quality

            print("══ Sanitizer (ASan/UBSan) ══")
            san = runner.run_sanitizer()
            report["sanitizer"] = san

            # Resolve null pointer safety: PASS if static guard exists OR sanitizer passes
            if "code_quality" in report:
                q = report["code_quality"]
                has_guard = q.get("_has_null_guard", True)
                san_pass = san.get("pass", False)
                q["null_pointer_safety"] = "PASS" if (has_guard or san_pass) else "FAIL"

            if not args.non_functional_only:
                print("══ Performance ══")
                perf = runner.run_performance()
                report["performance"] = perf

        print_report(report, benchmark_name)

        if args.output:
            Path(args.output).write_text(json.dumps(report, indent=2, default=str))
            print(f"  Report written to: {args.output}")

    finally:
        runner.cleanup()

    if "functional" in report and report["functional"]["failed"] > 0:
        sys.exit(1)


def run_single_eval(args_tuple):
    bench_dir, sub_dir, systemc_home, output_path, label = args_tuple
    try:
        runner = BenchmarkRunner(bench_dir, sub_dir, systemc_home)
        bench_name = runner.config.get("name", runner.benchmark_dir.name)
        report = {"benchmark": bench_name, "submission": str(sub_dir), "label": label}

        ok, err = runner.build()
        report["compilation"] = {"pass": ok}
        if not ok:
            report["compilation"]["error"] = err[:2000]
            report["status"] = "COMPILE_FAIL"
            runner.cleanup()
            if output_path:
                Path(output_path).write_text(json.dumps(report, indent=2, default=str))
            return report

        func = runner.run_functional()
        if func is None:
            report["status"] = "HANG"
            runner.cleanup()
            if output_path:
                Path(output_path).write_text(json.dumps(report, indent=2, default=str))
            return report

        report["functional"] = {k: v for k, v in func.items() if k != "stdout"}
        report["functional_breakdown"] = runner.compute_functional_breakdown(func)
        report["register_coverage"] = runner.compute_register_coverage(func)

        perf = runner.run_performance()
        report["performance"] = perf
        report["status"] = "OK"

        runner.cleanup()
        if output_path:
            Path(output_path).write_text(json.dumps(report, indent=2, default=str))
        return report
    except Exception as e:
        return {"benchmark": str(bench_dir), "label": label, "status": "ERROR", "error": str(e)}


def batch_main():
    import concurrent.futures

    parser = argparse.ArgumentParser(description="LLM-VPBench Batch Evaluation")
    parser.add_argument("--benchmarks-dir", required=True, help="Directory containing benchmark subdirs")
    parser.add_argument("--submissions-dir", required=True, help="Directory containing model subdirs")
    parser.add_argument("--systemc-home", default=os.environ.get("SYSTEMC_HOME", ""),
                        help="SystemC install path")
    parser.add_argument("--output-dir", default="results", help="Output directory for JSON reports")
    parser.add_argument("--parallel", type=int, default=4, help="Max parallel workers")
    parser.add_argument("--include-golden", action="store_true", help="Also run golden models")
    args = parser.parse_args()

    if not args.systemc_home:
        sys.exit("ERROR: Set SYSTEMC_HOME or pass --systemc-home")

    benchmarks_dir = Path(args.benchmarks_dir).resolve()
    submissions_dir = Path(args.submissions_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    benchmarks = [d for d in sorted(benchmarks_dir.iterdir())
                  if d.is_dir() and (d / "CMakeLists.txt").exists()]
    models = [d for d in sorted(submissions_dir.iterdir()) if d.is_dir()]

    tasks = []
    for bench in benchmarks:
        bench_name = bench.name
        if args.include_golden:
            golden_dir = bench / "golden"
            if golden_dir.exists():
                out = output_dir / f"{bench_name}_golden.json"
                tasks.append((str(bench), str(golden_dir), args.systemc_home, str(out), f"{bench_name}/golden"))

        for model in models:
            sub = model / bench_name
            if sub.exists() and list(sub.glob("*.h")):
                out = output_dir / f"{bench_name}_{model.name}.json"
                tasks.append((str(bench), str(sub), args.systemc_home, str(out), f"{bench_name}/{model.name}"))

    print(f"Running {len(tasks)} evaluations with {args.parallel} workers...\n")

    results = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.parallel) as executor:
        futures = {executor.submit(run_single_eval, t): t[-1] for t in tasks}
        for future in concurrent.futures.as_completed(futures):
            label = futures[future]
            try:
                r = future.result(timeout=300)
                status = r.get("status", "?")
                passed = r.get("functional", {}).get("passed", "-")
                total = r.get("functional", {}).get("total", "-")
                perf = r.get("performance", {}) or {}
                wall = perf.get("wall_time_s", "-")
                rss = perf.get("max_rss_kb", "-")
                sim = perf.get("sim_time_ns", "-")
                print(f"  {label:<40} {status:<12} {passed}/{total}  wall={wall}s  RSS={rss}KB  sim={sim}ns")
                results.append(r)
            except Exception as e:
                print(f"  {label:<40} EXCEPTION: {e}")
                results.append({"label": label, "status": "EXCEPTION", "error": str(e)})

    summary_path = output_dir / "summary.json"
    summary_path.write_text(json.dumps(results, indent=2, default=str))
    print(f"\n  Summary written to: {summary_path}")

    print(f"\n{'═' * 80}")
    print(f"  {'Benchmark':<20} {'Model':<16} {'Pass':<6} {'Total':<6} {'%':<8} {'Wall(s)':<8} {'RSS(KB)':<8} {'SimTime(ns)'}")
    print(f"{'─' * 80}")
    for r in sorted(results, key=lambda x: x.get("label", "")):
        parts = r.get("label", "/").split("/")
        bench = parts[0] if parts else "?"
        model = parts[1] if len(parts) > 1 else "?"
        if r.get("status") == "OK":
            f = r["functional"]
            perf = r.get("performance", {}) or {}
            pct = f"{f['pass_rate']*100:.1f}%"
            print(f"  {bench:<20} {model:<16} {f['passed']:<6} {f['total']:<6} {pct:<8} {perf.get('wall_time_s', '-'):<8} {perf.get('max_rss_kb', '-'):<8} {perf.get('sim_time_ns', '-')}")
        else:
            print(f"  {bench:<20} {model:<16} {r.get('status', '?')}")
    print(f"{'═' * 80}")


if __name__ == "__main__":
    if "--benchmarks-dir" in sys.argv:
        batch_main()
    else:
        main()
