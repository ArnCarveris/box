"""Ore Else driven through its command port: power. Production and demand, the all or nothing
blackout, the levels of the allocation and oreelse.power_set(), the drills, the factories as
workers of the order book and the status lamps.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta
of 1/60 s per frame, waits that count frames. The cases place the buildings directly, with
scripts that create instances of the prefabs with their OreBuilding, and set the allocation
(OrePowerAlloc) through the REST endpoints of the port. To measure rates a case pauses the game,
plays a few frames and pauses again: the drills' timers and the crafters' work advance only
while playing, as does the clock, so the clock tells how much play the frames were.
"""
import json
import urllib.error
import urllib.request

from box_client import DELTA_TIME, parse_value, srgb

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

# Frames per second of game time and the delta of a frame
FPS = round(1 / float(DELTA_TIME))
DT = float(DELTA_TIME)

STATE = ("eval oreelse.OreGameState.has(flecs.script.core.pair("
         "oreelse.OreGameState,oreelse.OreGameState.%s))")
TOAST = "get oreelse.OreToast oreelse.OreToast.text"
PLAYER = "astronaut"
INVENTORY = "oreelse.OreInventory"
BOOK = "oreelse.OreCraftBook"
POWER = "oreelse.OrePowerState"
ALLOC = "oreelse.OrePowerAlloc"

# The tile grid (cfg.x0, cfg.z0, cfg.tile)
X0 = -47
Z0 = -47
TILE = 2

SLOTS = 15

# Watts of the buildings (OrePowerProducer, OrePowerConsumer of etc/buildings)
PANEL = 20
DRILL = 4
FACTORY = 75

# The bars of the allocation (cfg.powerBars) and the warning when none is free
BARS = 6
NO_BARS = "Only 6 power bars - free one somewhere else"

# Mining time of stone in seconds (OreMined of resources.flecs)
STONE_TIME = 1.5

# Craft time of a rocket engine in seconds (OreRecipe of resources.flecs)
ENGINE = "items.RocketEngine"
ENGINE_TIME = 30

# Seconds a swing at a building takes (cfg.dozeTime)
DOZE_TIME = 0.8
SWING_SLACK = 1.5

# The colors of a status lamp (the Light template's props), the original's bytes encoded as the
# port writes them (apps/oreelse/README.md, Colors), and the strength of a drill's lamps
DEAD = srgb((248, 84, 72))
WARN = srgb((240, 152, 72))
OK = srgb((124, 226, 148))
LIVE = srgb((92, 236, 222))
DRILL_LAMP = 2.8

# A corner of the map of seed 8 without deposits, where the cases build: rows and columns 3 to
# 12 (see oreelse_buildings.py)
AREA = (3, 12)

# Cursor position in points of the 640x360 window: the center hovers a tile next to the
# astronaut
CENTER = (320, 180)

SETTLE = 5


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
    name = "power_case_%d" % SCRIPTS[0]
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


def start(app):
    """Set up and leave the title screen with a key press."""
    setup(app)
    app.expect(STATE % "Title", "true")
    app.exec("key_press Space")
    app.wait_input()
    app.wait_for(STATE % "Playing", "true")


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
    """Set every slot of the inventory to the stacks in order."""
    slots = [{"item": s[0], "amount": s[1]} for s in stacks]
    slots += [{"item": 0, "amount": 0}] * (SLOTS - len(slots))
    put(app, INVENTORY, INVENTORY, {"slots": slots})
    app.wait_for("get %s oreelse.OreInventory.slots[0].amount" % INVENTORY,
                 str(stacks[0][1]) if stacks else "0")


def rows_of(app, expr):
    """Run a query of the port: the path and the value of the first field of every result."""
    result = {}
    for row in parse_value(app.exec('query "%s"' % expr)):
        fields = row["fields"]
        result[row["entity"]] = next(iter(fields[0]["value"].values())) if fields else None
    return result


def deposits(app):
    return rows_of(app, "oreelse.OreDeposit")


