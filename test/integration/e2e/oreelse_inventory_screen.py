"""Ore Else driven through its command port: the inventory screen (E). E and the Inventory (E)
button open and close it, ESC closes it before it clears the selection, Tab flips its tabs only
while it is open, a click on a building in a stock slot selects it and closes the screen, a
click on a slot button orders one (five with Shift) and a dim one nothing, the world takes no
mining, placing or pipette while it is open, the craft keys work with it closed, and a restart
from an open screen leaves it closed with E opening it at the first press.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window, so the 802x368 screen hangs
over its edges and the cases click the elements that lie inside it. A case finds the window
point of an element from its WorldTransform (the HUD's camera shows one point per unit, centered
on the window) and checks with PickStatus that the element handles the pointer there before it
clicks.
"""
from oreelse_buildings import (  # noqa: F401 (ARGS)
    ARGS, PLACEMENT, PLAYER, SETTLE, STATE, aim_tile, button, count, deposit_tile, entity,
    inventory, key, placement, press, selected, set_inventory, set_state, start, tiles,
    wait_toast)

APP = "apps/oreelse"

SCREEN_STATE = "oreelse.OreInventoryScreen"
INFO = "oreelse.OreItemInfo"
BOOK = "oreelse.OreCraftBook"
PICK = "get box.picking.PickStatus PickStatus."

# The window of the cases, in points
WIDTH = 640
HEIGHT = 360

# The elements of the screen (ui/inventory.flecs) under the HUD's screen
UI = "hud.ui.screen.inventory"
SCREEN = UI + ".screen"
GRID = SCREEN + ".stock_section.body.stock_body.grid"
TABS = SCREEN + ".construct_section.body.tabs"
PAGES = SCREEN + ".construct_section.body.pages"
BUILDINGS = PAGES + ".buildings_page"
ROCKET = PAGES + ".rocket_page"
INFO_PANEL = UI + ".info_panel"
BUTTON = UI + ".inventory_button"

# A point of the window below the screen and left of the Inventory (E) button, where a click
# reaches the world (the screen spans y -64..304 and the button x 222..418)
OFF_SCREEN = (60, 330)

# A corner of the map without deposits (oreelse_buildings.AREA)
TILE = (6, 6)


def is_open(app):
    return app.get(SCREEN_STATE, "OreInventoryScreen")["open"]


def tab(app):
    return app.get(SCREEN_STATE, "OreInventoryScreen")["tab"]


def wait_open(app, value):
    app.wait_for("get %s OreInventoryScreen.open" % SCREEN_STATE,
                 "true" if value else "false",
                 what="the screen %s" % ("open" if value else "closed"))
    app.wait_frames(SETTLE)


