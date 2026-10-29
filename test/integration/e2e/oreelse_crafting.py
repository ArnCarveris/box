"""Ore Else driven through its command port: the order book. Orders paid when placed, planned
down to the raw ore with a pool per order, the workers that craft them oldest order first,
cancel and refunds, the limits, OreCanOrder on the item prefabs and the raw ore of OreNeeds.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta
of 1/60 s per frame, waits that count frames. The cases set up the inventory directly and call
oreelse.craft_order() and oreelse.craft_cancel() from scripts, which store the result on an
entity of their own. The astronaut is the worker; the cases pause it while they look at a plan
and speed it up (OreCrafter.speed) so that a chain of crafts takes few frames.
"""
import json
import urllib.error
import urllib.request

from box_client import DELTA_TIME

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

# Frames per second of game time
FPS = round(1 / float(DELTA_TIME))

STATE = "has oreelse.OreGameState (oreelse.OreGameState, oreelse.OreGameState.%s)"
TOAST = "get oreelse.OreToast OreToast.text"
CLOCK = "get oreelse.OreClock OreClock.elapsed"
PLAYER = "astronaut"
INVENTORY = "oreelse.OreInventory"
BOOK = "oreelse.OreCraftBook"
NEEDS = "oreelse.OreNeeds"

SLOTS = 15
NO_ROOM = "No room for the refund"

# Craft times in seconds (OreRecipe of resources.flecs)
PLATE_TIME = 0.5
ENGINE_TIME = 30

# The speed of a fast worker: a plate craft takes one frame of work, an engine 30
FAST = 60

# Item kinds besides iron, copper, the plates and the engine, for a full inventory: the other
# items and three prefabs of the scene, since the inventory holds any entity
OTHERS = ["items.Stone", "items.Luminite", "items.StoneBrick", "items.Wire",
          "items.Electronics", "items.RocketCargoBay", "items.RocketLifeSupport",
          "items.RocketFuelStorage", "TileMarker", "TileMarkerBad", "Astronaut"]

ENGINE = "items.RocketEngine"

# Frames a case waits for a chain of fast crafts
CRAFT_FRAMES = 4 * FPS
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
    """Run a script through the REST endpoint of the port, a new script entity every time;
    returns the number of the script."""
    SCRIPTS[0] += 1
    name = "crafting_case_%d" % SCRIPTS[0]
    request = urllib.request.Request(app.url("script/" + name, {"code": code}), method="PUT")
    try:
        with urllib.request.urlopen(request, timeout=10) as r:
            status, body = r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read().decode(errors="replace")
    app.check(status == 200 and '"error"' not in body,
              "the script %r failed (%d): %s" % (code, status, body))
    return SCRIPTS[0]


def order(app, item, count=1):
    """Order units of an item with oreelse.craft_order(); returns the units ordered."""
    n = SCRIPTS[0] + 1
    run_script(app, "craft_result_%d { oreelse.OreStack: {item: %s, amount: "
                    "oreelse.craft_order(%s, %d)} }" % (n, item, item, count))
    return app.get("craft_result_%d" % n, "OreStack")["amount"]


def cancel(app, order_id):
    """Take one unit off an order with oreelse.craft_cancel(); returns whether it did."""
    n = SCRIPTS[0] + 1
    run_script(app, "craft_result_%d { oreelse.OreCanOrder: {value: "
                    "oreelse.craft_cancel(%d)} }" % (n, order_id))
    return app.get("craft_result_%d" % n, "OreCanOrder")["value"]


def inventory_add(app, item, amount):
    run_script(app, "const ok: bool = oreelse.inventory_add(%s, %d)" % (item, amount))


def inventory_remove(app, item, amount):
    run_script(app, "const ok: bool = oreelse.inventory_remove(%s, %d)" % (item, amount))


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


