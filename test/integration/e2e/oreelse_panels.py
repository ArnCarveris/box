"""Ore Else driven through its command port: the panels of play. A click on a header row of the
order queue cancels one unit and a click on an indented row does nothing; the bars of the power
allocator follow the rules of oreelse.power_set() and the FREE readout follows them; the plus
and minus steps of the rocket assembly install and uninstall a part; the Luminite load section
and the LAUNCH button work by click and by key; the rocket panel is on top where the queue grows
under it; the panels hide while paused; and a click on a panel does not place the selected
building on the tile under it.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup, but in a 640x480 window: the rocket panel
reaches 440 pixels down. The camera's UI shows one point per pixel, so the cases click the
panels at the points of their layout, measured from the corners of the window as the HUD
anchors them (ui/hud.flecs). They set the inventory and the orders directly and pause the
astronaut's crafting, so the order book holds still.
"""
from box_client import parse_value
from oreelse_buildings import (  # noqa: F401 (ARGS)
    ARGS, OFFSETS, PLAYER, SETTLE, STATE, TOAST, aim_tile, count, free_area, key,
    move_cursor, run_script, select, set_inventory, set_state, setup, tiles)
from oreelse_hud import color, shown, text, wait_shown
from oreelse_rocket import (
    CARGO, ENGINE, FUEL, LIFE, LUMINITE, assemble, parts, place_pad, press, rocket, wait_rocket)

APP = "apps/oreelse"

WIDTH = 640
HEIGHT = 480

BOOK = "oreelse.OreCraftBook"
ALLOC = "oreelse.OrePowerAlloc"

NO_BARS = "Only 6 power bars - free one somewhere else"

IDLE = ("Idle", "OreLaunchPhaseIdle")

SCREEN = "hud.ui.screen"
QUEUE = SCREEN + ".queue_panel"
POWER = SCREEN + ".power_panel"
FREE = POWER + ".free"
ROCKET_PANEL = SCREEN + ".rocket_panel"
LOAD_COUNT = ROCKET_PANEL + ".load_section.count_label"
NOTE = ROCKET_PANEL + ".launch_button.note_label"

# The power panel: 264x158, 18 from the left and the bottom; its columns and the y of the bars
# of levels 4 to 1, 52x11 at 11 into the column
POWER_LEFT = 18
POWER_TOP = HEIGHT - 18 - 158
COLUMNS = [12, 95, 178]
COLUMN_NAMES = ["mining", "weapons", "construction"]
BAR_Y = {4: 38, 3: 54, 2: 70, 1: 86}

# The rocket panel: 272x366, 18 from the right and 74 from the top; its sections 50 apart from
# y 44, with the minus steps at x 176 and the plus steps at 220, 40x30 at 8 into the row
ROCKET_LEFT = WIDTH - 18 - 272
ROCKET_TOP = 74
SECTIONS = {"life": 0, "cargo": 1, "fuel": 2, "engine": 3}
LOAD_POINT = (ROCKET_LEFT + 12 + 124, ROCKET_TOP + 254 + 20)
LAUNCH_POINT = (ROCKET_LEFT + 12 + 124, ROCKET_TOP + 306 + 24)

# The queue panel: 252 wide, 18 from the right and 40 from the bottom, 14 + rows * 28 high;
# row i at (12, 11 + i * 28), 228x26
QUEUE_LEFT = WIDTH - 18 - 252
QUEUE_BOTTOM = HEIGHT - 40

# Points on the plates of the panels, away from their buttons: between the title and the bars
# of the power panel, between the title and the first section of the rocket panel
ON_POWER = (POWER_LEFT + 150, POWER_TOP + 30)
ON_ROCKET = (ROCKET_LEFT + 136, ROCKET_TOP + 38)

# Colors of the panels
ROW_HOVER = (52, 56, 66)
STEP_FACE = (46, 50, 58)
STEP_DIM = (30, 33, 39)
LAUNCH_FACE = (78, 62, 26)
CYAN = (92, 236, 222)
ORANGE = (240, 152, 72)
OFF_HOVER = (34, 37, 44)


