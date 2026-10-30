#!/usr/bin/env python3
"""Writes a seamless periodic fractal noise texture as an 8 bit grayscale PNG.

The texture is a sum of octaves of 2D gradient (Perlin) noise, fbm: octave o has a lattice of
round(frequency * lacunarity^o) cells across the tile and the amplitude gain^o. The gradients of
every octave repeat over its lattice, so every octave, and with them the sum, is periodic over the
tile: the texture wraps without a seam in both directions. Every octave is shifted by its own
random offset so that the lattices of the octaves do not line up. The gradients and offsets come
from random.Random seeded with the seed and the octave, so the same options write the same file.

Mapping to 8 bit: with m the median of the fbm values and h the larger of m - p1 and p99 - m, where
p1 and p99 are the 1st and 99th percentiles,

    value = clamp(0.5 + 0.5 * (n - m) / h, 0, 1),    byte = round(255 * value)

so the histogram is centered on 0.5 (half of the texels are below 0.5) and spans 0..1 with about
1% of the texels clipped to 0 and to 1 (exactly 1% at the end that is farther from the median, a
little less at the other). A cookie threshold of 0.5 covers half of the ground; since fbm values are
roughly normally distributed the coverage of other thresholds t is close to, but steeper than, t.

etc/assets/textures/cloud_noise.png was written with the default options:

    python3 tools/noise_texture.py etc/assets/textures/cloud_noise.png

test/assets/textures/cloud_noise_256.png, the copy the tests use, with:

    python3 tools/noise_texture.py test/assets/textures/cloud_noise_256.png --size 256

Usage: noise_texture.py output.png [--size 1024] [--frequency 8] [--octaves 5] [--lacunarity 2]
                        [--gain 0.5] [--seed 1]
"""
import argparse
import math
import random
import struct
import sys
import zlib


def fade(t):
    """Quintic smoothstep of Perlin's improved noise."""
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def octave(values, size, period, amplitude, rng):
    """Add one octave of gradient noise periodic over period cells across the tile to values."""
    angles = [[rng.random() * 2.0 * math.pi for _ in range(period)] for _ in range(period)]
    gx = [[math.cos(a) for a in row] for row in angles]
    gy = [[math.sin(a) for a in row] for row in angles]
    offset_x = rng.random() * period
    offset_y = rng.random() * period

    def axis(offset):
        cells, nexts, fracs, fades = [], [], [], []
        for i in range(size):
            p = (i + 0.5) * period / size + offset
            c = math.floor(p)
            f = p - c
            cells.append(c % period)
            nexts.append((c + 1) % period)
            fracs.append(f)
            fades.append(fade(f))
        return cells, nexts, fracs, fades

    cx, nx, fx, ux = axis(offset_x)
    columns = list(zip(cx, nx, fx, ux))
    cy, ny, fy, uy = axis(offset_y)
    for y in range(size):
        gx0, gy0 = gx[cy[y]], gy[cy[y]]
        gx1, gy1 = gx[ny[y]], gy[ny[y]]
        fy0 = fy[y]
        fy1 = fy0 - 1.0
        v = uy[y]
        row = values[y]
        for x, (c0, c1, fx0, u) in enumerate(columns):
            fx1 = fx0 - 1.0
            n00 = gx0[c0] * fx0 + gy0[c0] * fy0
            n10 = gx0[c1] * fx1 + gy0[c1] * fy0
            n01 = gx1[c0] * fx0 + gy1[c0] * fy1
            n11 = gx1[c1] * fx1 + gy1[c1] * fy1
            a = n00 + u * (n10 - n00)
            b = n01 + u * (n11 - n01)
            row[x] += amplitude * (a + v * (b - a))


def fbm(size, frequency=8, octaves=5, lacunarity=2.0, gain=0.5, seed=1):
    """Rows of periodic fbm values of a size x size tile."""
    values = [[0.0] * size for _ in range(size)]
    amplitude = 1.0
    for o in range(octaves):
        period = max(1, int(round(frequency * lacunarity ** o)))
        octave(values, size, period, amplitude, random.Random(seed * 1000003 + o))
        amplitude *= gain
    return values


def quantize(values):
    """Remap the fbm values to bytes as the header describes, one bytes object per row."""
    flat = sorted(v for row in values for v in row)
    n = len(flat)
    median = 0.5 * (flat[(n - 1) // 2] + flat[n // 2])
    p1 = flat[int(0.01 * (n - 1))]
    p99 = flat[int(math.ceil(0.99 * (n - 1)))]
    half = max(median - p1, p99 - median, 1e-12)
    scale = 0.5 / half
    rows = []
    for row in values:
        rows.append(bytes(min(255, max(0, int(round(255.0 * (0.5 + (v - median) * scale)))))
                          for v in row))
    return rows


def chunk(kind, data):
    """A PNG chunk with its length and CRC."""
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def write_png(path, rows):
    """Write rows of bytes as an 8 bit grayscale PNG, every row with the Sub filter."""
    height = len(rows)
    width = len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(1)
        prev = 0
        for b in row:
            raw.append((b - prev) & 0xFF)
            prev = b
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def read_png(path):
    """Read an 8 bit grayscale PNG as rows of bytes. Supports all filter types, no interlacing."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos = 8
    idat = bytearray()
    width = height = 0
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color != 0 or interlace != 0:
                raise ValueError("%s is not an 8 bit grayscale PNG" % path)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(bytes(idat))
    rows = []
    prev = bytearray(width)
    for y in range(height):
        start = y * (width + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + width])
        for x in range(width):
            a = line[x - 1] if x > 0 else 0
            b = prev[x]
            c = prev[x - 1] if x > 0 else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 0xFF
        rows.append(bytes(line))
        prev = line
    return rows


def generate(path, size=1024, frequency=8, octaves=5, lacunarity=2.0, gain=0.5, seed=1):
    """Write the noise texture of the options to path."""
    write_png(path, quantize(fbm(size, frequency, octaves, lacunarity, gain, seed)))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write a seamless periodic fbm noise PNG.")
    parser.add_argument("output", help="the PNG file to write")
    parser.add_argument("--size", type=int, default=1024, help="width and height in texels")
    parser.add_argument("--frequency", type=int, default=8,
                        help="lattice cells across the tile of the first octave")
    parser.add_argument("--octaves", type=int, default=5, help="number of octaves")
    parser.add_argument("--lacunarity", type=float, default=2.0,
                        help="frequency factor between octaves, cells are rounded to integers")
    parser.add_argument("--gain", type=float, default=0.5,
                        help="amplitude factor between octaves")
    parser.add_argument("--seed", type=int, default=1, help="random seed")
    args = parser.parse_args(argv)
    if args.size < 1 or args.frequency < 1 or args.octaves < 1:
        parser.error("size, frequency and octaves must be at least 1")
    generate(args.output, args.size, args.frequency, args.octaves, args.lacunarity, args.gain,
             args.seed)


if __name__ == "__main__":
    main(sys.argv[1:])
