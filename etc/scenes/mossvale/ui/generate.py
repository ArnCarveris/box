#!/usr/bin/env python3
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))

INK = (62, 40, 24)


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


def to_image(a):
    return Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8))


def edge_distance(h, w):
    y = np.linspace(0, 1, h)[:, None]
    x = np.linspace(0, 1, w)[None, :]
    dx = np.minimum(x, 1 - x) * w / min(h, w)
    dy = np.minimum(y, 1 - y) * h / min(h, w)
    return np.minimum(dx, dy)


def parchment():
    h, w = 512, 1024
    low = noise(h, w, 2.2, 1)
    mid = noise(h, w, 1.4, 2)
    fiber = noise(h, w * 4, 0.6, 3)[:, ::4]
    stain = noise(h, w, 2.6, 4)
    base = mix(rgb(244, 232, 202), rgb(230, 212, 172), low)
    base = mix(base, rgb(250, 242, 220), smooth(0.55, 0.9, mid) * 0.5)
    base = mix(base, rgb(214, 186, 136), smooth(0.72, 0.95, stain) * 0.35)
    base = base * (0.96 + fiber[..., None] * 0.06)
    e = edge_distance(h, w)
    burn = 1 - smooth(0.0, 0.16, e + (low - 0.5) * 0.05)
    base = mix(base, rgb(176, 134, 82), burn * 0.75)
    to_image(base).save(os.path.join(OUT, "parchment.jpg"), quality=88)


def leather():
    h, w = 256, 1024
    grain = noise(h, w, 0.9, 11)
    low = noise(h, w, 2.0, 12)
    pores = noise(h, w, 0.2, 13)
    base = mix(rgb(56, 74, 46), rgb(38, 52, 32), low)
    base = base * (0.86 + grain[..., None] * 0.26)
    base = base * (1 - smooth(0.82, 1.0, pores)[..., None] * 0.25)
    e = edge_distance(h, w)
    base = mix(base, rgb(20, 28, 16), (1 - smooth(0.0, 0.1, e)) * 0.8)
    img = to_image(base)
    d = ImageDraw.Draw(img)
    inset = 16
    for x in range(inset + 6, w - inset - 6, 22):
        d.line([(x, inset), (x + 11, inset)], fill=(170, 140, 82), width=3)
        d.line([(x, h - inset), (x + 11, h - inset)], fill=(170, 140, 82), width=3)
    img.save(os.path.join(OUT, "leather.jpg"), quality=88)


def vignette():
    n = 512
    y = np.linspace(-1, 1, n)[:, None]
    x = np.linspace(-1, 1, n)[None, :]
    r = np.sqrt(x * x * 0.8 + y * y)
    a = smooth(0.25, 1.1, r) * 0.95
    out = np.zeros((n, n, 4))
    out[..., 0] = 0.05
    out[..., 1] = 0.035
    out[..., 2] = 0.06
    out[..., 3] = a
    to_image(out).save(os.path.join(OUT, "vignette.png"))


def fract(v):
    return v - math.floor(v)


def hash1(i):
    return fract(math.sin(i * 12.9898 + 4.1414) * 43758.5453)


def hash2(x, z):
    return fract(math.sin(x * 12.9898 + z * 78.233) * 43758.5453)


def dist2(ax, az, bx, bz):
    return (ax - bx) ** 2 + (az - bz) ** 2


LAKE = (-72, -6, 15)
ISLE = (-75, -8, 5.5)
WALL_N0, WALL_N1, WALL_S0, WALL_S1 = -41, -31, 31, 41


def road_x(z):
    return 2.5 * math.sin(z * 0.045)


def road_z(x):
    return 1.2 * math.sin(x * 0.05 + 0.5)


def river_x(z):
    return -44 + 2.5 * math.sin(z * 0.06 + 1)


def on_ns_road(x, z):
    return abs(x - road_x(z)) < 3 and -84 < z < 90


def camp_spot(x, z):
    return dist2(x, z, -20, -60) < 121 or (abs(z + 60) < 3 and -20 < x < 2)


def shrine_spot(x, z):
    return dist2(x, z, 18, -80) < 144 or (abs(z + 72) < 3 and 0 < x < 18)


def grove_spot(x, z):
    return dist2(x, z, 36, -58) < 100 or (abs(z + 58) < 3 and 0 < x < 36)


HILL_SPOTS = [(-31, 70, 9), (-5, 80, 9), (12, 52, 9), (-36, 48, 9), (50, 50, 9), (-38, 88, 9), (48, 60, 9),
              (60, 88, 9), (4, 92, 9), (-30, 86, 16), (-26, 79, 64)]


def hill_spot(x, z):
    return any(dist2(x, z, a, b) < r for a, b, r in HILL_SPOTS) or (abs(x + 22) < 3 and 60 < z < 80)


def wood_spot(x, z):
    return any(dist2(x, z, a, b) < 9 for a, b in [(-36, -70), (6, -90), (44, -82), (-6, -48)])


def forest_clear(x, z):
    return on_ns_road(x, z) or camp_spot(x, z) or shrine_spot(x, z) or grove_spot(x, z) or wood_spot(x, z) or abs(x - river_x(z)) < 5


def quarry_spot(x, z):
    return dist2(x, z, 26, 63) < 225 or (abs(z - 62) < 3 and 0 < x < 26)


def pasture_spot(x, z):
    return dist2(x, z, -16, 56) < 100 or (abs(z - 56) < 3 and -16 < x < 0)


def hills_clear(x, z):
    return on_ns_road(x, z) or quarry_spot(x, z) or pasture_spot(x, z) or hill_spot(x, z) or abs(x - river_x(z)) < 5


def tree_pos(i, j):
    x = -38 + i * 3.4 + (hash2(i, j) - 0.5) * 2.2
    z = -93 + j * 3.4 + (hash2(j * 1.7, i * 3.1) - 0.5) * 2.2
    return x, z


def tree_here(i, j):
    x, z = tree_pos(i, j)
    wall = z > WALL_N0 - 1
    h = hash2(i * 0.73 + 5, j * 1.31 + 2)
    return x < 94 and not forest_clear(x, z) and (h > 0.04 if wall else h > 0.42)


def rock_pos(i, j):
    x = -38 + i * 4.6 + (hash2(i + 11, j) - 0.5) * 3
    z = WALL_S0 + 0.5 + j * 4.6 + (hash2(j + 3, i * 2.3) - 0.5) * 3
    return x, z


