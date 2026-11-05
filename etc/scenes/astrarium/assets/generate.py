#!/usr/bin/env python3
import math
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "icons")
RNG = np.random.default_rng(1609)


def write(path, text):
    with open(os.path.join(HERE, path), "w") as f:
        f.write(text)


def svg(size, body, defs=""):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">'
            '<defs>%s</defs>%s</svg>\n' % (size, size, size, size, defs, body))


def polar(cx, cy, r, a):
    return cx + r * math.cos(a), cy + r * math.sin(a)


def tick(cx, cy, r0, r1, a, width, opacity=1):
    x0, y0 = polar(cx, cy, r0, a)
    x1, y1 = polar(cx, cy, r1, a)
    return ('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" stroke="#fff" stroke-width="%.2f" '
            'opacity="%.2f"/>' % (x0, y0, x1, y1, width, opacity))


def ring(cx, cy, r, width, opacity=1, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    return ('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="none" stroke="#fff" stroke-width="%.2f" '
            'opacity="%.2f"%s/>' % (cx, cy, r, width, opacity, d))


def arc(cx, cy, r, a0, a1, width, opacity=1):
    x0, y0 = polar(cx, cy, r, a0)
    x1, y1 = polar(cx, cy, r, a1)
    large = 1 if a1 - a0 > math.pi else 0
    return ('<path d="M%.2f %.2f A%.2f %.2f 0 %d 1 %.2f %.2f" fill="none" stroke="#fff" '
            'stroke-width="%.2f" stroke-linecap="round" opacity="%.2f"/>'
            % (x0, y0, r, r, large, x1, y1, width, opacity))


def dot(x, y, r, opacity=1):
    return '<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#fff" opacity="%.2f"/>' % (x, y, r, opacity)


def diamond(cx, cy, r, a, ratio=0.5, opacity=1):
    p = []
    for i in range(4):
        rr = r if i % 2 == 0 else r * ratio
        p.append("%.2f,%.2f" % polar(cx, cy, rr, a + i * math.pi / 2))
    return '<polygon points="%s" fill="#fff" opacity="%.2f"/>' % (" ".join(p), opacity)


def sparkle(cx, cy, r, inner=0.12, points=4, turn=0.0, opacity=1):
    p = []
    for i in range(points * 2):
        rr = r if i % 2 == 0 else r * inner
        p.append("%.2f,%.2f" % polar(cx, cy, rr, turn + i * math.pi / points))
    return '<polygon points="%s" fill="#fff" opacity="%.2f"/>' % (" ".join(p), opacity)


FIGURES = [
    [(-1, .5), (-.4, -.5), (.2, .1), (.9, -.7)],
    [(-.9, -.6), (-.2, .6), (.6, .2), (.9, .8)],
    [(-1, 0), (-.3, -.7), (.3, -.1), (.9, .4), (.4, .9)],
    [(-.8, .8), (-.5, -.2), (.2, -.8), (.8, -.1)],
    [(-1, -.2), (-.3, .5), (.3, .5), (1, -.2)],
    [(-.9, .3), (-.2, -.5), (.5, -.2), (.2, .7)],
]


def astro_outer():
    c, b = 512, []
    b += [ring(c, c, 506, 2.6), ring(c, c, 494, 1, .7), ring(c, c, 446, 1, .75), ring(c, c, 438, 2.2)]
    for i in range(360):
        a = i * math.pi / 180
        if i % 30 == 0:
            continue
        if i % 10 == 0:
            b.append(tick(c, c, 472, 494, a, 2.2))
        elif i % 5 == 0:
            b.append(tick(c, c, 480, 494, a, 1.6, .85))
        else:
            b.append(tick(c, c, 486, 494, a, 1, .55))
    for i in range(12):
        a = i * math.pi / 6
        x, y = polar(c, c, 466, a)
        b.append(diamond(x, y, 26, a, .32))
        fig = FIGURES[i % len(FIGURES)]
        mid = a + math.pi / 12
        fx, fy = polar(c, c, 459, mid)
        pts = []
        for px, py in fig:
            rx = fx + (px * math.cos(mid + math.pi / 2) - py * math.sin(mid + math.pi / 2)) * 15
            ry = fy + (px * math.sin(mid + math.pi / 2) + py * math.cos(mid + math.pi / 2)) * 9
            pts.append((rx, ry))
        b.append('<polyline points="%s" fill="none" stroke="#fff" stroke-width="1" opacity=".6"/>'
                 % " ".join("%.1f,%.1f" % p for p in pts))
        b += [dot(x2, y2, 2.1) for x2, y2 in pts]
    return svg(1024, "".join(b))


def astro_mid():
    c, b = 512, []
    b += [ring(c, c, 398, 2), ring(c, c, 386, 1, .6)]
    n = 72
    for i in range(n):
        a = i * 2 * math.pi / n
        if i % 2 == 0:
            x0, y0 = polar(c, c, 398, a - .018)
            x1, y1 = polar(c, c, 398, a + .018)
            x2, y2 = polar(c, c, 414, a + .012)
            x3, y3 = polar(c, c, 414, a - .012)
            b.append('<polygon points="%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" fill="#fff"/>'
                     % (x0, y0, x1, y1, x2, y2, x3, y3))
    for i in range(8):
        a0 = i * math.pi / 4 + .06
        a1 = (i + 1) * math.pi / 4 - .06
        b.append(arc(c, c, 352, a0, a1, 2.4, .9))
        x, y = polar(c, c, 352, i * math.pi / 4)
        b.append(ring(x, y, 11, 1.8))
        b.append(dot(x, y, 4))
    b.append(ring(c, c, 322, 1, .45))
    for i in range(48):
        b.append(tick(c, c, 322, 330 if i % 4 else 340, i * math.pi / 24, 1.4, .7))
    return svg(1024, "".join(b))


def astro_inner():
    c, b = 512, []
    b += [ring(c, c, 268, 1.6), ring(c, c, 258, 1, .5, "2 8")]
    for i in range(3):
        a0 = i * 2 * math.pi / 3 + .3
        b.append(arc(c, c, 232, a0, a0 + 1.5, 3.4, .95))
        x, y = polar(c, c, 232, a0 + 1.8)
        b.append(dot(x, y, 8))
        b.append(ring(x, y, 15, 1, .7))
    b.append(ring(c, c, 192, 1.2, .5))
    for i in range(24):
        a = i * math.pi / 12
        b.append(tick(c, c, 178, 192, a, 1.3, .7))
    mx, my = polar(c, c, 268, -.7)
    b.append('<path d="M%.1f %.1f a26 26 0 1 0 22 42 a20 20 0 1 1 -22 -42Z" fill="#fff"/>' % (mx - 10, my - 20))
    return svg(1024, "".join(b))


def astro_rose():
    c, b = 512, []
    for i in range(8):
        a = i * math.pi / 4
        length = 168 if i % 2 == 0 else 104
        half = .32 if i % 2 == 0 else .36
        tip = polar(c, c, length, a)
        left = polar(c, c, length * .24, a - half * 1.8)
        right = polar(c, c, length * .24, a + half * 1.8)
        b.append('<polygon points="%.1f,%.1f %.1f,%.1f %d,%d" fill="#fff" opacity=".95"/>'
                 % (tip[0], tip[1], left[0], left[1], c, c))
        b.append('<polygon points="%.1f,%.1f %.1f,%.1f %d,%d" fill="#fff" opacity=".42"/>'
                 % (tip[0], tip[1], right[0], right[1], c, c))
    b += [ring(c, c, 56, 1.6), ring(c, c, 44, 1, .6), dot(c, c, 9)]
    return svg(1024, "".join(b))


def rays():
    c, b = 256, []
    for i in range(16):
        a = i * math.pi / 8
        w = .05 if i % 2 else .1
        p0 = polar(c, c, 250, a - w)
        p1 = polar(c, c, 250, a + w)
        b.append('<polygon points="%d,%d %.1f,%.1f %.1f,%.1f" fill="url(#r)"/>' % (c, c, p0[0], p0[1], p1[0], p1[1]))
    defs = ('<radialGradient id="r" gradientUnits="userSpaceOnUse" cx="256" cy="256" r="250">'
            '<stop offset="0" stop-color="#fff" stop-opacity=".9"/>'
            '<stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>')
    return svg(512, "".join(b), defs)


def corner():
    return ('<g fill="none" stroke="#fff" stroke-linecap="round">'
            '<path d="M2 62 V14 Q2 2 14 2 H62" stroke-width="2"/>'
            '<path d="M9 44 V19 Q9 9 19 9 H44" stroke-width="1" opacity=".7"/>'
            '<path d="M15 15 Q27 16 27 27 Q27 34 20 34 Q15 34 15 29" stroke-width="1.3"/>'
            '<path d="M15 15 Q16 27 27 27 Q34 27 34 20 Q34 15 29 15" stroke-width="1.3"/>'
            '</g><polygon points="9,2 13,6 9,10 5,6" fill="#fff" transform="translate(-3 -2)"/>')


def frame():
    b = ['<rect x=".5" y=".5" width="95" height="95" rx="5" fill="none" stroke="#fff" '
         'stroke-width="1" opacity=".55"/>']
    for turn in (0, 90, 180, 270):
        b.append('<g transform="rotate(%d 48 48) translate(4 4) scale(.46875)">%s</g>' % (turn, corner()))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 96 96" width="192" height="192">'
            '%s</svg>\n' % "".join(b))


