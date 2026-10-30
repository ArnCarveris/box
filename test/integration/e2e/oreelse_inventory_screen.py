"""Ore Else driven through its command port: the inventory screen (E). E and the Inventory (E)
button open and close it, ESC closes it before it clears the selection, Tab flips its tabs only
while it is open, a click on a building in a stock slot selects it and closes the screen, a
click on a slot button orders one (five with Shift) and a dim one nothing, the world takes no
mining, placing or pipette while it is open, the craft keys work with it closed, and a restart
from an open screen leaves it closed with E opening it at the first press.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup. The window then grows to the 1280x720 points
the original lays the screen out for, rendered at half resolution (640x360 pixels), so the screen
lies inside it and clear of the status bar. A case finds the window point of an element from its
WorldTransform (the HUD's camera shows one point per unit, centered on the window) and checks
with PickStatus that the element handles the pointer there before it clicks.
"""
from box_client import parse_value
from oreelse_buildings import (  # noqa: F401 (ARGS)
    ARGS, PLACEMENT, PLAYER, SETTLE, STATE, aim_tile, button, count, deposit_tile, entity,
    inventory, key, placement, rows_of, selected, set_inventory, set_state, tiles, wait_toast)
from oreelse_buildings import start as start_game

APP = "apps/oreelse"

SCREEN_STATE = "oreelse.OreInventoryScreen"
INFO = "oreelse.OreItemInfo"
BOOK = "oreelse.OreCraftBook"
PICK = "get box.picking.PickStatus box.picking.PickStatus."

# The window of the cases, in points, and the pixels a point renders at
WIDTH = 1280
HEIGHT = 720
RESOLUTION_SCALE = 2

# The elements of the screen (ui/inventory.flecs) under the HUD's screen
UI = "hud.ui.screen"
SCREEN = UI + ".inventory"
GRID = SCREEN + ".stock_section.body.stock_body.grid"
TABS = SCREEN + ".construct_section.body.tabs"
PAGES = SCREEN + ".construct_section.body.pages"
BUILDINGS = PAGES + ".buildings_page"
ROCKET = PAGES + ".rocket_page"
INFO_PANEL = UI + ".info_panel"
BUTTON = UI + ".inventory_button"

# A point of the window below the screen, left of the info panel and the Inventory (E) button
# and right of the power panel, where a click reaches the world (the screen spans y 116..484,
# the info panel x 360..920, the button x 542..738 and the power panel x 18..282, y 544..702)
OFF_SCREEN = (320, 660)

# The face of a slot button that cannot order, the original's bytes: the UI keeps them, decoded
# as sRGB (apps/oreelse/README.md, Colors)
DIM_FACE = (30, 33, 39)

# A corner of the map without deposits (oreelse_buildings.AREA)
TILE = (6, 6)


def start(app):
    """Set up, leave the title screen and grow the window to the points of the original."""
    start_game(app)
    app.exec("set window box.surface.Surface {width: %d, height: %d, resolution_scale: %g}" %
             (WIDTH, HEIGHT, RESOLUTION_SCALE))
    app.wait_for("get window box.surface.SurfaceStatus.point_height", str(HEIGHT))
    app.wait_frames(SETTLE)


def press(app, row, col):
    """Press the left button on a tile, aimed from OFF_SCREEN: the drag is anchored there."""
    aim_tile(app, row, col, OFF_SCREEN)
    button(app, "left", True)
    app.wait_frames(2)
    app.check(placement(app)["dragging"], "the press on %s started no drag" % ((row, col),))


def shown(app, path):
    """Whether an element of the screen is shown: it exists, with its Layout."""
    return path in rows_of(app, "box.layout.Layout")


def is_open(app):
    return app.get(SCREEN_STATE, "oreelse.OreInventoryScreen")["open"]


def tab(app):
    return app.get(SCREEN_STATE, "oreelse.OreInventoryScreen")["tab"]


def wait_open(app, value):
    app.wait_for("get %s oreelse.OreInventoryScreen.open" % SCREEN_STATE,
                 "true" if value else "false",
                 what="the screen %s" % ("open" if value else "closed"))
    app.wait_frames(SETTLE)


def wait_tab(app, value):
    app.wait_for("get %s oreelse.OreInventoryScreen.tab" % SCREEN_STATE, str(value),
                 what="the tab %d" % value)
    app.wait_frames(SETTLE)