def start(app):
    """Set up, leave the title screen with a key press and pause the astronaut's crafting."""
    setup(app)
    app.expect(STATE % "Title", "true")
    app.exec("key.press Space")
    app.wait_input()
    app.wait_for(STATE % "Playing", "true")
    worker(app, PLAYER, speed=1, paused=True)


def worker(app, path, speed=1, paused=False):
    """Set the crafter of an idle worker, creating the entity when it does not exist."""
    app.exec("touch %s" % path)
    app.exec("set %s oreelse.OreCrafter {speed: %g, paused: %s}" %
             (path, speed, "true" if paused else "false"))


def resume(app, speed, path=PLAYER):
    """Set the speed of a worker and let it work; it keeps its craft (set assigns only the
    members it names)."""
    app.exec("set %s OreCrafter {speed: %g, paused: false}" % (path, speed))


def crafter(app, path=PLAYER):
    c = app.get(path, "OreCrafter")
    c["item"] = entity(c["item"])
    return c


def entity(value):
    """An entity as get prints it, None for no entity."""
    return None if value in (0, "0", "#0", "", None) else value


def inventory(app):
    """The slots of the inventory in order: (item, amount), None for a free slot."""
    slots = app.get(INVENTORY, "OreInventory")["slots"]
    return [(entity(s["item"]), s["amount"]) if entity(s["item"]) else None for s in slots]


def held(app):
    """What the inventory holds by item."""
    return {s[0]: s[1] for s in inventory(app) if s}


def count(app, item):
    return held(app).get(item, 0)


def set_inventory(app, stacks):
    """Set every slot of the inventory to the stacks in order."""
    slots = [{"item": s[0], "amount": s[1]} for s in stacks]
    slots += [{"item": 0, "amount": 0}] * (SLOTS - len(slots))
    put(app, INVENTORY, INVENTORY, {"slots": slots})
    app.wait_for("get %s OreInventory.slots[0].amount" % INVENTORY,
                 str(stacks[0][1]) if stacks else "0")


def book(app):
    return app.get(BOOK, "OreCraftBook")


def orders(app):
    """The orders oldest first, each with its entries and pool cut to their counts."""
    b = book(app)
    result = []
    for o in b["orders"][:b["count"]]:
        o["entries"] = o["entries"][:o["entry_count"]]
        o["pool"] = {s["item"]: s["amount"] for s in o["pool"]["stacks"][:o["pool"]["count"]]}
        result.append(o)
    return result


def rows(app):
    """The rows of the queue panel: (order, item, sub)."""
    b = book(app)
    return [(r["order"], r["item"], r["sub"]) for r in b["rows"][:b["row_count"]]]


def plan(o):
    """The entries of an order: (item, crafts left plus running)."""
    return [(e["item"], e["count"]) for e in o["entries"]]


def only_order(app):
    found = orders(app)
    app.check(len(found) == 1, "the order book holds %d orders, not 1: %s" % (len(found), found))
    return found[0]


def can_order(app, item):
    return app.exec("get %s OreCanOrder.value" % item) == "true"


def test_order_from_ore(app):
    start(app)
    set_inventory(app, [("items.Iron", 16), ("items.Copper", 8)])
    app.check(order(app, ENGINE) == 1, "the engine was not ordered")
    app.check(held(app) == {}, "the order did not take the ore at once: %s" % held(app))

    o = only_order(app)
    app.check(o["item"] == ENGINE and o["demand"] == 1, "the order is %s" % o)
    app.check(plan(o) == [("items.IronPlate", 8), ("items.CopperPlate", 4), (ENGINE, 1)],
              "the order plans %s, not the plates ahead of the engine" % plan(o))
    app.check(o["pool"] == {"items.Iron": 16, "items.Copper": 8},
              "the pool of the order holds %s" % o["pool"])
    app.check(rows(app) == [(o["id"], ENGINE, False), (o["id"], "items.IronPlate", True),
                            (o["id"], "items.CopperPlate", True)],
              "the queue rows are %s" % rows(app))

    clock = float(app.exec(CLOCK))
    resume(app, FAST)
    app.wait_for("get %s OreInventory.slots[0].item" % INVENTORY, ENGINE,
                 frames=CRAFT_FRAMES, what="the engine in the inventory")
    took = float(app.exec(CLOCK)) - clock
    work = (12 * PLATE_TIME + ENGINE_TIME) / FAST
    app.check(took >= work - 0.05, "the engine came after %g s, not after the %g s of its "
              "crafts" % (took, work))
    app.check(held(app) == {ENGINE: 1}, "the inventory holds %s" % held(app))
    app.wait_for("get %s OreCraftBook.count" % BOOK, "0", what="the order removed")


