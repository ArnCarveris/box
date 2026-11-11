#!/usr/bin/env python3
"""Generates the floor textures, the console screen, the hazard stripes and the icons of the
shipyard scene into this folder. The hull textures, the environment and the planet come from the
space station scene (etc/scenes/space_station/assets/generate.py).

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