def start(app):
    """Set up in a 640x480 window, leave the title screen and pause the astronaut's crafting."""
    setup(app)
    app.exec("set window Surface {width: %d, height: %d}" % (WIDTH, HEIGHT))
    app.wait_for("get window SurfaceStatus.height", str(HEIGHT))
    OFFSETS.clear()
    app.expect(STATE % "Title", "true")
    key(app, "Space")
    app.wait_for(STATE % "Playing", "true")
    free_area(app)
    app.exec("set %s OreCrafter {speed: 1, paused: true}" % PLAYER)


def click_at(app, point):
    """Click the left button at a point of the window."""
    move_cursor(app, point)
    app.exec("mouse.click left")
    app.wait_input()
    app.wait_frames(SETTLE)


def bar_point(kind, level):
    return (POWER_LEFT + COLUMNS[kind] + 11 + 26, POWER_TOP + BAR_Y[level] + 5.5)


def step_point(part, plus):
    k = SECTIONS[part]
    return (ROCKET_LEFT + (220 if plus else 176) + 20, ROCKET_TOP + 44 + 50 * k + 8 + 15)


def row_point(i, rows):
    top = QUEUE_BOTTOM - (14 + rows * 28)
    return (QUEUE_LEFT + 12 + 114, top + 11 + 28 * i + 13)


def order(app, item, units):
    """Order units of an item with oreelse.craft_order()."""
    run_script(app, "const ordered: i32 = oreelse.craft_order(%s, %d)" % (item, units))


def book(app):
    return app.get(BOOK, "OreCraftBook")


def demands(app):
    """The orders oldest first: (id, item, demand)."""
    b = book(app)
    return [(o["id"], o["item"], o["demand"]) for o in b["orders"][:b["count"]]]


def row_count(app):
    return book(app)["row_count"]


def row_label(order_id, name):
    return "%s.row_%d_%s.name_label" % (QUEUE, order_id, name)


def levels(app):
    return app.get(ALLOC, "OrePowerAlloc")["level"]


def wait_levels(app, expected):
    app.wait_for("get %s OrePowerAlloc" % ALLOC,
                 lambda out: parse_value(out)["level"] == expected,
                 what="the levels %s" % expected)


def test_queue(app):
    start(app)
    set_inventory(app, [("items.Iron", 32), ("items.Copper", 16)])
    order(app, ENGINE, 2)
    found = demands(app)
    app.check(len(found) == 1 and found[0][1:] == (ENGINE, 2), "the book holds %s" % found)
    oid = found[0][0]

    # A header row and the indented rows of the plates, oldest part first
    rows = row_count(app)
    app.check(rows == 3, "the queue has %d rows, not 3" % rows)
    wait_shown(app, row_label(oid, "RocketEngine"), "Rocket engine (2x)")
    wait_shown(app, row_label(oid, "IronPlate"), "Iron plate (16x)")
    wait_shown(app, row_label(oid, "CopperPlate"), "Copper plate (8x)")

    # A click on an indented row does nothing
    click_at(app, row_point(1, rows))
    app.check(demands(app) == [(oid, ENGINE, 2)], "the click on an indented row gave %s" %
              demands(app))

    # The header row lights up under the cursor; a click takes one unit off the order
    move_cursor(app, row_point(0, rows))
    app.wait_frames(SETTLE)
    header = "%s.row_%d_RocketEngine" % (QUEUE, oid)
    app.check(color(app, header) == ROW_HOVER, "the hovered header is %s" %
              (color(app, header),))
    click_at(app, row_point(0, rows))
    app.wait_for("get %s OreCraftBook.orders[0].demand" % BOOK, "1",
                 what="one unit cancelled")
    wait_shown(app, row_label(oid, "RocketEngine"), "Rocket engine (1x)")
    app.check(count(app, "items.Iron") == 16 and count(app, "items.Copper") == 8,
              "the cancel refunded %d iron and %d copper" %
              (count(app, "items.Iron"), count(app, "items.Copper")))

    # The header, which the click changed, goes dark when the cursor leaves it: no face while
    # no worker is on it
    move_cursor(app, row_point(1, row_count(app)))
    app.wait_frames(SETTLE)
    faces = [line for line in app.exec("get %s" % header).splitlines()
             if line.startswith("box.material.Rgb")]
    app.check(faces == [], "the header keeps a face after the cursor left it: %s" % faces)

    # The last unit: the order is gone and the queue with it
    click_at(app, row_point(0, row_count(app)))
    app.wait_for("get %s OreCraftBook.count" % BOOK, "0", what="the order cancelled")
    app.wait_for("get %s OreCraftBook.row_count" % BOOK, "0", what="no rows")
    app.wait_frames(SETTLE)
    app.check(not shown(app, QUEUE), "the queue shows without orders")


