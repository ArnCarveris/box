#!/usr/bin/env python3
"""Runs the bench projects under bench/ and writes one JSON report per project.

Every case reports its median, confidence interval and percentiles in the format that
project/pm.py charts. A project given with a slash is a path to a bench project elsewhere;
its report is named after the path with slashes as underscores. --baseline takes a report
or a directory of reports and flags every case whose median moved beyond --threshold.

Usage:
  bench.py [--out dir] [--env name] [--cfg release] [--baseline path] [--threshold 0.05]
           [--time sec] [--samples n] [--fail-on-regression] [project ...]
"""
import argparse
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH_DIR = os.path.join(ROOT, "bench")
OUT_DIR = os.path.join(BENCH_DIR, "out")


def projects():
    found = []
    if os.path.isdir(BENCH_DIR):
        for name in sorted(os.listdir(BENCH_DIR)):
            config = read_json(os.path.join(BENCH_DIR, name, "project.json"))
            if isinstance(config, dict) and "bench" in config:
                found.append(name)
    return found


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def project_dir(name):
    return name if "/" in name else "bench/" + name


def slug(name):
    return name.replace("/", "_")


def project_id(name):
    config = read_json(os.path.join(ROOT, project_dir(name), "project.json"))
    if isinstance(config, dict) and isinstance(config.get("id"), str):
        return config["id"]
    return "bench_" + slug(name)


def baseline_report(baseline, name, single):
    if os.path.isfile(baseline):
        data = read_json(baseline)
        if isinstance(data, dict) and (single or data.get("project") == project_id(name)):
            return baseline
        return None
    direct = os.path.join(baseline, slug(name) + ".json")
    if os.path.isfile(direct):
        return direct
    for candidate in sorted(glob.glob(os.path.join(baseline, "*.json"))):
        data = read_json(candidate)
        if isinstance(data, dict) and data.get("project") == project_id(name):
            return candidate
    return None


def run_project(name, out, env, cfg, previous, extra):
    report = os.path.abspath(os.path.join(out, slug(name) + ".json"))
    if os.path.isfile(report):
        os.remove(report)
    args = ["--json", report, *extra]
    if previous:
        args += ["--baseline", os.path.abspath(previous)]
    env_flag = f"--local-env={env}" if env else "--local-env"
    proc = subprocess.run(
        ["bake3", "run", project_dir(name), env_flag, "--cfg", cfg, "--", *args],
        cwd=ROOT, text=True, capture_output=True)
    data = read_json(report)
    if not isinstance(data, dict) or data.get("failed") or proc.returncode:
        print(f"FAIL {project_dir(name)}")
        for failure in (data or {}).get("failures") or []:
            print(f"FAILED {failure.get('suite')}.{failure.get('case')} ({failure.get('status')})")
        print(proc.stdout[-4000:], proc.stderr[-4000:])
        return None, report
    return data, report


def changes(data):
    result = []
    for case in data.get("benchmarks") or []:
        change = case.get("change")
        if isinstance(change, (int, float)):
            result.append((f"{case.get('suite')}.{case.get('case')}", change))
    return result


def format_ns(value):
    if not isinstance(value, (int, float)):
        return "-"
    for unit, scale in (("s", 1e9), ("ms", 1e6), ("us", 1e3)):
        if value >= scale:
            return f"{value / scale:.2f}{unit}"
    return f"{value:.1f}ns"


def print_cases(data):
    for case in data.get("benchmarks") or []:
        name = f"{case.get('suite')}.{case.get('case')}"
        line = (f"  {name:<40} {format_ns(case.get('median_ns')):>10} "
                f"[{format_ns(case.get('ci_low_ns'))}, {format_ns(case.get('ci_high_ns'))}] "
                f"p99 {format_ns(case.get('p99_ns'))}")
        if isinstance(case.get("change"), (int, float)):
            line += f"  {case['change'] * 100:+.1f}%"
        print(line)


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("projects", nargs="*")
    parser.add_argument("--out", default=OUT_DIR)
    parser.add_argument("--env")
    parser.add_argument("--cfg", default="release")
    parser.add_argument("--baseline")
    parser.add_argument("--time", type=float)
    parser.add_argument("--samples", type=int)
    parser.add_argument("--threshold", type=float, default=0.05)
    parser.add_argument("--fail-on-regression", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()
    names = args.projects or projects()
    if not names:
        sys.exit("no bench projects under bench/")
    if args.baseline and not os.path.exists(args.baseline):
        sys.exit(f"baseline {args.baseline} does not exist")

    extra = ["--threshold", str(args.threshold)]
    if args.time is not None:
        extra += ["--time", str(args.time)]
    if args.samples is not None:
        extra += ["--samples", str(args.samples)]

    os.makedirs(args.out, exist_ok=True)
    failed = []
    regressions = []
    improvements = []
    for name in names:
        previous = None
        if args.baseline:
            previous = baseline_report(args.baseline, name, len(names) == 1)
            if not previous:
                print(f"no baseline report for {name}")
        data, report = run_project(name, args.out, args.env, args.cfg, previous, extra)
        if data is None:
            failed.append(name)
            continue
        for case, change in changes(data):
            row = (f"{name}/{case}", change)
            if change > args.threshold:
                regressions.append(row)
            elif change < -args.threshold:
                improvements.append(row)
        shown = os.path.relpath(report, ROOT)
        cases = data.get("benchmarks") or []
        print(f"PASS {project_dir(name)}: {len(cases)} case(s) in {data.get('time_sec', 0):.2f}s "
              f"-> {report if shown.startswith('..') else shown}")
        print_cases(data)

    if args.baseline:
        print(f"baseline {args.baseline}: {len(regressions)} regression(s), "
              f"{len(improvements)} improvement(s) beyond {args.threshold * 100:.1f}%")
        for case, change in regressions:
            print(f"REGRESSION {case} {change * 100:+.1f}%")
        for case, change in improvements:
            print(f"IMPROVEMENT {case} {change * 100:+.1f}%")

    if failed:
        return 1

    return 1 if regressions and args.fail_on_regression else 0


if __name__ == "__main__":
    sys.exit(main())
