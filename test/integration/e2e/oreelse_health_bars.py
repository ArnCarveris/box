"""Ore Else driven through its command port: the health bars over the damaged buildings and the
astronaut. A bar appears when a wall takes damage, its fill and color follow the health, it goes
when the wall is repaired to full and when it is destroyed; it is as wide as the projection of
the wall's model box and sits 6 px above it, follows the zoom level, and its view draws only
while playing. The astronaut's bar works the same: it appears when the astronaut is hurt,
follows it as it moves, and goes at full health, at its death and when it boards the rocket.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup. The cases set up state directly: walls created
by a script on chosen tiles, their health set through the command port, a mite spawned with
oreelse.spawn_critter(). The bars are the instances of the HealthBar template (the component
holds the building and the fraction) under health_bars.bars, laid out in the scene of the bars.
"""
import struct

from oreelse_critters import (  # noqa: F401 (ARGS)
    ARGS, FPS, PLAYER, PLAYER_HP, STATE, buildings, entity, health, hold_waves, place_player,
    rows_of, set_state, setup, spawn, start, tile_point, wait_until, wall)

APP = "apps/oreelse"

VIEW = "health_bars_view"
CAMERA = "health_bars.camera"
LEVEL = "get oreelse.OreView OreView.level"

# The bar: height, margin above the box, narrowest shown (cfg.hpBarHeight, cfg.hpBarMargin,
# cfg.hpBarMinWidth)
BAR_HEIGHT = 4
BAR_MARGIN = 6
BAR_MIN = 6

# The fill colors: full, mid and low health (cfg.hpBarFull, cfg.hpBarMid, cfg.hpBarLow), the
# original's bytes: the bar blends them and keeps the blend's bytes, as the rest of the UI
# (apps/oreelse/README.md, Colors)
FULL = (92, 236, 140)
MID = (240, 176, 58)
LOW = (248, 84, 72)

# A tile east of the center without deposits on the map of the seed, the astronaut 4 units west
# of it, so the wall is on screen and the astronaut does not hover over it
ROW, COL = 23, 39

# Frames for the camera to settle after the astronaut moved, and for a change to show
SETTLE = 5
GLIDE = 3 * FPS

WALL_HP = 300


def bars(app):
    """The health bars: path -> the HealthBar props."""
    return rows_of(app, "HealthBar")


def bar_of(app, building):
    """The bar of a building, None for none."""
    for path, props in bars(app).items():
        if entity(props["building"]) == building:
            return path
    return None


def wait_bar(app, building, frac):
    """Wait for the bar of a building at a fraction: its path."""
    def found():
        path = bar_of(app, building)
        return path and abs(bars(app)[path]["frac"] - frac) < 1e-4
    wait_until(app, found, 30, "the bar of %s at %g" % (building, frac))
    return bar_of(app, building)


def wait_no_bar(app, building):
    wait_until(app, lambda: bar_of(app, building) is None, 30, "the bar of %s gone" % building)


def set_health(app, building, value, max_hp=WALL_HP):
    app.exec("set %s OreHealth {value: %g, max: %g}" % (building, value, max_hp))


def f32(x):
    """x rounded to a C float."""
    return struct.unpack("f", struct.pack("f", x))[0]


def blend(a, b, t):
    """The color of the original's oreBarLerp: a + (b - a) * t per channel in floats,
    truncated."""
    t = min(max(t, 0), 1)
    return tuple(int(f32(a[i] + f32((b[i] - a[i]) * t))) for i in range(3))


def fill_color(value, max_hp):
    """The fill color of the original at value / max_hp, in floats as its C code: its bytes, which
    the UI keeps."""
    frac = f32(value / max_hp)
    if frac >= 0.5:
        return blend(MID, FULL, f32((frac - 0.5) * 2))
    return blend(LOW, MID, f32(frac * 2))


def color(app, path):
    c = app.get(path, "Rgb")
    return (c["r"], c["g"], c["b"])


def projected(app, bar):
    """The projection of the wall's box: left, right, top, bottom."""
    b = app.get(bar, "ProjectedBounds")
    return b["left"], b["right"], b["top"], b["bottom"]


def width(app, bar):
    return float(app.exec("get %s LayoutBounds.width" % bar))


def check_fill(app, bar, value, max_hp=WALL_HP):
    """The fill: (width - 2) * frac wide, 2 px tall, in the color of the blend."""
    frac = value / max_hp
    w = width(app, bar)
    fill = bar + ".track.fill"
    fw = float(app.exec("get %s LayoutBounds.width" % fill))
    fh = float(app.exec("get %s LayoutBounds.height" % fill))
    app.check(abs(fw - (w - 2) * frac) < 0.01,
              "the fill at %g is %g wide in a bar of %g" % (frac, fw, w))
    app.check(abs(fh - (BAR_HEIGHT - 2)) < 0.01, "the fill is %g tall" % fh)
    expected = fill_color(value, max_hp)
    got = color(app, fill)
    app.check(got == expected,
              "the fill at %g is %s, not %s" % (frac, got, expected))


