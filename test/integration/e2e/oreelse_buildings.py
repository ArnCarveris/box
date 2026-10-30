"""Ore Else driven through its command port: the buildings as items, placing them with a click,
a line, an L and a rectangle, the selection keys and the pipette, demolishing with salvage, the
connectors between walls, the jetpack and the steps of the ESC chain that placement adds.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta
of 1/60 s per frame, waits that count frames. The cases set up the inventory, the astronaut's
position and the cursor directly. The camera follows the astronaut rigidly at zoom level 0, so a
cursor position always hovers the same offset from the astronaut: a case aims at a ground point
by reading that offset once and placing the astronaut the offset away from the point. A drag
presses at one cursor position and releases at another, the astronaut moved in between so the
release lands on the point the case wants, down to the fraction inside the tile.
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

STATE = ("eval oreelse.OreGameState.has(flecs.script.core.pair("
         "oreelse.OreGameState,oreelse.OreGameState.%s))")
TOAST = "get oreelse.OreToast oreelse.OreToast.text"
PLAYER = "astronaut"
INVENTORY = "oreelse.OreInventory"
PLACEMENT = "oreelse.OrePlacement"

# The tile grid (cfg.x0, cfg.z0, cfg.tile, cfg.rows) and the reach (cfg.reach)
X0 = -47
Z0 = -47
TILE = 2
REACH = 15

# Seconds a swing at a building takes (cfg.dozeTime), the jetpack's height (cfg.hoverHeight)
DOZE_TIME = 0.8
HOVER_HEIGHT = 3

SLOTS = 15
FULL = "Inventory full - 15 item kinds is the limit"

# Cursor positions in points of the 640x360 window: the center hovers a tile next to the
# astronaut, the release of a drag a point a little to the right and below it
CENTER = (320, 180)
RELEASE = (352, 196)

# A corner of the map of seed 8 without deposits (the clusters lie within 10 tiles of the
# center), where the cases build: rows and columns 3 to 12
AREA = (3, 12)

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
    name = "buildings_case_%d" % SCRIPTS[0]
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
    app.exec("set window box.surface.Surface {width: 640, height: 360}")
    app.exec("set main_view box.view.View.shadows.enabled false")
    app.wait_for("get window box.surface.SurfaceStatus.height", "360")


def key(app, name, frames=None):
    app.exec("key_press %s" % name + (" %d" % frames if frames else ""))
    app.wait_input()


def start(app):
    """Set up and leave the title screen with a key press."""
    setup(app)
    app.expect(STATE % "Title", "true")
    key(app, "Space")
    app.wait_for(STATE % "Playing", "true")
    free_area(app)


def entity(value):
    """An entity as get prints it, None for no entity."""
    return None if value in (0, "0", "#0", "", None) else value


def inventory(app):
    """The slots of the inventory in order: (item, amount), None for a free slot."""
    slots = app.get(INVENTORY, "oreelse.OreInventory")["slots"]
    return [(entity(s["item"]), s["amount"]) if entity(s["item"]) else None for s in slots]


def count(app, item):
    for s in inventory(app):
        if s and s[0] == item:
            return s[1]
    return 0


def set_inventory(app, stacks):
    """Set every slot of the inventory: the stacks in order, None for a free slot."""
    slots = [{"item": s[0], "amount": s[1]} if s else {"item": 0, "amount": 0} for s in stacks]
    slots += [{"item": 0, "amount": 0}] * (SLOTS - len(slots))
    put(app, INVENTORY, INVENTORY, {"slots": slots})
    first = stacks[0]
    app.wait_for("get %s oreelse.OreInventory.slots[0].amount" % INVENTORY,
                 str(first[1]) if first else "0")


def wait_toast(app, text, frames=None):
    app.wait_for(TOAST, '"%s"' % text, frames=frames or 30, what="the toast %r" % text)


def placement(app):
    p = app.get(PLACEMENT, "oreelse.OrePlacement")
    p["selected"] = entity(p["selected"])
    return p


def selected(app):
    return placement(app)["selected"]


def rows_of(app, expr):
    """Run a query of the port: the path and the value of the first field of every result."""
    result = {}
    for row in parse_value(app.exec('query "%s"' % expr)):
        fields = row["fields"]
        result[row["entity"]] = next(iter(fields[0]["value"].values())) if fields else None
    return result


def deposits(app):
    return rows_of(app, "oreelse.OreDeposit")


def buildings(app, prefab=None):
    """The placed buildings, of one prefab when given: path -> (row, col) of the top left tile."""
    expr = "oreelse.OreBuilding" + (", (IsA, %s)" % prefab if prefab else "")
    return {path: (b["row"], b["col"]) for path, b in rows_of(app, expr).items()}


def tiles(app, prefab=None):
    return sorted(buildings(app, prefab).values())


def building_at(app, row, col, prefab=None):
    for path, tile in buildings(app, prefab).items():
        if tile == (row, col):
            return path
    return None


def connectors(app):
    """The connectors between walls: (row, col, vertical) of their edges."""
    return sorted((k["row"], k["col"], k["vertical"])
                  for k in rows_of(app, "oreelse.OreConnector").values())


def free_area(app):
    """The cases build in AREA: no deposit may be there on the map of the seed."""
    lo, hi = AREA
    for path, d in deposits(app).items():
        app.check(not (lo <= d["row"] <= hi and lo <= d["col"] <= hi),
                  "the deposit %s lies in the building area at %s" % (path, (d["row"], d["col"])))


def deposit_tile(app):
    """A tile with a deposit: (row, col)."""
    found = deposits(app)
    app.check(found, "the map has no deposits")
    d = found[sorted(found)[0]]
    return d["row"], d["col"]


def tile_point(row, col, fx=0.5, fz=0.5):
    """The ground point at fractions fx, fz inside a tile."""
    return X0 + col * TILE + (fx - 0.5) * TILE, Z0 + row * TILE + (fz - 0.5) * TILE


def hover(app):
    return app.get("oreelse.OreHover", "oreelse.OreHover")


def point(h):
    p = h["point"]
    return (p["x"], p["z"]) if isinstance(p, dict) else (p[0], p[2])


def player(app):
    return app.get(PLAYER, "box.transform.Position")


def move_cursor(app, cursor):
    app.exec("mouse_move [%g, %g]" % cursor)
    app.wait_input()
    app.wait_frames(SETTLE)


def place_player(app, x, z):
    app.exec("set %s box.transform.Position {%g, 0, %g}" % (PLAYER, x, z))
    app.wait_frames(SETTLE)


OFFSETS = {}


def offset(app, cursor):
    """The ground point a cursor position hovers, relative to the astronaut, per window size (the
    scenarios that import this one set theirs)."""
    k = (cursor, app.exec("get window box.surface.SurfaceStatus.width"),
         app.exec("get window box.surface.SurfaceStatus.height"))
    if k not in OFFSETS:
        move_cursor(app, cursor)
        p = player(app)
        x, z = point(hover(app))
        OFFSETS[k] = (x - p["x"], z - p["z"])
    return OFFSETS[k]


def aim(app, x, z, cursor=CENTER):
    """Place the astronaut so that a cursor position hovers a ground point."""
    ox, oz = offset(app, cursor)
    move_cursor(app, cursor)
    place_player(app, x - ox, z - oz)


def aim_tile(app, row, col, cursor=CENTER):
    x, z = tile_point(row, col)
    aim(app, x, z, cursor)
    h = hover(app)
    app.check(h["on_grid"] and (h["row"], h["col"]) == (row, col),
              "the cursor hovers %s, not the tile %s" % (h, (row, col)))


def button(app, name, down):
    app.exec("mouse_button %s %s" % (name, "true" if down else "false"))
    app.wait_input()


def click(app, row, col):
    """Click the left button on a tile."""
    aim_tile(app, row, col)
    app.exec("mouse_click left")
    app.wait_input()
    app.wait_frames(SETTLE)


def press(app, row, col):
    """Press the left button on a tile: the drag is anchored there."""
    aim_tile(app, row, col)
    button(app, "left", True)
    app.wait_frames(2)
    app.check(placement(app)["dragging"], "the press on %s started no drag" % ((row, col),))


def release(app, row, col, fx=0.5, fz=0.5, shift=False):
    """Move the cursor and the astronaut so that the cursor hovers a point inside a tile, then
    release the left button there, with Shift held when shift is set."""
    x, z = tile_point(row, col, fx, fz)
    ox, oz = offset(app, RELEASE)
    place_player(app, x - ox, z - oz)
    move_cursor(app, RELEASE)
    h = hover(app)
    app.check(h["on_grid"] and (h["row"], h["col"]) == (row, col),
              "the release hovers %s, not the tile %s" % (h, (row, col)))
    if shift:
        app.exec("key_down ShiftLeft")
        app.wait_input()
    button(app, "left", False)
    app.wait_frames(SETTLE)
    if shift:
        app.exec("key_up ShiftLeft")
        app.wait_input()


def drag(app, anchor, dest, fx=0.5, fz=0.5, shift=False):
    press(app, *anchor)
    release(app, dest[0], dest[1], fx, fz, shift)


def select(app, digit, expected):
    key(app, digit)
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT,
                 lambda out: entity(parse_value(out)) == expected,
                 what="the selection %s" % expected)


def keep_selected(app, digit, expected):
    """Select a building with its key unless it is selected already (the key would clear it)."""
    if selected(app) != expected:
        select(app, digit, expected)


def mine(app, row, col, down=True):
    aim_tile(app, row, col)
    button(app, "right", down)


def test_craft_building(app):
    start(app)
    set_inventory(app, [("items.StoneBrick", 6)])
    run_script(app, "craft_result { oreelse.OreStack: {item: Wall, "
                    "amount: oreelse.craft_order(Wall, 1)} }")
    app.check(app.get("craft_result", "oreelse.OreStack")["amount"] == 1,
              "the order of a wall failed")
    app.check(count(app, "items.StoneBrick") == 2,
              "the order took %d bricks, not 4" % (6 - count(app, "items.StoneBrick")))
    app.wait_for("get %s oreelse.OreInventory.slots[1].item" % INVENTORY, '"Wall"',
                 frames=round((1 + SWING_SLACK) * FPS), what="the crafted wall")
    app.check(count(app, "Wall") == 1, "the inventory holds %s" % inventory(app))


def test_click_and_rules(app):
    start(app)
    set_inventory(app, [("Wall", 3), ("Drill", 1), ("RocketPad", 2)])
    select(app, "1", "Wall")

    # The preview: a ghost of a wall on the hovered tile
    aim_tile(app, 6, 6)
    app.wait_frames(SETTLE)
    ghosts = rows_of(app, "oreelse.OreGhost, (IsA, Wall)")
    app.check(len(ghosts) == 1, "the preview shows %d wall ghosts, not 1" % len(ghosts))

    click(app, 6, 6)
    app.check(tiles(app, "Wall") == [(6, 6)], "the walls are at %s" % tiles(app, "Wall"))
    app.check(count(app, "Wall") == 2, "%d walls left, not 2" % count(app, "Wall"))
    path = building_at(app, 6, 6, "Wall")
    p = app.get(path, "box.transform.Position")
    x, z = tile_point(6, 6)
    app.check(abs(p["x"] - x) < 1e-4 and abs(p["z"] - z) < 1e-4,
              "the wall stands at %s, not at the tile center %s" % (p, (x, z)))
    health = app.get(path, "oreelse.OreHealth")
    app.check(health["value"] == 300 and health["max"] == 300, "the wall has %s" % health)

    # A tile with a building takes no other
    click(app, 6, 6)
    app.check(len(buildings(app)) == 1 and count(app, "Wall") == 2,
              "a second wall went on the tile")

    # Nothing but a drill goes on a deposit
    dr, dc = deposit_tile(app)
    click(app, dr, dc)
    app.check(building_at(app, dr, dc) is None, "a wall went on the deposit at %s" % ((dr, dc),))
    app.check(count(app, "Wall") == 2, "a wall was spent on the deposit")

    select(app, "2", "Drill")
    click(app, 8, 8)
    app.check(building_at(app, 8, 8) is None, "a drill went on empty ground")
    click(app, dr, dc)
    app.check(building_at(app, dr, dc, "Drill") is not None,
              "no drill on the deposit at %s" % ((dr, dc),))
    app.check(count(app, "Drill") == 0, "the drill was not spent")
    app.check(selected(app) is None, "the last drill placed left the selection %s" %
              selected(app))

    # One rocket pad on the map: its footprint is 3x3 from the clicked tile, centered
    select(app, "8", "RocketPad")
    click(app, 4, 9)
    pads = buildings(app, "RocketPad")
    app.check(list(pads.values()) == [(4, 9)], "the rocket pads are %s" % pads)
    p = app.get(list(pads)[0], "box.transform.Position")
    x, z = tile_point(5, 10)
    app.check(abs(p["x"] - x) < 1e-4 and abs(p["z"] - z) < 1e-4,
              "the pad stands at %s, not at the center of its footprint %s" % (p, (x, z)))
    click(app, 9, 3)
    app.check(len(buildings(app, "RocketPad")) == 1, "a second rocket pad was placed")
    app.check(count(app, "RocketPad") == 1, "the refused pad was spent")

    # The pad covers its 3x3 tiles: a wall does not go on any of them
    select(app, "1", "Wall")
    click(app, 6, 11)
    app.check(building_at(app, 6, 11) is None, "a wall went on a tile of the pad")


def test_reach_of_a_pad(app):
    start(app)
    set_inventory(app, [("RocketPad", 1), ("Wall", 1)])
    select(app, "1", "Wall")

    # Out of reach a click places nothing and warns. The press anchors the tile, then the
    # astronaut steps away before the release, which checks the reach
    press(app, 6, 6)
    x, z = tile_point(6, 6)
    place_player(app, x + REACH + 3, z)
    button(app, "left", False)
    wait_toast(app, "Too far away")
    app.check(not buildings(app) and count(app, "Wall") == 1, "a wall was placed out of reach")

    # A 3x3 pad from (4, 4): its tile centers span (4..6, 4..6). Released 14 units east of
    # its east column the nearest tile is in reach although the clicked tile and the center
    # of the pad are not
    select(app, "8", "RocketPad")
    x0, z0 = tile_point(4, 4)
    x1, z1 = tile_point(6, 6)
    press(app, 4, 4)
    place_player(app, x1 + 16, z1)
    button(app, "left", False)
    app.wait_frames(SETTLE)
    app.check(not buildings(app, "RocketPad"), "the pad was placed 16 units from its nearest tile")
    press(app, 4, 4)
    px, pz = x1 + 14, z1
    place_player(app, px, pz)
    app.check(((px - x0) ** 2 + (pz - z0) ** 2) ** 0.5 > REACH and
              ((px - (x0 + x1) / 2) ** 2 + (pz - (z0 + z1) / 2) ** 2) ** 0.5 > REACH,
              "the clicked tile or the center of the pad is in reach")
    button(app, "left", False)
    app.wait_frames(SETTLE)
    app.check(list(buildings(app, "RocketPad").values()) == [(4, 4)],
              "the pad in reach of its nearest tile was not placed: %s" % buildings(app))


def test_line_and_connectors(app):
    start(app)
    set_inventory(app, [("Wall", 10)])
    select(app, "1", "Wall")
    drag(app, (6, 6), (6, 10))
    app.check(tiles(app, "Wall") == [(6, c) for c in range(6, 11)],
              "the line of walls is %s" % tiles(app, "Wall"))
    app.check(count(app, "Wall") == 5, "%d walls left, not 5" % count(app, "Wall"))
    app.check(selected(app) == "Wall", "the selection is %s" % selected(app))

    # A wall segment between every two neighbors, on their shared edge
    app.wait_frames(SETTLE)
    links = connectors(app)
    app.check(links == [(6, c, False) for c in range(6, 10)],
              "the connectors are %s" % links)
    for path, k in rows_of(app, "oreelse.OreConnector, (IsA, WallSegment)").items():
        p = app.get(path, "box.transform.Position")
        x, z = tile_point(k["row"], k["col"])
        app.check(abs(p["x"] - (x + TILE / 2)) < 1e-4 and abs(p["z"] - z) < 1e-4,
                  "the connector %s is at %s, not on the edge" % (k, p))

    # A vertical line: the connectors turn
    drag(app, (8, 6), (9, 6))
    app.wait_frames(SETTLE)
    app.check((8, 6, True) in connectors(app), "no turned connector between (8, 6) and (9, 6): "
              "%s" % connectors(app))

    # Demolishing a wall removes its connectors
    path = building_at(app, 6, 8)
    mine(app, 6, 8)
    app.wait_for("get %s oreelse.OreInventory.slots[0].amount" % INVENTORY, "4",
                 frames=round((DOZE_TIME + SWING_SLACK) * FPS), what="the wall demolished")
    button(app, "right", False)
    app.wait_frames(SETTLE)
    links = connectors(app)
    app.check((6, 7, False) not in links and (6, 8, False) not in links,
              "the connectors of the demolished wall %s stay: %s" % (path, links))


def test_l_and_rectangle(app):
    start(app)
    set_inventory(app, [("Wall", 40)])
    select(app, "1", "Wall")

    # Released in the left part of the bottom of its tile: across <= along, the corner is
    # below the anchor, along the anchor column, then the release row
    drag(app, (3, 3), (6, 6), fx=0.2, fz=0.8)
    expected = sorted([(r, 3) for r in range(3, 7)] + [(6, c) for c in range(4, 7)])
    app.check(tiles(app, "Wall") == expected, "the L is %s, not %s" %
              (tiles(app, "Wall"), expected))

    # Released in the right part of the top of its tile: the corner is right of the anchor
    drag(app, (3, 8), (6, 11), fx=0.8, fz=0.2)
    expected += [(3, c) for c in range(8, 12)] + [(r, 11) for r in range(4, 7)]
    app.check(tiles(app, "Wall") == sorted(expected), "the second L gives %s" %
              tiles(app, "Wall"))
    left = count(app, "Wall")

    # Shift at the release fills the rectangle
    drag(app, (9, 3), (11, 6), shift=True)
    rect = [(r, c) for r in range(9, 12) for c in range(3, 7)]
    app.check(all(t in tiles(app, "Wall") for t in rect),
              "the rectangle %s is not filled: %s" % (rect, tiles(app, "Wall")))
    app.check(count(app, "Wall") == left - len(rect), "the rectangle spent %d walls" %
              (left - count(app, "Wall")))


def test_out_of_walls(app):
    start(app)
    set_inventory(app, [("Wall", 3)])
    select(app, "1", "Wall")

    # A line of 5 dragged from right to left: the first 3 in row major order are built
    drag(app, (6, 10), (6, 6))
    app.check(tiles(app, "Wall") == [(6, 6), (6, 7), (6, 8)],
              "with 3 walls the line is %s" % tiles(app, "Wall"))
    app.check(count(app, "Wall") == 0, "%d walls left" % count(app, "Wall"))
    app.check(selected(app) is None, "the selection %s stays without walls" % selected(app))


def test_cancel_drag(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    select(app, "1", "Wall")

    # ESC throws the drag away; the release places nothing
    press(app, 6, 6)
    key(app, "Escape")
    wait_toast(app, "Placement cancelled")
    app.check(not placement(app)["dragging"], "the drag is still in flight")
    release(app, 6, 9)
    app.check(not buildings(app), "the cancelled drag placed %s" % tiles(app))
    app.check(selected(app) == "Wall", "the cancel cleared the selection")
    app.check(app.exec(STATE % "Playing") == "true", "the cancel paused the game")

    # So does a press of the right button
    put(app, "oreelse.OreToast", "oreelse.OreToast", {"text": "", "warn": False, "age": 0})
    press(app, 6, 6)
    button(app, "right", True)
    wait_toast(app, "Placement cancelled")
    button(app, "right", False)
    release(app, 6, 9)
    app.check(not buildings(app), "the drag cancelled by the right button placed %s" %
              tiles(app))


def test_escape_chain(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    select(app, "1", "Wall")
    press(app, 6, 6)

    # First the drag, then the selection, then the game pauses
    key(app, "Escape")
    app.wait_for("get %s oreelse.OrePlacement.dragging" % PLACEMENT, "false",
                 what="the drag cancelled")
    app.check(selected(app) == "Wall", "the first ESC cleared the selection")
    app.check(app.exec(STATE % "Playing") == "true", "the first ESC paused")
    button(app, "left", False)

    key(app, "Escape")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT,
                 lambda out: entity(parse_value(out)) is None,
                 what="the selection cleared")
    app.check(app.exec(STATE % "Playing") == "true", "the second ESC paused")

    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")
    app.check(not buildings(app), "the ESC chain placed %s" % tiles(app))


def test_selection_keys(app):
    start(app)
    set_inventory(app, [("Wall", 2), ("GunTurret", 1), ("Factory", 1)])
    select(app, "1", "Wall")
    select(app, "1", None)

    # A key without stock does nothing
    key(app, "2")
    app.wait_frames(SETTLE)
    app.check(selected(app) is None, "the drill key selected %s without drills" % selected(app))
    select(app, "4", "GunTurret")
    key(app, "2")
    app.wait_frames(SETTLE)
    app.check(selected(app) == "GunTurret", "the drill key changed the selection to %s" %
              selected(app))
    select(app, "0", "Factory")

    # Shift and the digit select too
    app.exec("key_down ShiftLeft")
    app.wait_input()
    select(app, "1", "Wall")
    select(app, "1", None)
    app.exec("key_up ShiftLeft")
    app.wait_input()


def test_pipette(app):
    start(app)
    set_inventory(app, [("Wall", 2), ("Drill", 1)])
    select(app, "1", "Wall")
    click(app, 6, 6)
    select(app, "1", None)

    # The building under the cursor
    aim_tile(app, 6, 6)
    key(app, "Q")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT, '"Wall"',
                 what="the pipette's wall")

    # A deposit picks the drill
    dr, dc = deposit_tile(app)
    aim_tile(app, dr, dc)
    key(app, "Q")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT, '"Drill"',
                 what="the pipette's drill")

    # Empty ground clears the selection
    aim_tile(app, 9, 9)
    key(app, "Q")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT,
                 lambda out: entity(parse_value(out)) is None,
                 what="the selection cleared")

    # Without drills the pipette warns and keeps the selection
    set_inventory(app, [("Wall", 1)])
    select(app, "1", "Wall")
    aim_tile(app, dr, dc)
    key(app, "Q")
    wait_toast(app, "No Drill in inventory")
    app.check(selected(app) == "Wall", "the selection is %s, not the wall" % selected(app))


def demolish(app, row, col):
    """Hold the right button on a tile for a swing at a building, then release it."""
    mine(app, row, col)
    app.wait_frames(round((DOZE_TIME + 0.5) * FPS))
    button(app, "right", False)
    app.wait_frames(SETTLE)


def test_demolish(app):
    start(app)
    set_inventory(app, [("GunTurret", 3)])
    select(app, "4", "GunTurret")
    click(app, 6, 6)
    app.check(count(app, "GunTurret") == 2, "the turret was not spent")

    # An intact turret goes back into the inventory
    mine(app, 6, 6)
    app.wait_frames(10)
    m = app.get(PLAYER, "oreelse.OreMining")
    app.check(entity(m["target"]) == building_at(app, 6, 6) and 0 < m["progress"] < 1,
              "the swing is %s, not at the turret" % m)
    app.check(abs(m["left"] / (1 - m["progress"]) - DOZE_TIME) < 1e-3,
              "a swing at a building takes %g s, not %g" %
              (m["left"] / (1 - m["progress"]), DOZE_TIME))
    app.wait_for("get %s oreelse.OreInventory.slots[0].amount" % INVENTORY, "3",
                 frames=round((DOZE_TIME + SWING_SLACK) * FPS), what="the turret back")
    button(app, "right", False)
    app.wait_frames(SETTLE)
    app.check(not buildings(app), "the demolished turret is still there")

    # One at 10% health gives 1 iron plate and 1 wire and no electronics
    keep_selected(app, "4", "GunTurret")
    click(app, 6, 6)
    path = building_at(app, 6, 6)
    app.exec("set %s oreelse.OreHealth {value: 12, max: 120}" % path)
    demolish(app, 6, 6)
    wait_toast(app, "Salvaged 1 Iron plate, 1 Wire (10%)")
    app.check(not buildings(app), "the salvaged turret is still there")
    app.check(count(app, "items.IronPlate") == 1 and count(app, "items.Wire") == 1 and
              count(app, "items.Electronics") == 0 and count(app, "GunTurret") == 2,
              "the salvage gave %s" % inventory(app))

    # A salvage whose new kinds do not fit is refused and the turret stays
    full = [("GunTurret", 2), ("items.Iron", 1), ("items.Copper", 1), ("items.Stone", 1),
            ("items.Luminite", 1), ("items.CopperPlate", 1), ("items.StoneBrick", 1),
            ("items.RocketEngine", 1), ("items.RocketCargoBay", 1),
            ("items.RocketLifeSupport", 1), ("items.RocketFuelStorage", 1), ("Wall", 1),
            ("Drill", 1), ("SolarPanel", 1), ("Factory", 1)]
    set_inventory(app, full)
    keep_selected(app, "4", "GunTurret")
    click(app, 6, 6)
    path = building_at(app, 6, 6)
    app.check(path is not None, "no turret placed with a full inventory")
    app.exec("set %s oreelse.OreHealth {value: 60, max: 120}" % path)
    mine(app, 6, 6)
    wait_toast(app, "Inventory full - can't salvage",
               frames=round((DOZE_TIME + SWING_SLACK) * FPS))
    button(app, "right", False)
    app.wait_frames(SETTLE)
    app.check(building_at(app, 6, 6) == path, "the refused salvage removed the turret")
    app.check(count(app, "items.IronPlate") == 0, "the refused salvage gave %s" % inventory(app))

    # An intact building of a new kind does not fit a full inventory either
    app.exec("set %s oreelse.OreHealth {value: 120, max: 120}" % path)
    set_inventory(app, [(k, 1) for k, _ in full[1:]] + [("items.IronPlate", 1)])
    demolish(app, 6, 6)
    wait_toast(app, FULL)
    app.check(building_at(app, 6, 6) == path, "the turret was demolished into a full inventory")


def test_destroyed(app):
    start(app)
    set_inventory(app, [("Wall", 1)])
    select(app, "1", "Wall")
    click(app, 6, 6)
    path = building_at(app, 6, 6)
    app.exec("set %s oreelse.OreHealth {value: 0, max: 300}" % path)
    app.wait_frames(SETTLE)
    app.check(not buildings(app), "the wall at 0 health stands")
    app.check(count(app, "Wall") == 0, "the destroyed wall went into the inventory")


def test_jetpack(app):
    start(app)
    set_inventory(app, [("Wall", 1)])
    select(app, "1", "Wall")
    click(app, 6, 6)
    x, z = tile_point(6, 6)
    place_player(app, x, z)
    app.wait_for("get %s box.transform.Position.y" % PLAYER,
                 lambda out: abs(float(out) - HOVER_HEIGHT) < 1e-4,
                 frames=FPS, what="the astronaut hovering at %g" % HOVER_HEIGHT)
    app.check(app.get(PLAYER, "oreelse.OrePlayer")["airborne"], "the astronaut is not airborne")

    # Next to the wall it comes down
    x, z = tile_point(6, 7)
    app.exec("set %s box.transform.Position {%g, %g, %g}" % (PLAYER, x, HOVER_HEIGHT, z))
    app.wait_frames(10)
    y = float(app.exec("get %s box.transform.Position.y" % PLAYER))
    app.check(0 < y < HOVER_HEIGHT, "after 10 frames next to the wall the height is %g" % y)
    app.wait_for("get %s box.transform.Position.y" % PLAYER, lambda out: float(out) == 0,
                 frames=FPS, what="the astronaut on the ground")
    app.check(not app.get(PLAYER, "oreelse.OrePlayer")["airborne"], "the astronaut is airborne")
