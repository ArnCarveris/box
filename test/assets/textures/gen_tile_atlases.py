#!/usr/bin/env python3
"""Writes the RGBA tile atlases of the TileTerrain tests in test/gpu/render.

The images are committed next to this script; run it again only to change them:
python3 test/assets/textures/gen_tile_atlases.py. Every cell is 16 by 16 pixels of one color.

tiles_base.png      64 x 64, every variant (60, 60, 60, 255)
tiles_variants.png  64 x 64, variant i (16 i + 8, 255 - 16 i, 128, 255)
tiles_half.png      64 x 64, every variant (200, 40, 40, 128)
tiles_extended.png  128 x 64, variants 0..15 as tiles_variants, variant 16 + j (16 j + 8, 40,
                    200, 255)
"""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CELL = 16


def variant(i):
    return (16 * i + 8, 255 - 16 * i, 128, 255)


def extra(j):
    return (16 * j + 8, 40, 200, 255)


def atlas(columns, color):
    img = Image.new("RGBA", (columns * CELL, 4 * CELL))
    for row in range(4):
        for col in range(columns):
            index = (col % 4) + 4 * row + 16 * (col // 4)
            img.paste(color(index), (col * CELL, row * CELL, (col + 1) * CELL, (row + 1) * CELL))
    return img


def main():
    atlas(4, lambda i: (60, 60, 60, 255)).save(os.path.join(HERE, "tiles_base.png"))
    atlas(4, variant).save(os.path.join(HERE, "tiles_variants.png"))
    atlas(4, lambda i: (200, 40, 40, 128)).save(os.path.join(HERE, "tiles_half.png"))
    atlas(8, lambda i: variant(i) if i < 16 else extra(i - 16)).save(
        os.path.join(HERE, "tiles_extended.png"))


if __name__ == "__main__":
    main()
