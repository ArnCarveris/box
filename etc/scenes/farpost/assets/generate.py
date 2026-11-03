#!/usr/bin/env python3
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))


def fbm(w, h, beta=2.0, seed=0):
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(h)[:, None] * h
    fx = np.fft.rfftfreq(w)[None, :] * w
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    amp = f ** (-beta / 2.0)
    amp[0, 0] = 0
    phase = rng.random((h, w // 2 + 1)) * 2 * np.pi
    out = np.fft.irfft2(amp * np.exp(1j * phase), s=(h, w))
    return (out - out.min()) / (out.max() - out.min())


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def stretch(a, wx, wy):
    h, w = a.shape
    img = Image.fromarray((a * 255).astype(np.uint8))
    return np.asarray(img.resize((wx, wy), Image.BILINEAR).resize((w, h), Image.BICUBIC), dtype=np.float32) / 255.0


def blur(a, r):
    img = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(r)), dtype=np.float32) / 255.0


def scratches(size, count, seed, length=(30, 240), fade=0.7):
    rng = np.random.default_rng(seed)
    img = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(img)
    for _ in range(count):
        x, y = rng.uniform(0, size, 2)
        a = rng.normal(math.pi / 2, 0.3) if rng.random() < 0.55 else rng.uniform(0, math.pi)
        n = rng.uniform(*length)
        d.line((x, y, x + math.cos(a) * n, y + math.sin(a) * n), fill=int(255 * rng.uniform(fade * 0.4, fade)), width=1)
    return np.asarray(img, dtype=np.float32) / 255.0


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.dstack((-dx, dy, np.ones_like(h)))
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n * 0.5 + 0.5


def save_rgb(name, rgb, quality=88):
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")
    path = os.path.join(OUT, name)
    if name.endswith(".jpg"):
        img.save(path, quality=quality, subsampling=0)
    else:
        img.save(path, optimize=True)


def save_rgba(name, rgba):
    Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(OUT, name), optimize=True)


def ramp(t, stops):
    xs = [s[0] for s in stops]
    out = np.zeros(t.shape + (3,), dtype=np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, xs, [s[1][c] / 255.0 for s in stops])
    return out