def flat(value):
    """The numbers of a nested list, in order."""
    if isinstance(value, dict):
        value = list(value.values())
    if isinstance(value, (list, tuple)):
        return [n for v in value for n in flat(v)]
    return [value]


def point(app, path):
    """The window point of the origin of an element of the HUD: its WorldTransform translation,
    y up from the center of the window."""
    m = flat(app.get(path, "box.transform.WorldTransform"))
    app.check(len(m) == 16, "the WorldTransform of %s is %s" % (path, m))
    x, y = WIDTH / 2 + m[12], HEIGHT / 2 - m[13]
    app.check(0 <= x < WIDTH and 0 <= y < HEIGHT,
              "%s is at %s, outside the %dx%d window" % (path, (x, y), WIDTH, HEIGHT))
    return x, y


def hover(app, path):
    """Move the cursor onto an element and wait until it handles the pointer there."""
    p = point(app, path)
    app.exec("mouse_move [%g, %g]" % p)
    app.wait_input()
    app.wait_for(PICK + "handler", '"%s"' % path, what="%s under the cursor" % path)
    return p


def click(app, path, shift=False):
    """Click an element of the HUD, with Shift held when shift is set."""
    p = hover(app, path)
    if shift:
        app.exec("key_down ShiftLeft")
        app.wait_input()
    app.exec("mouse_click left [%g, %g]" % p)
    app.wait_input()
    app.wait_frames(SETTLE)
    if shift:
        app.exec("key_up ShiftLeft")
        app.wait_input()


def orders(app):
    book = app.get(BOOK, "oreelse.OreCraftBook")
    return [(o["item"], o["demand"]) for o in book["orders"][:book["count"]]]


def ordered(app):
    """The orders taken in the run (next_id, the id of the last one) and the last one: its item
    and demand, None once it is done."""
    book = app.get(BOOK, "oreelse.OreCraftBook")
    last = [(o["item"], o["demand"]) for o in book["orders"][:book["count"]]
            if o["id"] == book["next_id"]]
    return book["next_id"], last[0] if last else None


def wait_ordered(app, n, what):
    app.wait_for("get %s oreelse.OreCraftBook.next_id" % BOOK, str(n), what=what)


def can_order(app, item):
    return app.get(item, "oreelse.OreCanOrder")["value"]


def lit(app, name):
    """Whether a slot button of the Buildings page shows its item can be ordered: its face is
    the lit one."""
    c = app.get("%s.row_base.%s" % (BUILDINGS, name), "box.material.Rgb")
    return (c["r"], c["g"], c["b"]) != DIM_FACE


def test_open_close(app):
    start(app)
    app.check(not is_open(app), "the screen is open at the start")
    app.check(not shown(app, SCREEN), "the screen shows at the start")

    # E opens it and closes it again; the game keeps running
    key(app, "E")
    wait_open(app, True)
    app.check(shown(app, SCREEN), "the open screen does not show")
    app.expect(STATE % "Playing", "true")
    key(app, "E")
    wait_open(app, False)

    # So does the Inventory (E) button
    click(app, BUTTON)
    wait_open(app, True)
    click(app, BUTTON)
    wait_open(app, False)

    # Hidden while paused, E does nothing then; open again once the game resumes
    key(app, "E")
    wait_open(app, True)
    set_state(app, "Paused")
    app.wait_frames(SETTLE)
    app.check(not shown(app, SCREEN), "the screen shows while paused")
    key(app, "E")
    app.wait_frames(SETTLE)
    app.check(is_open(app), "E changed the screen while paused")
    set_state(app, "Playing")
    app.wait_frames(SETTLE)
    app.check(shown(app, SCREEN), "the screen did not come back")


def test_escape_and_drag(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    key(app, "1")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT, '"Wall"',
                 what="the wall selected")

    # Opening throws the drag in flight away; the release places nothing
    press(app, *TILE)
    key(app, "E")
    wait_open(app, True)
    wait_toast(app, "Placement cancelled")
    app.check(not placement(app)["dragging"], "the drag is still in flight")
    button(app, "left", False)
    app.wait_frames(SETTLE)
    app.check(tiles(app, "Wall") == [], "the thrown away drag placed %s" % tiles(app, "Wall"))

    # ESC closes the screen before it clears the selection, then pauses
    key(app, "Escape")
    wait_open(app, False)
    app.check(selected(app) == "Wall", "the ESC that closed the screen cleared the selection")
    key(app, "Escape")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT,
                 lambda out: entity(parse_value(out)) is None,
                 what="the selection cleared")
    app.expect(STATE % "Playing", "true")
    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")


