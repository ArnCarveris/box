"""Ore Else driven through its command port: the title screen, pause and resume, restart, the
seeded deposits of the map, walking, the zoom levels of the camera and the toast.

Every case runs in a fresh apps/oreelse process with its default scene
(apps/oreelse/etc/oreelse.flecs) and a fixed session seed, and gets the App of
tools/box_client.py; the runner checks after every case that the app logged no error and quits
cleanly. The runner steps the app by a fixed delta of 1/60 s per frame (--delta-time), and the
software renderer of the test machines draws a few frames per second. So every case starts
with setup: the window shrinks to 640x360 and the world view drops its shadows; --fps 20 caps
it on faster machines. Waits count frames, so they hold under load: FPS frames are one second
of the game. The cases set up state directly (the game state, the clock, the toast) instead of
playing up to it. Those are singletons of the oreelse module, which the commands of the port do
not write, so the cases write them through the REST endpoints of the same port.
"""
import json
import math
import urllib.error
import urllib.request

from box_client import App, DELTA_TIME, parse_value

APP = "apps/oreelse"
# The map of seed 8 has two clusters that overlap on two tiles, so the cases also see the first
# deposit generated keep a tile
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

# Frames per second of game time
FPS = round(1 / float(DELTA_TIME))

STATE = ("eval oreelse.OreGameState.has(flecs.script.core.pair("
         "oreelse.OreGameState,oreelse.OreGameState.%s))")
CLOCK = "get oreelse.OreClock oreelse.OreClock.elapsed"
TOAST = "get oreelse.OreToast oreelse.OreToast.text"
LEVEL = "get oreelse.OreView oreelse.OreView.level"
CAMERA = "get camera box.transform.Position"
CAMERA_PITCH = "get camera box.transform.Rotation.x"
PLAYER = "astronaut"

# Walking speed in units per second and the run multiplier (cfg.walkSpeed, cfg.runMultiplier)
WALK = 7
RUN = 2

# The astronaut stays within MAP_CLAMP of the center (cfg.mapClamp)
MAP_CLAMP = 47

# A walk of WALK_FRAMES frames at 1/60 s; positions agree within half a frame of walking
WALK_FRAMES = 60
WALK_TOLERANCE = 0.05

# The zoom levels: height of the camera and its pitch in degrees (cfg.zoom)
ZOOM = [(15, 44.6), (26.3, 51.8), (38.9, 59.5)]
ZOOM_FRAMES = 3 * FPS
HEIGHT_TOLERANCE = 0.05
PITCH_TOLERANCE = math.radians(0.1)

# Seconds a warning and any other toast are shown (cfg.warnToast, cfg.infoToast)
WARN_TOAST = 2.5
INFO_TOAST = 4
TOAST_TEXT = "Too far away"

# The Luminite deposits take the four tiles at the center, in generation order
LUMINITE_TILES = [(23, 23), (23, 24), (24, 23), (24, 24)]
DEPOSIT_AMOUNT = 1000
CLUSTER_ORES = {"iron": "items.Iron", "copper": "items.Copper", "stone": "items.Stone"}

# Deposits of a map without overlaps: 4 Luminite and two clusters of 9 of the other kinds
DEPOSITS = len(LUMINITE_TILES) + 2 * 9 * len(CLUSTER_ORES)


def number(out):
    return float(out)


def put(app, entity, component, value=None):
    """Add a component or pair to an entity, or set it to a value, through the REST endpoint of
    the port."""
    params = {"component": component}
    if value is not None:
        params["value"] = json.dumps(value)
    path = "component/" + entity.replace(".", "/")
    request = urllib.request.Request(app.url(path, params), method="PUT")
    try:
        with urllib.request.urlopen(request, timeout=10) as r:
            status, body = r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read().decode(errors="replace")
    app.check(status == 200, "PUT %s %s failed (%d): %s" % (entity, component, status, body))


def set_state(app, state):
    put(app, "oreelse.OreGameState",
        "(oreelse.OreGameState,oreelse.OreGameState.%s)" % state)
    app.wait_for(STATE % state, "true")


def toast(app, text, warn):
    put(app, "oreelse.OreToast", "oreelse.OreToast", {"text": text, "warn": warn, "age": 0})


def setup(app):
    """Shrink the window and drop the shadows of the world view, so the software renderer keeps
    up."""
    app.exec("set window box.surface.Surface {width: 640, height: 360}")
    app.exec("set main_view box.view.View.shadows.enabled false")
    app.wait_for("get window box.surface.SurfaceStatus.height", "360")


def key(app, name, frames=None):
    app.exec("key_press %s" % name + (" %d" % frames if frames else ""))
    app.wait_input()


def start(app):
    """Leave the title screen with a key press."""
    app.expect(STATE % "Title", "true")
    key(app, "Space")
    app.wait_for(STATE % "Playing", "true")


