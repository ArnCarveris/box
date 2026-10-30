"""Ore Else driven through its command port in the stress test mode (--stresstest[=N]): the game
starts playing a late game base on the map of the seed, a drill on every deposit, two wall rings,
the turret ring, a rocket pad, two factories and two drone pads near their tiles and a solar farm
sized to the demand, the stock added to the starting crate, the clock at 30 minutes (N with
--stresstest=N) and the next wave 10 s away. A restart builds the same layout on the new map.

Every case runs in a fresh apps/oreelse process with --stresstest and a fixed session seed, like
oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta of
1/60 s per frame, waits that count frames. The case pauses the game while it reads the base, and
shortens the 10 s to the next wave through the REST endpoint of the port once it checked them.
"""
from box_client import App
from oreelse_critters import (
    FPS, STATE, WAVE, clock, critters, entity, put_waves, rows_of, set_state, setup, wait_until,
    waves)

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20", "--stresstest"]

POWER = "oreelse.OrePowerState"
INVENTORY = "oreelse.OreInventory"
PLAYER = "astronaut"

# The tile grid (cfg.rows, cfg.x0, cfg.z0, cfg.tile)
ROWS = 48
X0 = -47
Z0 = -47
TILE = 2

# The layout (cfg.stressEdge, cfg.stressFar, cfg.stressCenter, cfg.stressSearch,
# cfg.stressPowerMargin, cfg.stressSolarInset)
EDGE = 10
FAR = 37
CENTER = 24
SEARCH = 8
MARGIN = 1.15
SOLAR_INSET = 3
TURRETS = ["GunTurret", "LaserTurret", "GunTurret", "MissileTurret"]

# The buildings near a tile: prefab, tile the search starts at, how many
SITES = [("RocketPad", (29, 23), 1), ("Factory", (20, 20), 1), ("Factory", (20, 27), 1),
         ("DronePad", (27, 28), 1), ("DronePad", (23, 18), 1)]

# Footprints and watts of the buildings (their prefabs): + produces, - consumes
FOOTPRINT = {"Factory": 2, "DronePad": 2, "RocketPad": 3}
WATTS = {"SolarPanel": 20, "Drill": -4, "GunTurret": 0, "LaserTurret": -25,
         "MissileTurret": -20, "Factory": -75, "DronePad": -20, "Wall": 0, "RocketPad": 0}

# The starting crate and the stock the stress test adds to it
CRATE = {"items.IronPlate": 16, "items.CopperPlate": 12, "SolarPanel": 1}
STOCK = {"items.Iron": 500, "items.Copper": 500, "items.Stone": 500, "items.Luminite": 200,
         "items.IronPlate": 200, "items.CopperPlate": 200, "items.StoneBrick": 200, "Wall": 40,
         "Drill": 6, "SolarPanel": 10, "GunTurret": 6, "LaserTurret": 4, "MissileTurret": 4}
ORES = ["items.Iron", "items.Copper", "items.Stone", "items.Luminite"]

# The clock and the waves (cfg.stressMinutes, cfg.firstWaveAt, cfg.waveGap,
# cfg.stressWaveLead, cfg.waveBudgetBase, cfg.waveBudgetGrowth)
MINUTES = 30
FIRST_WAVE = 90
GAP = 50
LEAD = 10
BUDGET_BASE = 2
BUDGET_GROWTH = 1.3


def key(app, name):
    app.exec("key_press %s" % name)
    app.wait_input()