def test_tabs(app):
    start(app)

    # Tab does nothing while the screen is closed
    key(app, "Tab")
    app.wait_frames(SETTLE)
    app.check(tab(app) == 0, "Tab flipped the tab of the closed screen")

    # Open, Tab flips between the pages
    key(app, "E")
    wait_open(app, True)
    app.check(shown(app, BUILDINGS), "no Buildings page")
    key(app, "Tab")
    wait_tab(app, 1)
    app.check(shown(app, ROCKET), "no Rocket page on the Rocket tab")
    app.check(not shown(app, BUILDINGS),
              "the Buildings page shows on the Rocket tab")
    key(app, "Tab")
    wait_tab(app, 0)

    # A click on a tab button selects its tab, the active one again changes nothing
    click(app, TABS + ".rocket_tab")
    wait_tab(app, 1)
    click(app, TABS + ".rocket_tab")
    app.check(tab(app) == 1, "a click on the active tab changed it")
    click(app, TABS + ".buildings_tab")
    wait_tab(app, 0)
    app.check(is_open(app), "a click on a tab closed the screen")

    # Closed again, Tab does nothing; the tab stays for the next opening
    key(app, "Tab")
    wait_tab(app, 1)
    key(app, "E")
    wait_open(app, False)
    key(app, "Tab")
    app.wait_frames(SETTLE)
    app.check(tab(app) == 1, "Tab flipped the tab of the closed screen")


def test_pick(app):
    start(app)
    set_inventory(app, [None, ("items.IronPlate", 3), ("Wall", 5)])
    key(app, "E")
    wait_open(app, True)

    # Over a building in a stock slot the info panel shows it ready to place
    hover(app, GRID + ".slot_2")
    app.wait_for("get %s oreelse.OreItemInfo.item" % INFO, '"Wall"',
                 what="the wall under the pointer")
    app.check(app.get(INFO, "oreelse.OreItemInfo")["place"],
              "the wall of the slot is not to place")
    app.wait_for("get %s.note box.text.Text.value" % INFO_PANEL, '"Ready to place"',
                 what="the note of the wall")

    # A click on it selects it and closes the screen
    click(app, GRID + ".slot_2")
    wait_open(app, False)
    app.check(selected(app) == "Wall", "the click on the wall selected %s" % selected(app))

    # The selected one again clears the selection
    key(app, "E")
    wait_open(app, True)
    click(app, GRID + ".slot_2")
    wait_open(app, False)
    app.check(selected(app) is None, "the click on the selected wall left %s" % selected(app))

    # Any other item does nothing
    key(app, "E")
    wait_open(app, True)
    click(app, GRID + ".slot_1")
    app.wait_frames(SETTLE)
    app.check(is_open(app), "the click on the iron plates closed the screen")
    app.check(selected(app) is None, "the click on the iron plates selected %s" % selected(app))

    # The digit key of a building selects it and closes the screen too
    key(app, "1")
    wait_open(app, False)
    app.check(selected(app) == "Wall", "the key 1 selected %s" % selected(app))


