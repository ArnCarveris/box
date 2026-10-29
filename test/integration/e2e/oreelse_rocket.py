"""Ore Else driven through its command port: the rocket on the rocket pad. Installing parts with
their keys and the full payload warnings, the validity, loading Luminite into the cargo bays,
uninstalling a part with what comes back with it, the cargo launch that ships Luminite, the
crewed launch that wins, the parts lost with a demolished pad and the craft keys of the rocket
parts.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup. The cases place a rocket pad with a click (the
helpers of oreelse_buildings.py) and set the inventory directly. Uninstalling has no key: the
cases call oreelse.rocket_uninstall() from an async block of a script.
"""
from box_client import parse_value
from oreelse_buildings import (  # noqa: F401 (ARGS)
    ARGS, DOZE_TIME, FPS, PLAYER, SETTLE, STATE, TOAST, button, click, count,
    inventory, key, mine, place_player, put, run_script, select, set_inventory, start,
    tile_point, wait_toast)

ROCKET = "oreelse.OreRocketState"
BOOK = "oreelse.OreCraftBook"

# The pad goes on the tiles (4..6, 9..11), its center on the tile (5, 10)
PAD = (4, 9)
PAD_CENTER = (5, 10)

# The rocket stands at base_y above the pad (OreLaunchPad of RocketPad)
BASE_Y = 0.74

# A cargo bay holds 25 Luminite (cfg.cargoCapacity)
CAPACITY = 25

# Seconds of ignition (cfg.ignitionTime) and of the ascent to 60 units at 9 units/s^2
IGNITION = 1.6
FLIGHT = 5.25

ENGINE = "items.RocketEngine"
FUEL = "items.RocketFuelStorage"
CARGO = "items.RocketCargoBay"
LIFE = "items.RocketLifeSupport"
LUMINITE = "items.Luminite"

ENGINES_FULL = "Engines full for this payload - add a cargo bay (U) first"
FUEL_FULL = "Fuel tanks full for this payload - add a cargo bay (U) first"

# Kinds of items other than the rocket parts and Luminite, to fill the inventory
FILLER = ["items.Iron", "items.Copper", "items.Stone", "items.IronPlate", "items.CopperPlate",
          "items.StoneBrick", "items.Wire", "items.Electronics", "Wall", "Drill", "SolarPanel",
          "GunTurret", "LaserTurret", "MissileTurret"]


def rocket(app):
    return app.get(ROCKET, "OreRocketState")


def wait_rocket(app, check, what, frames=None):
    """Wait until the rocket state passes a check; returns it."""
    app.wait_for("get %s OreRocketState" % ROCKET, lambda out: check(parse_value(out)),
                 frames=frames or 30, what=what)
    return rocket(app)


def place_pad(app, stacks):
    """Place the rocket pad on PAD with a click, then set the inventory to stacks and walk the
    astronaut off the pad."""
    set_inventory(app, [("RocketPad", 1)])
    select(app, "8", "RocketPad")
    click(app, *PAD)
    wait_rocket(app, lambda r: r["pad"] not in (0, "0", "#0", "", None), "the rocket pad")
    set_inventory(app, stacks)
    x, z = tile_point(PAD[0] + 4, PAD[1])
    place_player(app, x, z)


def press(app, keys):
    """Press keys one after the other."""
    for k in keys:
        key(app, k)
    app.wait_frames(SETTLE)


def uninstall(app, item):
    run_script(app, "async {\n  await oreelse.rocket_uninstall(%s)\n}" % item)
    app.wait_frames(SETTLE)


def assemble(app, life=False):
    """Install a cargo bay (or the life support), 4 engines and a fuel storage with their keys."""
    press(app, ["I" if life else "U", "T", "T", "T", "T", "Y"])
    return wait_rocket(app, lambda r: r["valid"], "a valid rocket")


def parts(r):
    return (r["engines"], r["fuel"], r["cargo"], r["life_support"])


