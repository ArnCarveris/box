"""Ore Else driven through its command port: the items of the inventory, the hover tile under the
cursor and mining deposits with the right mouse button.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta
of 1/60 s per frame, waits that count frames. The cases set up the inventory, the astronaut's
position and the cursor directly. The camera follows the astronaut rigidly at zoom level 0, so a
cursor position always hovers the same offset from the astronaut: a case aims at a tile by
reading that offset once and placing the astronaut the offset away from the tile.
"""
import json
import urllib.error
import urllib.request

from box_client import DELTA_TIME, parse_value

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

# Frames per second of game time
FPS = round(1 / float(DELTA_TIME))

STATE = "has oreelse.OreGameState (oreelse.OreGameState, oreelse.OreGameState.%s)"
TOAST = "get oreelse.OreToast OreToast.text"
PLAYER = "astronaut"
INVENTORY = "oreelse.OreInventory"
MARKER = "markers.mine"
MARKER_BAD = "markers.mine_bad"

# The tile grid (cfg.x0, cfg.z0, cfg.tile) and the reach of mining (cfg.reach)
X0 = -47
Z0 = -47
TILE = 2
REACH = 15

# The slots of the inventory, the starting crate in slot order and the full warning
SLOTS = 15
CRATE = [("items.IronPlate", 16), ("items.CopperPlate", 12), ("SolarPanel", 1)]
FULL = "Inventory full - 15 item kinds is the limit"

# Mining times in seconds (OreMined of resources.flecs) and a deposit's amount
STONE_TIME = 1.5
IRON_TIME = 4.5
DEPOSIT_AMOUNT = 1000

# Fifteen kinds without stone for a full inventory: the items besides the buildings, and three
# other prefabs of the scene, since the inventory holds any entity
OTHER_KINDS = ["items.Iron", "items.Copper", "items.Luminite", "items.IronPlate",
               "items.CopperPlate", "items.StoneBrick", "items.Wire", "items.Electronics",
               "items.RocketEngine", "items.RocketCargoBay", "items.RocketLifeSupport",
               "items.RocketFuelStorage", "TileMarker", "TileMarkerBad", "Astronaut"]

# Cursor positions in points of the 640x360 window (the headless app has no window size to
# scale normalized positions with): the center hovers a tile next to the astronaut, the far
# point one out of reach, up and right of it beside the status bar of the HUD (x 116..622,
# y 16..62), which keeps the buttons from the world
CENTER = (320, 180)
LEFT = (256, 180)
FAR = (630, 100)

# Frames for the cursor ray and the camera to follow a change
SETTLE = 5
SWING_SLACK = 1.5


def put(app, entity, component, value=None):
    """Add a component to an entity, or set it to a value, through the REST endpoint of the
    port."""
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


SCRIPTS = [0]


def run_script(app, code):
    """Run a script through the REST endpoint of the port, a new script entity every time."""
    SCRIPTS[0] += 1
    name = "mining_case_%d" % SCRIPTS[0]
    request = urllib.request.Request(app.url("script/" + name, {"code": code}), method="PUT")
    try:
        with urllib.request.urlopen(request, timeout=10) as r:
            status, body = r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read().decode(errors="replace")
    app.check(status == 200 and '"error"' not in body,
              "the script %r failed (%d): %s" % (code, status, body))


def set_state(app, state):
    put(app, "oreelse.OreGameState",
        "(oreelse.OreGameState,oreelse.OreGameState.%s)" % state)
    app.wait_for(STATE % state, "true")


def setup(app):
    """Shrink the window and drop the shadows of the world view, so the software renderer keeps
    up."""
    app.exec("set window Surface {width: 640, height: 360}")
    app.exec("set main_view View.shadows.enabled false")
    app.wait_for("get window SurfaceStatus.height", "360")


def key(app, name, frames=None):
    app.exec("key.press %s" % name + (" %d" % frames if frames else ""))
    app.wait_input()


def start(app):
    """Set up and leave the title screen with a key press."""
    setup(app)
    app.expect(STATE % "Title", "true")
    key(app, "Space")
    app.wait_for(STATE % "Playing", "true")


def entity(value):
    """An entity as get prints it, None for no entity."""
    return None if value in (0, "0", "#0", "", None) else value