def wait_tab(app, value):
    app.wait_for("get %s OreInventoryScreen.tab" % SCREEN_STATE, str(value),
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
    m = flat(app.get(path, "WorldTransform"))
    app.check(len(m) == 16, "the WorldTransform of %s is %s" % (path, m))
    x, y = WIDTH / 2 + m[12], HEIGHT / 2 - m[13]
    app.check(0 <= x < WIDTH and 0 <= y < HEIGHT,
              "%s is at %s, outside the %dx%d window" % (path, (x, y), WIDTH, HEIGHT))
    return x, y


def hover(app, path):
    """Move the cursor onto an element and wait until it handles the pointer there."""
    p = point(app, path)
    app.exec("mouse.move [%g, %g]" % p)
    app.wait_input()
    app.wait_for(PICK + "handler", path, what="%s under the cursor" % path)
    return p


def click(app, path, shift=False):
    """Click an element of the HUD, with Shift held when shift is set."""
    p = hover(app, path)
    if shift:
        app.exec("key.down ShiftLeft")
        app.wait_input()
    app.exec("mouse.click left [%g, %g]" % p)
    app.wait_input()
    app.wait_frames(SETTLE)
    if shift:
        app.exec("key.up ShiftLeft")
        app.wait_input()


def orders(app):
    book = app.get(BOOK, "OreCraftBook")
    return [(o["item"], o["demand"]) for o in book["orders"][:book["count"]]]


def can_order(app, item):
    return app.get(item, "OreCanOrder")["value"]


def test_open_close(app):
    start(app)
    app.check(not is_open(app), "the screen is open at the start")
    app.check(app.exec("has %s Panel" % SCREEN) == "false", "the screen shows at the start")

    # E opens it and closes it again; the game keeps running
    key(app, "E")
    wait_open(app, True)
    app.check(app.exec("has %s Panel" % SCREEN) == "true", "the open screen does not show")
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
    app.check(app.exec("has %s Panel" % SCREEN) == "false", "the screen shows while paused")
    key(app, "E")
    app.wait_frames(SETTLE)
    app.check(is_open(app), "E changed the screen while paused")
    set_state(app, "Playing")
    app.wait_frames(SETTLE)
    app.check(app.exec("has %s Panel" % SCREEN) == "true", "the screen did not come back")


def test_escape_and_drag(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    key(app, "1")
    app.wait_for("get %s OrePlacement.selected" % PLACEMENT, "Wall", what="the wall selected")

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
    app.wait_for("get %s OrePlacement.selected" % PLACEMENT, lambda out: entity(out) is None,
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
    app.check(app.exec("has %s Layout" % BUILDINGS) == "true", "no Buildings page")
    key(app, "Tab")
    wait_tab(app, 1)
    app.check(app.exec("has %s Layout" % ROCKET) == "true", "no Rocket page on the Rocket tab")
    app.check(app.exec("has %s Layout" % BUILDINGS) == "false",
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
    app.wait_for("get %s OreItemInfo.item" % INFO, "Wall", what="the wall under the pointer")
    app.check(app.get(INFO, "OreItemInfo")["place"], "the wall of the slot is not to place")
    app.wait_for("get %s.note Text.value" % INFO_PANEL, '"Ready to place"',
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
    set_inventory(app, [("items.IronPlate", 100), ("items.CopperPlate", 100)])
    key(app, "E")
    wait_open(app, True)
    app.wait_for("get Drill OreCanOrder.value", "true", what="a drill to order")
    app.check(not can_order(app, "Wall"), "a wall can be ordered without stone")

    # Over a slot button the info panel shows the recipe and the needs
    hover(app, BUILDINGS + ".row_base.drill")
    app.wait_for("get %s OreItemInfo.item" % INFO, "Drill", what="the drill under the pointer")
    app.wait_for("get %s.note Text.value" % INFO_PANEL, '"Craft 1.0s - 4 W"',
                 what="the note of the drill")

    # A click orders one, the screen stays open
    click(app, BUILDINGS + ".row_base.drill")
    app.wait_for("get %s OreCraftBook.count" % BOOK, "1", what="the order of a drill")
    app.check(orders(app) == [("Drill", 1)], "the order book holds %s" % orders(app))
    app.check(count(app, "items.IronPlate") == 94, "the drill took %s" % inventory(app))
    app.check(is_open(app), "the order closed the screen")

    # With Shift five
    click(app, BUILDINGS + ".row_base.drill", shift=True)
    app.wait_for("get %s OreCraftBook.count" % BOOK, "2", what="the order of five drills")
    app.check(orders(app) == [("Drill", 1), ("Drill", 5)], "the order book holds %s" %
              orders(app))
    app.check(count(app, "items.IronPlate") == 64, "six drills took %s" % inventory(app))

    # A dim button orders nothing
    click(app, BUILDINGS + ".row_base.wall")
    app.wait_frames(SETTLE)
    app.check(len(orders(app)) == 2, "the dim wall ordered %s" % orders(app))

    # The rocket page orders its parts the same way
    key(app, "Tab")
    wait_tab(app, 1)
    click(app, ROCKET + ".row_rocket.engine")
    app.wait_for("get %s OreCraftBook.count" % BOOK, "3", what="the order of an engine")
    app.check(orders(app)[2] == ("items.RocketEngine", 1), "the order book holds %s" %
              orders(app))
    app.check(tiles(app) == [], "the clicks on the screen placed %s" % tiles(app))


def test_world_blocked(app):
    start(app)
    set_inventory(app, [("Wall", 5)])
    key(app, "1")
    app.wait_for("get %s OrePlacement.selected" % PLACEMENT, "Wall", what="the wall selected")
    key(app, "E")
    wait_open(app, True)

    # The left button places nothing, off the screen too
    aim_tile(app, TILE[0], TILE[1], OFF_SCREEN)
    app.exec("mouse.click left")
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
    progress = app.get(PLAYER, "OreMining")["progress"]
    button(app, "right", False)
    app.check(progress == 0, "the swing went on with the screen open (%g)" % progress)

    # Closed, the right button mines again
    key(app, "E")
    wait_open(app, False)
    aim_tile(app, dr, dc, OFF_SCREEN)
    button(app, "right", True)
    app.wait_for("get %s OreMining.progress" % PLAYER, lambda out: float(out) > 0,
                 what="a swing with the screen closed")
    button(app, "right", False)


def test_craft_keys(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 100), ("items.CopperPlate", 100)])

    # F with the screen closed and with it open
    key(app, "F")
    app.wait_for("get %s OreCraftBook.count" % BOOK, "1", what="the engine of F")
    key(app, "E")
    wait_open(app, True)
    key(app, "F")
    app.wait_for("get %s OreCraftBook.count" % BOOK, "2", what="the engine of F, screen open")
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
    app.check(app.exec("has %s Panel" % SCREEN) == "false", "the screen shows on the end screen")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    wait_open(app, False)
    app.check(tab(app) == 0, "the restart kept the tab %d" % tab(app))
    info = app.get(INFO, "OreItemInfo")
    app.check(info["slot"] == -1 and entity(info["item"]) is None,
              "the restart kept the info panel's %s" % info)

    # The first E opens it
    key(app, "E")
    wait_open(app, True)
    app.check(app.exec("has %s Panel" % SCREEN) == "true", "the screen does not show")
