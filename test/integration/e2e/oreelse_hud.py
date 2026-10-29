"""Ore Else driven through its command port: the HUD. The status bar shows outside the title
screen; its gauges follow the health, the Luminite held and shipped, the waves and the power;
the toast shows its message until its time is up (frozen while paused); the suit breach alert,
the mine progress bar and the rocket status line show with what they report, every text of the
rocket included; and the status bar, a panel, keeps clicks and the wheel from the world.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup. The cases that read the gauges pause the game,
so no system of play overwrites the state they put; the HUD shows while paused. The HUD's texts
and bars are read from the entities of its instance (hud.ui.screen).
"""
from box_client import srgb
from oreelse_buildings import (  # noqa: F401 (ARGS)
    ARGS, FPS, PLAYER, SETTLE, STATE, TOAST, aim_tile, click, count, key, put, rows_of, select,
    set_inventory, set_state, setup, start, tiles)
from oreelse_rocket import CARGO, ENGINE, FUEL, place_pad, press

APP = "apps/oreelse"

SCREEN = "hud.ui.screen"
STATUS = SCREEN + ".status"
LUMINITE_LABEL = STATUS + ".luminite.label.line"
POWER_LABEL = STATUS + ".power.label.line"
TOAST_LINE = SCREEN + ".toast"
ALERT = SCREEN + ".alert"
MINE_BAR = SCREEN + ".mine_bar"
ROCKET_LINE = SCREEN + ".rocket_line"

ROCKET = "oreelse.OreRocketState"
POWER = "oreelse.OrePowerState"
WAVES = "oreelse.OreWaveState"
BLINK = "oreelse.OreBlink"
LEVEL = "get oreelse.OreView OreView.level"

# The colors of the HUD, the original's bytes encoded as the port writes them
# (apps/oreelse/README.md, Colors)
RED = srgb((228, 96, 84))
AMBER = srgb((248, 196, 112))
GREEN = srgb((124, 226, 148))
TEAL = srgb((96, 240, 218))
WAVE_FLASH = {True: srgb((248, 96, 84)), False: srgb((132, 48, 44))}
WAVE_SOON = srgb((240, 152, 72))
WAVE_CALM = srgb((138, 122, 174))
READY = {True: srgb((124, 226, 148)), False: srgb((68, 148, 96))}
ALERT_BLINK = {True: srgb((255, 96, 84)), False: srgb((124, 44, 40))}
MINE_FILL = srgb((236, 176, 104))

# The bars: width of the HP, wave and power bars, the mine bar
HP_BAR = 84
WAVE_BAR = 92
POWER_BAR = 72
MINE_BAR_W = 224

# Seconds a warning and any other toast are shown (cfg.warnToast, cfg.infoToast)
WARN_TOAST = 2.5
INFO_TOAST = 4

# A point of the 640x360 window on the status bar (the panel spans x -18..622, y 16..62), whose
# cursor ray meets the ground beyond the reach of the astronaut it is aimed from
ON_PANEL = (320, 40)

# The tile the clicks aim at, in the corner of the map without deposits (oreelse_buildings.AREA)
TILE = (5, 5)


def hud_texts(app):
    """The texts of the HUD: path -> value."""
    return {path: t["value"] for path, t in rows_of(app, "box.text.Text").items()
            if path.startswith(SCREEN + ".")}


def text(app, path):
    """The text of an element of the HUD, None while it is not shown."""
    return hud_texts(app).get(path)


def wait_text(app, path, check, what):
    """Wait until the text of an element passes a check (None while it is not shown)."""
    start_frame = app.frame()
    while True:
        value = text(app, path)
        if check(value):
            return value
        app.check(app.frame() - start_frame < 60,
                  "expected %s at %s, the HUD shows %r" % (what, path, value))


def wait_shown(app, path, expected):
    return wait_text(app, path, lambda v: v == expected, "%r" % expected)


