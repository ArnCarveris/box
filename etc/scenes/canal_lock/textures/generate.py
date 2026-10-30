#!/usr/bin/env python3
"""Generates the textures of the canal lock scene into this folder. Needs numpy and Pillow.

Run it from anywhere: python3 etc/scenes/canal_lock/textures/generate.py
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))


def noise(h, w, beta, seed):
    """Periodic noise from 0 to 1 whose spectrum falls with frequency to the power of beta."""
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
    """Blur a height or mask with wrap around, so tiling stays seamless."""
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi * radius) ** 2 * (fx * fx + fy * fy))
    return np.fft.irfft2(np.fft.rfft2(a) * g, s=(h, w))


def normal_map(height, strength):
    """Tangent space normals of a periodic height field, with green pointing up the image."""
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


def save(name, a, quality=86):
    a = np.clip(a, 0, 1)
    img = Image.fromarray((a * 255 + 0.5).astype(np.uint8))
    if a.ndim == 3 and a.shape[2] == 4:
        img.save(os.path.join(OUT, name + ".png"), optimize=True)
    else:
        img.save(os.path.join(OUT, name + ".jpg"), quality=quality, subsampling=0)


def blocks(h, w, courses, per_course, gap, seed, jitter=0.25, stagger=0.5):
    """Coursed blocks: a mask that is 1 on a block and 0 in the joints, and a value per block."""
    rng = np.random.default_rng(seed)
    mask = np.zeros((h, w))
    ids = np.zeros((h, w))
    ch = h / courses
    for c in range(courses):
        y0, y1 = int(c * ch), int((c + 1) * ch)
        cuts = (np.arange(per_course) + rng.uniform(-jitter, jitter, per_course)) / per_course
        cuts = (cuts + (c % 2) * stagger / per_course) % 1
        cuts = np.sort(cuts)
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


def ashlar(h, w, courses, per_course, seed, base, joint):
    """Weathered stone blocks: the albedo and the height."""
    mask, ids = blocks(h, w, courses, per_course, 2.4, seed)
    n_fine = noise(h, w, 0.7, seed + 1)
    n_big = noise(h, w, 1.6, seed + 2)
    tone = 0.84 + ids * 0.2 + (n_big - 0.5) * 0.3 + (n_fine - 0.5) * 0.22
    albedo = mix(joint[None, None, :] * np.ones((h, w, 3)), base[None, None, :] * tone[..., None], mask)
    height = mask * (0.75 + ids * 0.25) + n_fine * 0.22 + n_big * 0.2
    return albedo, height


def chamber_wall():
    """The wall of the lock chamber, 13 by 4.5 units: dry above the high water line, wet and green
    down to the low water line, dark and slimy below it."""
    h, w = 354, 1024
    albedo, height = ashlar(h, w, 8, 22, 11, rgb(178, 170, 150), rgb(70, 66, 58))
    y = np.linspace(0, 4.5, h)[:, None] * np.ones((1, w))
    wobble = (noise(h, w, 1.5, 12) - 0.5) * 0.4
    drip = (noise(h, w, 0.9, 13) - 0.5) * 0.25
    wet = smooth(0.25, 0.5, y + wobble + drip)
    deep = smooth(2.1, 2.6, y + wobble)
    moss = noise(h, w, 1.1, 14)
    algae = mix(np.full((h, w, 3), 0.0) + rgb(46, 54, 34), rgb(74, 88, 44) * np.ones((h, w, 3)), moss)
    streak = noise(h, w, 1.2, 15)
    streak = np.mean(streak, axis=0, keepdims=True) * np.ones((h, 1))
    albedo = albedo * (1 - 0.3 * smooth(0, 0.5, y) * smooth(0.45, 0.8, streak))[..., None]
    albedo = mix(albedo, albedo * 0.5 + algae * 0.5, wet)
    albedo = mix(albedo, rgb(58, 66, 46) * (0.7 + moss[..., None] * 0.6), deep * 0.7)
    orm = np.ones((h, w, 3))
    orm[..., 1] = 0.92 - 0.55 * wet
    orm[..., 2] = 0
    save("chamber_wall", albedo)
    save("chamber_wall_normal", normal_map(height, 3.4), 90)
    save("chamber_wall_orm", orm, 80)


def masonry():
    albedo, height = ashlar(512, 512, 8, 6, 21, rgb(172, 164, 146), rgb(74, 70, 62))
    save("ashlar", albedo)
    save("ashlar_normal", normal_map(height, 3.4), 90)


def stone():
    n = noise(256, 256, 1.0, 31)
    f = noise(256, 256, 0.45, 32)
    tone = 0.76 + n * 0.32 + (f - 0.5) * 0.12
    save("stone", np.ones((256, 256, 3)) * tone[..., None])
    save("stone_normal", normal_map(n * 1.2 + f * 0.25, 3.0), 88)


def brick():
    h, w = 512, 512
    mask, ids = blocks(h, w, 12, 5, 3, 41, jitter=0.0)
    n = noise(h, w, 0.8, 42)
    big = noise(h, w, 1.6, 43)
    colors = mix(rgb(150, 66, 44)[None, None] * np.ones((h, w, 3)), rgb(104, 50, 40), ids)
    colors = mix(colors, rgb(172, 98, 66), smooth(0.72, 1.0, ids))
    colors = colors * (0.82 + n[..., None] * 0.3) * (0.82 + big[..., None] * 0.34)
    albedo = mix(np.ones((h, w, 3)) * rgb(128, 120, 106) * (0.8 + n[..., None] * 0.3), colors, mask)
    save("brick", albedo)
    save("brick_normal", normal_map(mask * 1.1 + n * 0.18, 3.2), 90)


def wood():
    """Planks with the grain along U and bright enough for a material to tint."""
    h, w = 512, 512
    boards = 5
    rng = np.random.default_rng(51)
    fine = noise(h, w, 0.8, 52)
    grain = blur(fine, 0) * 0
    stretched = np.array(Image.fromarray((fine * 255).astype(np.uint8)).resize((w // 16, h), Image.BILINEAR))
    grain = np.array(Image.fromarray(stretched).resize((w, h), Image.BICUBIC)) / 255
    grain = (grain - grain.min()) / (grain.max() - grain.min())
    rings = 0.5 + 0.5 * np.sin(grain * 34)
    tone = np.ones((h, w))
    mask = np.ones((h, w))
    for b in range(boards):
        y0, y1 = int(b * h / boards), int((b + 1) * h / boards)
        tone[y0:y1] = 0.74 + rng.random() * 0.3
        d = np.minimum(np.arange(y1 - y0), y1 - y0 - 1 - np.arange(y1 - y0))[:, None]
        mask[y0:y1] = smooth(1, 4, d)
    v = tone * (0.72 + rings * 0.22 + grain * 0.18) * (0.1 + 0.9 * mask)
    save("wood", np.ones((h, w, 3)) * v[..., None] * rgb(255, 240, 222))
    save("wood_normal", normal_map(mask * 1.2 + rings * 0.09 + grain * 0.14, 3), 88)


def table():
    """Walnut without plank gaps, the grain along U, for the table under the diorama."""
    h, w = 512, 512
    fine = noise(h, w, 1.0, 141)
    stretched = np.array(Image.fromarray((fine * 255).astype(np.uint8)).resize((w // 24, h), Image.BILINEAR))
    grain = np.array(Image.fromarray(stretched).resize((w, h), Image.BICUBIC)) / 255
    grain = (grain - grain.min()) / (grain.max() - grain.min())
    v = 0.66 + grain * 0.34 + np.sin(grain * 40)[..., ] * 0.05
    save("table", np.ones((h, w, 3)) * v[..., None])


def slate():
    """Rows of slates: each slate rises toward the row below, and the rows overlap."""
    h, w = 512, 512
    rows, per_row = 9, 8
    mask, ids = blocks(h, w, rows, per_row, 1.2, 61, jitter=0.1)
    n = noise(h, w, 0.8, 62)
    big = noise(h, w, 1.4, 63)
    ramp = (np.arange(h)[:, None] * rows / h) % 1
    moss = smooth(0.66, 0.82, big + n * 0.14) * (0.5 + ramp * 0.5)
    tone = 0.64 + ids * 0.36 + (n - 0.5) * 0.26
    albedo = rgb(84, 88, 98)[None, None] * tone[..., None] * (0.25 + 0.75 * mask[..., None])
    albedo = mix(albedo, rgb(92, 104, 60) * (0.7 + n[..., None] * 0.6), moss * 0.55)
    save("slate", albedo)
    save("slate_normal", normal_map(mask * 0.6 + ramp * 0.9 + n * 0.14, 3.6), 88)


def plaster():
    n = noise(256, 256, 1.1, 71)
    f = noise(256, 256, 0.6, 72)
    tone = 0.84 + n * 0.2 + (f - 0.5) * 0.05
    save("plaster", np.ones((256, 256, 3)) * tone[..., None])
    save("plaster_normal", normal_map(n * 1.1 + f * 0.12, 0.9), 86)


def bark():
    h, w = 256, 256
    n = noise(h, w, 0.8, 81)
    ridges = np.array(Image.fromarray((n * 255).astype(np.uint8)).resize((w, h // 16)).resize((w, h), Image.BICUBIC)) / 255
    ridges = (ridges - ridges.min()) / (ridges.max() - ridges.min())
    groove = smooth(0.25, 0.7, ridges)
    save("bark", np.ones((h, w, 3)) * (0.42 + groove[..., None] * 0.62) * (0.85 + n[..., None] * 0.3))
    save("bark_normal", normal_map(groove * 1.6 + n * 0.25, 4), 86)


def strata():
    """The cut face of the diorama from the turf (top) to bedrock, tiling in U."""
    h, w = 256, 1024
    n = noise(h, w, 1.2, 91)
    f = noise(h, w, 0.6, 92)
    y = np.linspace(0, 4.9, h)[:, None] * np.ones((1, w))
    wave = (noise(h, w, 2.2, 93) - 0.5) * 0.9
    d = y + wave
    top = rgb(58, 42, 30)
    clay = rgb(136, 96, 62)
    sand = rgb(170, 138, 94)
    gravel = rgb(112, 100, 86)
    rock = rgb(86, 82, 78)
    a = np.ones((h, w, 3)) * top
    a = mix(a, clay, smooth(0.5, 0.8, d))
    a = mix(a, sand, smooth(1.9, 2.0, d))
    a = mix(a, gravel, smooth(2.6, 2.7, d))
    a = mix(a, rock, smooth(3.4, 3.5, d))
    a = a * (0.72 + n[..., None] * 0.4) * (0.86 + f[..., None] * 0.28)
    rng = np.random.default_rng(94)
    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    for _ in range(420):
        px, py = rng.uniform(0, w), rng.uniform(h * 0.42, h)
        r = rng.uniform(1.5, 6)
        tone = int(rng.uniform(90, 255))
        for ox in (-w, 0, w):
            draw.ellipse((px + ox - r * 1.5, py - r, px + ox + r * 1.5, py + r), fill=tone)
    pebbles = np.array(img) / 255
    pebble_mask = (pebbles > 0)[..., None]
    a = np.where(pebble_mask, rgb(150, 142, 128) * pebbles[..., None] + 0.05, a)
    crack = smooth(0.0, 0.04, np.abs(noise(h, w, 1.6, 95) - 0.5)) * 0.4 + 0.6
    a = mix(a, a * crack[..., None], smooth(3.4, 3.6, d))
    height = n * 0.8 + f * 0.3 + blur(pebbles, 1) * 1.1 + smooth(3.4, 3.5, d) * (1 - crack) * -2
    save("strata", a)
    save("strata_normal", normal_map(height, 3.2), 86)


def water():
    """Small wind ripples: bands of waves in a few directions, tiling."""
    h, w = 512, 512
    yy, xx = np.mgrid[0:h, 0:w] / h
    height = np.zeros((h, w))
    rng = np.random.default_rng(101)
    warp = noise(h, w, 1.8, 102) * 2 * np.pi
    for _ in range(26):
        fx, fy = rng.integers(-12, 13), rng.integers(4, 18)
        height += np.sin(2 * np.pi * (fx * xx + fy * yy) + rng.uniform(0, 6.28) + warp * 1.4) / (fx * fx + fy * fy) ** 0.5
    height = height * 0.6 + noise(h, w, 1.3, 103) * 3
    height = (height - height.min()) / (height.max() - height.min())
    save("water_normal", normal_map(height, 3.2), 88)


def ground():
    """The grass of the whole diorama, 42 by 20 units, with the worn paths painted in. The
    pixel at u, v is the world position x = -21 + 42 u, z = -10 + 20 v."""
    h, w = 976, 2048
    su = w / 42
    x = (np.arange(w) + 0.5) / su - 21
    z = (np.arange(h) + 0.5) / su - 10
    X, Z = np.meshgrid(x, z)
    big = noise(h, w, 1.9, 111)
    mid = noise(h, w, 1.2, 112)
    fine = noise(h, w, 0.35, 113)
    clover = noise(h, w, 0.9, 114)
    a = mix(rgb(70, 102, 40) * np.ones((h, w, 3)), rgb(128, 148, 62), big * 1.1 - 0.05)
    a = mix(a, rgb(52, 84, 36), smooth(0.56, 0.74, mid) * 0.55)
    a = mix(a, rgb(146, 150, 76), smooth(0.68, 0.8, clover) * 0.4)
    a = a * (0.74 + fine[..., None] * 0.52)
    wobble = (noise(h, w, 1.6, 115) - 0.5) * 0.5 + (fine - 0.5) * 0.28
    def worn(x0, x1, z0, z1, soft=0.34):
        dx = np.maximum(x0 - X, X - x1)
        dz = np.maximum(z0 - Z, Z - z1)
        return 1 - smooth(-soft, soft, np.maximum(dx, dz) + wobble)
    path = np.maximum.reduce([
        worn(-21.5, 21.5, 3.45, 4.75),
        worn(1.2, 21.5, -4.8, -3.55),
        worn(-1.45, -0.35, -5.4, -3.55) * 0,
        worn(2.6, 4.15, -5.3, -3.6),
        worn(-9.6, -6.5, 7.15, 8.85, 0.28),
        worn(-9.6, -8.2, 4.5, 8.8, 0.3),
        worn(6.4, 8.6, 4.5, 6.3, 0.3),
    ])
    pebbles = noise(h, w, 0.12, 116)
    dirt = mix(rgb(148, 122, 86) * np.ones((h, w, 3)), rgb(186, 164, 124), smooth(0.4, 0.8, mid))
    dirt = dirt * (0.72 + pebbles[..., None] * 0.56)
    dirt = mix(dirt, rgb(214, 202, 176), smooth(0.8, 0.92, pebbles) * 0.8)
    a = mix(a, dirt, path)
    rng = np.random.default_rng(117)
    img = Image.new("RGB", (w, h), 0)
    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    draw_m = ImageDraw.Draw(mask)
    colors = [(248, 244, 226), (246, 210, 72), (230, 120, 170), (150, 130, 230)]
    for i in range(4200):
        px, py = rng.uniform(0, w), rng.uniform(0, h)
        c = colors[rng.choice(4, p=[0.62, 0.26, 0.07, 0.05])]
        r = rng.uniform(0.7, 1.7)
        draw.ellipse((px - r, py - r, px + r, py + r), fill=c)
        draw_m.ellipse((px - r, py - r, px + r, py + r), fill=255)
    flowers = np.array(img) / 255
    fmask = np.array(mask) / 255 * smooth(0.4, 0.62, big) * (1 - path)
    a = mix(a, flowers, fmask)
    edge = np.maximum.reduce([worn(-22, 22, -3.3, 3.3, 0.9) * 0.55])
    a = a * (1 - edge[..., None] * 0.36)
    save("ground", a, 84)


def leaves():
    """A clump of leaves on transparency with a lobed outline, for the foliage cards."""
    size = 256
    rng = np.random.default_rng(121)
    img = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
    lobes = rng.uniform(0, 6.28, 5)
    for layer in range(3):
        for i in range(420):
            angle = rng.uniform(0, 6.283)
            outline = 0.62 + sum(np.sin(angle * (k + 2) + lobes[k]) for k in range(5)) * 0.06
            r = rng.uniform(0, 1) ** 0.55 * outline * size * 0.92
            cx, cy = size + np.cos(angle) * r, size + np.sin(angle) * r
            length, width = rng.uniform(13, 24), rng.uniform(6, 10)
            shade = min(1, (0.5 + layer * 0.23) * rng.uniform(0.76, 1.2))
            color = (int(255 * shade), int(255 * shade), int(255 * shade), 255)
            leaf = Image.new("RGBA", (60, 60), (0, 0, 0, 0))
            ImageDraw.Draw(leaf).ellipse((30 - length, 30 - width, 30 + length, 30 + width), fill=color)
            leaf = leaf.rotate(rng.uniform(0, 360), Image.BICUBIC)
            img.alpha_composite(leaf, (int(cx - 30), int(cy - 30)))
    img = img.resize((size, size), Image.LANCZOS)
    a = np.array(img).astype(float) / 255
    a[..., :3] = np.where(a[..., 3:] > 0.01, a[..., :3], 0.7)
    save("leaves", a)


def tuft():
    """Blades of grass on transparency, rooted at the bottom edge."""
    size = 256
    rng = np.random.default_rng(131)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i in range(70):
        x0 = rng.uniform(size * 0.12, size * 0.88)
        height = rng.uniform(0.45, 0.98) * size
        lean = rng.uniform(-0.3, 0.3) * height
        width = rng.uniform(2.5, 5)
        shade = rng.uniform(0.62, 1)
        color = (int(255 * shade), int(255 * shade), int(255 * shade), 255)
        d.polygon([(x0 - width, size), (x0 + width, size), (x0 + lean, size - height)], fill=color)
    a = np.array(img).astype(float) / 255
    a[..., :3] = np.where(a[..., 3:] > 0.01, a[..., :3], 0.8)
    save("tuft", a)


for generate in (chamber_wall, masonry, stone, brick, wood, table, slate, plaster, bark, strata, water, ground, leaves, tuft):
    generate()