def test_install(app):
    start(app)
    place_pad(app, [(ENGINE, 5), (FUEL, 2), (CARGO, 2)])

    # Without a unit there is no room for an engine or a fuel storage
    key(app, "T")
    wait_toast(app, ENGINES_FULL)
    key(app, "Y")
    wait_toast(app, FUEL_FULL)
    r = rocket(app)
    app.check(parts(r) == (0, 0, 0, 0) and count(app, ENGINE) == 5 and count(app, FUEL) == 2,
              "the refused parts were installed: %s, %s" % (r, inventory(app)))
    app.check(not r["install"]["engine"] and not r["install"]["fuel"] and
              r["install"]["cargo"] and not r["install"]["life_support"],
              "the rocket offers to install %s" % r["install"])
    app.check(not r["valid"] and not r["launch"] and not r["shown"], "the empty rocket is %s" % r)

    # A cargo bay, 4 engines and a fuel storage make a valid rocket
    r = assemble(app)
    app.check(parts(r) == (4, 1, 1, 0) and r["units"] == 1 and r["capacity"] == CAPACITY,
              "the rocket is %s" % r)
    app.check(r["launch"] and r["shown"], "the valid rocket cannot launch: %s" % r)
    app.check(count(app, ENGINE) == 1 and count(app, FUEL) == 1 and count(app, CARGO) == 1,
              "the parts were not spent: %s" % inventory(app))
    x, z = tile_point(*PAD_CENTER)
    app.check(abs(r["x"] - x) < 1e-4 and abs(r["z"] - z) < 1e-4 and
              abs(r["y"] - BASE_Y) < 1e-4, "the rocket stands at %s" % r)

    # The model follows the state
    model = app.get("rocket", "Rocket")
    app.check(model["engines"] == 4 and model["fuel"] == 1 and model["cargo"] == 1 and
              model["life_support"] == 0 and model["valid"] and not model["loaded"],
              "the model is %s" % model)
    p = app.get("rocket", "Position")
    app.check(abs(p["x"] - x) < 1e-4 and abs(p["z"] - z) < 1e-4 and abs(p["y"] - BASE_Y) < 1e-4,
              "the model stands at %s" % p)

    # The fifth engine and the second fuel storage do not fit one unit
    key(app, "T")
    wait_toast(app, ENGINES_FULL)
    key(app, "Y")
    wait_toast(app, FUEL_FULL)
    app.check(parts(rocket(app)) == (4, 1, 1, 0), "the rocket took more: %s" % rocket(app))


def test_load(app):
    start(app)
    place_pad(app, [(CARGO, 2), (LUMINITE, 60)])
    press(app, ["U", "O"])
    r = wait_rocket(app, lambda r: r["loaded"] == CAPACITY, "25 Luminite loaded")
    app.check(count(app, LUMINITE) == 35, "%d Luminite left, not 35" % count(app, LUMINITE))
    app.check(not r["load"], "the full rocket offers to load: %s" % r)
    app.check(app.get("rocket", "Rocket")["loaded"], "the loaded model shows no load")

    # A full bay takes no more
    press(app, ["O"])
    app.check(rocket(app)["loaded"] == CAPACITY and count(app, LUMINITE) == 35,
              "the full rocket loaded more: %s" % rocket(app))

    # A second bay takes 25 more, of which 10 are left after
    press(app, ["U", "O"])
    r = wait_rocket(app, lambda r: r["loaded"] == 2 * CAPACITY, "50 Luminite loaded")
    app.check(r["capacity"] == 2 * CAPACITY and count(app, LUMINITE) == 10,
              "with two bays: %s, %s" % (r, inventory(app)))