def player(app):
    return app.get(PLAYER, "box.transform.Position")


def deposits(app):
    """The deposits of the map by path: their OreDeposit (resource, amount, capacity, row,
    col), from one query of the command port."""
    out = app.exec("query oreelse.OreDeposit")
    return {row["entity"]: row["fields"][0]["value"]["OreDeposit"] for row in parse_value(out)}


def layout(found):
    """The layout of a map: the resource and the tile of every deposit."""
    return sorted((path, d["resource"], d["row"], d["col"]) for path, d in found.items())


def generated(path):
    """The place of a deposit in the generation order: Luminite, then the clusters of iron,
    copper and stone, each cluster tile by tile."""
    name = path.split(".")[-1]
    kind, numbers = name.split("_", 1)
    order = ["luminite"] + list(CLUSTER_ORES)
    return [order.index(kind)] + [int(n) for n in numbers.split("_")]


def check_layout(app, found):
    """The Luminite at the center tiles, every other kind in 3x3 clusters, no two deposits on
    one tile, a tile two clusters share held by the deposit generated first, every deposit
    full."""
    tiles = {}
    for path, d in found.items():
        tile = (d["row"], d["col"])
        app.check(tile not in tiles, "%s and %s share the tile %s" % (path, tiles.get(tile), tile))
        tiles[tile] = path
        app.check(d["amount"] == DEPOSIT_AMOUNT and d["capacity"] == DEPOSIT_AMOUNT,
                  "%s holds %s of %s, not %d" % (path, d["amount"], d["capacity"],
                                                 DEPOSIT_AMOUNT))
        app.check(path.startswith("deposits."), "%s is not a child of deposits" % path)

    for j, tile in enumerate(LUMINITE_TILES):
        d = found.get("deposits.luminite_%d" % j)
        app.check(d is not None, "deposits.luminite_%d is missing" % j)
        app.check(d["resource"] == "items.Luminite" and (d["row"], d["col"]) == tile,
                  "deposits.luminite_%d is %s at %s, not Luminite at %s" %
                  (j, d["resource"], (d["row"], d["col"]), tile))

    for kind, resource in CLUSTER_ORES.items():
        for i in range(2):
            members = {j: found["deposits.%s_%d_%d" % (kind, i, j)] for j in range(9)
                       if "deposits.%s_%d_%d" % (kind, i, j) in found}
            app.check(members, "the %s cluster %d has no deposits" % (kind, i))
            j, d = next(iter(members.items()))
            cc = d["col"] - (j % 3 - 1)
            cr = d["row"] - (j // 3 - 1)
            for j in range(9):
                tile = (cr + j // 3 - 1, cc + j % 3 - 1)
                d = members.get(j)
                if d is None:
                    path = "deposits.%s_%d_%d" % (kind, i, j)
                    app.check(tile in tiles, "%s is missing but %s is free" % (path, tile))
                    app.check(generated(tiles[tile]) < generated(path),
                              "%s holds %s, which %s was generated before" %
                              (tiles[tile], tile, path))
                    continue
                app.check((d["row"], d["col"]) == tile and d["resource"] == resource,
                          "%s_%d_%d is %s at %s, not %s at %s of the 3x3 cluster" %
                          (kind, i, j, d["resource"], (d["row"], d["col"]), resource, tile))


def test_title_and_pause(app):
    setup(app)
    app.expect(STATE % "Title", "true")
    app.wait_frames(10)
    app.expect(STATE % "Title", "true")
    app.expect(CLOCK, "0")

    # The ESC that starts the game does not also pause it
    key(app, "Escape")
    app.wait_for(STATE % "Playing", "true")
    app.wait_frames(5)
    app.expect(STATE % "Playing", "true")
    app.wait_for(CLOCK, lambda out: number(out) > 0, what="the clock counting")

    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")
    paused = app.exec(CLOCK)
    x = player(app)["x"]
    key(app, "D", 10)
    app.wait_frames(FPS // 2)
    app.expect(CLOCK, paused)
    app.check(player(app)["x"] == x, "the astronaut walked while paused")

    key(app, "Escape")
    app.wait_for(STATE % "Playing", "true")
    app.wait_for(CLOCK, lambda out: number(out) > number(paused), what="the clock counting again")


def test_restart(app):
    setup(app)
    start(app)
    before = deposits(app)
    check_layout(app, before)

    # Restart does nothing while playing
    key(app, "R")
    app.wait_frames(5)
    app.expect(STATE % "Playing", "true")
    app.check(layout(deposits(app)) == layout(before), "R changed the map while playing")

    key(app, "D", 30)
    app.check(player(app)["x"] > 1, "the astronaut did not walk away from the start")
    put(app, "oreelse.OreClock", "oreelse.OreClock", {"elapsed": 100})
    app.exec("mouse_scroll 0 -1")
    app.wait_input()
    app.wait_for(LEVEL, "1")
    set_state(app, "Lost")

    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    elapsed = number(app.exec(CLOCK))
    app.check(elapsed < 2, "the clock is at %g after the restart" % elapsed)
    pos = player(app)
    app.check(pos == {"x": 0, "y": 0, "z": 0}, "the astronaut is at %s after the restart" % pos)
    app.expect(LEVEL, "1")
    after = deposits(app)
    check_layout(app, after)
    app.check(layout(after) != layout(before), "the new run has the map of the previous one")


def test_seeded_map(app):
    first = deposits(app)
    check_layout(app, first)
    app.check(len(first) < DEPOSITS, "the map of seed %d has no overlapping clusters" % SEED)
    log = app.log[:-len(".log")] + ".second.log" if app.log else None
    with App(app.binary, app.args, log=log, timeout=app.timeout, out=app.out,
             prefix=app.prefix + ".second") as other:
        second = deposits(other)
        other.expect("logs error", '""')
        other.quit()
    app.check(layout(first) == layout(second),
              "two runs with --seed %d made different maps" % SEED)


def test_walk(app):
    setup(app)
    start(app)
    x = player(app)["x"]
    key(app, "D", WALK_FRAMES)
    pos = player(app)
    app.check(abs(pos["x"] - x - WALK) < WALK_TOLERANCE and pos["z"] == 0,
              "the astronaut walked to %s, not %g along +X" % (pos, WALK))
    yaw = app.get(PLAYER, "box.transform.Rotation.y")
    app.check(abs(yaw - math.pi / 2) < 1e-4, "the astronaut faces %g, not +X" % yaw)

    x = pos["x"]
    app.exec("key_down ShiftLeft")
    app.wait_input()
    key(app, "D", WALK_FRAMES)
    app.exec("key_up ShiftLeft")
    app.wait_input()
    pos = player(app)
    app.check(abs(pos["x"] - x - WALK * RUN) < WALK_TOLERANCE,
              "the astronaut ran to %s, not %g along +X" % (pos, WALK * RUN))

    app.exec("set %s box.transform.Position {%d, 0, 0}" % (PLAYER, MAP_CLAMP - 3))
    key(app, "D", WALK_FRAMES)
    pos = player(app)
    app.check(pos["x"] == MAP_CLAMP, "the astronaut stopped at %s, not at x = %d" %
              (pos, MAP_CLAMP))


def test_zoom(app):
    setup(app)

    def reaches(level):
        height, pitch = ZOOM[level]
        app.wait_for(LEVEL, str(level))
        app.wait_for(CAMERA, lambda out: abs(parse_value(out)["y"] - height) < HEIGHT_TOLERANCE,
                     frames=ZOOM_FRAMES, what="the camera at height %g" % height)
        out = number(app.exec(CAMERA_PITCH))
        app.check(abs(out + math.radians(pitch)) < PITCH_TOLERANCE,
                  "the camera pitch is %g, not -%g degrees" % (out, pitch))

    # The wheel zooms on the title screen too; down zooms out, up zooms in
    reaches(0)
    app.exec("mouse_scroll 0 -1")
    app.wait_input()
    reaches(1)
    app.exec("mouse_scroll 0 -1")
    app.wait_input()
    reaches(2)
    app.exec("mouse_scroll 0 -1")
    app.wait_input()
    app.wait_frames(5)
    app.expect(LEVEL, "2")
    app.exec("mouse_scroll 0 1")
    app.wait_input()
    reaches(1)


def test_toast(app):
    setup(app)
    start(app)
    for warn, seconds in ((True, WARN_TOAST), (False, INFO_TOAST)):
        shown = number(app.exec(CLOCK))
        toast(app, TOAST_TEXT, warn)
        app.wait_for(TOAST, '"%s"' % TOAST_TEXT)
        app.wait_for(TOAST, '""', frames=round((seconds + 2) * FPS),
                     what="the toast gone after %g s" % seconds)
        shown = number(app.exec(CLOCK)) - shown
        app.check(seconds - 0.05 <= shown <= seconds + 0.5,
                  "a toast with warn %s was shown %g s of play, not %g" % (warn, shown, seconds))

    toast(app, TOAST_TEXT, True)
    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")
    app.wait_frames(round(WARN_TOAST * FPS) + 30)
    app.expect(TOAST, '"%s"' % TOAST_TEXT)
    key(app, "Escape")
    app.wait_for(STATE % "Playing", "true")
    app.wait_for(TOAST, '""', frames=round((WARN_TOAST + 2) * FPS),
                 what="the toast gone after %g s of play" % WARN_TOAST)
