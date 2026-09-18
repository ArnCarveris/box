#!/usr/bin/env python3
"""Generates the texture assets of apps/box_app/etc/textures.

The engine has no way to author images from a scene, so the textures the visual
scene uses are generated here and committed next to it. Usage: gen_textures.py
[outdir]; without an argument it writes apps/box_app/etc/textures.

Normal maps follow the OpenGL convention: red is the +u tangent and green is the
+v tangent, so the half of a bump that faces +v is the bright green half. The
loader flips the rows of an image so uv v = 0 is the bottom row of the file,
which puts +v up the artwork as it is displayed; a row of this file with a
larger y is therefore a smaller v and gets the darker green.
"""
import math
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 256


def write_png(path, size, pixels):
    """Write RGBA8 pixels as a PNG without any image library."""
    raw = b"".join(b"\x00" + bytes(pixels[y * size * 4:(y + 1) * size * 4]) for y in range(size))
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def checker(size):
    """Eight by eight checker board in two warm colors with a border per cell."""
    out = bytearray(size * size * 4)
    cell = size // 8
    for y in range(size):
        for x in range(size):
            odd = ((x // cell) + (y // cell)) % 2
            edge = min(x % cell, y % cell, cell - 1 - x % cell, cell - 1 - y % cell) < 2
            color = (232, 226, 210) if odd else (176, 74, 56)
            if edge:
                color = tuple(int(c * 0.6) for c in color)
            i = (y * size + x) * 4
            out[i:i + 4] = bytes(color) + b"\xff"
    return out


def bumps(size):
    """Tangent space normal map of a grid of round bumps."""
    out = bytearray(size * size * 4)
    cell = size // 8
    radius = cell * 0.42
    for y in range(size):
        for x in range(size):
            dx = (x % cell) - cell / 2 + 0.5
            dy = (y % cell) - cell / 2 + 0.5
            d = math.hypot(dx, dy)
            nx, ny, nz = 0.0, 0.0, 1.0
            if d < radius and d > 1e-3:
                s = math.sin(math.pi * 0.5 * d / radius)
                nx, ny = dx / d * s, -dy / d * s
                nz = math.sqrt(max(1.0 - nx * nx - ny * ny, 0.0))
            i = (y * size + x) * 4
            out[i:i + 4] = bytes(int((v * 0.5 + 0.5) * 255) for v in (nx, ny, nz)) + b"\xff"
    return out


def bump_single(size):
    """Tangent space normal map of one large hemispherical bump in the middle."""
    out = bytearray(size * size * 4)
    radius = size * 0.42
    for y in range(size):
        for x in range(size):
            dx = x - size / 2 + 0.5
            dy = y - size / 2 + 0.5
            d = math.hypot(dx, dy)
            nx, ny, nz = 0.0, 0.0, 1.0
            if d < radius and d > 1e-3:
                s = d / radius
                nx, ny = dx / d * s, -dy / d * s
                nz = math.sqrt(max(1.0 - nx * nx - ny * ny, 0.0))
            i = (y * size + x) * 4
            out[i:i + 4] = bytes(int((v * 0.5 + 0.5) * 255) for v in (nx, ny, nz)) + b"\xff"
    return out


def roughness(size):
    """Horizontal roughness ramp with vertical stripes of scratched material."""
    out = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            v = x / (size - 1)
            if (y // 16) % 2 == 0:
                v = min(1.0, v + 0.25)
            g = int(max(0.05, v) * 255)
            i = (y * size + x) * 4
            out[i:i + 4] = bytes((g, g, g)) + b"\xff"
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "apps", "box_app", "etc", "textures")
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in (("checker", checker), ("bumps_normal", bumps),
                     ("bump_single", bump_single), ("roughness", roughness)):
        path = os.path.join(out_dir, name + ".png")
        write_png(path, SIZE, fn(SIZE))
        print("%s %d bytes" % (path, os.path.getsize(path)))


main()
