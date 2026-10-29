"""Ore Else driven through its command port: the repair drones. A drone pad parks four drones
over its bays; they fly to the nearest damaged building of the map, weld it back to full at
14 HP a second for 1 Iron per 10 HP, never share a target with a drone of their pad, and return
and dock without Iron or in a blackout.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup, but in a 320x180 window without shadows: the
cases play long flights and repairs, which the software renderer keeps up with at that size. A
fixed delta of 1/60 s per frame, waits that count frames. The cases place the buildings directly,
with scripts that create instances of the prefabs with their OreBuilding (as oreelse_power.py
does), damage them by setting their OreHealth and set the inventory through the REST endpoints of
the port. The waves are held off. Every pad gets a solar panel, which covers its 20 W, so there is
no blackout unless a case removes the panel. Times are measured on the play clock (OreClock).
"""
import json
import math
import urllib.error
import urllib.request

from box_client import DELTA_TIME, parse_value

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

# Frames per second of game time
FPS = round(1 / float(DELTA_TIME))
DT = float(DELTA_TIME)

STATE = "has oreelse.OreGameState (oreelse.OreGameState, oreelse.OreGameState.%s)"
CLOCK = "get oreelse.OreClock OreClock.elapsed"
WAVES = "oreelse.OreWaveState"
INVENTORY = "oreelse.OreInventory"
POWER = "oreelse.OrePowerState"
PLAYER = "astronaut"
IRON = "items.Iron"

# The tile grid (cfg.x0, cfg.z0, cfg.tile)
X0 = -47
Z0 = -47
TILE = 2

SLOTS = 15

# The drones of a pad (OreDroneBay of the DronePad) and their constants (cfg.drone*)
DRONES = 4
BAY_RADIUS = 1.4
DOCK = 0.62
SPEED = 7
CRUISE = 2.8
REPAIR = 14
HP_PER_IRON = 10
SCAN = 0.5

# Hit points of a wall (its prefab)
WALL_HP = 300

# Seconds a swing at a building takes (cfg.dozeTime)
DOZE_TIME = 0.8
SWING_SLACK = 1.5

# Frames of play between the two pauses of a measured rate, and its tolerance
MEASURE_FRAMES = 30
RATE_TOLERANCE = 0.05

# Slack in seconds of a time read by polling the clock
TIME_SLACK = 0.5

# Cursor position in points of the 320x180 window: the center hovers a tile next to the
# astronaut
CENTER = (160, 90)

SETTLE = 5


def put(app, entity, component, value=None):
    """Add a component to an entity, or set members of it, through the REST endpoint of the
    port: the members the value leaves out keep their values."""
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
    name = "drones_case_%d" % SCRIPTS[0]
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
    app.exec("set window Surface {width: 320, height: 180}")
    app.exec("set main_view View.shadows.enabled false")
    app.wait_for("get window SurfaceStatus.height", "180")


def start(app):
    """Set up, leave the title screen with a key press and hold the waves off."""
    setup(app)
    app.expect(STATE % "Title", "true")
    app.exec("key.press Space")
    app.wait_input()
    app.wait_for(STATE % "Playing", "true")
    put(app, WAVES, WAVES, {"timer": 100000, "left": 0, "spawn": [0, 0, 0, 0]})


def entity(value):
    """An entity as get prints it, None for no entity."""
    return None if value in (0, "0", "#0", "", None) else value


def clock(app):
    return float(app.exec(CLOCK))


def inventory(app):
    """The slots of the inventory in order: (item, amount), None for a free slot."""
    slots = app.get(INVENTORY, "OreInventory")["slots"]
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
    app.wait_for("get %s OreInventory.slots[0].amount" % INVENTORY,
                 str(stacks[0][1]) if stacks else "0")


def rows_of(app, expr):
    """Run a query of the port: the path and the value of the first field of every result."""
    app.exec("query found %s" % expr)
    app.exec("iter found")
    result = {}
    done = False
    while not done:
        for line in app.exec("next 1000").splitlines():
            done = done or line == "(end)"
            if not line.strip() or line == "(end)":
                continue
            parts = line.split(" ", 1)
            result[parts[0]] = parse_value(parts[1]) if len(parts) > 1 else None
    return result