def inventory(app):
    """The slots of the inventory in order: (item, amount), None for a free slot."""
    slots = app.get(INVENTORY, "OreInventory")["slots"]
    return [(entity(s["item"]), s["amount"]) if entity(s["item"]) else None for s in slots]


def held(slots):
    """The kinds an inventory holds in slot order."""
    return [s for s in slots if s]


def count(app, item):
    for s in held(inventory(app)):
        if s[0] == item:
            return s[1]
    return 0


def set_inventory(app, stacks):
    """Set every slot of the inventory: the stacks in order, None for a free slot."""
    slots = [{"item": s[0], "amount": s[1]} if s else {"item": 0, "amount": 0} for s in stacks]
    slots += [{"item": 0, "amount": 0}] * (SLOTS - len(slots))
    put(app, INVENTORY, INVENTORY, {"slots": slots})


def deposits(app):
    """The deposits of the map by path: their OreDeposit (resource, amount, capacity, row,
    col), from one query of the command port."""
    app.exec("query ores oreelse.OreDeposit")
    app.exec("iter ores")
    out = app.exec("next 1000")
    result = {}
    for line in out.splitlines():
        if not line.strip() or line == "(end)":
            continue
        path, value = line.split(" ", 1)
        result[path] = parse_value(value)
    return result


def deposit_of(app, kind, neighbor=False):
    """The path and OreDeposit of a full deposit of a kind, with a deposit on the tile left of
    it when neighbor is set: (path, deposit, neighbor path)."""
    found = deposits(app)
    tiles = {(d["row"], d["col"]): path for path, d in found.items()}
    for path in sorted(found):
        d = found[path]
        if d["resource"] != kind or d["amount"] != DEPOSIT_AMOUNT:
            continue
        left = tiles.get((d["row"], d["col"] - 1))
        if not neighbor or left:
            return path, d, left
    app.fail("the map of seed %d has no deposit of %s%s" %
             (SEED, kind, " with a neighbor" if neighbor else ""))


def tile_center(d):
    return X0 + d["col"] * TILE, Z0 + d["row"] * TILE


def hover(app):
    return app.get("oreelse.OreHover", "OreHover")


def point(h):
    p = h["point"]
    return (p["x"], p["z"]) if isinstance(p, dict) else (p[0], p[2])


def player(app):
    return app.get(PLAYER, "Position")


def move_cursor(app, cursor):
    app.exec("mouse.move [%g, %g]" % cursor)
    app.wait_input()
    app.wait_frames(SETTLE)


def offset(app, cursor):
    """The ground point a cursor position hovers, relative to the astronaut."""
    move_cursor(app, cursor)
    p = player(app)
    x, z = point(hover(app))
    return x - p["x"], z - p["z"]


def place(app, x, z):
    app.exec("set %s Position {%g, 0, %g}" % (PLAYER, x, z))
    app.wait_frames(SETTLE)


def aim(app, d, cursor=CENTER):
    """Place the astronaut so that a cursor position hovers the center of the tile of a
    deposit; returns the distance of the astronaut to the tile center."""
    ox, oz = offset(app, cursor)
    x, z = tile_center(d)
    place(app, x - ox, z - oz)
    h = hover(app)
    app.check(h["on_grid"] and (h["row"], h["col"]) == (d["row"], d["col"]),
              "the cursor hovers %s, not the deposit at %s" %
              (h, (d["row"], d["col"])))
    return (ox * ox + oz * oz) ** 0.5


def mouse(app, down):
    app.exec("mouse.button right %s" % ("true" if down else "false"))
    app.wait_input()


def mining(app):
    m = app.get(PLAYER, "OreMining")
    m["target"] = entity(m["target"])
    return m


def swing_time(m):
    """The time of the swing in progress, from its seconds left and its progress."""
    return m["left"] / (1 - m["progress"])


def disabled(app, marker):
    return app.exec("has %s flecs.core.Disabled" % marker) == "true"