def wait_hidden(app, path, frames=60):
    start_frame = app.frame()
    while text(app, path) is not None:
        app.check(app.frame() - start_frame < frames,
                  "%s still shows %r" % (path, text(app, path)))


def shown(app, path):
    """Whether an element of the HUD is shown: it exists, with its LayoutItem."""
    return path in rows_of(app, "box.layout.LayoutItem")


def color(app, path):
    c = app.get(path, "Rgb")
    return (c["r"], c["g"], c["b"])


def bar_width(app, path):
    return float(app.exec("get %s LayoutItem.width.value" % path))


def check_bar(app, gauge, bar, frac):
    """The fill of a gauge of the status bar: 3 + (bar - 3) * frac wide."""
    width = bar_width(app, "%s.%s.fill" % (STATUS, gauge))
    expected = 3 + (bar - 3) * frac
    app.check(abs(width - expected) < 1e-3,
              "the %s bar is %g wide, not %g" % (gauge, width, expected))


def pause(app):
    start(app)
    set_state(app, "Paused")


def test_title(app):
    """Nothing of the HUD but the title screen shows on the title screen; the status bar comes
    with play."""
    setup(app)
    app.expect(STATE % "Title", "true")
    app.wait_frames(SETTLE)
    others = [p for p in hud_texts(app) if not p.startswith(SCREEN + ".title_screen.")]
    app.check(not others, "the title screen shows %s" % others)
    key(app, "Space")
    app.wait_for(STATE % "Playing", "true")
    wait_text(app, LUMINITE_LABEL, lambda v: v is not None, "the Luminite readout")
    wait_text(app, POWER_LABEL, lambda v: v is not None, "the power readout")


def test_luminite_and_power(app):
    pause(app)

    # Luminite in the inventory / Luminite shipped
    set_inventory(app, [("items.Luminite", 40), ("items.IronPlate", 3)])
    put(app, ROCKET, ROCKET, {"shipped": 25})
    wait_shown(app, LUMINITE_LABEL, "40 / 25")
    app.check(color(app, LUMINITE_LABEL) == TEAL,
              "the Luminite readout is %s" % (color(app, LUMINITE_LABEL),))
    set_inventory(app, [("items.IronPlate", 3)])
    wait_shown(app, LUMINITE_LABEL, "0 / 25")

    # Enough power: the watts rounded down, green, the bar at the ratio
    put(app, POWER, POWER, {"production": 40.7, "demand": 24.9, "ratio": 1, "blackout": False})
    wait_shown(app, POWER_LABEL, "40 / 24 W")
    app.check(color(app, POWER_LABEL) == GREEN and
              color(app, STATUS + ".power.fill") == GREEN,
              "the power gauge is %s" % (color(app, POWER_LABEL),))
    check_bar(app, "power", POWER_BAR, 1)

    # A blackout: red
    put(app, POWER, POWER, {"production": 20, "demand": 49.5, "ratio": 0.404,
                            "blackout": True})
    wait_shown(app, POWER_LABEL, "20 / 49 W")
    app.check(color(app, POWER_LABEL) == RED and color(app, STATUS + ".power.fill") == RED,
              "the power gauge in a blackout is %s" % (color(app, POWER_LABEL),))
    check_bar(app, "power", POWER_BAR, 0.404)