def rock_here(i, j):
    x, z = rock_pos(i, j)
    h = hash2(i * 1.9 + 1, j * 0.77 + 9)
    wall = z < WALL_S1 + 1
    return x < 94 and z < 94 and not hills_clear(x, z) and (h > 0.0 if wall else h > 0.72)


HOUSES = [(-17, 18, 7, 5.5, 2, 4), (-22, -17, 9, 7, 0, 3), (-9, -17.5, 8, 6, 0, 2), (12, -16, 7, 6, 0, 1),
          (15, 16.5, 8, 6, 2, 1), (-31, 18, 6.5, 5.5, 2, 0), (-8, 21.5, 7, 5.5, 1, 1), (25, -9, 6, 5, 0, 0),
          (26, 11, 6, 5, 2, 2), (24, -21, 6, 5, 3, 3), (-33, -16, 6, 5, 1, 1), (50, -14, 8, 6, 0, 1),
          (64, -16, 10, 8, 0, 1), (84, 12, 7, 5.5, 3, 0), (-55, 15, 6, 5, 3, 2), (-24, -66.5, 6, 5, 0, 3)]

ROOF = {0: (196, 160, 96), 1: (168, 74, 56), 2: (78, 96, 136), 3: (84, 116, 78), 4: (106, 78, 132)}