def test_order(app):
    start(app)
    set_inventory(app, [("items.Stone", 1)])
    key(app, "E")
    wait_open(app, True)
    app.check(not lit(app, "drill"), "the drill button is lit without iron plates")

    # The buttons follow what can be ordered while the screen is open
    set_inventory(app, [("items.IronPlate", 100), ("items.CopperPlate", 100)])
    app.wait_for("get Drill oreelse.OreCanOrder.value", "true", what="a drill to order")
    app.wait_for("get %s.row_base.drill SlotButton.can" % BUILDINGS, "true",
                 what="the drill button lit")
    app.check(not can_order(app, "Wall"), "a wall can be ordered without stone")
    app.check(not lit(app, "wall"), "the wall button is lit without stone")

    # Over a slot button the info panel shows the recipe and the needs
    hover(app, BUILDINGS + ".row_base.drill")
    app.wait_for("get %s oreelse.OreItemInfo.item" % INFO, '"Drill"',
                 what="the drill under the pointer")
    app.wait_for("get %s.note box.text.Text.value" % INFO_PANEL, '"Craft 1.0s - 4 W"',
                 what="the note of the drill")
    app.wait_for("get %s.needs_list.need_0.amount box.text.Text.value" % INFO_PANEL, '"12/12"',
                 what="the iron the drill needs")

    # A click orders one, the screen stays open (a drill takes a second to craft, so the order
    # may be done by the time the case looks)
    click(app, BUILDINGS + ".row_base.drill")
    wait_ordered(app, 1, "the order of a drill")
    last = ordered(app)[1]
    app.check(last in (("Drill", 1), None), "the click ordered %s" % (last,))
    app.check(count(app, "items.IronPlate") == 94, "the drill took %s" % inventory(app))
    app.check(is_open(app), "the order closed the screen")

    # With Shift five
    click(app, BUILDINGS + ".row_base.drill", shift=True)
    wait_ordered(app, 2, "the order of five drills")
    app.check(ordered(app)[1] == ("Drill", 5), "the click with Shift ordered %s" %
              (ordered(app)[1],))
    app.check(count(app, "items.IronPlate") == 64, "six drills took %s" % inventory(app))

    # A dim button orders nothing
    click(app, BUILDINGS + ".row_base.wall")
    app.wait_frames(SETTLE)
    app.check(ordered(app)[0] == 2, "the dim wall ordered %s" % orders(app))

    # The rocket page orders its parts the same way
    key(app, "Tab")
    wait_tab(app, 1)
    click(app, ROCKET + ".row_rocket.engine")
    wait_ordered(app, 3, "the order of an engine")
    app.check(ordered(app)[1] == ("items.RocketEngine", 1), "the engine button ordered %s" %
              (ordered(app)[1],))
    app.check(tiles(app) == [], "the clicks on the screen placed %s" % tiles(app))


def test_world_blocked(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    key(app, "1")
    app.wait_for("get %s oreelse.OrePlacement.selected" % PLACEMENT, '"Wall"',
                 what="the wall selected")
    key(app, "E")
    wait_open(app, True)

    # The left button places nothing, off the screen too
    aim_tile(app, TILE[0], TILE[1], OFF_SCREEN)
    app.exec("mouse_click left")
    app.wait_input()
    app.wait_frames(SETTLE)
    app.check(tiles(app, "Wall") == [], "the click with the screen open placed %s" %
              tiles(app, "Wall"))
    app.check(count(app, "Wall") == 5, "the click with the screen open spent a wall")

    # The pipette over empty ground keeps the selection
    key(app, "Q")
    app.wait_frames(SETTLE)
    app.check(selected(app) == "Wall", "Q with the screen open changed the selection to %s" %
              selected(app))

    # The right button does not mine
    dr, dc = deposit_tile(app)
    aim_tile(app, dr, dc, OFF_SCREEN)
    button(app, "right", True)
    app.wait_frames(30)
    progress = app.get(PLAYER, "oreelse.OreMining")["progress"]
    button(app, "right", False)
    app.check(progress == 0, "the swing went on with the screen open (%g)" % progress)

    # Closed, the right button mines again
    key(app, "E")
    wait_open(app, False)
    aim_tile(app, dr, dc, OFF_SCREEN)
    button(app, "right", True)
    app.wait_for("get %s oreelse.OreMining.progress" % PLAYER, lambda out: float(out) > 0,
                 what="a swing with the screen closed")
    button(app, "right", False)


def test_craft_keys(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 100), ("items.CopperPlate", 100)])

    # F with the screen closed and with it open
    key(app, "F")
    app.wait_for("get %s oreelse.OreCraftBook.count" % BOOK, "1", what="the engine of F")
    key(app, "E")
    wait_open(app, True)
    key(app, "F")
    app.wait_for("get %s oreelse.OreCraftBook.count" % BOOK, "2",
                 what="the engine of F, screen open")
    app.check(orders(app) == [("items.RocketEngine", 1), ("items.RocketEngine", 1)],
              "the order book holds %s" % orders(app))
    app.check(is_open(app), "F closed the screen")


def test_restart(app):
    start(app)
    key(app, "E")
    wait_open(app, True)
    key(app, "Tab")
    wait_tab(app, 1)

    # Lost with the screen open: it hides; the restart closes it on the Buildings tab
    set_state(app, "Lost")
    app.wait_frames(SETTLE)
    app.check(not shown(app, SCREEN), "the screen shows on the end screen")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    wait_open(app, False)
    app.check(tab(app) == 0, "the restart kept the tab %d" % tab(app))
    info = app.get(INFO, "oreelse.OreItemInfo")
    app.check(info["slot"] == -1 and entity(info["item"]) is None,
              "the restart kept the info panel's %s" % info)

    # The first E opens it
    key(app, "E")
    wait_open(app, True)
    app.check(shown(app, SCREEN), "the screen does not show")