def divider():
    b = ('<g fill="none" stroke="#fff" stroke-linecap="round">'
         '<path d="M6 16 H120" stroke-width="1" opacity=".9"/>'
         '<path d="M136 16 H250" stroke-width="1" opacity=".9"/>'
         '<path d="M116 16 Q122 8 128 16 Q134 24 140 16" stroke-width="1.2"/>'
         '<path d="M116 16 Q122 24 128 16 Q134 8 140 16" stroke-width="1.2"/></g>'
         + diamond(128, 16, 4, 0, 1) + dot(4, 16, 1.6) + dot(252, 16, 1.6))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 32" width="512" height="64">'
            '%s</svg>\n' % b)


def orb_frame():
    c, b = 256, []
    defs = ('<linearGradient id="m" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff"/>'
            '<stop offset=".5" stop-color="#9a9a9a"/><stop offset="1" stop-color="#e0e0e0"/></linearGradient>')
    b.append('<circle cx="256" cy="256" r="228" fill="none" stroke="url(#m)" stroke-width="20"/>')
    b.append('<circle cx="256" cy="256" r="214" fill="none" stroke="#000" stroke-width="5" opacity=".65"/>')
    b.append('<circle cx="256" cy="256" r="240" fill="none" stroke="#fff" stroke-width="2"/>')
    for i in range(60):
        a = i * math.pi / 30
        b.append(tick(c, c, 222, 234, a, 2.2 if i % 5 == 0 else 1.2, .9 if i % 5 == 0 else .45))
    for i in range(4):
        a = math.pi / 4 + i * math.pi / 2
        x, y = polar(c, c, 228, a)
        b.append(diamond(x, y, 19, a, .6))
    return svg(512, "".join(b), defs)


