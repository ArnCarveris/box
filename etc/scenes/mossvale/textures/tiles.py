#!/usr/bin/env python3
import os

import numpy as np
from PIL import Image

from generate import noise, worley, normal_map, rgb, mix, smooth

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "tiles")
CELL = 128


def aniso(beta, seed, sx, sy, lo=1.0):
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(CELL)[:, None] * CELL
    fx = np.fft.rfftfreq(CELL)[None, :] * CELL
    f = np.sqrt((fx * sx) ** 2 + (fy * sy) ** 2)
    f[0, 0] = 1
    spectrum = np.fft.rfft2(rng.standard_normal((CELL, CELL))) / f ** beta
    spectrum[np.sqrt(fx * fx + fy * fy) < lo] = 0
    n = np.fft.irfft2(spectrum, s=(CELL, CELL))
    return (n - n.min()) / (n.max() - n.min())


def field(beta, seed, lo=1.0):
    return aniso(beta, seed, 1, 1, lo)


def decode(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def encode(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def tinted(albedo, tint):
    return encode(decode(np.clip(albedo, 0, 1)) * decode(rgb(*tint)))


def dots(seed, count, r0, r1):
    rng = np.random.default_rng(seed)
    out = np.zeros((CELL, CELL))
    idx = np.arange(CELL)
    for _ in range(count):
        y, x = rng.uniform(0, CELL, 2)
        r = rng.uniform(r0, r1)
        dy = (idx - y + CELL / 2) % CELL - CELL / 2
        dx = (idx - x + CELL / 2) % CELL - CELL / 2
        d = np.sqrt(dy[:, None] ** 2 + dx[None, :] ** 2)
        out = np.maximum(out, smooth(r, r * 0.4, d))
    return out


def grass_surface(seed, warm, cool, flowers):
    big = field(1.6, seed)
    fine = field(0.6, seed + 1)
    blades = aniso(0.4, seed + 2, 1, 4)
    short = aniso(0.3, seed + 3, 1, 2)
    f1, _, _, _, _ = worley(CELL, CELL, 10, 10, seed + 4)
    clover = smooth(0.42, 0.18, f1) * smooth(0.55, 0.75, field(1.4, seed + 5))
    base = mix(np.ones((CELL, CELL, 3)) * rgb(*cool), rgb(*warm), smooth(0.2, 0.8, big))
    tone = 0.8 + (fine - 0.5) * 0.14 + (blades - 0.5) * 0.34 + (short - 0.5) * 0.2
    albedo = base * tone[..., None]
    albedo = mix(albedo, albedo * rgb(190, 230, 170) / 0.85, clover * 0.5)
    spots = dots(seed + 6, flowers, 0.9, 1.6)
    petal = mix(np.ones((CELL, CELL, 3)) * rgb(255, 250, 235), rgb(255, 226, 110),
                smooth(0.4, 0.6, field(1.0, seed + 7)))
    albedo = mix(albedo, petal, spots)
    height = blades * 0.6 + fine * 0.4 + clover * 0.5 + spots * 0.3
    return albedo, height


def grass(seed):
    return grass_surface(seed, (240, 234, 196), (212, 230, 214), 5)


def meadow(seed):
    return grass_surface(seed, (246, 240, 200), (220, 236, 210), 12)


def hills(seed):
    return grass_surface(seed, (250, 236, 190), (226, 226, 200), 2)


def forest(seed):
    f1, _, cid, ox, oy = worley(CELL, CELL, 12, 12, seed, 0.95)
    leaf = smooth(0.48, 0.3, f1 * (1 + 0.5 * np.abs(np.sin(np.arctan2(oy, ox) * 2 + cid * 6))))
    col = mix(np.ones((CELL, CELL, 3)) * rgb(214, 160, 92), rgb(150, 120, 84), smooth(0.3, 0.6, cid))
    col = mix(col, rgb(196, 112, 64), smooth(0.75, 0.9, cid))
    col = mix(col, rgb(150, 160, 96), smooth(0.92, 1.0, cid))
    soil = rgb(110, 92, 70) * (0.8 + field(1.0, seed + 1)[..., None] * 0.4)
    moss = smooth(0.55, 0.75, field(1.5, seed + 2))
    soil = mix(soil, rgb(120, 150, 86), moss)
    cover = leaf * smooth(0.25, 0.55, field(1.2, seed + 3)) + leaf * 0.4
    albedo = mix(soil, col * (0.8 + f1[..., None] * 0.4), cover)
    return albedo, leaf * 0.8 + field(0.7, seed + 4) * 0.3


def dirt_surface(seed, grass_tufts):
    big = field(1.5, seed)
    fine = field(0.7, seed + 1)
    f1, _, cid, _, _ = worley(CELL, CELL, 7, 7, seed + 2, 1.0)
    pebble = smooth(0.2, 0.08, f1 * (1 + field(0.8, seed + 3) * 0.8)) * (cid > 0.8)
    s1, _, sid, _, _ = worley(CELL, CELL, 22, 22, seed + 4, 1.0)
    grit = smooth(0.22, 0.08, s1) * (sid > 0.6)
    ruts = aniso(1.1, seed + 5, 1, 4)
    tone = 0.82 + (big - 0.5) * 0.2 + (fine - 0.5) * 0.14 + (ruts - 0.5) * 0.1
    albedo = np.repeat(tone[..., None], 3, -1) * rgb(255, 246, 232)
    albedo = mix(albedo, np.repeat((0.7 + cid * 0.25)[..., None], 3, -1) * rgb(240, 236, 228),
                 pebble * 0.8)
    albedo = mix(albedo, np.repeat((0.66 + sid * 0.3)[..., None], 3, -1), grit * 0.35)
    tufts = smooth(0.74, 0.86, field(1.3, seed + 6)) * grass_tufts
    albedo = mix(albedo, rgb(150, 190, 110), tufts * 0.8)
    height = pebble * 0.8 + grit * 0.25 + fine * 0.3 + big * 0.3 + ruts * 0.3
    return albedo, height


def dirt(seed):
    return dirt_surface(seed, 1.0)


def ash(seed):
    albedo, height = dirt_surface(seed, 0.0)
    char = smooth(0.45, 0.8, field(0.9, seed + 20, lo=3.0))
    embers = dots(seed + 21, 3, 0.6, 1.0)
    albedo = mix(albedo, rgb(60, 54, 50), char * 0.45)
    albedo = mix(albedo, rgb(255, 150, 80), embers * 0.6)
    return albedo, height


def sand(seed):
    n = field(0.5, seed)
    big = field(1.5, seed + 1)
    x = np.arange(CELL)[None, :] / CELL
    ripple = 0.5 + 0.5 * np.sin(2 * np.pi * 5 * x + big * 6)
    tone = 0.84 + (n - 0.5) * 0.14 + (big - 0.5) * 0.12 + ripple * 0.05
    shells = dots(seed + 2, 2, 0.6, 1.0)
    albedo = np.repeat(tone[..., None], 3, -1) * rgb(255, 250, 240)
    albedo = mix(albedo, rgb(255, 255, 250), shells * 0.7)
    return albedo, ripple * 0.5 + n * 0.3


class Stones:
    def __init__(self, cols, rows, jitter, seed):
        self.cols = cols
        self.rows = rows
        self.f1, self.f2, self.cid, ox, oy = worley(CELL, CELL, rows, cols, seed, jitter)
        ys = (np.arange(CELL) + 0.5) / CELL
        xs = (np.arange(CELL) + 0.5) / CELL
        self.y, self.x = np.meshgrid(ys, xs, indexing="ij")
        self.px = self.x - ox / cols
        self.py = self.y - oy / rows
        self.edge = self.f2 - self.f1
        self.body = smooth(0.04, 0.16, self.edge)


def cobble(seed, stones):
    dome = np.sqrt(np.clip(stones.body, 0, 1)) * (1 - stones.f1 * 0.6)
    n = field(0.8, seed + 1)
    tone = 0.74 + (stones.cid - 0.5) * 0.3 + (n - 0.5) * 0.2
    col = np.repeat(tone[..., None], 3, -1) * mix(np.ones((CELL, CELL, 3)) * rgb(255, 248, 238),
                                                  rgb(236, 240, 250), stones.cid[..., None])
    joint = rgb(120, 130, 90) * (0.7 + field(1.2, seed + 2)[..., None] * 0.5)
    albedo = mix(joint, col, stones.body) * (0.8 + 0.2 * dome[..., None])
    return albedo, dome * 1.4 + n * 0.12


def rubble(seed, stones):
    dome = np.sqrt(np.clip(stones.body, 0, 1))
    n = field(0.9, seed + 1)
    big = field(1.5, seed + 2)
    tone = 0.72 + (stones.cid - 0.5) * 0.34 + (n - 0.5) * 0.22
    tint = mix(np.ones((CELL, CELL, 3)) * rgb(255, 246, 230), rgb(226, 232, 240),
               smooth(0.3, 0.8, stones.cid))
    albedo = mix(np.repeat((0.42 + n * 0.1)[..., None], 3, -1),
                 np.repeat(tone[..., None], 3, -1) * tint, stones.body)
    moss = smooth(0.62, 0.8, big + n * 0.15) * (1 - dome * 0.4)
    albedo = mix(albedo, rgb(140, 170, 90) * (0.7 + n[..., None] * 0.4), moss * 0.7)
    return albedo, dome * 1.3 + n * 0.2


def flags(seed, stones):
    flat = smooth(0.02, 0.06, stones.edge)
    n = field(0.8, seed + 1)
    crack = smooth(0.06, 0.0, np.abs(field(1.2, seed + 3) - 0.5)) * 0.6
    tone = 0.78 + (stones.cid - 0.5) * 0.22 + (n - 0.5) * 0.12 - crack * 0.15
    col = np.repeat(tone[..., None], 3, -1) * rgb(250, 246, 238)
    joint = rgb(96, 104, 80) * (0.7 + field(1.2, seed + 2)[..., None] * 0.4)
    albedo = mix(joint, col, flat)
    return albedo, flat * 0.8 + n * 0.15 - crack * 0.2


KINDS = [
    ("grass", grass, None, (122, 172, 80), {"amp": 0.62, "soft": 0.03, "rim": 1.12, "shadow": 0.18}),
    ("meadow", meadow, None, (138, 182, 88), {"amp": 0.62, "soft": 0.03, "rim": 1.12, "shadow": 0.16}),
    ("hills", hills, None, (160, 166, 96), {"amp": 0.6, "soft": 0.04, "rim": 1.1, "shadow": 0.16}),
    ("forest", forest, None, (176, 170, 140), {"amp": 0.6, "soft": 0.04, "rim": 0.85, "shadow": 0.22}),
    ("sand", sand, None, (236, 216, 166), {"amp": 0.62, "soft": 0.07, "rim": 1.08, "shadow": 0.08}),
    ("dirt", dirt, None, (208, 178, 130), {"amp": 0.6, "soft": 0.05, "rim": 0.86, "shadow": 0.2}),
    ("ash", ash, None, (120, 114, 106), {"amp": 0.55, "soft": 0.1, "rim": 0.7, "shadow": 0.25}),
    ("rubble", rubble, (5, 4, 0.8), (212, 200, 180), {"amp": 0.45, "soft": 0.04, "rim": 0.9, "shadow": 0.3}),
    ("cobble", cobble, (6, 6, 0.75), (204, 194, 178), {"amp": 0.4, "soft": 0.04, "rim": 0.92, "shadow": 0.32}),
    ("flags", flags, (4, 4, 0.5), (196, 188, 172), {"amp": 0.4, "soft": 0.04, "rim": 0.9, "shadow": 0.3}),
]


def corners(mask, u, v):
    tl = float(mask & 1)
    tr = float((mask >> 1) & 1)
    bl = float((mask >> 2) & 1)
    br = float((mask >> 3) & 1)
    return tl * (1 - u) * (1 - v) + tr * u * (1 - v) + bl * (1 - u) * v + br * u * v


def edge_noise(seed):
    low = field(1.6, seed, lo=1.0)
    mid = field(1.1, seed + 1, lo=3.0)
    high = field(0.5, seed + 2, lo=8.0)
    return (low * 0.5 + mid * 0.3 + high * 0.2 - 0.5) * 1.6


def sample(img, u, v):
    xi = np.clip((u * CELL).astype(int) % CELL, 0, CELL - 1)
    yi = np.clip((v * CELL).astype(int) % CELL, 0, CELL - 1)
    return img[yi, xi]


def snap(p, reach):
    return np.where(p < reach, 0.0, np.where(p > 1 - reach, 1.0, p))


def interior():
    t = (np.arange(CELL) + 0.5) / CELL
    d = np.minimum(np.minimum(t[:, None], 1 - t[:, None]), np.minimum(t[None, :], 1 - t[None, :]))
    return smooth(0.12, 0.3, d)


def variant(mask, surface, normal, noise_img, style, stones):
    ys = (np.arange(CELL) + 0.5) / CELL
    u = ys[None, :].repeat(CELL, 0)
    v = ys[:, None].repeat(CELL, 1)
    amp = style["amp"]
    f = corners(mask, u, v) + amp * 0.5 * noise_img
    if stones is not None:
        reach = 0.55 / max(stones.cols, stones.rows)
        su = snap(stones.px, reach)
        sv = snap(stones.py, reach)
        fs = corners(mask, su, sv) + amp * 0.5 * sample(noise_img, su, sv)
        whole = (fs > 0.5) * stones.body
        mortar = smooth(0.56, 0.72, f)
        cover = np.maximum(whole, mortar)
    else:
        cover = smooth(0.5 - style["soft"], 0.5 + style["soft"], f)
    if mask == 15:
        cover = np.ones_like(cover)
    rim = smooth(0.66, 0.5, f) * (mask != 15)
    color = surface * (1 + (style["rim"] - 1) * rim[..., None])
    shade = style["shadow"] * smooth(0.36, 0.5, f) * (1 - cover) * (mask != 15)
    alpha = cover + shade * (1 - cover)
    rgb_out = (color * cover[..., None] + rgb(20, 18, 12) * (shade * (1 - cover))[..., None])
    rgb_out = rgb_out / np.maximum(alpha, 1e-6)[..., None]
    flat = np.zeros_like(normal)
    flat[..., 2] = 1.0
    weight = (cover / np.maximum(alpha, 1e-6))[..., None]
    n = normal * 2 - 1
    n = n * weight + flat * (1 - weight)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.concatenate([rgb_out, alpha[..., None]], -1), n * 0.5 + 0.5


def build(index, name, fn, stone_desc, tint, style):
    seed = 1000 + index * 100
    stones = Stones(*stone_desc, seed + 50) if stone_desc else None
    args = (seed, stones) if stones else (seed,)
    albedo, height = fn(*args)
    surface = tinted(albedo, tint)
    strength = 2.2 if stones is None else 3.0
    normal = normal_map(height / max(height.max(), 1e-6), strength)
    noise_img = edge_noise(seed + 70)
    if index == 0:
        Image.fromarray((np.clip(surface, 0, 1) * 255 + 0.5).astype(np.uint8)).save(
            os.path.join(OUT, name + "_tile.jpg"), quality=90, subsampling=0)
        Image.fromarray((np.clip(normal, 0, 1) * 255 + 0.5).astype(np.uint8)).save(
            os.path.join(OUT, name + "_tile_normal.jpg"), quality=90, subsampling=0)
    atlas = np.zeros((4 * CELL, 8 * CELL, 4))
    atlas_n = np.zeros((4 * CELL, 8 * CELL, 3))
    keep = interior()[..., None]
    for i in range(32):
        col = (i % 4) + 4 * (i // 16)
        row = (i // 4) % 4
        surf, nrm = surface, normal
        if i >= 16:
            args = (seed + 7 * (i - 15), stones) if stones else (seed + 7 * (i - 15),)
            alt, alt_h = fn(*args)
            alt_n = normal_map(alt_h / max(alt_h.max(), 1e-6), strength)
            surf = mix(surface, tinted(alt, tint), keep)
            nrm = mix(normal, alt_n, keep)
        mask = 15 if i >= 15 else i
        a, n = variant(mask, surf, nrm, noise_img, style, stones)
        if i == 0:
            a = np.zeros_like(a)
            n = np.zeros_like(n) + np.array([0.5, 0.5, 1.0])
        atlas[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL] = a
        atlas_n[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL] = n
    Image.fromarray((np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(
        os.path.join(OUT, name + ".png"), optimize=True)
    Image.fromarray((np.clip(atlas_n, 0, 1) * 255 + 0.5).astype(np.uint8)).save(
        os.path.join(OUT, name + "_normal.jpg"), quality=90, subsampling=0)


def main():
    import sys
    os.makedirs(OUT, exist_ok=True)
    for index, (name, fn, stone_desc, tint, style) in enumerate(KINDS):
        if len(sys.argv) > 1 and name not in sys.argv[1:]:
            continue
        build(index, name, fn, stone_desc, tint, style)
        print(name)


if __name__ == "__main__":
    main()
