"""The fit command over the shell endpoints: a camera that does not see two crates off to its
right is moved back from their center along its view until it sees both of them.

Every case runs in a fresh apps/boxrun process with test/assets/scenes/fit.flecs (named from the
repository root), which declares its own 1280x720 window Surface. The crates span x 39..41 and
43..45, y 0..2 and z -21..-19; the camera looks along -Z from (0, 2, 0) with an aspect ratio of
16:9. fit keeps the direction, so it ends up at the center (42, 1, -20) plus 4.25 along +Z, where
the crates fill 90% of the width of the view, the left one around x 256 and the right one around
x 1024 of the capture, with the background between them.
"""
from box_client import mean_luma, parse_value

APP = "apps/boxrun"
ARGS = ["test/assets/scenes/fit.flecs"]

FIT = "fit camera to 2 entities, center (42.00, 1.00, -20.00), distance 4.25"

# Regions of the 1280x720 capture inside the front faces of the crates and in the gap between
LEFT = (200, 320, 310, 400)
RIGHT = (970, 320, 1080, 400)
GAP = (620, 320, 660, 400)
WHOLE = (0, 0, 1280, 720)

# The crates are lit bright yellow on a background of about 20; a region at least BRIGHT above
# the background shows a crate
BRIGHT = 60


def test_fit(app):
    before = app.capture("before")
    background = mean_luma(before, WHOLE)
    app.check(background < 40, "the capture before fit is %.1f bright, the crates are not "
              "expected in view yet" % background)

    out = parse_value(app.exec("fit"))
    app.check(out == FIT, "fit printed '%s', expected '%s'" % (out, FIT))
    position = app.get("camera", "box.transform.Position")
    app.check(abs(position["x"] - 42) < 0.01 and abs(position["y"] - 1) < 0.01 and
              abs(position["z"] + 15.75) < 0.01, "the camera is at %s after fit" % position)
    app.wait_frames(2)

    after = app.capture("after")
    left, right, gap = mean_luma(after, LEFT), mean_luma(after, RIGHT), mean_luma(after, GAP)
    app.check(left - background >= BRIGHT and right - background >= BRIGHT,
              "the crates are %.1f and %.1f bright after fit, the background %.1f" %
              (left, right, background))
    app.check(gap - background < BRIGHT, "the gap between the crates is %.1f bright" % gap)


def test_fit_named(app):
    out = parse_value(app.exec("fit camera"))
    app.check(out == FIT, "fit camera printed '%s', expected '%s'" % (out, FIT))
