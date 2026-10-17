#!/usr/bin/env python3
"""Writes the procedural test environments in etc/environments as Radiance .hdr files.

axes.hdr gives every axis direction its own color (the axis the direction is closest to), so a
cubemap face shows one color at its center. sky.hdr is a gradient sky over a dark ground.
Equirectangular mapping as EnvironmentImage reads it: column u is the longitude with -Z at the
center column and +X to its right, row v the latitude with +Y at the top row.

Usage: environment_images.py [output folder]
"""
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

AXES = {
    (0, 1): (1.0, 0.0, 0.0),    # +X red
    (0, -1): (0.0, 1.0, 1.0),   # -X cyan
    (1, 1): (0.0, 1.0, 0.0),    # +Y green
    (1, -1): (1.0, 0.0, 1.0),   # -Y magenta
    (2, 1): (0.0, 0.0, 1.0),    # +Z blue
    (2, -1): (1.0, 1.0, 0.0),   # -Z yellow
}


def direction(u, v):
    """Direction of the equirectangular coordinates u, v in [0, 1]."""
    lon = (u - 0.5) * 2.0 * math.pi
    lat = v * math.pi
    s = math.sin(lat)
    return (s * math.sin(lon), math.cos(lat), -s * math.cos(lon))


def rgbe(color):
    """Encode a linear color as Radiance RGBE bytes."""
    m = max(color)
    if m < 1e-32:
        return bytes((0, 0, 0, 0))
    mant, exp = math.frexp(m)
    scale = mant * 256.0 / m
    return bytes([int(c * scale) for c in color] + [exp + 128])


def write(path, width, height, shade):
    """Write an uncompressed .hdr image whose texel colors come from shade(direction)."""
    data = bytearray()
    data += b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n"
    data += b"-Y %d +X %d\n" % (height, width)
    for y in range(height):
        for x in range(width):
            data += rgbe(shade(direction((x + 0.5) / width, (y + 0.5) / height)))
    with open(path, "wb") as f:
        f.write(data)


def axes(d):
    """Color of the axis a direction is closest to."""
    axis = max(range(3), key=lambda i: abs(d[i]))
    return AXES[(axis, 1 if d[axis] > 0 else -1)]


def sky(d):
    """A blue sky brightening toward the horizon over a dark brown ground."""
    y = d[1]
    if y >= 0:
        t = 1.0 - y
        return (0.2 + 0.8 * t ** 3, 0.4 + 0.55 * t ** 3, 1.0)
    t = min(1.0, -y * 4.0)
    return (0.3 - 0.2 * t, 0.25 - 0.17 * t, 0.2 - 0.14 * t)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "etc", "environments")
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "axes.hdr"), 64, 32, axes)
    write(os.path.join(out, "sky.hdr"), 128, 64, sky)


if __name__ == "__main__":
    main()