def test_uninstall(app):
    start(app)
    place_pad(app, [(CARGO, 1), (ENGINE, 4), (FUEL, 1), (LUMINITE, 30)])
    assemble(app)
    press(app, ["O"])
    wait_rocket(app, lambda r: r["loaded"] == CAPACITY, "25 Luminite loaded")

    # The cargo bay comes back with the 4 engines, the fuel storage and the Luminite
    uninstall(app, CARGO)
    wait_toast(app, "Payload removed - returned 4 engines, 1 fuel tank, 25 Luminite")
    r = rocket(app)
    app.check(parts(r) == (0, 0, 0, 0) and r["loaded"] == 0 and not r["shown"],
              "the rocket is %s" % r)
    app.check(count(app, CARGO) == 1 and count(app, ENGINE) == 4 and count(app, FUEL) == 1 and
              count(app, LUMINITE) == 30, "the inventory holds %s" % inventory(app))

    # A single engine comes back alone, without a toast
    press(app, ["U", "T"])
    wait_rocket(app, lambda r: r["engines"] == 1, "an engine installed")
    put(app, "oreelse.OreToast", "oreelse.OreToast", {"text": "", "warn": False, "age": 0})
    uninstall(app, ENGINE)
    wait_rocket(app, lambda r: r["engines"] == 0, "the engine uninstalled")
    app.check(app.exec(TOAST) == '""', "uninstalling an engine toasts %s" % app.exec(TOAST))
    app.check(count(app, ENGINE) == 4, "the engine did not come back: %s" % inventory(app))

    # Refused when the returned kinds do not fit the inventory
    press(app, ["T", "T", "T", "T", "Y", "O"])
    r = wait_rocket(app, lambda r: r["valid"] and r["loaded"] == CAPACITY, "a loaded rocket")
    set_inventory(app, [(LUMINITE, 5)] + [(k, 1) for k in FILLER])
    uninstall(app, CARGO)
    wait_toast(app, "No room in the inventory for the parts")
    after = rocket(app)
    app.check(parts(after) == parts(r) and after["loaded"] == CAPACITY,
              "the refused uninstall changed the rocket to %s" % after)
    app.check(count(app, CARGO) == 0 and count(app, ENGINE) == 0 and count(app, LUMINITE) == 5,
              "the refused uninstall returned %s" % inventory(app))


def test_cargo_launch(app):
    start(app)
    place_pad(app, [(CARGO, 1), (ENGINE, 4), (FUEL, 1), (LUMINITE, 25)])
    assemble(app)

    # The launch loads first
    key(app, "P")
    r = wait_rocket(app, lambda r: r["loaded"] == CAPACITY, "the launch")
    app.check(r["phase"] in ("Ignition", "OreLaunchPhaseIgnition") and r["loaded"] == CAPACITY,
              "after the launch the rocket is %s" % r)
    app.check(count(app, LUMINITE) == 0, "the launch left %d Luminite" % count(app, LUMINITE))
    app.check(not r["launch"] and not r["install"]["cargo"] and not r["uninstall"]["engine"],
              "the rocket in flight offers %s" % r)

    # It rises during the ascent
    app.wait_frames(round((IGNITION + 1) * FPS))
    r = rocket(app)
    app.check(r["phase"] in ("Ascent", "OreLaunchPhaseAscent") and r["y"] > BASE_Y,
              "a second into the ascent the rocket is %s" % r)
    p = app.get("rocket", "Position")
    app.check(p["y"] > BASE_Y + 1, "the model is at %s, the rocket at %g" % (p, r["y"]))
    app.check(rocket(app)["shipped"] == 0, "shipped before the arrival")

    # It arrives about 5.25 s after the launch
    app.wait_for("get %s OreRocketState.shipped" % ROCKET, str(CAPACITY),
                 frames=round((FLIGHT - IGNITION - 1 + 0.6) * FPS), what="25 Luminite shipped")
    wait_toast(app, "Shipment away!  +25 Luminite")
    r = rocket(app)
    app.check(parts(r) == (0, 0, 0, 0) and r["loaded"] == 0 and not r["shown"],
              "the pad is not empty: %s" % r)
    app.check(r["phase"] in ("Idle", "OreLaunchPhaseIdle") and r["pad"] not in (0, "0", "#0"),
              "after the arrival the rocket is %s" % r)
    app.check(app.exec(STATE % "Playing") == "true", "the cargo rocket ended the game")
    app.check(not r["aboard"], "the astronaut boarded a cargo rocket")