def test_health_and_mining(app):
    pause(app)

    # Full health: a full green bar, no alert
    app.wait_frames(SETTLE)
    check_bar(app, "hp", HP_BAR, 1)
    app.check(color(app, STATUS + ".hp.fill") == GREEN, "the full HP bar is not green")
    app.check(text(app, ALERT) is None, "the alert shows at full health")

    # Below 60%: amber
    app.exec("set %s OreHealth {value: 45, max: 100}" % PLAYER)
    app.wait_frames(SETTLE)
    check_bar(app, "hp", HP_BAR, 0.45)
    app.check(color(app, STATUS + ".hp.fill") == AMBER, "the HP bar at 45% is not amber")
    app.check(text(app, ALERT) is None, "the alert shows at 45%")

    # Below 30%: red, and the suit breach alert blinks with the fast signal
    app.exec("set %s OreHealth {value: 25, max: 100}" % PLAYER)
    wait_shown(app, ALERT, "! SUIT BREACH !")
    check_bar(app, "hp", HP_BAR, 0.25)
    app.check(color(app, STATUS + ".hp.fill") == RED, "the HP bar at 25% is not red")
    for fast, expected in ALERT_BLINK.items():
        put(app, BLINK, BLINK, {"fast": fast})
        app.wait_frames(SETTLE)
        app.check(color(app, ALERT) == expected,
                  "the alert is %s with the fast blink %s" % (color(app, ALERT), fast))

    app.exec("set %s OreHealth {value: 100, max: 100}" % PLAYER)
    wait_hidden(app, ALERT)

    # The mine progress bar shows while the progress is above 0
    app.check(not shown(app, MINE_BAR), "the mine bar shows without progress")
    app.exec("set %s OreMining {target: 0, left: 1, progress: 0.5}" % PLAYER)
    app.wait_for("get %s.fill LayoutItem.width.value" % MINE_BAR,
                 lambda out: abs(float(out) - (3 + (MINE_BAR_W - 3) * 0.5)) < 1e-3,
                 what="the mine bar half filled")
    app.check(color(app, MINE_BAR + ".fill") == MINE_FILL, "the mine bar fill is %s" %
              (color(app, MINE_BAR + ".fill"),))
    app.exec("set %s OreMining {target: 0, left: 0, progress: 0}" % PLAYER)
    app.wait_frames(SETTLE)
    app.check(not shown(app, MINE_BAR), "the mine bar shows after the progress ended")


def test_waves(app):
    pause(app)

    # The next wave: calm, then orange past 0.75 of the gap
    put(app, WAVES, WAVES, {"frac": 0.5, "left": 0})
    app.wait_frames(SETTLE)
    check_bar(app, "wave", WAVE_BAR, 0.5)
    app.check(color(app, STATUS + ".wave.fill") == WAVE_CALM, "the wave bar at 0.5 is %s" %
              (color(app, STATUS + ".wave.fill"),))
    put(app, WAVES, WAVES, {"frac": 0.8})
    app.wait_frames(SETTLE)
    check_bar(app, "wave", WAVE_BAR, 0.8)
    app.check(color(app, STATUS + ".wave.fill") == WAVE_SOON, "the wave bar at 0.8 is %s" %
              (color(app, STATUS + ".wave.fill"),))

    # A wave spawning: a full bar blinking with the fast signal
    put(app, WAVES, WAVES, {"frac": 1, "left": 3, "spawn": [3, 0, 0, 0]})
    for fast in (True, False):
        put(app, BLINK, BLINK, {"fast": fast})
        app.wait_frames(SETTLE)
        check_bar(app, "wave", WAVE_BAR, 1)
        app.check(color(app, STATUS + ".wave.fill") == WAVE_FLASH[fast],
                  "the spawning wave bar is %s with the fast blink %s" %
                  (color(app, STATUS + ".wave.fill"), fast))