def check_marker(app, d, good):
    """The marker of a target in reach or out of reach shows on its tile, the other is
    hidden."""
    shown, hidden = (MARKER, MARKER_BAD) if good else (MARKER_BAD, MARKER)
    app.wait_for("has %s flecs.core.Disabled" % shown, "false",
                 what="the %s marker shown" % shown)
    app.check(disabled(app, hidden), "the %s marker shows too" % hidden)
    p = app.get(shown, "Position")
    x, z = tile_center(d)
    app.check(abs(p["x"] - x) < 1e-4 and abs(p["y"] - 0.14) < 1e-4 and abs(p["z"] - z) < 1e-4,
              "the marker is at %s, not on the tile at (%g, 0.14, %g)" % (p, x, z))


def check_no_marker(app):
    app.wait_for("has %s flecs.core.Disabled" % MARKER, "true", what="no marker")
    app.check(disabled(app, MARKER_BAD), "the out of reach marker shows")


def amount(app, path):
    d = deposits(app).get(path)
    return d["amount"] if d else None


def test_starting_crate(app):
    start(app)
    slots = inventory(app)
    app.check(len(slots) == SLOTS, "the inventory has %d slots, not %d" % (len(slots), SLOTS))
    app.check(slots[:len(CRATE)] == CRATE and not held(slots[len(CRATE):]),
              "the inventory holds %s, not the crate %s in slot order" % (held(slots), CRATE))

    # A restart brings the crate back
    set_inventory(app, [("items.Stone", 3), None, ("items.Wire", 5)])
    app.wait_for("get %s OreInventory.slots[0].amount" % INVENTORY, "3")
    set_state(app, "Lost")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    slots = inventory(app)
    app.check(slots[:len(CRATE)] == CRATE and not held(slots[len(CRATE):]),
              "after a restart the inventory holds %s, not the crate %s" % (held(slots), CRATE))


def test_mine_stone_and_iron(app):
    start(app)
    path, d, _ = deposit_of(app, "items.Stone")
    distance = aim(app, d)
    app.check(distance <= REACH, "the tile is %g away, out of reach" % distance)
    check_no_marker(app)

    clock = float(app.exec("get oreelse.OreClock OreClock.elapsed"))
    mouse(app, True)
    app.wait_frames(10)
    m = mining(app)
    app.check(m["target"] == path and 0 < m["progress"] < 1,
              "the swing is %s, not at %s in progress" % (m, path))
    app.check(abs(swing_time(m) - STONE_TIME) < 1e-3,
              "a swing at stone takes %g s, not %g" % (swing_time(m), STONE_TIME))
    check_marker(app, d, True)
    app.wait_for("get %s OreInventory.slots[3].amount" % INVENTORY, "1",
                 frames=round((STONE_TIME + SWING_SLACK) * FPS), what="1 stone mined")
    took = float(app.exec("get oreelse.OreClock OreClock.elapsed")) - clock
    app.check(took >= STONE_TIME - 0.05, "the stone was mined after %g s, not %g" %
              (took, STONE_TIME))
    app.check(inventory(app)[3] == ("items.Stone", 1),
              "the stone is not in the fourth slot, after the crate: %s" % inventory(app))
    app.check(amount(app, path) == DEPOSIT_AMOUNT - 1,
              "%s holds %s, not %d" % (path, amount(app, path), DEPOSIT_AMOUNT - 1))

    # The next swing starts at once
    m = mining(app)
    app.check(m["target"] == path, "the next swing is at %s, not %s" % (m["target"], path))
    mouse(app, False)

    path, d, _ = deposit_of(app, "items.Iron")
    aim(app, d)
    mouse(app, True)
    app.wait_frames(10)
    m = mining(app)
    app.check(m["target"] == path and 0 < m["progress"] < 1,
              "the swing is %s, not at %s in progress" % (m, path))
    app.check(abs(swing_time(m) - IRON_TIME) < 1e-3,
              "a swing at iron takes %g s, not %g" % (swing_time(m), IRON_TIME))

    # Skip ahead to the end of the swing
    app.exec("set %s OreMining {target: %s, left: 0.25, progress: %g}" %
             (PLAYER, path, 1 - 0.25 / IRON_TIME))
    app.wait_for("get %s OreInventory.slots[4].amount" % INVENTORY, "1",
                 frames=round((0.25 + SWING_SLACK) * FPS), what="1 iron mined")
    app.check(inventory(app)[4] == ("items.Iron", 1),
              "the iron is not in the fifth slot: %s" % inventory(app))
    app.check(amount(app, path) == DEPOSIT_AMOUNT - 1,
              "%s holds %s, not %d" % (path, amount(app, path), DEPOSIT_AMOUNT - 1))
    mouse(app, False)


