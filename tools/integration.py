#!/usr/bin/env python3
"""Runs integration test scenes: visual scenes to screenshots, bench scenes to JSON
reports and command scenes to session command logs.

Usage:
  integration.py visual  [--out dir] [--env name] [--frames n] [--update-baseline] [scene.flecs ...]
  Visual scenes are compared with test/integration/visual/baseline/<scene>.png and fail only when the
  largest channel delta exceeds 24 and more than 200 pixels differ by more than 16 (GPU speckle passes);
  a missing baseline is created, --update-baseline replaces the baselines of the scenes given.
  integration.py bench   [--out dir] [--env name] [--frames n] [scene.flecs ...]
  integration.py command [--out dir] [--env name] [--frames n] [scene.flecs ...]

A command scene holds a terminal named terminals.agent0 with a CommandScript that
points at the <scene>.cmd next to it. The scene runs headless with a generated
harness scene that sets Terminal.log, so the session log survives the process
folder; the log is written to <out>/<scene>.log, with the absolute path of the
repository stripped from it, and compared with the <scene>.expected next to the
scene when that file exists. A <scene>_content.flecs
next to a scene is loaded before it, which keeps scripts that commands patch out
of the scene that holds the terminal. The PNGs that render commands write to the
session resources are copied to <out>/<scene>_<image>.png while the scene runs,
because the process removes its folder when it exits.
"""
import argparse
import difflib
import glob
import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VISUAL_DIR = os.path.join(ROOT, "test", "integration", "visual")
BENCH_DIR = os.path.join(ROOT, "test", "integration", "bench")
COMMAND_DIR = os.path.join(ROOT, "test", "integration", "command")
HARNESS = "using box.command\n\nterminals {\n  agent0 {\n    Terminal: {log: \"%s\"}\n  }\n}\n"
DOTTED_ROOT = ROOT.replace(".", "\\.").replace("/", ".") + "."
PNG_END = b"IEND\xaeB`\x82"


def bake(args, cwd=None):
    return subprocess.run(["bake3", *args], cwd=cwd or ROOT, text=True, capture_output=True)


def env_flag(name):
    return f"--local-env={name}" if name else "--local-env"


BASELINE_DIR = os.path.join(VISUAL_DIR, "baseline")
MAX_DELTA = 24
PIXEL_DELTA = 16
MAX_PIXELS = 200


def read_png(path):
    """Decode an 8 bit RGB or RGBA PNG into an (h, w, 3) uint8 array without external libraries."""
    import struct
    import zlib
    import numpy as np
    with open(path, "rb") as f:
        data = f.read()
    pos = 8
    idat = b""
    width = height = channels = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            channels = {2: 3, 6: 4, 0: 1, 4: 2}[color]
            if depth != 8:
                raise ValueError("%s: unsupported bit depth %d" % (path, depth))
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = np.zeros((height, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.int32)
    offset = 0
    for y in range(height):
        kind = raw[offset]
        line = np.frombuffer(raw, dtype=np.uint8, count=stride, offset=offset + 1).astype(np.int32)
        offset += stride + 1
        if kind == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 255
        elif kind == 2:
            line = (line + prev) & 255
        elif kind == 3:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
        elif kind == 4:
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                b = prev[x]
                c = prev[x - channels] if x >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        rows[y] = line
        prev = line
    pixels = rows.reshape(height, width, channels)
    return pixels[:, :, :3] if channels >= 3 else np.repeat(pixels[:, :, :1], 3, axis=2)


def compare_images(baseline_path, actual_path):
    """Compare two PNGs the way flecs-engine did: fail only when the difference is strong and widespread."""
    import numpy as np
    base = read_png(baseline_path)
    actual = read_png(actual_path)
    if base.shape != actual.shape:
        return {"ok": False, "reason": "size %s != %s" % (base.shape[:2], actual.shape[:2])}
    diff = np.abs(base.astype(np.int16) - actual.astype(np.int16))
    max_delta = int(diff.max())
    mean_delta = float(diff.mean())
    npx = int((diff.max(axis=2) > PIXEL_DELTA).sum())
    ok = not (max_delta > MAX_DELTA and npx > MAX_PIXELS)
    return {"ok": ok, "max": max_delta, "mean": round(mean_delta, 4), "px": npx}


def compare_baseline(name, png, update):
    """Store or compare a render against test/integration/visual/baseline/<name>.png."""
    baseline = os.path.join(BASELINE_DIR, name + ".png")
    os.makedirs(BASELINE_DIR, exist_ok=True)
    if update or not os.path.isfile(baseline):
        status = "updated" if os.path.isfile(baseline) else "new"
        with open(png, "rb") as src, open(baseline, "wb") as dst:
            dst.write(src.read())
        return {"ok": True, "status": status}
    result = compare_images(baseline, png)
    result["status"] = "match" if result["ok"] else "differs"
    return result


def run_visual(scenes, out, env, frames, width, height, update=False):
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
        rendered = proc.returncode == 0 and os.path.isfile(png)
        cmp = compare_baseline(name, png, update) if rendered else {"ok": False, "status": "missing"}
        ok = rendered and cmp["ok"]
        detail = cmp["status"]
        if "max" in cmp:
            detail = "max %d mean %.4f px>%d %d %s" % (cmp["max"], cmp["mean"], PIXEL_DELTA, cmp["px"], cmp["status"])
        results.append({"scene": os.path.relpath(scene, ROOT), "status": "pass" if ok else "fail",
                        "baseline": cmp, "image": os.path.relpath(png, ROOT) if rendered else None})
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)} ({detail})")
        if not rendered:
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