def check_placed(app, bar):
    """The bar is as wide as the projected box and sits BAR_MARGIN above its top edge."""
    app.check(app.exec("has %s box.projection.ProjectedHidden" % bar) == "false",
              "the bar is hidden")
    left, right, top, _ = projected(app, bar)
    w = width(app, bar)
    h = float(app.exec("get %s LayoutBounds.height" % bar))
    app.check(abs(w - (right - left)) < 0.01,
              "the bar is %g wide over a box %g wide" % (w, right - left))
    app.check(w >= BAR_MIN, "the bar is only %g wide" % w)
    app.check(abs(h - BAR_HEIGHT) < 0.01, "the bar is %g tall" % h)
    p = app.get(bar, "Position")
    app.check(abs(p["x"] - (left + right) / 2) < 0.01,
              "the bar is centered at %g over a box from %g to %g" % (p["x"], left, right))
    app.check(abs(p["y"] - (top + BAR_MARGIN + BAR_HEIGHT / 2)) < 0.01,
              "the bar is centered at y %g over a box whose top is %g" % (p["y"], top))
    return w


def near_wall(app):
    """A wall on (ROW, COL) with the astronaut 4 units west of it, the camera settled: its
    path."""
    hold_waves(app)
    x, z = tile_point(ROW, COL)
    place_player(app, x - 4, z)
    w = wall(app, ROW, COL)
    app.wait_frames(GLIDE)
    return w


def stable_width(app, bar):
    """The width of a bar once the camera stopped gliding."""
    last = None
    start_frame = app.frame()
    while True:
        w = width(app, bar)
        if last is not None and abs(w - last) < 0.01:
            return w
        app.check(app.frame() - start_frame < 2 * GLIDE, "the bar keeps changing width")
        last = w
        app.wait_frames(SETTLE)


def test_chewed(app):
    """A mite chewing a wall: the bar appears and its fraction follows the health."""
    start(app)
    w = near_wall(app)
    app.check(not bars(app), "bars over undamaged buildings: %s" % bars(app))

    x, z = tile_point(ROW, COL)
    spawn(app, "Mite", x + 2.5, z)
    wait_until(app, lambda: bar_of(app, w) is not None, 3 * FPS, "a bar over the chewed wall")
    set_state(app, "Paused")
    bar = bar_of(app, w)
    hp = health(app, w)
    frac = bars(app)[bar]["frac"]
    app.check(0 < hp < WALL_HP, "the chewed wall has %g HP" % hp)
    app.check(abs(frac - hp / WALL_HP) < 1e-4,
              "the bar is at %g with the wall at %g HP" % (frac, hp))
    app.check(len(bars(app)) == 1, "bars: %s" % bars(app))


def test_fill_and_repair(app):
    start(app)
    w = near_wall(app)
    app.check(not bars(app), "bars over undamaged buildings: %s" % bars(app))

    # Half health: amber, half the inside of the bar
    set_health(app, w, 150)
    bar = wait_bar(app, w, 0.5)
    app.wait_frames(SETTLE)
    check_placed(app, bar)
    check_fill(app, bar, 150)

    # The same bar follows the health: red to amber below half, amber to green above
    for value in (75, 270, 30):
        set_health(app, w, value)
        app.check(wait_bar(app, w, value / WALL_HP) == bar, "the bar was replaced")
        app.wait_frames(SETTLE)
        check_fill(app, bar, value)
    app.check(len(bars(app)) == 1, "bars: %s" % bars(app))

    # Repaired to full: the bar goes
    set_health(app, w, WALL_HP)
    wait_no_bar(app, w)
    app.check(entity(app.get(w, "OreHealthBar")["bar"]) is None,
              "the repaired wall keeps the bar %s" % app.get(w, "OreHealthBar"))
    app.check(not bars(app), "bars left: %s" % bars(app))

    # Damaged again and destroyed: the wall and its bar go
    set_health(app, w, 150)
    wait_bar(app, w, 0.5)
    set_health(app, w, 0)
    wait_until(app, lambda: w not in buildings(app), FPS, "the wall destroyed")
    app.check(not bars(app), "bars left after the wall was destroyed: %s" % bars(app))


def test_paused(app):
    # Not on the title screen
    setup(app)
    app.wait_frames(SETTLE)
    app.check(entity(app.exec("get %s View.camera" % VIEW)) is None,
              "the view of the bars draws on the title screen")

    app.exec("key.press Space")
    app.wait_input()
    app.wait_for("get %s View.camera" % VIEW, CAMERA, what="the view of the bars drawing")
    w = near_wall(app)
    set_health(app, w, 150)
    bar = wait_bar(app, w, 0.5)

    # Paused: the view has no camera, the bar stays
    set_state(app, "Paused")
    app.wait_for("get %s View.camera" % VIEW, lambda out: entity(out) is None,
                 what="the view of the bars hidden")
    app.check(bar_of(app, w) == bar, "the bar is gone while paused")

    set_state(app, "Playing")
    app.wait_for("get %s View.camera" % VIEW, CAMERA, what="the view of the bars drawing")
    app.check(bar_of(app, w) == bar, "the bar was replaced")

    # Lost: hidden again
    set_state(app, "Lost")
    app.wait_for("get %s View.camera" % VIEW, lambda out: entity(out) is None,
                 what="the view of the bars hidden when lost")