def tile_point(row, col):
    return X0 + col * TILE, Z0 + row * TILE


def place(app, name, prefab, row, col, w=1, h=1):
    """Place a building directly: an instance of its prefab named name with its top left tile
    at (row, col), standing at the center of its footprint of w by h tiles."""
    x, z = tile_point(row, col)
    x += (w - 1) * TILE / 2
    z += (h - 1) * TILE / 2
    run_script(app, "using box.transform\n%s : %s {\n  Position: {%g, 0, %g}\n"
                    "  oreelse.OreBuilding: {row: %d, col: %d}\n}" %
               (name, prefab, x, z, row, col))
    app.wait_for("has %s oreelse.OreBuilding" % name, "true")


def place_pad(app, name, row, col):
    """Place a drone pad and the solar panel that powers it, on the tile above its top left
    one; wait for its drones."""
    place(app, name, "DronePad", row, col, 2, 2)
    place(app, name + "_panel", "SolarPanel", row - 1, col)
    wait_until(app, lambda: len(drones_of(app, name)) == DRONES, FPS,
               "the %d drones of %s" % (DRONES, name))


def place_wall(app, name, row, col, hp):
    """Place a wall at hp of its hit points."""
    place(app, name, "Wall", row, col)
    put(app, name, "oreelse.OreHealth", {"value": hp})
    app.wait_for("get %s OreHealth.value" % name, lambda out: float(out) == hp)


def health(app, path):
    return app.get(path, "OreHealth")["value"]


def drones(app):
    """The drones: path -> OreDrone, with pad and target as entities."""
    result = rows_of(app, "oreelse.OreDrone")
    for d in result.values():
        d["pad"] = entity(d["pad"])
        d["target"] = entity(d["target"])
    return result


def drones_of(app, pad):
    return {path: d for path, d in drones(app).items() if d["pad"] == pad}


def wait_until(app, condition, frames, what):
    """Poll a condition of the app until it holds; fail after a budget of frames."""
    start = app.frame()
    while not condition():
        if app.frame() - start > frames:
            app.fail("expected %s within %d frames" % (what, frames))


def all_docked(app, pad):
    found = drones_of(app, pad)
    return len(found) == DRONES and all(d["state"] == "Docked" and not d["target"]
                                        for d in found.values())


def check_pad_targets(app, pad):
    """Check that no two drones of a pad share a target; returns their targets."""
    targets = [d["target"] for d in drones_of(app, pad).values() if d["target"]]
    app.check(len(targets) == len(set(targets)),
              "drones of %s share a target: %s" % (pad, targets))
    return targets


def flight_frames(distance):
    """A budget of frames for a flight over a distance, with the time to take off and land."""
    return round((distance / SPEED + 2 * SCAN + 3) * FPS)


def measure(app, value, frames=MEASURE_FRAMES):
    """Play a number of frames between two pauses: the change of a value and the play time it
    took. The game is playing again after."""
    set_state(app, "Paused")
    v0, t0 = value(), clock(app)
    set_state(app, "Playing")
    app.wait_frames(frames)
    set_state(app, "Paused")
    v1, t1 = value(), clock(app)
    set_state(app, "Playing")
    app.check(t1 > t0, "no time passed between the pauses")
    return v1 - v0, t1 - t0


def hover(app):
    return app.get("oreelse.OreHover", "OreHover")


def point(h):
    p = h["point"]
    return (p["x"], p["z"]) if isinstance(p, dict) else (p[0], p[2])


def move_cursor(app, cursor):
    app.exec("mouse.move [%g, %g]" % cursor)
    app.wait_input()
    app.wait_frames(SETTLE)


def place_player(app, x, z):
    app.exec("set %s Position {%g, 0, %g}" % (PLAYER, x, z))
    app.wait_frames(SETTLE)


def aim_tile(app, row, col):
    """Place the astronaut so that the center of the window hovers the center of a tile."""
    move_cursor(app, CENTER)
    p = app.get(PLAYER, "Position")
    hx, hz = point(hover(app))
    x, z = tile_point(row, col)
    place_player(app, x - (hx - p["x"]), z - (hz - p["z"]))
    h = hover(app)
    app.check(h["on_grid"] and (h["row"], h["col"]) == (row, col),
              "the cursor hovers %s, not the tile %s" % (h, (row, col)))