def test_borrow_parts(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 8), ("items.CopperPlate", 4)])
    app.check(order(app, ENGINE) == 1, "the engine was not ordered")
    o = only_order(app)
    app.check(plan(o) == [(ENGINE, 1)], "with the plates held the order plans %s" % plan(o))
    app.check(o["pool"] == {"items.IronPlate": 8, "items.CopperPlate": 4},
              "the pool holds %s, not the plates" % o["pool"])
    app.check(held(app) == {}, "the plates stayed in the inventory: %s" % held(app))
    app.check(rows(app) == [(o["id"], ENGINE, False)], "the queue rows are %s" % rows(app))

    # Plates already held are used before new ones are crafted from ore
    set_inventory(app, [("items.IronPlate", 3), ("items.Iron", 12), ("items.Copper", 8)])
    app.check(order(app, ENGINE) == 1, "the second engine was not ordered")
    o = orders(app)[1]
    app.check(plan(o) == [("items.IronPlate", 5), ("items.CopperPlate", 4), (ENGINE, 1)],
              "with 3 plates held the order plans %s" % plan(o))
    app.check(o["pool"] == {"items.IronPlate": 3, "items.Iron": 10, "items.Copper": 8},
              "the pool holds %s" % o["pool"])
    app.check(held(app) == {"items.Iron": 2}, "the inventory holds %s" % held(app))


def test_refused_and_partial(app):
    start(app)
    stacks = [("items.Iron", 15), ("items.Copper", 8)]
    set_inventory(app, stacks)
    app.check(order(app, ENGINE) == 0, "an engine was ordered with 15 iron")
    app.check(inventory(app)[:2] == stacks, "the refused order changed the inventory: %s" %
              held(app))
    app.check(orders(app) == [], "the refused order is in the book")

    set_inventory(app, [("items.IronPlate", 26), ("items.CopperPlate", 13)])
    app.check(order(app, ENGINE, 5) == 3, "five engines with plates for three did not order 3")
    o = only_order(app)
    app.check(o["demand"] == 3 and plan(o) == [(ENGINE, 3)],
              "the order is %s, not one order of three" % o)
    app.check(o["pool"] == {"items.IronPlate": 24, "items.CopperPlate": 12},
              "the pool holds %s" % o["pool"])
    app.check(held(app) == {"items.IronPlate": 2, "items.CopperPlate": 1},
              "the inventory holds %s" % held(app))


def test_orders_oldest_first(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 16), ("items.CopperPlate", 8)])
    app.check(order(app, ENGINE) == 1 and order(app, ENGINE) == 1, "two orders were refused")
    first, second = orders(app)
    app.check(first["id"] != second["id"], "the orders share the id %d" % first["id"])
    for o in (first, second):
        app.check(o["pool"] == {"items.IronPlate": 8, "items.CopperPlate": 4},
                  "order %d holds %s, not parts of its own" % (o["id"], o["pool"]))

    app.exec("set %s OreCrafter.paused false" % PLAYER)
    app.wait_for("get %s OreCrafter.order" % PLAYER, str(first["id"]),
                 what="the worker on the first order")
    first, second = orders(app)
    app.check(first["pool"] == {} and second["pool"] == {"items.IronPlate": 8,
                                                         "items.CopperPlate": 4},
              "the craft took its inputs from %s and %s" % (first["pool"], second["pool"]))
    app.check(second["entries"][0]["running"] == 0, "the second order started too")

    # Skip to the end of the first engine: then the worker starts the second order
    app.exec("set %s OreCrafter.left 0.05" % PLAYER)
    app.wait_for("get %s OreCrafter.order" % PLAYER, str(second["id"]),
                 frames=SETTLE * 4, what="the worker on the second order")
    app.check(count(app, ENGINE) == 1, "the first engine was not delivered")
    app.check([o["id"] for o in orders(app)] == [second["id"]], "the first order stayed")


