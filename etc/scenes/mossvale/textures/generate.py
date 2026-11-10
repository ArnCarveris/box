#!/usr/bin/env python3
import os

import numpy as np
from PIL import Image

OUT = os.path.dirname(os.path.abspath(__file__))


def noise(h, w, beta, seed):
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(h)[:, None] * h
    fx = np.fft.rfftfreq(w)[None, :] * w
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    spectrum = np.fft.rfft2(rng.standard_normal((h, w))) / f ** beta
    spectrum[0, 0] = 0
    n = np.fft.irfft2(spectrum, s=(h, w))
    return (n - n.min()) / (n.max() - n.min())


def band(h, w, beta, seed, lo, hi):
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(h)[:, None] * h
    fx = np.fft.rfftfreq(w)[None, :] * w
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    keep = (f >= lo) & (f <= hi)
    spectrum = np.fft.rfft2(rng.standard_normal((h, w))) / f ** beta * keep
    n = np.fft.irfft2(spectrum, s=(h, w))
    return (n - n.min()) / (n.max() - n.min())


def stretch(a, sx, sy):
    h, w = a.shape
    img = Image.fromarray((a * 255).astype(np.uint8))
    small = img.resize((max(1, int(w / sx)), max(1, int(h / sy))), Image.BILINEAR)
    big = np.array(small.resize((w, h), Image.BICUBIC)) / 255.0
    return (big - big.min()) / (big.max() - big.min() + 1e-9)


def blur(a, radius):
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi * radius) ** 2 * (fx * fx + fy * fy))
    return np.fft.irfft2(np.fft.rfft2(a) * g, s=(h, w))


