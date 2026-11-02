#!/usr/bin/env python3
import os

import numpy as np
from PIL import Image, ImageDraw

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


def stretched(n, sx, sy):
    h, w = n.shape
    small = Image.fromarray((n * 255).astype(np.uint8)).resize((max(w // sx, 2), max(h // sy, 2)), Image.BILINEAR)
    a = np.array(small.resize((w, h), Image.BICUBIC)) / 255
    return (a - a.min()) / (a.max() - a.min())


def save(name, a, quality=84):
    a = np.clip(a, 0, 1)
    img = Image.fromarray((a * 255 + 0.5).astype(np.uint8))
    if a.ndim == 3 and a.shape[2] == 4:
        img.save(os.path.join(OUT, name + ".png"), optimize=True)
    else:
        img.save(os.path.join(OUT, name + ".jpg"), quality=quality, subsampling=0)


def save_card(name, img, fill):
    a = np.array(img).astype(float) / 255
    a[..., :3] = np.where(a[..., 3:] > 0.01, a[..., :3], fill)
    save(name, a)


def plaster():
    h, w = 512, 512
    big = noise(h, w, 1.9, 1)
    mid = noise(h, w, 1.1, 2)
    fine = noise(h, w, 0.4, 3)
    streaks = stretched(noise(h, w, 0.8, 4), 24, 1)
    v = 0.86 + (big - 0.5) * 0.16 + (mid - 0.5) * 0.08 + (fine - 0.5) * 0.06
    v -= smooth(0.56, 0.86, streaks) * smooth(0.3, 0.7, big) * 0.2
    a = np.ones((h, w, 3)) * v[..., None]
    moss = smooth(0.62, 0.8, big * 0.6 + mid * 0.4) * 0.28
    a = mix(a, a * rgb(170, 190, 130), moss)
    save("plaster", a)
    save("plaster_normal", normal_map(fine * 0.5 + mid * 0.5, 1.6), 82)


def concrete():
    h, w = 512, 512
    fine = noise(h, w, 0.5, 11)
    big = noise(h, w, 1.8, 12)
    boards = stretched(noise(h, w, 0.7, 13), 1, 18)
    streaks = stretched(noise(h, w, 0.8, 14), 20, 1)
    joint = np.ones((h, w))
    bands = 4
    for b in range(bands):
        y = int(b * h / bands)
        joint[max(y - 1, 0):y + 2, :] = 0.2
    x = w // 2
    joint[:, x - 1:x + 2] = 0.3
    holes = np.ones((h, w))
    for cy in range(int(h / bands / 2), h, int(h / bands)):
        for cx in range(int(w / 8), w, int(w / 4)):
            yy, xx = np.ogrid[-cy:h - cy, -cx:w - cx]
            holes[yy * yy + xx * xx <= 16] = 0.45
    v = 0.74 + (big - 0.5) * 0.24 + (boards - 0.5) * 0.12 + (fine - 0.5) * 0.09
    v -= smooth(0.58, 0.86, streaks) * 0.14
    a = np.ones((h, w, 3)) * (v * joint * holes)[..., None]
    save("concrete", a)
    save("concrete_normal", normal_map(joint * 1.6 + holes * 1.2 + boards * 0.4 + fine * 0.3, 2.4), 82)


def wood():
    h, w = 512, 512
    boards = 5
    rng = np.random.default_rng(21)
    fine = noise(h, w, 0.8, 22)
    grain = stretched(fine, 16, 1)
    rings = 0.5 + 0.5 * np.sin(grain * 34)
    tone = np.ones((h, w))
    mask = np.ones((h, w))
    for b in range(boards):
        y0, y1 = int(b * h / boards), int((b + 1) * h / boards)
        tone[y0:y1] = 0.78 + rng.random() * 0.3
        d = np.minimum(np.arange(y1 - y0), y1 - y0 - 1 - np.arange(y1 - y0))[:, None]
        mask[y0:y1] = smooth(1, 4, d)
    v = tone * (0.74 + rings * 0.2 + grain * 0.14) * (0.12 + 0.88 * mask)
    save("wood", np.ones((h, w, 3)) * v[..., None] * rgb(255, 242, 226))
    save("wood_normal", normal_map(mask * 1.2 + rings * 0.09 + grain * 0.14, 3), 82)


def deck():
    h, w = 512, 512
    planks = 8
    rng = np.random.default_rng(31)
    fine = noise(h, w, 0.8, 32)
    grain = stretched(fine, 1, 18)
    rings = 0.5 + 0.5 * np.sin(grain * 26)
    tone = np.ones((h, w))
    mask = np.ones((h, w))
    nails = np.ones((h, w))
    for p in range(planks):
        x0, x1 = int(p * w / planks), int((p + 1) * w / planks)
        tone[:, x0:x1] = 0.74 + rng.random() * 0.34
        d = np.minimum(np.arange(x1 - x0), x1 - x0 - 1 - np.arange(x1 - x0))[None, :]
        mask[:, x0:x1] = smooth(0.8, 3.2, d)
        for cy in (int(h * 0.08), int(h * 0.92), int(h * 0.5)):
            for ox in (0.28, 0.72):
                cx = int(x0 + (x1 - x0) * ox)
                nails[cy - 2:cy + 2, cx - 2:cx + 2] = 0.55
    weather = noise(h, w, 1.6, 33)
    v = tone * (0.72 + rings * 0.18 + grain * 0.16) * (0.08 + 0.92 * mask) * nails
    a = np.ones((h, w, 3)) * v[..., None] * rgb(255, 238, 214)
    a = mix(a, a * rgb(186, 196, 170), smooth(0.56, 0.8, weather) * 0.7)
    save("deck", a)
    save("deck_normal", normal_map(mask * 1.4 + rings * 0.08 + grain * 0.12, 2.6), 82)


def blocks(h, w, courses, per_course, gap, seed, jitter=0.25):
    rng = np.random.default_rng(seed)
    mask = np.zeros((h, w))
    ids = np.zeros((h, w))
    ch = h / courses
    for c in range(courses):
        y0, y1 = int(c * ch), int((c + 1) * ch)
        cuts = (np.arange(per_course) + rng.uniform(-jitter, jitter, per_course)) / per_course
        cuts = np.sort((cuts + (c % 2) * 0.5 / per_course) % 1)
        px = (cuts * w).astype(int)
        for i in range(len(px)):
            x0, x1 = px[i], px[(i + 1) % len(px)]
            value = rng.random()
            xs = np.arange(x0, x1 if x1 > x0 else x1 + w) % w
            ids[y0:y1, xs] = value
            dist_x = np.minimum(np.arange(len(xs)), len(xs) - 1 - np.arange(len(xs)))
            dist_y = np.minimum(np.arange(y1 - y0), y1 - y0 - 1 - np.arange(y1 - y0))
            d = np.minimum.outer(dist_y, dist_x)
            mask[np.ix_(np.arange(y0, y1), xs)] = smooth(gap * 0.5, gap * 1.8, d)
    return mask, ids


def pavers():
    h, w = 512, 512
    mask, ids = blocks(h, w, 8, 6, 3.2, 41)
    fine = noise(h, w, 0.6, 42)
    big = noise(h, w, 1.7, 43)
    tone = 0.76 + ids * 0.24 + (big - 0.5) * 0.24 + (fine - 0.5) * 0.14
    stone = np.ones((h, w, 3)) * tone[..., None] * rgb(255, 248, 232)
    joint = mix(np.ones((h, w, 3)) * rgb(58, 66, 42), np.ones((h, w, 3)) * rgb(70, 106, 48), big)
    a = mix(joint, stone, mask)
    moss = smooth(0.6, 0.78, big) * (1 - mask * 0.72)
    a = mix(a, a * rgb(150, 182, 112), moss)
    save("pavers", a)
    save("pavers_normal", normal_map(mask * 1.5 + fine * 0.28 + big * 0.2, 2.4), 82)


def ashlar():
    h, w = 512, 512
    mask, ids = blocks(h, w, 6, 4, 2.4, 51)
    fine = noise(h, w, 0.7, 52)
    big = noise(h, w, 1.6, 53)
    tone = 0.84 + ids * 0.18 + (big - 0.5) * 0.26 + (fine - 0.5) * 0.16
    a = mix(np.ones((h, w, 3)) * rgb(92, 92, 78), np.ones((h, w, 3)) * tone[..., None] * rgb(255, 250, 236), mask)
    a = mix(a, a * rgb(158, 186, 122), smooth(0.62, 0.8, big) * 0.55)
    save("ashlar", a)
    save("ashlar_normal", normal_map(mask * 1.3 + fine * 0.22 + big * 0.2, 2.2), 82)


def bark():
    h, w = 256, 256
    n = noise(h, w, 0.8, 61)
    ridges = stretched(n, 1, 16)
    groove = smooth(0.25, 0.7, ridges)
    save("bark", np.ones((h, w, 3)) * (0.42 + groove[..., None] * 0.62) * (0.85 + n[..., None] * 0.3))
    save("bark_normal", normal_map(groove * 1.6 + n * 0.25, 4), 82)


def ground():
    h, w = 1024, 1024
    big = noise(h, w, 2.0, 71)
    mid = noise(h, w, 1.25, 72)
    fine = noise(h, w, 0.4, 73)
    soil = noise(h, w, 1.0, 74)
    a = mix(np.ones((h, w, 3)) * rgb(44, 64, 28), np.ones((h, w, 3)) * rgb(92, 118, 46), big * 1.2 - 0.1)
    a = mix(a, rgb(32, 54, 26), smooth(0.54, 0.72, mid) * 0.65)
    bare = smooth(0.58, 0.72, soil * 0.65 + (1 - big) * 0.35)
    earth = mix(np.ones((h, w, 3)) * rgb(62, 46, 30), np.ones((h, w, 3)) * rgb(92, 72, 46), fine)
    a = mix(a, earth, bare * 0.9)
    a = a * (0.72 + fine[..., None] * 0.56)
    rng = np.random.default_rng(75)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    palette = [(150, 104, 52), (122, 84, 42), (172, 132, 66), (96, 78, 40), (136, 92, 40)]
    heights = Image.new("L", (w, h), 0)
    dh = ImageDraw.Draw(heights)
    for _ in range(5200):
        px, py = rng.uniform(0, w), rng.uniform(0, h)
        length, width, angle = rng.uniform(3, 10), rng.uniform(1.2, 3.2), rng.uniform(0, 3.14)
        ca, sa = np.cos(angle), np.sin(angle)
        pts = [(px + ca * length, py + sa * length), (px - sa * width, py + ca * width),
               (px - ca * length, py - sa * length), (px + sa * width, py - ca * width)]
        color = palette[rng.integers(5)]
        draw.polygon(pts, fill=color + (255,))
        dh.polygon(pts, fill=int(rng.uniform(120, 255)))
    litter = np.array(img).astype(float) / 255
    lmask = litter[..., 3] * smooth(0.36, 0.62, soil * 0.6 + (1 - big) * 0.4)
    a = mix(a, litter[..., :3], lmask)
    height = np.array(heights) / 255 * lmask * 0.8 + mid * 1.1 + fine * 0.5
    save("ground", a, 82)
    save("ground_normal", normal_map(height, 3), 80)


def strata():
    h, w = 256, 1024
    n = noise(h, w, 1.2, 81)
    f = noise(h, w, 0.6, 82)
    y = np.linspace(0, 4.9, h)[:, None] * np.ones((1, w))
    d = y + (noise(h, w, 2.2, 83) - 0.5) * 0.9
    a = np.ones((h, w, 3)) * rgb(48, 62, 30)
    a = mix(a, rgb(58, 40, 28), smooth(0.28, 0.5, d))
    a = mix(a, rgb(118, 78, 50), smooth(0.9, 1.2, d))
    a = mix(a, rgb(146, 104, 66), smooth(1.9, 2.0, d))
    a = mix(a, rgb(104, 88, 72), smooth(2.8, 2.9, d))
    a = mix(a, rgb(78, 76, 70), smooth(3.6, 3.7, d))
    a = a * (0.72 + n[..., None] * 0.4) * (0.86 + f[..., None] * 0.28)
    rng = np.random.default_rng(84)
    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    for _ in range(420):
        px, py = rng.uniform(0, w), rng.uniform(h * 0.42, h)
        r = rng.uniform(1.5, 6)
        tone = int(rng.uniform(90, 255))
        for ox in (-w, 0, w):
            draw.ellipse((px + ox - r * 1.5, py - r, px + ox + r * 1.5, py + r), fill=tone)
    roots = Image.new("L", (w, h), 0)
    dr = ImageDraw.Draw(roots)
    for _ in range(130):
        x, yy = rng.uniform(0, w), 0.0
        reach = rng.uniform(40, 150)
        pts = [(x, yy)]
        while yy < reach:
            yy += rng.uniform(5, 14)
            x += rng.uniform(-7, 7)
            pts.append((x, yy))
        dr.line(pts, fill=255, width=int(rng.uniform(1, 3)))
    pebbles = np.array(img) / 255
    root_mask = np.array(roots) / 255
    a = np.where((pebbles > 0)[..., None], rgb(150, 142, 128) * pebbles[..., None] + 0.05, a)
    a = mix(a, rgb(44, 32, 22), root_mask * 0.85)
    height = n * 0.8 + f * 0.3 + blur(pebbles, 1) * 1.1 + blur(root_mask, 1) * 0.9
    save("strata", a)
    save("strata_normal", normal_map(height, 3.2), 82)


def water():
    h, w = 512, 512
    yy, xx = np.mgrid[0:h, 0:w] / h
    height = np.zeros((h, w))
    rng = np.random.default_rng(91)
    warp = noise(h, w, 1.8, 92) * 2 * np.pi
    for _ in range(26):
        fx, fy = rng.integers(-12, 13), rng.integers(4, 18)
        height += np.sin(2 * np.pi * (fx * xx + fy * yy) + rng.uniform(0, 6.28) + warp * 1.4) / (fx * fx + fy * fy) ** 0.5
    height = height * 0.6 + noise(h, w, 1.3, 93) * 3
    height = (height - height.min()) / (height.max() - height.min())
    save("water_normal", normal_map(height, 3.2), 84)


def facade():
    bays, floors, cell = 8, 8, 128
    size = bays * cell
    rng = np.random.default_rng(101)
    albedo = Image.new("RGB", (size, size))
    emissive = Image.new("L", (size, size), 0)
    orm = Image.new("RGB", (size, size))
    d = ImageDraw.Draw(albedo)
    de = ImageDraw.Draw(emissive)
    dr = ImageDraw.Draw(orm)
    wall = (206, 204, 190)
    frame = (70, 74, 72)
    dr.rectangle((0, 0, size, size), fill=(255, 210, 0))
    d.rectangle((0, 0, size, size), fill=wall)
    for f in range(floors):
        for b in range(bays):
            x0, y0 = b * cell, f * cell
            x1, y1 = x0 + cell, y0 + cell
            d.rectangle((x0, y0, x1, y0 + 8), fill=(168, 166, 150))
            d.rectangle((x0, y1 - 14, x1, y1), fill=(186, 184, 170))
            gx0, gy0, gx1, gy1 = x0 + 8, y0 + 13, x1 - 8, y1 - 19
            kind = rng.random()
            lit = kind < 0.26
            if lit:
                warm = rng.uniform(0.78, 1)
                room = (int(112 * warm), int(88 * warm), int(62 * warm))
                d.rectangle((gx0, gy0, gx1, gy1), fill=room)
                de.rectangle((gx0, gy0, gx1, gy1), fill=int(255 * rng.uniform(0.62, 1)))
                dr.rectangle((gx0, gy0, gx1, gy1), fill=(255, 90, 0))
                ceiling = int(rng.uniform(12, 28))
                d.rectangle((gx0, gy0, gx1, gy0 + ceiling), fill=(int(room[0] * 1.06), int(room[1] * 1.06), int(room[2] * 1.05)))
                furniture = rng.random()
                if furniture < 0.4:
                    px = int(rng.uniform(gx0 + 4, gx1 - 46))
                    d.rectangle((px, gy1 - 22, px + 42, gy1), fill=(60, 48, 40))
                    de.rectangle((px, gy1 - 22, px + 42, gy1), fill=24)
                    d.rectangle((px, gy1 - 34, px + 8, gy1), fill=(60, 48, 40))
                    de.rectangle((px, gy1 - 34, px + 8, gy1), fill=24)
                elif furniture < 0.7:
                    px = int(rng.uniform(gx0 + 12, gx1 - 14))
                    stalk = int(rng.uniform(40, 66))
                    d.line((px, gy1, px, gy1 - stalk), fill=(52, 42, 36), width=2)
                    d.ellipse((px - 8, gy1 - stalk - 7, px + 8, gy1 - stalk + 7), fill=(255, 226, 170))
                    de.ellipse((px - 8, gy1 - stalk - 7, px + 8, gy1 - stalk + 7), fill=255)
            else:
                dark = rng.uniform(0.7, 1.15)
                for y in range(gy0, gy1):
                    t = (y - gy0) / max(gy1 - gy0, 1)
                    d.line((gx0, y, gx1, y), fill=(int((64 - 34 * t) * dark), int((112 - 56 * t) * dark), int((118 - 52 * t) * dark)))
                dr.rectangle((gx0, gy0, gx1, gy1), fill=(255, 40, 80))
                if rng.random() < 0.3:
                    px = int(rng.uniform(gx0 + 14, gx1 - 22))
                    for _ in range(5):
                        ox, oy, rad = rng.uniform(-12, 12), rng.uniform(-14, 10), rng.uniform(5, 10)
                        d.ellipse((px + ox - rad, gy1 - 28 + oy - rad, px + ox + rad, gy1 - 28 + oy + rad), fill=(26, 56, 38))
                    d.rectangle((px - 7, gy1 - 13, px + 7, gy1), fill=(60, 48, 38))
            curtains = rng.random()
            if curtains < 0.5:
                cw = int(rng.uniform(10, 34))
                tone = int(rng.uniform(190, 236))
                color = (tone, int(tone * 0.95), int(tone * 0.84))
                side = rng.integers(3)
                if side != 1:
                    d.rectangle((gx0, gy0, gx0 + cw, gy1), fill=color)
                    if lit:
                        de.rectangle((gx0, gy0, gx0 + cw, gy1), fill=120)
                    dr.rectangle((gx0, gy0, gx0 + cw, gy1), fill=(255, 220, 0))
                if side != 0:
                    d.rectangle((gx1 - cw, gy0, gx1, gy1), fill=color)
                    if lit:
                        de.rectangle((gx1 - cw, gy0, gx1, gy1), fill=120)
                    dr.rectangle((gx1 - cw, gy0, gx1, gy1), fill=(255, 220, 0))
            elif curtains < 0.68:
                blind = int(rng.uniform(16, 56))
                d.rectangle((gx0, gy0, gx1, gy0 + blind), fill=(206, 198, 170))
                de.rectangle((gx0, gy0, gx1, gy0 + blind), fill=70 if lit else 0)
                dr.rectangle((gx0, gy0, gx1, gy0 + blind), fill=(255, 220, 0))
            d.rectangle((gx0 - 3, gy0 - 3, gx1 + 3, gy0), fill=frame)
            d.rectangle((gx0 - 3, gy1, gx1 + 3, gy1 + 3), fill=frame)
            d.rectangle((gx0 - 3, gy0, gx0, gy1), fill=frame)
            d.rectangle((gx1, gy0, gx1 + 3, gy1), fill=frame)
            mx = (gx0 + gx1) // 2
            d.rectangle((mx - 1, gy0, mx + 1, gy1), fill=frame)
            de.rectangle((mx - 1, gy0, mx + 1, gy1), fill=0)
            d.rectangle((gx0, gy0 + 22, gx1, gy0 + 23), fill=frame)
            de.rectangle((gx0, gy0 + 22, gx1, gy0 + 23), fill=0)
            d.rectangle((x0 + 4, y1 - 17, x1 - 4, y1 - 13), fill=(226, 224, 208))
    a = np.array(albedo).astype(float) / 255
    n = noise(size, size, 1.3, 102)
    a = a * (0.9 + n[..., None] * 0.2)
    save("facade", a, 86)
    save("facade_emissive", np.stack([np.array(emissive) / 255] * 3, -1), 82)
    save("facade_orm", np.array(orm).astype(float) / 255, 82)


def leaf_shape(img, cx, cy, length, width, angle, shade, tint=(1.0, 1.0, 1.0)):
    size = int(length * 2.4)
    leaf = Image.new("RGBA", (size * 4, size * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(leaf)
    c = size * 2
    pts_top, pts_bot = [], []
    steps = 14
    span = length * 4
    for i in range(steps + 1):
        t = i / steps
        half = width * 4 * np.sin(np.pi * t ** 0.72) * (1 - t * 0.18)
        x = c - span * 0.5 + span * t
        pts_top.append((x, c - half))
        pts_bot.append((x, c + half))
    color = tuple(int(255 * min(1, shade * v)) for v in tint) + (255,)
    d.polygon(pts_top + pts_bot[::-1], fill=color)
    rib = tuple(int(255 * min(1, shade * 1.3 * v)) for v in tint) + (255,)
    d.line((c - span * 0.5, c, c + span * 0.5, c), fill=rib, width=max(int(width * 0.3), 1))
    dark = tuple(int(255 * shade * 0.8 * v) for v in tint) + (255,)
    for i in range(1, 7):
        t = i / 7
        half = width * 4 * np.sin(np.pi * t ** 0.72) * (1 - t * 0.18)
        x = c - span * 0.5 + span * t
        d.line((x, c, x + span * 0.08, c - half * 0.9), fill=dark, width=max(int(width * 0.14), 1))
        d.line((x, c, x + span * 0.08, c + half * 0.9), fill=dark, width=max(int(width * 0.14), 1))
    leaf = leaf.resize((size, size), Image.LANCZOS).rotate(angle, Image.BICUBIC)
    img.alpha_composite(leaf, (max(int(cx - size / 2), 0), max(int(cy - size / 2), 0)))


def broadleaf():
    size = 512
    rng = np.random.default_rng(111)
    img = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
    lobes = rng.uniform(0, 6.28, 5)
    for layer in range(3):
        for _ in range(74 + layer * 22):
            angle = rng.uniform(0, 6.283)
            outline = 0.6 + sum(np.sin(angle * (k + 2) + lobes[k]) for k in range(5)) * 0.06
            r = rng.uniform(0, 1) ** 0.6 * outline * size * 1.34
            cx, cy = size + np.cos(angle) * r, size + np.sin(angle) * r
            length = rng.uniform(84, 150)
            width = length * rng.uniform(0.24, 0.36)
            shade = min(1, (0.46 + layer * 0.22) * rng.uniform(0.8, 1.2))
            leaf_shape(img, cx, cy, length, width, np.degrees(-angle) + rng.uniform(-50, 50), shade)
    save_card("broadleaf", img.resize((size, size), Image.LANCZOS), 0.7)


def understory():
    w, h = 512, 512
    img = Image.new("RGBA", (w * 2, h * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = w
    steps = 40
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        half = w * 0.92 * np.sin(np.pi * t ** 0.62) ** 0.8
        y = h * 2 * (1 - t)
        left.append((cx - half, y))
        right.append((cx + half, y))
    d.polygon(left + right[::-1], fill=(232, 232, 232, 255))
    for i in range(1, 15):
        t = i / 15
        half = w * 0.92 * np.sin(np.pi * t ** 0.62) ** 0.8
        y = h * 2 * (1 - t)
        for side in (-1, 1):
            d.line((cx, y, cx + side * half, y - h * 0.16), fill=(176, 176, 170, 255), width=5)
    d.line((cx, h * 2, cx, 0), fill=(255, 255, 236, 255), width=12)
    save_card("bigleaf", img.resize((w, h), Image.LANCZOS), 0.7)


def frond(name, w, h, leaflets, width_scale, seed):
    rng = np.random.default_rng(seed)
    img = Image.new("RGBA", (w * 2, h * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = w
    for i in range(leaflets):
        t = (i + 0.5) / leaflets
        y = h * 2 * (1 - t) - 4
        reach = w * 0.96 * np.sin(np.pi * min(t * 1.05 + 0.06, 1)) ** 0.7 * width_scale
        for side in (-1, 1):
            tip = (cx + side * reach, y - h * 0.09 - rng.uniform(0, h * 0.02))
            thick = h * 2 / leaflets * 0.42
            shade = rng.uniform(0.74, 1)
            color = (int(255 * shade), int(255 * shade), int(255 * shade), 255)
            d.polygon([(cx, y - thick), (cx, y + thick), tip], fill=color)
    d.line((cx, h * 2, cx, 0), fill=(214, 214, 196, 255), width=max(w // 34, 2))
    save_card(name, img.resize((w, h), Image.LANCZOS), 0.75)


def vine():
    w, h = 256, 1024
    rng = np.random.default_rng(131)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for strand in range(5):
        x = w * (0.2 + strand * 0.15) + rng.uniform(-8, 8)
        end = h * rng.uniform(0.6, 1)
        pts, y = [(x, 0)], 0.0
        while y < end:
            y += rng.uniform(26, 60)
            x = np.clip(x + rng.uniform(-13, 13), 16, w - 16)
            pts.append((x, y))
        d.line(pts, fill=(170, 170, 120, 255), width=3)
        for p in pts[1:]:
            for _ in range(5):
                ox, oy = rng.uniform(-30, 30), rng.uniform(-30, 30)
                leaf_shape(img, np.clip(p[0] + ox, 26, w - 26), max(p[1] + oy, 26), rng.uniform(13, 26), rng.uniform(5, 10), rng.uniform(0, 360), rng.uniform(0.58, 1))
    save_card("vine", img, 0.7)


def tuft():
    size = 256
    rng = np.random.default_rng(141)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for _ in range(60):
        x0 = rng.uniform(size * 0.12, size * 0.88)
        height = rng.uniform(0.45, 0.98) * size
        lean = rng.uniform(-0.34, 0.34) * height
        width = rng.uniform(3, 6)
        shade = rng.uniform(0.62, 1)
        color = (int(255 * shade), int(255 * shade), int(255 * shade), 255)
        d.polygon([(x0 - width, size), (x0 + width, size), (x0 + lean, size - height)], fill=color)
    save_card("tuft", img, 0.8)


def blossom():
    size = 512
    rng = np.random.default_rng(151)
    img = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    centers = [(rng.uniform(170, size * 2 - 170), rng.uniform(170, size * 2 - 170), rng.uniform(70, 170)) for _ in range(16)]
    for cx, cy, spread in centers:
        for _ in range(46):
            angle, r = rng.uniform(0, 6.283), rng.uniform(0, 1) ** 0.7 * spread
            x, y = cx + np.cos(angle) * r, cy + np.sin(angle) * r
            s = rng.uniform(7, 16)
            shade = int(255 * rng.uniform(0.66, 1))
            for petal in range(4):
                a = petal * 1.5708 + angle
                px, py = x + np.cos(a) * s * 0.7, y + np.sin(a) * s * 0.7
                d.ellipse((px - s * 0.7, py - s * 0.7, px + s * 0.7, py + s * 0.7), fill=(shade, shade, shade, 255))
            d.ellipse((x - s * 0.24, y - s * 0.24, x + s * 0.24, y + s * 0.24), fill=(255, 244, 150, 255))
    save_card("blossom", img.resize((size, size), Image.LANCZOS), 0.8)


def mosaic():
    size = 1024
    y, x = np.mgrid[0:size, 0:size]
    u = (x + 0.5) / size * 2 - 1
    v = (y + 0.5) / size * 2 - 1
    r = np.sqrt(u * u + v * v)
    t = np.arctan2(v, u)
    n = noise(size, size, 0.8, 161)
    joint_r = np.abs(((r * 26) % 1) - 0.5) * 2
    sectors = np.floor(r * 26) * 4 + 16
    joint_t = np.abs(((t / (2 * np.pi) * sectors) % 1) - 0.5) * 2
    mask = smooth(0.0, 0.16, 1 - joint_r) * smooth(0.0, 0.16, 1 - joint_t)
    ivory = rgb(214, 208, 184)
    terra = rgb(160, 92, 58)
    jade = rgb(54, 118, 96)
    gold = rgb(202, 158, 78)
    a = np.ones((size, size, 3)) * ivory
    star = np.abs(np.cos(t * 6)) ** 0.5
    a = mix(a, terra, ((r < 0.48 * (0.45 + star * 0.55)) & (r > 0.12)).astype(float))
    a = mix(a, jade, ((r < 0.72) & (r > 0.66)).astype(float))
    a = mix(a, gold, ((r < 0.12) & (r > 0.09)).astype(float))
    a = mix(a, jade, (r < 0.09).astype(float))
    petals = np.abs(np.sin(t * 12))
    a = mix(a, terra, ((r > 0.76) & (r < 0.76 + 0.14 * petals)).astype(float))
    a = mix(a, gold, ((r > 0.93) & (r < 0.96)).astype(float))
    a = a * (0.82 + n[..., None] * 0.34)
    a = mix(np.ones((size, size, 3)) * rgb(70, 66, 54), a, mask)
    alpha = 1 - smooth(0.985, 0.995, r)
    save("mosaic", np.concatenate([a, alpha[..., None]], -1))
    save("mosaic_normal", normal_map(mask * 1.1 + n * 0.25, 2), 82)


for generate in (plaster, concrete, wood, deck, pavers, ashlar, bark, ground, strata, water, facade, broadleaf,
                 understory, vine, tuft, blossom, mosaic):
    generate()
frond("palm", 256, 1024, 44, 0.92, 121)
frond("fern", 256, 768, 34, 0.86, 122)