def test_queue_hover_while_crafting(app):
    start(app)
    set_inventory(app, [("items.Iron", 32), ("items.Copper", 16)])
    order(app, ENGINE, 2)
    oid = demands(app)[0][0]
    header = "%s.row_%d_RocketEngine" % (QUEUE, oid)
    move_cursor(app, row_point(0, row_count(app)))
    app.wait_frames(SETTLE)
    app.check(color(app, header) == ROW_HOVER, "the hovered header is %s" %
              (color(app, header),))

    # A slow craft changes the book every frame: the header stays lit under the cursor
    app.exec("set %s OreCrafter {speed: 0.1, paused: false}" % PLAYER)
    app.wait_for("get %s OreCraftBook.rows[1].progress" % BOOK, lambda out: float(out) > 0.05,
                 what="the craft under way")
    for _ in range(3):
        app.wait_frames(2)
        app.check(color(app, header) == ROW_HOVER, "the header is %s while the book changes" %
                  (color(app, header),))


def test_power(app):
    start(app)
    app.check(levels(app) == [2, 2, 2], "a run starts at the levels %s" % levels(app))
    wait_shown(app, FREE, "FREE 0")

    # Mining to 4 with no free bar: the warning, nothing changes
    click_at(app, bar_point(0, 4))
    app.wait_for(TOAST, '"%s"' % NO_BARS, what="the toast %r" % NO_BARS)
    app.check(levels(app) == [2, 2, 2], "the refused click left %s" % levels(app))

    # The top lit bar steps Mining down; a lower bar sets Weapons to it
    click_at(app, bar_point(0, 2))
    wait_levels(app, [1, 2, 2])
    wait_shown(app, FREE, "FREE 1")
    click_at(app, bar_point(1, 1))
    wait_levels(app, [1, 1, 2])
    wait_shown(app, FREE, "FREE 2")

    # Construction up to 4 takes the two free bars
    click_at(app, bar_point(2, 4))
    wait_levels(app, [1, 1, 4])
    wait_shown(app, FREE, "FREE 0")

    # The single lit bar of Mining switches it off
    click_at(app, bar_point(0, 1))
    wait_levels(app, [0, 1, 4])
    wait_shown(app, FREE, "FREE 1")

    # The lit bars take the color of their column's level; the hovered unlit bar is lighter
    app.wait_frames(SETTLE)
    for level in (1, 2, 3, 4):
        path = "%s.construction.bar_%d" % (POWER, level)
        app.check(color(app, path) == CYAN, "%s is %s at level 4" % (path, color(app, path)))
    path = POWER + ".weapons.bar_1"
    app.check(color(app, path) == ORANGE, "%s is %s at level 1" % (path, color(app, path)))
    path = POWER + ".mining.bar_1"
    app.check(color(app, path) == OFF_HOVER, "the hovered unlit %s is %s" %
              (path, color(app, path)))