def test_retarget_and_release(app):
    start(app)
    path, d, other = deposit_of(app, "items.Stone", neighbor=True)
    center = offset(app, CENTER)
    left = offset(app, LEFT)
    app.check(abs(left[1] - center[1]) < 1e-3, "a cursor further left hovers another row")
    # The cursor further left hovers TILE units left of the center at this cursor x
    shift = CENTER[0] + (LEFT[0] - CENTER[0]) * -TILE / (left[0] - center[0])
    move_cursor(app, CENTER)
    aim(app, d)

    mouse(app, True)
    app.wait_frames(30)
    before = mining(app)
    app.check(before["target"] == path and before["progress"] > 0.1,
              "the swing is %s, not at %s in progress" % (before, path))

    move_cursor(app, (shift, CENTER[1]))
    h = hover(app)
    app.check((h["row"], h["col"]) == (d["row"], d["col"] - 1),
              "the cursor hovers %s, not the deposit left of %s" % (h, path))
    after = mining(app)
    app.check(after["target"] == other and after["progress"] < before["progress"],
              "after moving to %s the swing is %s, not restarted from %s" %
              (other, after, before))
    app.check(amount(app, path) == DEPOSIT_AMOUNT, "%s was mined" % path)

    mouse(app, False)
    app.wait_frames(SETTLE)
    m = mining(app)
    app.check(m["progress"] == 0 and m["target"] is None,
              "after the release the swing is %s, not reset" % m)
    check_no_marker(app)


def test_out_of_reach(app):
    start(app)
    path, d, _ = deposit_of(app, "items.Stone")
    distance = aim(app, d, FAR)
    app.check(distance > REACH, "the tile is %g away, within reach" % distance)

    mouse(app, True)
    check_marker(app, d, False)
    app.wait_frames(round((STONE_TIME + 0.5) * FPS))
    m = mining(app)
    app.check(m["progress"] == 0 and m["target"] is None,
              "out of reach the swing is %s" % m)
    app.check(count(app, "items.Stone") == 0, "a stone was mined out of reach")
    app.check(amount(app, path) == DEPOSIT_AMOUNT, "%s was mined out of reach" % path)
    check_marker(app, d, False)

    # The marker only shows while playing
    set_state(app, "Paused")
    check_no_marker(app)
    set_state(app, "Playing")
    check_marker(app, d, False)
    mouse(app, False)
    check_no_marker(app)


def test_last_ore(app):
    start(app)
    path, d, _ = deposit_of(app, "items.Stone")
    app.exec("set %s OreDeposit {resource: items.Stone, amount: 1, capacity: %d, row: %d, "
             "col: %d}" % (path, DEPOSIT_AMOUNT, d["row"], d["col"]))
    app.wait_for("get %s OreDeposit.amount" % path, "1")
    aim(app, d)

    mouse(app, True)
    app.wait_for("get %s OreInventory.slots[3].amount" % INVENTORY, "1",
                 frames=round((STONE_TIME + SWING_SLACK) * FPS), what="the last stone mined")
    app.wait_frames(SETTLE)
    app.check(path not in deposits(app), "%s is still there with 0 ore" % path)

    # The tile is empty ground: nothing to mine and no marker while the button is held
    h = hover(app)
    app.check((h["row"], h["col"]) == (d["row"], d["col"]), "the cursor left the tile")
    m = mining(app)
    app.check(m["target"] is None and m["progress"] == 0,
              "the swing is %s on an empty tile" % m)
    check_no_marker(app)
    mouse(app, False)


def test_inventory_full(app):
    start(app)
    full = [(kind, 1) for kind in OTHER_KINDS]
    set_inventory(app, full)
    app.wait_for("get %s OreInventory.slots[14].amount" % INVENTORY, "1")
    path, d, _ = deposit_of(app, "items.Stone")
    aim(app, d)

    mouse(app, True)
    app.wait_for(TOAST, '"%s"' % FULL, frames=round((STONE_TIME + SWING_SLACK) * FPS),
                 what="the inventory full warning")
    app.check(inventory(app) == full, "the inventory changed: %s" % inventory(app))
    app.check(amount(app, path) == DEPOSIT_AMOUNT,
              "%s holds %s, not %d" % (path, amount(app, path), DEPOSIT_AMOUNT))

    # The next swing starts at the same deposit
    m = mining(app)
    app.check(m["target"] == path and m["progress"] < 1, "the next swing is %s" % m)
    mouse(app, False)


