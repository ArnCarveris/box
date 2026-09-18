#!/usr/bin/env python3
"""Runs the fast camera check of the Hi-Z occlusion culling.

The camera of test/integration/visual/occlusion_sweep.flecs sweeps past a row of
walls at sixty units per second while the surface writes every frame, once with
occlusion culling on and once with it off. A headless run has a fixed frame
delta, so the two runs see the same camera path and every pair of frames must be
byte for byte identical; a difference is an object the occlusion test dropped.

Usage:
  occlusion_sweep.py [--out dir] [--env name] [--frames n] [--size WxH]
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = os.path.join(ROOT, "test", "integration", "visual", "occlusion_sweep.flecs")
HARNESS = ("using box.surface\n$ { Surface: {width: %d, height: %d, "
           "write_to_file: \"%s\", write_nth: 1} }\n")
OVERLAY = ("using box.view\n$ { CullSettings: {enabled: true, gpu_threshold: 2048, "
           "occlusion: false, hiz_levels: 0} }\n")


def write_temp(text, suffix=".flecs"):
    with tempfile.NamedTemporaryFile("w", suffix=suffix, delete=False) as f:
        f.write(text)
        return f.name


def run(out, env, frames, width, height, occlusion):
    name = "on" if occlusion else "off"
    harness = write_temp(HARNESS % (width, height, os.path.join(out, name + ".png")))
    scripts = [harness, SCENE] + ([] if occlusion else [write_temp(OVERLAY)])
    flag = f"--local-env={env}" if env else "--local-env"
    proc = subprocess.run(["bake3", "run", "box_app", flag, "--cfg", "release", "--",
                           "--headless", "--frames", str(frames), "--port", "0", *scripts],
                          cwd=ROOT, text=True, capture_output=True)
    for path in scripts:
        if path != SCENE:
            os.unlink(path)
    if proc.returncode:
        print(proc.stdout[-2000:], proc.stderr[-2000:])
    return proc.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=os.path.join(ROOT, "test", "integration", "out", "sweep"))
    parser.add_argument("--env")
    parser.add_argument("--frames", type=int, default=90)
    parser.add_argument("--size", default="640x360")
    args = parser.parse_args()
    width, height = (int(v) for v in args.size.split("x"))
    os.makedirs(args.out, exist_ok=True)
    for png in glob.glob(os.path.join(args.out, "*.png")):
        os.unlink(png)

    if not run(args.out, args.env, args.frames, width, height, True):
        return 1
    if not run(args.out, args.env, args.frames, width, height, False):
        return 1

    on = sorted(glob.glob(os.path.join(args.out, "on_*.png")))
    bad = []
    for image in on:
        other = image.replace("on_", "off_")
        if not os.path.isfile(other):
            bad.append(os.path.basename(image) + " has no twin")
            continue
        if open(image, "rb").read() != open(other, "rb").read():
            bad.append(os.path.basename(image) + " differs")

    print(f"{len(on)} frames compared, {len(bad)} differ")
    for line in bad:
        print(" ", line)
    return 0 if on and not bad else 1


if __name__ == "__main__":
    sys.exit(main())
