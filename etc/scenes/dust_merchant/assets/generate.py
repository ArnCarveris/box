#!/usr/bin/env python3
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))
RNG = np.random.default_rng(1977)


def fbm(w, h, beta=2.0, seed=0, lo=0.0):
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(h)[:, None] * h
    fx = np.fft.rfftfreq(w)[None, :] * w
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    amp = f ** (-beta / 2.0)
    if lo > 0:
        amp *= f > lo
    amp[0, 0] = 0
    phase = rng.random((h, w // 2 + 1)) * 2 * np.pi
    out = np.fft.irfft2(amp * np.exp(1j * phase), s=(h, w))
    return (out - out.min()) / (out.max() - out.min())


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def scratches(size, count, seed, length=(30, 240), width=(1, 2), fade=0.7):
    rng = random_state(seed)
    img = Image.new("L", (size * 3, size * 3), 0)
    d = ImageDraw.Draw(img)
    for _ in range(count):
        x = rng.uniform(size, size * 2)
        y = rng.uniform(size, size * 2)
        a = rng.uniform(0, math.pi)
        if rng.random() < 0.55:
            a = rng.normal(math.pi / 2, 0.22)
        n = rng.uniform(*length)
        wobble = rng.uniform(-0.2, 0.2)
        pts = []
        for i in range(8):
            t = i / 7.0
            pts.append((x + math.cos(a + wobble * t) * n * t, y + math.sin(a + wobble * t) * n * t))
        d.line(pts, fill=int(255 * rng.uniform(fade * 0.4, fade)), width=int(rng.integers(width[0], width[1] + 1)))
    arr = np.asarray(img, dtype=np.float32) / 255.0
    return wrap_fold(arr, size)


def random_state(seed):
    return np.random.default_rng(seed)


def wrap_fold(arr, size):
    out = np.zeros((size, size), dtype=np.float32)
    for j in range(3):
        for i in range(3):
            out = np.maximum(out, arr[j * size:(j + 1) * size, i * size:(i + 1) * size])
    return out


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.dstack((-dx, dy, np.ones_like(h)))
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n * 0.5 + 0.5


def save_rgb(name, rgb, quality=None):
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")
    path = os.path.join(OUT, name)
    if name.endswith(".jpg"):
        img.save(path, quality=quality or 90, subsampling=0)
    else:
        img.save(path, optimize=True)


def save_rgba(name, rgba):
    Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(
        os.path.join(OUT, name), optimize=True)


def worn_steel():
    s = 1024
    grime = fbm(s, s, 2.3, 1)
    blotch = fbm(s, s, 1.6, 2)
    fine = fbm(s, s, 0.9, 3)
    chips_field = fbm(s, s, 1.5, 4)
    edge = fbm(s, s, 2.8, 5)
    cuts = scratches(s, 260, 6, length=(8, 60), width=(1, 1), fade=0.5)
    long_cuts = scratches(s, 14, 7, length=(90, 300), width=(1, 1), fade=0.42)
    chips = smooth(0.72, 0.75, chips_field) * smooth(0.45, 0.62, edge)
    rub = smooth(0.58, 0.82, fbm(s, s, 2.0, 9))
    bare = np.clip(chips + (cuts + long_cuts) * 0.75, 0, 1)
    streak = np.asarray(Image.fromarray((fbm(s, s, 1.8, 8) * 255).astype(np.uint8)).resize((s // 16, s), Image.BILINEAR).resize((s, s), Image.BICUBIC)) / 255.0
    paint = 0.64 + (blotch - 0.5) * 0.22 + (fine - 0.5) * 0.07 + rub * 0.09
    dirt = smooth(0.46, 0.82, grime) * 0.34 + smooth(0.5, 0.82, streak) * 0.24
    paint *= 1 - dirt
    alb = np.dstack((paint * 1.0, paint * 0.975, paint * 0.93))
    metal = np.array([0.62, 0.60, 0.56])
    alb = alb * (1 - bare[..., None]) + metal * bare[..., None]
    save_rgb("steel_albedo.jpg", alb, 88)
    rough = 0.72 + dirt * 0.22 - bare * 0.2 - rub * 0.14 + (fine - 0.5) * 0.14
    metallic = chips * 0.8 + rub * 0.12 + 0.05
    occ = 1 - dirt * 0.25
    save_rgb("steel_orm.jpg", np.dstack((occ, rough, metallic)), 90)
    height = -chips * 0.5 - (cuts + long_cuts) * 0.22 + grime * 0.4 + fine * 0.28
    save_rgb("steel_normal.jpg", normal_from_height(height, 2.2), 92)


def hull_plating():
    s = 1024
    grime = fbm(s, s, 2.2, 11)
    fine = fbm(s, s, 0.9, 12)
    blotch = fbm(s, s, 1.5, 13)
    chips_field = fbm(s, s, 1.4, 14)
    cuts = scratches(s, 120, 15, length=(14, 90), width=(1, 1), fade=0.5)
    rng = random_state(16)
    panels = []

    def split(x0, y0, x1, y1, depth):
        w = x1 - x0
        h = y1 - y0
        if depth >= 5 or (depth >= 3 and rng.random() < 0.3) or max(w, h) < 170:
            panels.append((x0, y0, x1, y1))
            return
        along_x = w > h * 0.8 if rng.random() < 0.85 else w <= h * 0.8
        k = rng.uniform(0.32, 0.68)
        if along_x:
            cut = int(x0 + w * k)
            split(x0, y0, cut, y1, depth + 1)
            split(cut, y0, x1, y1, depth + 1)
        else:
            cut = int(y0 + h * k)
            split(x0, y0, x1, cut, depth + 1)
            split(x0, cut, x1, y1, depth + 1)

    split(0, 0, s, s, 0)
    tone = np.ones((s, s), dtype=np.float32)
    hue = np.zeros((s, s, 3), dtype=np.float32)
    seam_img = Image.new("L", (s, s), 0)
    sd = ImageDraw.Draw(seam_img)
    rivet_img = Image.new("L", (s, s), 0)
    rd = ImageDraw.Draw(rivet_img)
    for x0, y0, x1, y1 in panels:
        k = 1 + rng.normal(0, 0.05)
        odd = rng.random() < 0.12
        if odd:
            k *= rng.uniform(0.66, 0.84)
            hue[y0:y1, x0:x1] = np.array([rng.uniform(-0.03, 0.05), rng.uniform(-0.02, 0.02), rng.uniform(-0.05, 0.03)])
        tone[y0:y1, x0:x1] = k
        sd.rectangle((x0 - 1, y0 - 1, x1, y0), fill=255)
        sd.rectangle((x0 - 1, y1 - 1, x1, y1), fill=255)
        sd.rectangle((x0 - 1, y0, x0, y1), fill=255)
        sd.rectangle((x1 - 1, y0, x1, y1), fill=255)
        if rng.random() < 0.55:
            step = 34
            for rx in range(x0 + 12, x1 - 8, step):
                rd.ellipse((rx - 2, y0 + 7, rx + 2, y0 + 11), fill=255)
                rd.ellipse((rx - 2, y1 - 11, rx + 2, y1 - 7), fill=255)
            for ry in range(y0 + 12, y1 - 8, step):
                rd.ellipse((x0 + 7, ry - 2, x0 + 11, ry + 2), fill=255)
                rd.ellipse((x1 - 11, ry - 2, x1 - 7, ry + 2), fill=255)
    seam = np.asarray(seam_img, dtype=np.float32) / 255.0
    rivet = np.asarray(rivet_img, dtype=np.float32) / 255.0
    grime_seam = np.asarray(Image.fromarray((seam * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(7)), dtype=np.float32) / 255.0
    grime_run = np.asarray(Image.fromarray((grime_seam * 255).astype(np.uint8)).resize((s // 8, s), Image.BILINEAR).filter(ImageFilter.GaussianBlur(6)).resize((s, s), Image.BICUBIC), dtype=np.float32) / 255.0
    chips = smooth(0.71, 0.74, chips_field + grime_seam * 0.2)
    bare = np.clip(chips + cuts, 0, 1)
    paint = (0.60 + (blotch - 0.5) * 0.2 + (fine - 0.5) * 0.08) * tone
    dirt = np.clip(smooth(0.5, 0.84, grime) * 0.3 + grime_seam * 0.75 + grime_run * 0.7, 0, 0.82)
    paint = paint * (1 - dirt)
    alb = np.dstack((paint, paint * 0.97, paint * 0.9)) + hue
    metal = np.array([0.5, 0.48, 0.44])
    alb = alb * (1 - bare[..., None]) + metal * bare[..., None]
    alb *= 1 - seam[..., None] * 0.7
    save_rgb("plate_albedo.jpg", alb, 88)
    rough = 0.70 + dirt * 0.24 - bare * 0.26 + (fine - 0.5) * 0.14
    save_rgb("plate_orm.jpg", np.dstack((1 - seam * 0.7 - grime_seam * 0.3, rough, chips * 0.8 + 0.06)), 90)
    height = -seam * 2.0 + rivet * 0.9 - chips * 0.4 + grime * 0.4 + fine * 0.2
    save_rgb("plate_normal.jpg", normal_from_height(height, 2.0), 92)


def grating():
    s = 512
    y, x = np.mgrid[0:s, 0:s]
    bars = (np.abs(((x + y) % 64) - 32) < 11) | (np.abs(((x - y) % 64) - 32) < 11)
    bars = bars.astype(np.float32)
    dirt = fbm(s, s, 2.0, 21)
    alb = bars[..., None] * (0.35 + 0.18 * dirt[..., None]) * np.array([1.0, 0.97, 0.92])
    save_rgb("grate_albedo.jpg", alb + 0.015, 85)


def window_grime():
    s = 1024
    streak = fbm(s, s, 1.9, 31)
    streak = np.asarray(Image.fromarray((streak * 255).astype(np.uint8)).resize((s // 10, s), Image.BILINEAR).resize((s, s), Image.BICUBIC)) / 255.0
    soot = fbm(s, s, 2.4, 32)
    cuts = scratches(s, 90, 33, length=(30, 300), width=(1, 1), fade=1.0)
    u, v = np.meshgrid(np.linspace(0, 1, s), np.linspace(0, 1, s))
    border = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v))
    edge = 1 - smooth(0.0, 0.2, border + (soot - 0.5) * 0.14)
    alpha = edge * edge * (0.28 + smooth(0.4, 0.8, soot) * 0.5) + smooth(0.68, 0.94, soot) * 0.05 + smooth(0.72, 0.94, streak) * 0.022 + cuts * 0.09 + 0.012
    col = np.dstack((0.46 + cuts * 0.5, 0.36 + cuts * 0.55, 0.25 + cuts * 0.6))
    save_rgba("window_grime.png", np.dstack((col, np.clip(alpha, 0, 1))))


def sky():
    w, h = 4096, 2048
    n1 = fbm(w, h, 2.6, 41)
    n2 = fbm(w, h, 2.1, 42)
    n3 = fbm(w, h, 2.9, 43)
    lat = np.linspace(-1, 1, h)[:, None]
    band = np.exp(-(lat * 2.6 + (n3 - 0.5) * 1.3) ** 2)
    cloud = smooth(0.42, 0.86, n1) * band
    dust = smooth(0.5, 0.74, n2) * band
    r = cloud * 0.42 * (1 - dust * 0.65) + 0.006
    g = cloud * 0.34 * (1 - dust * 0.7) + 0.007
    b = cloud * 0.5 * (1 - dust * 0.6) + 0.012
    hue = smooth(0.35, 0.7, n3)
    r = r * (0.7 + hue * 0.9)
    b = b * (1.5 - hue * 0.9)
    img = np.dstack((r, g, b)).astype(np.float32)
    rng = random_state(44)
    density = 0.25 + band[:, 0] * 0.75
    n = 26000
    ys = rng.integers(0, h, n)
    keep = rng.random(n) < density[ys]
    ys = ys[keep]
    xs = rng.integers(0, w, ys.size)
    mag = rng.random(ys.size) ** 16
    tint = rng.random((ys.size, 3)) * 0.3 + 0.7
    for x, y, m, t in zip(xs, ys, mag, tint):
        val = 0.16 + m * 1.2
        rad = 0 if m < 0.7 else 1
        if rad == 0:
            img[y, x] += val * t
        else:
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    f = math.exp(-(dx * dx + dy * dy) / 0.8)
                    img[(y + dy) % h, (x + dx) % w] += val * f * t
    save_rgb("sky.jpg", np.sqrt(np.clip(img, 0, 1)), 86)


def ramp(t, stops):
    xs = [s[0] for s in stops]
    out = np.zeros(t.shape + (3,), dtype=np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, xs, [s[1][c] / 255.0 for s in stops])
    return out


def planets():
    w, h = 2048, 1024
    lat = np.linspace(-1, 1, h)[:, None]
    warp = fbm(w, h, 2.5, 51) - 0.5
    base = fbm(w, h, 2.2, 52)
    fine = fbm(w, h, 1.4, 53)
    t = np.clip(base * 0.75 + fine * 0.25 + warp * 0.3, 0, 1)
    t = (t - t.min()) / (t.max() - t.min())
    desert = ramp(t, [(0, (66, 32, 20)), (0.32, (128, 66, 34)), (0.52, (190, 112, 58)), (0.72, (222, 164, 102)), (1, (246, 216, 170))])
    pole = smooth(0.8, 0.97, np.abs(lat + warp * 0.2))
    desert = desert * (1 - pole[..., None]) + np.array([0.92, 0.9, 0.88]) * pole[..., None]
    save_rgb("planet_oro.jpg", desert, 88)

    fy = lat + fbm(w, h, 2.6, 54)[:, :] * 0.06 + (fbm(w, h, 1.9, 55) - 0.5) * 0.05
    bands = 0.5 + 0.5 * np.sin(fy * 17) * 0.55 + 0.5 * np.sin(fy * 41 + 1.3) * 0.3 + (fine - 0.5) * 0.18
    giant = ramp(np.clip(bands, 0, 1), [(0, (44, 80, 110)), (0.35, (92, 148, 170)), (0.6, (172, 208, 212)), (0.82, (224, 228, 208)), (1, (246, 240, 226))])
    save_rgb("planet_naur.jpg", giant, 88)

    cracks = 1 - np.abs(fbm(w, h, 2.1, 56) * 2 - 1)
    lava = smooth(0.90, 0.985, cracks)
    rock = ramp(t, [(0, (18, 15, 15)), (0.5, (44, 36, 34)), (1, (88, 74, 68))])
    save_rgb("planet_cinder.jpg", rock * (1 - lava[..., None]), 88)
    glow = np.dstack((lava, lava * lava * 0.4, lava * lava * lava * 0.1))
    save_rgb("planet_cinder_glow.jpg", glow, 60)

    rs = 1024
    ry, rx = np.mgrid[0:rs, 0:rs] / (rs - 1.0)
    rr = np.sqrt((rx - 0.5) ** 2 + (ry - 0.5) ** 2) * 2
    density = np.interp(rr, [0, 0.5, 0.53, 0.62, 0.64, 0.66, 0.8, 0.82, 0.84, 0.97, 1.0], [0, 0, 0.3, 0.72, 0.08, 0.8, 0.9, 0.06, 0.55, 0.2, 0])
    grain = np.interp(rr, np.linspace(0, 1, 512), fbm(512, 8, 1.1, 57)[0])
    alpha = np.clip(density * (0.42 + grain * 0.9), 0, 1)
    save_rgba("rings.png", np.dstack((np.full_like(rr, 0.92), 0.86 + grain * 0.1, 0.76 + grain * 0.18, alpha)))


def overlays():
    s = 512
    y, x = np.mgrid[0:s, 0:s] / (s - 1.0)
    r = np.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2) * 2
    halo = np.clip(1 - r, 0, 1) ** 2.6
    save_rgba("halo.png", np.dstack((np.ones_like(r), np.ones_like(r), np.ones_like(r), halo)))
    limb = smooth(0.79, 0.835, r) * (1 - smooth(0.835, 1.0, r)) ** 2.2
    save_rgba("limb.png", np.dstack((np.ones_like(r), np.ones_like(r), np.ones_like(r), limb)))

    vig = np.clip(1 - smooth(0.25, 1.28, np.maximum(np.abs(x - 0.5) * 2, np.abs(y - 0.5) * 2) * 0.52 + r * 0.52), 0, 1)
    save_rgba("crt_vignette.png", np.dstack((np.zeros_like(r), np.zeros_like(r), np.zeros_like(r), 1 - vig)))

    lines = np.zeros((600, 800, 4), dtype=np.float32)
    lines[np.arange(600) % 3 == 1, :, 3] = 0.34
    lines[np.arange(600) % 3 == 2, :, 3] = 0.58
    save_rgba("scanlines.png", lines)

    beam_w, beam_h = 256, 512
    by = np.mgrid[0:beam_h, 0:beam_w][0] / (beam_h - 1.0)
    bx = np.mgrid[0:beam_h, 0:beam_w][1] / (beam_w - 1.0)
    edge = 1 - np.abs(bx - 0.5) * 2
    streaks = fbm(beam_w, beam_h, 1.5, 61)
    streaks = np.tile(streaks[0:1, :], (beam_h, 1))
    a = smooth(0, 0.8, edge) * (by ** 1.6) * (0.35 + streaks * 0.65)
    save_rgba("holo_beam.png", np.dstack((np.ones_like(a), np.ones_like(a), np.ones_like(a), a)))

    tun = fbm(1024, 1024, 1.7, 62)
    tun = np.asarray(Image.fromarray((tun * 255).astype(np.uint8)).resize((1024, 48), Image.BILINEAR).resize((1024, 1024), Image.BICUBIC)) / 255.0
    tun = smooth(0.35, 0.86, tun)
    save_rgb("tunnel.jpg", np.dstack((tun * tun * 0.62, tun * 0.78, tun ** 0.8)), 84)


def tape():
    w, h = 512, 192
    base = fbm(w, h, 1.5, 71)
    a = np.ones((h, w), dtype=np.float32)
    rng = random_state(72)
    img = Image.fromarray((a * 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    for x in range(0, w, 9):
        d.polygon([(x, 0), (x + 5, int(rng.integers(0, 5))), (x + 9, 0)], fill=0)
        d.polygon([(x, h), (x + 5, h - int(rng.integers(0, 5))), (x + 9, h)], fill=0)
    a = np.asarray(img, dtype=np.float32) / 255.0
    edge_l = smooth(0, 10, np.arange(w, dtype=np.float32) + rng.random(w) * 8)
    edge_r = smooth(0, 10, (w - 1 - np.arange(w, dtype=np.float32)) + rng.random(w) * 8)
    a = a * edge_l[None, :] * edge_r[None, :]
    col = 0.72 + (base - 0.5) * 0.25
    save_rgba("tape.png", np.dstack((col, col * 0.93, col * 0.74, a)))

    s = 256
    img = Image.new("RGBA", (s * 2, s), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    for i in range(-s, s * 2 + s, 64):
        d.polygon([(i, s), (i + 32, s), (i + 32 + s, 0), (i + s, 0)], fill=(226, 172, 40, 255))
    arr = np.asarray(img, dtype=np.float32) / 255.0
    wear = smooth(0.55, 0.75, fbm(s * 2, s, 1.6, 73))[..., None]
    arr[..., :3] = arr[..., :3] * (1 - wear * 0.55) + np.array([0.3, 0.28, 0.25]) * wear * 0.55
    save_rgb("hazard.jpg", arr[..., :3], 88)


def quilt():
    s = 1024
    y, x = np.mgrid[0:s, 0:s] / float(s)
    n = 4
    a = np.abs(np.sin((x + y) * n * np.pi))
    b = np.abs(np.sin((x - y) * n * np.pi))
    pad = np.minimum(a, b) ** 0.55
    crease = 1 - smooth(0.0, 0.2, np.minimum(a, b))
    u = (x + y) * n
    v = (x - y) * n
    du = np.abs(u - np.round(u))
    dv = np.abs(v - np.round(v))
    dimple = np.exp(-(du * du + dv * dv) / 0.0035)
    fine = fbm(s, s, 0.8, 91)
    blotch = fbm(s, s, 1.8, 92)
    wear = smooth(0.62, 0.9, pad * 0.55 + blotch * 0.6)
    base = 0.56 + (blotch - 0.5) * 0.24 + (fine - 0.5) * 0.08
    shade = base * (1 - crease * 0.72) * (1 - dimple * 0.8) + wear * 0.15
    save_rgb("quilt_albedo.jpg", np.dstack((shade, shade * 0.93, shade * 0.84)), 88)
    height = pad * 6.0 - dimple * 2.4 + fine * 0.18
    save_rgb("quilt_normal.jpg", normal_from_height(height, 1.2), 92)


def decals():
    s = 512
    y, x = np.mgrid[0:s, 0:s] / (s - 1.0)
    r = np.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2) * 2
    cloud = fbm(s, s, 1.9, 101)
    grain = fbm(s, s, 0.7, 102)
    alpha = smooth(0.3, 0.75, cloud + (1 - r) * 0.72 - 0.2) * (1 - smooth(0.62, 1.0, r))
    alpha = np.clip(alpha * (0.62 + grain * 0.76), 0, 1)
    ones = np.ones_like(alpha)
    save_rgba("patch.png", np.dstack((ones, ones, ones, alpha)))

    streak = np.asarray(Image.fromarray((fbm(s, s, 1.7, 103) * 255).astype(np.uint8)).resize((s // 14, s), Image.BILINEAR).resize((s, s), Image.BICUBIC)) / 255.0
    side = 1 - smooth(0.12, 0.5, np.abs(x - 0.5))
    run = smooth(0.0, 0.08, y) * (1 - smooth(0.25, 1.0, y))
    alpha = np.clip(smooth(0.42, 0.74, streak) * side * run * 1.2, 0, 1)
    save_rgba("streak.png", np.dstack((ones, ones, ones, alpha)))

    rng = random_state(104)
    img = Image.new("L", (1024, 1024), 0)
    d = ImageDraw.Draw(img)
    cx, cy = 512, 512
    angles = np.sort(rng.uniform(0, 2 * math.pi, 15))
    for a in angles:
        px, py = cx, cy
        reach = rng.uniform(170, 490)
        steps = 11
        heading = a
        for i in range(steps):
            heading += rng.normal(0, 0.1)
            nx = px + math.cos(heading) * reach / steps
            ny = py + math.sin(heading) * reach / steps
            d.line((px, py, nx, ny), fill=int(250 - i * 13), width=2 if i < 5 else 1)
            if rng.random() < 0.32:
                side_a = heading + rng.choice([-1.0, 1.0]) * rng.uniform(0.4, 1.0)
                n = rng.uniform(18, 80)
                d.line((px, py, px + math.cos(side_a) * n, py + math.sin(side_a) * n), fill=int(190 - i * 8), width=1)
            px, py = nx, ny
    for rad in (48, 96, 168):
        pts = []
        wob = rng.uniform(0, 7)
        for i in range(90):
            t = i / 89.0 * 2 * math.pi
            k = rad * (1 + 0.08 * math.sin(t * 5 + wob))
            pts.append((cx + math.cos(t) * k, cy + math.sin(t) * k))
        for i in range(89):
            if math.sin(i * 0.9 + wob) > -0.3:
                d.line((pts[i], pts[i + 1]), fill=120, width=1)
    d.ellipse((cx - 11, cy - 11, cx + 11, cy + 11), fill=240)
    arr = np.asarray(img.filter(ImageFilter.GaussianBlur(0.5)), dtype=np.float32) / 255.0
    gy, gx = np.mgrid[0:1024, 0:1024] / 1023.0
    rr = np.sqrt((gx - 0.5) ** 2 + (gy - 0.5) ** 2) * 2
    bruise = np.exp(-(rr / 0.09) ** 2) * 0.5
    ones = np.ones_like(arr)
    save_rgba("crack.png", np.dstack((ones, ones, ones, np.clip(arr * 0.92 + bruise, 0, 1))))

    t = 256
    y, x = np.mgrid[0:t, 0:t] / (t - 1.0)
    alpha = np.zeros((t, t), dtype=np.float32)
    noise = fbm(t, t, 1.6, 105)
    for cx, cy, rad, gap in ((0.40, 0.42, 0.30, 0.9), (0.66, 0.6, 0.27, -1.2)):
        dist = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
        ang = np.arctan2(y - cy, x - cx)
        band = np.exp(-((dist - rad) / 0.017) ** 2) * 0.9 + np.exp(-((dist - rad + 0.022) / 0.03) ** 2) * 0.18
        alpha = np.maximum(alpha, band * smooth(-0.9, 0.3, np.cos(ang - gap) + (noise - 0.5) * 1.6))
    save_rgba("coffee_ring.png", np.dstack((np.full_like(alpha, 0.24), np.full_like(alpha, 0.12), np.full_like(alpha, 0.05), np.clip(alpha * (0.5 + noise), 0, 1))))


def photo():
    w, h = 256, 310
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    img = np.ones((h, w, 3), dtype=np.float32) * np.array([0.86, 0.82, 0.72])
    x0, y0, x1, y1 = 16, 16, w - 16, h - 62
    u = (x - x0) / (x1 - x0)
    v = (y - y0) / (y1 - y0)
    sky_top = np.array([0.22, 0.13, 0.20])
    sky_low = np.array([0.93, 0.56, 0.26])
    sky_col = sky_top[None, None] * (1 - v[..., None]) + sky_low[None, None] * v[..., None]
    sun = np.exp(-(((u - 0.66) / 0.17) ** 2 + ((v - 0.62) / 0.14) ** 2))
    sky_col = sky_col + sun[..., None] * np.array([0.55, 0.38, 0.16])
    dune = 0.76 + 0.05 * np.sin(u * 7.0) + 0.02 * np.sin(u * 19.0 + 1.0)
    ground = (v > dune).astype(np.float32)[..., None]
    pic = sky_col * (1 - ground) + np.array([0.20, 0.11, 0.07]) * ground
    hull = (np.abs(u - 0.34) < 0.17) & (np.abs(v - 0.69) < 0.045)
    nose = (u > 0.5) & (u < 0.60) & (np.abs(v - 0.69) < 0.045 * (0.6 - u) / 0.1)
    fin = (np.abs(u - 0.22) < 0.022) & (v > 0.56) & (v < 0.66)
    gear = (np.abs(u - 0.3) < 0.012) & (v > 0.72) & (v < 0.8)
    a = (np.abs(u - 0.72) < 0.012) & (v > 0.66) & (v < 0.78)
    b = (np.abs(u - 0.78) < 0.010) & (v > 0.68) & (v < 0.78)
    mask = (hull | nose | fin | gear | a | b)[..., None].astype(np.float32)
    pic = pic * (1 - mask) + np.array([0.05, 0.035, 0.04]) * mask
    grain = fbm(w, h, 0.5, 111)[..., None]
    pic = pic * (0.86 + grain * 0.28)
    inside = ((x >= x0) & (x < x1) & (y >= y0) & (y < y1))[..., None].astype(np.float32)
    img = img * (1 - inside) + pic * inside
    stain = smooth(0.55, 0.85, fbm(w, h, 2.2, 112))[..., None]
    img = img * (1 - stain * 0.2)
    save_rgb("photo.jpg", img, 88)


def chart():
    s = 1024
    paper = fbm(s, s, 1.8, 121)
    fine = fbm(s, s, 0.7, 122)
    y, x = np.mgrid[0:s, 0:s] / (s - 1.0)
    fold = np.exp(-((x - 0.5) / 0.003) ** 2) * 0.1 + np.exp(-((y - 0.5) / 0.003) ** 2) * 0.09
    edge = np.minimum(np.minimum(x, 1 - x), np.minimum(y, 1 - y))
    tan = 1 - smooth(0.0, 0.16, edge + (paper - 0.5) * 0.09)
    base = 0.76 + (paper - 0.5) * 0.16 + (fine - 0.5) * 0.05 - tan * 0.32 - fold
    rgb = np.dstack((base, base * 0.88, base * 0.66))
    field = fbm(s, s, 2.8, 123) * 24
    contour = np.exp(-((field - np.round(field)) / 0.045) ** 2)
    ink = contour * 0.28
    img = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(img)
    for i in range(1, 8):
        d.line((i * s // 8, 0, i * s // 8, s), fill=70, width=1)
        d.line((0, i * s // 8, s, i * s // 8), fill=70, width=1)
    ports = [(300, 590), (760, 300), (190, 880), (610, 760), (850, 840)]
    for a, b in ((0, 1), (0, 2), (0, 3), (3, 4)):
        ax, ay = ports[a]
        bx, by = ports[b]
        n = int(math.hypot(bx - ax, by - ay) / 18)
        for i in range(n):
            if i % 2 == 0:
                t0 = i / float(n)
                t1 = (i + 1) / float(n)
                d.line((ax + (bx - ax) * t0, ay + (by - ay) * t0, ax + (bx - ax) * t1, ay + (by - ay) * t1), fill=240, width=4)
    for px, py in ports:
        d.ellipse((px - 18, py - 18, px + 18, py + 18), outline=255, width=4)
        d.ellipse((px - 5, py - 5, px + 5, py + 5), fill=255)
    marks = np.asarray(img, dtype=np.float32) / 255.0
    ink = np.clip(ink + marks, 0, 1)[..., None]
    rgb = rgb * (1 - ink * 0.78) + np.array([0.12, 0.09, 0.12]) * ink * 0.78
    redimg = Image.new("L", (s, s), 0)
    rd = ImageDraw.Draw(redimg)
    rd.ellipse((590, 740, 630, 780), outline=255, width=6)
    rd.line((700, 620, 800, 720), fill=255, width=6)
    rd.line((800, 620, 700, 720), fill=255, width=6)
    red = (np.asarray(redimg, dtype=np.float32) / 255.0)[..., None]
    rgb = rgb * (1 - red * 0.88) + np.array([0.56, 0.08, 0.05]) * red * 0.88
    save_rgb("chart.jpg", rgb, 88)


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
            base = 0.015 + n * n * 0.09
            front = max(0.0, -d[2]) ** 2
            warm = max(0.0, d[0] * 0.5 + d[1] * 0.5 - d[2] * 0.7) ** 6 * 1.8
            c = (base * 1.0 + warm * 1.0, base * 0.95 + warm * 0.72, base * 1.25 + warm * 0.5 + front * 0.03)
            m = max(c)
            mant, exp = math.frexp(m)
            k = mant * 256.0 / m
            data += bytes((int(c[0] * k), int(c[1] * k), int(c[2] * k), exp + 128))
    with open(os.path.join(OUT, "space.hdr"), "wb") as f:
        f.write(data)


STEPS = {
    "steel": worn_steel,
    "plate": hull_plating,
    "grate": grating,
    "grime": window_grime,
    "sky": sky,
    "planets": planets,
    "overlays": overlays,
    "tape": tape,
    "quilt": quilt,
    "decals": decals,
    "photo": photo,
    "chart": chart,
    "environment": environment,
}


if __name__ == "__main__":
    for name in sys.argv[1:] or STEPS:
        STEPS[name]()