def normal_map(height, strength):
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * strength
    n = np.stack([-dx, dy, np.ones_like(height)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def rgb(*c):
    return np.array(c, dtype=float) / 255


def mix(a, b, t):
    t = np.clip(t, 0, 1)
    if np.ndim(t) == 2:
        t = t[..., None]
    return a * (1 - t) + b * t


def smooth(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def gray(v):
    return np.repeat(v[..., None], 3, -1)


def save(name, a, quality=88):
    a = np.clip(a, 0, 1)
    img = Image.fromarray((a * 255 + 0.5).astype(np.uint8))
    if a.ndim == 3 and a.shape[2] == 4:
        img.save(os.path.join(OUT, name + ".png"), optimize=True)
    else:
        img.save(os.path.join(OUT, name + ".jpg"), quality=quality, subsampling=0)


def worley(h, w, cells_y, cells_x, seed, jitter=0.9):
    rng = np.random.default_rng(seed)
    pts = rng.uniform(0.5 - jitter / 2, 0.5 + jitter / 2, (cells_y, cells_x, 2))
    ids = rng.random((cells_y, cells_x))
    ys = (np.arange(h) + 0.5) / h * cells_y
    xs = (np.arange(w) + 0.5) / w * cells_x
    Y, X = np.meshgrid(ys, xs, indexing="ij")
    cy = np.floor(Y).astype(int)
    cx = np.floor(X).astype(int)
    f1 = np.full((h, w), 9.0)
    f2 = np.full((h, w), 9.0)
    idv = np.zeros((h, w))
    ox = np.zeros((h, w))
    oy = np.zeros((h, w))
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ny = cy + dy
            nx = cx + dx
            py = pts[ny % cells_y, nx % cells_x, 0] + ny
            px = pts[ny % cells_y, nx % cells_x, 1] + nx
            d = np.sqrt((Y - py) ** 2 + (X - px) ** 2)
            closer = d < f1
            f2 = np.where(closer, f1, np.minimum(f2, d))
            idv = np.where(closer, ids[ny % cells_y, nx % cells_x], idv)
            ox = np.where(closer, X - px, ox)
            oy = np.where(closer, Y - py, oy)
            f1 = np.where(closer, d, f1)
    return f1, f2, idv, ox, oy


def grass():
    h = w = 1024
    big = noise(h, w, 1.9, 1)
    mid = noise(h, w, 1.3, 2)
    fine = noise(h, w, 0.6, 3)
    blades = stretch(noise(h, w, 0.3, 4), 1, 5)
    clover_f1, clover_f2, clover_id, _, _ = worley(h, w, 48, 48, 5)
    clover = smooth(0.42, 0.18, clover_f1) * smooth(0.55, 0.75, noise(h, w, 1.6, 6))
    warm = rgb(240, 234, 196)
    cool = rgb(212, 230, 214)
    base = mix(np.ones((h, w, 3)) * cool, warm, smooth(0.2, 0.8, big))
    short = stretch(noise(h, w, 0.2, 10), 1, 3)
    tone = 0.8 + (mid - 0.5) * 0.18 + (fine - 0.5) * 0.12 + (blades - 0.5) * 0.3 + (short - 0.5) * 0.22
    albedo = base * tone[..., None]
    albedo = mix(albedo, albedo * rgb(190, 230, 170) / 0.85, clover * 0.5)
    rng = np.random.default_rng(7)
    dots = np.zeros((h, w))
    for _ in range(140):
        y, x = rng.integers(0, h), rng.integers(0, w)
        r = rng.uniform(1.5, 3)
        yy = (np.arange(h) - y + h // 2) % h - h // 2
        xx = (np.arange(w) - x + w // 2) % w - w // 2
        sel = (np.abs(yy) < 5)[:, None] & (np.abs(xx) < 5)[None, :]
        d = np.sqrt(yy[:, None] ** 2 + xx[None, :] ** 2)
        dots = np.maximum(dots, np.where(sel, smooth(r, r * 0.4, d), 0))
    flowers = smooth(0.6, 0.8, noise(h, w, 1.5, 8))
    petal = mix(np.ones((h, w, 3)) * rgb(255, 250, 235), rgb(255, 230, 120), smooth(0.4, 0.6, noise(h, w, 1.0, 9)))
    albedo = mix(albedo, petal, dots * flowers)
    save("grass", albedo)
    height = blades * 0.6 + fine * 0.4 + clover * 0.5
    save("grass_normal", normal_map(height, 2.2))


def forest_floor():
    h = w = 1024
    f1, f2, cid, ox, oy = worley(h, w, 56, 56, 21, 0.95)
    leaf = smooth(0.48, 0.3, f1 * (1 + 0.5 * np.abs(np.sin(np.arctan2(oy, ox) * 2 + cid * 6))))
    hue = cid
    col = mix(np.ones((h, w, 3)) * rgb(214, 160, 92), rgb(150, 120, 84), smooth(0.3, 0.6, hue))
    col = mix(col, rgb(196, 112, 64), smooth(0.75, 0.9, hue))
    col = mix(col, rgb(150, 160, 96), smooth(0.92, 1.0, hue))
    soil = rgb(110, 92, 70) * (0.8 + noise(h, w, 1.0, 22)[..., None] * 0.4)
    moss = smooth(0.55, 0.75, noise(h, w, 1.7, 23))
    soil = mix(soil, rgb(120, 150, 86), moss)
    albedo = mix(soil, col * (0.8 + f1[..., None] * 0.4), leaf * smooth(0.25, 0.55, noise(h, w, 1.2, 24)) + leaf * 0.4)
    save("forest_floor", albedo)
    save("forest_floor_normal", normal_map(leaf * 0.8 + noise(h, w, 0.7, 25) * 0.3, 2.5))


def dirt():
    h = w = 512
    big = noise(h, w, 1.6, 31)
    fine = noise(h, w, 0.7, 32)
    f1, f2, cid, _, _ = worley(h, w, 26, 26, 33, 1.0)
    pebble = smooth(0.2, 0.08, f1 * (1 + noise(h, w, 0.8, 37) * 0.8)) * (cid > 0.82)
    small_f1, _, sid, _, _ = worley(h, w, 90, 90, 34, 1.0)
    grit = smooth(0.22, 0.08, small_f1) * (sid > 0.6)
    ruts = stretch(noise(h, w, 1.2, 38), 1, 8)
    tone = 0.82 + (big - 0.5) * 0.22 + (fine - 0.5) * 0.14 + (ruts - 0.5) * 0.1
    albedo = gray(tone) * rgb(255, 246, 232)
    albedo = mix(albedo, gray(0.7 + cid * 0.25) * rgb(240, 236, 228), pebble * 0.8)
    albedo = mix(albedo, gray(0.66 + sid * 0.3), grit * 0.35)
    tufts = smooth(0.72, 0.85, noise(h, w, 1.3, 35)) * stretch(noise(h, w, 0.4, 36), 1, 3)
    albedo = mix(albedo, rgb(150, 190, 110), smooth(0.4, 0.7, tufts) * 0.8)
    save("dirt", albedo)
    height = pebble * 0.8 + grit * 0.25 + fine * 0.3 + big * 0.3 + ruts * 0.3
    save("dirt_normal", normal_map(height, 3))


def cobble():
    h = w = 512
    f1, f2, cid, _, _ = worley(h, w, 9, 9, 41, 0.75)
    edge = f2 - f1
    stone = smooth(0.04, 0.2, edge)
    dome = np.sqrt(np.clip(stone, 0, 1)) * (1 - f1 * 0.6)
    n = noise(h, w, 0.8, 42)
    tone = 0.74 + (cid - 0.5) * 0.3 + (n - 0.5) * 0.2
    col = gray(tone) * mix(np.ones((h, w, 3)) * rgb(255, 248, 238), rgb(236, 240, 250), cid[..., None])
    joint = rgb(120, 130, 90) * (0.7 + noise(h, w, 1.2, 43)[..., None] * 0.5)
    albedo = mix(joint, col, stone)
    albedo = albedo * (0.8 + 0.2 * dome[..., None])
    save("cobble", albedo)
    save("cobble_normal", normal_map(dome * 1.4 + n * 0.12, 4))


def rubble():
    h = w = 512
    f1, f2, cid, ox, oy = worley(h, w, 7, 5, 51, 0.8)
    fx = f1 * 1.0
    edge = f2 - f1
    stone = smooth(0.05, 0.16, edge)
    dome = np.sqrt(np.clip(stone, 0, 1))
    n = noise(h, w, 0.9, 52)
    big = noise(h, w, 1.6, 53)
    tone = 0.72 + (cid - 0.5) * 0.34 + (n - 0.5) * 0.22
    tint = mix(np.ones((h, w, 3)) * rgb(255, 246, 230), rgb(226, 232, 240), smooth(0.3, 0.8, cid))
    albedo = mix(gray(0.42 + n * 0.1), gray(tone) * tint, stone)
    moss = smooth(0.6, 0.78, big + n * 0.15) * (1 - dome * 0.4)
    albedo = mix(albedo, rgb(140, 170, 90) * (0.7 + n[..., None] * 0.4), moss * 0.7)
    save("rubble", albedo)
    save("rubble_normal", normal_map(dome * 1.3 + n * 0.2, 4))


def ashlar():
    h = w = 512
    rng = np.random.default_rng(61)
    courses = 6
    mask = np.zeros((h, w))
    ids = np.zeros((h, w))
    ch = h / courses
    for c in range(courses):
        y0, y1 = int(c * ch), int((c + 1) * ch)
        per = rng.integers(3, 5)
        cuts = np.sort((np.arange(per) + rng.uniform(-0.25, 0.25, per)) / per + (c % 2) * 0.5 / per) % 1
        cuts = np.sort(cuts)
        px = (cuts * w).astype(int)
        for i in range(len(px)):
            x0, x1 = px[i], px[(i + 1) % len(px)]
            xs = np.arange(x0, x1 if x1 > x0 else x1 + w) % w
            ids[y0:y1, xs] = rng.random()
            dx = np.minimum(np.arange(len(xs)), len(xs) - 1 - np.arange(len(xs)))
            dy = np.minimum(np.arange(y1 - y0), y1 - y0 - 1 - np.arange(y1 - y0))
            d = np.minimum.outer(dy, dx)
            mask[np.ix_(np.arange(y0, y1), xs)] = smooth(1.5, 7, d)
    n = noise(h, w, 0.8, 62)
    big = noise(h, w, 1.5, 63)
    tone = 0.76 + ids * 0.2 + (n - 0.5) * 0.2 + (big - 0.5) * 0.14
    albedo = mix(gray(0.45 + n * 0.1), gray(tone) * rgb(255, 248, 236), mask)
    moss = smooth(0.66, 0.8, big + n * 0.1)
    albedo = mix(albedo, rgb(150, 170, 100) * (0.7 + n[..., None] * 0.4), moss * 0.5)
    save("ashlar", albedo)
    save("ashlar_normal", normal_map(np.sqrt(mask) * 1.2 + n * 0.25, 3.5))


def rock():
    h = w = 512
    big = noise(h, w, 1.5, 71)
    n = noise(h, w, 0.9, 72)
    cr = band(h, w, 0.5, 73, 4, 24)
    crack = smooth(0.025, 0.0, np.abs(cr - 0.5)) * smooth(0.62, 0.75, noise(h, w, 1.4, 74))
    strata = 0.5 + 0.5 * np.sin(np.linspace(0, 14 * np.pi, h)[:, None] + big * 6)
    tone = 0.74 + (big - 0.5) * 0.3 + (n - 0.5) * 0.2 + strata * 0.06
    albedo = gray(tone) * rgb(255, 250, 240)
    albedo = albedo * (1 - crack[..., None] * 0.35)
    lichen = smooth(0.7, 0.8, noise(h, w, 1.1, 75)) * smooth(0.5, 0.7, n)
    albedo = mix(albedo, rgb(214, 206, 130), lichen * 0.6)
    moss = smooth(0.66, 0.8, noise(h, w, 1.7, 76))
    albedo = mix(albedo, rgb(130, 160, 84) * (0.8 + n[..., None] * 0.3), moss * 0.55)
    save("rock", albedo)
    save("rock_normal", normal_map(big * 1.2 + n * 0.5 - crack * 0.5 + strata * 0.1, 3.5))


def plaster():
    h = w = 512
    n = noise(h, w, 1.1, 81)
    f = noise(h, w, 0.5, 82)
    trowel = stretch(noise(h, w, 0.9, 83), 3, 1)
    stain = smooth(0.62, 0.85, noise(h, w, 1.8, 84))
    y = np.linspace(0, 1, h)[:, None] * np.ones((1, w))
    damp = smooth(0.78, 1.0, y + (n - 0.5) * 0.15)
    tone = 0.9 + (n - 0.5) * 0.08 + (f - 0.5) * 0.05 + (trowel - 0.5) * 0.06
    albedo = gray(tone) * rgb(255, 252, 246)
    albedo = albedo * (1 - stain[..., None] * 0.07)
    albedo = mix(albedo, albedo * rgb(200, 206, 180), damp * 0.6)
    save("plaster", albedo)
    save("plaster_normal", normal_map(n * 0.9 + f * 0.15 + trowel * 0.4, 1.2))


def wood():
    h = w = 512
    boards = 4
    rng = np.random.default_rng(91)
    fine = noise(h, w, 0.8, 92)
    grain = stretch(fine, 24, 1)
    rings = 0.5 + 0.5 * np.sin(grain * 30)
    tone = np.ones((h, w))
    mask = np.ones((h, w))
    for b in range(boards):
        y0, y1 = int(b * h / boards), int((b + 1) * h / boards)
        tone[y0:y1] = 0.78 + rng.random() * 0.2
        d = np.minimum(np.arange(y1 - y0), y1 - y0 - 1 - np.arange(y1 - y0))[:, None]
        mask[y0:y1] = smooth(1, 5, d)
    knots_f1, _, kid, _, _ = worley(h, w, 4, 6, 93)
    knot = smooth(0.12, 0.02, knots_f1 * np.where(kid > 0.75, 1, 9))
    v = tone * (0.74 + rings * 0.14 + grain * 0.14) * (0.25 + 0.75 * mask) * (1 - knot * 0.4)
    save("wood", gray(v) * rgb(255, 244, 230))
    save("wood_normal", normal_map(mask * 1.2 + rings * 0.08 + grain * 0.2 - knot * 0.3, 3))


def timber():
    h = w = 512
    fine = noise(h, w, 0.7, 101)
    grain = stretch(fine, 30, 1)
    adze = stretch(noise(h, w, 1.2, 102), 4, 1.5)
    checks = smooth(0.05, 0.0, np.abs(stretch(noise(h, w, 0.6, 103), 40, 1) - 0.5)) * smooth(0.5, 0.7, noise(h, w, 1.2, 104))
    v = (0.8 + (grain - 0.5) * 0.3 + (adze - 0.5) * 0.14) * (1 - checks * 0.55)
    save("timber", gray(v) * rgb(255, 240, 224))
    save("timber_normal", normal_map(grain * 0.4 + adze * 0.9 - checks * 0.7, 3))


def shingles():
    h = w = 512
    rows = 8
    per = 6
    rng = np.random.default_rng(111)
    Y = (np.arange(h) + 0.5) / h * rows
    X = (np.arange(w) + 0.5) / w * per
    Y, X = np.meshgrid(Y, X, indexing="ij")
    r = np.floor(Y).astype(int)
    v = Y - r
    shift = (r % 2) * 0.5
    tx = X + shift
    col = np.floor(tx).astype(int)
    t = tx - col
    prev_shift = ((r - 1) % 2) * 0.5
    ptx = X + prev_shift
    pcol = np.floor(ptx).astype(int)
    pt = ptx - pcol
    lip = 0.26 * np.sqrt(np.clip(1 - (2 * pt - 1) ** 2, 0, 1))
    covered = v < lip
    ids = rng.random((rows + 1, per + 1))
    tid = np.where(covered, ids[(r - 1) % rows, pcol % per], ids[r % rows, col % per])
    local = np.where(covered, 1 + v, v)
    edge_t = np.where(covered, pt, t)
    side = smooth(0.0, 0.05, edge_t) * smooth(1.0, 0.95, edge_t)
    shade = np.where(covered, 1.0, smooth(0.0, 0.18, v - lip))
    n = noise(h, w, 0.8, 112)
    tone = (0.72 + tid * 0.26 + (n - 0.5) * 0.16) * (0.55 + 0.45 * shade) * (0.6 + 0.4 * side)
    tone = tone * (0.9 + 0.1 * np.clip(local, 0, 1.2))
    big = noise(h, w, 1.6, 113)
    albedo = gray(tone) * rgb(255, 248, 240)
    moss = smooth(0.7, 0.84, big + n * 0.1)
    albedo = mix(albedo, rgb(150, 176, 96) * (0.6 + n[..., None] * 0.4), moss * 0.6)
    height = local * 0.6 + n * 0.12 + side * 0.15
    save("shingles", albedo)
    save("shingles_normal", normal_map(height, 3.5))


def thatch():
    h = w = 512
    rows = 5
    strands = stretch(noise(h, w, 0.4, 121), 1, 22)
    strands2 = stretch(noise(h, w, 0.5, 122), 1, 10)
    Y = np.linspace(0, rows, h, endpoint=False)[:, None] * np.ones((1, w))
    wob = (stretch(noise(h, w, 1.0, 123), 4, 1) - 0.5) * 0.25
    v = (Y + wob) % 1
    layer = smooth(0.0, 0.25, v)
    tone = (0.74 + (strands - 0.5) * 0.45 + (strands2 - 0.5) * 0.2) * (0.55 + 0.45 * layer)
    big = noise(h, w, 1.6, 124)
    albedo = gray(tone) * rgb(255, 246, 226)
    albedo = mix(albedo, albedo * rgb(170, 180, 140), smooth(0.6, 0.8, big) * 0.5)
    save("thatch", albedo)
    save("thatch_normal", normal_map(strands * 0.8 + v * 0.7 + strands2 * 0.3, 3))


def bark():
    h = w = 512
    n = noise(h, w, 0.8, 131)
    ridges = stretch(n, 1.2, 30)
    groove = smooth(0.35, 0.65, ridges)
    plates = stretch(noise(h, w, 1.0, 132), 1, 6)
    tone = (0.62 + groove * 0.38) * (0.9 + plates * 0.15)
    moss = smooth(0.62, 0.8, noise(h, w, 1.6, 133)) * (1 - groove * 0.5)
    albedo = gray(tone) * rgb(255, 244, 232)
    albedo = mix(albedo, rgb(160, 190, 110), moss * 0.5)
    save("bark", albedo)
    save("bark_normal", normal_map(groove * 1.6 + plates * 0.4 + n * 0.2, 4))


def birch():
    h = w = 512
    n = noise(h, w, 1.0, 141)
    marks = stretch(noise(h, w, 0.6, 142), 8, 0.6)
    lent = smooth(0.78, 0.86, marks) * smooth(0.4, 0.6, noise(h, w, 1.2, 143))
    rng = np.random.default_rng(144)
    big = np.zeros((h, w))
    for _ in range(9):
        y = rng.integers(0, h)
        x = rng.integers(0, w)
        ry = rng.uniform(3, 9)
        rx = rng.uniform(20, 60)
        yy = (np.arange(h) - y + h // 2) % h - h // 2
        xx = (np.arange(w) - x + w // 2) % w - w // 2
        d = (yy[:, None] / ry) ** 2 + (xx[None, :] / rx) ** 2
        big = np.maximum(big, smooth(1.0, 0.5, d + (n - 0.5) * 0.8))
    tone = 0.92 + (n - 0.5) * 0.1
    albedo = gray(tone) * rgb(255, 252, 246)
    albedo = mix(albedo, np.ones((h, w, 3)) * 0.18, np.maximum(lent, big))
    save("birch", albedo)
    save("birch_normal", normal_map(n * 0.4 - lent * 0.4 - big * 0.6, 2))


def foliage():
    h = w = 512
    f1, f2, cid, ox, oy = worley(h, w, 14, 14, 151, 0.95)
    ang = np.arctan2(oy, ox) + cid * 6.283
    leaf_shape = f1 * (1 + 0.35 * np.cos(ang * 2))
    leaf = smooth(0.5, 0.2, leaf_shape)
    g1, g2, gid, gox, goy = worley(h, w, 24, 24, 152, 0.95)
    leaf2 = smooth(0.5, 0.22, g1 * (1 + 0.35 * np.cos((np.arctan2(goy, gox) + gid * 6.283) * 2)))
    top = np.maximum(leaf, leaf2 * 0.85)
    light = np.clip(0.5 - (oy + ox) * 0.6, 0, 1)
    vein = smooth(0.03, 0.0, np.abs(oy * np.cos(cid * 6.283) - ox * np.sin(cid * 6.283))) * leaf
    big = noise(h, w, 1.5, 153)
    hue = mix(np.ones((h, w, 3)) * rgb(214, 236, 190), rgb(246, 236, 176), cid[..., None] * 0.6 + big[..., None] * 0.3)
    tone = 0.45 + top * 0.45 + light * leaf * 0.15 - vein * 0.08
    albedo = hue * tone[..., None]
    save("foliage", albedo)
    save("foliage_normal", normal_map(top * 1.4 + leaf * 0.4, 3.5))


def water():
    h = w = 512
    a = band(h, w, 0.6, 161, 3, 16)
    b = band(h, w, 0.4, 162, 10, 40)
    height = a * 0.8 + b * 0.35
    save("water_normal", normal_map(height, 2.4))


def cloth():
    h = w = 256
    x = np.arange(w)[None, :] * np.ones((h, 1))
    y = np.arange(h)[:, None] * np.ones((1, w))
    warp = 0.5 + 0.5 * np.sin(x / w * 2 * np.pi * 48)
    weft = 0.5 + 0.5 * np.sin(y / h * 2 * np.pi * 48)
    checker = ((np.floor(x / w * 48) + np.floor(y / h * 48)) % 2)
    weave = np.where(checker > 0, warp, weft)
    n = noise(h, w, 1.2, 171)
    folds = stretch(noise(h, w, 1.4, 172), 1, 3)
    tone = 0.86 + (weave - 0.5) * 0.05 + (n - 0.5) * 0.08 + (folds - 0.5) * 0.08
    save("cloth", gray(tone))
    save("cloth_normal", normal_map(weave * 0.15 + n * 0.3 + folds * 0.8, 1.5))


def sand():
    h = w = 512
    n = noise(h, w, 0.5, 181)
    big = noise(h, w, 1.6, 182)
    ripple = 0.5 + 0.5 * np.sin(np.linspace(0, 22 * np.pi, w)[None, :] + big * 8)
    tone = 0.84 + (n - 0.5) * 0.14 + (big - 0.5) * 0.14 + ripple * 0.04
    save("sand", gray(tone) * rgb(255, 250, 240))
    save("sand_normal", normal_map(ripple * 0.5 + n * 0.3, 2))


def wheat():
    h = w = 512
    stalks = stretch(noise(h, w, 0.3, 191), 1, 14)
    heads = smooth(0.6, 0.9, noise(h, w, 0.5, 192))
    tone = 0.7 + (stalks - 0.5) * 0.4 + heads * 0.2
    save("wheat", gray(tone) * rgb(255, 246, 220))
    save("wheat_normal", normal_map(stalks * 0.8 + heads * 0.5, 3))


if __name__ == "__main__":
    import sys
    for fn in (grass, forest_floor, dirt, cobble, rubble, ashlar, rock, plaster, wood, timber,
               shingles, thatch, bark, birch, foliage, water, cloth, sand, wheat):
        if len(sys.argv) > 1 and fn.__name__ not in sys.argv[1:]:
            continue
        fn()
        print(fn.__name__)