def map_image():
    n = 1024
    k = n / 200.0

    def px(x, z):
        return ((x + 100) * k, (z + 100) * k)

    paper = noise(n, n, 2.2, 21)
    grain = noise(n, n, 0.8, 22)
    wash = noise(n, n, 1.8, 23)
    ys, xs = np.mgrid[0:n, 0:n]
    wx = xs / k - 100
    wz = ys / k - 100
    base = mix(rgb(238, 224, 186), rgb(222, 204, 160), paper)
    meadow = mix(rgb(190, 204, 136), rgb(170, 190, 118), wash)
    base = mix(base, meadow, 0.55)
    east = smooth(-52, -48, wx)
    forest = smooth(-29, -33, wz) * east
    base = mix(base, mix(rgb(132, 158, 102), rgb(116, 146, 92), wash), forest * 0.75)
    hills = smooth(29, 33, wz) * east
    base = mix(base, mix(rgb(206, 196, 140), rgb(190, 178, 124), wash), hills * 0.8)
    farm = smooth(0, 1.5, np.minimum.reduce([wx - 33, 95 - wx, wz + 29, 29 - wz]))
    base = mix(base, mix(rgb(208, 206, 122), rgb(196, 190, 108), wash), farm * 0.6)
    rx = -44 + 2.5 * np.sin(wz * 0.06 + 1)
    river = smooth(3.4, 2.6, np.abs(wx - rx))
    lake = smooth(LAKE[2] + 0.4, LAKE[2] - 0.4, np.sqrt((wx - LAKE[0]) ** 2 + (wz - LAKE[1]) ** 2))
    isle = smooth(ISLE[2] + 0.4, ISLE[2] - 0.4, np.sqrt((wx - ISLE[0]) ** 2 + (wz - ISLE[1]) ** 2))
    water = np.clip(np.maximum(river, lake) - isle, 0, 1)
    shore = np.clip(np.maximum(smooth(4.6, 3.0, np.abs(wx - rx)), smooth(LAKE[2] + 1.8, LAKE[2], np.sqrt((wx - LAKE[0]) ** 2 + (wz - LAKE[1]) ** 2))) - water, 0, 1)
    base = mix(base, rgb(226, 210, 160), shore * 0.7)
    depth = smooth(LAKE[2] - 2, LAKE[2] - 9, np.sqrt((wx - LAKE[0]) ** 2 + (wz - LAKE[1]) ** 2))
    wcol = mix(rgb(132, 178, 196), rgb(96, 148, 178), np.maximum(depth, wash * 0.5))
    base = mix(base, wcol, water)
    base = base * (0.94 + grain[..., None] * 0.1)
    edge = edge_distance(n, n)
    base = mix(base, rgb(170, 128, 78), (1 - smooth(0.0, 0.06, edge + (paper - 0.5) * 0.02)) * 0.7)
    img = to_image(base).convert("RGBA")

    over = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)

    def poly_line(pts, color, width):
        d.line(pts, fill=color, width=width, joint="curve")

    rv_l = [px(river_x(z) - 3, z) for z in np.arange(-100, 101, 1.0)]
    rv_r = [px(river_x(z) + 3, z) for z in np.arange(-100, 101, 1.0)]
    poly_line(rv_l, (60, 92, 110, 200), 3)
    poly_line(rv_r, (60, 92, 110, 200), 3)
    for r, a in ((LAKE[2], 210), (LAKE[2] - 1.6, 70), (LAKE[2] - 3.2, 40)):
        cx, cy = px(LAKE[0], LAKE[1])
        d.ellipse([cx - r * k, cy - r * k, cx + r * k, cy + r * k], outline=(60, 92, 110, a), width=3 if a > 100 else 2)
    cx, cy = px(ISLE[0], ISLE[1])
    d.ellipse([cx - ISLE[2] * k, cy - ISLE[2] * k, cx + ISLE[2] * k, cy + ISLE[2] * k], outline=(80, 70, 50, 220), width=3)
    for z in np.arange(-96, 96, 7.0):
        x = river_x(z) + (hash1(z) - 0.5) * 2
        a, b = px(x - 0.8, z), px(x + 0.8, z + 0.6)
        d.arc([a[0], a[1] - 4, b[0], b[1] + 4], 200, 340, fill=(240, 248, 250, 160), width=2)

    def road(pts):
        poly_line(pts, (120, 88, 52, 230), 13)
        poly_line(pts, (222, 196, 142, 255), 8)

    road([px(road_x(z), z) for z in np.arange(-84, 91, 1.0)])
    road([px(x, road_z(x)) for x in np.arange(-60, 93, 1.0)])
    for path in ([(-2, -60), (-20, -60)], [(2, -58), (36, -58)], [(0, 62), (26, 62)], [(0, -72), (18, -72)], [(0, 56), (-16, 56)]):
        poly_line([px(*p) for p in path], (130, 96, 60, 200), 5)
        for t in np.linspace(0, 1, 14)[:-1]:
            ax = path[0][0] + (path[1][0] - path[0][0]) * t
            az = path[0][1] + (path[1][1] - path[0][1]) * t
            d.ellipse([px(ax, az)[0] - 1.5, px(ax, az)[1] - 1.5, px(ax, az)[0] + 1.5, px(ax, az)[1] + 1.5], fill=(230, 210, 160, 255))
    sx, sy = px(0, 0)
    d.ellipse([sx - 11 * k, sy - 11 * k, sx + 11 * k, sy + 11 * k], fill=(214, 200, 168, 255), outline=(110, 84, 56, 255), width=3)
    d.ellipse([sx - 3 * k, sy - 3 * k, sx + 3 * k, sy + 3 * k], fill=(140, 180, 196, 255), outline=(80, 70, 50, 255), width=2)
    for c, r in (((-20, -60), 9), ((36, -58), 7.5), ((26, 66), 13)):
        x, y = px(*c)
        d.ellipse([x - r * k, y - r * k, x + r * k, y + r * k], fill=(214, 190, 140, 200), outline=(130, 96, 60, 160), width=2)
    x, y = px(18, -80)
    d.ellipse([x - 8 * k, y - 8 * k, x + 8 * k, y + 8 * k], fill=(206, 200, 184, 230), outline=(110, 100, 90, 220), width=2)
    for i in range(9):
        a = i * 0.698 + 0.2
        mx, my = px(18 + math.cos(a) * 9.5, -80 + math.sin(a) * 9.5)
        d.rectangle([mx - 4, my - 4, mx + 4, my + 4], fill=(150, 146, 136, 255), outline=(60, 50, 40, 255), width=2)
    for i in range(6):
        a = i * 1.047
        fx, fy = px(-78 + math.cos(a) * 4, 44 + math.sin(a) * 4)
        d.ellipse([fx - 5, fy - 5, fx + 5, fy + 5], fill=(214, 90, 80, 255), outline=(80, 40, 30, 255), width=2)
    for i in range(10):
        x0, y0 = px(40.4 + i * 2, -27.8)
        x1, y1 = px(41.6 + i * 2, -22.2)
        d.rectangle([x0, y0, x1, y1], fill=(222, 190, 96, 255), outline=(150, 116, 50, 255), width=1)
    for i in range(8):
        cx2 = 44 + (i % 4) * 7
        cz2 = 9 + (i // 4) * 7
        x0, y0 = px(cx2 - 2.6, cz2 - 2.6)
        x1, y1 = px(cx2 + 2.6, cz2 + 2.6)
        d.rectangle([x0, y0, x1, y1], fill=(150, 116, 74, 255), outline=(96, 70, 44, 255), width=2)
        for r in range(4):
            yy = y0 + (r + 0.5) * (y1 - y0) / 4
            d.line([(x0 + 3, yy), (x1 - 3, yy)], fill=(110, 150, 70, 255), width=2)
    for c, w2, d2 in (((40, -6), 6, 5), ((-16, 56), 10, 8)):
        x0, y0 = px(c[0] - w2 / 2, c[1] - d2 / 2)
        x1, y1 = px(c[0] + w2 / 2, c[1] + d2 / 2)
        d.rectangle([x0, y0, x1, y1], outline=(110, 80, 50, 255), width=2)
    x0, y0 = px(-59 - 7, 4 - 1.2)
    x1, y1 = px(-59, 4 + 1.2)
    d.rectangle([x0, y0, x1, y1], fill=(170, 124, 80, 255), outline=(90, 60, 36, 255), width=2)

    glyphs = []
    for j in range(19):
        for i in range(39):
            if tree_here(i, j):
                x, z = tree_pos(i, j)
                h = hash2(i * 3.3, j * 7.1)
                glyphs.append((z, "pine" if h < 0.4 else ("autumn" if h > 0.92 else "tree"), x, h))
    for i in range(56):
        t = i * 3.6 - 100
        glyphs.append((-99 - hash1(i * 3) * 4, "pine", t, hash1(i)))
        glyphs.append((99 + hash1(i * 5) * 4, "pine", t, hash1(i + 9)))
        glyphs.append((t, "pine" if i % 3 == 0 else "tree", 99 + hash1(i * 7) * 4, hash1(i + 17)))
        glyphs.append((t, "pine" if i % 3 == 0 else "tree", -99 - hash1(i * 11) * 4, hash1(i + 23)))
    for i in range(60):
        x = -96 + hash1(i * 4.1) * 50
        z = -94 + hash1(i * 3.3 + 1) * 188
        if i % 4 == 0 and dist2(x, z, LAKE[0], LAKE[1]) > (LAKE[2] + 2.5) ** 2 and abs(x - river_x(z)) > 5 and abs(z - road_z(x)) >= 3:
            glyphs.append((z, "tree", x, hash1(i * 7)))
    for i in range(18):
        x = -36 + i * 7.5 + hash1(i) * 3
        if x < 92 and abs(x - road_x(0)) > 6 and not (-36 < x < 30):
            glyphs.append((-27 + hash1(i * 3) * 2, "tree", x, hash1(i * 2)))
    for i in range(6):
        glyphs.append((-24 + (i // 2) * 5, "tree", 86 + (i % 2) * 5, 0.5 + i * 0.07))
    for j in range(14):
        for i in range(29):
            if rock_here(i, j):
                x, z = rock_pos(i, j)
                glyphs.append((z, "rock", x, hash2(i * 1.3, j * 2.7)))
            x, z = rock_pos(i, j)
            x += 2.2
            z += 1.6
            h = hash2(i * 5.3, j * 1.9)
            if h > 0.86 and not hills_clear(x, z) and z > WALL_S1 + 2 and x < 92 and z < 92:
                glyphs.append((z, "pine", x, h))
    for hx, hz, w, dd, face, roof in HOUSES:
        glyphs.append((hz, "house", hx, (w, dd, face, roof)))
    glyphs.append((-18, "mill", 80, 0))
    glyphs.append((-9.5, "tower", -77.5, 0))
    glyphs.append((86, "tower", -30, 0))
    glyphs.append((51, "mine", 30, 0))
    glyphs.append((-80, "altar", 18, 0))
    glyphs.sort(key=lambda g: g[0])

    outline = (52, 38, 24, 255)
    for z, kind, x, h in glyphs:
        cx, cy = px(x, z)
        if not (-20 < cx < n + 20 and -20 < cy < n + 20):
            continue
        if kind == "tree" or kind == "autumn":
            r = (1.7 + h * 0.5) * k
            fill = (214, 150, 70, 255) if kind == "autumn" else (96 + int(h * 30), 140 + int(h * 20), 74, 255)
            shade = tuple(int(c * 0.72) for c in fill[:3]) + (255,)
            d.line([(cx, cy), (cx, cy + r * 0.9)], fill=outline, width=3)
            d.ellipse([cx - r, cy - r * 1.55, cx + r, cy + r * 0.25], fill=shade, outline=outline, width=2)
            d.ellipse([cx - r * 0.82, cy - r * 1.5, cx + r * 0.55, cy - r * 0.05], fill=fill)
            d.ellipse([cx - r * 0.5, cy - r * 1.3, cx - r * 0.05, cy - r * 0.85], fill=tuple(min(255, int(c * 1.18)) for c in fill[:3]) + (255,))
        elif kind == "pine":
            r = (1.5 + h * 0.4) * k
            fill = (62, 108, 78, 255)
            d.line([(cx, cy - r * 0.2), (cx, cy + r * 0.7)], fill=outline, width=3)
            d.polygon([(cx, cy - r * 2.2), (cx + r, cy + r * 0.2), (cx - r, cy + r * 0.2)], fill=fill, outline=outline, width=2)
            d.polygon([(cx, cy - r * 2.0), (cx, cy + r * 0.1), (cx - r * 0.8, cy + r * 0.1)], fill=(84, 136, 98, 255))
        elif kind == "rock":
            r = (1.2 + h * 0.6) * k
            d.chord([cx - r * 1.3, cy - r * 1.4, cx + r * 1.3, cy + r * 0.6], 180, 360, fill=(170, 160, 136, 255), outline=outline, width=2)
            d.line([(cx - r * 1.3, cy - r * 0.4), (cx + r * 1.3, cy - r * 0.4)], fill=outline, width=2)
            for s in range(3):
                xx = cx + r * (0.2 + s * 0.3)
                d.line([(xx, cy - r * 0.5), (xx - r * 0.2, cy - r * 1.0 + s * 0.15 * r)], fill=(110, 96, 74, 255), width=1)
        elif kind == "house":
            w, dd, face, roof = h
            if face % 2:
                w, dd = dd, w
            x0, y0 = px(x - w / 2, z - dd / 2)
            x1, y1 = px(x + w / 2, z + dd / 2)
            rc = ROOF[roof]
            d.rectangle([x0 + 3, y0 + 3, x1 + 3, y1 + 3], fill=(60, 44, 30, 120))
            d.rectangle([x0, y0, x1, y1], fill=rc + (255,), outline=outline, width=2)
            if face % 2:
                d.line([((x0 + x1) / 2, y0), ((x0 + x1) / 2, y1)], fill=outline, width=2)
                d.rectangle([(x0 + x1) / 2, y0 + 2, x1 - 2, y1 - 2], fill=tuple(int(c * 0.78) for c in rc) + (255,))
            else:
                d.line([(x0, (y0 + y1) / 2), (x1, (y0 + y1) / 2)], fill=outline, width=2)
                d.rectangle([x0 + 2, (y0 + y1) / 2, x1 - 2, y1 - 2], fill=tuple(int(c * 0.78) for c in rc) + (255,))
        elif kind == "mill":
            r = 3 * k
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(236, 226, 204, 255), outline=outline, width=2)
            for a in range(4):
                ang = a * math.pi / 2 + 0.4
                d.line([(cx, cy), (cx + math.cos(ang) * r * 2.2, cy + math.sin(ang) * r * 2.2)], fill=outline, width=4)
        elif kind == "tower":
            r = 2.2 * k
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(176, 170, 156, 255), outline=outline, width=2)
            d.ellipse([cx - r * 0.6, cy - r * 0.6, cx + r * 0.6, cy + r * 0.6], fill=(110, 80, 136, 255), outline=outline, width=2)
        elif kind == "mine":
            d.chord([cx - 8 * k, cy - 6 * k, cx + 8 * k, cy + 3 * k], 180, 360, fill=(150, 130, 104, 255), outline=outline, width=2)
            d.rectangle([cx - 1.6 * k, cy - 2.4 * k, cx + 1.6 * k, cy - 1.5], fill=(30, 24, 20, 255))
        elif kind == "altar":
            d.rectangle([cx - 1.2 * k, cy - 0.7 * k, cx + 1.2 * k, cy + 0.7 * k], fill=(120, 116, 106, 255), outline=outline, width=2)
            d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(150, 210, 250, 255))
    img = Image.alpha_composite(img, over.filter(ImageFilter.GaussianBlur(0.5)))
    img.convert("RGB").save(os.path.join(OUT, "map.jpg"), quality=90)


GOLD_DEFS = """<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff0b8"/><stop offset="0.35" stop-color="#e2b456"/><stop offset="0.7" stop-color="#b07a2a"/><stop offset="1" stop-color="#6e4614"/></linearGradient>
<linearGradient id="leaf" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9cc070"/><stop offset="1" stop-color="#3e6a34"/></linearGradient>"""


def svg(name, w, h, body, defs=""):
    with open(os.path.join(OUT, name), "w") as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" width="%g" height="%g">\n<defs>%s\n%s</defs>\n%s\n</svg>\n' % (w, h, w, h, GOLD_DEFS, defs, body))


def leaf_path(x, y, ang, s):
    c, si = math.cos(ang), math.sin(ang)

    def p(a, b):
        return "%.2f %.2f" % (x + (a * c - b * si) * s, y + (a * si + b * c) * s)

    return "M%s Q%s %s Q%s %s Z" % (p(0, 0), p(0.5, -0.45), p(1, 0), p(0.5, 0.45), p(0, 0))


def corner():
    leaves = ""
    for x, y, a, s in ((30, 6, 0.3, 13), (6, 30, 1.27, 13), (46, 9, -0.5, 10), (9, 46, 2.07, 10), (20, 20, 0.785, 11)):
        leaves += '<path d="%s" fill="url(#leaf)" stroke="#2a1c0e" stroke-width="1.3"/>\n' % leaf_path(x, y, a, s)
    body = """<path d="M5 60 V18 Q5 5 18 5 H60" fill="none" stroke="#2a1c0e" stroke-width="7" stroke-linecap="round"/>
<path d="M5 60 V18 Q5 5 18 5 H60" fill="none" stroke="url(#gold)" stroke-width="4.2" stroke-linecap="round"/>
<path d="M14 54 Q14 30 22 22 Q30 14 54 14" fill="none" stroke="#2a1c0e" stroke-width="3.6" stroke-linecap="round"/>
<path d="M14 54 Q14 30 22 22 Q30 14 54 14" fill="none" stroke="url(#gold)" stroke-width="1.8" stroke-linecap="round"/>
<path d="M54 14 q6 0 6 -5 q0 -4 -4 -3" fill="none" stroke="#2a1c0e" stroke-width="2.4"/>
<path d="M14 54 q0 6 -5 6 q-4 0 -3 -4" fill="none" stroke="#2a1c0e" stroke-width="2.4"/>
%s<circle cx="11" cy="11" r="6" fill="url(#gold)" stroke="#2a1c0e" stroke-width="1.6"/>
<circle cx="9.5" cy="9.5" r="2" fill="#fff8d8" opacity="0.85"/>
<circle cx="38" cy="22" r="2.6" fill="#c0392b" stroke="#2a1c0e" stroke-width="1"/>
<circle cx="22" cy="38" r="2.6" fill="#c0392b" stroke="#2a1c0e" stroke-width="1"/>""" % leaves
    svg("corner.svg", 64, 64, body)


def divider():
    leaves = ""
    for side in (-1, 1):
        for i, (x, a) in enumerate(((60, -0.6), (100, 0.5), (140, -0.5), (176, 0.45))):
            xx = 256 + side * x
            ang = (math.pi if side < 0 else 0) + a * side
            leaves += '<path d="%s" fill="url(#leaf)" stroke="#2a1c0e" stroke-width="1.2"/>\n' % leaf_path(xx, 32 + (4 if i % 2 else -4), ang, 16 - i * 2)
    body = """<path d="M30 32 Q90 14 150 32 T246 32 M266 32 Q326 50 386 32 T482 32" fill="none" stroke="#2a1c0e" stroke-width="5" stroke-linecap="round"/>
<path d="M30 32 Q90 14 150 32 T246 32 M266 32 Q326 50 386 32 T482 32" fill="none" stroke="url(#gold)" stroke-width="2.6" stroke-linecap="round"/>
<path d="M30 32 q-14 0 -14 -9 q0 -7 8 -6" fill="none" stroke="#2a1c0e" stroke-width="2.6"/>
<path d="M482 32 q14 0 14 9 q0 7 -8 6" fill="none" stroke="#2a1c0e" stroke-width="2.6"/>
%s<path d="M256 14 L270 32 L256 50 L242 32 Z" fill="url(#gold)" stroke="#2a1c0e" stroke-width="2"/>
<circle cx="256" cy="32" r="5" fill="#7e57c2" stroke="#2a1c0e" stroke-width="1.4"/>
<circle cx="254.5" cy="30.5" r="1.6" fill="#efe4ff"/>""" % leaves
    svg("divider.svg", 512, 64, body)


def ribbon():
    body = """<path d="M6 16 H44 L44 60 H6 L18 38 Z" fill="#2c4424" stroke="#1a1208" stroke-width="2.5" stroke-linejoin="round"/>
<path d="M394 16 H356 L356 60 H394 L382 38 Z" fill="#2c4424" stroke="#1a1208" stroke-width="2.5" stroke-linejoin="round"/>
<path d="M44 60 L56 68 L56 58 Z M356 60 L344 68 L344 58 Z" fill="#1c2c16"/>
<rect x="36" y="8" width="328" height="52" rx="3" fill="#4a6a3a" stroke="#1a1208" stroke-width="2.5"/>
<rect x="42" y="13" width="316" height="42" rx="2" fill="none" stroke="url(#gold)" stroke-width="2"/>
<rect x="36" y="9" width="328" height="8" fill="#ffffff" opacity="0.12"/>"""
    svg("ribbon.svg", 400, 72, body)


def roller():
    body = """<defs><linearGradient id="wood" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#4a2e18"/><stop offset="0.35" stop-color="#a8743e"/><stop offset="0.6" stop-color="#7a4e26"/><stop offset="1" stop-color="#3a2210"/></linearGradient>
<linearGradient id="roll" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#b08a58"/><stop offset="0.4" stop-color="#f4e6c4"/><stop offset="0.75" stop-color="#e0c896"/><stop offset="1" stop-color="#9a7446"/></linearGradient></defs>
<rect x="15" y="4" width="18" height="12" rx="5" fill="url(#gold)" stroke="#2a1c0e" stroke-width="1.6"/>
<rect x="15" y="240" width="18" height="12" rx="5" fill="url(#gold)" stroke="#2a1c0e" stroke-width="1.6"/>
<rect x="19" y="10" width="10" height="236" fill="url(#wood)" stroke="#2a1c0e" stroke-width="1.4"/>
<rect x="6" y="20" width="36" height="216" rx="14" fill="url(#roll)" stroke="#5a3c1e" stroke-width="2"/>
<path d="M14 26 V230" stroke="#fffaf0" stroke-width="2" opacity="0.5"/>"""
    svg("roller.svg", 48, 256, body)


def compass():
    pts = ""
    for i in range(8):
        a = i * math.pi / 4 - math.pi / 2
        long_ = i % 2 == 0
        r = 46 if long_ else 30
        w = 9 if long_ else 7
        tip = (64 + math.cos(a) * r, 64 + math.sin(a) * r)
        l = (64 + math.cos(a - math.pi / 2) * w, 64 + math.sin(a - math.pi / 2) * w)
        rr = (64 + math.cos(a + math.pi / 2) * w, 64 + math.sin(a + math.pi / 2) * w)
        dark = "#b33a2a" if i == 0 else ("#3e2a18" if long_ else "#7a5a36")
        light = "#e86a50" if i == 0 else ("url(#gold)" if long_ else "#e8d6aa")
        pts += '<path d="M64 64 L%.1f %.1f L%.1f %.1f Z" fill="%s" stroke="#2a1c0e" stroke-width="1.2"/>\n' % (l[0], l[1], tip[0], tip[1], light, )
        pts += '<path d="M64 64 L%.1f %.1f L%.1f %.1f Z" fill="%s" stroke="#2a1c0e" stroke-width="1.2"/>\n' % (rr[0], rr[1], tip[0], tip[1], dark)
    body = """<circle cx="64" cy="64" r="34" fill="#f4e8c8" fill-opacity="0.85" stroke="#2a1c0e" stroke-width="2"/>
<circle cx="64" cy="64" r="29" fill="none" stroke="#2a1c0e" stroke-width="1" stroke-dasharray="2 3"/>
%s<circle cx="64" cy="64" r="5" fill="url(#gold)" stroke="#2a1c0e" stroke-width="1.4"/>
<path d="M57 11 V1 L71 11 V1" fill="none" stroke="#2a1c0e" stroke-width="5" stroke-linejoin="round"/>
<path d="M57 11 V1 L71 11 V1" fill="none" stroke="#b33a2a" stroke-width="2.4" stroke-linejoin="round"/>""" % pts
    svg("compass.svg", 128, 128, body)


def spell_icons():
    w = 'fill="#fffaf0" stroke="#3a2410" stroke-width="3" stroke-linejoin="round"'
    icons = {
        1: """<path d="M64 10 C78 34 98 46 96 74 C94 100 78 116 64 116 C48 116 32 102 32 78 C32 62 40 54 46 44 C48 58 54 64 60 64 C56 46 58 28 64 10 Z" %s/>
<path d="M64 62 C72 74 80 82 78 96 C76 106 70 110 64 110 C56 110 50 104 50 94 C50 84 58 80 64 62 Z" fill="#ffd27a" opacity="0.9"/>""" % w,
        2: """<path d="M64 8 C76 34 98 56 98 80 C98 102 82 118 64 118 C46 118 30 102 30 80 C30 56 52 34 64 8 Z" %s/>
<path d="M46 80 C46 94 54 102 64 104" fill="none" stroke="#9fd2ff" stroke-width="6" stroke-linecap="round"/>
<ellipse cx="52" cy="62" rx="6" ry="10" fill="#ffffff" transform="rotate(25 52 62)"/>""" % w,
        3: """<path d="M10 50 H76 C90 50 98 40 96 30 C94 20 82 16 74 22 C68 28 72 38 80 36" fill="none" stroke="#3a2410" stroke-width="14" stroke-linecap="round"/>
<path d="M10 50 H76 C90 50 98 40 96 30 C94 20 82 16 74 22 C68 28 72 38 80 36" fill="none" stroke="#fffaf0" stroke-width="8" stroke-linecap="round"/>
<path d="M20 74 H100 C114 74 120 86 116 96 C112 106 98 106 94 98 C90 90 98 84 104 88" fill="none" stroke="#3a2410" stroke-width="14" stroke-linecap="round"/>
<path d="M20 74 H100 C114 74 120 86 116 96 C112 106 98 106 94 98 C90 90 98 84 104 88" fill="none" stroke="#fffaf0" stroke-width="8" stroke-linecap="round"/>
<path d="M30 98 H64" fill="none" stroke="#3a2410" stroke-width="12" stroke-linecap="round"/>
<path d="M30 98 H64" fill="none" stroke="#fffaf0" stroke-width="6" stroke-linecap="round"/>""",
        4: "".join('<ellipse cx="64" cy="36" rx="17" ry="27" %s transform="rotate(%d 64 64)"/>\n' % (w, a) for a in range(0, 360, 72)) + '<circle cx="64" cy="64" r="15" fill="#ffd84a" stroke="#3a2410" stroke-width="3"/>\n<circle cx="60" cy="60" r="4" fill="#fff6c8"/>',
        5: "".join('<g transform="rotate(%d 64 64)"><path d="M64 10 V118 M64 30 L50 18 M64 30 L78 18 M64 98 L50 110 M64 98 L78 110" fill="none" stroke="#3a2410" stroke-width="12" stroke-linecap="round"/><path d="M64 10 V118 M64 30 L50 18 M64 30 L78 18 M64 98 L50 110 M64 98 L78 110" fill="none" stroke="#fffaf0" stroke-width="6" stroke-linecap="round"/></g>\n' % a for a in (0, 60, 120)) + '<path d="M64 52 L74 64 L64 76 L54 64 Z" fill="#cfefff" stroke="#3a2410" stroke-width="3"/>',
        6: """<path d="M64 8 L104 52 H80 V96 H48 V52 H24 Z" %s/>
<path d="M64 22 L86 46 H70" fill="none" stroke="#ffffff" stroke-width="4" stroke-linecap="round" opacity="0.8"/>
<rect x="36" y="104" width="56" height="12" rx="6" %s/>""" % (w, w),
    }
    for s, body in icons.items():
        svg("spell_%d.svg" % s, 128, 128, body)


def small_icons():
    svg("quill.svg", 64, 64, """<path d="M54 6 C30 10 16 30 12 54 C24 40 36 30 54 6 Z" fill="#fbf4e2" stroke="#3a2410" stroke-width="2.4" stroke-linejoin="round"/>
<path d="M54 6 C38 20 24 36 12 54" fill="none" stroke="#3a2410" stroke-width="2"/>
<path d="M22 32 L30 36 M28 24 L36 28 M34 18 L42 21" stroke="#3a2410" stroke-width="1.6"/>
<path d="M12 54 L6 60" stroke="#3a2410" stroke-width="3" stroke-linecap="round"/>
<circle cx="8" cy="58" r="3" fill="#2a1c0e"/>""")
    svg("wisp.svg", 64, 64, """<defs><radialGradient id="w" cx="0.5" cy="0.62" r="0.5"><stop offset="0" stop-color="#ffffff"/><stop offset="0.45" stop-color="#a8e0ff"/><stop offset="1" stop-color="#3a8ad0"/></radialGradient></defs>
<path d="M32 4 C38 16 52 24 50 40 C48 54 40 60 32 60 C22 60 14 52 14 40 C14 30 22 26 24 18 C28 26 30 30 34 30 C30 22 30 12 32 4 Z" fill="url(#w)" stroke="#1e3a5a" stroke-width="2.4" stroke-linejoin="round"/>
<circle cx="27" cy="42" r="3" fill="#1e3a5a"/><circle cx="38" cy="42" r="3" fill="#1e3a5a"/>""")
    svg("leaf.svg", 64, 64, '<path d="%s" fill="url(#leaf)" stroke="#2a1c0e" stroke-width="3"/><path d="M10 50 L46 18" stroke="#2a1c0e" stroke-width="2"/>' % leaf_path(8, 54, -0.8, 62))
    svg("check.svg", 64, 64, '<path d="M10 34 L26 50 L56 12" fill="none" stroke="#2a1c0e" stroke-width="12" stroke-linecap="round" stroke-linejoin="round"/><path d="M10 34 L26 50 L56 12" fill="none" stroke="#5c8a3a" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>')


NPCS = {
    0: dict(outfit=(70, 104, 70), hair=(236, 232, 224), skin=0, hat=0, beard=True, bald=True),
    1: dict(outfit=(176, 60, 52), hair=(120, 70, 40), skin=0, hat=4, apron=True),
    2: dict(outfit=(96, 140, 84), hair=(90, 50, 30), skin=0, hat=0),
    3: dict(outfit=(96, 70, 52), hair=(40, 30, 26), skin=2, hat=0, beard=True, bald=True, apron=True),
    4: dict(outfit=(196, 96, 120), hair=(226, 190, 110), skin=0, hat=0, apron=True),
    5: dict(outfit=(70, 110, 190), hair=(170, 100, 50), skin=0, hat=3, kid=True),
    6: dict(outfit=(120, 92, 60), hair=(130, 100, 70), skin=1, hat=1, beard=True),
    7: dict(outfit=(120, 80, 140), hair=(200, 200, 206), skin=0, hat=4),
    8: dict(outfit=(170, 50, 46), hair=(150, 80, 40), skin=1, hat=3, beard=True),
    9: dict(outfit=(130, 128, 120), hair=(210, 210, 214), skin=0, hat=2, beard=True),
    10: dict(outfit=(220, 186, 60), hair=(190, 190, 190), skin=0, hat=6, beard=True),
    11: dict(outfit=(70, 66, 70), hair=(70, 60, 56), skin=1, hat=5, beard=True),
    12: dict(outfit=(80, 120, 180), hair=(190, 140, 80), skin=1, hat=1),
    13: dict(outfit=(92, 64, 150), hair=(190, 96, 50), skin=0, hat=7),
}

SKIN = {0: (232, 186, 150), 1: (196, 144, 104), 2: (140, 98, 70)}


def hexc(c, f=1.0):
    return "#%02x%02x%02x" % tuple(max(0, min(255, int(v * f))) for v in c)


def portrait(i, p):
    o, hr, sk = p["outfit"], p["hair"], SKIN[p["skin"]]
    kid = p.get("kid", False)
    hy = 60 if not kid else 64
    hrad = 25 if not kid else 23
    defs = """<radialGradient id="bg" cx="0.5" cy="0.35" r="0.7"><stop offset="0" stop-color="#f6ecd0"/><stop offset="1" stop-color="#c8b688"/></radialGradient>
<radialGradient id="face" cx="0.4" cy="0.35" r="0.75"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></radialGradient>
<linearGradient id="cloth" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>
<clipPath id="disc"><circle cx="64" cy="64" r="64"/></clipPath>""" % (hexc(sk, 1.08), hexc(sk, 0.86), hexc(o, 1.12), hexc(o, 0.7))
    s = ['<g clip-path="url(#disc)">', '<rect width="128" height="128" fill="url(#bg)"/>']
    s.append('<path d="M0 128 V120 C10 100 34 92 64 92 C94 92 118 100 128 120 V128 Z" fill="%s"/>' % hexc((150, 170, 110), 0.9))
    for k in range(5):
        x = 8 + k * 28
        s.append('<path d="%s" fill="%s" opacity="0.35"/>' % (leaf_path(x, 26 + (k % 2) * 8, -0.6 + k * 0.3, 14), hexc((110, 140, 80))))
    hood = p["hat"] == 2
    if hood:
        s.append('<path d="M%d %d C%d %d %d %d %d %d C%d %d %d %d %d %d Z" fill="%s" stroke="#2a1c0e" stroke-width="2.4"/>' % (
            64 - 38, 128, 64 - 40, 70, 64 - 30, hy - 40, 64, hy - 40, 64 + 30, hy - 40, 64 + 40, 70, 64 + 38, 128, hexc(o, 0.85)))
    if not p.get("bald") and p["hat"] not in (2,):
        s.append('<ellipse cx="64" cy="%d" rx="%d" ry="%d" fill="%s" stroke="#2a1c0e" stroke-width="2.4"/>' % (hy + 6, hrad + 6, hrad + 14, hexc(hr, 0.85)))
    s.append('<path d="M20 128 C22 104 40 94 64 94 C88 94 106 104 108 128 Z" fill="url(#cloth)" stroke="#2a1c0e" stroke-width="2.4"/>')
    if p.get("apron"):
        s.append('<path d="M46 128 V108 C46 102 82 102 82 108 V128 Z" fill="#efe6d2" stroke="#2a1c0e" stroke-width="2"/>')
    s.append('<path d="M54 92 C54 100 74 100 74 92 V84 H54 Z" fill="%s" stroke="#2a1c0e" stroke-width="2"/>' % hexc(sk, 0.9))
    s.append('<path d="M50 96 L64 108 L78 96" fill="none" stroke="%s" stroke-width="3"/>' % hexc(o, 0.5))
    s.append('<circle cx="64" cy="%d" r="%d" fill="url(#face)" stroke="#2a1c0e" stroke-width="2.4"/>' % (hy, hrad))
    s.append('<circle cx="%d" cy="%d" r="%d" fill="url(#face)" stroke="#2a1c0e" stroke-width="2"/>' % (64 - hrad, hy + 2, 5))
    s.append('<circle cx="%d" cy="%d" r="%d" fill="url(#face)" stroke="#2a1c0e" stroke-width="2"/>' % (64 + hrad, hy + 2, 5))
    if not p.get("bald") and p["hat"] not in (2,):
        s.append('<path d="M%d %d C%d %d %d %d %d %d C%d %d %d %d %d %d C%d %d %d %d %d %d Z" fill="%s" stroke="#2a1c0e" stroke-width="2.2"/>' % (
            64 - hrad - 2, hy + 2, 64 - hrad - 4, hy - 30, 64 + hrad + 4, hy - 30, 64 + hrad + 2, hy + 2,
            64 + hrad - 6, hy - 10, 64, hy - 18, 64 - 6, hy - 10, 64 - 12, hy - 18, 64 - hrad + 4, hy - 12, 64 - hrad - 2, hy + 2, hexc(hr)))
    if p.get("bald"):
        s.append('<ellipse cx="56" cy="%d" rx="9" ry="5" fill="#ffffff" opacity="0.35"/>' % (hy - 16))
        s.append('<path d="M%d %d q4 -8 10 -8 M%d %d q-4 -8 -10 -8" fill="none" stroke="%s" stroke-width="5" stroke-linecap="round"/>' % (64 - hrad + 1, hy - 2, 64 + hrad - 1, hy - 2, hexc(p["hair"])))
    s.append('<circle cx="55" cy="%d" r="3.2" fill="#2a1c0e"/><circle cx="73" cy="%d" r="3.2" fill="#2a1c0e"/>' % (hy - 1, hy - 1))
    s.append('<circle cx="54" cy="%d" r="1" fill="#ffffff"/><circle cx="72" cy="%d" r="1" fill="#ffffff"/>' % (hy - 2, hy - 2))
    s.append('<path d="M50 %d q5 -3 10 0 M68 %d q5 -3 10 0" fill="none" stroke="%s" stroke-width="2" stroke-linecap="round"/>' % (hy - 8, hy - 8, hexc(hr, 0.6)))
    s.append('<circle cx="49" cy="%d" r="5" fill="#e07a6a" opacity="0.35"/><circle cx="79" cy="%d" r="5" fill="#e07a6a" opacity="0.35"/>' % (hy + 8, hy + 8))
    s.append('<path d="M64 %d q-3 7 1 8" fill="none" stroke="#2a1c0e" stroke-width="1.8" stroke-linecap="round"/>' % (hy + 1))
    if p.get("beard"):
        s.append('<path d="M%d %d C%d %d %d %d 64 %d C%d %d %d %d %d %d C%d %d %d %d 64 %d C%d %d %d %d %d %d Z" fill="%s" stroke="#2a1c0e" stroke-width="2.2"/>' % (
            64 - hrad + 2, hy + 2, 64 - hrad, hy + 30, 64 - 12, hy + 38, hy + 40, 64 + 12, hy + 38, 64 + hrad, hy + 30, 64 + hrad - 2, hy + 2,
            64 + 14, hy + 14, 64 + 6, hy + 10, hy + 12, 64 - 6, hy + 10, 64 - 14, hy + 14, 64 - hrad + 2, hy + 2, hexc(hr)))
        s.append('<path d="M57 %d q7 4 14 0" fill="none" stroke="#2a1c0e" stroke-width="2" stroke-linecap="round"/>' % (hy + 15))
    else:
        s.append('<path d="M57 %d q7 5 14 0" fill="none" stroke="#2a1c0e" stroke-width="2" stroke-linecap="round"/>' % (hy + 13))
    hat = p["hat"]
    if hat == 1:
        s.append('<ellipse cx="64" cy="%d" rx="44" ry="10" fill="#dcb468" stroke="#2a1c0e" stroke-width="2.4"/>' % (hy - 16))
        s.append('<path d="M44 %d C44 %d 84 %d 84 %d Z" fill="#e8c47a" stroke="#2a1c0e" stroke-width="2.4"/>' % (hy - 16, hy - 46, hy - 46, hy - 16))
        s.append('<path d="M45 %d C55 %d 73 %d 83 %d" fill="none" stroke="#b03a2e" stroke-width="5"/>' % (hy - 20, hy - 16, hy - 16, hy - 20))
    elif hat == 3:
        s.append('<path d="M%d %d C%d %d %d %d %d %d Z" fill="#3c3c46" stroke="#2a1c0e" stroke-width="2.4"/>' % (64 - hrad - 2, hy - 8, 64 - hrad, hy - 40, 64 + hrad, hy - 40, 64 + hrad + 2, hy - 8))
        s.append('<path d="M52 %d Q64 %d 92 %d Q70 %d 52 %d Z" fill="#2a2a32" stroke="#2a1c0e" stroke-width="2"/>' % (hy - 10, hy - 16, hy - 4, hy - 2, hy - 10))
        s.append('<circle cx="64" cy="%d" r="3" fill="#5a5a66"/>' % (hy - 34))
    elif hat == 4:
        s.append('<path d="M%d %d C%d %d %d %d %d %d C%d %d %d %d %d %d Z" fill="#efe4cc" stroke="#2a1c0e" stroke-width="2.4"/>' % (
            64 - hrad - 4, hy + 6, 64 - hrad - 6, hy - 40, 64 + hrad + 6, hy - 40, 64 + hrad + 4, hy + 6, 64 + hrad - 4, hy - 14, 64 - hrad + 4, hy - 14, 64 - hrad - 4, hy + 6))
        s.append('<path d="M50 %d q14 -6 28 0" fill="none" stroke="%s" stroke-width="3" stroke-dasharray="3 4"/>' % (hy - 22, hexc(o)))
    elif hat == 5:
        s.append('<path d="M%d %d C%d %d %d %d %d %d Z" fill="#c8a058" stroke="#2a1c0e" stroke-width="2.4"/>' % (64 - hrad - 5, hy - 4, 64 - hrad - 2, hy - 44, 64 + hrad + 2, hy - 44, 64 + hrad + 5, hy - 4))
        s.append('<rect x="%d" y="%d" width="%d" height="6" rx="3" fill="#a07c3a" stroke="#2a1c0e" stroke-width="2"/>' % (64 - hrad - 8, hy - 8, 2 * hrad + 16))
        s.append('<circle cx="64" cy="%d" r="8" fill="#fff2b0" stroke="#2a1c0e" stroke-width="2"/><circle cx="64" cy="%d" r="14" fill="#ffe680" opacity="0.35"/>' % (hy - 26, hy - 26))
    elif hat == 6:
        s.append('<ellipse cx="64" cy="%d" rx="40" ry="11" fill="#e6be46" stroke="#2a1c0e" stroke-width="2.4" transform="rotate(-6 64 %d)"/>' % (hy - 14, hy - 14))
        s.append('<path d="M44 %d C44 %d 84 %d 84 %d Z" fill="#f0cc56" stroke="#2a1c0e" stroke-width="2.4"/>' % (hy - 16, hy - 44, hy - 44, hy - 18))
    elif hat == 7:
        s.append('<ellipse cx="64" cy="%d" rx="46" ry="10" fill="#46307a" stroke="#2a1c0e" stroke-width="2.4"/>' % (hy - 15))
        s.append('<path d="M38 %d C46 %d 58 %d 76 %d C86 %d 92 %d 98 %d C88 %d 86 %d 90 %d Z" fill="#5c40a0" stroke="#2a1c0e" stroke-width="2.4" stroke-linejoin="round"/>' % (
            hy - 16, hy - 50, hy - 70, hy - 76, hy - 79, hy - 70, hy - 62, hy - 58, hy - 40, hy - 16))
        s.append('<path d="M38 %d C52 %d 76 %d 90 %d" fill="none" stroke="url(#gold)" stroke-width="6"/>' % (hy - 18, hy - 24, hy - 24, hy - 18))
        s.append('<path d="M64 %d l3 6 l6 1 l-5 4 l1 6 l-5 -3 l-5 3 l1 -6 l-5 -4 l6 -1 Z" fill="url(#gold)" stroke="#2a1c0e" stroke-width="1"/>' % (hy - 50))
    s.append("</g>")
    svg("portrait_%d.svg" % i, 128, 128, "\n".join(s), defs)


if __name__ == "__main__":
    parchment()
    leather()
    vignette()
    map_image()
    corner()
    divider()
    ribbon()
    roller()
    compass()
    spell_icons()
    small_icons()
    for i, p in NPCS.items():
        portrait(i, p)
