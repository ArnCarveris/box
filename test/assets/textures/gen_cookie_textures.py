#!/usr/bin/env python3
"""Writes the 8 bit grayscale light cookie textures of the LightCookie tests in test/gpu/render.

The images are committed next to this script; run it again only to change them:
python3 test/assets/textures/gen_cookie_textures.py. The first row of an image is the top, at
v = 0; the tests load them with TexturePath.single_channel, which reads the gray as stored.

cookie_gray.png    16 x 16 of 128, a uniform half
cookie_black.png   16 x 16 of 0
cookie_ramp.png    256 x 16, the gray of column x is x: the value grows with u
cookie_split.png   64 x 64, the rows above the middle 0 and the rows below 255
cookie_stripes.png 64 x 64, stripes 2 rows high, alternately 0 and 255: the value changes
                   with v, and its mip levels from the 16 x 16 one on are a uniform half
cookie_columns.png 64 x 64, stripes 2 columns wide, alternately 0 and 255: the value changes
                   with u, and its mip levels from the 16 x 16 one on are a uniform half
cloud_white.png    8 x 8 of 255, a fully covered CloudLayer of the AtmosphereClouds tests
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))

from noise_texture import write_png  # noqa: E402


def main():
    images = {
        "cookie_gray.png": [bytes([128] * 16) for _ in range(16)],
        "cookie_black.png": [bytes(16) for _ in range(16)],
        "cookie_ramp.png": [bytes(range(256)) for _ in range(16)],
        "cookie_split.png": [bytes([0 if y < 32 else 255] * 64) for y in range(64)],
        "cookie_stripes.png": [bytes([255 * ((y // 2) % 2)] * 64) for y in range(64)],
        "cookie_columns.png": [bytes([255 * ((x // 2) % 2) for x in range(64)])
                               for _ in range(64)],
        "cloud_white.png": [bytes([255] * 8) for _ in range(8)],
    }
    for name, rows in images.items():
        write_png(os.path.join(HERE, name), rows)
        print("%s %d bytes" % (name, os.path.getsize(os.path.join(HERE, name))))


main()