def test_steps(app):
    start(app)
    place_pad(app, [(ENGINE, 5), (FUEL, 2), (CARGO, 2)])
    app.wait_frames(SETTLE)
    app.check(shown(app, ROCKET_PANEL), "the rocket panel does not show with a pad")
    wait_shown(app, NOTE, "FIT A CARGO BAY OR LIFE SUPPORT")

    # Without a unit only the cargo bay goes on; no life support is held
    life_plus = ROCKET_PANEL + ".life_plus"
    cargo_plus = ROCKET_PANEL + ".cargo_plus"
    app.check(color(app, life_plus) == STEP_DIM, "the life support's plus step is %s" %
              (color(app, life_plus),))
    app.check(color(app, cargo_plus) == STEP_FACE, "the cargo bay's plus step is %s" %
              (color(app, cargo_plus),))

    # Plus installs one
    click_at(app, step_point("cargo", True))
    wait_rocket(app, lambda r: parts(r) == (0, 0, 1, 0), "a cargo bay")
    wait_shown(app, NOTE, "NEEDS 4 MORE ENGINES")
    for _ in range(4):
        click_at(app, step_point("engine", True))
    wait_rocket(app, lambda r: parts(r) == (4, 0, 1, 0), "4 engines")

    # The cargo bay's plus step, clicked and left for the engines' one, is not lit any more
    app.check(color(app, cargo_plus) == STEP_FACE, "the cargo bay's plus step is %s after the "
              "cursor left it" % (color(app, cargo_plus),))
    wait_shown(app, NOTE, "NEEDS 1 MORE FUEL TANKS")
    click_at(app, step_point("fuel", True))
    r = wait_rocket(app, lambda r: r["valid"], "a valid rocket")
    app.check(parts(r) == (4, 1, 1, 0) and count(app, ENGINE) == 1 and count(app, FUEL) == 1 and
              count(app, CARGO) == 1, "the rocket is %s with %d engines, %d fuel and %d cargo "
              "bays left" % (r, count(app, ENGINE), count(app, FUEL), count(app, CARGO)))

    # A fifth engine does not fit one unit
    click_at(app, step_point("engine", True))
    app.wait_frames(SETTLE)
    app.check(parts(rocket(app)) == (4, 1, 1, 0), "a fifth engine went on: %s" % rocket(app))

    # Minus uninstalls one into the inventory
    click_at(app, step_point("engine", False))
    wait_rocket(app, lambda r: parts(r) == (3, 1, 1, 0), "3 engines")
    app.check(count(app, ENGINE) == 2, "the engine did not come back: %d" % count(app, ENGINE))
    click_at(app, step_point("fuel", False))
    wait_rocket(app, lambda r: parts(r) == (3, 0, 1, 0), "no fuel tank")
    app.check(count(app, FUEL) == 2, "the fuel tank did not come back: %d" % count(app, FUEL))

    # The cargo bay's minus takes the engines above the limit of no unit back with it
    click_at(app, step_point("cargo", False))
    wait_rocket(app, lambda r: parts(r) == (0, 0, 0, 0), "an empty rocket")
    app.check(count(app, ENGINE) == 5 and count(app, CARGO) == 2,
              "after the cargo bay's minus the inventory holds %d engines and %d cargo bays" %
              (count(app, ENGINE), count(app, CARGO)))

    # A minus with nothing installed does nothing
    click_at(app, step_point("life", False))
    app.wait_frames(SETTLE)
    app.check(parts(rocket(app)) == (0, 0, 0, 0), "the life support's minus gave %s" %
              rocket(app))


