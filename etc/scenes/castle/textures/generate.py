import os

import numpy as np
from PIL import Image, ImageDraw

OUT = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(os.path.dirname(OUT), "ui")


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


def stretched(h, w, beta, seed, sx, sy):
    n = noise(h // sy, w // sx, beta, seed)
    img = Image.fromarray((n * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    a = np.array(img) / 255.0
    return (a - a.min()) / (a.max() - a.min())


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
    return np.repeat(np.asarray(v)[..., None], 3, -1)


def save(name, a, quality=86, folder=OUT):
    a = np.clip(a, 0, 1)
    img = Image.fromarray((a * 255 + 0.5).astype(np.uint8))
    if a.ndim == 3 and a.shape[2] == 4:
        img.save(os.path.join(folder, name + ".png"), optimize=True)
    else:
        img.save(os.path.join(folder, name + ".jpg"), quality=quality, subsampling=0)


def blocks(h, w, courses, per_course, gap, seed, jitter=0.25, stagger=0.5):
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


def cells(h, w, n, seed, squash=1.0):
    rng = np.random.default_rng(seed)
    gx = max(1, int(round(np.sqrt(n * w / h / squash))))
    gy = max(1, int(round(n / gx)))
    jx, jy = np.meshgrid(np.arange(gx), np.arange(gy))
    jitter = rng.uniform(0.15, 0.85, (gy, gx, 2))
    pts = np.stack([(jx + jitter[..., 0]) * w / gx, (jy + jitter[..., 1]) * h / gy], -1).reshape(-1, 2)
    ids = rng.random(len(pts))
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    d1 = np.full((h, w), 1e9)
    d2 = np.full((h, w), 1e9)
    idx = np.zeros((h, w), dtype=int)
    for i, (px, py) in enumerate(pts):
        dx = np.abs(xx - px)
        dx = np.minimum(dx, w - dx)
        dy = np.abs(yy - py)
        dy = np.minimum(dy, h - dy) * squash
        d = np.sqrt(dx * dx + dy * dy)
        closer = d < d1
        d2 = np.where(closer, d1, np.minimum(d2, d))
        idx = np.where(closer, i, idx)
        d1 = np.where(closer, d, d1)
    return d1, d2, ids[idx]


def ashlar():
    h, w = 512, 512
    mask, ids = blocks(h, w, 8, 4, 3.2, 11, jitter=0.22)
    fine = noise(h, w, 0.7, 12)
    big = noise(h, w, 1.6, 13)
    chip = smooth(0.62, 0.8, noise(h, w, 1.1, 14))
    tone = 0.8 + ids * 0.2 + (big - 0.5) * 0.24 + (fine - 0.5) * 0.2
    face = gray(tone) * rgb(255, 250, 240)
    warm = smooth(0.82, 1.0, ids)
    face = mix(face, face * rgb(255, 232, 206), warm * 0.6)
    cool = smooth(0.0, 0.14, 0.14 - ids)
    face = mix(face, face * rgb(214, 220, 222), cool * 0.7)
    moss = smooth(0.7, 0.86, big + (fine - 0.5) * 0.3)
    face = mix(face, rgb(150, 162, 110) * (0.7 + fine[..., None] * 0.4), moss * 0.35)
    joint = gray(0.5 + fine * 0.12) * rgb(240, 232, 216)
    albedo = mix(joint, face, mask)
    albedo = albedo * (1 - chip[..., None] * 0.08 * mask[..., None])
    height = mask * (0.8 + ids * 0.2) + fine * 0.2 + big * 0.18 - chip * 0.1
    save("ashlar", albedo)
    save("ashlar_normal", normal_map(blur(height, 0.8), 3.0), 88)


def rubble():
    h, w = 512, 512
    d1, d2, ids = cells(h, w, 64, 21, squash=1.6)
    edge = d2 - d1
    mask = smooth(1.5, 6.0, edge)
    fine = noise(h, w, 0.8, 22)
    big = noise(h, w, 1.5, 23)
    dome = np.sqrt(np.clip(edge / 18, 0, 1))
    tone = 0.66 + ids * 0.3 + (fine - 0.5) * 0.24 + (big - 0.5) * 0.2
    face = gray(tone) * mix(rgb(255, 248, 236) * np.ones((h, w, 3)), rgb(232, 236, 240), ids)
    joint = gray(0.3 + fine * 0.1)
    albedo = mix(joint, face, mask)
    height = dome * mask + fine * 0.22
    save("rubble", albedo)
    save("rubble_normal", normal_map(height, 4.0), 88)


def slate():
    h, w = 512, 512
    rows, per_row = 10, 7
    rng = np.random.default_rng(31)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    rh = h / rows
    row = np.floor(yy / rh)
    fy = (yy - row * rh) / rh
    shift = (row % 2) * 0.5
    u = (xx / w * per_row + shift) % 1
    col = np.floor((xx / w * per_row + shift) % per_row)
    ids = np.zeros((h, w))
    for r in range(rows):
        vals = rng.random(per_row)
        sel = row == r
        ids[sel] = vals[col[sel].astype(int)]
    bottom = 1 - 0.32 * (1 - np.sqrt(np.clip(1 - (2 * u - 1) ** 2, 0, 1)))
    inside = smooth(0.0, 0.03, bottom - fy)
    side = smooth(0.0, 0.05, np.minimum(u, 1 - u))
    mask = inside * side
    fine = noise(h, w, 0.8, 32)
    big = noise(h, w, 1.4, 33)
    tone = 0.7 + ids * 0.3 + (fine - 0.5) * 0.22
    lichen = smooth(0.7, 0.84, big + (fine - 0.5) * 0.25)
    face = gray(tone) * (0.75 + 0.25 * fy[..., None])
    face = mix(face, rgb(196, 200, 150) * (0.8 + fine[..., None] * 0.3), lichen * 0.45)
    albedo = face * (0.35 + 0.65 * mask[..., None])
    height = mask * (0.4 + fy * 0.9) + fine * 0.12
    save("slate", albedo)
    save("slate_normal", normal_map(height, 3.6), 88)


def rooftile():
    h, w = 512, 512
    rows, per_row = 9, 8
    rng = np.random.default_rng(41)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    rh = h / rows
    row = np.floor(yy / rh)
    fy = (yy - row * rh) / rh
    u = (xx / w * per_row) % 1
    col = np.floor(xx / w * per_row)
    ids = np.zeros((h, w))
    for r in range(rows):
        vals = rng.random(per_row)
        sel = row == r
        ids[sel] = vals[col[sel].astype(int)]
    barrel = np.sin(u * np.pi)
    lip = smooth(0.82, 0.98, fy)
    fine = noise(h, w, 0.8, 42)
    big = noise(h, w, 1.5, 43)
    tone = 0.72 + ids * 0.3 + (fine - 0.5) * 0.2 + (big - 0.5) * 0.16
    face = gray(tone) * (0.62 + 0.38 * barrel[..., None]) * (1 - lip[..., None] * 0.4)
    moss = smooth(0.72, 0.86, big + (fine - 0.5) * 0.3)
    face = mix(face, rgb(150, 160, 96) * (0.7 + fine[..., None] * 0.4), moss * 0.4)
    albedo = face * rgb(255, 246, 236)
    height = barrel * 0.6 + fy * 0.8 - lip * 0.8 + fine * 0.1
    save("rooftile", albedo)
    save("rooftile_normal", normal_map(height, 3.2), 88)


def thatch():
    h, w = 512, 512
    straw = stretched(h, w, 0.6, 51, 1, 24)
    coarse = stretched(h, w, 1.0, 52, 2, 8)
    rows = 6
    yy = np.arange(h)[:, None] / h * rows % 1
    ridge = smooth(0.0, 0.85, yy) * (1 - smooth(0.9, 1.0, yy))
    fine = noise(h, w, 0.9, 53)
    tone = 0.62 + straw * 0.34 + (coarse - 0.5) * 0.24
    tone = tone * (0.7 + ridge * 0.3)
    old = smooth(0.5, 0.8, noise(h, w, 1.6, 54))
    albedo = gray(tone) * mix(rgb(255, 244, 220) * np.ones((h, w, 3)), rgb(190, 188, 176), old * 0.6)
    albedo = albedo * (0.9 + fine[..., None] * 0.14)
    height = straw * 0.5 + coarse * 0.4 + ridge * 0.9
    save("thatch", albedo)
    save("thatch_normal", normal_map(height, 3.4), 88)


def plaster():
    h, w = 256, 256
    n = noise(h, w, 1.1, 61)
    f = noise(h, w, 0.6, 62)
    stain = smooth(0.62, 0.9, noise(h, w, 1.8, 63))
    tone = 0.86 + n * 0.14 + (f - 0.5) * 0.06 - stain * 0.1
    albedo = gray(tone) * mix(np.ones((h, w, 3)), rgb(236, 226, 200), stain)
    save("plaster", albedo)
    save("plaster_normal", normal_map(n * 1.1 + f * 0.14, 1.0), 86)


def planks():
    h, w = 512, 512
    boards = 6
    rng = np.random.default_rng(71)
    grain = stretched(h, w, 0.8, 72, 16, 1)
    rings = 0.5 + 0.5 * np.sin(grain * 36)
    tone = np.ones((h, w))
    mask = np.ones((h, w))
    for b in range(boards):
        y0, y1 = int(b * h / boards), int((b + 1) * h / boards)
        tone[y0:y1] = 0.76 + rng.random() * 0.26
        d = np.minimum(np.arange(y1 - y0), y1 - y0 - 1 - np.arange(y1 - y0))[:, None]
        mask[y0:y1] = smooth(1, 4, d)
        cut = int(rng.random() * w)
        xs = np.arange(w)
        dx = np.minimum(np.abs(xs - cut), w - np.abs(xs - cut))
        mask[y0:y1] *= smooth(1, 3, dx)[None, :]
    v = tone * (0.74 + rings * 0.18 + grain * 0.16) * (0.15 + 0.85 * mask)
    save("planks", gray(v) * rgb(255, 242, 226))
    save("planks_normal", normal_map(mask * 1.2 + rings * 0.08 + grain * 0.14, 3), 88)


def bark():
    h, w = 256, 256
    ridges = stretched(h, w, 0.8, 81, 1, 16)
    n = noise(h, w, 0.8, 82)
    groove = smooth(0.25, 0.7, ridges)
    v = (0.45 + groove * 0.6) * (0.85 + n * 0.3)
    save("bark", gray(v))
    save("bark_normal", normal_map(groove * 1.6 + n * 0.25, 4), 86)


def grass():
    h, w = 512, 512
    big = noise(h, w, 1.6, 91)
    mid = noise(h, w, 1.0, 92)
    fine = noise(h, w, 0.3, 93)
    blades = noise(h, w, 0.05, 94)
    tone = 0.74 + (big - 0.5) * 0.24 + (mid - 0.5) * 0.18 + (fine - 0.5) * 0.3 + (blades - 0.5) * 0.18
    dry = smooth(0.62, 0.82, mid)
    albedo = gray(tone) * mix(rgb(250, 255, 236) * np.ones((h, w, 3)), rgb(255, 238, 196), dry * 0.7)
    rng = np.random.default_rng(95)
    img = Image.new("RGB", (w, h), 0)
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(img)
    dm = ImageDraw.Draw(m)
    colors = [(255, 255, 250), (255, 236, 120), (255, 190, 220), (210, 200, 255)]
    for _ in range(260):
        px, py = rng.uniform(0, w), rng.uniform(0, h)
        c = colors[rng.choice(4, p=[0.55, 0.3, 0.1, 0.05])]
        r = rng.uniform(1.0, 2.0)
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                d.ellipse((px + ox - r, py + oy - r, px + ox + r, py + oy + r), fill=c)
                dm.ellipse((px + ox - r, py + oy - r, px + ox + r, py + oy + r), fill=255)
    flowers = np.array(img) / 255.0
    fmask = np.array(m) / 255.0 * smooth(0.45, 0.65, big)
    albedo = mix(albedo, flowers * 2.4, fmask)
    save("grass", albedo)
    save("grass_normal", normal_map(fine * 0.8 + blades * 0.6 + mid * 0.3, 2.4), 86)


def earth():
    h, w = 512, 512
    big = noise(h, w, 1.6, 101)
    mid = noise(h, w, 1.0, 102)
    fine = noise(h, w, 0.4, 103)
    tone = 0.76 + (big - 0.5) * 0.26 + (mid - 0.5) * 0.18 + (fine - 0.5) * 0.24
    albedo = gray(tone)
    rng = np.random.default_rng(104)
    img = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(img)
    for _ in range(900):
        px, py = rng.uniform(0, w), rng.uniform(0, h)
        r = rng.uniform(1.2, 4.5)
        tone_p = int(rng.uniform(120, 255))
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                d.ellipse((px + ox - r * 1.3, py + oy - r, px + ox + r * 1.3, py + oy + r), fill=tone_p)
    pebbles = np.array(img) / 255.0
    pmask = (pebbles > 0).astype(float)
    albedo = mix(albedo, gray(0.6 + pebbles * 0.5) * rgb(236, 236, 240), pmask * 0.85)
    height = mid * 0.4 + fine * 0.4 + blur(pebbles, 1.2) * 1.2
    save("earth", albedo)
    save("earth_normal", normal_map(height, 3.0), 86)


def cobble():
    h, w = 512, 512
    d1, d2, ids = cells(h, w, 100, 111, squash=1.0)
    edge = d2 - d1
    mask = smooth(2.0, 7.0, edge)
    dome = np.sqrt(np.clip(edge / 22, 0, 1))
    fine = noise(h, w, 0.8, 112)
    tone = 0.66 + ids * 0.32 + (fine - 0.5) * 0.2
    face = gray(tone) * mix(rgb(255, 246, 232) * np.ones((h, w, 3)), rgb(226, 232, 238), ids)
    joint = gray(0.28 + fine * 0.14) * rgb(255, 236, 206)
    albedo = mix(joint, face * (0.8 + dome[..., None] * 0.3), mask)
    height = dome * mask * 1.2 + fine * 0.15
    save("cobble", albedo)
    save("cobble_normal", normal_map(height, 3.6), 88)


def water():
    h, w = 512, 512
    yy, xx = np.mgrid[0:h, 0:w] / h
    height = np.zeros((h, w))
    rng = np.random.default_rng(121)
    warp = noise(h, w, 1.8, 122) * 2 * np.pi
    for _ in range(26):
        fx, fy = rng.integers(-12, 13), rng.integers(4, 18)
        height += np.sin(2 * np.pi * (fx * xx + fy * yy) + rng.uniform(0, 6.28) + warp * 1.4) / (fx * fx + fy * fy) ** 0.5
    height = height * 0.6 + noise(h, w, 1.3, 123) * 3
    height = (height - height.min()) / (height.max() - height.min())
    save("water_normal", normal_map(height, 3.0), 88)


def leaves():
    h, w = 256, 256
    rng = np.random.default_rng(131)
    img = Image.new("L", (w * 3, h * 3), 0)
    d = ImageDraw.Draw(img)
    for _ in range(1400):
        px, py = rng.uniform(w, 2 * w), rng.uniform(h, 2 * h)
        length, width = rng.uniform(7, 13), rng.uniform(3.5, 6)
        a = rng.uniform(0, np.pi)
        shade = int(rng.uniform(110, 255))
        ca, sa = np.cos(a), np.sin(a)
        pts = []
        for t in np.linspace(0, 2 * np.pi, 10, endpoint=False):
            lx, ly = np.cos(t) * length, np.sin(t) * width
            pts.append((lx * ca - ly * sa, lx * sa + ly * ca))
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                d.polygon([(px + ox + x, py + oy + y) for x, y in pts], fill=shade)
    a = np.array(img)[h:2 * h, w:2 * w] / 255.0
    gap = (a == 0).astype(float)
    v = 0.35 + a * 0.75
    v = v * (1 - gap * 0.45)
    save("leaves", gray(v))
    save("leaves_normal", normal_map(blur(a, 1.0) * 1.4, 3.0), 86)


def furrows():
    h, w = 512, 512
    rows = 12
    yy = np.arange(h)[:, None] / h * rows % 1
    ridge = 0.5 + 0.5 * np.cos(yy * 2 * np.pi)
    ridge = ridge * np.ones((1, w))
    plants = stretched(h, w, 0.4, 141, 1, 1)
    fine = noise(h, w, 0.5, 142)
    big = noise(h, w, 1.5, 143)
    tone = 0.74 + ridge * 0.22 + (fine - 0.5) * 0.24 + (big - 0.5) * 0.16 + (plants - 0.5) * 0.14
    save("furrows", gray(tone))
    save("furrows_normal", normal_map(ridge * 2.0 + fine * 0.3, 2.6), 86)


def ui_wood():
    h, w = 512, 2048
    rng = np.random.default_rng(201)
    boards = 9
    grain = stretched(h, w, 0.8, 202, 24, 1)
    fine = stretched(h, w, 0.5, 203, 6, 1)
    knots = noise(h, w, 1.8, 204)
    rings = 0.5 + 0.5 * np.sin(grain * 44 + fine * 6)
    tone = np.ones((h, w))
    mask = np.ones((h, w))
    shade = np.ones((h, w))
    for b in range(boards):
        y0, y1 = int(b * h / boards), int((b + 1) * h / boards)
        tone[y0:y1] = 0.82 + rng.random() * 0.22
        n = y1 - y0
        d = np.minimum(np.arange(n), n - 1 - np.arange(n))[:, None]
        mask[y0:y1] = smooth(0.5, 3.5, d)
        t = np.arange(n)[:, None] / n
        shade[y0:y1] = 1.08 - 0.18 * t
    v = tone * (0.66 + rings * 0.2 + grain * 0.2 + (fine - 0.5) * 0.12) * shade
    v = v * (1 - smooth(0.82, 0.95, knots) * 0.3)
    base = mix(rgb(150, 98, 58) * np.ones((h, w, 3)), rgb(118, 72, 40), 1 - grain)
    albedo = base * v[..., None] * (0.25 + 0.75 * mask[..., None])
    save("wood", albedo, 90, UI)


def ui_parchment():
    h, w = 256, 1024
    big = noise(h, w, 1.8, 211)
    mid = noise(h, w, 1.1, 212)
    fine = noise(h, w, 0.4, 213)
    tone = 0.92 + (big - 0.5) * 0.12 + (mid - 0.5) * 0.08 + (fine - 0.5) * 0.06
    stain = smooth(0.66, 0.9, big + (mid - 0.5) * 0.4)
    albedo = gray(tone) * rgb(244, 226, 186)
    albedo = mix(albedo, albedo * rgb(214, 176, 120), stain * 0.6)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    ex = np.minimum(xx, w - 1 - xx) / 60
    ey = np.minimum(yy, h - 1 - yy) / 40
    e = np.minimum(ex, ey) + (mid - 0.5) * 0.5
    burn = 1 - smooth(0.0, 1.0, e)
    albedo = mix(albedo, albedo * rgb(150, 100, 56), burn * 0.8)
    save("parchment", albedo, 90, UI)


for generate in (ashlar, rubble, slate, rooftile, thatch, plaster, planks, bark, grass, earth,
                 cobble, water, leaves, furrows, ui_wood, ui_parchment):
    generate()
