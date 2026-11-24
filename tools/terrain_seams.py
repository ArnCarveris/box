#!/usr/bin/env python3
"""Measures sawtooth edges and discontinuities in the height of a terrain graph, deterministically.

  tools/terrain_seams.py etc/scenes/infinite_forest.flecs infinite_forest.land.height \
      --window=0,-4000,256,1 --window=-2000,-4500,1024,4

The scene is loaded headless with a probe next to it: per window a TextureGraph on DomainWorld of
the height node, read back to the CPU. Each window is x,z (its corner), its size in meters and
the meters between its texels. The heights do not depend on the camera or the level of detail of
the tiles, only on the graph.

Along every row and column the second difference d2 of the heights is taken at each texel. A
smooth surface, a crest or the edge of a cliff resolved by the texels gives a single large d2. A
sawtooth, a one texel spike or a step between two texels gives two large d2 of opposite sign at
adjacent texels: those pairs are counted as zigzags, and three alternating in a row as saw. Per
window it prints the zigzags, the saw, the share of the texels and the largest |d2|, and it exits
with 1 when any window has more saw than --max.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from box_client import App
from integration import ROOT

BINARY = os.path.join(ROOT, ".bake", "local_env", "build", "boxrun", "arm64-Darwin-debug",
    "boxrun")

BODY = """using flecs.script
using flecs.script.math
using box.texture
using box.transform

{probes}

async {{
  for f in 0..30 {{
    await next_frame()
  }}

{measures}
}}
"""

PROBE = """terrain_seams_{i} {{
  Graph: {{output: {height}, width: {n}, domain: DomainWorld, format: GraphFloat,
    window: {{x: {x}, z: {z}, width: {w}}}}}
  Readback: {{}}
}}

terrain_seams_{i}_result {{
  Position: {{-1, 0, 0}}
}}

terrain_seams_{i}_worst {{
  Position: {{0, 0, 0}}
}}
"""

MEASURE = """  while !terrain_seams_{i}[ReadbackStatus].ready {{
    await next_frame()
  }}

  mut zig_{i}: f64 = 0
  mut top_{i}: f64 = 0
  mut worst_{i}: f64 = 0
  mut saw_{i}: f64 = 0
  mut wx_{i}: f64 = 0
  mut wz_{i}: f64 = 0
  for a in 0..{n} {{
    mut prev_r: f64 = 0
    mut prev_c: f64 = 0
    mut prev2_r: f64 = 0
    mut prev2_c: f64 = 0
    for b in 1..{last} {{
      const ua = (a + 0.5) / {n}
      const r0 = box.texture.sample(terrain_seams_{i}, (b - 0.5) / {n}, ua)
      const r1 = box.texture.sample(terrain_seams_{i}, (b + 0.5) / {n}, ua)
      const r2 = box.texture.sample(terrain_seams_{i}, (b + 1.5) / {n}, ua)
      const c0 = box.texture.sample(terrain_seams_{i}, ua, (b - 0.5) / {n})
      const c1 = box.texture.sample(terrain_seams_{i}, ua, (b + 0.5) / {n})
      const c2 = box.texture.sample(terrain_seams_{i}, ua, (b + 1.5) / {n})
      const dr = r0 - 2 * r1 + r2
      const dc = c0 - 2 * c1 + c2
      if prev_r * dr < 0 && abs(prev_r) > {t} && abs(dr) > {t} {{
        zig_{i} = zig_{i} + 1
        if abs(dr) > worst_{i} {{
          worst_{i} = abs(dr)
          wx_{i} = {x} + b * {s}
          wz_{i} = {z} + a * {s}
        }}
      }}
      if prev_c * dc < 0 && abs(prev_c) > {t} && abs(dc) > {t} {{
        zig_{i} = zig_{i} + 1
        if abs(dc) > worst_{i} {{
          worst_{i} = abs(dc)
          wx_{i} = {x} + a * {s}
          wz_{i} = {z} + b * {s}
        }}
      }}
      if prev2_r * prev_r < 0 && prev_r * dr < 0 && abs(prev2_r) > {t} && abs(prev_r) > {t} &&
        abs(dr) > {t}
      {{
        saw_{i} = saw_{i} + 1
      }}
      if prev2_c * prev_c < 0 && prev_c * dc < 0 && abs(prev2_c) > {t} && abs(prev_c) > {t} &&
        abs(dc) > {t}
      {{
        saw_{i} = saw_{i} + 1
      }}
      top_{i} = max(top_{i}, max(abs(dr), abs(dc)))
      prev2_r = prev_r
      prev2_c = prev_c
      prev_r = dr
      prev_c = dc
    }}
  }}

  terrain_seams_{i}_worst[Position] = {{wx_{i}, wz_{i}, worst_{i}}}
  terrain_seams_{i}_result[Position] = {{zig_{i}, top_{i}, saw_{i}}}
