"""Transitions through the shell: a value set over the command port is the target for every
reader at once, while the rendered pixels move to it.

Every case runs in a fresh apps/boxrun process with test/assets/scenes/transitions.flecs (named
from the repository root) on a 320x180 surface and gets the App of tools/box_client.py; the
runner checks after every case that the app logged no error and quits cleanly. The scene is 2D
and orthographic with one pixel per unit, so a capture shows the square at its rendered place and
color. Its transitions are linear and last 1 s, 60 frames at the fixed delta of 1/60 s. --fps 60
caps the app at that rate: uncapped, a headless app runs about 70 frames per command and more
before a capture arrives, past the end of the transition. Capped, a few frames pass between the
set and the capture, so the capture is in between, and after TRANSITION_FRAMES more frames it is
at the target.
"""
from box_client import DELTA_TIME, parse_value

APP = "apps/boxrun"
ARGS = ["--fps", "60", "--surface", "320x180", "test/assets/scenes/transitions.flecs"]

FPS = round(1 / float(DELTA_TIME))
# The transitions of the scene last 1 s; a few frames more for the frames in flight
TRANSITION_FRAMES = FPS + 10
# Pixels or channel values a capture must be away from the start and the target to count as in
# between: one frame moves the square 200 / 60 = 3.3 px and the color 255 / 60 = 4.25, and the
# capture after the set is about six frames in
MARGIN = 2


def pixels(image, box, accept):
    """The (x, y, (r, g, b)) of the pixels of image in box (x0, y0, x1, y1) that accept takes."""
    width, height, rows = image
    found = []
    for y in range(max(0, box[1]), min(height, box[3])):
        row = rows[y]
        for x in range(max(0, box[0]), min(width, box[2])):
            rgb = (row[x * 3], row[x * 3 + 1], row[x * 3 + 2])
            if accept(rgb):
                found.append((x, y, rgb))
    return found


def center_x(app, image, box, accept, what):
    found = pixels(image, box, accept)
    app.check(found, "no %s in the capture" % what)
    return sum(p[0] for p in found) / len(found)


def mean_color(app, image, box, what):
    """Mean color of the bright pixels in box: the square, not the background."""
    found = pixels(image, box, lambda c: max(c) > 60)
    app.check(found, "no %s in the capture" % what)
    return tuple(sum(p[2][i] for p in found) / len(found) for i in range(3))


def bands(image):
    """The rows of the color squares, the mover and the markers, from the surface size."""
    width, height, _ = image
    cy = height // 2
    return ((0, cy - 50 - 14, width, cy - 50 + 14), (0, cy - 14, width, cy + 14),
            (0, cy + 50 - 10, width, cy + 50 + 10))


def expect_x(app, x):
    """Check that get returns x as the Position.x of the mover."""
    value = parse_value(app.exec("get mover box.transform.Position.x"))
    app.check(value == x, "get returns x %r, not %r" % (value, x))


def white(c):
    return min(c) > 200


def grey(c):
    return 90 < c[0] < 170 and abs(c[0] - c[1]) < 12 and abs(c[1] - c[2]) < 12


def green(c):
    return c[1] > 120 and c[0] < 80 and c[2] < 80


def test_position(app):
    expect_x(app, -100)
    image = app.capture("start")
    colors, mover, markers = bands(image)
    start = center_x(app, image, markers, grey, "start marker")
    target = center_x(app, image, markers, green, "target marker")
    app.check(target - start > 100, "the markers are %.1f px apart" % (target - start))
    at = center_x(app, image, mover, white, "square")
    app.check(abs(at - start) < MARGIN, "the square starts at %.1f, not at %.1f" % (at, start))

    app.exec("set mover box.transform.Position {x: 100, y: 0, z: 0}")
    # The next command reads the target: no reader on the game thread sees a value in between
    value = parse_value(app.exec("get mover box.transform.Position"))
    app.check(value["x"] == 100, "get returns %r after the set, not the target" % value)
    image = app.capture("between")
    expect_x(app, 100)
    at = center_x(app, image, mover, white, "square")
    app.check(start + MARGIN < at < target - MARGIN,
              "the capture after the set shows the square at %.1f, not between %.1f and %.1f"
              % (at, start, target))

    app.wait_frames(TRANSITION_FRAMES)
    image = app.capture("target")
    at = center_x(app, image, mover, white, "square")
    app.check(abs(at - target) < MARGIN, "the square ends at %.1f, not at %.1f" % (at, target))
    expect_x(app, 100)


def test_rgb(app):
    app.expect("get shifter box.material.Rgb.b", "0")
    image = app.capture("start")
    colors, _, _ = bands(image)
    width = image[0]
    cx = width // 2
    left = (cx - 70, colors[1], cx - 50, colors[3])
    middle = (cx - 10, colors[1], cx + 10, colors[3])
    right = (cx + 50, colors[1], cx + 70, colors[3])
    red = mean_color(app, image, left, "start color")
    blue = mean_color(app, image, right, "target color")
    first = mean_color(app, image, middle, "square")
    app.check(all(abs(a - b) < MARGIN for a, b in zip(first, red)),
              "the square starts %r, not %r" % (first, red))

    app.exec("set shifter box.material.Rgb {r: 0, g: 0, b: 255}")
    value = parse_value(app.exec("get shifter box.material.Rgb"))
    app.check(value == {"r": 0, "g": 0, "b": 255},
              "get returns %r after the set, not the target" % (value,))
    image = app.capture("between")
    app.expect("get shifter box.material.Rgb.r", "0")
    now = mean_color(app, image, middle, "square")
    app.check(blue[0] + MARGIN < now[0] < red[0] - MARGIN and
              red[2] + MARGIN < now[2] < blue[2] - MARGIN,
              "the capture after the set shows %r, not between %r and %r" % (now, red, blue))

    app.wait_frames(TRANSITION_FRAMES)
    image = app.capture("target")
    now = mean_color(app, image, middle, "square")
    app.check(all(abs(a - b) < MARGIN for a, b in zip(now, blue)),
              "the square ends %r, not %r" % (now, blue))
