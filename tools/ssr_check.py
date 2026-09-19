#!/usr/bin/env python3
"""Counts the isolated miss pixels of the screen space reflection scene.

A reflection ray that misses falls back to the prefiltered environment, which on
the glossy floor of test/integration/visual/ssr.flecs is the sky. A march whose
hit depends on where its steps happened to land turns single pixels into misses
while their neighbours hit, which reads as blue dithering over the reflections.
This renders the scene headless and counts the pixels inside the floor region
that are bluer than the median of the eight around them while at most one of
those eight is, which is what such a miss looks like and what the edge where a
reflection ends does not; the run fails when there are more than --limit.

Usage:
  ssr_check.py [--out dir] [--env name] [--frames n] [--size WxH] [--limit n]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import integration

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = os.path.join(ROOT, "test", "integration", "visual", "ssr.flecs")

# Fraction of the image the floor of the scene covers, left, top, right, bottom.
REGION = (0.08, 0.56, 0.92, 0.98)

# Levels a pixel has to beat the median of its neighbours by to count as a miss.
THRESHOLD = 10


def isolated(pixels, region, threshold):
    """Count the pixels of a region that keep the sky while their neighbours do not.

    A pixel counts when it is bluer than the median of the eight around it and at
    most one of those eight is too, which is what a single missed ray looks like
    and what the edge where a reflection ends does not.
    """
    import numpy as np
    height, width, _ = pixels.shape
    x0 = int(region[0] * width)
    y0 = int(region[1] * height)
    x1 = int(region[2] * width)
    y1 = int(region[3] * height)
    crop = pixels[y0:y1, x0:x1].astype(np.int16)
    offsets = [(dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dy or dx]
    taps = [np.roll(np.roll(crop, dy, 0), dx, 1) for dy, dx in offsets]
    median = np.median(np.stack(taps), axis=0)
    delta = crop - median
    # A miss keeps the sky: it is brighter in blue and darker in red than its neighbours.
    miss = (delta[:, :, 2] > threshold) & (delta[:, :, 0] < threshold)
    around = sum(np.roll(np.roll(miss, dy, 0), dx, 1).astype(np.int16) for dy, dx in offsets)
    alone = miss & (around <= 1)
    return int(alone[1:-1, 1:-1].sum()), (x1 - x0) * (y1 - y0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join(ROOT, "test", "integration", "out"))
    parser.add_argument("--env", default=None)
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--size", default="960x540")
    parser.add_argument("--limit", type=int, default=100)
    args = parser.parse_args()
    width, height = (int(v) for v in args.size.split("x"))
    os.makedirs(args.out, exist_ok=True)
    results = integration.run_visual([SCENE], args.out, args.env, args.frames, width,
                                     height)
    if results[0]["status"] != "pass" and not results[0].get("image"):
        print("FAIL ssr_check: the scene did not render")
        return 1

    png = os.path.join(args.out, "ssr.png")
    count, total = isolated(integration.read_png(png), REGION, THRESHOLD)
    ok = count <= args.limit
    print("%s ssr_check: %d isolated miss pixel(s) of %d in the floor, limit %d" %
          ("PASS" if ok else "FAIL", count, total, args.limit))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