def deposit_of(app, kind):
    """The tile (row, col) of a full deposit of a kind."""
    found = deposits(app)
    for path in sorted(found):
        d = found[path]
        if d["resource"] == kind and d["amount"] == d["capacity"]:
            return d["row"], d["col"]
    app.fail("the map of seed %d has no deposit of %s" % (SEED, kind))


def buildings(app, prefab):
    return rows_of(app, "oreelse.OreBuilding, (IsA, %s)" % prefab)


def tile_point(row, col, fx=0.5, fz=0.5):
    """The ground point at fractions fx, fz inside a tile."""
    return X0 + col * TILE + (fx - 0.5) * TILE, Z0 + row * TILE + (fz - 0.5) * TILE


def place(app, name, prefab, row, col, w=1, h=1):
    """Place a building directly: an instance of its prefab named name with its top left tile
    at (row, col), standing at the center of its footprint of w by h tiles."""
    x, z = tile_point(row, col)
    x += (w - 1) * TILE / 2
    z += (h - 1) * TILE / 2
    run_script(app, "using box.transform\n%s : %s {\n  Position: {%g, 0, %g}\n"
                    "  oreelse.OreBuilding: {row: %d, col: %d}\n}" %
               (name, prefab, x, z, row, col))
    app.wait_for("eval %s.has(oreelse.OreBuilding)" % name, "true")


def power(app):
    return app.get(POWER, "oreelse.OrePowerState")


def wait_power(app, production, demand, blackout):
    """Wait for the power state of a production, a demand and a blackout."""
    def matches(out):
        p = parse_value(out)
        ratio = 1 if not blackout else production / demand
        return (abs(p["production"] - production) < 1e-3 and
                abs(p["demand"] - demand) < 1e-3 and p["blackout"] == blackout and
                abs(p["ratio"] - ratio) < 1e-4)
    app.wait_for("get %s oreelse.OrePowerState" % POWER, matches,
                 what="production %g, demand %g, blackout %s" % (production, demand, blackout))


def set_levels(app, mining, weapons, construction):
    put(app, ALLOC, ALLOC, {"level": [mining, weapons, construction]})
    app.wait_for("get %s oreelse.OrePowerAlloc" % ALLOC,
                 lambda out: parse_value(out)["level"] == [mining, weapons, construction])


def levels(app):
    return app.get(ALLOC, "oreelse.OrePowerAlloc")["level"]


def power_set(app, category, level):
    """Click bar level of the column of a category with oreelse.power_set(); returns whether
    the level changed."""
    n = SCRIPTS[0] + 1
    run_script(app, "power_result_%d { oreelse.OreCanOrder: {value: "
                    "oreelse.power_set(%d, %d)} }" % (n, category, level))
    return app.get("power_result_%d" % n, "oreelse.OreCanOrder")["value"]


def clock(app):
    return app.get("oreelse.OreClock", "oreelse.OreClock")["elapsed"]


def play(app, frames):
    """From a pause, play a number of frames and pause again; returns the seconds played."""
    before = clock(app)
    set_state(app, "Playing")
    app.wait_frames(frames)
    set_state(app, "Paused")
    return clock(app) - before


def drill_state(app, path):
    return app.get(path, "oreelse.OreDrill")


def crafter(app, path):
    c = app.get(path, "oreelse.OreCrafter")
    c["item"] = entity(c["item"])
    return c


def orders(app):
    """The orders oldest first, each with its entries and pool cut to their counts."""
    b = app.get(BOOK, "oreelse.OreCraftBook")
    result = []
    for o in b["orders"][:b["count"]]:
        o["entries"] = o["entries"][:o["entry_count"]]
        o["pool"] = {s["item"]: s["amount"] for s in o["pool"]["stacks"][:o["pool"]["count"]]}
        result.append(o)
    return result