def test_two_workers(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 24), ("items.CopperPlate", 12)])
    app.check(order(app, ENGINE, 2) == 2, "two engines were not ordered")
    app.check(order(app, ENGINE) == 1, "the second order was not placed")
    first, second = orders(app)
    worker(app, "worker2")
    app.exec("set %s OreCrafter.paused false" % PLAYER)
    app.wait_for("get worker2 OreCrafter.order", str(first["id"]),
                 what="the second worker on the oldest order")
    app.wait_for("get %s OreCrafter.order" % PLAYER, str(first["id"]),
                 what="the astronaut on the oldest order")
    first, second = orders(app)
    app.check(first["entries"][0]["running"] == 2 and first["entries"][0]["active"],
              "the oldest order's engine runs %s" % first["entries"][0])
    app.check(second["entries"][0]["running"] == 0, "a worker went to the second order")


def test_spare_wire(app):
    start(app)
    set_inventory(app, [("items.Copper", 4), ("items.Wire", 1)])
    app.check(order(app, "items.Electronics") == 1, "the electronics were not ordered")
    o = only_order(app)
    app.check(plan(o) == [("items.CopperPlate", 2), ("items.Wire", 1),
                          ("items.Electronics", 1)],
              "the electronics plan %s" % plan(o))
    app.check(o["pool"] == {"items.Copper": 4, "items.Wire": 1}, "the pool holds %s" % o["pool"])
    app.check(held(app) == {}, "the inventory holds %s" % held(app))

    resume(app, FAST)
    app.wait_for("get %s OreCraftBook.count" % BOOK, "0", frames=CRAFT_FRAMES,
                 what="the order completed")
    app.check(held(app) == {"items.Electronics": 1, "items.Wire": 1},
              "the spare wire did not come back: %s" % held(app))


def test_cancel(app):
    start(app)
    set_inventory(app, [("items.Iron", 48), ("items.Copper", 24)])
    app.check(order(app, ENGINE, 3) == 3, "three engines were not ordered")
    o = only_order(app)

    # A slow worker keeps its first plate craft in progress
    resume(app, 0.01)
    app.wait_for("get %s OreCrafter.item" % PLAYER, "items.IronPlate",
                 what="a plate craft in progress")

    app.check(cancel(app, o["id"]), "the first cancel was refused")
    o = only_order(app)
    app.check(o["demand"] == 2, "after a cancel of three the order holds %d" % o["demand"])
    app.check(held(app) == {"items.Iron": 16, "items.Copper": 8},
              "the first cancel refunded %s" % held(app))

    app.check(cancel(app, o["id"]), "the second cancel was refused")
    app.check(only_order(app)["demand"] == 1, "the order does not hold 1")
    app.check(held(app) == {"items.Iron": 32, "items.Copper": 16},
              "after two cancels the inventory holds %s" % held(app))

    app.check(cancel(app, o["id"]), "the last cancel was refused")
    app.check(orders(app) == [], "the order stayed after its last cancel")
    app.check(held(app) == {"items.Iron": 48, "items.Copper": 24},
              "the last cancel refunded %s, not every borrowed material" % held(app))
    app.check(crafter(app)["item"] is None, "the worker kept its craft: %s" % crafter(app))
    app.check(not cancel(app, o["id"]), "a removed order was cancelled")


