#!/usr/bin/env python3
"""Runs the deliverable pipeline of an epic into project/db/<epic>/deliverable.

Usage:
  epic.py <epic> [--baseline <epic>] [--env name] [--jobs n] [--steps a,b] [--skip a,b]

The steps in order, each writing into the deliverable folder of the epic:

  build_debug    bake3 build box_app, the debug build the unit tests link against,
                 reported with --build-json into build_debug.json
  build_release  the same build in release, into build_native.json
  tests          bake3 run test/<project> --json for every project under test/,
                 into tests_<project>.json and the merged tests.json
  visual         tools/integration.py visual into integration/
  bench_scenes   tools/integration.py bench into integration/
  command        tools/integration.py command into integration/
  bench          tools/bench.py into bench/, compared with the bench folder of the
                 baseline epic (the last completed epic that has one)
  wasm           bake3 build box_app --target em --cfg release into build_wasm.json,
                 with box_app.html, box_app.js and box_app.wasm copied into wasm/

It prints one summary line per step and exits non zero when a build, a test, a scene
or a report is missing or failed. Write deliverable/README.md yourself and close the
epic with project/pm.py done <epic>.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "project"))
import pm

DB = os.path.join(ROOT, "project", "db")
TEST_DIR = os.path.join(ROOT, "test")
TOOLS = os.path.join(ROOT, "tools")
STEPS = ("build_debug", "build_release", "tests", "visual", "bench_scenes", "command", "bench", "wasm")
BUILD_REPORTS = {"build_debug": "build_debug.json", "build_release": "build_native.json",
                 "wasm": "build_wasm.json"}
WASM_BIN = ("wasm32-Emscripten", "release", "bin")
WASM_FILES = ("box_app.html", "box_app.js", "box_app.wasm")
INTEGRATION_KINDS = {"visual": "visual", "bench_scenes": "bench", "command": "command"}
DEFAULT_ENV = "epic"
DEFAULT_JOBS = 8
THRESHOLD = 0.05


def run(cmd):
    """Run a command from the repository root, streaming its output.

    :param cmd: argument list.
    :return: (exit code, wall clock seconds).
    """
    start = time.time()
    code = subprocess.run(cmd, cwd=ROOT).returncode

    return code, time.time() - start


def step(name, ok, detail, seconds):
    """Build the result row of one step."""
    return {"step": name, "ok": bool(ok), "detail": detail, "seconds": seconds}


def test_projects():
    """List the test projects under test/, which are the directories with a project.json."""
    if not os.path.isdir(TEST_DIR):
        return []

    return [name for name in sorted(os.listdir(TEST_DIR))
            if os.path.isfile(os.path.join(TEST_DIR, name, "project.json"))]


def build_files(data):
    """Count the files a build report compiled."""
    projects = ((data.get("totals") or {}).get("project") or {}) if isinstance(data, dict) else {}

    return sum(pm.number(row.get("files")) for row in projects.values() if isinstance(row, dict))


def build_step(name, out, env, cfg, target, jobs):
    """Build box_app for one configuration and keep its bake3 build report."""
    report = os.path.join(out, BUILD_REPORTS[name])
    cmd = ["bake3", "build", "box_app", f"--local-env={env}", "--cfg", cfg,
           "-j", str(jobs), "--build-json", report]
    if target:
        cmd[3:3] = ["--target", target]
    code, seconds = run(cmd)
    data = pm.read_json(report)
    if not isinstance(data, dict):
        return step(name, False, f"no report at {BUILD_REPORTS[name]}", seconds)

    ok = code == 0 and data.get("ok") is True
    detail = "%d file(s), %.1fs of build in %s" % (
        build_files(data), pm.number(data.get("total_sec")), data.get("target") or cfg)

    return step(name, ok, detail, seconds)


def tests_step(out, env, jobs):
    """Run every test project with a JSON report and merge them into tests.json."""
    reports = []
    broken = []
    start = time.time()
    for name in test_projects():
        report = os.path.join(out, f"tests_{name}.json")
        code, _ = run(["bake3", "run", "test/" + name, f"--local-env={env}", "--",
                       "-j", str(jobs), "--json", report])
        data = pm.read_json(report)
        if not isinstance(data, dict) or not isinstance(data.get("tests"), list):
            broken.append(name)
            print(f"FAIL test/{name}: no usable report", flush=True)
            continue

        reports.append(data)
        if pm.number(data.get("fail")) or code != 0:
            broken.append(name)
    if not reports:
        return step("tests", False, "no test project produced a report", time.time() - start)

    merged = pm.merge_test_reports(reports)
    pm.write_json(os.path.join(out, "tests.json"), merged)
    detail = "%d passed, %d failed over %d project(s)" % (
        merged["pass"], merged["fail"], len(reports))
    if broken:
        detail += ": " + ", ".join(broken)

    return step("tests", not broken, detail, time.time() - start)


def integration_step(name, out, env):
    """Run one kind of integration scene into the integration folder of the deliverable."""
    kind = INTEGRATION_KINDS[name]
    target = os.path.join(out, "integration")
    code, seconds = run([sys.executable, os.path.join(TOOLS, "integration.py"), kind,
                         "--out", target, "--env", env])
    data = pm.read_json(os.path.join(target, kind + ".json"))
    results = (data or {}).get("results") if isinstance(data, dict) else None
    if not isinstance(results, list):
        return step(name, False, f"no {kind}.json report", seconds)

    failed = [r.get("scene") for r in results if r.get("status") != "pass"]
    detail = "%d of %d scene(s) passed" % (len(results) - len(failed), len(results))
    if failed:
        detail += ": " + ", ".join(os.path.basename(str(s)) for s in failed)

    return step(name, code == 0 and not failed, detail, seconds)


def epic_progress(name):
    """Read the progress.json of an epic."""
    data = pm.read_json(os.path.join(DB, name, "progress.json"))

    return data if isinstance(data, dict) else {}


def bench_baseline(epic, given):
    """Resolve the bench folder to compare against: the one given, or the last completed epic."""
    if given:
        folder = os.path.join(DB, given, "deliverable", "bench")
        if not os.path.isdir(folder):
            sys.exit(f"epic '{given}' has no deliverable/bench to compare against")

        return given, folder

    best = None
    for name in sorted(os.listdir(DB)):
        folder = os.path.join(DB, name, "deliverable", "bench")
        progress = epic_progress(name)
        if name == epic or progress.get("status") != "complete":
            continue
        if not glob.glob(os.path.join(folder, "*.json")):
            continue

        stamp = pm.time_value(progress.get("end_time") or progress.get("created") or "")
        if best is None or stamp > best[0]:
            best = (stamp, name, folder)

    return (best[1], best[2]) if best else (None, None)


def bench_changes(folder):
    """Collect the cases a bench run reported beyond the threshold against its baseline."""
    regressions = []
    improvements = []
    cases = 0
    for report in sorted(glob.glob(os.path.join(folder, "*.json"))):
        data = pm.read_json(report)
        rows = (data or {}).get("benchmarks") if isinstance(data, dict) else None
        for case in rows or []:
            cases += 1
            change = case.get("change")
            if not isinstance(change, (int, float)) or isinstance(change, bool):
                continue

            name = "%s/%s.%s" % (os.path.splitext(os.path.basename(report))[0],
                                 case.get("suite"), case.get("case"))
            if change > THRESHOLD:
                regressions.append((name, change))
            elif change < -THRESHOLD:
                improvements.append((name, change))

    return cases, regressions, improvements


def bench_step(out, env, baseline):
    """Run every bench project in release against the baseline of the previous epic."""
    folder = os.path.join(out, "bench")
    cmd = [sys.executable, os.path.join(TOOLS, "bench.py"), "--env", env, "--out", folder,
           "--threshold", str(THRESHOLD)]
    if baseline:
        cmd += ["--baseline", baseline]
    code, seconds = run(cmd)
    reports = sorted(glob.glob(os.path.join(folder, "*.json")))
    if not reports:
        return step("bench", False, "no benchmark report", seconds)

    cases, regressions, improvements = bench_changes(folder)
    detail = "%d case(s) over %d project(s), %d regression(s), %d improvement(s)" % (
        cases, len(reports), len(regressions), len(improvements))
    result = step("bench", code == 0, detail, seconds)
    result["regressions"] = regressions
    result["improvements"] = improvements

    return result


def wasm_step(out, env, jobs):
    """Build the wasm image with its build report and copy it into the deliverable."""
    result = build_step("wasm", out, env, "release", "em", jobs)
    if not result["ok"]:
        return result

    source = os.path.join(ROOT, ".bake", "local_env", env, *WASM_BIN)
    target = os.path.join(out, "wasm")
    os.makedirs(target, exist_ok=True)
    missing = []
    for name in WASM_FILES:
        image = os.path.join(source, name)
        if os.path.isfile(image):
            shutil.copyfile(image, os.path.join(target, name))
        else:
            missing.append(name)
    if missing:
        result["ok"] = False
        result["detail"] += ", missing " + ", ".join(missing)
    else:
        result["detail"] += ", %s copied into wasm/" % ", ".join(WASM_FILES)

    return result


def run_step(name, out, args, baseline):
    """Run one pipeline step and return its result row."""
    print(f"\n=== {name}", flush=True)
    if name in INTEGRATION_KINDS:
        return integration_step(name, out, args.env)
    if name == "build_debug":
        return build_step(name, out, args.env, "debug", None, args.jobs)
    if name == "build_release":
        return build_step(name, out, args.env, "release", None, args.jobs)
    if name == "tests":
        return tests_step(out, args.env, args.jobs)
    if name == "bench":
        return bench_step(out, args.env, baseline)

    return wasm_step(out, args.env, args.jobs)


def selected_steps(args):
    """Work out which steps to run from --steps and --skip."""
    names = [n.strip() for n in (args.steps or ",".join(STEPS)).split(",") if n.strip()]
    skipped = [n.strip() for n in (args.skip or "").split(",") if n.strip()]
    for name in names + skipped:
        if name not in STEPS:
            sys.exit(f"unknown step '{name}', pick from: " + ", ".join(STEPS))

    return [name for name in STEPS if name in names and name not in skipped]


def print_summary(epic, out, results):
    """Print the one screen summary of the run."""
    print("\n=== %s -> %s" % (epic, os.path.relpath(out, ROOT)))
    for result in results:
        print("%-4s %-14s %6.1fs  %s" % ("PASS" if result["ok"] else "FAIL", result["step"],
                                         result["seconds"], result["detail"]))
    for result in results:
        for case, change in result.get("regressions") or []:
            print("REGRESSION %s %+.1f%%" % (case, change * 100))
        for case, change in result.get("improvements") or []:
            print("IMPROVEMENT %s %+.1f%%" % (case, change * 100))
    failed = [r["step"] for r in results if not r["ok"]]
    total = sum(r["seconds"] for r in results)
    print("%s: %d step(s) in %.1f min, %d failed%s" % (
        epic, len(results), total / 60, len(failed),
        ": " + ", ".join(failed) if failed else ""))


def print_gaps(epic, directory):
    """Print what project/pm.py done still wants from the epic."""
    gaps = pm.deliverable_gaps(pm.walk_item(directory, 0, epic))
    for gap in gaps:
        print(f"{epic}: missing {gap}")
    if not gaps:
        print(f"{epic} is ready for: python3 project/pm.py done {epic}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("epic")
    parser.add_argument("--baseline", help="epic to compare the benchmarks with")
    parser.add_argument("--env", default=DEFAULT_ENV, help="private build environment")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    parser.add_argument("--steps", help="comma separated subset of: " + ", ".join(STEPS))
    parser.add_argument("--skip", help="comma separated steps to leave out")
    args = parser.parse_args()

    directory = os.path.join(DB, args.epic)
    if not os.path.isfile(os.path.join(directory, "progress.json")):
        sys.exit(f"'{args.epic}' is not an epic under project/db")

    out = os.path.join(directory, "deliverable")
    os.makedirs(out, exist_ok=True)
    names = selected_steps(args)
    baseline_epic, baseline = bench_baseline(args.epic, args.baseline)
    print("epic %s in env %s, %d step(s), benchmark baseline %s" % (
        args.epic, args.env, len(names), baseline_epic or "none"), flush=True)

    results = [run_step(name, out, args, baseline) for name in names]
    print_summary(args.epic, out, results)
    print_gaps(args.epic, directory)

    return 1 if any(not r["ok"] for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