def test_slot_frees(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 16), ("items.Stone", 2), ("items.CopperPlate", 12)])
    app.wait_for("get %s OreInventory.slots[1].amount" % INVENTORY, "2")

    run_script(app, "const ok: bool = oreelse.inventory_remove(items.Stone, 1)")
    app.check(inventory(app)[:3] == [("items.IronPlate", 16), ("items.Stone", 1),
                                     ("items.CopperPlate", 12)],
              "after taking 1 stone the inventory is %s" % held(inventory(app)))

    # Taking more than the inventory holds changes nothing
    run_script(app, "const ok: bool = oreelse.inventory_remove(items.Stone, 2)")
    app.check(count(app, "items.Stone") == 1, "2 of 1 stone were taken")

    run_script(app, "const ok: bool = oreelse.inventory_remove(items.Stone, 1)")
    slots = inventory(app)
    app.check(slots[:3] == [("items.IronPlate", 16), None, ("items.CopperPlate", 12)],
              "at 0 stone the slots are %s, not with the second one free" % slots[:3])

    # A kind that is held keeps its slot, a new kind takes the first free one
    run_script(app, "const ok: bool = oreelse.inventory_add(items.CopperPlate, 3)")
    run_script(app, "const ok: bool = oreelse.inventory_add(items.Wire, 2)")
    slots = inventory(app)
    app.check(slots[:4] == [("items.IronPlate", 16), ("items.Wire", 2),
                            ("items.CopperPlate", 15), None],
              "the wire did not take the free slot: %s" % slots[:4])

    # So does a mined one
    run_script(app, "const ok: bool = oreelse.inventory_remove(items.IronPlate, 16)")
    path, d, _ = deposit_of(app, "items.Stone")
    aim(app, d)
    mouse(app, True)
    app.wait_for("get %s OreInventory.slots[0].amount" % INVENTORY, "1",
                 frames=round((STONE_TIME + SWING_SLACK) * FPS), what="1 stone mined")
    mouse(app, False)
    slots = inventory(app)
    app.check(slots[:3] == [("items.Stone", 1), ("items.Wire", 2), ("items.CopperPlate", 15)],
              "the mined stone did not take the free first slot: %s" % slots[:3])


def test_hover_through_box(app):
    start(app)
    cursors = [CENTER, LEFT, (384, 144)]
    tiles = []
    for cursor in cursors:
        move_cursor(app, cursor)
        h = hover(app)
        app.check(h["on_grid"], "the cursor at %s hovers no tile" % (cursor,))
        tiles.append((h["row"], h["col"], point(h)))
    app.check(len(set(t[:2] for t in tiles)) == len(tiles),
              "the cursor positions hover the same tile: %s" % tiles)

    # A tall box over the ground under the cursors, between them and the camera
    x, z = tiles[0][2]
    app.exec("touch blocker")
    app.exec("set blocker Position {%g, 2, %g}" % (x, z))
    app.exec("set blocker Box {14, 4, 10}")
    app.exec("set blocker Rgb {90, 90, 90}")
    app.wait_frames(SETTLE)

    for cursor, tile in zip(cursors, tiles):
        move_cursor(app, cursor)
        app.check(app.get("box.picking.PickStatus", "PickStatus")["hit"] == "blocker",
                  "the cursor at %s does not point at the box" % (cursor,))
        h = hover(app)
        app.check(h["on_grid"] and (h["row"], h["col"]) == tile[:2],
                  "behind the box the cursor at %s hovers %s, not the tile %s" %
                  (cursor, h, tile[:2]))
        px, pz = point(h)
        app.check(abs(px - tile[2][0]) < 1e-3 and abs(pz - tile[2][1]) < 1e-3,
                  "behind the box the ground point is %s, not %s" % ((px, pz), tile[2]))
        app.check(0 <= h["fx"] <= 1 and 0 <= h["fz"] <= 1,
                  "the fractions in the tile are %g, %g" % (h["fx"], h["fz"]))