def test_cancel_no_room(app):
    start(app)
    others = [(kind, 1) for kind in OTHERS + ["items.RocketEngine"]]
    set_inventory(app, [("items.Iron", 16), ("items.Copper", 8)] + others)
    app.check(order(app, ENGINE) == 1, "the engine was not ordered")
    inventory_add(app, "items.IronPlate", 1)
    inventory_add(app, "items.CopperPlate", 1)
    before = held(app)
    app.check(len(before) == 14, "the inventory holds %d kinds, not 14" % len(before))

    o = only_order(app)
    app.check(not cancel(app, o["id"]), "a cancel whose refund does not fit went through")
    app.check(app.exec(TOAST) == '"%s"' % NO_ROOM, "the toast is %s" % app.exec(TOAST))
    app.check(held(app) == before, "the refused cancel changed the inventory: %s" % held(app))
    after = only_order(app)
    app.check(after["demand"] == 1 and after["pool"] == o["pool"],
              "the refused cancel changed the order: %s" % after)

    inventory_remove(app, "items.Stone", 1)
    app.check(cancel(app, o["id"]), "the cancel was refused with room for the refund")
    app.check(count(app, "items.Iron") == 16 and count(app, "items.Copper") == 8,
              "the refund is not in the inventory: %s" % held(app))


def test_full_inventory_blocks_start(app):
    start(app)
    others = [(kind, 1) for kind in ["items.Iron", "items.Copper"] + OTHERS]
    set_inventory(app, [("items.IronPlate", 9), ("items.CopperPlate", 5)] + others)
    app.check(order(app, ENGINE) == 1, "the engine was not ordered")
    app.check(len(held(app)) == SLOTS, "the inventory holds %d kinds" % len(held(app)))

    resume(app, FAST)
    app.wait_frames(SETTLE * 2)
    app.check(crafter(app)["item"] is None, "the engine started with 15 kinds held")
    o = only_order(app)
    app.check(o["stalled"] and o["entries"][0]["full"],
              "the order is not stalled by the full inventory: %s" % o)
    header = book(app)["rows"][0]
    app.check(header["stalled"] and header["full"], "the header row is %s" % header)

    inventory_remove(app, "items.Stone", 1)
    app.wait_for("get %s OreCraftBook.count" % BOOK, "0", frames=CRAFT_FRAMES,
                 what="the engine crafted once a slot freed")
    app.check(count(app, ENGINE) == 1, "the engine is not in the inventory: %s" % held(app))


def test_delivery_blocked(app):
    start(app)
    others = [(kind, 1) for kind in ["items.Iron", "items.Copper"] + OTHERS[:-1]]
    set_inventory(app, [("items.IronPlate", 8), ("items.CopperPlate", 4)] + others)
    app.check(order(app, ENGINE) == 1, "the engine was not ordered")
    app.exec("set %s OreCrafter.paused false" % PLAYER)
    app.wait_for("get %s OreCrafter.item" % PLAYER, ENGINE, what="the engine started")

    for kind in ["items.IronPlate", "items.CopperPlate", "Astronaut"]:
        inventory_add(app, kind, 1)
    app.check(len(held(app)) == SLOTS, "the inventory holds %d kinds" % len(held(app)))
    app.exec("set %s OreCrafter.left 0.05" % PLAYER)
    app.wait_for("get %s OreCrafter.left" % PLAYER, "0", what="the engine finished")
    app.wait_frames(SETTLE)

    c = crafter(app)
    app.check(c["item"] == ENGINE, "the worker does not hold the engine: %s" % c)
    app.check(count(app, ENGINE) == 0, "the engine was delivered to a full inventory")
    o = only_order(app)
    app.check(o["stalled"] and o["entries"][0]["full"] and o["demand"] == 1,
              "the order is not stalled on the delivery: %s" % o)

    inventory_remove(app, "items.Stone", 1)
    app.wait_for("get %s OreCraftBook.count" % BOOK, "0", what="the engine delivered")
    app.check(count(app, ENGINE) == 1, "the engine is not in the inventory: %s" % held(app))


