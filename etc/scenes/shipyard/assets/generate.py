#!/usr/bin/env python3
"""Generates the floor textures, the console screen, the hazard stripes, the icons and the drive
plume of the shipyard scene into this folder. The hull textures, the environment and the planet
come from the space station scene (etc/scenes/space_station/assets/generate.py).

Needs numpy and Pillow. Run it from anywhere: python3 etc/scenes/shipyard/assets/generate.py
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))


def noise(size, beta, seed):
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(size)[:, None] * size
    fx = np.fft.rfftfreq(size)[None, :] * size
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    phase = rng.uniform(0, 2 * np.pi, f.shape)
    v = np.fft.irfft2(np.exp(1j * phase) / f ** beta, s=(size, size))
    return (v - v.min()) / (v.max() - v.min())


def normal_from_height(height, strength):
    dx = np.roll(height, -1, 1) - np.roll(height, 1, 1)
    dy = np.roll(height, -1, 0) - np.roll(height, 1, 0)
    nx, ny, nz = -dx * strength, dy * strength, np.ones_like(height)
    n = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.dstack([(c / n * 0.5 + 0.5) * 255 for c in (nx, ny, nz)]).astype(np.uint8)


def save_rgb(name, data, quality=88):
    Image.fromarray(np.clip(data, 0, 255).astype(np.uint8)).save(
        os.path.join(OUT, name), quality=quality, subsampling=0)


def floor(size=1024):
    tone = Image.new("L", (size, size), 120)
    relief = Image.new("L", (size, size), 128)
    dt, dr = ImageDraw.Draw(tone), ImageDraw.Draw(relief)
    half = size // 2
    rng = np.random.default_rng(5)
    for i in range(2):
        for j in range(2):
            x0, y0 = i * half, j * half
            dr.rectangle([x0, y0, x0 + half - 1, y0 + half - 1], fill=96)
            dr.rectangle([x0 + 6, y0 + 6, x0 + half - 7, y0 + half - 7], fill=150)
            dt.rectangle([x0 + 6, y0 + 6, x0 + half - 7, y0 + half - 7], fill=int(rng.integers(124, 140)))
            if (i + j) % 2 == 0:
                for k in range(14):
                    y = y0 + 40 + k * (half - 80) / 13
                    dr.rounded_rectangle([x0 + 40, y - 7, x0 + half - 40, y + 7], radius=7, fill=110)
                    dt.rounded_rectangle([x0 + 40, y - 7, x0 + half - 40, y + 7], radius=7, fill=70)
            else:
                for a in range(6):
                    for b in range(6):
                        cx = x0 + 52 + a * (half - 104) / 5
                        cy = y0 + 52 + b * (half - 104) / 5
                        dr.polygon([(cx - 14, cy), (cx, cy - 14), (cx + 14, cy), (cx, cy + 14)], fill=176)
            for cx, cy in ((x0 + 20, y0 + 20), (x0 + half - 20, y0 + 20),
                           (x0 + 20, y0 + half - 20), (x0 + half - 20, y0 + half - 20)):
                dr.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=196)
                dt.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=170)
    tone = np.asarray(tone, float) / 128
    relief = np.asarray(relief.filter(ImageFilter.GaussianBlur(1.2)), float) / 255
    grime = noise(size, 1.4, 41)
    scratch = noise(size, 0.6, 42)
    base = tone * (0.78 + 0.34 * grime) + (scratch - 0.5) * 0.05
    save_rgb("floor.jpg", np.dstack([base * 150, base * 156, base * 166]))
    save_rgb("floor_normal.jpg", normal_from_height(relief + scratch * 0.01, 3.4), 90)
    rough = np.clip(0.55 + 0.4 * grime, 0, 1)
    occl = np.clip(relief * 1.8, 0.35, 1)
    save_rgb("floor_orm.jpg", np.dstack([occl * 255, rough * 255, np.full_like(rough, 190)]), 90)


def periodic_streaks(width, height, seed, lo, hi, drift):
    rng = np.random.default_rng(seed)
    u = np.arange(width)[None, :] / width
    v = np.arange(height)[:, None] / height
    s = np.zeros((height, width))
    for f in range(lo, hi):
        a = rng.uniform(0.4, 1) / f ** 0.6
        s += a * np.sin(2 * np.pi * (f * u + rng.uniform() + drift * rng.uniform(-1, 1) * v))
    return (s - s.min()) / (s.max() - s.min())


def save_emissive(name, lin):
    Image.fromarray((np.clip(lin, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).save(os.path.join(OUT, name))


def plume(width=64, height=256):
    v = (np.arange(height)[:, None] + 0.5) / height
    lin = lambda c: (np.array(c, float) / 255) ** 2.2
    ramp = lambda stops: np.dstack([np.interp(v[:, 0], [t for t, _ in stops], [lin(c)[i] for _, c in stops])[:, None]
                                    * np.ones((1, width)) for i in range(3)])

    knots = sum(np.exp(-((v - t) / w) ** 2) * a for t, w, a in
                ((0.0, 0.07, 0.7), (0.21, 0.035, 1), (0.39, 0.032, 0.8), (0.55, 0.03, 0.6), (0.69, 0.03, 0.4)))
    fade = np.clip(1 - v, 0, 1) ** 1.4
    streak = 0.8 + 0.2 * periodic_streaks(width, height, 11, 3, 14, 0.6)
    core = (fade * (0.2 + 0.8 * knots) * streak)[:, :, None] * ramp(
        [(0, (255, 255, 255)), (0.3, (205, 236, 255)), (0.7, (120, 170, 255)), (1, (110, 90, 255))])
    save_emissive("plume_core.png", core)


def windows(size=512, cols=6, rows=4):
    rng = np.random.default_rng(23)
    albedo = np.zeros((size, size, 3))
    glow = np.zeros((size, size, 3))
    albedo[:] = (28, 34, 44)
    cw, rh = size // cols, size // rows
    for r in range(rows):
        y0 = r * rh
        albedo[y0 + rh - 6:y0 + rh, :] = (206, 212, 220)
        albedo[y0 + rh - 9:y0 + rh - 6, :] = (90, 96, 108)
        for c in range(cols):
            x0 = c * cw
            wx0, wx1 = x0 + 14, x0 + cw - 14
            wy0, wy1 = y0 + 34, y0 + rh - 30
            albedo[wy0:wy1, wx0:wx1] = (44, 56, 72)
            if rng.uniform() < 0.5:
                pick = rng.uniform()
                tone = (1.0, 0.72, 0.42) if pick < 0.65 else (1.0, 0.86, 0.62) if pick < 0.88 else (0.6, 0.8, 1.0)
                level = rng.uniform(0.35, 1.0)
                glow[wy0:wy1, wx0:wx1] = np.array(tone) * level
    save_rgb("windows_albedo.png", albedo)
    save_emissive("windows.png", glow)


def containers(size=512, tiers=4):
    rng = np.random.default_rng(31)
    paint = [(164, 70, 44), (38, 76, 132), (52, 106, 72), (206, 154, 38), (124, 130, 138),
             (30, 112, 120), (226, 228, 230), (214, 104, 40), (120, 40, 44)]
    img = np.zeros((size, size, 3))
    height = np.zeros((size, size))
    th = size // tiers
    for t in range(tiers):
        y0 = t * th
        spans = [(0, size)] if rng.uniform() < 0.5 else [(0, size // 2), (size // 2, size)]
        for x0, x1 in spans:
            base = np.array(paint[rng.integers(len(paint))], float) * rng.uniform(0.82, 1.05)
            block = np.ones((th, x1 - x0, 3)) * base
            ribs = (np.arange(x1 - x0) // 6) % 2 == 0
            block[:, ribs] *= 0.86
            block[:5] *= 0.45
            block[-5:] *= 0.45
            block[:, :5] *= 0.4
            block[:, -5:] *= 0.4
            block[th // 2 - 1:th // 2 + 1] *= 0.92
            img[y0:y0 + th, x0:x1] = block
            relief = np.ones((th, x1 - x0)) * (0.5 + 0.5 * np.cos(np.arange(x1 - x0) * np.pi / 6))[None, :] * 0.35
            relief[:5] = -1
            relief[-5:] = -1
            relief[:, :5] = -1
            relief[:, -5:] = -1
            height[y0:y0 + th, x0:x1] = relief
    grime = noise(size, 1.3, 33)[:, :, None]
    save_rgb("containers.jpg", img * (0.82 + 0.3 * grime))
    save_rgb("containers_normal.jpg", normal_from_height(height, 1.2), 90)


def heat_fins(size=256, lines=10):
    v = (np.arange(size)[:, None] + 0.5) / size
    phase = (v * lines) % 1.0
    line = np.exp(-((phase - 0.5) / 0.045) ** 2)
    streak = 0.75 + 0.25 * periodic_streaks(size, size, 17, 2, 9, 0.2).T
    lin = (line * streak)[:, :, None] * np.array([1.0, 0.42, 0.14])
    save_emissive("heat_fins.png", lin)


def radiator(size=512, lines=10):
    v = (np.arange(size)[:, None] + 0.5) / size
    u = (np.arange(size)[None, :] + 0.5) / size
    phase = (v * lines) % 1.0 - 0.5
    tube = np.exp(-(phase / 0.07) ** 2) * np.ones((1, size))
    lip = np.exp(-((np.abs(phase) - 0.1) / 0.025) ** 2) * np.ones((1, size))
    seam = np.exp(-(np.minimum(u, 1 - u) / 0.006) ** 2) * np.ones((size, 1))
    streak = periodic_streaks(size, size, 41, 3, 24, 0.05).T
    stain = noise(size, 1.6, 43)
    speck = noise(size, 0.4, 47)
    base = np.array([62, 60, 58], float)
    metal = np.array([128, 122, 114], float)
    rust = np.array([96, 66, 48], float)
    img = base * (0.82 + 0.3 * streak[:, :, None])
    img = img * (1 - tube[:, :, None]) + metal * tube[:, :, None]
    img *= 1 - 0.45 * lip[:, :, None]
    img *= 1 - 0.6 * seam[:, :, None]
    burn = np.clip((stain - 0.45) * 2.5, 0, 1)[:, :, None]
    img = img * (1 - 0.5 * burn) + rust * 0.5 * burn
    img *= 0.88 + 0.24 * speck[:, :, None]
    save_rgb("radiator.jpg", img)
    height = tube - 0.5 * seam + 0.04 * streak
    save_rgb("radiator_normal.jpg", normal_from_height(height, 2.4), 90)


def nozzle(width=1024, height=256, tubes=120):
    v = (np.arange(height)[:, None] + 0.5) / height
    u = (np.arange(width)[None, :] + 0.5) / width
    phase = (u * tubes) % 1.0 - 0.5
    groove = np.exp(-(phase / 0.09) ** 2) * np.ones((height, 1))
    rng = np.random.default_rng(53)
    per_tube = rng.uniform(0.86, 1.08, tubes)[((u * tubes).astype(int)) % tubes] * np.ones((height, 1))
    stops = [(0.0, (44, 40, 46)), (0.08, (62, 60, 80)), (0.16, (90, 84, 104)), (0.25, (136, 112, 84)),
             (0.36, (148, 138, 118)), (0.52, (128, 126, 124)), (1.0, (106, 106, 108))]
    ramp = np.dstack([np.interp(v[:, 0], [t for t, _ in stops], [c[i] for _, c in stops])[:, None]
                      * np.ones((1, width)) for i in range(3)])
    soot = periodic_streaks(width, height, 59, 4, 40, 0.02)
    soot = np.clip((soot - 0.35) * 1.8, 0, 1) * np.clip((v - 0.55) / 0.45, 0, 1)
    grime = noise(height, 1.4, 61)
    grime = np.tile(grime, (1, width // height))
    img = ramp * per_tube[:, :, None] * (1 - 0.55 * groove[:, :, None])
    img *= (1 - 0.55 * soot[:, :, None]) * (0.86 + 0.22 * grime[:, :, None])
    save_rgb("nozzle.jpg", img)
    save_rgb("nozzle_normal.jpg", normal_from_height(-groove, 1.6), 90)
    glow = np.exp(-v / 0.07) * (1 - 0.6 * groove) * per_tube
    save_emissive("nozzle_glow.png", glow[:, :, None] * np.array([1.0, 0.36, 0.1]))


def dots(size=128, count=2):
    u = (np.arange(size) + 0.5) / size
    pu = (u[None, :] * count) % 1.0 - 0.5
    pv = (u[:, None] * count) % 1.0 - 0.5
    d = np.exp(-(pu * pu + pv * pv) / (2 * 0.05 ** 2))
    save_emissive("dots.png", np.dstack([d, d, d]))


SCREEN = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 160" width="512" height="320">
<rect width="256" height="160" fill="#05101a"/>
<g fill="none" stroke="#5fd8ff" stroke-width="2">
<rect x="6" y="6" width="244" height="148" rx="4" stroke-opacity="0.5"/>
<path d="M14 120 L44 92 L70 104 L100 64 L132 82 L160 40 L190 58 L242 26" />
<circle cx="200" cy="112" r="28" stroke-opacity="0.8"/>
<circle cx="200" cy="112" r="16" stroke-opacity="0.5"/>
<path d="M200 84v56M172 112h56" stroke-opacity="0.4"/>
</g>
<g fill="#5fd8ff">
<rect x="14" y="132" width="60" height="6" fill-opacity="0.8"/>
<rect x="80" y="132" width="34" height="6" fill-opacity="0.5"/>
<rect x="14" y="14" width="80" height="8" fill-opacity="0.9"/>
<rect x="14" y="26" width="50" height="4" fill-opacity="0.5"/>
<circle cx="214" cy="100" r="4"/>
<rect x="120" y="128" width="40" height="14" fill="#ffb347" fill-opacity="0.9"/>
</g>
</svg>
"""