def order(app, item, units):
    """Order units of an item with oreelse.craft_order(); returns the units ordered."""
    n = SCRIPTS[0] + 1
    run_script(app, "craft_result_%d { oreelse.OreStack: {item: %s, amount: "
                    "oreelse.craft_order(%s, %d)} }" % (n, item, item, units))
    return app.get("craft_result_%d" % n, "oreelse.OreStack")["amount"]


def hover(app):
    return app.get("oreelse.OreHover", "oreelse.OreHover")


def point(h):
    p = h["point"]
    return (p["x"], p["z"]) if isinstance(p, dict) else (p[0], p[2])


def move_cursor(app, cursor):
    app.exec("mouse_move [%g, %g]" % cursor)
    app.wait_input()
    app.wait_frames(SETTLE)


def place_player(app, x, z):
    app.exec("set %s box.transform.Position {%g, 0, %g}" % (PLAYER, x, z))
    app.wait_frames(SETTLE)


def aim_tile(app, row, col):
    """Place the astronaut so that the center of the window hovers the center of a tile."""
    move_cursor(app, CENTER)
    p = app.get(PLAYER, "box.transform.Position")
    hx, hz = point(hover(app))
    x, z = tile_point(row, col)
    place_player(app, x - (hx - p["x"]), z - (hz - p["z"]))
    h = hover(app)
    app.check(h["on_grid"] and (h["row"], h["col"]) == (row, col),
              "the cursor hovers %s, not the tile %s" % (h, (row, col)))


def button(app, name, down):
    app.exec("mouse_button %s %s" % (name, "true" if down else "false"))
    app.wait_input()


def lamps_of(app, owner):
    """The bulbs of the status lamps of a building."""
    return sorted(path for path, lamp in rows_of(app, "oreelse.OreStatusLamp").items()
                  if entity(lamp["owner"]) == owner)


def lamp(app, path):
    """The color and the emissive strength of a bulb."""
    rgb = app.get(path, "box.material.Rgb")
    return (rgb["r"], rgb["g"], rgb["b"]), app.get(path, "box.material.Emissive")["strength"]


def wait_lamps(app, owner, color, what):
    """Wait until every status lamp of a building shows a color."""
    def shows(_):
        bulbs = lamps_of(app, owner)
        return bulbs and all(lamp(app, b)[0] == color for b in bulbs)
    app.wait_for("get %s oreelse.OrePowerState" % POWER, shows, what=what)


def test_production_and_demand(app):
    start(app)
    lo, _ = AREA
    first, second = sorted((d["row"], d["col"]) for d in deposits(app).values())[:2]
    place(app, "panel_a", "SolarPanel", lo + 1, lo)
    place(app, "drill_a", "Drill", *first)
    place(app, "drill_b", "Drill", *second)
    wait_power(app, PANEL, 2 * DRILL, False)

    # Four factories with one panel: a blackout
    for i, (row, col) in enumerate([(6, 6), (6, 8), (8, 6), (8, 8)]):
        place(app, "factory_%d" % i, "Factory", row, col, 2, 2)
    raw = 2 * DRILL + 4 * FACTORY
    wait_power(app, PANEL, raw, True)

    # The demand follows the bars assigned: everything at 1 draws half the watts
    set_levels(app, 1, 1, 1)
    wait_power(app, PANEL, raw / 2, True)
    set_levels(app, 4, 1, 0)
    wait_power(app, PANEL, raw * 5 / BARS, True)

    # No bar assigned: no demand, no blackout
    set_levels(app, 0, 0, 0)
    wait_power(app, PANEL, 0, False)