def button(app, name, down):
    app.exec("mouse.button %s %s" % (name, "true" if down else "false"))
    app.wait_input()


def test_pad_spawns_drones(app):
    start(app)
    place_pad(app, "pad_a", 4, 4)

    # Four docked drones on the bays, relative to the pad's center
    found = drones_of(app, "pad_a")
    bays = sorted(d["bay"] for d in found.values())
    app.check(bays == list(range(DRONES)), "the drones sit on the bays %s" % bays)
    for path, d in found.items():
        app.check(d["state"] == "Docked" and not d["target"],
                  "a new drone is %s with the target %s" % (d["state"], d["target"]))
        a = (d["bay"] + 0.5) * 2 * math.pi / DRONES
        p = app.get(path, "Position")
        want = (math.sin(a) * BAY_RADIUS, DOCK, math.cos(a) * BAY_RADIUS)
        app.check(all(abs(g - w) < 1e-4 for g, w in zip((p["x"], p["y"], p["z"]), want)),
                  "drone %d is docked at %s, not %s" % (d["bay"], p, want))
        app.check(app.exec("has %s oreelse.OreHealth" % path) == "false",
                  "the drone %s has hit points" % path)
        app.check(app.exec("has %s (IsA, RepairDrone)" % path) == "true",
                  "the drone %s is no RepairDrone" % path)

    # Nothing to repair: they stay docked
    app.wait_frames(2 * round(SCAN * FPS))
    app.check(all_docked(app, "pad_a"), "drones left the dock with nothing to repair")

    # Demolishing the pad deletes its drones
    aim_tile(app, 4, 4)
    button(app, "right", True)
    app.wait_for("get %s OreInventory" % INVENTORY,
                 lambda out: any(s["item"] == "DronePad" and s["amount"] == 1
                                 for s in parse_value(out)["slots"]),
                 frames=round((DOZE_TIME + SWING_SLACK) * FPS), what="the pad demolished")
    button(app, "right", False)
    wait_until(app, lambda: not drones(app), SETTLE * 2, "the drones deleted with their pad")

    # A pad destroyed by damage takes its drones with it too
    place_pad(app, "pad_b", 4, 8)
    put(app, "pad_b", "oreelse.OreHealth", {"value": 0})
    wait_until(app, lambda: not drones(app), FPS, "the drones deleted with their pad")
    app.check(not rows_of(app, "oreelse.OreBuilding, (IsA, DronePad)"),
              "the destroyed pad stands")


def test_repair_wall(app):
    start(app)
    set_inventory(app, [(IRON, 20)])
    place_pad(app, "pad_a", 4, 4)
    place_wall(app, "wall_a", 8, 8, 200)

    # One drone takes the wall, the others stay docked
    wait_until(app, lambda: any(d["target"] == "wall_a" for d in drones(app).values()),
               2 * round(SCAN * FPS) + SETTLE, "a drone on the wall")
    repairing = [None]

    def welds():
        found = drones_of(app, "pad_a")
        busy = [p for p, d in found.items() if d["target"]]
        app.check(len(busy) == 1, "%d drones have a target, not 1: %s" % (len(busy), found))
        if found[busy[0]]["state"] == "Repairing":
            repairing[0] = busy[0]
        return repairing[0] is not None
    wait_until(app, welds, flight_frames(10), "the drone welding the wall")
    t0, hp0 = clock(app), health(app, "wall_a")
    app.check(hp0 < WALL_HP, "the wall was full before the weld")

    # It welds at 14 HP a second, hovering at cruise height above the wall
    gained, played = measure(app, lambda: health(app, "wall_a"))
    rate = gained / played
    app.check(abs(rate - REPAIR) <= REPAIR * RATE_TOLERANCE,
              "the drone welds %.3f HP/s, not %g" % (rate, REPAIR))
    p = app.get(repairing[0], "Position")
    app.check(abs(p["y"] - CRUISE) < 0.5, "the welding drone flies at %g" % p["y"])

    # Full after (300 - hp0) / 14 s, for 10 Iron
    app.wait_for("get wall_a OreHealth.value", lambda out: float(out) >= WALL_HP,
                 frames=round(((WALL_HP - hp0) / REPAIR + 2) * FPS), what="the wall repaired")
    took = clock(app) - t0
    want = (WALL_HP - hp0) / REPAIR
    app.check(abs(took - want) < TIME_SLACK,
              "the wall took %.3f s from %g HP, not %.3f" % (took, hp0, want))
    app.check(health(app, "wall_a") == WALL_HP, "the wall is at %g" % health(app, "wall_a"))
    app.check(count(app, IRON) == 10, "the repair left %d Iron of 20, not 10" %
              count(app, IRON))

    # Nothing left to repair: back to the dock
    wait_until(app, lambda: all_docked(app, "pad_a"), flight_frames(10), "the drones docked")
    app.check(count(app, IRON) == 10, "Iron went after the repair: %d" % count(app, IRON))