"""


def window(text):
    x, z, size, spacing = (float(v) for v in text.split(","))
    return x, z, size, spacing


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("scene", help="scene file, such as etc/scenes/infinite_forest.flecs")
    parser.add_argument("height", help="path of the height node of the terrain graph")
    parser.add_argument("--window", action="append", type=window, required=True,
        help="x,z,size,spacing in meters; repeat for more windows")
    parser.add_argument("--threshold", type=float, default=3,
        help="meters of |d2| that both texels of a zigzag exceed, default 3")
    parser.add_argument("--max", type=int, default=0,
        help="saw texels a window may have before the run fails, default 0")
    parser.add_argument("--binary", default=BINARY, help="the boxrun binary")
    parser.add_argument("--timeout", type=float, default=600, help="seconds the run may take")
    args = parser.parse_args()

    scene = os.path.join(ROOT, args.scene) if not os.path.isabs(args.scene) else args.scene
    folder = os.path.dirname(scene)
    name = os.path.splitext(os.path.basename(scene))[0]
    tag = "terrain_seams_%d" % os.getpid()
    wrapper = os.path.join(folder, tag + ".flecs")
    body = os.path.join(folder, tag + "_body.flecs")

    probes, measures, sizes = [], [], []
    for i, (x, z, size, spacing) in enumerate(args.window):
        n = int(round(size / spacing)) + 1
        sizes.append(n)
        probes.append(PROBE.format(i=i, height=args.height, n=n, x=x, z=z, w=size))
        measures.append(MEASURE.format(i=i, n=n, last=n - 1, t=args.threshold, x=x, z=z,
            s=size / (n - 1)))

    with open(body, "w") as f:
        f.write(BODY.format(probes="\n".join(probes), measures="\n".join(measures)))
    with open(wrapper, "w") as f:
        f.write("include %s\ninclude %s_body\n" % (name, tag))

    failed = False
    try:
        with App(args.binary, [os.path.relpath(wrapper, ROOT)], timeout=int(args.timeout),
                prefix="terrain_seams") as app:
            deadline = time.time() + args.timeout
            results = [None] * len(args.window)
            while None in results and time.time() < deadline:
                time.sleep(1)
                for i in range(len(results)):
                    if results[i] is None:
                        p = app.get("terrain_seams_%d_result" % i, "box.transform.Position")
                        if p["x"] >= 0:
                            results[i] = p
                            p["worst"] = app.get("terrain_seams_%d_worst" % i,
                                "box.transform.Position")

            print("%-28s %8s %6s %8s %10s  %s" % ("window x,z,size,spacing", "zigzags", "saw",
                "share", "max |d2|", "worst zigzag x,z"))
            for (x, z, size, spacing), n, r in zip(args.window, sizes, results):
                label = "%g,%g,%g,%g" % (x, z, size, spacing)
                if r is None:
                    print("%-28s %8s" % (label, "timeout"))
                    failed = True
                    continue

                share = r["x"] / float(2 * n * (n - 2))
                w = r["worst"]
                where = "%.0f,%.0f (%.1f m)" % (w["x"], w["y"], w["z"]) if r["x"] else ""
                print("%-28s %8d %6d %7.3f%% %10.2f  %s" % (label, r["x"], r["z"], share * 100,
                    r["y"], where))
                failed = failed or r["z"] > args.max
            app.quit()
    finally:
        os.remove(wrapper)
        os.remove(body)

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
