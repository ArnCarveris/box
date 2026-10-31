#!/usr/bin/env python3
"""Generates the textures, environment and icons of the space station scene into this folder.

Needs numpy and Pillow. Run it from anywhere: python3 etc/scenes/space_station/assets/generate.py
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))


def noise(size, beta, seed):
    """Periodic noise from 0 to 1 whose spectrum falls with frequency to the power of beta."""
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(size)[:, None] * size
    fx = np.fft.rfftfreq(size)[None, :] * size
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    phase = rng.uniform(0, 2 * np.pi, f.shape)
    v = np.fft.irfft2(np.exp(1j * phase) / f ** beta, s=(size, size))
    return (v - v.min()) / (v.max() - v.min())


def normal_from_height(height, strength):
    """A tangent space normal map as bytes, wrapping at the edges, green up the image."""
    dx = np.roll(height, -1, 1) - np.roll(height, 1, 1)
    dy = np.roll(height, -1, 0) - np.roll(height, 1, 0)
    nx, ny, nz = -dx * strength, dy * strength, np.ones_like(height)
    n = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.dstack([(c / n * 0.5 + 0.5) * 255 for c in (nx, ny, nz)]).astype(np.uint8)


def save_rgb(name, data, quality=88):
    Image.fromarray(np.clip(data, 0, 255).astype(np.uint8)).save(
        os.path.join(OUT, name), quality=quality, subsampling=0)


def split_panels(rng, x0, y0, x1, y1, depth, out):
    """Cuts a rectangle into hull panels of mixed sizes."""
    w, h = x1 - x0, y1 - y0
    if depth == 0 or (max(w, h) < 170 and rng.random() < 0.4):
        out.append((x0, y0, x1, y1))
        return
    if w > h:
        cut = x0 + w * rng.choice([0.25, 0.5, 0.75])
        split_panels(rng, x0, y0, cut, y1, depth - 1, out)
        split_panels(rng, cut, y0, x1, y1, depth - 1, out)
    else:
        cut = y0 + h * rng.choice([0.25, 0.5, 0.75])
        split_panels(rng, x0, y0, x1, cut, depth - 1, out)
        split_panels(rng, x0, cut, x1, y1, depth - 1, out)


def hull(size=1024):
    rng = np.random.default_rng(11)
    panels = []
    split_panels(rng, 0, 0, size, size, 5, panels)
    tone = Image.new("L", (size, size), 128)
    seam = Image.new("L", (size, size), 255)
    rivet = Image.new("L", (size, size), 0)
    dt, ds, dr = ImageDraw.Draw(tone), ImageDraw.Draw(seam), ImageDraw.Draw(rivet)
    for x0, y0, x1, y1 in panels:
        dt.rectangle([x0, y0, x1, y1], fill=int(rng.integers(112, 146)))
        ds.rectangle([x0, y0, x1, y1], outline=0, width=3)
        if rng.random() < 0.55 and min(x1 - x0, y1 - y0) > 60:
            count_x = max(int((x1 - x0) / 34), 2)
            count_y = max(int((y1 - y0) / 34), 2)
            for i in range(count_x + 1):
                for y in (y0 + 10, y1 - 10):
                    x = x0 + 10 + (x1 - x0 - 20) * i / count_x
                    dr.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5], fill=255)
            for i in range(count_y + 1):
                for x in (x0 + 10, x1 - 10):
                    y = y0 + 10 + (y1 - y0 - 20) * i / count_y
                    dr.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5], fill=255)
    tone = np.asarray(tone, float) / 128
    seam_soft = np.asarray(seam.filter(ImageFilter.GaussianBlur(1.3)), float) / 255
    grime_ao = np.asarray(seam.filter(ImageFilter.GaussianBlur(12)), float) / 255
    rivets = np.asarray(rivet.filter(ImageFilter.GaussianBlur(0.8)), float) / 255
    streak = noise(size, 1.6, 3)
    fine = noise(size, 0.7, 4)
    wear = np.clip((noise(size, 1.2, 5) - 0.58) * 6, 0, 1)
    v = tone * (0.84 + 0.2 * streak) * (0.6 + 0.4 * seam_soft) * (0.82 + 0.18 * grime_ao)
    v = v * (1 - 0.22 * wear) + rivets * 0.08 + (fine - 0.5) * 0.03
    save_rgb("hull.jpg", np.dstack([v * 214, v * 218, v * 224]))
    height = seam_soft * 2.2 + rivets * 1.2 + tone * 0.5 + fine * 0.06
    save_rgb("hull_normal.jpg", normal_from_height(height, 1.5), 90)
    rough = np.clip(0.62 + 0.45 * streak + 0.3 * wear - 0.15 * rivets, 0, 1)
    metal = np.clip(0.8 + 0.2 * wear, 0, 1)
    occl = np.clip(0.25 + 0.75 * seam_soft, 0, 1) * (0.7 + 0.3 * grime_ao)
    save_rgb("hull_orm.jpg", np.dstack([occl * 255, rough * 255, metal * 255]), 90)


def deck(size=1024):
    tone = Image.new("L", (size, size), 110)
    relief = Image.new("L", (size, size), 128)
    paint = Image.new("L", (size, size), 0)
    dt, dr, dp = ImageDraw.Draw(tone), ImageDraw.Draw(relief), ImageDraw.Draw(paint)
    m = size * 0.035
    dr.rectangle([m, m, size - m, size - m], fill=150)
    dt.rectangle([m, m, size - m, size - m], fill=132)
    step = size / 8
    for i in range(1, 8):
        p = i * step
        dr.line([p, m, p, size - m], fill=92, width=4)
        dr.line([m, p, size - m, p], fill=92, width=4)
        dt.line([p, m, p, size - m], fill=78, width=4)
        dt.line([m, p, size - m, p], fill=78, width=4)
    rng = np.random.default_rng(23)
    for i in range(8):
        for j in range(8):
            x0, y0 = i * step + 8, j * step + 8
            dt.rectangle([x0, y0, x0 + step - 16, y0 + step - 16],
                         fill=int(rng.integers(118, 144)))
            for cx, cy in ((x0 + 8, y0 + 8), (x0 + step - 24, y0 + 8),
                           (x0 + 8, y0 + step - 24), (x0 + step - 24, y0 + step - 24)):
                dr.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=190)
    c = size / 2
    for radius, width in ((size * 0.28, 7), (size * 0.25, 3)):
        dp.ellipse([c - radius, c - radius, c + radius, c + radius], outline=255, width=width)
    for a in range(0, 360, 90):
        s, k = math.sin(math.radians(a)), math.cos(math.radians(a))
        dp.line([c + k * size * 0.31, c + s * size * 0.31, c + k * size * 0.37, c + s * size * 0.37],
                fill=255, width=8)
    hazard = Image.new("L", (size, size), 0)
    dh = ImageDraw.Draw(hazard)
    band = size * 0.045
    for corner_x, corner_y, sx, sy in ((m, m, 1, 1), (size - m, m, -1, 1),
                                       (m, size - m, 1, -1), (size - m, size - m, -1, -1)):
        length = size * 0.17
        stripes = 5
        for i in range(stripes):
            a = i * length / stripes
            b = a + length / stripes * 0.55
            dh.polygon([(corner_x + sx * a, corner_y), (corner_x + sx * b, corner_y),
                        (corner_x + sx * (b + band), corner_y + sy * band),
                        (corner_x + sx * (a + band), corner_y + sy * band)], fill=255)
            dh.polygon([(corner_x, corner_y + sy * a), (corner_x, corner_y + sy * b),
                        (corner_x + sx * band, corner_y + sy * (b + band)),
                        (corner_x + sx * band, corner_y + sy * (a + band))], fill=255)
    tone = np.asarray(tone, float) / 128
    relief = np.asarray(relief.filter(ImageFilter.GaussianBlur(0.8)), float) / 255
    paint = np.asarray(paint.filter(ImageFilter.GaussianBlur(0.7)), float) / 255
    hazard = np.asarray(hazard.filter(ImageFilter.GaussianBlur(0.7)), float) / 255
    grime = noise(size, 1.5, 31)
    scratch = noise(size, 0.6, 32)
    worn = np.clip((noise(size, 1.3, 33) - 0.42) * 5, 0, 1)
    base = tone * (0.72 + 0.42 * grime) + (scratch - 0.5) * 0.05
    r = base * 158 * (1 - paint) * (1 - hazard) + paint * worn * 196 + paint * (1 - worn) * base * 158
    g = base * 164 * (1 - paint) * (1 - hazard) + paint * worn * 214 + paint * (1 - worn) * base * 164
    b = base * 172 * (1 - paint) * (1 - hazard) + paint * worn * 224 + paint * (1 - worn) * base * 172
    r += hazard * (worn * 226 + (1 - worn) * base * 150)
    g += hazard * (worn * 170 + (1 - worn) * base * 140)
    b += hazard * (worn * 40 + (1 - worn) * base * 130)
    save_rgb("deck.jpg", np.dstack([r, g, b]))
    save_rgb("deck_normal.jpg", normal_from_height(relief + scratch * 0.012, 3.2), 90)
    rough = np.clip(0.6 + 0.5 * grime - 0.18 * paint - 0.1 * hazard, 0, 1)
    occl = np.clip(relief * 1.9, 0.3, 1)
    save_rgb("deck_orm.jpg", np.dstack([occl * 255, rough * 255, np.full_like(rough, 200)]), 90)


def solar(size=512):
    cells = 6
    step = size / cells
    v = np.zeros((size, size, 3))
    yy, xx = np.mgrid[0:size, 0:size]
    u, w = (xx % step) / step, (yy % step) / step
    grid = np.minimum(np.minimum(u, 1 - u), np.minimum(w, 1 - w)) * step
    finger = (xx % (step / 14)) < 1
    bus = np.abs(w - 0.33) * step < 1.5
    bus |= np.abs(w - 0.66) * step < 1.5
    shade = (noise(size, 1.3, 41) * 0.4 + 0.8)
    tile = np.floor(xx / step) * 13 + np.floor(yy / step) * 7
    tint = 0.85 + 0.3 * ((tile % 5) / 4)
    for c, value in enumerate((12, 30, 86)):
        v[:, :, c] = value * shade * tint
    v[finger] = v[finger] * 1.5 + 16
    v[bus] = (176, 182, 190)
    v[grid < 2.5] = (8, 10, 14)
    save_rgb("solar.jpg", v)


def lattice_noise(px, py, pz, freq, seed):
    """Smooth 3D value noise from a periodic random lattice, at the points px, py, pz."""
    n = 64
    table = np.random.default_rng(seed).random((n, n, n))
    x, y, z = (px + 2) * freq, (py + 2) * freq, (pz + 2) * freq
    x0, y0, z0 = np.floor(x).astype(int), np.floor(y).astype(int), np.floor(z).astype(int)
    fx, fy, fz = x - x0, y - y0, z - z0
    fx, fy, fz = (c * c * (3 - 2 * c) for c in (fx, fy, fz))
    result = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                wx = fx if dx else 1 - fx
                wy = fy if dy else 1 - fy
                wz = fz if dz else 1 - fz
                result = result + table[(x0 + dx) % n, (y0 + dy) % n, (z0 + dz) % n] * wx * wy * wz
    return result


def fbm(px, py, pz, freq, octaves, seed, gain=0.5):
    total, amp, norm = 0, 1, 0
    for i in range(octaves):
        total = total + lattice_noise(px, py, pz, freq * 2 ** i, seed + i) * amp
        norm += amp
        amp *= gain
    return total / norm


def sphere_grid(width, height):
    lat = (np.arange(height) + 0.5) / height * np.pi
    lon = (np.arange(width) + 0.5) / width * 2 * np.pi
    lon, lat = np.meshgrid(lon, lat)
    s = np.sin(lat)
    return s * np.cos(lon), np.cos(lat), s * np.sin(lon)


def ramp(t, stops):
    keys = [s[0] for s in stops]
    return np.dstack([np.interp(t, keys, [s[1][c] for s in stops]) for c in range(3)])


def planet(width=2048, height=1024):
    px, py, pz = sphere_grid(width, height)
    warp = fbm(px, py, pz, 2.2, 4, 300) - 0.5
    land = fbm(px + warp * 0.5, py + warp * 0.4, pz - warp * 0.5, 1.5, 8, 310)
    level = 0.515
    coast = np.clip((land - level) * 90, 0, 1)
    climate = 1 - np.abs(py) + (fbm(px, py, pz, 3, 5, 330) - 0.5) * 0.5
    mountains = np.clip((land - level) * 5, 0, 1)
    detail = fbm(px, py, pz, 18, 5, 340)
    sea = ramp(np.clip((land - 0.3) / (level - 0.3), 0, 1),
               [(0, (6, 22, 62)), (0.75, (10, 44, 96)), (0.97, (24, 92, 130)), (1, (64, 150, 158))])
    dry = ramp(mountains + (detail - 0.5) * 0.3,
               [(0, (156, 128, 82)), (0.4, (136, 96, 60)), (0.8, (96, 80, 68)), (1, (190, 186, 182))])
    green = ramp(mountains + (detail - 0.5) * 0.35,
                 [(0, (44, 94, 44)), (0.45, (58, 92, 46)), (0.8, (94, 84, 66)), (1, (200, 198, 196))])
    moisture = fbm(px, py, pz, 2.6, 5, 350)
    wet = np.clip((moisture - 0.46 + (climate - 0.6) * 0.3) * 6, 0, 1)[:, :, None]
    ground = green * wet + dry * (1 - wet)
    albedo = ground * coast[:, :, None] + sea * (1 - coast[:, :, None])
    ice = np.clip((np.abs(py) - 0.86 + (detail - 0.5) * 0.2) * 16, 0, 1)[:, :, None]
    albedo = albedo * (1 - ice) + np.array([232, 240, 246]) * ice
    save_rgb("planet.jpg", albedo)

    cities = np.clip((fbm(px, py, pz, 24, 4, 370) - 0.53) * 12, 0, 1)
    region = np.clip((fbm(px, py, pz, 5, 3, 380) - 0.5) * 8, 0, 1)
    glow = cities * region * coast * (1 - ice[:, :, 0])
    save_rgb("planet_night.jpg", np.dstack([glow * 255, glow * 196, glow * 120]), 80)

    swirl = fbm(px, py, pz, 2, 4, 400) - 0.5
    cloud = fbm(px + swirl * 0.5, py * 1.9 + swirl * 0.25, pz - swirl * 0.5, 5.5, 7, 410, 0.55)
    alpha = np.clip((cloud - 0.54) * 7, 0, 1) ** 1.2
    rgba = np.dstack([np.full_like(alpha, 255), np.full_like(alpha, 255), np.full_like(alpha, 255),
                      alpha * 235]).astype(np.uint8)
    Image.fromarray(rgba).save(os.path.join(OUT, "clouds.png"), optimize=True)


def halo(size=512):
    yy, xx = np.mgrid[0:size, 0:size]
    r = np.hypot(xx - size / 2 + 0.5, yy - size / 2 + 0.5) / (size / 2)
    limb = 0.74
    out = np.exp(-np.clip(r - limb, 0, None) * 24) * 0.75 + np.exp(-np.clip(r - limb, 0, None) * 7) * 0.25
    fade = np.clip((1 - r) * 8, 0, 1)
    alpha = np.where(r < limb - 0.01, 0, out) * fade
    Image.fromarray((alpha * 255).astype(np.uint8), "L").convert("RGB").save(
        os.path.join(OUT, "halo.png"), optimize=True)


def write_hdr(path, rgb):
    height, width, _ = rgb.shape
    m = np.maximum(rgb.max(axis=2), 1e-32)
    mant, exp = np.frexp(m)
    scale = mant * 256 / m
    data = np.zeros((height, width, 4), np.uint8)
    for c in range(3):
        data[:, :, c] = np.clip(rgb[:, :, c] * scale, 0, 255)
    data[:, :, 3] = np.where(m < 1e-30, 0, exp + 128)
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (height, width))
        for y in range(height):
            f.write(bytes([2, 2, width >> 8, width & 255]))
            for c in range(4):
                row = data[y, :, c]
                x = 0
                while x < width:
                    run = 1
                    while x + run < width and run < 127 and row[x + run] == row[x]:
                        run += 1
                    if run >= 4:
                        f.write(bytes([128 + run, row[x]]))
                        x += run
                    else:
                        end = x
                        while end < width and end - x < 128:
                            if end + 3 < width and row[end] == row[end + 1] == row[end + 2] == row[end + 3]:
                                break
                            end += 1
                        f.write(bytes([end - x]) + row[x:end].tobytes())
                        x = end


def environment(width=1024, height=512):
    lat = (np.arange(height) + 0.5) / height * np.pi
    lon = (np.arange(width) + 0.5) / width * 2 * np.pi - np.pi
    lon, lat = np.meshgrid(lon, lat)
    s = np.sin(lat)
    dx, dy, dz = s * np.sin(lon), np.cos(lat), -s * np.cos(lon)
    warp = fbm(dx, dy, dz, 1.6, 4, 500) - 0.5
    cloud = fbm(dx + warp, dy - warp, dz + warp * 0.6, 1.7, 6, 510)
    dust = fbm(dx - warp, dy + warp, dz, 3.4, 5, 520)
    band = np.exp(-((dy * 0.9 + dx * 0.4 - dz * 0.1) * 2.6) ** 2)
    density = np.clip((cloud - 0.42) * 3.5, 0, 1) ** 1.6 * (0.3 + 0.9 * band)
    hue = np.clip((dust - 0.36) * 3.6, 0, 1)[:, :, None]
    color = np.array([0.04, 0.09, 0.26]) * (1 - hue) + np.array([0.2, 0.06, 0.22]) * hue
    rgb = density[:, :, None] * color * 0.42 + np.array([0.0008, 0.0012, 0.0024])
    pl = np.array([560, -390, -520], float)
    pl /= np.linalg.norm(pl)
    toward = dx * pl[0] + dy * pl[1] + dz * pl[2]
    rgb = rgb + np.clip(toward, 0, 1)[:, :, None] ** 40 * np.array([0.22, 0.42, 0.8])
    write_hdr(os.path.join(OUT, "space.hdr"), rgb)


ICON = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="128" height="128">%s</svg>'
S = 'fill="none" stroke="#fff" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"'
F = 'fill="#fff"'

ICONS = {
    "solar": f'<g {S}><path d="M10 12h44l6 26H4z"/><path d="M24.6 12l-3 26M39.4 12l3 26M7 25h50"/>'
             '<path d="M32 38v12M20 54h24"/></g>',
    "reactor": f'<g {S}><circle cx="32" cy="32" r="7"/><ellipse cx="32" cy="32" rx="27" ry="10"/>'
               '<ellipse cx="32" cy="32" rx="27" ry="10" transform="rotate(60 32 32)"/>'
               '<ellipse cx="32" cy="32" rx="27" ry="10" transform="rotate(120 32 32)"/></g>',
    "life": f'<g {S}><path d="M32 57C16 46 7 37 7 24c0-8 6-14 13-14 5 0 9 3 12 8 3-5 7-8 12-8 7 0 13 6 13 14 0 13-9 22-25 33z"/>'
            '<path d="M13 31h11l4-8 7 15 4-7h12"/></g>',
    "air": f'<g {S}><path d="M6 22h32a8 8 0 1 0-8-8"/><path d="M6 33h46a7 7 0 1 1-7 7"/>'
           '<path d="M6 44h24a7 7 0 1 1-7 7"/></g>',
    "farm": f'<g {S}><path d="M32 58V24"/><path d="M32 30C32 16 22 8 8 8c0 14 10 22 24 22z"/>'
            '<path d="M32 42c0-10 8-17 24-17 0 12-10 17-24 17z"/></g>',
    "hab": f'<g {S}><circle cx="32" cy="32" r="26"/><circle cx="32" cy="32" r="12"/>'
           '<path d="M32 6v14M32 44v14M6 32h14M44 32h14"/></g>',
    "pax": f'<g {S}><path d="M32 4c9 8 12 18 12 28v12H20V32c0-10 3-20 12-28z"/>'
           '<path d="M20 32l-10 12v8l10-4M44 32l10 12v8l-10-4M26 52h12"/><circle cx="32" cy="24" r="4"/></g>',
    "cargo": f'<g {S}><path d="M32 6l24 12v28L32 58 8 46V18z"/><path d="M8 18l24 12 24-12M32 30v28"/>'
             '<path d="M20 12l24 12"/></g>',
    "bay": f'<g {S}><path d="M6 58V22L32 8l26 14v36"/><path d="M16 58V30h32v28"/>'
           '<path d="M16 40h32M16 49h32"/></g>',
    "sell": f'<g {S}><path d="M10 16h44M24 16V8h16v8M15 16l3 40h28l3-40"/><path d="M27 27v18M37 27v18"/></g>',
    "credit": f'<g {S}><circle cx="32" cy="32" r="26"/><path d="M42 23c-3-4-6-5-10-5-8 0-14 6-14 14s6 14 14 14c4 0 7-1 10-5"/></g>',
    "power": f'<path {F} d="M37 3L11 37h17l-3 24 28-36H35z"/>',
    "people": f'<g {S}><circle cx="24" cy="20" r="10"/><path d="M6 56c0-13 8-20 18-20s18 7 18 20"/>'
              '<path d="M42 11a10 10 0 0 1 0 18M48 37c6 3 10 9 10 19"/></g>',
    "food": f'<g {S}><path d="M32 18c-8-6-22-3-22 13 0 14 10 27 16 27 3 0 4-2 6-2s3 2 6 2c6 0 16-13 16-27 0-16-14-19-22-13z"/>'
            '<path d="M32 18c0-6 3-10 9-12"/></g>',
    "star": f'<path {F} d="M32 4l8.6 18.6 20.4 2.4-15 13.9 4 20.1L32 49l-18 10 4-20.1-15-13.9 20.4-2.4z"/>',
    "pause": f'<path {F} d="M14 8h12v48H14zM38 8h12v48H38z"/>',
    "play1": f'<path {F} d="M18 8l34 24-34 24z"/>',
    "play2": f'<path {F} d="M6 8l26 24L6 56zM32 8l26 24-26 24z"/>',
    "play4": f'<path {F} d="M2 10l20 22-20 22zM22 10l20 22-20 22zM42 10l20 22-20 22z"/>',
    "rank": f'<g {S}><path d="M32 4l24 10v18c0 14-10 23-24 28C18 55 8 46 8 32V14z"/>'
            '<path d="M32 18l4.3 9.2 10 1.3-7.4 7 2 10-8.9-5-8.9 5 2-10-7.4-7 10-1.3z"/></g>',
    "shuttle": f'<path {F} d="M60 32L38 22H22L8 12l4 16H4v8h8l-4 16 14-10h16z"/>',
    "freighter": f'<path {F} d="M4 22h10v-6h14v6h4v-6h14v6h6l8 10-8 10h-6v6H32v-6h-4v6H14v-6H4l4-10z"/>',
}


def icons():
    folder = os.path.join(OUT, "icons")
    os.makedirs(folder, exist_ok=True)
    for name, body in ICONS.items():
        with open(os.path.join(folder, name + ".svg"), "w") as f:
            f.write(ICON % body + "\n")


if __name__ == "__main__":
    hull()
    deck()
    solar()
    planet()
    halo()
    environment()
    icons()