def test_four_walls(app):
    start(app)
    set_inventory(app, [(IRON, 100)])
    place_pad(app, "pad_a", 4, 4)
    walls = ["wall_%d" % i for i in range(4)]
    for name, (row, col) in zip(walls, [(8, 8), (8, 10), (10, 8), (10, 10)]):
        place_wall(app, name, row, col, 100)

    # Four drones weld the four walls at once
    def all_welding():
        found = drones_of(app, "pad_a")
        targets = check_pad_targets(app, "pad_a")
        return (len(targets) == DRONES and set(targets) == set(walls) and
                all(d["state"] == "Repairing" for d in found.values()))
    wait_until(app, all_welding, flight_frames(14), "the four drones welding the four walls")

    # All four walls go up together
    before = [health(app, w) for w in walls]
    app.wait_frames(round(FPS / 2))
    after = [health(app, w) for w in walls]
    app.check(all(a > b for a, b in zip(after, before)),
              "the walls went from %s to %s" % (before, after))

    def all_full():
        check_pad_targets(app, "pad_a")
        return all(health(app, w) >= WALL_HP for w in walls)
    wait_until(app, all_full, round((200 / REPAIR + 3) * FPS), "the four walls repaired")
    app.check(count(app, IRON) == 100 - 4 * 200 // HP_PER_IRON,
              "the repairs left %d Iron of 100, not %d" %
              (count(app, IRON), 100 - 4 * 200 // HP_PER_IRON))
    wait_until(app, lambda: all_docked(app, "pad_a"), flight_frames(14), "the drones docked")


def test_shared_targets(app):
    start(app)
    set_inventory(app, [(IRON, 200)])
    place_pad(app, "pad_a", 4, 4)
    place_pad(app, "pad_b", 4, 10)
    place_wall(app, "wall_a", 10, 7, 30)

    # One drone of every pad goes for the single damaged wall
    def both():
        a = check_pad_targets(app, "pad_a")
        b = check_pad_targets(app, "pad_b")
        return a == ["wall_a"] and b == ["wall_a"]
    wait_until(app, both, 2 * round(SCAN * FPS) + SETTLE, "a drone of each pad on the wall")

    def both_welding():
        both()
        found = [d for d in drones(app).values() if d["target"] == "wall_a"]
        return len(found) == 2 and all(d["state"] == "Repairing" for d in found)
    wait_until(app, both_welding, flight_frames(14), "both drones welding the wall")

    # Two drones weld twice as fast
    gained, played = measure(app, lambda: health(app, "wall_a"))
    rate = gained / played
    app.check(abs(rate - 2 * REPAIR) <= 2 * REPAIR * RATE_TOLERANCE,
              "two drones weld %.3f HP/s, not %g" % (rate, 2 * REPAIR))

    # A second wall: a second drone of each pad, never one already taken by its pad
    place_wall(app, "wall_b", 10, 9, 200)

    def second():
        a = check_pad_targets(app, "pad_a")
        b = check_pad_targets(app, "pad_b")
        return sorted(a) == ["wall_a", "wall_b"] and sorted(b) == ["wall_a", "wall_b"]
    wait_until(app, second, 2 * round(SCAN * FPS) + SETTLE, "two drones of each pad busy")
    for _ in range(10):
        check_pad_targets(app, "pad_a")
        check_pad_targets(app, "pad_b")
        app.wait_frames(SETTLE)


def test_nearest_far(app):
    start(app)
    set_inventory(app, [(IRON, 200)])
    place_pad(app, "pad_a", 4, 4)

    # Two damaged walls far across the map, the corner near the pad's column nearer; paused, so
    # no drone looks before both are damaged
    set_state(app, "Paused")
    place_wall(app, "wall_far", 44, 44, 299)
    place_wall(app, "wall_near", 44, 4, 100)
    set_state(app, "Playing")
    pad_x, pad_z = tile_point(4, 4)
    pad_x += TILE / 2
    pad_z += TILE / 2
    near = math.hypot(tile_point(44, 4)[0] - pad_x, tile_point(44, 4)[1] - pad_z)

    first = [None]

    def taken():
        found = drones_of(app, "pad_a")
        targets = [d["target"] for d in found.values() if d["target"]]
        if targets and first[0] is None:
            first[0] = targets
        return bool(targets)
    wait_until(app, taken, 2 * round(SCAN * FPS) + SETTLE, "a drone with a target")
    app.check("wall_near" in first[0],
              "the first drone out went for %s, not the nearest wall" % first[0])

    # The next drone takes the other one, the nearest left
    wait_until(app, lambda: sorted(check_pad_targets(app, "pad_a")) == ["wall_far", "wall_near"],
               2 * round(SCAN * FPS) + SETTLE, "a second drone on the far wall")

    # It crosses the map and welds the wall
    hp = health(app, "wall_near")
    app.wait_for("get wall_near OreHealth.value", lambda out: float(out) > hp + 1,
                 frames=flight_frames(near), what="the drone welding the near wall")


def test_no_iron_or_blackout(app):
    start(app)
    set_inventory(app, [(IRON, 5)])
    place_pad(app, "pad_a", 4, 4)
    place_wall(app, "wall_a", 10, 10, 50)

    # 5 Iron pay for 50 HP; then the drone returns and docks
    wait_until(app, lambda: count(app, IRON) == 0, flight_frames(14) +
               round(50 / REPAIR * FPS), "the Iron spent")
    wait_until(app, lambda: all_docked(app, "pad_a"), flight_frames(14), "the drones docked")
    hp = health(app, "wall_a")
    app.check(abs(hp - 100) < 1, "5 Iron repaired the wall from 50 to %g" % hp)
    app.wait_frames(2 * round(SCAN * FPS))
    app.check(all_docked(app, "pad_a") and health(app, "wall_a") == hp,
              "without Iron a drone took off or welded")

    # With Iron it resumes
    set_inventory(app, [(IRON, 100)])
    wait_until(app, lambda: any(d["state"] == "Repairing"
                                for d in drones_of(app, "pad_a").values()),
               flight_frames(14), "the drone welding again")
    app.wait_frames(SETTLE)
    app.check(health(app, "wall_a") > hp, "the wall stayed at %g" % hp)

    # A blackout sends it home
    app.exec("rm pad_a_panel")
    app.wait_for("get %s OrePowerState.blackout" % POWER, "true", what="the blackout")
    wait_until(app, lambda: all_docked(app, "pad_a"), flight_frames(14),
               "the drones docked in the blackout")
    hp = health(app, "wall_a")
    iron = count(app, IRON)
    app.wait_frames(2 * round(SCAN * FPS))
    app.check(all_docked(app, "pad_a") and health(app, "wall_a") == hp and
              count(app, IRON) == iron, "in a blackout a drone took off or welded")

    # The power back, it resumes and finishes the wall
    place(app, "panel_b", "SolarPanel", 3, 5)
    app.wait_for("get %s OrePowerState.blackout" % POWER, "false", what="the power back")
    app.wait_for("get wall_a OreHealth.value", lambda out: float(out) >= WALL_HP,
                 frames=flight_frames(14) + round((WALL_HP - hp) / REPAIR * FPS),
                 what="the wall repaired")