def wave_of(minutes):
    """The wave counter of a stress test started at a number of minutes."""
    return max(0, (minutes * 60 - FIRST_WAVE) // GAP)


def ring(lo, hi):
    """The tiles of the perimeter of the square of rows and columns lo to hi."""
    return {(r, c) for r in range(lo, hi + 1) for c in range(lo, hi + 1)
            if r in (lo, hi) or c in (lo, hi)}


def ring_turrets(taken):
    """The turret of every tile of the turret ring, placed as the original's oreStressRing does
    on the tiles not taken before it: step i places on the top row from the left, the bottom row
    from the right, the left column from the bottom and the right column from the top."""
    lo, hi = EDGE + 2, FAR - 2
    taken = set(taken)
    result = {}
    for n, i in enumerate(range(lo, hi + 1)):
        for k, tile in enumerate(((lo, i), (hi, hi - (i - lo)), (hi - (i - lo), lo), (i, hi))):
            if tile not in taken:
                taken.add(tile)
                result[tile] = TURRETS[(n + k) % len(TURRETS)]
    return result


def placed(app):
    """The placed buildings: prefab -> top left tiles, and tile -> prefab for every tile."""
    by_prefab, tiles = {}, {}
    for prefab in WATTS:
        found = rows_of(app, "oreelse.OreBuilding, (IsA, %s)" % prefab).values()
        by_prefab[prefab] = sorted((b["row"], b["col"]) for b in found)
        size = FOOTPRINT.get(prefab, 1)
        for row, col in by_prefab[prefab]:
            for r in range(row, row + size):
                for c in range(col, col + size):
                    app.check((r, c) not in tiles, "%s and %s share the tile %s" %
                              (prefab, tiles.get((r, c)), (r, c)))
                    tiles[(r, c)] = prefab
    return by_prefab, tiles


def inventory(app):
    slots = app.get(INVENTORY, "oreelse.OreInventory")["slots"]
    return {entity(s["item"]): s["amount"] for s in slots if entity(s["item"])}


def check_base(app):
    """The base of the stress test on the current map, read while paused: returns the tiles of
    its deposits."""
    deposits = {(d["row"], d["col"]) for d in rows_of(app, "oreelse.OreDeposit").values()}
    by_prefab, tiles = placed(app)

    # A drill on every deposit and nowhere else
    app.check(set(by_prefab["Drill"]) == deposits,
              "the drills stand on %s, the deposits on %s" % (by_prefab["Drill"], deposits))

    # The two wall rings complete; no deposit lies on them on this map
    walls = ring(EDGE, FAR) | ring(EDGE + 1, FAR - 1)
    app.check(not walls & deposits, "a deposit lies on the wall rings: %s" % (walls & deposits))
    app.check(set(by_prefab["Wall"]) == walls, "%d walls, not the %d of the two rings: "
              "missing %s, extra %s" % (len(by_prefab["Wall"]), len(walls),
                                        sorted(walls - set(by_prefab["Wall"])),
                                        sorted(set(by_prefab["Wall"]) - walls)))

    # The turret ring complete, cycling gun, laser, gun and missile turrets, a drill on any of
    # its tiles with a deposit
    expected = ring_turrets(ring(EDGE + 2, FAR - 2) & deposits)
    for tile in ring(EDGE + 2, FAR - 2):
        want = expected.get(tile, "Drill")
        app.check(tiles.get(tile) == want, "the turret ring holds %s at %s, not %s" %
                  (tiles.get(tile), tile, want))
    count = sum(len(by_prefab[t]) for t in set(TURRETS))
    app.check(count == len(expected), "%d turrets, not the %d of the ring" %
              (count, len(expected)))

    # The rocket pad, the factories and the drone pads near their tiles
    for prefab in ("RocketPad", "Factory", "DronePad"):
        sites = [s for s in SITES if s[0] == prefab]
        app.check(len(by_prefab[prefab]) == sum(s[2] for s in sites), "%d of %s placed: %s" %
                  (len(by_prefab[prefab]), prefab, by_prefab[prefab]))
        for _, (row, col), _ in sites:
            app.check(any(max(abs(r - row), abs(c - col)) <= SEARCH
                          for r, c in by_prefab[prefab]),
                      "no %s within %d of %s: %s" % (prefab, SEARCH, (row, col),
                                                    by_prefab[prefab]))

    # The solar panels on the rings from the inset on, enough for the demand and at most one
    # step of four panels more
    lo, hi = SOLAR_INSET, ROWS - 1 - SOLAR_INSET
    for r, c in by_prefab["SolarPanel"]:
        app.check(lo <= r <= hi and lo <= c <= hi, "a solar panel at %s is outside %d..%d" %
                  ((r, c), lo, hi))
    raw = -sum(WATTS[p] * len(by_prefab[p]) for p in WATTS if WATTS[p] < 0)
    production = WATTS["SolarPanel"] * len(by_prefab["SolarPanel"])
    app.check(MARGIN * raw <= production < MARGIN * raw + 4 * WATTS["SolarPanel"],
              "%d W of panels for %d W of consumers" % (production, raw))
    app.wait_for("get %s oreelse.OrePowerState.production" % POWER,
                 lambda out: float(out) > 0, what="the production of the panels")
    power = app.get(POWER, "oreelse.OrePowerState")
    app.check(abs(power["production"] - production) < 1e-3 and not power["blackout"] and
              power["production"] >= MARGIN * raw - 1e-3,
              "the power is %s for %d W of consumers" % (power, raw))

    # The astronaut on the free tile nearest to the center
    pos = app.get(PLAYER, "box.transform.Position")
    tile = (round((pos["z"] - Z0) / TILE), round((pos["x"] - X0) / TILE))
    free = [(r, c) for r in range(ROWS) for c in range(ROWS)
            if (r, c) not in tiles and (r, c) not in deposits]
    nearest = min(max(abs(r - CENTER), abs(c - CENTER)) for r, c in free)
    app.check(tile in free and max(abs(tile[0] - CENTER), abs(tile[1] - CENTER)) == nearest,
              "the astronaut stands at %s, not on a free tile %d from the center" %
              (tile, nearest))
    return deposits


def check_stock(app):
    """The starting crate with the stock added; the drills may have mined some ore since."""
    held = inventory(app)
    for item in set(CRATE) | set(STOCK):
        want = CRATE.get(item, 0) + STOCK.get(item, 0)
        ok = held.get(item, 0) >= want if item in ORES else held.get(item, 0) == want
        app.check(ok, "the inventory holds %s of %s, not %d" % (held.get(item, 0), item, want))


def check_clock(app, minutes):
    """The clock at the minutes and the next wave LEAD seconds after them."""
    w = waves(app)
    played = clock(app) - minutes * 60
    app.check(0 <= played < LEAD, "the clock is at %g, not %d minutes" %
              (clock(app), minutes))
    app.check(w["wave"] == wave_of(minutes) and w["left"] == 0,
              "the waves are at %s, not wave %d" % (w, wave_of(minutes)))
    app.check(abs(w["timer"] - (LEAD - played)) < 1e-3 and abs(w["gap"] - LEAD) < 1e-4,
              "%g s after the start the next wave is %g s away of %g" %
              (played, w["timer"], w["gap"]))


def begin(app):
    """Set up, wait for the game to play and pause it."""
    setup(app)
    app.wait_for(STATE % "Playing", "true")
    app.wait_for("get %s oreelse.OrePowerState.production" % POWER,
                 lambda out: float(out) > 0, what="the power of the base")
    set_state(app, "Paused")


def test_base(app):
    begin(app)
    check_base(app)
    check_stock(app)
    check_clock(app, MINUTES)


def test_next_wave(app):
    begin(app)
    check_clock(app, MINUTES)

    # The timer counts down from 10 s; shortened, the wave after 34 starts and its critters
    # spawn
    wave = wave_of(MINUTES)
    app.check(wave == 34, "the wave counter is %d, not 34" % wave)
    before = waves(app)["timer"]
    set_state(app, "Playing")
    app.wait_frames(FPS // 2)
    set_state(app, "Paused")
    app.check(waves(app)["timer"] < before, "the timer to the next wave did not count down")
    put_waves(app, timer=0.05)
    set_state(app, "Playing")
    app.wait_for(WAVE % "wave", str(wave + 1), frames=FPS, what="wave %d" % (wave + 1))
    wait_until(app, lambda: bool(critters(app)), FPS, "the critters of wave %d" % (wave + 1))
    set_state(app, "Paused")
    w = waves(app)
    budget = BUDGET_BASE * BUDGET_GROWTH ** wave
    app.check(abs(w["budget"] - budget) < budget * 1e-4,
              "wave %d has a budget of %g, not %g" % (w["wave"], w["budget"], budget))


def test_minutes(app):
    # The case's app quits first: two stress test apps at once outgrow the memory of a test box
    app.expect("logs error", '""')
    app.quit()
    args = [a for a in app.args if a != "--stresstest"] + ["--stresstest=45"]
    log = app.log[:-len(".log")] + ".minutes.log" if app.log else None
    with App(app.binary, args, log=log, timeout=app.timeout, out=app.out,
             prefix=app.prefix + ".minutes") as other:
        begin(other)
        check_clock(other, 45)
        other.check(waves(other)["wave"] == 52, "the wave counter is %d at 45 minutes, not 52" %
                    waves(other)["wave"])
        other.expect("logs error", '""')
        other.quit()


def test_restart(app):
    begin(app)
    before = check_base(app)

    set_state(app, "Lost")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    app.wait_for("get %s oreelse.OrePowerState.production" % POWER,
                 lambda out: float(out) > 0, what="the power of the new base")
    set_state(app, "Paused")
    after = check_base(app)
    app.check(after != before, "the new run has the map of the previous one")
    check_stock(app)
    check_clock(app, MINUTES)