def test_power_set(app):
    start(app)
    app.check(levels(app) == [2, 2, 2], "a run starts at the levels %s" % levels(app))

    # Raising Mining to 4 with the others at 2: no bar is free, nothing changes
    app.check(not power_set(app, 0, 4), "Mining went up with no free bar")
    app.wait_for(TOAST, '"%s"' % NO_BARS, what="the toast %r" % NO_BARS)
    app.check(levels(app) == [2, 2, 2], "the refused click left %s" % levels(app))

    # The topmost lit bar steps the category down one level
    app.check(power_set(app, 0, 2), "the click on the top lit bar changed nothing")
    app.check(levels(app) == [1, 2, 2], "the click on the top lit bar gives %s" % levels(app))
    app.check(power_set(app, 1, 2) and levels(app) == [1, 1, 2],
              "Weapons did not step down: %s" % levels(app))

    # An increase is capped by the free bars: 3 for Mining
    app.check(power_set(app, 0, 4), "Mining did not go up")
    app.check(levels(app) == [3, 1, 2], "the capped increase gives %s" % levels(app))

    # A click on a lower bar lowers the category to it, at 1 the top bar turns it off
    app.check(power_set(app, 0, 1) and levels(app) == [1, 1, 2],
              "the click on bar 1 gives %s" % levels(app))
    app.check(power_set(app, 0, 1) and levels(app) == [0, 1, 2],
              "the click on the single lit bar gives %s" % levels(app))
    app.check(power_set(app, 0, 1) and levels(app) == [1, 1, 2],
              "the click on bar 1 of an off category gives %s" % levels(app))

    # Out of range bars and categories do nothing
    app.check(not power_set(app, 0, 5) and not power_set(app, 3, 1) and
              levels(app) == [1, 1, 2], "an invalid click changed the levels to %s" %
              levels(app))