def orb_gloss():
    b = ('<g fill="none" stroke="#fff" stroke-linecap="round">'
         '<path d="M104 196 A172 172 0 0 1 212 94" stroke-width="13" opacity=".2"/>'
         '<path d="M236 88 A172 172 0 0 1 258 86" stroke-width="13" opacity=".2"/>'
         '<path d="M408 322 A172 172 0 0 1 300 424" stroke-width="7" opacity=".1"/></g>')
    return svg(512, b)


def dashes():
    b = ring(128, 128, 118, 4, 1, "22 12") + ring(128, 128, 104, 1.5, .6)
    for i in range(4):
        x, y = polar(128, 128, 118, i * math.pi / 2)
        b += diamond(x, y, 9, i * math.pi / 2, .7)
    return svg(256, b)


def sigil():
    b = sparkle(64, 64, 62, .11) + sparkle(64, 64, 26, .2, 4, math.pi / 4, .7) + dot(64, 64, 7)
    return svg(128, b)


def spell(name, body):
    write("icons/%s.svg" % name, svg(64, body))


def spells():
    spell("lance", '<path d="M56 6 L34 18 L39 21 L8 52 L12 56 L43 25 L46 30Z" fill="#fff"/>'
          '<path d="M20 12 q6 8 0 14 q-8 -5 0 -14Z M46 42 q7 7 2 15 q-9 -6 -2 -15Z" fill="#fff" opacity=".6"/>')
    spell("mend", '<path d="M32 6 C44 24 52 31 52 42 A20 20 0 0 1 12 42 C12 31 20 24 32 6Z" fill="#fff"/>'
          '<path d="M29 30 H35 V38 H43 V44 H35 V52 H29 V44 H21 V38 H29Z" fill="#000" opacity=".72"/>')
    spell("aegis", '<path d="M32 4 L54 12 V30 C54 44 44 54 32 60 C20 54 10 44 10 30 V12Z" fill="#fff"/>'
          '<path d="M32 14 L44 19 V30 C44 39 38 45 32 49 Z" fill="#000" opacity=".42"/>'
          '<path d="M32 22 v20 M24 30 l8 6 l8 -6" stroke="#000" stroke-width="2.4" fill="none" opacity=".7"/>')
    spell("veil", '<path d="M42 6 A27 27 0 1 0 42 58 A21 21 0 1 1 42 6Z" fill="#fff"/>'
          '<path d="M38 22 H58 M42 32 H62 M38 42 H56" stroke="#fff" stroke-width="3.4" stroke-linecap="round" opacity=".7"/>')
    spell("starfall", sparkle(40, 26, 22, .16) + dot(40, 26, 3)
          + '<path d="M8 56 L22 42 M4 40 L14 30 M22 60 L30 52" stroke="#fff" stroke-width="3" stroke-linecap="round" opacity=".7"/>')
    spell("pact", '<path d="M32 56 C8 40 6 26 12 18 C19 9 29 13 32 22 C35 13 45 9 52 18 C58 26 56 40 32 56Z" fill="#fff"/>'
          '<path d="M34 8 L28 30 L34 28 L30 50 L42 22 L36 24 L42 8Z" fill="#000" opacity=".72"/>')
    spell("might", '<path d="M46 6 H58 V18 L28 48 L34 54 L28 60 L20 52 L10 60 L4 54 L12 44 L4 36 L10 30 L16 36Z" fill="#fff"/>')
    spell("ward", '<path d="M32 4 L54 12 V30 C54 44 44 54 32 60 C20 54 10 44 10 30 V12Z" fill="#fff"/>')
    spell("haste", '<path d="M8 10 L28 32 L8 54 H20 L40 32 L20 10Z M28 10 L48 32 L28 54 H40 L60 32 L40 10Z" fill="#fff"/>')
    spell("aether", '<path d="M32 4 C44 22 54 30 54 42 A22 22 0 0 1 10 42 C10 30 20 22 32 4Z" fill="#fff"/>')
    spell("heart", '<path d="M32 58 C6 40 4 24 11 15 C19 6 29 11 32 20 C35 11 45 6 53 15 C60 24 58 40 32 58Z" fill="#fff"/>')
    spell("blade", '<path d="M52 4 H60 V12 L24 48 L16 40Z" fill="#fff"/><path d="M12 36 L28 52 L24 56 L18 50 L10 58 L6 54 L14 46 L8 40Z" fill="#fff"/>')
    spell("relic", '<circle cx="32" cy="30" r="22" fill="#fff"/><path d="M18 54 H46 L42 60 H22Z" fill="#fff"/>')
    spell("crown", '<path d="M6 18 L18 32 L32 8 L46 32 L58 18 L52 50 H12Z" fill="#fff"/>')
    spell("band", '<path d="M32 16 A22 22 0 1 0 32 60 A22 22 0 1 0 32 16Z M32 25 A13 13 0 1 1 32 51 A13 13 0 1 1 32 25Z" fill="#fff" fill-rule="evenodd"/><polygon points="32,4 42,14 32,24 22,14" fill="#fff"/>')