def test_toast(app):
    start(app)

    # A warning in red, gone after its time
    put(app, "oreelse.OreToast", "oreelse.OreToast",
        {"text": "Too far away", "warn": True, "age": 0})
    wait_shown(app, TOAST_LINE, "Too far away")
    app.check(color(app, TOAST_LINE) == RED, "the warning is %s" % (color(app, TOAST_LINE),))
    wait_hidden(app, TOAST_LINE, frames=int(WARN_TOAST * FPS) + 30)

    # A message in teal, its time frozen while paused
    put(app, "oreelse.OreToast", "oreelse.OreToast",
        {"text": "Shipment away!  +25 Luminite", "warn": False, "age": 0})
    wait_shown(app, TOAST_LINE, "Shipment away!  +25 Luminite")
    app.check(color(app, TOAST_LINE) == TEAL, "the message is %s" % (color(app, TOAST_LINE),))
    set_state(app, "Paused")
    age = app.exec("get oreelse.OreToast OreToast.age")
    app.wait_frames(FPS)
    app.expect("get oreelse.OreToast OreToast.age", age)
    app.check(text(app, TOAST_LINE) == "Shipment away!  +25 Luminite",
              "the toast went away while paused")
    set_state(app, "Playing")
    wait_hidden(app, TOAST_LINE, frames=int(INFO_TOAST * FPS) + 30)

    # Not on the end screens
    put(app, "oreelse.OreToast", "oreelse.OreToast", {"text": "Too far away", "warn": True,
                                                       "age": 0})
    wait_shown(app, TOAST_LINE, "Too far away")
    set_state(app, "Lost")
    wait_hidden(app, TOAST_LINE)
    app.check(text(app, LUMINITE_LABEL) is not None, "the status bar is gone when lost")


def test_rocket_line(app):
    start(app)
    app.wait_frames(SETTLE)
    app.check(text(app, ROCKET_LINE) is None, "the rocket line shows without a pad")

    place_pad(app, [(ENGINE, 4), (FUEL, 1), (CARGO, 1)])
    wait_shown(app, ROCKET_LINE, "Install a cargo bay (U) or life support (I)")
    app.check(color(app, ROCKET_LINE) == AMBER, "the rocket line is %s" %
              (color(app, ROCKET_LINE),))

    press(app, ["U"])
    wait_shown(app, ROCKET_LINE, "Needs 4 engines, 0 installed (T)")
    press(app, ["T", "T"])
    wait_shown(app, ROCKET_LINE, "Needs 4 engines, 2 installed (T)")
    press(app, ["T", "T"])
    wait_shown(app, ROCKET_LINE, "Needs 1 fuel tanks, 0 installed (Y)")
    app.check(color(app, ROCKET_LINE) == AMBER, "the rocket line is %s" %
              (color(app, ROCKET_LINE),))

    press(app, ["Y"])
    wait_shown(app, ROCKET_LINE, "Rocket ready to launch (P)")
    app.check(color(app, ROCKET_LINE) in READY.values(), "the ready rocket line is %s" %
              (color(app, ROCKET_LINE),))
    set_state(app, "Paused")
    for slow in (True, False):
        put(app, BLINK, BLINK, {"slow": slow})
        app.wait_frames(SETTLE)
        app.check(color(app, ROCKET_LINE) == READY[slow],
                  "the ready rocket line is %s with the slow blink %s" %
                  (color(app, ROCKET_LINE), slow))
    set_state(app, "Playing")

    press(app, ["P"])
    wait_shown(app, ROCKET_LINE, "LAUNCHING")


def test_panel_blocks(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    select(app, "1", "Wall")

    # A click on the status bar does not reach the world: nothing is placed and no warning shows
    # (the tile under the cursor is out of reach, so a click that got through would warn)
    aim_tile(app, TILE[0], TILE[1], ON_PANEL)
    app.exec("mouse.click left")
    app.wait_input()
    app.wait_frames(SETTLE)
    app.check(tiles(app, "Wall") == [], "the click on the status bar placed %s" %
              tiles(app, "Wall"))
    app.expect(TOAST, '""')
    app.check(count(app, "Wall") == 5, "the click on the status bar spent a wall")

    # Nor does the wheel: the camera keeps its zoom level
    app.exec("mouse.scroll 0 -1")
    app.wait_input()
    app.wait_frames(SETTLE)
    app.expect(LEVEL, "0")

    # Off the panel the click places the wall and the wheel zooms out
    click(app, *TILE)
    app.check(tiles(app, "Wall") == [TILE], "the click off the panel placed %s" %
              tiles(app, "Wall"))
    app.exec("mouse.scroll 0 -1")
    app.wait_input()
    app.wait_for(LEVEL, "1", what="the wheel off the panel zooming out")