def iron():
    s = 1024
    grime, blotch, fine = fbm(s, s, 2.3, 1), fbm(s, s, 1.6, 2), fbm(s, s, 0.9, 3)
    chips = smooth(0.70, 0.73, fbm(s, s, 1.5, 4)) * smooth(0.42, 0.6, fbm(s, s, 2.8, 5))
    cuts = scratches(s, 320, 6, (8, 80), 0.55)
    rust = smooth(0.62, 0.86, fbm(s, s, 2.0, 7)) * smooth(0.45, 0.7, grime)
    run = smooth(0.55, 0.82, stretch(fbm(s, s, 1.8, 8), s // 16, s))
    bare = np.clip(chips + cuts * 0.8, 0, 1)
    paint = 0.62 + (blotch - 0.5) * 0.24 + (fine - 0.5) * 0.08
    dirt = smooth(0.46, 0.82, grime) * 0.36 + run * 0.26
    paint *= 1 - dirt
    alb = np.dstack((paint, paint * 0.98, paint * 0.95))
    alb = alb * (1 - bare[..., None]) + np.array([0.60, 0.58, 0.55]) * bare[..., None]
    alb = alb * (1 - rust[..., None] * 0.8) + np.array([0.30, 0.14, 0.07]) * rust[..., None] * 0.8
    save_rgb("iron_albedo.jpg", alb)
    rough = 0.70 + dirt * 0.22 - bare * 0.24 + rust * 0.2 + (fine - 0.5) * 0.14
    save_rgb("iron_orm.jpg", np.dstack((1 - dirt * 0.25, rough, chips * 0.8 + 0.05)), 90)
    height = -chips * 0.5 - cuts * 0.22 + grime * 0.4 + fine * 0.28 + rust * 0.3
    save_rgb("iron_normal.jpg", normal_from_height(height, 2.2), 92)


def plating():
    s = 1024
    grime, fine, blotch = fbm(s, s, 2.2, 11), fbm(s, s, 0.9, 12), fbm(s, s, 1.5, 13)
    rng = np.random.default_rng(16)
    panels = []

    def split(x0, y0, x1, y1, depth):
        w, h = x1 - x0, y1 - y0
        if depth >= 5 or (depth >= 3 and rng.random() < 0.3) or max(w, h) < 150:
            panels.append((x0, y0, x1, y1))
            return
        k = rng.uniform(0.32, 0.68)
        if w > h * 0.8:
            c = int(x0 + w * k)
            split(x0, y0, c, y1, depth + 1)
            split(c, y0, x1, y1, depth + 1)
        else:
            c = int(y0 + h * k)
            split(x0, y0, x1, c, depth + 1)
            split(x0, c, x1, y1, depth + 1)

    split(0, 0, s, s, 0)
    tone = np.ones((s, s), dtype=np.float32)
    hue = np.zeros((s, s, 3), dtype=np.float32)
    seam_img = Image.new("L", (s, s), 0)
    sd = ImageDraw.Draw(seam_img)
    riv_img = Image.new("L", (s, s), 0)
    rd = ImageDraw.Draw(riv_img)
    for x0, y0, x1, y1 in panels:
        k = 1 + rng.normal(0, 0.07)
        if rng.random() < 0.2:
            k *= rng.uniform(0.5, 0.8)
            hue[y0:y1, x0:x1] = rng.choice([[0.10, 0.02, -0.03], [-0.05, 0.02, 0.05], [0.09, 0.05, -0.06], [-0.04, -0.04, -0.04]])
        tone[y0:y1, x0:x1] = k
        sd.rectangle((x0, y0, x1 - 1, y1 - 1), outline=255)
        if rng.random() < 0.6:
            for rx in range(x0 + 12, x1 - 8, 30):
                rd.ellipse((rx - 2, y0 + 7, rx + 2, y0 + 11), fill=255)
                rd.ellipse((rx - 2, y1 - 11, rx + 2, y1 - 7), fill=255)
            for ry in range(y0 + 12, y1 - 8, 30):
                rd.ellipse((x0 + 7, ry - 2, x0 + 11, ry + 2), fill=255)
                rd.ellipse((x1 - 11, ry - 2, x1 - 7, ry + 2), fill=255)
    seam = np.asarray(seam_img, dtype=np.float32) / 255.0
    rivet = np.asarray(riv_img, dtype=np.float32) / 255.0
    halo = blur(seam, 7)
    run = stretch(halo, s // 8, s)
    chips = smooth(0.70, 0.74, fbm(s, s, 1.4, 14) + halo * 0.2)
    bare = np.clip(chips + scratches(s, 140, 15, (14, 110), 0.5), 0, 1)
    rust = smooth(0.58, 0.8, fbm(s, s, 2.0, 17) + run * 0.5)
    paint = (0.58 + (blotch - 0.5) * 0.22 + (fine - 0.5) * 0.08) * tone
    dirt = np.clip(smooth(0.5, 0.84, grime) * 0.3 + halo * 0.8 + run * 0.8, 0, 0.84)
    paint = paint * (1 - dirt)
    alb = np.dstack((paint, paint * 0.97, paint * 0.92)) + hue * (1 - dirt[..., None])
    alb = alb * (1 - bare[..., None]) + np.array([0.48, 0.46, 0.43]) * bare[..., None]
    alb = alb * (1 - rust[..., None] * 0.7) + np.array([0.27, 0.13, 0.07]) * rust[..., None] * 0.7
    alb *= 1 - seam[..., None] * 0.7
    save_rgb("plate_albedo.jpg", alb)
    rough = 0.68 + dirt * 0.24 - bare * 0.26 + rust * 0.2 + (fine - 0.5) * 0.14
    save_rgb("plate_orm.jpg", np.dstack((1 - seam * 0.7 - halo * 0.3, rough, chips * 0.8 + 0.08)), 90)
    height = -seam * 2.0 + rivet * 0.9 - chips * 0.4 + grime * 0.4 + fine * 0.2
    save_rgb("plate_normal.jpg", normal_from_height(height, 2.0), 92)


def deck():
    s = 1024
    y, x = np.mgrid[0:s, 0:s]
    cell = 64
    u, v = (x % cell) / cell - 0.5, (y % cell) / cell - 0.5
    tread = ((x // cell + y // cell) % 2).astype(np.float32)
    bar_a = np.exp(-((u + v) / 0.09) ** 2) * (np.abs(u - v) < 0.7)
    bar_b = np.exp(-((u - v) / 0.09) ** 2) * (np.abs(u + v) < 0.7)
    raised = np.where(tread > 0.5, bar_a, bar_b)
    grime, fine = fbm(s, s, 2.2, 21), fbm(s, s, 1.0, 22)
    wear = smooth(0.48, 0.74, fbm(s, s, 2.6, 23))
    base = 0.2 + (grime - 0.5) * 0.14 + (fine - 0.5) * 0.05
    alb = base + raised * (0.18 + wear * 0.28)
    save_rgb("deck_albedo.jpg", np.dstack((alb, alb * 0.97, alb * 0.93)))
    save_rgb("deck_orm.jpg", np.dstack((1 - (1 - raised) * 0.2, 0.62 - raised * wear * 0.34 + (fine - 0.5) * 0.14, 0.6 + raised * 0.35)), 90)
    save_rgb("deck_normal.jpg", normal_from_height(raised * 3 + fine * 0.3, 1.1), 92)


def wet_floor():
    s = 2048
    rng = np.random.default_rng(301)
    tiles = 6
    cell = s // tiles
    seam = np.zeros((s, s), dtype=np.float32)
    seam_img = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(seam_img)
    glow = np.zeros((s, s, 3), dtype=np.float32)
    pit = np.zeros((s, s), dtype=np.float32)
    for j in range(tiles):
        for i in range(tiles):
            x0, y0 = i * cell, j * cell
            d.rectangle((x0, y0, x0 + cell - 1, y0 + cell - 1), outline=255, width=5)
            d.rectangle((x0 + 14, y0 + 14, x0 + cell - 15, y0 + cell - 15), outline=130, width=2)
            if rng.random() < 0.34:
                pit[y0 + 26:y0 + cell - 26, x0 + 26:x0 + cell - 26] = 1
                for _ in range(90):
                    px, py = rng.integers(x0 + 34, x0 + cell - 34), rng.integers(y0 + 34, y0 + cell - 34)
                    c = rng.choice([[1.0, 0.62, 0.12], [1.0, 0.78, 0.3], [1.0, 0.46, 0.08], [0.95, 0.9, 0.7]])
                    sz = rng.integers(1, 4)
                    glow[py:py + sz * 2, px:px + sz] = np.array(c) * rng.uniform(0.4, 1)
    seam = np.asarray(seam_img, dtype=np.float32) / 255.0
    halo = blur(seam, 9)
    grime, fine, blotch = fbm(s, s, 2.3, 302), fbm(s, s, 1.0, 303), fbm(s, s, 1.7, 304)
    puddle = smooth(0.5, 0.58, fbm(s, s, 2.9, 305))
    rust = smooth(0.6, 0.82, fbm(s, s, 2.0, 306) + halo * 0.35)
    base = 0.17 + (blotch - 0.5) * 0.16 + (fine - 0.5) * 0.05
    alb = np.dstack((base * 1.02, base, base * 0.92))
    alb = alb * (1 - rust[..., None] * 0.7) + np.array([0.24, 0.12, 0.06]) * rust[..., None] * 0.7
    alb *= (1 - seam[..., None] * 0.75) * (1 - halo[..., None] * 0.5)
    alb = alb * (1 - pit[..., None]) + np.array([0.012, 0.012, 0.014]) * pit[..., None]
    alb *= 1 - smooth(0.45, 0.9, grime)[..., None] * 0.34
    save_rgb("floor_albedo.jpg", alb, 86)
    rough = 0.62 - puddle * 0.54 + (fine - 0.5) * 0.14 + rust * 0.2
    rough = rough * (1 - pit) + 0.06 * pit
    metal = 0.75 * (1 - puddle * 0.5) * (1 - pit)
    save_rgb("floor_orm.jpg", np.dstack((1 - halo * 0.5, np.clip(rough, 0.04, 1), metal)), 88)
    save_rgb("floor_normal.jpg", normal_from_height(-seam * 3 + fine * 0.3 * (1 - puddle) + grime * 0.3 * (1 - puddle) - pit * 2, 1.4), 86)
    save_rgb("floor_glow.jpg", blur_rgb(glow, 1.2) * 1.4 + glow * 0.5, 84)


def blur_rgb(a, r):
    return np.dstack([blur(a[..., c], r) for c in range(3)])


def stained():
    s = 1024
    grime, blotch, fine = fbm(s, s, 2.5, 311), fbm(s, s, 1.8, 312), fbm(s, s, 0.9, 313)
    run = smooth(0.46, 0.8, stretch(fbm(s, s, 1.7, 314), s // 20, s))
    chip = smooth(0.66, 0.72, fbm(s, s, 1.5, 315))
    rust = smooth(0.56, 0.78, fbm(s, s, 2.1, 316) + run * 0.2)
    base = 0.44 + (blotch - 0.5) * 0.3 + (fine - 0.5) * 0.1
    alb = np.dstack((base, base * 0.98, base * 0.9))
    alb *= 1 - smooth(0.4, 0.85, grime)[..., None] * 0.5 - run[..., None] * 0.34
    alb = alb * (1 - rust[..., None] * 0.82) + np.array([0.26, 0.13, 0.06]) * rust[..., None] * 0.82
    alb = alb * (1 - chip[..., None] * 0.5) + np.array([0.56, 0.54, 0.5]) * chip[..., None] * 0.5
    save_rgb("stain_albedo.jpg", alb, 86)
    save_rgb("stain_orm.jpg", np.dstack((1 - run * 0.3, 0.72 + rust * 0.2 - chip * 0.3 + (fine - 0.5) * 0.2, chip * 0.8 + 0.1)), 86)
    save_rgb("stain_normal.jpg", normal_from_height(grime * 0.6 + fine * 0.4 + rust * 0.5 - chip * 0.4, 2.4), 86)


def grate():
    s = 512
    y, x = np.mgrid[0:s, 0:s]
    bars = ((np.abs(((x + y) % 64) - 32) < 8) | (np.abs(((x - y) % 64) - 32) < 8)).astype(np.float32)
    dirt = fbm(s, s, 2.0, 25)
    col = 0.42 + 0.2 * dirt
    save_rgba("grate.png", np.dstack((col, col * 0.97, col * 0.92, bars)))


def rock():
    s = 1536
    big, mid, fine = fbm(s, s, 2.6, 31), fbm(s, s, 1.9, 32), fbm(s, s, 1.1, 33)
    rng = np.random.default_rng(34)
    pits = np.zeros((s, s), dtype=np.float32)
    y, x = np.mgrid[0:s, 0:s]
    for _ in range(110):
        cx, cy, r = rng.uniform(0, s), rng.uniform(0, s), rng.uniform(8, 56)
        dx = np.minimum(np.abs(x - cx), s - np.abs(x - cx))
        dy = np.minimum(np.abs(y - cy), s - np.abs(y - cy))
        d = np.sqrt(dx * dx + dy * dy) / r
        pits += np.where(d < 1.3, -np.cos(np.clip(d, 0, 1.3) * 2.4) * (1 - d / 1.3) * 0.6, 0).astype(np.float32)
    height = big * 1.6 + mid * 0.9 + fine * 0.28 - pits * 0.34
    t = np.clip(mid * 0.7 + big * 0.5 - pits * 0.1 - 0.1, 0, 1)
    alb = ramp(t, [(0, (21, 19, 21)), (0.35, (47, 42, 42)), (0.6, (80, 71, 66)), (0.85, (118, 104, 92)), (1, (150, 134, 118))])
    save_rgb("rock_albedo.jpg", alb, 84)
    save_rgb("rock_normal.jpg", normal_from_height(height, 6.0), 80)
    save_rgb("rock_orm.jpg", np.dstack((0.65 + 0.35 * smooth(0.25, 0.7, t), 0.82 + (fine - 0.5) * 0.2, np.full_like(t, 0.04))), 84)
    a = 1 - np.abs(fbm(s, s, 2.4, 35) * 2 - 1)
    b = 1 - np.abs(fbm(s, s, 2.1, 36) * 2 - 1)
    zone = smooth(0.52, 0.7, fbm(s, s, 2.8, 37)) * smooth(0.06, 0.2, np.linspace(0, 1, s)[:, None]) * smooth(0.06, 0.2, 1 - np.linspace(0, 1, s)[:, None])
    vein = (smooth(0.975, 0.998, a) + smooth(0.982, 0.999, b) * 0.8) * zone
    crystal = smooth(0.76, 0.82, fbm(s, s, 0.9, 38)) * blur(vein, 7) * 3
    glow = np.clip(vein + crystal, 0, 1)
    core = glow ** 2
    save_rgb("rock_veins.jpg", np.dstack((glow * 0.72 + core * 0.2, glow * 0.56 + core * 0.44, glow)), 84)


def sky():
    w, h = 4096, 2048
    n1, n2, n3, n4 = fbm(w, h, 3.2, 41), fbm(w, h, 2.9, 42), fbm(w, h, 3.3, 43), fbm(w, h, 2.7, 46)
    lat = np.linspace(-1, 1, h)[:, None]
    lon = np.linspace(-1, 1, w)[None, :]
    band = np.exp(-((lat + 0.1 + lon * 0.28) * 2.1 + (n3 - 0.5) * 1.6) ** 2)
    cloud = smooth(0.42, 0.9, n1) * band
    dust = smooth(0.46, 0.72, n2) * band
    hue = smooth(0.32, 0.72, n3)
    fil = smooth(0.84, 1.0, 1 - np.abs(n4 * 2 - 1)) * band
    r = cloud * (0.05 + hue * 0.30) * (1 - dust * 0.78) + fil * 0.03 + 0.002
    g = cloud * (0.12 + hue * 0.04) * (1 - dust * 0.82) + fil * 0.05 + 0.002
    b = cloud * (0.30 - hue * 0.04) * (1 - dust * 0.70) + fil * 0.08 + 0.005
    img = np.dstack((r, g, b)).astype(np.float32)
    img = img * img * 2.2
    rng = np.random.default_rng(44)
    density = 0.22 + band.mean(axis=1) * 0.78
    n = 42000
    ys = rng.integers(0, h, n)
    ys = ys[rng.random(n) < density[ys]]
    xs = rng.integers(0, w, ys.size)
    mag = rng.random(ys.size) ** 18
    tint = np.array([1, 1, 1]) * 0.65 + rng.random((ys.size, 3)) * 0.35
    for x, y, m, t in zip(xs, ys, mag, tint):
        val = 0.10 + m * 1.5
        if m < 0.55:
            img[y, x] += val * t
        else:
            for dy in range(-3, 4):
                for dx in range(-3, 4):
                    img[(y + dy) % h, (x + dx) % w] += val * math.exp(-(dx * dx + dy * dy) / 1.1) * t
    save_rgb("sky.jpg", np.sqrt(np.clip(img, 0, 1)), 88)


def environment():
    w, h = 512, 256
    data = bytearray(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (h, w))
    neb = fbm(w, h, 2.6, 81)
    for y in range(h):
        lat = (y + 0.5) / h * math.pi
        for x in range(w):
            lon = ((x + 0.5) / w - 0.5) * 2 * math.pi
            d = (math.sin(lat) * math.sin(lon), math.cos(lat), -math.sin(lat) * math.cos(lon))
            n = float(neb[y, x])
            base = 0.012 + n * n * 0.08
            sun = max(0.0, d[0] * -0.55 + d[1] * 0.3 - d[2] * 0.78) ** 8 * 2.4
            giant = max(0.0, d[0] * 0.62 - d[1] * 0.3 - d[2] * 0.72) ** 5 * 0.42
            c = (base * 0.8 + sun + giant * 1.0, base * 0.9 + sun * 0.8 + giant * 0.5, base * 1.5 + sun * 0.62 + giant * 0.72)
            m = max(c)
            mant, exp = math.frexp(m)
            k = mant * 256.0 / m
            data += bytes((int(c[0] * k), int(c[1] * k), int(c[2] * k), exp + 128))
    with open(os.path.join(OUT, "space.hdr"), "wb") as f:
        f.write(data)


def giant():
    w, h = 4096, 2048
    lat = np.linspace(-1, 1, h)[:, None]
    warp = fbm(w, h, 2.6, 51) - 0.5
    swirl = fbm(w, h, 1.8, 52) - 0.5
    fy = lat + warp * 0.12 + swirl * 0.022
    bands = 0.5 + 0.28 * np.sin(fy * 13 + 0.6) + 0.14 * np.sin(fy * 37 + 1.3) + 0.08 * np.sin(fy * 91) + swirl * 0.22
    col = ramp(np.clip(bands, 0, 1), [(0, (31, 18, 40)), (0.25, (70, 38, 66)), (0.45, (132, 74, 80)), (0.62, (190, 124, 92)), (0.8, (226, 184, 142)), (1, (246, 230, 202))])
    yy, xx = np.mgrid[0:h, 0:w]
    dx = np.minimum(np.abs(xx - w * 0.31), w - np.abs(xx - w * 0.31)) / (w * 0.042)
    dy = (yy - h * 0.62) / (h * 0.03)
    storm = np.exp(-(dx * dx + dy * dy))
    col = col * (1 - storm[..., None] * 0.86) + np.array([0.86, 0.34, 0.20]) * storm[..., None] * 0.86
    save_rgb("giant.jpg", col, 88)
    rs = 1024
    ry, rx = np.mgrid[0:rs, 0:rs] / (rs - 1.0)
    rr = np.sqrt((rx - 0.5) ** 2 + (ry - 0.5) ** 2) * 2
    density = np.interp(rr, [0, 0.54, 0.56, 0.64, 0.66, 0.69, 0.82, 0.835, 0.85, 0.97, 1.0], [0, 0, 0.3, 0.75, 0.06, 0.85, 0.95, 0.05, 0.55, 0.18, 0])
    grain = np.interp(rr, np.linspace(0, 1, 1024), fbm(1024, 8, 1.0, 57)[0])
    alpha = np.clip(density * (0.42 + grain * 0.9), 0, 1)
    save_rgba("rings.png", np.dstack((0.88 + grain * 0.08, 0.76 + grain * 0.1, 0.72 + grain * 0.14, alpha)))


def overlays():
    s = 512
    y, x = np.mgrid[0:s, 0:s] / (s - 1.0)
    r = np.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2) * 2
    ones = np.ones_like(r)
    save_rgba("halo.png", np.dstack((ones, ones, ones, np.clip(1 - r, 0, 1) ** 2.6)))
    limb = smooth(0.79, 0.835, r) * (1 - smooth(0.835, 1.0, r)) ** 2.2
    save_rgba("limb.png", np.dstack((ones, ones, ones, limb)))
    flare = np.exp(-(np.abs(y - 0.5) / 0.006)) * np.exp(-(np.abs(x - 0.5) / 0.2)) + np.clip(1 - r, 0, 1) ** 9
    save_rgba("flare.png", np.dstack((ones, ones, ones, np.clip(flare, 0, 1))))
    w, h = 1024, 256
    gy, gx = np.mgrid[0:h, 0:w] / np.array([h - 1.0, w - 1.0])[:, None, None]
    sheen = np.exp(-(((gx - 0.34 + (gy - 0.5) * 0.5) / 0.09) ** 2)) * 0.5 + np.exp(-(((gx - 0.46 + (gy - 0.5) * 0.5) / 0.022) ** 2)) * 0.32
    save_rgba("sheen.png", np.dstack((np.ones_like(sheen), np.ones_like(sheen), np.ones_like(sheen), sheen)))
    fan_w, fan_h = 256, 512
    fy, fx = np.mgrid[0:fan_h, 0:fan_w] / np.array([fan_h - 1.0, fan_w - 1.0])[:, None, None]
    spread = 0.04 + fy * 0.5
    fan = np.exp(-((fx - 0.5) / spread) ** 2) * fy ** 1.3 * (1 - smooth(0.92, 1, fy))
    rays = np.tile(fbm(fan_w, 8, 1.2, 61)[0:1, :], (fan_h, 1))
    save_rgba("fan.png", np.dstack((np.ones_like(fan), np.ones_like(fan), np.ones_like(fan), fan * (0.6 + rays * 0.6))))


def grime():
    s = 1024
    run = stretch(fbm(s, s, 1.9, 91), s // 10, s)
    soot = fbm(s, s, 2.4, 92)
    cuts = scratches(s, 110, 93, (30, 320), 1.0)
    u, v = np.meshgrid(np.linspace(0, 1, s), np.linspace(0, 1, s))
    border = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v))
    edge = 1 - smooth(0.0, 0.18, border + (soot - 0.5) * 0.14)
    alpha = edge * edge * (0.24 + smooth(0.4, 0.8, soot) * 0.5) + smooth(0.66, 0.94, soot) * 0.05 + smooth(0.72, 0.94, run) * 0.02 + cuts * 0.07 + 0.008
    save_rgba("window_grime.png", np.dstack((0.34 + cuts * 0.5, 0.32 + cuts * 0.55, 0.32 + cuts * 0.6, np.clip(alpha, 0, 1))))


def decals():
    s = 512
    y, x = np.mgrid[0:s, 0:s] / (s - 1.0)
    r = np.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2) * 2
    cloud, gr = fbm(s, s, 1.9, 101), fbm(s, s, 0.7, 102)
    a = smooth(0.3, 0.75, cloud + (1 - r) * 0.72 - 0.2) * (1 - smooth(0.62, 1.0, r))
    ones = np.ones_like(r)
    save_rgba("patch.png", np.dstack((ones, ones, ones, np.clip(a * (0.62 + gr * 0.76), 0, 1))))
    rn = stretch(fbm(s, s, 1.7, 103), s // 14, s)
    side = 1 - smooth(0.12, 0.5, np.abs(x - 0.5))
    run = smooth(0.0, 0.08, y) * (1 - smooth(0.25, 1.0, y))
    save_rgba("streak.png", np.dstack((ones, ones, ones, np.clip(smooth(0.42, 0.74, rn) * side * run * 1.2, 0, 1))))
    w, h = 512, 128
    img = Image.new("RGB", (w, h), (10, 9, 8))
    d = ImageDraw.Draw(img)
    for i in range(-h, w + h, 56):
        d.polygon([(i, h), (i + 28, h), (i + 28 + h, 0), (i + h, 0)], fill=(214, 164, 34))
    arr = np.asarray(img, dtype=np.float32) / 255.0
    wear = smooth(0.55, 0.76, fbm(w, h, 1.6, 104))[..., None]
    save_rgb("hazard.jpg", arr * (1 - wear * 0.6) + np.array([0.26, 0.24, 0.22]) * wear * 0.6)


def hull_lights():
    s = 1024
    rng = np.random.default_rng(111)
    img = np.zeros((s, s, 3), dtype=np.float32)
    bays = fbm(s, s, 2.4, 112)
    for row in range(0, s, 32):
        for col in range(0, s, 20):
            if bays[row, col] > 0.42 and rng.random() < 0.62:
                t = rng.choice([[1.0, 0.72, 0.38], [1.0, 0.82, 0.55], [0.72, 0.86, 1.0], [1.0, 0.6, 0.3]])
                k = rng.uniform(0.35, 1.0)
                img[row + 8:row + 22, col + 5:col + 15] = np.array(t) * k
    save_rgb("hull_lights.jpg", img, 86)


def rock_mesh(seed, rings=30, segs=44, lump=0.34):
    rng = np.random.default_rng(seed)
    count = 42
    dirs = rng.normal(size=(count, 3))
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    k = np.exp(rng.uniform(math.log(1.1), math.log(14.0), count))
    amps = lump * 0.42 / (k ** 1.05)
    phase = rng.uniform(0, 6.28, count)
    craters = rng.normal(size=(9, 3))
    craters /= np.linalg.norm(craters, axis=1, keepdims=True)
    crad = rng.uniform(0.14, 0.5, 9)
    stretch_v = rng.uniform(0.62, 1.2, 3)
    verts, uvs = [], []
    for j in range(rings + 1):
        v = j / rings
        lat = v * math.pi
        for i in range(segs + 1):
            u = i / segs
            lon = u * 2 * math.pi
            p = np.array([math.sin(lat) * math.cos(lon), math.cos(lat), math.sin(lat) * math.sin(lon)])
            d = 1.0
            d += float(np.sum(amps * np.abs(np.sin(k * (dirs @ p) + phase))) - np.sum(amps) * 0.6) * 2.0
            for a, r in zip(craters, crad):
                t = math.acos(max(-1, min(1, float(p @ a)))) / r
                if t < 1.25:
                    d -= 0.13 * r * (math.cos(t * 2.5) + 1) * (1 - t / 1.25) * 2.0
                    d += 0.05 * r * math.exp(-((t - 1) / 0.13) ** 2)
            verts.append(p * d * stretch_v)
            uvs.append((u * 2, v))
    verts = np.array(verts)
    idx = []
    for j in range(rings):
        for i in range(segs):
            a = j * (segs + 1) + i
            b = a + segs + 1
            idx += [a, a + 1, b, a + 1, b + 1, b]
    normals = np.zeros_like(verts)
    tri = np.array(idx).reshape(-1, 3)
    fn = np.cross(verts[tri[:, 1]] - verts[tri[:, 0]], verts[tri[:, 2]] - verts[tri[:, 0]])
    for c in range(3):
        np.add.at(normals, tri[:, c], fn)
    for j in range(rings + 1):
        a, b = j * (segs + 1), j * (segs + 1) + segs
        n = normals[a] + normals[b]
        normals[a] = normals[b] = n
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-6)
    if float(np.mean(np.sum(normals * verts, axis=1))) < 0:
        normals = -normals
        tri = tri[:, ::-1]
    return verts, normals, uvs, tri.flatten().tolist()


def rocks():
    lines = ["using box.geometry", ""]
    for n, (seed, rings, segs, lump) in enumerate(((201, 28, 40, 0.34), (202, 28, 40, 0.42), (203, 28, 40, 0.26), (204, 28, 40, 0.48), (205, 18, 26, 0.36), (206, 18, 26, 0.44))):
        v, nr, uv, idx = rock_mesh(seed, rings, segs, lump)
        f = lambda a: ", ".join("{%.3f, %.3f, %.3f}" % tuple(p) for p in a)
        lines.append("prefab RockMesh%d {" % n)
        lines.append("  Mesh3: {")
        lines.append("    vertices: [%s]," % f(v))
        lines.append("    normals: [%s]," % f(nr))
        lines.append("    uvs: [%s]," % ", ".join("{%.3f, %.3f}" % p for p in uv))
        lines.append("    indices: [%s]" % ", ".join(map(str, idx)))
        lines.append("  }")
        lines.append("}")
        lines.append("")
    with open(os.path.join(OUT, "..", "rocks.flecs"), "w") as f2:
        f2.write("\n".join(lines))


STEPS = {"iron": iron, "plating": plating, "deck": deck, "grate": grate, "wet_floor": wet_floor, "stained": stained, "rock": rock, "sky": sky, "environment": environment,
         "giant": giant, "overlays": overlays, "grime": grime, "decals": decals, "hull_lights": hull_lights, "rocks": rocks}

if __name__ == "__main__":
    for name in sys.argv[1:] or STEPS:
        STEPS[name]()
