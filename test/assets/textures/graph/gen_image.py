#!/usr/bin/env python3
"""Writes the RGBA image the TextureNodesImage tests in test/gpu/render sample.

The image is committed next to this script; run it again only to change it:
python3 test/assets/textures/graph/gen_image.py. The first row is the top, at v = 0.

image_8x4.png  8 x 4, opaque, every texel another color: the texel of column x and row y is
               (16 + 32 x, 32 + 64 y, 255 - 24 (x + y), 255), which the tests compute too
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def chunk(kind, data):
    """Make a PNG chunk with its length and CRC."""
    return struct.pack(">I", len(data)) + kind + data + struct.pack(
        ">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_rgba_png(path, width, height, pixel):
    """Write an 8 bit RGBA PNG whose texel (x, y) is pixel(x, y), without filters."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def main():
    path = os.path.join(HERE, "image_8x4.png")
    write_rgba_png(path, 8, 4,
                   lambda x, y: (16 + 32 * x, 32 + 64 * y, 255 - 24 * (x + y), 255))
    print("%s %d bytes" % (os.path.basename(path), os.path.getsize(path)))


main()
