#!/usr/bin/env python3
import os
import re

import numpy as np
from generate import band

HERE = os.path.dirname(os.path.abspath(__file__))
SCENE = os.path.dirname(HERE)

HALF = 128.0
TILE = 2.0
COUNT = int(HALF * 2 / TILE)

ST = {"ST_MAIN": 0, "ST_LANE": 1, "ST_ALLEY": 2, "ST_ROAD": 3, "ST_PATH": 4}
AR = {"AR_MEADOW": 0, "AR_FOREST": 1, "AR_HILLS": 2, "AR_FARM": 3, "AR_CLEARING": 4,
      "AR_ASH": 5, "AR_STONE": 6, "AR_YARD": 7, "AR_SQUARE": 8, "AR_COURT": 9,
      "AR_PASTURE": 10}

KINDS = [("grass", "."), ("meadow", ","), ("hills", "^"), ("forest", "%"), ("sand", "s"),
         ("dirt", ":"), ("ash", "*"), ("rubble", "+"), ("cobble", "#"), ("flags", "@")]
SYMBOL = dict(KINDS)

STREET_KIND = {0: "cobble", 1: "rubble", 2: "rubble", 3: "dirt", 4: "dirt"}
STREET_RANK = {0: 3, 1: 2, 2: 2, 3: 1, 4: 0}
AREA_KIND = {AR["AR_MEADOW"]: "meadow", AR["AR_FOREST"]: "forest", AR["AR_HILLS"]: "hills",
             AR["AR_FARM"]: "meadow", AR["AR_PASTURE"]: "meadow", AR["AR_CLEARING"]: "dirt",
             AR["AR_ASH"]: "ash", AR["AR_STONE"]: "flags", AR["AR_YARD"]: "dirt",
             AR["AR_SQUARE"]: "cobble", AR["AR_COURT"]: "cobble"}
BIG_AREAS = {AR["AR_MEADOW"], AR["AR_FOREST"], AR["AR_HILLS"], AR["AR_FARM"]}


def read(name):
    with open(os.path.join(SCENE, name)) as f:
        return f.read()


def consts():
    out = {}
    for m in re.finditer(r"export const (\w+): f32 = (-?[\d.]+)", read("geo.flecs")):
        out[m.group(1)] = float(m.group(2))
    return out


def value(token, env):
    token = token.strip()
    if token in env:
        return env[token]
    return float(token)


def streets():
    out = []
    for line in read("town.flecs").splitlines():
        m = re.search(r"MvStreet: \{w: ([\d.]+), kind: (-?\w+)\}; Path3D: \{start: \{(-?[\d.]+), 0, (-?[\d.]+)\}", line)
        if not m:
            continue
        kind = ST.get(m.group(2))
        if kind is None:
            continue
        pts = [(float(m.group(3)), float(m.group(4)))]
        pts += [(float(a), float(b)) for a, b in re.findall(r"end: \{(-?[\d.]+), 0, (-?[\d.]+)\}", line)]
        out.append((float(m.group(1)), kind, pts))
    return out


def areas():
    env = consts()
    block = read("town.flecs").split("MV_AREAS: MvArea[] = [")[1].split("]")[0]
    out = []
    for m in re.finditer(r"\{([^}]*)\}", block):
        f = m.group(1).split(",")
        out.append(tuple(value(t, env) for t in f[:5]) + (AR[f[5].strip()],))
    return out


def catmull_rom(pts):
    n = len(pts) - 1
    a = np.array(pts)
    out = [a[0]]
    for i in range(n):
        p0, p1, p2, p3 = a[max(i - 1, 0)], a[i], a[i + 1], a[min(i + 2, n)]
        c1 = p1 + (p2 - p0) / 6
        c2 = p2 - (p3 - p1) / 6
        for j in range(1, 17):
            t = j / 16
            u = 1 - t
            out.append(u * u * u * p1 + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * p2)
    return np.array(out)


def river():
    line = [l for l in read("town.flecs").splitlines() if "river { MvStreet" in l][0]
    m = re.search(r"start: \{(-?[\d.]+), 0, (-?[\d.]+)\}", line)
    pts = [(float(m.group(1)), float(m.group(2)))]
    pts += [(float(a), float(b)) for a, b in re.findall(r"end: \{(-?[\d.]+), 0, (-?[\d.]+)\}", line)]
    w = float(re.search(r"MvStreet: \{w: ([\d.]+)", line).group(1))
    return w, pts


def centers():
    c = -HALF + (np.arange(COUNT) + 0.5) * TILE
    z, x = np.meshgrid(c, c, indexing="ij")
    return x, z


def warp(x, z, amount):
    size = (COUNT, COUNT)
    wx = (band(*size, 0.5, 101, 2, 5) - 0.5) * amount
    wz = (band(*size, 0.5, 102, 2, 5) - 0.5) * amount
    return x + wx, z + wz


def curve_distance(x, z, pts):
    curve = catmull_rom(pts)
    dist = np.full(x.shape, 1e9)
    for a, b in zip(curve[:-1], curve[1:]):
        d = b - a
        t = np.clip(((x - a[0]) * d[0] + (z - a[1]) * d[1]) / max(d @ d, 1e-9), 0, 1)
        e = np.sqrt((x - a[0] - t * d[0]) ** 2 + (z - a[1] - t * d[1]) ** 2)
        np.minimum(dist, e, out=dist)
    return dist


def area_distance(x, z, ax, az, aw, ad, ar):
    if ar * 2 >= aw - 0.01:
        return np.sqrt((x - ax) ** 2 + (z - az) ** 2) - ar
    qx = np.abs(x - ax) - (aw / 2 - ar)
    qz = np.abs(z - az) - (ad / 2 - ar)
    return np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qz, 0) ** 2) + \
        np.minimum(np.maximum(qx, qz), 0) - ar


def main():
    env = consts()
    x, z = centers()
    wx, wz = warp(x, z, 8.0)
    kind = np.full(x.shape, "grass", dtype=object)

    for ax, az, aw, ad, ar, k in areas():
        big = k in BIG_AREAS
        d = area_distance(wx if big else x, wz if big else z, ax, az, aw, ad, ar)
        kind[d < (0 if big else 0.6)] = AREA_KIND[k]

    lake = np.sqrt((x - env["LAKE_X"]) ** 2 + (z - env["LAKE_Z"]) ** 2)
    kind[lake < env["LAKE_R"] + 2.6] = "sand"
    w, pts = river()
    kind[curve_distance(x, z, pts) < w / 2 + 1.8] = "sand"

    for w, k, pts in sorted(streets(), key=lambda s: -STREET_RANK[s[1]]):
        kind[curve_distance(x, z, pts) < w / 2 + 0.2] = STREET_KIND[k]

    rows = ["".join(SYMBOL[k] for k in row) for row in kind]
    kinds = ", ".join("{'%s', mv_tile_%s}" % (sym, name) for name, sym in KINDS)
    grid = "\n".join("      " + r for r in rows)
    with open(os.path.join(SCENE, "tiles.flecs"), "w") as f:
        f.write("using box.transform\nusing box.material\nusing box.terrain\nusing assets\n\n")
        f.write("mossvale {\n  tiles {\n")
        f.write("    Position: {0, 0, 0}\n    Rgb: {255, 255, 255}\n    PbrMaterial: {roughness: 0.95}\n    NoShadow\n")
        f.write("    TileTerrain: {size: %g, kinds: [%s], tiles: `\n" % (TILE, kinds))
        f.write(grid + "\n    `}\n  }\n}\n")


if __name__ == "__main__":
    main()