def grad(a, b, id_="g"):
    return ('<linearGradient id="%s" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="%s"/>'
            '<stop offset="1" stop-color="%s"/></linearGradient>' % (id_, a, b))


def item_blade(name, a, b, c, curve):
    defs = grad(a, b) + grad("#f8e6b0", "#8a6a2a", "h")
    body = ('<path d="M8 8 C%d 16 %d 34 52 50 L46 56 C28 42 12 %d 8 8Z" fill="url(#g)" stroke="%s" stroke-width="1"/>'
            '<path d="M8 8 C%d 18 %d 34 49 53" fill="none" stroke="#fff" stroke-width="1.2" opacity=".7"/>'
            '<path d="M38 56 L58 36 L62 40 L42 60Z" fill="url(#h)"/>'
            '<path d="M50 50 L60 60" stroke="url(#h)" stroke-width="6" stroke-linecap="round"/>'
            '<circle cx="50" cy="48" r="3" fill="%s"/>' % (curve, 26, 38 - curve // 4, c, curve - 4, 22, c))
    write("icons/%s.svg" % name, svg(64, body, defs))


def item_orb(name, a, b, c):
    defs = ('<radialGradient id="g" cx=".38" cy=".32" r=".8"><stop offset="0" stop-color="#fff"/>'
            '<stop offset=".35" stop-color="%s"/><stop offset="1" stop-color="%s"/></radialGradient>' % (a, b)
            + grad("#f8e6b0", "#8a6a2a", "h"))
    body = ('<circle cx="32" cy="27" r="21" fill="url(#g)"/>'
            '<ellipse cx="32" cy="27" rx="29" ry="9" fill="none" stroke="url(#h)" stroke-width="2.4" transform="rotate(-18 32 27)"/>'
            '<path d="M20 49 H44 L49 58 H15Z" fill="url(#h)"/><circle cx="25" cy="19" r="4.5" fill="#fff" opacity=".85"/>'
            '<path d="M32 15 l2.5 9 l9 3 l-9 2.5 l-2.5 9 l-3 -9 l-9 -2.5 l9 -3Z" fill="%s" opacity=".85"/>' % c)
    write("icons/%s.svg" % name, svg(64, body, defs))


def item_crown(name, a, b, gem):
    defs = grad(a, b)
    body = ('<path d="M6 22 L18 34 L32 8 L46 34 L58 22 L53 52 H11Z" fill="url(#g)" stroke="#fff" stroke-opacity=".5"/>'
            '<path d="M11 45 H53 V52 H11Z" fill="#000" opacity=".28"/>'
            '<circle cx="6" cy="20" r="3.4" fill="%s"/><circle cx="32" cy="7" r="4" fill="%s"/><circle cx="58" cy="20" r="3.4" fill="%s"/>'
            '<polygon points="32,30 38,38 32,46 26,38" fill="%s" stroke="#fff" stroke-opacity=".7"/>' % (gem, gem, gem, gem))
    write("icons/%s.svg" % name, svg(64, body, defs))


def item_ring(name, a, b, gem):
    defs = grad(a, b)
    body = ('<path d="M32 20 A21 21 0 1 0 32 62 A21 21 0 1 0 32 20Z M32 28 A13 13 0 1 1 32 54 A13 13 0 1 1 32 28Z" fill="url(#g)" fill-rule="evenodd" stroke="#fff" stroke-opacity=".35"/>'
            '<polygon points="32,3 44,14 32,28 20,14" fill="%s" stroke="#fff" stroke-opacity=".8"/>'
            '<polygon points="32,3 44,14 32,14 " fill="#fff" opacity=".5"/><polygon points="20,14 32,14 32,28" fill="#000" opacity=".22"/>' % gem)
    write("icons/%s.svg" % name, svg(64, body, defs))


def items():
    item_blade("ember_fang", "#fff0b0", "#ff5a1f", "#ffcf6a", 34)
    item_blade("tidecaller", "#d8f6ff", "#2a8ee8", "#7fe0ff", 28)
    item_blade("thornreaver", "#dcffd8", "#2fae6c", "#96f5b8", 22)
    item_blade("ash_dirk", "#d8d8dc", "#62626c", "#a0a0a8", 18)
    item_orb("drowned_heart", "#ffb45e", "#a01e12", "#fff2c8")
    item_orb("moon_prism", "#c9a6ff", "#3a1e94", "#ffffff")
    item_orb("sun_lens", "#8fd8ff", "#14508c", "#ffffff")
    item_crown("nine_comets", "#f4dcff", "#7a34d0", "#e6c0ff")
    item_crown("vesper", "#d0ecff", "#2a70c8", "#a0e0ff")
    item_ring("last_light", "#ffefb8", "#b86a12", "#ff6a2a")
    item_ring("serpent_loop", "#d8ffe2", "#2a9a64", "#7ff5b0")
    item_ring("iron_vow", "#d4d4da", "#5c5c66", "#9a9aa4")
    item_blade("moth_blade", "#ecd8ff", "#7a4ae0", "#c8a0ff", 30)
    item_orb("star_compass", "#fff2b8", "#a8780e", "#ffffff")
    item_crown("thorn_circlet", "#d8ffe0", "#1f8a58", "#8cf5b4")
    item_ring("mirror_loop", "#d8f4ff", "#2f7ac0", "#a8e8ff")


def fbm(width, height, base, octaves, seed, gain=.55):
    rng = np.random.default_rng(seed)
    total = np.zeros((height, width), np.float32)
    amp, norm, cells = 1.0, 0.0, base
    for _ in range(octaves):
        grid = rng.random((max(2, int(cells * height / width)) + 2, cells + 2), dtype=np.float32)
        layer = np.asarray(Image.fromarray(grid).resize((width, height), Image.BICUBIC), np.float32)
        total += layer * amp
        norm += amp
        amp *= gain
        cells *= 2
    return total / norm


def warp(field, dx, dy, amount):
    h, w = field.shape
    ys, xs = np.mgrid[0:h, 0:w]
    sx = np.clip(xs + (dx - .5) * amount, 0, w - 1).astype(np.int32)
    sy = np.clip(ys + (dy - .5) * amount, 0, h - 1).astype(np.int32)
    return field[sy, sx]


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def nebula():
    w, h = 1920, 1080
    dx = fbm(w, h, 4, 5, 11)
    dy = fbm(w, h, 4, 5, 12)
    base = warp(fbm(w, h, 3, 7, 21), dx, dy, 900)
    wisps = warp(fbm(w, h, 5, 7, 22), dy, dx, 620)
    deep = warp(fbm(w, h, 2, 6, 23), dx, dy, 500)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    nx, ny = xs / w - .5, (ys / h - .5) * h / w
    center = np.exp(-(nx * nx + ny * ny) * 5.5)
    band = np.exp(-((ny + nx * .55) ** 2) * 16)
    cloud = smooth(.42, .78, base) * (0.3 + .9 * band)
    veil = smooth(.46, .74, wisps) * (0.25 + .85 * band)
    glow = smooth(.35, .8, deep)
    img = np.zeros((h, w, 3), np.float32)
    img[..., 0] = .012 + .006 * glow
    img[..., 1] = .016 + .010 * glow
    img[..., 2] = .046 + .030 * glow
    img += cloud[..., None] * np.array([.060, .085, .30], np.float32)
    img += veil[..., None] * np.array([.28, .055, .20], np.float32) * .42
    img += (cloud * veil)[..., None] * np.array([.12, .42, .50], np.float32) * .9
    img += center[..., None] * np.array([.075, .055, .10], np.float32)
    rng = np.random.default_rng(77)
    density = .35 + cloud * .8
    count = 5200
    px = rng.integers(0, w, count)
    py = rng.integers(0, h, count)
    bright = rng.random(count) ** 7
    tint = np.stack([.75 + .25 * rng.random(count), .8 + .2 * rng.random(count), .9 + .1 * rng.random(count)], 1)
    for i in range(count):
        if rng.random() > density[py[i], px[i]]:
            continue
        v = .10 + bright[i] * .95
        img[py[i], px[i]] += tint[i] * v
    big = rng.integers(0, count, 150)
    for i in big:
        x, y, v = px[i], py[i], .35 + .65 * rng.random()
        radius = rng.integers(3, 8)
        x0, x1, y0, y1 = max(x - radius, 0), min(x + radius + 1, w), max(y - radius, 0), min(y + radius + 1, h)
        gy, gx = np.mgrid[y0:y1, x0:x1]
        d2 = (gx - x) ** 2 + (gy - y) ** 2
        img[y0:y1, x0:x1] += (np.exp(-d2 / (radius * .55) ** 2) * v)[..., None] * tint[i]
    edge = np.clip(1.25 - np.sqrt(nx * nx * 1.8 + ny * ny * 3.4) * 1.2, .12, 1) ** 1.6
    img *= edge[..., None]
    img *= .62
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    img += (RNG.random(img.shape, dtype=np.float32) - .5) * 1.6 / 255
    out = (np.clip(img, 0, 1) * 255 + .5).astype(np.uint8)
    Image.fromarray(out).save(os.path.join(HERE, "nebula.jpg"), quality=88, optimize=True)


def halo():
    n = 256
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32)
    d = np.sqrt((xs - n / 2 + .5) ** 2 + (ys - n / 2 + .5) ** 2) / (n / 2)
    falloff = np.clip(1 - d, 0, 1) ** 2.4
    falloff = falloff * .75 + np.exp(-d * d * 22) * .25
    v = (falloff * 255 + RNG.random(falloff.shape) * .9).clip(0, 255).astype(np.uint8)
    rgba = np.dstack([np.full_like(v, 255), np.full_like(v, 255), np.full_like(v, 255), v])
    Image.fromarray(rgba, "RGBA").save(os.path.join(HERE, "halo.png"), optimize=True)


def panel():
    h, w = 256, 32
    t = np.linspace(0, 1, h)[:, None]
    v = 1.0 - .36 * t
    alpha = .90 + .05 * t
    v = np.repeat(v, w, 1)
    alpha = np.repeat(alpha, w, 1)
    c = (v * 255 + RNG.random(v.shape) * .9).clip(0, 255).astype(np.uint8)
    a = (alpha * 255 + RNG.random(v.shape) * .9).clip(0, 255).astype(np.uint8)
    Image.fromarray(np.dstack([c, c, c, a]), "RGBA").save(os.path.join(HERE, "panel.png"), optimize=True)


def sheen():
    h, w = 128, 32
    t = np.linspace(0, 1, h)[:, None]
    a = np.repeat(np.clip(1 - t * 1.05, 0, 1) ** 2, w, 1)
    c = np.full_like(a, 255).astype(np.uint8)
    v = (a * 255 + RNG.random(a.shape) * .9).clip(0, 255).astype(np.uint8)
    Image.fromarray(np.dstack([c, c, c, v]), "RGBA").save(os.path.join(HERE, "sheen.png"), optimize=True)


def vignette():
    w, h = 480, 270
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    nx, ny = (xs + .5) / w * 2 - 1, (ys + .5) / h * 2 - 1
    d = np.sqrt(nx * nx * .85 + ny * ny * 1.05)
    v = smooth(.55, 1.32, d) ** 1.3 * .86
    a = (v * 255 + RNG.random(v.shape) * .9).clip(0, 255).astype(np.uint8)
    z = np.zeros_like(a)
    Image.fromarray(np.dstack([z, z, z, a]), "RGBA").save(os.path.join(HERE, "vignette.png"), optimize=True)


def main():
    os.makedirs(ICONS, exist_ok=True)
    write("astro_outer.svg", astro_outer())
    write("astro_mid.svg", astro_mid())
    write("astro_inner.svg", astro_inner())
    write("astro_rose.svg", astro_rose())
    write("rays.svg", rays())
    write("frame.svg", frame())
    write("divider.svg", divider())
    write("orb_frame.svg", orb_frame())
    write("orb_gloss.svg", orb_gloss())
    write("dashes.svg", dashes())
    write("sigil.svg", sigil())
    spells()
    items()
    halo()
    panel()
    sheen()
    vignette()
    nebula()


if __name__ == "__main__":
    main()
