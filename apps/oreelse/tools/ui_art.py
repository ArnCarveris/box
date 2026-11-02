#!/usr/bin/env python3
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "etc", "ui", "art")
RNG = np.random.default_rng(4807)


def save(name, rgb, alpha):
    c = (rgb * 255 + RNG.random(rgb.shape) * .9).clip(0, 255).astype(np.uint8)
    a = (alpha * 255 + RNG.random(alpha.shape) * .9).clip(0, 255).astype(np.uint8)
    Image.fromarray(np.dstack([c, c, c, a]), "RGBA").save(os.path.join(ART, name), optimize=True)


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def halo():
    n = 256
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32)
    d = np.sqrt((xs - n / 2 + .5) ** 2 + (ys - n / 2 + .5) ** 2) / (n / 2)
    falloff = np.clip(1 - d, 0, 1) ** 2.2
    save("halo.png", np.ones_like(falloff), falloff)


def vignette():
    w, h = 480, 270
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    nx, ny = (xs + .5) / w * 2 - 1, (ys + .5) / h * 2 - 1
    d = np.sqrt(nx * nx * .85 + ny * ny * 1.05)
    save("vignette.png", np.ones_like(d), smooth(.5, 1.3, d) ** 1.2)


def plate():
    n = 512
    ys = np.linspace(0, 1, n)[:, None]
    base = np.repeat(1 - .34 * ys, n, 1)
    streaks = RNG.normal(0, 1, (n, 1))
    streaks = np.convolve(streaks[:, 0], np.ones(3) / 3, "same")[:, None] * .028
    grain = RNG.normal(0, .02, (n, n))
    wear = np.zeros((n, n))
    for _ in range(46):
        x, y = RNG.integers(0, n, 2)
        length = RNG.integers(14, 90)
        slope = RNG.uniform(-.25, .25)
        for i in range(length):
            px, py = (x + i) % n, int(y + i * slope) % n
            wear[py, px] += .1
    save("plate.png", np.clip(base + streaks + grain + wear, 0, 1), np.ones((n, n)))


def write_svg(name, body, width, height=None):
    height = height or width
    with open(os.path.join(ART, name), "w") as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" '
                'height="%d">%s</svg>\n' % (width, height, width, height, body))


def bracket():
    write_svg("bracket.svg",
              '<path d="M3 29 V6 Q3 3 6 3 H29 V9 H12 Q9 9 9 12 V29 Z" fill="#fff"/>'
              '<circle cx="18" cy="18" r="3" fill="#fff"/>', 32)


def rays():
    n, cx, r = 18, 512, 512
    body = ""
    for i in range(n):
        a0 = (i + .36) * 2 * np.pi / n
        a1 = (i + .64) * 2 * np.pi / n
        body += '<path d="M%d %d L%.1f %.1f L%.1f %.1f Z" fill="url(#g)"/>' % (
            cx, cx, cx + r * np.cos(a0), cx + r * np.sin(a0),
            cx + r * np.cos(a1), cx + r * np.sin(a1))
    defs = ('<defs><radialGradient id="g" gradientUnits="userSpaceOnUse" cx="512" cy="512" r="512">'
            '<stop offset="0" stop-color="#fff" stop-opacity=".9"/>'
            '<stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient></defs>')
    write_svg("rays.svg", defs + body, 1024)


def main():
    os.makedirs(ART, exist_ok=True)
    halo()
    vignette()
    plate()
    bracket()
    rays()


if __name__ == "__main__":
    main()