def test_load_and_launch(app):
    start(app)
    place_pad(app, [(ENGINE, 4), (FUEL, 1), (CARGO, 1), (LUMINITE, 10)])

    # LAUNCH does nothing while the rocket is not valid
    click_at(app, LAUNCH_POINT)
    app.check(rocket(app)["phase"] in IDLE, "an empty rocket launched: %s" % rocket(app))
    assemble(app)

    # The load key loads what is held, a click on the load section the rest of the capacity
    press(app, ["O"])
    wait_rocket(app, lambda r: r["loaded"] == 10, "10 Luminite loaded")
    wait_shown(app, LOAD_COUNT, "10 / 25")
    set_inventory(app, [(ENGINE, 1), (LUMINITE, 30)])
    wait_shown(app, NOTE, "25 LUMINITE ABOARD")
    click_at(app, LOAD_POINT)
    wait_rocket(app, lambda r: r["loaded"] == 25, "25 Luminite loaded")
    wait_shown(app, LOAD_COUNT, "25 / 25")
    app.check(count(app, LUMINITE) == 15, "the load left %d Luminite" % count(app, LUMINITE))

    # A click on LAUNCH launches the valid rocket
    launch = ROCKET_PANEL + ".launch_button"
    app.check(color(app, launch) == LAUNCH_FACE, "the LAUNCH button is %s" %
              (color(app, launch),))
    click_at(app, LAUNCH_POINT)
    wait_rocket(app, lambda r: r["phase"] not in IDLE, "the launch")
    wait_shown(app, NOTE, "LIFTOFF")


def test_launch_key(app):
    start(app)
    place_pad(app, [(ENGINE, 4), (FUEL, 1), (LIFE, 1)])
    assemble(app, life=True)
    wait_shown(app, NOTE, "CREW + 0 LUMINITE ABOARD")
    press(app, ["P"])
    wait_rocket(app, lambda r: r["phase"] not in IDLE, "the launch")
    wait_shown(app, NOTE, "LIFTOFF")


def test_rocket_over_queue(app):
    start(app)
    place_pad(app, [(CARGO, 1), ("items.Iron", 16), ("items.Copper", 8), (LUMINITE, 10)])
    press(app, ["U"])
    wait_rocket(app, lambda r: r["cargo"] == 1, "a cargo bay")
    order(app, ENGINE, 1)
    rows = row_count(app)
    app.check(rows == 3, "the queue has %d rows, not 3" % rows)

    # The header row (y 353 to 379) reaches up under the load section (y 328 to 368): the
    # click on both loads and cancels nothing
    click_at(app, (row_point(0, rows)[0], 360))
    wait_rocket(app, lambda r: r["loaded"] == 10, "the Luminite loaded")
    app.check(demands(app)[0][2] == 1, "the click under the rocket panel cancelled the order: "
              "%s" % demands(app))


def test_hidden_while_paused(app):
    start(app)
    place_pad(app, [("items.Iron", 16), ("items.Copper", 8)])
    order(app, ENGINE, 1)
    app.wait_frames(SETTLE)
    panels = [POWER, QUEUE, ROCKET_PANEL]
    for path in panels:
        app.check(shown(app, path), "%s does not show while playing" % path)

    set_state(app, "Paused")
    app.wait_frames(SETTLE)
    for path in panels:
        app.check(not shown(app, path), "%s shows while paused" % path)
    app.check(text(app, FREE) is None, "the power panel's readout shows while paused")

    set_state(app, "Playing")
    app.wait_frames(SETTLE)
    for path in panels:
        app.check(shown(app, path), "%s did not come back with play" % path)


def test_panel_blocks(app):
    start(app)
    place_pad(app, [("Wall", 5)])
    select(app, "1", "Wall")

    # A click on the plate of a panel does not reach the world: no wall on the tile under it,
    # no warning, no wall spent
    for cursor, tile in ((ON_POWER, (10, 5)), (ON_ROCKET, (5, 5))):
        aim_tile(app, tile[0], tile[1], cursor)
        app.exec("mouse.click left")
        app.wait_input()
        app.wait_frames(SETTLE)
        app.check(tiles(app, "Wall") == [], "the click on the panel at %s placed %s" %
                  (cursor, tiles(app, "Wall")))
        app.expect(TOAST, '""')
        app.check(count(app, "Wall") == 5, "the click on the panel at %s spent a wall" %
                  (cursor,))
    app.check(levels(app) == [2, 2, 2], "the clicks changed the power to %s" % levels(app))
    app.check(parts(rocket(app)) == (0, 0, 0, 0), "the clicks changed the rocket: %s" %
              rocket(app))