def command_scripts(scene):
    content = os.path.splitext(scene)[0] + "_content.flecs"
    scripts = [content] if os.path.isfile(content) else []
    return [os.path.abspath(f) for f in scripts + [scene]]


def command_normalize(log):
    text = open(log).read()
    stripped = text.replace(DOTTED_ROOT, "").replace(ROOT + "/", "")
    if stripped != text:
        open(log, "w").write(stripped)


def command_diff(log, expected):
    actual = [line.rstrip() for line in open(log).read().splitlines()]
    want = [line.rstrip() for line in open(expected).read().splitlines()]
    return list(difflib.unified_diff(want, actual, os.path.basename(expected),
                                     os.path.basename(log), lineterm=""))


def command_images(out, name, start, seen):
    """Copy the images a scene rendered, which its process removes when it exits.

    A file is copied once it ends with the PNG end marker, because the renderer
    writes it while the scene keeps running.
    """
    for png in sorted(glob.glob("/tmp/box/*/*/resources/*.png")):
        if png in seen or os.path.getmtime(png) < start:
            continue
        with open(png, "rb") as f:
            data = f.read()
        if not data.endswith(PNG_END):
            continue
        seen.add(png)
        with open(os.path.join(out, f"{name}_{os.path.basename(png)}"), "wb") as f:
            f.write(data)


def run_command(scenes, out, env, frames):
    os.makedirs(out, exist_ok=True)
    results = []
    for scene in scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        log = os.path.abspath(os.path.join(out, name + ".log"))
        if os.path.exists(log):
            os.unlink(log)
        with tempfile.NamedTemporaryFile("w", suffix=".flecs", delete=False) as harness:
            harness.write(HARNESS % log)
            harness_path = harness.name
        seen, start = set(), time.time()
        with tempfile.TemporaryFile("w+") as output:
            proc = subprocess.Popen(["bake3", "run", "box_app", env_flag(env), "--cfg", "release", "--",
                                     "--headless", "--frames", str(frames), "--fps", "60", "--port", "0",
                                     *command_scripts(scene), harness_path],
                                    cwd=ROOT, text=True, stdout=output, stderr=subprocess.STDOUT)
            while proc.poll() is None:
                command_images(out, name, start, seen)
                time.sleep(0.01)
            command_images(out, name, start, seen)
            output.seek(0)
            printed = output.read()
        os.unlink(harness_path)
        ok = proc.returncode == 0 and os.path.isfile(log)
        if os.path.isfile(log):
            command_normalize(log)
        expected = os.path.join(os.path.dirname(scene), name + ".expected")
        diff = command_diff(log, expected) if ok and os.path.isfile(expected) else []
        ok = ok and not diff
        results.append({"scene": os.path.relpath(scene, ROOT), "status": "pass" if ok else "fail",
                        "log": os.path.relpath(log, ROOT) if os.path.isfile(log) else None,
                        "expected": os.path.relpath(expected, ROOT) if os.path.isfile(expected) else None,
                        "images": [f"{name}_{os.path.basename(p)}" for p in sorted(seen)]})
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)}")
        if diff:
            print("\n".join(diff))
        elif not ok:
            print(printed[-2000:])
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", choices=["visual", "bench", "command"])
    parser.add_argument("scenes", nargs="*")
    parser.add_argument("--out")
    parser.add_argument("--env")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--size", default="960x540")
    parser.add_argument("--churn", default="1-1000")
    parser.add_argument("--churn-color", action="store_true")
    parser.add_argument("--update-baseline", action="store_true")
    args = parser.parse_args()

    if args.kind == "visual":
        scenes = args.scenes or sorted(glob.glob(os.path.join(VISUAL_DIR, "*.flecs")))
        width, height = (int(v) for v in args.size.split("x"))
        results = run_visual(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                             args.frames or 3, width, height, update=args.update_baseline)
    elif args.kind == "command":
        scenes = args.scenes or sorted(s for s in glob.glob(os.path.join(COMMAND_DIR, "*.flecs"))
                                       if not s.endswith("_content.flecs"))
        results = run_command(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                              args.frames or 24)
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