def check_drill(app, path, timer, stone, played, mul):
    """Check the timer and the stone of a drill that started at timer with stone in the
    inventory and played seconds at a multiplier: every STONE_TIME of timer is one stone.
    Returns the stone mined."""
    d = drill_state(app, path)
    got = count(app, "items.Stone") - stone
    total = timer + played * mul
    tolerance = 2 * DT * mul + 1e-3
    mined = int(total // STONE_TIME)
    near = abs(total - round(total / STONE_TIME) * STONE_TIME) <= tolerance
    app.check(got == mined or (near and abs(got - total / STONE_TIME) < 1),
              "after %.3f s at %g the drill mined %d stone, not %d" % (played, mul, got, mined))
    app.check(abs(got * STONE_TIME + d["timer"] - total) <= tolerance,
              "after %.3f s at %g from %g the drill's timer is %g with %d stone mined" %
              (played, mul, timer, d["timer"], got))
    return got


def test_drill(app):
    start(app)
    set_inventory(app, [("items.Iron", 1)])
    row, col = deposit_of(app, "items.Stone")
    place(app, "drill_a", "Drill", row, col)
    wait_power(app, 0, DRILL, True)

    # In a blackout the drill holds its progress
    set_state(app, "Paused")
    app.exec("set drill_a oreelse.OreDrill {timer: 1.2}")
    played = play(app, 12)
    app.check(played > 0, "no play passed")
    d = drill_state(app, "drill_a")
    app.check(d["timer"] == 1.2 or abs(d["timer"] - 1.2) < 1e-6,
              "the drill's timer went from 1.2 to %g in a blackout" % d["timer"])
    app.check(count(app, "items.Stone") == 0, "the drill mined in a blackout")

    # Powered at level 2 it goes on from its progress: 1 stone every 1.5 s
    place(app, "panel_a", "SolarPanel", 4, 4)
    stone = count(app, "items.Stone")
    played = play(app, 24)
    app.check(check_drill(app, "drill_a", 1.2, stone, played, 1) >= 1,
              "%.3f s of play at level 2 mined nothing from 1.2" % played)

    # At level 4 every 0.75 s
    set_levels(app, 4, 1, 1)
    app.exec("set drill_a oreelse.OreDrill {timer: 1.2}")
    stone = count(app, "items.Stone")
    played = play(app, 24)
    app.check(check_drill(app, "drill_a", 1.2, stone, played, 2) >= 1,
              "%.3f s of play at level 4 mined nothing from 1.2" % played)

    # At level 0 not at all
    set_levels(app, 0, 3, 3)
    app.exec("set drill_a oreelse.OreDrill {timer: 1.2}")
    stone = count(app, "items.Stone")
    played = play(app, 12)
    check_drill(app, "drill_a", 1.2, stone, played, 0)
    app.check(count(app, "items.Stone") == stone, "the drill mined at level 0")
    app.check(not drill_state(app, "drill_a")["running"], "the drill runs at level 0")


def test_factory(app):
    start(app)
    app.exec("set %s oreelse.OreCrafter {speed: 1, paused: true}" % PLAYER)
    for i in range(5):
        place(app, "panel_%d" % i, "SolarPanel", 4, 3 + i)
    place(app, "factory_a", "Factory", 6, 6, 2, 2)
    app.wait_for("eval factory_a.has(oreelse.OreCrafter)", "true", what="the factory's crafter")
    wait_power(app, 5 * PANEL, FACTORY, False)
    c = crafter(app, "factory_a")
    app.check(c["speed"] == 1 and not c["paused"], "the factory's crafter is %s" % c)

    # Its speed is the Construction multiplier: 2 at level 4, 1.5 at 3, stopped at 0
    set_levels(app, 2, 0, 4)
    app.wait_for("get factory_a oreelse.OreCrafter.speed", lambda out: float(out) == 2,
                 what="the factory at speed 2")
    set_levels(app, 1, 2, 3)
    app.wait_for("get factory_a oreelse.OreCrafter.speed", lambda out: float(out) == 1.5,
                 what="the factory at speed 1.5")
    set_levels(app, 2, 2, 0)
    app.wait_for("get factory_a oreelse.OreCrafter.paused", "true", what="the factory stopped")

    # An order of two engines, nobody working on it yet
    set_inventory(app, [("items.IronPlate", 16), ("items.CopperPlate", 8)])
    app.check(order(app, ENGINE, 2) == 2, "the order of two engines failed")
    set_state(app, "Paused")
    o = orders(app)[0]
    app.check(o["entries"][0]["count"] == 2 and o["entries"][0]["running"] == 0,
              "the order is %s" % o)

    # With the factory working too, the two crafts run at once: twice the rate
    set_levels(app, 2, 2, 2)
    app.exec("set %s oreelse.OreCrafter {paused: false}" % PLAYER)
    play(app, 12)
    player, factory = crafter(app, PLAYER), crafter(app, "factory_a")
    app.check(player["item"] == ENGINE and factory["item"] == ENGINE,
              "the workers craft %s and %s" % (player["item"], factory["item"]))
    app.check(orders(app)[0]["entries"][0]["running"] == 2,
              "%d crafts run, not 2" % orders(app)[0]["entries"][0]["running"])
    app.check(abs(player["left"] - factory["left"]) < 1e-4 and factory["left"] < ENGINE_TIME,
              "the factory is at %g s left, the astronaut at %g" %
              (factory["left"], player["left"]))

    # In a blackout it holds its craft while the astronaut goes on
    app.exec("rm panel_3")
    app.exec("rm panel_4")
    play(app, 12)
    p = power(app)
    app.check(p["blackout"], "no blackout with %g W for %g W" % (p["production"], p["demand"]))
    held = crafter(app, "factory_a")
    app.check(held["paused"] and held["item"] == ENGINE and held["left"] == factory["left"],
              "in a blackout the factory went from %s to %s" % (factory, held))
    app.check(crafter(app, PLAYER)["left"] < player["left"],
              "the astronaut stopped crafting in a blackout")

    # With the power back it resumes its craft
    place(app, "panel_5", "SolarPanel", 4, 8)
    place(app, "panel_6", "SolarPanel", 4, 9)
    play(app, 12)
    resumed = crafter(app, "factory_a")
    app.check(not resumed["paused"] and resumed["item"] == ENGINE and
              resumed["left"] < held["left"],
              "with the power back the factory went from %s to %s" % (held, resumed))

    # Demolished mid-craft it returns the inputs of its craft to the order
    set_state(app, "Playing")
    aim_tile(app, 7, 6)
    button(app, "right", True)
    app.wait_for("get %s oreelse.OreInventory" % INVENTORY,
                 lambda out: any(s["item"] == "Factory" and s["amount"] == 1
                                 for s in parse_value(out)["slots"]),
                 frames=round((DOZE_TIME + SWING_SLACK) * FPS), what="the factory demolished")
    button(app, "right", False)
    set_state(app, "Paused")
    app.check(not buildings(app, "Factory"), "the demolished factory stands")
    o = orders(app)[0]
    app.check(o["pool"].get("items.IronPlate") == 8 and o["pool"].get("items.CopperPlate") == 4,
              "the order's pool is %s, not the inputs of an engine" % o["pool"])
    app.check(o["entries"][0]["running"] == 1, "%d crafts run, not 1" %
              o["entries"][0]["running"])


def test_player_crafts_in_blackout(app):
    start(app)
    row, col = deposit_of(app, "items.Stone")
    place(app, "drill_a", "Drill", row, col)
    wait_power(app, 0, DRILL, True)
    set_inventory(app, [("items.Iron", 2)])
    app.check(order(app, "items.IronPlate", 1) == 1, "the order of an iron plate failed")
    app.wait_for("get %s oreelse.OreInventory" % INVENTORY,
                 lambda out: any(s["item"] == "items.IronPlate" and s["amount"] == 1
                                 for s in parse_value(out)["slots"]),
                 frames=2 * FPS, what="the iron plate crafted in a blackout")
    app.check(power(app)["blackout"], "the blackout ended")


def test_lamps(app):
    start(app)
    row, col = deposit_of(app, "items.Stone")
    place(app, "panel_a", "SolarPanel", 4, 4)
    place(app, "drill_a", "Drill", row, col)
    place(app, "gun_a", "GunTurret", 4, 6)
    wait_power(app, PANEL, DRILL, False)

    # By the level of their category: 2 green, 1 orange, 3 cyan
    wait_lamps(app, "drill_a", OK, "the drill's lamps green at level 2")
    bulbs = lamps_of(app, "drill_a")
    app.check(len(bulbs) == 2, "the drill has %d status lamps, not 2" % len(bulbs))
    app.check(all(abs(lamp(app, b)[1] - DRILL_LAMP) < 1e-4 for b in bulbs),
              "the drill's lamps glow at %s" % [lamp(app, b)[1] for b in bulbs])
    wait_lamps(app, "gun_a", OK, "the gun turret's lamp green at level 2")
    set_levels(app, 1, 2, 2)
    wait_lamps(app, "drill_a", WARN, "the drill's lamps orange at level 1")
    set_levels(app, 3, 2, 1)
    wait_lamps(app, "drill_a", LIVE, "the drill's lamps cyan at level 3")

    # A blackout kills the drill's lamps, which blink; the gun turret draws no power
    app.exec("rm panel_a")
    wait_power(app, 0, DRILL * 6 / BARS, True)
    wait_lamps(app, "drill_a", DEAD, "the drill's lamps red in a blackout")
    bulb = lamps_of(app, "drill_a")[0]
    app.wait_for("get %s box.material.Emissive.strength" % bulb, lambda out: float(out) == 0,
                 frames=2 * FPS, what="the dead lamp in its off half")
    app.wait_for("get %s box.material.Emissive.strength" % bulb,
                 lambda out: abs(float(out) - DRILL_LAMP) < 1e-4,
                 frames=2 * FPS, what="the dead lamp in its on half")
    for _ in range(3):
        for b in lamps_of(app, "gun_a"):
            color, strength = lamp(app, b)
            app.check(color == OK and strength > 0,
                      "in a blackout the gun turret's lamp is %s at %g" % (color, strength))
        app.wait_frames(SETTLE)

    # Weapons at 0 kills it
    set_levels(app, 3, 0, 1)
    wait_lamps(app, "gun_a", DEAD, "the gun turret's lamp red with Weapons at 0")
