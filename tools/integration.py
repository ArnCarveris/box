#!/usr/bin/env python3
"""Runs integration test scenes: visual scenes to screenshots, bench scenes to JSON reports.

Usage:
  integration.py visual [--out dir] [--env name] [--frames n] [scene.flecs ...]
  integration.py bench  [--out dir] [--env name] [--frames n] [scene.flecs ...]
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VISUAL_DIR = os.path.join(ROOT, "test", "integration", "visual")
BENCH_DIR = os.path.join(ROOT, "test", "integration", "bench")


def bake(args, cwd=None):
    return subprocess.run(["bake3", *args], cwd=cwd or ROOT, text=True, capture_output=True)


def env_flag(name):
    return f"--local-env={name}" if name else "--local-env"


def run_visual(scenes, out, env, frames, width, height):
    os.makedirs(out, exist_ok=True)
    results = []
    for scene in scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        png = os.path.abspath(os.path.join(out, name + ".png"))
        with tempfile.NamedTemporaryFile("w", suffix=".flecs", delete=False) as harness:
            harness.write("using box.surface\n$ { Surface: {width: %d, height: %d, write_to_file: \"%s\"} }\n"
                          % (width, height, png))
            harness_path = harness.name
        proc = bake(["run", "box_app", env_flag(env), "--cfg", "release", "--",
                     "--headless", "--frames", str(frames), harness_path, os.path.abspath(scene)])
        os.unlink(harness_path)
        ok = proc.returncode == 0 and os.path.isfile(png)
        results.append({"scene": os.path.relpath(scene, ROOT), "status": "pass" if ok else "fail",
                        "image": os.path.relpath(png, ROOT) if ok else None})
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)}")
        if not ok:
            print(proc.stdout[-2000:], proc.stderr[-2000:])
    return results


def run_bench(scenes, out, env, frames, extra):
    os.makedirs(out, exist_ok=True)
    results = []
    for scene in scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        report = os.path.abspath(os.path.join(out, name + ".json"))
        proc = bake(["run", "box_bench", env_flag(env), "--cfg", "release", "--",
                     os.path.abspath(scene), "--frames", str(frames), "--report", report, *extra])
        ok = proc.returncode == 0 and os.path.isfile(report)
        data = json.load(open(report)) if ok else {}
        results.append({"scene": os.path.relpath(scene, ROOT), "status": "pass" if ok else "fail", **data})
        summary = f"mean {data.get('mean_ms', 0):.2f} ms, p99 {data.get('p99_ms', 0):.2f} ms, {data.get('fps', 0):.0f} fps" if ok else ""
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)} {summary}")
        if not ok:
            print(proc.stdout[-2000:], proc.stderr[-2000:])
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", choices=["visual", "bench"])
    parser.add_argument("scenes", nargs="*")
    parser.add_argument("--out")
    parser.add_argument("--env")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--size", default="960x540")
    parser.add_argument("--churn", default="1-1000")
    parser.add_argument("--churn-color", action="store_true")
    args = parser.parse_args()

    if args.kind == "visual":
        scenes = args.scenes or sorted(glob.glob(os.path.join(VISUAL_DIR, "*.flecs")))
        width, height = (int(v) for v in args.size.split("x"))
        results = run_visual(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                             args.frames or 3, width, height)
    else:
        scenes = args.scenes or sorted(glob.glob(os.path.join(BENCH_DIR, "*.flecs")))
        extra = ["--churn", args.churn] + (["--churn-color"] if args.churn_color else [])
        results = run_bench(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                            args.frames or 300, extra)

    with open(os.path.join(args.out or os.path.join(ROOT, "test", "integration", "out"), f"{args.kind}.json"), "w") as f:
        json.dump({"kind": args.kind, "results": results}, f, indent=2)
    return 0 if all(r["status"] == "pass" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
