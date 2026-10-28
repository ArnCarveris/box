#!/usr/bin/env python3
"""Generates the texture assets of the texture scenes in test/integration/visual.

The images are committed next to this script; run it again only to change them:
python3 test/integration/visual/assets/gen_assets.py. The first row of an image is the top,
at v = 0. Normal maps point green up the image, so a bump lights up on the side facing +y.
"""
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SIZE = 128


def write_png(name, size, pixels):
    """Write RGBA8 pixels as a PNG without any image library."""
    raw = b"".join(b"\x00" + bytes(pixels[y * size * 4:(y + 1) * size * 4])
                   for y in range(size))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(png)


def image(size, fn):
    """Fill an image with the color fn(x, y) returns per pixel."""
    out = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            i = (y * size + x) * 4
            out[i:i + 4] = bytes(fn(x, y))
    return out


def checker(x, y):
    """Four by four checker board in cream and brick red with a dark border per cell."""
    cell = SIZE // 4
    odd = ((x // cell) + (y // cell)) % 2
    edge = min(x % cell, y % cell, cell - 1 - x % cell, cell - 1 - y % cell) < 2
    color = (232, 226, 210) if odd else (176, 74, 56)
    if edge:
        color = tuple(int(c * 0.55) for c in color)
    return color + (255,)


def letter(x, y):
    """A blue F on white with a red bar at the top and a green bar on the left edge.

    The F is not symmetric in either axis, so flips, offsets and scales are visible.
    """
    u, v = x / SIZE, y / SIZE
    if v < 0.08:
        return (220, 40, 40, 255)
    if u < 0.08:
        return (40, 170, 60, 255)
    stem = 0.3 <= u < 0.42 and 0.2 <= v < 0.85
    top = 0.3 <= u < 0.78 and 0.2 <= v < 0.32
    middle = 0.3 <= u < 0.66 and 0.47 <= v < 0.58
    if stem or top or middle:
        return (40, 70, 200, 255)
    return (245, 245, 240, 255)


def bumps(x, y):
    """Tangent space normal map of a grid of four by four round bumps."""
    cell = SIZE // 4
    radius = cell * 0.42
    dx = (x % cell) - cell / 2 + 0.5
    dy = (y % cell) - cell / 2 + 0.5
    d = math.hypot(dx, dy)
    nx, ny, nz = 0.0, 0.0, 1.0
    if 1e-3 < d < radius:
        s = math.sin(math.pi * 0.5 * d / radius) * 0.8
        nx, ny = dx / d * s, -dy / d * s
        nz = math.sqrt(max(1.0 - nx * nx - ny * ny, 0.0))
    return tuple(int(round((c * 0.5 + 0.5) * 255)) for c in (nx, ny, nz)) + (255,)


def stripes(x, y):
    """ORM of smooth and rough horizontal bands: roughness in green, no occlusion in red and the
    metallic of the material in blue."""
    g = 30 if (y // (SIZE // 8)) % 2 == 0 else 255
    return (255, g, 255, 255)


def orm(x, y):
    """ORM of four by four tiles with occluded grooves (red) between them, a smooth top and a
    rough bottom half per tile (green) and metal and dielectric columns (blue)."""
    cell = SIZE // 4
    u, v = x % cell, y % cell
    edge = min(u, v, cell - 1 - u, cell - 1 - v)
    r = 255 if edge >= 4 else 20 + edge * 55
    g = 40 if v < cell // 2 else 255
    b = 255 if (x // cell) % 2 == 0 else 0
    return (r, g, b, 255)


def ring(x, y):
    """Emissive ring, white on black."""
    d = math.hypot(x - SIZE / 2 + 0.5, y - SIZE / 2 + 0.5) / (SIZE / 2)
    v = 255 if 0.55 < d < 0.8 else 0
    return (v, v, v, 255)


def fence(x, y):
    """Wooden lattice of slats crossing at 45 degrees inside a frame, transparent between the
    slats, for alpha tested shapes. The edges of the slats are hard: alpha is 0 or 255."""
    period = SIZE // 4
    half = 5
    frame = min(x, y, SIZE - 1 - x, SIZE - 1 - y) < 6
    a = (x + y) % period
    b = (x - y) % period
    slat_a = min(a, period - a) < half
    slat_b = min(b, period - b) < half
    if not (frame or slat_a or slat_b):
        return (0, 0, 0, 0)
    grain = 0.88 + 0.12 * math.sin(y * 0.9 if slat_a and not frame else x * 0.9)
    base = (150, 104, 62) if frame else (178, 128, 80)
    return tuple(int(c * grain) for c in base) + (255,)


def leaf(x, y):
    """One broad leaf from a short stem at the bottom to a pointed tip at the top, green with a
    paler midrib and side veins, whose alpha falls off smoothly over the last few texels of the
    edge, for alpha tested foliage."""
    px, py = (x + 0.5) / SIZE - 0.5, (y + 0.5) / SIZE
    t = (0.9 - py) / 0.84
    across = abs(px + 0.06 * math.sin(math.pi * t))
    if t < 0.0:
        coverage = max(0.0, min(1.0, (0.014 - abs(px)) / 0.01)) if t > -0.08 else 0.0
        return (70, 96, 40) + (int(round(coverage * 255)),)
    if t >= 1.0:
        return (0, 0, 0, 0)
    width = 0.4 * math.sin(math.pi * t ** 0.75) * (1.0 - 0.25 * t)
    coverage = max(0.0, min(1.0, (width - across) / 0.035))
    if coverage <= 0.0:
        return (0, 0, 0, 0)
    shade = 0.8 + 0.2 * (1.0 - across / max(width, 1e-3))
    color = (int(58 * shade), int(142 * shade), int(50 * shade))
    side = (t - 1.1 * across) / 0.12
    if across < 0.007 or (abs(side - round(side)) < 0.07 and across < width * 0.8):
        color = (104, 170, 80)
    return color + (int(round(coverage * 255)),)


def main():
    for name, fn in (("checker.png", checker), ("letter.png", letter),
                     ("bumps.png", bumps), ("stripes.png", stripes), ("ring.png", ring),
                     ("orm.png", orm), ("fence.png", fence), ("leaf.png", leaf)):
        write_png(name, SIZE, image(SIZE, fn))
        print("%s %d bytes" % (name, os.path.getsize(os.path.join(HERE, name))))


main()
