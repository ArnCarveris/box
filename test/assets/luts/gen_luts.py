#!/usr/bin/env python3
"""Generates the strip LUTs of the ColorGrading tests.

The images are committed next to this script; run it again only to change them:
python3 test/assets/luts/gen_luts.py. A strip of N slices of NxN is N*N wide and N high: red
increases to the right within a slice, green downward and blue from slice to slice. The values
are sRGB encoded, as the LUT is sampled on the encoded color.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def write_png(name, width, height, pixels):
    """Write RGBA8 pixels as a PNG without any image library."""
    raw = b"".join(b"\x00" + bytes(pixels[y * width * 4:(y + 1) * width * 4])
                   for y in range(height))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(png)


def identity(r, g, b):
    return r, g, b


def sepia(r, g, b):
    """The classic sepia matrix on the encoded color."""
    return (0.393 * r + 0.769 * g + 0.189 * b,
            0.349 * r + 0.686 * g + 0.168 * b,
            0.272 * r + 0.534 * g + 0.131 * b)


def strip(name, n, fn):
    pixels = bytearray()
    for y in range(n):
        for x in range(n * n):
            r, g, b = (x % n) / (n - 1), y / (n - 1), (x // n) / (n - 1)
            out = fn(r, g, b)
            pixels += bytes(int(round(min(max(c, 0.0), 1.0) * 255)) for c in out)
            pixels += b"\xff"
    write_png(name, n * n, n, pixels)


strip("identity_16.png", 16, identity)
strip("identity_32.png", 32, identity)
strip("sepia_16.png", 16, sepia)

# An image whose width is not the square of its height, which ColorGrading ignores
write_png("not_a_strip.png", 32, 16, bytes([128, 128, 128, 255]) * (32 * 16))