def test_crewed_launch(app):
    start(app)
    place_pad(app, [(LIFE, 1), (ENGINE, 4), (FUEL, 1), (LUMINITE, 10)])
    r = assemble(app, life=True)
    app.check(r["capacity"] == 0 and r["units"] == 1, "the life support rocket is %s" % r)
    key(app, "P")

    # The astronaut boards: on the pad, hidden, and it cannot walk
    r = wait_rocket(app, lambda r: r["aboard"], "the astronaut aboard")
    x, z = tile_point(*PAD_CENTER)
    p = app.get(PLAYER, "Position")
    app.check(abs(p["x"] - x) < 1e-4 and abs(p["z"] - z) < 1e-4,
              "the astronaut is at %s, not on the pad" % p)
    s = app.get(PLAYER, "Scale")
    app.check(s["x"] < 0.01 and s["y"] < 0.01 and s["z"] < 0.01, "the astronaut shows: %s" % s)
    app.exec("key.down D")
    app.wait_input()
    app.wait_frames(10)
    app.exec("key.up D")
    app.wait_input()
    p = app.get(PLAYER, "Position")
    app.check(abs(p["x"] - x) < 1e-4 and abs(p["z"] - z) < 1e-4,
              "the astronaut aboard walked to %s" % p)
    app.check(count(app, LUMINITE) == 10, "the rocket without bays loaded Luminite")

    app.wait_for(STATE % "Won", "true", frames=round((FLIGHT + 1) * FPS), what="the game won")
    r = rocket(app)
    app.check(r["shipped"] == 0 and r["aboard"], "after the win the rocket is %s" % r)


def test_demolish_pad(app):
    start(app)
    place_pad(app, [(CARGO, 1), (ENGINE, 4), (FUEL, 1), (LUMINITE, 10)])
    assemble(app)
    press(app, ["O"])
    wait_rocket(app, lambda r: r["loaded"] == 10, "10 Luminite loaded")

    # The undamaged pad goes back into the inventory, the parts and the Luminite are lost
    mine(app, *PAD_CENTER)
    app.wait_for("get %s OreRocketState.pad" % ROCKET, lambda out: out in ("0", "#0", ""),
                 frames=round((DOZE_TIME + 1.5) * FPS), what="the pad demolished")
    button(app, "right", False)
    app.wait_frames(SETTLE)
    r = rocket(app)
    app.check(parts(r) == (0, 0, 0, 0) and r["loaded"] == 0 and not r["shown"],
              "the rocket of the demolished pad is %s" % r)
    app.check(count(app, "RocketPad") == 1, "the pad did not come back: %s" % inventory(app))
    app.check(count(app, ENGINE) == 0 and count(app, CARGO) == 0 and count(app, FUEL) == 0 and
              count(app, LUMINITE) == 0, "the parts came back: %s" % inventory(app))
    app.check(not r["install"]["cargo"] and not r["load"],
              "without a pad the rocket offers %s" % r)


def orders(app):
    book = app.get(BOOK, "OreCraftBook")
    return [(o["item"], o["demand"]) for o in book["orders"][:book["count"]]]


def test_craft_keys(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 100), ("items.CopperPlate", 100)])

    # F orders one engine
    key(app, "F")
    app.wait_for("get %s OreCraftBook.count" % BOOK, "1", what="the order of an engine")
    app.check(orders(app) == [(ENGINE, 1)], "the order book holds %s" % orders(app))
    app.check(count(app, "items.IronPlate") == 92 and count(app, "items.CopperPlate") == 96,
              "the engine took %s" % inventory(app))

    # Shift and F order five in one order
    app.exec("key.down ShiftLeft")
    app.wait_input()
    key(app, "F")
    app.exec("key.up ShiftLeft")
    app.wait_input()
    app.wait_for("get %s OreCraftBook.count" % BOOK, "2", what="the order of five engines")
    app.check(orders(app) == [(ENGINE, 1), (ENGINE, 5)], "the order book holds %s" % orders(app))
    app.check(count(app, "items.IronPlate") == 52 and count(app, "items.CopperPlate") == 76,
              "six engines took %s" % inventory(app))

    # G, H and J order the other parts
    set_inventory(app, [("items.IronPlate", 100), ("items.CopperPlate", 100),
                        ("items.Electronics", 4)])
    for k in ["G", "H", "J"]:
        key(app, k)
    app.wait_for("get %s OreCraftBook.count" % BOOK, "5", what="the orders of G, H and J")
    app.check(orders(app)[2:] == [(FUEL, 1), (CARGO, 1), (LIFE, 1)],
              "the order book holds %s" % orders(app))
