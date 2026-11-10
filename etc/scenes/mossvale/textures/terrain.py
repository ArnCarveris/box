#!/usr/bin/env python3
import os
import re

import numpy as np
from PIL import Image

from generate import band, smooth

HERE = os.path.dirname(os.path.abspath(__file__))
SCENE = os.path.dirname(HERE)

X0, X1 = -64.0, 96.0
Z0, Z1 = -92.0, 94.0
TEXEL = 0.08

ROWS = int(round((X1 - X0) / TEXEL))
COLS = int(round((Z1 - Z0) / TEXEL))

ST = {"ST_MAIN": 0, "ST_LANE": 1, "ST_ALLEY": 2, "ST_ROAD": 3, "ST_PATH": 4}
AR = {"AR_MEADOW": 0, "AR_FOREST": 1, "AR_HILLS": 2, "AR_FARM": 3, "AR_CLEARING": 4,
      "AR_ASH": 5, "AR_STONE": 6, "AR_YARD": 7, "AR_SQUARE": 8, "AR_COURT": 9,
      "AR_PASTURE": 10}

DIRT, COBBLE, RUBBLE = 0, 1, 2
STREET_LAYER = {0: COBBLE, 1: RUBBLE, 2: RUBBLE, 3: DIRT, 4: DIRT}
AREA_LAYER = {AR["AR_YARD"]: DIRT, AR["AR_SQUARE"]: COBBLE, AR["AR_COURT"]: COBBLE,
              AR["AR_CLEARING"]: DIRT}

TINTS = {"dirt": (208, 178, 130), "cobble": (204, 194, 178), "rubble": (212, 200, 180)}
GRASS_TINT = (122, 172, 80)


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


def grid():
    x = X1 - (np.arange(ROWS) + 0.5) * TEXEL
    z = Z0 + (np.arange(COLS) + 0.5) * TEXEL
    return np.meshgrid(x, z, indexing="ij")


def warp(x, z):
    size = (ROWS, COLS)
    span = max(X1 - X0, Z1 - Z0)
    coarse = span / 5.0
    fine = span / 1.2
    wx = (band(*size, 0.5, 101, coarse * 0.5, coarse) - 0.5) * 1.6
    wz = (band(*size, 0.5, 102, coarse * 0.5, coarse) - 0.5) * 1.6
    wx += (band(*size, 0.5, 103, fine * 0.5, fine) - 0.5) * 0.6
    wz += (band(*size, 0.5, 104, fine * 0.5, fine) - 0.5) * 0.6
    return x + wx, z + wz


def segment_distance(dist, x, z, a, b, reach):
    lo = np.minimum(a, b) - reach
    hi = np.maximum(a, b) + reach
    r0 = max(0, int((X1 - hi[0]) / TEXEL))
    r1 = min(ROWS, int((X1 - lo[0]) / TEXEL) + 1)
    c0 = max(0, int((lo[1] - Z0) / TEXEL))
    c1 = min(COLS, int((hi[1] - Z0) / TEXEL) + 1)
    if r0 >= r1 or c0 >= c1:
        return
    px = x[r0:r1, c0:c1] - a[0]
    pz = z[r0:r1, c0:c1] - a[1]
    d = b - a
    t = np.clip((px * d[0] + pz * d[1]) / max(d @ d, 1e-9), 0, 1)
    e = np.sqrt((px - t * d[0]) ** 2 + (pz - t * d[1]) ** 2)
    np.minimum(dist[r0:r1, c0:c1], e, out=dist[r0:r1, c0:c1])


def coverage(d, edge):
    return smooth(edge, -edge, d)


def main():
    x, z = grid()
    wx, wz = warp(x, z)
    size = (ROWS, COLS)
    cover = [np.zeros(size) for _ in range(3)]
    crown = np.zeros(size)
    worn = np.zeros(size)

    for w, kind, pts in streets():
        curve = catmull_rom(pts)
        half = w * 0.53
        dist = np.full(size, 1e9)
        for a, b in zip(curve[:-1], curve[1:]):
            segment_distance(dist, wx, wz, a, b, half + 2)
        layer = STREET_LAYER[kind]
        cover[layer] = np.maximum(cover[layer], coverage(dist - half, 0.45))
        if layer == DIRT:
            worn = np.maximum(worn, coverage(dist - half, 0.45))
        if kind == ST["ST_ROAD"]:
            crown = np.maximum(crown, coverage(dist - 0.35, 0.25))

    for ax, az, aw, ad, ar, kind in areas():
        layer = AREA_LAYER.get(kind)
        if layer is None:
            continue
        if ar * 2 >= aw - 0.01:
            d = np.sqrt((wx - ax) ** 2 + (wz - az) ** 2) - ar
        else:
            qx = np.abs(wx - ax) - (aw / 2 - ar)
            qz = np.abs(wz - az) - (ad / 2 - ar)
            d = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qz, 0) ** 2) + \
                np.minimum(np.maximum(qx, qz), 0) - ar
        cover[layer] = np.maximum(cover[layer], coverage(d, 0.45))

    patches = smooth(0.7, 0.76, band(*size, 1.0, 105, 30, 120)) * worn
    crown = crown * smooth(0.3, 0.42, band(*size, 1.0, 106, 15, 60))
    cover[DIRT] = cover[DIRT] * (1 - crown) * (1 - patches)

    cobble = cover[COBBLE]
    rubble = cover[RUBBLE] * (1 - cobble)
    dirt = cover[DIRT] * (1 - cobble - rubble)
    grass = 1 - cobble - rubble - dirt
    mask = np.stack([grass, dirt, cobble, rubble], -1)
    Image.fromarray((np.clip(mask, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(
        os.path.join(HERE, "terrain_mask.png"), optimize=True)

    for name, tint in TINTS.items():
        src = np.asarray(Image.open(os.path.join(HERE, name + ".jpg")).convert("RGB"), float)
        out = src * np.array(tint) / 255
        Image.fromarray((out + 0.5).astype(np.uint8)).save(
            os.path.join(HERE, "terrain_" + name + ".jpg"), quality=88, subsampling=0)

    grass_px = np.asarray(Image.open(os.path.join(HERE, "grass.jpg")).convert("RGB"), float)
    color = grass_px.reshape(-1, 3).mean(0) * np.array(GRASS_TINT) / 255
    clear = np.zeros((4, 4, 4), np.uint8)
    clear[..., :3] = (color + 0.5).astype(np.uint8)
    Image.fromarray(clear, "RGBA").save(os.path.join(HERE, "terrain_clear.png"))


if __name__ == "__main__":
    main()