def test_worker_deleted(app):
    start(app)
    set_inventory(app, [("items.IronPlate", 8), ("items.CopperPlate", 4)])
    app.check(order(app, ENGINE) == 1, "the engine was not ordered")
    worker(app, "worker2")
    app.wait_for("get worker2 OreCrafter.item", ENGINE, what="the second worker on the engine")
    o = only_order(app)
    app.check(o["pool"] == {} and o["entries"][0]["running"] == 1,
              "the craft did not take its inputs: %s" % o)

    app.exec("rm worker2")
    app.wait_frames(SETTLE)
    o = only_order(app)
    app.check(o["pool"] == {"items.IronPlate": 8, "items.CopperPlate": 4},
              "the deleted worker returned %s" % o["pool"])
    app.check(o["entries"][0]["running"] == 0, "the engine still runs: %s" % o["entries"])

    resume(app, FAST)
    app.wait_for("get %s OreCraftBook.count" % BOOK, "0", frames=CRAFT_FRAMES,
                 what="the astronaut completed the engine")
    app.check(count(app, ENGINE) == 1, "the engine is not in the inventory: %s" % held(app))


def test_ninth_order(app):
    start(app)
    set_inventory(app, [("items.CopperPlate", 9)])
    for i in range(8):
        app.check(order(app, "items.Wire") == 1, "order %d was refused" % (i + 1))
    app.wait_for("get items.Wire OreCanOrder.value", "false",
                 what="wire not orderable with 8 orders")
    app.check(order(app, "items.Wire") == 0, "a ninth order was placed")
    app.check(len(orders(app)) == 8, "the book holds %d orders" % len(orders(app)))
    app.check(held(app) == {"items.CopperPlate": 1}, "the inventory holds %s" % held(app))


def test_can_order(app):
    start(app)
    set_inventory(app, [])
    app.wait_for("get %s OreCanOrder.value" % ENGINE, "false", what="no engine from nothing")
    app.check(not can_order(app, "items.Electronics"), "electronics orderable from nothing")

    set_inventory(app, [("items.Iron", 16), ("items.Copper", 8)])
    app.wait_for("get %s OreCanOrder.value" % ENGINE, "true", what="an engine from its ore")
    app.check(can_order(app, "items.Electronics"), "electronics not orderable with 8 copper")
    app.check(not can_order(app, "items.RocketLifeSupport"), "life support orderable")

    set_inventory(app, [("items.Iron", 16), ("items.Copper", 7)])
    app.wait_for("get %s OreCanOrder.value" % ENGINE, "false",
                 what="no engine with a copper missing")


def needs(app):
    n = app.get(NEEDS, "OreNeeds")
    return [(o["item"], o["have"], o["need"]) for o in n["ores"][:n["count"]]]


def test_needs(app):
    start(app)
    set_inventory(app, [("items.Iron", 10), ("items.IronPlate", 2)])
    put(app, NEEDS, NEEDS, {"item": ENGINE})
    app.wait_for("get %s OreNeeds.count" % NEEDS, "2", what="the needs of the engine")
    app.check(needs(app) == [("items.Iron", 14, 16), ("items.Copper", 0, 8)],
              "the engine needs %s, not Iron 14/16 and Copper 0/8" % needs(app))

    set_inventory(app, [("items.CopperPlate", 4), ("items.Iron", 20)])
    app.wait_for("get %s OreNeeds.ores[1].have" % NEEDS, "8", what="the copper plates counted")
    app.check(needs(app) == [("items.Iron", 16, 16), ("items.Copper", 8, 8)],
              "with parts in stock the engine needs %s" % needs(app))

    set_inventory(app, [])
    put(app, NEEDS, NEEDS, {"item": "items.Electronics"})
    app.wait_for("get %s OreNeeds.count" % NEEDS, "1", what="the needs of electronics")
    app.check(needs(app) == [("items.Copper", 0, 4)],
              "electronics need %s, not Copper 0/4" % needs(app))