HAZARD = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="256" height="256">
<rect width="64" height="64" fill="#e2a91c"/>
<path d="M0 0h16L-16 64h-16zM32 0h16L16 64H0zM64 0h16L48 64H32zM96 0h16L80 64H64z" fill="#1b1d22"/>
</svg>
"""

ICON = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="128" height="128">%s</svg>'
S = 'fill="none" stroke="#fff" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"'
F = 'fill="#fff"'

ICONS = {
    "cockpit": f'<g {S}><path d="M6 44c4-16 16-26 34-26h8l10 12v14z"/><path d="M30 20l6 12h22"/>'
               '<path d="M6 44h52M16 44v8M48 44v8"/></g>',
    "hab": f'<g {S}><path d="M6 46V30h52v16"/><path d="M6 38h52"/><path d="M12 30v-6a5 5 0 0 1 5-5h10a5 5 0 0 1 5 5v6"/>'
           '<path d="M6 46v8M58 46v8"/></g>',
    "life": f'<g {S}><path d="M32 57C16 46 7 37 7 24c0-8 6-14 13-14 5 0 9 3 12 8 3-5 7-8 12-8 7 0 13 6 13 14 0 13-9 22-25 33z"/>'
            '<path d="M13 31h11l4-8 7 15 4-7h12"/></g>',
    "reactor": f'<g {S}><circle cx="32" cy="32" r="17"/><circle cx="32" cy="32" r="5"/>'
               '<path d="M32 4v10M32 50v10M4 32h10M50 32h10M12 12l7 7M45 45l7 7M52 12l-7 7M19 45l-7 7"/></g>',
    "radiator": f'<g {S}><path d="M6 32h8"/><path d="M14 10v44M14 10l40 8v28l-40 8"/>'
                '<path d="M24 16v32M34 18v28M44 20v24"/></g>',
    "cargo": f'<g {S}><path d="M32 6l24 12v28L32 58 8 46V18z"/><path d="M8 18l24 12 24-12M32 30v28"/>'
             '<path d="M20 12l24 12"/></g>',
    "fuel": f'<g {S}><path d="M14 58V14a8 8 0 0 1 8-8h12a8 8 0 0 1 8 8v44"/><path d="M8 58h40"/>'
            '<path d="M42 22h6a6 6 0 0 1 6 6v18a4 4 0 0 1-8 0V34"/><path d="M20 18h16v12H20z"/></g>',
    "weapon": f'<g {S}><circle cx="32" cy="32" r="22"/><circle cx="32" cy="32" r="8"/>'
              '<path d="M32 2v14M32 48v14M2 32h14M48 32h14"/></g>',
    "engine": f'<g {S}><path d="M6 20h20l10 6v12l-10 6H6z"/><path d="M36 26l12-8v28l-12-8"/>'
              '<path d="M52 24h8M52 32h10M52 40h8"/></g>',
    "drill": f'<g {S}><path d="M6 20h14v24H6z"/><path d="M20 24h6v16h-6"/><path d="M26 20l32 12-32 12z"/>'
             '<path d="M34 23l5 18M44 27l3 10"/></g>',
    "builder": f'<g {S}><path d="M8 56h24M20 56v-8"/><circle cx="20" cy="43" r="5"/><path d="M23 39l11-17"/>'
               '<circle cx="36" cy="19" r="4"/><path d="M40 21l12 9"/><path d="M52 30l6-3M52 30l3 6"/>'
               '<path d="M38 50h20M38 58h20M42 50l6 8 6-8"/></g>',
    "datacenter": f'<g {S}><rect x="10" y="7" width="44" height="14" rx="2"/><rect x="10" y="25" width="44" height="14" rx="2"/>'
                  '<rect x="10" y="43" width="44" height="14" rx="2"/><path d="M17 14h.01M17 32h.01M17 50h.01M34 14h12M34 32h12M34 50h12"/></g>',
    "autopilot": f'<g {S}><path d="M10 52V34a22 22 0 0 1 44 0v18z"/><rect x="17" y="30" width="30" height="9" rx="4.5"/>'
                 '<path d="M32 12V5M6 52h52M22 46h20"/></g>',
    "remove": f'<g {S}><path d="M10 16h44M24 16V8h16v8M15 16l3 40h28l3-40"/><path d="M27 27v18M37 27v18"/></g>',
    "deck": f'<g {S}><path d="M32 8l26 10-26 10L6 18z"/><path d="M6 30l26 10 26-10"/><path d="M6 42l26 10 26-10"/></g>',
    "power": f'<path {F} d="M37 3L11 37h17l-3 24 28-36H35z"/>',
    "crew": f'<g {S}><circle cx="24" cy="20" r="10"/><path d="M6 56c0-13 8-20 18-20s18 7 18 20"/>'
            '<path d="M42 11a10 10 0 0 1 0 18M48 37c6 3 10 9 10 19"/></g>',
    "thrust": f'<g {S}><path d="M8 32h34M30 18l14 14-14 14"/><path d="M50 14v36M58 20v24"/></g>',
    "range": f'<g {S}><circle cx="32" cy="32" r="26"/><path d="M32 32l14-14"/><circle cx="32" cy="32" r="4"/>'
             '<path d="M32 8v6M56 32h-6M32 56v-6M8 32h6"/></g>',
    "ok": f'<g {S}><circle cx="32" cy="32" r="26"/><path d="M20 33l8 8 16-17"/></g>',
    "warn": f'<g {S}><path d="M32 6l28 50H4z"/><path d="M32 24v14M32 46v1"/></g>',
    "ship": f'<g {S}><path d="M60 32L40 20H22L6 12l4 20-4 20 16-8h18z"/><path d="M22 20v24M40 20v24"/></g>',
    "liner": f'<g {S}><path d="M4 32h48"/><path d="M4 27v10"/><path d="M52 27h3l6 5-6 5h-3z"/>'
             '<ellipse cx="22" cy="32" rx="4" ry="24"/><ellipse cx="36" cy="32" rx="4" ry="17"/></g>',
    "bridge": f'<g {S}><path d="M4 54h56"/><path d="M8 54V38h48v16"/><path d="M14 38V26h36v12"/>'
              '<path d="M20 26l4-8h16l4 8"/><path d="M32 18V8"/><path d="M18 32h28"/></g>',
    "sensor": f'<g {S}><path d="M12 14a28 28 0 0 0 38 38z"/><path d="M31 33l14-14"/><circle cx="47" cy="17" r="3"/>'
              '<path d="M26 52l-8 8h28"/></g>',
    "quarters": f'<g {S}><rect x="6" y="16" width="52" height="32" rx="12"/>'
                '<path d="M17 27h4M27 27h4M37 27h4M47 27h.01M17 37h4M27 37h4M37 37h4M47 37h.01"/></g>',
    "truss": f'<g {S}><path d="M4 18h56M4 46h56M4 18v28M60 18v28M18 18v28M32 18v28M46 18v28"/>'
             '<path d="M4 46l14-28M18 46l14-28M32 46l14-28M46 46l14-28"/></g>',
    "yoke": f'<g {S}><circle cx="32" cy="32" r="10"/><circle cx="8" cy="32" r="5"/><circle cx="56" cy="32" r="5"/>'
            '<circle cx="32" cy="56" r="5"/><path d="M13 30h9M13 34h9M42 30h9M42 34h9M30 42v9M34 42v9"/></g>',
    "spine": f'<g {S}><circle cx="32" cy="32" r="9"/><circle cx="32" cy="9" r="5"/><circle cx="32" cy="55" r="5"/>'
             '<circle cx="9" cy="32" r="5"/><circle cx="55" cy="32" r="5"/></g>',
    "ring": f'<g {S}><path d="M4 32h56"/><ellipse cx="32" cy="32" rx="12" ry="26"/><ellipse cx="32" cy="32" rx="4" ry="9"/>'
            '<path d="M32 6v17M32 41v17"/></g>',
    "arbor": f'<g {S}><path d="M6 50a26 26 0 0 1 52 0"/><path d="M2 50h60"/><path d="M24 50V38"/><circle cx="24" cy="32" r="7"/>'
             '<path d="M42 50v-8"/><circle cx="42" cy="38" r="5"/></g>',
    "core": f'<g {S}><circle cx="32" cy="32" r="13"/><ellipse cx="32" cy="32" rx="28" ry="9"/><path d="M32 4v10M32 50v10"/></g>',
    "wing": f'<g {S}><path d="M8 8v48"/><path d="M8 12l48 6v28L8 52"/><path d="M8 24l48 3M8 32h48M8 40l48-3"/></g>',
    "drive": f'<g {S}><path d="M4 16h16v32H4z"/><path d="M20 18l20-8v20l-20 2zM20 46l20 8V34l-20-2z"/>'
             '<path d="M46 20h14M46 44h14M48 32h10"/></g>',
    "jump": f'<g {S}><path d="M2 32h60"/><ellipse cx="16" cy="32" rx="5" ry="20"/><ellipse cx="32" cy="32" rx="5" ry="24"/>'
            '<ellipse cx="48" cy="32" rx="5" ry="20"/></g>',
    "tanks": f'<g {S}><circle cx="19" cy="19" r="12"/><circle cx="45" cy="19" r="12"/><circle cx="19" cy="45" r="12"/>'
             '<circle cx="45" cy="45" r="12"/></g>',
    "freight": f'<g {S}><rect x="4" y="38" width="26" height="16"/><rect x="34" y="38" width="26" height="16"/>'
               '<rect x="19" y="18" width="26" height="16"/><path d="M11 42v8M17 42v8M23 42v8M41 42v8M47 42v8M53 42v8M26 22v8M32 22v8M38 22v8"/>'
               '<path d="M32 18V8"/></g>',
    "hangar": f'<g {S}><path d="M4 56V24l28-14 28 14v32"/><path d="M14 56V32h36v24"/><path d="M22 50l10-8 10 8z"/></g>',
    "slip": f'<g {S}><path d="M8 58V8h48v50"/><path d="M8 16h48"/><path d="M36 16v10"/><path d="M14 48c6-12 30-12 36 0z"/>'
            '<path d="M22 48v-6M32 48v-9M42 48v-6"/></g>',
    "refinery": f'<g {S}><path d="M6 56V22h12v34M22 56V12h12v44"/><path d="M38 56V38l9-7 9 7v18"/><path d="M2 56h60"/>'
                '<path d="M44 46h6"/></g>',
    "assembler": f'<g {S}><path d="M4 58h24M10 58v-8h12v8"/><circle cx="16" cy="44" r="5"/><path d="M19 40l9-18"/>'
                 '<circle cx="30" cy="19" r="4"/><path d="M34 18l16-6"/><path d="M50 12l6-4M50 12l4 7"/>'
                 '<path d="M24 34l8-6M36 58h24v-14H36z"/></g>',
    "shield": f'<g {S}><path d="M32 4l22 8v16c0 14-9 24-22 32C19 52 10 42 10 28V12z"/><path d="M32 14v38"/>'
              '<path d="M20 26c8-4 16-4 24 0"/></g>',
    "railgun": f'<g {S}><path d="M4 22h18v20H4z"/><path d="M22 26h38M22 38h38"/><path d="M30 32h28"/>'
               '<path d="M28 22v20M38 24v16M48 24v16"/></g>',
    "armor": f'<g {S}><path d="M6 56V26L30 8h28v48z"/><path d="M16 56V30l16-12h18"/><path d="M26 56V36l8-6h16"/></g>',
    "farm": f'<g {S}><path d="M32 56V10"/><path d="M32 22c-7 0-11-5-11-11 7 0 11 5 11 11zM32 22c7 0 11-5 11-11-7 0-11 5-11 11z"/>'
            '<path d="M32 36c-7 0-11-5-11-11 7 0 11 5 11 11zM32 36c7 0 11-5 11-11-7 0-11 5-11 11z"/><path d="M8 56h48"/></g>',
    "battery": f'<g {S}><path d="M4 52h56"/><path d="M10 52v-8a12 12 0 0 1 12-12h12a12 12 0 0 1 12 12v8"/>'
               '<path d="M34 36l26-14M34 31l22-20M37 41l25-9"/></g>',
}


def icons():
    folder = os.path.join(OUT, "icons")
    os.makedirs(folder, exist_ok=True)
    for name, body in ICONS.items():
        with open(os.path.join(folder, name + ".svg"), "w") as f:
            f.write(ICON % body + "\n")


if __name__ == "__main__":
    floor()
    with open(os.path.join(OUT, "screen.svg"), "w") as f:
        f.write(SCREEN)
    with open(os.path.join(OUT, "hazard.svg"), "w") as f:
        f.write(HAZARD)
    icons()
    plume()
    windows()
    containers()
    heat_fins()
    radiator()
    nozzle()
    dots()
