#!/usr/bin/env python3
"""Runs the bench projects under bench/ and writes one JSON report per project.

Usage:
  bench.py [--out dir] [--env name] [--cfg release] [--baseline dir]
           [--time sec] [--samples n] [--fail-on-regression] [project ...]
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH_DIR = os.path.join(ROOT, "bench")
OUT_DIR = os.path.join(BENCH_DIR, "out")


def projects():
    if not os.path.isdir(BENCH_DIR):
        return []
    found = []
    for name in sorted(os.listdir(BENCH_DIR)):
        config = os.path.join(BENCH_DIR, name, "project.json")
        if not os.path.isfile(config):
            continue
        try:
            with open(config) as f:
                if "bench" in json.load(f):
                    found.append(name)
        except (OSError, ValueError):
            continue
    return found


def env_flag(name):
    return f"--local-env={name}" if name else "--local-env"


def run_project(name, out, env, cfg, baseline, extra):
    report = os.path.abspath(os.path.join(out, name + ".json"))
    args = ["--json", report, *extra]
    if baseline:
        previous = os.path.join(baseline, name + ".json")
        if os.path.isfile(previous):
            args += ["--baseline", os.path.abspath(previous)]
        else:
            print(f"no baseline report for {name}")
    proc = subprocess.run(
        ["bake3", "run", "bench/" + name, env_flag(env), "--cfg", cfg, "--", *args],
        cwd=ROOT, text=True, capture_output=True)
    if proc.returncode != 0 or not os.path.isfile(report):
        print(f"FAIL bench/{name}")
        print(proc.stdout[-4000:], proc.stderr[-4000:])
        return None
    with open(report) as f:
        return json.load(f)


def changes(data):
    result = []
    for case in data.get("benchmarks") or []:
        change = case.get("change")
        if isinstance(change, (int, float)):
            result.append((f"{case.get('suite')}.{case.get('case')}", change))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("projects", nargs="*")
    parser.add_argument("--out", default=OUT_DIR)
    parser.add_argument("--env")
    parser.add_argument("--cfg", default="release")
    parser.add_argument("--baseline")
    parser.add_argument("--time", type=float)
    parser.add_argument("--samples", type=int)
    parser.add_argument("--threshold", type=float, default=0.05)
    parser.add_argument("--fail-on-regression", action="store_true")
    args = parser.parse_args()

    names = args.projects or projects()
    if not names:
        sys.exit("no bench projects under bench/")
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
        data = run_project(name, args.out, args.env, args.cfg, args.baseline, extra)
        if data is None:
            failed.append(name)
            continue
        for case, change in changes(data):
            row = (f"{name}/{case}", change)
            if change > args.threshold:
                regressions.append(row)
            elif change < -args.threshold:
                improvements.append(row)
        cases = data.get("benchmarks") or []
        report = os.path.abspath(os.path.join(args.out, name + ".json"))
        shown = os.path.relpath(report, ROOT)
        print(f"PASS bench/{name}: {len(cases)} case(s) in {data.get('time_sec', 0):.2f}s "
              f"-> {report if shown.startswith('..') else shown}")

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