def test_zoom(app):
    start(app)
    w = near_wall(app)
    set_health(app, w, 150)
    bar = wait_bar(app, w, 0.5)
    near = stable_width(app, bar)
    check_placed(app, bar)

    # Zoomed out to level 2 the wall looks smaller and the bar narrows with it; it is as wide as
    # the projected box in every frame
    app.exec("mouse.move [320, 300]")
    app.wait_input()
    for level in (1, 2):
        app.exec("mouse.scroll 0 -1")
        app.wait_input()
        app.wait_for(LEVEL, str(level), what="zoom level %d" % level)
    set_state(app, "Paused")
    far = stable_width(app, bar)
    app.check(far < near * 0.7, "zoomed out the bar is %g wide, %g zoomed in" % (far, near))
    check_placed(app, bar)
    check_fill(app, bar, 150)

    # Back at level 0 it is as wide as before
    set_state(app, "Playing")
    for level in (1, 0):
        app.exec("mouse.scroll 0 1")
        app.wait_input()
        app.wait_for(LEVEL, str(level), what="zoom level %d" % level)
    set_state(app, "Paused")
    again = stable_width(app, bar)
    app.check(abs(again - near) < 0.5, "back at level 0 the bar is %g wide, not %g" %
              (again, near))
    check_placed(app, bar)


def test_player(app):
    """The astronaut's bar: over it while it is hurt, following it, gone at full health and at
    its death."""
    start(app)
    hold_waves(app)
    x, z = tile_point(ROW, COL)
    place_player(app, x, z)
    app.wait_frames(GLIDE)
    app.check(not bars(app), "bars at full health: %s" % bars(app))

    # Hurt: the same HealthBar over the astronaut, anchored to its model box
    set_health(app, PLAYER, 40, PLAYER_HP)
    set_state(app, "Paused")
    bar = wait_bar(app, PLAYER, 0.4)
    props = bars(app)[bar]
    app.check(props["h"] > 2 and props["w"] > 0.5 and props["d"] > 0.5,
              "the astronaut's bar has the box %s" % props)
    set_state(app, "Playing")
    app.wait_frames(SETTLE)
    set_state(app, "Paused")
    app.wait_frames(SETTLE)
    check_placed(app, bar)
    check_fill(app, bar, 40, PLAYER_HP)
    before = app.get(bar, "Position")["x"]

    # It follows the astronaut: 3 units east the bar is further right on screen
    set_state(app, "Playing")
    app.exec("set %s OreHurt {since: 0}" % PLAYER)
    place_player(app, x + 3, z)
    app.wait_frames(SETTLE)
    set_state(app, "Paused")
    app.wait_frames(SETTLE)
    app.check(bar_of(app, PLAYER) == bar, "the bar was replaced")
    app.check(app.get(bar, "Position")["x"] != before,
              "the bar stayed at x %g as the astronaut moved" % before)
    check_placed(app, bar)

    # Back to full health: the bar goes
    set_health(app, PLAYER, PLAYER_HP, PLAYER_HP)
    wait_no_bar(app, PLAYER)
    app.check(not bars(app), "bars left: %s" % bars(app))

    # Hurt again and killed: the bar goes with the death
    set_health(app, PLAYER, 30, PLAYER_HP)
    wait_bar(app, PLAYER, 0.3)
    set_state(app, "Playing")
    set_health(app, PLAYER, 0, PLAYER_HP)
    app.wait_for(STATE % "Lost", "true", frames=2 * FPS, what="the game lost")
    wait_no_bar(app, PLAYER)


def test_player_aboard(app):
    """A hurt astronaut boarding a crewed rocket takes its bar with it: no bar over the pad
    while the rocket flies."""
    from oreelse_rocket import assemble, key, place_pad, wait_rocket
    start(app)
    hold_waves(app)
    place_pad(app, [("items.RocketLifeSupport", 1), ("items.RocketEngine", 4),
                    ("items.RocketFuelStorage", 1)])
    assemble(app, life=True)
    app.exec("set %s OreHurt {since: 0}" % PLAYER)
    set_health(app, PLAYER, 40, PLAYER_HP)
    wait_until(app, lambda: bar_of(app, PLAYER) is not None, 30, "the astronaut's bar")

    key(app, "P")
    wait_rocket(app, lambda r: r["aboard"], "the astronaut aboard")
    wait_no_bar(app, PLAYER)
    app.check(app.get(PLAYER, "OreHealth")["value"] < PLAYER_HP,
              "the astronaut aboard healed: %s" % app.get(PLAYER, "OreHealth"))
    app.check(not bars(app), "bars left: %s" % bars(app))
