"""Ore Else driven through its command port: the waves (the first at 90 s, the gap of 50 s after
the last spawn, the budget, the styles and the tiers a wave unlocks, the spawns at the edges on
alternating sides), the critters' targets, the probe ahead, chewing a wall, the crowd separation,
the astronaut's health, its regen and losing, and the kill counter.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta
of 1/60 s per frame, waits that count frames. The cases set up state directly: the wave state
through the REST endpoint of the port, critters spawned with oreelse.spawn_critter() where the
case wants them and walls created by a script on chosen tiles. Rates are measured over the play
time of OreClock between two pauses, so they hold however many frames the commands take.
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
WAVE = "get oreelse.OreWaveState OreWaveState.%s"
PLAYER = "astronaut"

# The tile grid (cfg.x0, cfg.z0, cfg.tile)
X0 = -47
Z0 = -47
TILE = 2

# The waves (cfg.firstWaveAt, cfg.waveGap, cfg.waveBudgetBase, cfg.waveBudgetGrowth,
# cfg.waveUnitCap, cfg.waveBehemothCap)
FIRST_WAVE = 90
GAP = 50
BUDGET_BASE = 2
BUDGET_GROWTH = 1.3
UNIT_CAP = 10000
BEHEMOTH_CAP = 25

# The tiers in the order of OreArsenal: prefab, cost, first wave
TIERS = [("Mite", 1, 1), ("Skitter", 3, 4), ("Brute", 15, 7), ("Behemoth", 120, 13)]

# Spawns: cfg.edgeInset inside the edge tiles, cfg.edgeSpawnJitter off, kept within the map
EDGE = 46
JITTER = 5
MAP_CLAMP = 47

# The mite (its prefab) and the astronaut (cfg.aggroRange, cfg.playerHp, cfg.playerRegen,
# cfg.playerRegenDelay, cfg.retargetInterval)
MITE_SPEED = 6.8
MITE_DAMAGE = 4
MITE_RADIUS = 0.34
BEHEMOTH_RADIUS = 2.2
AGGRO = 14
PLAYER_HP = 100
REGEN = 2
REGEN_DELAY = 5
RETARGET = 0.5

# Frames of play between the two pauses of a measured rate, and its tolerance
MEASURE_FRAMES = 20
RATE_TOLERANCE = 0.05


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
    """Run a script through the REST endpoint of the port, a new script entity every time. The
    script names its entity @NAME, which is replaced by a new name; returns that name."""
    SCRIPTS[0] += 1
    name = "critters_case_%d" % SCRIPTS[0]
    obj = "critters_obj_%d" % SCRIPTS[0]
    request = urllib.request.Request(app.url("script/" + name,
                                             {"code": code.replace("@NAME", obj)}),
                                     method="PUT")
    try:
        with urllib.request.urlopen(request, timeout=10) as r:
            status, body = r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read().decode(errors="replace")
    app.check(status == 200 and '"error"' not in body,
              "the script %r failed (%d): %s" % (code, status, body))
    return obj


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
    """Set up and leave the title screen with a key press."""
    setup(app)
    app.expect(STATE % "Title", "true")
    app.exec("key.press Space")
    app.wait_input()
    app.wait_for(STATE % "Playing", "true")


def entity(value):
    """An entity as get prints it, None for no entity."""
    return None if value in (0, "0", "#0", "", None) else str(value)


def clock(app):
    return float(app.exec(CLOCK))


def waves(app):
    return app.get(WAVES, "OreWaveState")


def put_waves(app, **fields):
    put(app, WAVES, WAVES, fields)


def hold_waves(app):
    """Keep the waves away for the case."""
    put_waves(app, timer=100000, left=0, spawn=[0, 0, 0, 0])


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


def critters(app, prefab=None):
    """The critters, of one prefab when given: path -> OreCritter."""
    return rows_of(app, "oreelse.OreCritter" + (", (IsA, %s)" % prefab if prefab else ""))


def buildings(app):
    return rows_of(app, "oreelse.OreBuilding")


def spawn(app, prefab, x, z):
    """Spawn a critter with oreelse.spawn_critter(): its path."""
    name = run_script(app, "@NAME { oreelse.OreStack: {item: "
                           "oreelse.spawn_critter(%s, %.4f, %.4f), amount: 1} }" % (prefab, x, z))
    path = entity(app.get(name, "OreStack")["item"])
    app.check(path is not None, "oreelse.spawn_critter(%s) spawned nothing" % prefab)
    return path


def wall(app, row, col):
    """Create a wall on a tile, registered in the tile grid: its path."""
    x, z = tile_point(row, col)
    return run_script(app, "@NAME { (IsA, Wall)\n box.transform.Position: {%g, 0, %g}\n "
                           "oreelse.OreBuilding: {row: %d, col: %d} }" % (x, z, row, col))


def tile_point(row, col):
    return X0 + col * TILE, Z0 + row * TILE


def position(app, path):
    p = app.get(path, "Position")
    return p["x"], p["z"]


def target(app, path):
    return entity(app.get(path, "OreTarget")["value"])


def health(app, path):
    return app.get(path, "OreHealth")["value"]


def place_player(app, x, z):
    app.exec("set %s Position {%g, 0, %g}" % (PLAYER, x, z))


def wait_until(app, condition, frames, what):
    """Poll a condition of the app until it holds; fail after a budget of frames."""
    start = app.frame()
    while not condition():
        if app.frame() - start > frames:
            app.fail("expected %s within %d frames" % (what, frames))


def measure(app, value, frames=MEASURE_FRAMES):
    """Play a number of frames between two pauses: the change of a value and the play time it
    took. The game is paused before and after."""
    set_state(app, "Paused")
    v0, t0 = value(), clock(app)
    set_state(app, "Playing")
    app.wait_frames(frames)
    set_state(app, "Paused")
    v1, t1 = value(), clock(app)
    app.check(t1 > t0, "no time passed between the pauses")
    return v1 - v0, t1 - t0


def sides_of(x, z):
    """The sides of the map a spawn position can come from: 0 north, 1 south, 2 west, 3 east."""
    tol = JITTER + 1e-3
    result = set()
    if abs(z + EDGE) <= tol:
        result.add(0)
    if abs(z - EDGE) <= tol:
        result.add(1)
    if abs(x + EDGE) <= tol:
        result.add(2)
    if abs(x - EDGE) <= tol:
        result.add(3)
    return result


def check_spawn(app, path, side=None):
    x, z = position(app, path)
    app.check(abs(x) <= MAP_CLAMP + 1e-4 and abs(z) <= MAP_CLAMP + 1e-4,
              "the critter %s spawned off the map at %s" % (path, (x, z)))
    sides = sides_of(x, z)
    app.check(sides, "the critter %s spawned at %s, not at an edge" % (path, (x, z)))
    if side is not None:
        app.check(side in sides, "the critter %s at %s is not on the side %d" %
                  (path, (x, z), side))
    return sides


def test_first_wave(app):
    start(app)
    set_state(app, "Paused")

    # Before the first wave the timer counts down from 90 s of play
    w = waves(app)
    elapsed = clock(app)
    app.check(w["wave"] == 0 and w["left"] == 0 and w["side"] == -1 and w["kills"] == 0,
              "the run starts with the waves at %s" % w)
    app.check(abs(w["timer"] - (FIRST_WAVE - elapsed)) < 1e-3,
              "after %g s of play the timer is at %g, not %g" %
              (elapsed, w["timer"], FIRST_WAVE - elapsed))
    app.check(abs(w["gap"] - FIRST_WAVE) < 1e-4 and abs(w["frac"] - elapsed / FIRST_WAVE) < 1e-3,
              "the gap and frac are %g and %g" % (w["gap"], w["frac"]))
    app.check(not critters(app), "critters before the first wave: %s" % critters(app))

    # At 0 the first wave starts: two mites, the first at once
    put_waves(app, timer=0.05)
    set_state(app, "Playing")
    app.wait_for(WAVE % "wave", "1", frames=FPS, what="the first wave")
    set_state(app, "Paused")
    w = waves(app)
    mites = critters(app, "Mite")
    app.check(abs(w["budget"] - BUDGET_BASE) < 1e-4, "the first wave's budget is %g" % w["budget"])
    app.check(len(mites) + w["left"] == 2 and len(critters(app)) == len(mites) and
              w["spawn"] == [2 - len(mites), 0, 0, 0],
              "the first wave is %s with %s spawned" % (w["spawn"], list(critters(app))))
    app.check(abs(w["frac"] - 1) < 1e-6 or w["left"] == 0, "frac is %g while spawning" % w["frac"])

    if w["left"] == 1:
        # While a wave spawns the timer to the next one holds at 50
        app.check(abs(w["timer"] - GAP) < 1e-4 and abs(w["frac"] - 1) < 1e-6,
                  "while spawning the timer is %g and frac %g" % (w["timer"], w["frac"]))
        first = list(mites)[0]
        check_spawn(app, first, w["side"])
        side = w["side"]
        put_waves(app, spawn_timer=0)
        set_state(app, "Playing")
        app.wait_for(WAVE % "left", "0", frames=FPS, what="the second mite")
        set_state(app, "Paused")
        w = waves(app)
        second = [p for p in critters(app, "Mite") if p != first]
        app.check(len(second) == 1, "the second spawn gave %s" % second)
        check_spawn(app, second[0], w["side"])
        app.check(w["side"] != side, "both mites spawned on the side %d" % side)
    else:
        # Both spawned before the pause: they stand on two different sides, the last on w.side
        paths = list(critters(app, "Mite"))
        app.check(len(paths) == 2, "the first wave spawned %s" % paths)
        sides = [check_spawn(app, p) for p in paths]
        app.check(any(w["side"] in s and any(o != w["side"] for o in sides[1 - k])
                      for k, s in enumerate(sides)),
                  "the mites at %s are not on alternating sides" % sides)

    # The next wave starts 50 s after the last spawn: the timer counts down from there
    w = waves(app)
    app.check(GAP - 1 < w["timer"] <= GAP + 1e-4 and abs(w["gap"] - GAP) < 1e-4,
              "after the last spawn the timer is %g, the gap %g" % (w["timer"], w["gap"]))
    down, took = measure(app, lambda: waves(app)["timer"])
    app.check(abs(-down - took) < 1e-3, "the timer went down %g in %g s" % (-down, took))
    w = waves(app)
    app.check(abs(w["frac"] - (1 - w["timer"] / GAP)) < 1e-4, "frac is %g" % w["frac"])

    put_waves(app, timer=0.25)
    t0 = clock(app)
    set_state(app, "Playing")
    app.wait_for(WAVE % "wave", "2", frames=FPS, what="the second wave")
    set_state(app, "Paused")
    app.check(clock(app) - t0 >= 0.25 - 1.5 * DT,
              "the second wave started %g s after the timer was at 0.25" % (clock(app) - t0))


def compose(app, wave):
    """Start a wave of a number: its counts per tier (spawned and left) and its wave state. The
    rest of the wave is called off."""
    set_state(app, "Paused")
    before = {tier: set(critters(app, tier[0])) for tier in TIERS}
    put_waves(app, wave=wave - 1, timer=0, left=0, spawn=[0, 0, 0, 0])
    set_state(app, "Playing")
    app.wait_for(WAVE % "wave", str(wave), frames=FPS, what="the wave %d" % wave)
    set_state(app, "Paused")
    w = waves(app)
    counts = list(w["spawn"])
    for t, tier in enumerate(TIERS):
        counts[t] += len(set(critters(app, tier[0])) - before[tier])
    put_waves(app, left=0, spawn=[0, 0, 0, 0], timer=100000)
    return counts, w


def check_budget(app, wave, counts, w):
    budget = BUDGET_BASE * BUDGET_GROWTH ** (wave - 1)
    spent = sum(c * tier[1] for c, tier in zip(counts, TIERS))
    units = sum(counts)
    app.check(abs(w["budget"] - budget) < budget * 1e-4,
              "wave %d has a budget of %g, not %g" % (wave, w["budget"], budget))
    app.check(spent <= budget + 1e-3 and (budget - spent < 1 or units >= UNIT_CAP),
              "wave %d spent %g of %g on %s" % (wave, spent, budget, counts))
    app.check(units <= UNIT_CAP, "wave %d has %d critters" % (wave, units))
    for c, tier in zip(counts, TIERS):
        app.check(c == 0 or wave >= tier[2], "wave %d has %d of %s, unlocked from wave %d" %
                  (wave, c, tier[0], tier[2]))


def test_composition(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)

    counts, w = compose(app, 1)
    check_budget(app, 1, counts, w)
    app.check(counts == [2, 0, 0, 0], "wave 1 is %s, not two mites" % counts)

    counts, w = compose(app, 3)
    check_budget(app, 3, counts, w)
    app.check(counts[1:] == [0, 0, 0], "wave 3 has skitters or more: %s" % counts)

    # Brutes are unlocked from wave 7 but cannot be afforded before wave 9
    counts, w = compose(app, 8)
    check_budget(app, 8, counts, w)
    app.check(counts[2] == 0 and counts[3] == 0, "wave 8 has brutes or behemoths: %s" % counts)

    # Waves above 17 can have behemoths, never more than 25
    found = False
    for wave in range(28, 33):
        counts, w = compose(app, wave)
        check_budget(app, wave, counts, w)
        app.check(counts[3] <= BEHEMOTH_CAP, "wave %d has %d behemoths" % (wave, counts[3]))
        app.check(w["style"] != "Swarm" or counts[3] == 0,
                  "the swarm wave %d has %d behemoths" % (wave, counts[3]))
        found = found or counts[3] > 0
    app.check(found, "no wave from 28 to 32 has behemoths")

    # A wave whose budget the unit cap cannot spend holds 25 behemoths in every style: near the
    # cap the fit rule turns down the cheap tiers and the dearest affordable one is picked, even
    # in a swarm, whose share of behemoths is 0
    counts, w = compose(app, 40)
    check_budget(app, 40, counts, w)
    app.check(counts[3] == BEHEMOTH_CAP,
              "the %s wave 40 has %d behemoths" % (w["style"], counts[3]))


def test_walk_and_targets(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")

    # Without the astronaut in range or a building the mite walks to the origin
    mite = spawn(app, "Mite", 30, 30)
    app.exec("set %s OreCritter {retarget: 0.5, jitter: 0, attacking: false}" % mite)
    x0, z0 = position(app, mite)
    walked, took = measure(app, lambda: -math.hypot(*position(app, mite)))
    app.check(abs(walked - MITE_SPEED * took) < 0.02,
              "the mite came %g closer to the origin in %g s, not %g" %
              (walked, took, MITE_SPEED * took))
    x1, z1 = position(app, mite)
    app.check(abs(x1 - z1) < 1e-3 and x1 < x0, "the mite walked from %s to %s, not to the origin" %
              ((x0, z0), (x1, z1)))
    app.check(target(app, mite) is None, "the mite targets %s" % target(app, mite))

    # The astronaut 16 units away is no target, 10 units away it is
    app.exec("set %s Position {20, 0, 0}" % mite)
    place_player(app, 20, 16)
    app.wait_frames(2)
    set_state(app, "Playing")
    app.wait_frames(round(2 * RETARGET * FPS))
    set_state(app, "Paused")
    app.check(target(app, mite) is None, "the mite targets %s 16 units away" % target(app, mite))
    app.exec("set %s Position {20, 0, 0}" % mite)
    place_player(app, 10, 0)
    set_state(app, "Playing")
    app.wait_for("get %s OreTarget.value" % mite, PLAYER, frames=2 * FPS,
                 what="the mite going for the astronaut")

    # A building closer than the astronaut wins: the wall 3 units west, the astronaut 10 units
    # east, so the mite turns around to the wall
    set_state(app, "Paused")
    row, col = 23, 39
    wx, wz = tile_point(row, col)
    w = wall(app, row, col)
    app.exec("set %s Position {%g, 0, %g}" % (mite, wx + 3, wz))
    place_player(app, wx + 13, wz)
    set_state(app, "Playing")
    app.wait_for("get %s OreTarget.value" % mite, w, frames=2 * FPS,
                 what="the mite going for the wall")

    # It chews the wall at 4 HP/s
    wait_until(app, lambda: health(app, w) < 300, 2 * FPS, "the wall chewed")
    lost, took = measure(app, lambda: -health(app, w))
    app.check(abs(lost / took - MITE_DAMAGE) < RATE_TOLERANCE,
              "the wall lost %g HP in %g s, not %g HP/s" % (lost, took, MITE_DAMAGE))
    app.check(app.get(mite, "OreCritter")["attacking"], "the mite chewing is not attacking")
    v = app.get(mite, "OreVelocity")
    app.check(v["x"] == 0 and v["z"] == 0, "the attacking mite has the velocity %s" % v)

    # At 0 the wall is deleted and the tile is free; the mite goes for the astronaut
    app.exec("set %s OreHealth {value: 0.5, max: 300}" % w)
    set_state(app, "Playing")
    wait_until(app, lambda: w not in buildings(app), FPS, "the wall deleted")
    app.check(not buildings(app), "buildings left: %s" % buildings(app))
    app.wait_for("get %s OreTarget.value" % mite, PLAYER, frames=2 * FPS,
                 what="the mite back on the astronaut")


def test_probe(app):
    start(app)
    hold_waves(app)
    set_state(app, "Paused")

    # The mite goes for the astronaut 12 units west; a wall stands on its way, closer than the
    # astronaut, but the mite does not look again for 100 s: the probe ahead takes the wall
    row, col = 23, 39
    wx, wz = tile_point(row, col)
    place_player(app, wx - 8, wz)
    mite = spawn(app, "Mite", wx + 4, wz)
    app.exec("set %s OreCritter {retarget: 100, jitter: 0, attacking: false}" % mite)
    app.exec("set %s OreTarget {value: %s}" % (mite, PLAYER))
    w = wall(app, row, col)
    set_state(app, "Playing")
    app.wait_for("get %s OreTarget.value" % mite, w, frames=FPS, what="the probe taking the wall")
    set_state(app, "Paused")
    retarget = app.get(mite, "OreCritter")["retarget"]
    app.check(0 < retarget <= RETARGET + 1e-4,
              "after the probe the retarget timer is %g, not %g" % (retarget, RETARGET))
    x, z = position(app, mite)
    app.check(x > wx, "the mite at %s walked through the wall" % ((x, z),))


def test_separation(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")

    # Two mites on top of each other end up apart
    a = spawn(app, "Mite", 30, 20)
    b = spawn(app, "Mite", 30, 20)
    for m in (a, b):
        app.exec("set %s OreCritter {retarget: 0.5, jitter: 0, attacking: false}" % m)
    set_state(app, "Playing")
    app.wait_frames(30)
    set_state(app, "Paused")
    pa, pb = position(app, a), position(app, b)
    d = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
    app.check(d > 2 * MITE_RADIUS - 0.08, "the two mites are %g apart" % d)

    # A behemoth barely moves when mites are pushed against it; the mites move out
    bx, bz = -20, 30
    beh = spawn(app, "Behemoth", bx, bz)
    mites = [spawn(app, "Mite", bx + 1.5, bz + dz) for dz in (-0.6, 0, 0.6)]
    for c in [beh] + mites:
        app.exec("set %s OreSpeed {value: 0}" % c)
    set_state(app, "Playing")
    app.wait_frames(45)
    set_state(app, "Paused")
    x, z = position(app, beh)
    moved = math.hypot(x - bx, z - bz)
    app.check(moved < 0.15, "the behemoth moved %g" % moved)
    for m in mites:
        mx, mz = position(app, m)
        gap = math.hypot(mx - x, mz - z)
        app.check(gap > MITE_RADIUS + BEHEMOTH_RADIUS - 0.08,
                  "the mite %s is %g from the behemoth" % (m, gap))


def test_player_health(app):
    start(app)
    hold_waves(app)
    set_state(app, "Paused")
    app.check(app.get(PLAYER, "OreHealth") == {"value": PLAYER_HP, "max": PLAYER_HP},
              "the astronaut has %s" % app.get(PLAYER, "OreHealth"))

    # A mite next to the astronaut chews it at 4 HP/s, and every bite starts the hurt timer over
    place_player(app, 20, 20)
    mite = spawn(app, "Mite", 20.5, 20)
    set_state(app, "Playing")
    wait_until(app, lambda: health(app, PLAYER) < PLAYER_HP, 2 * FPS, "the astronaut hurt")
    lost, took = measure(app, lambda: -health(app, PLAYER))
    app.check(abs(lost / took - MITE_DAMAGE) < RATE_TOLERANCE,
              "the astronaut lost %g HP in %g s, not %g HP/s" % (lost, took, MITE_DAMAGE))
    app.check(app.get(PLAYER, "OreHurt")["since"] < 2 * DT + 1e-4,
              "the hurt timer is at %g while chewed" % app.get(PLAYER, "OreHurt")["since"])

    # Without damage it does not regenerate before 5 s
    app.exec("set %s OreHealth {value: 0, max: 12}" % mite)
    app.exec("set %s OreHurt {since: 0}" % PLAYER)
    gained, took = measure(app, lambda: health(app, PLAYER))
    app.check(not critters(app), "the mite at 0 HP is still there")
    app.check(gained == 0, "the astronaut gained %g HP %g s after it was hurt" % (gained, took))

    # After 5 s it regenerates 2 HP/s, up to its maximum
    app.exec("set %s OreHurt {since: %g}" % (PLAYER, REGEN_DELAY))
    gained, took = measure(app, lambda: health(app, PLAYER))
    app.check(abs(gained / took - REGEN) < RATE_TOLERANCE,
              "the astronaut regenerated %g HP in %g s, not %g HP/s" % (gained, took, REGEN))
    app.exec("set %s OreHealth {value: %g, max: %g}" % (PLAYER, PLAYER_HP - 0.05, PLAYER_HP))
    measure(app, lambda: 0)
    app.check(health(app, PLAYER) == PLAYER_HP, "the regen went to %g" % health(app, PLAYER))

    # At 0 HP the game is lost
    app.exec("set %s OreHealth {value: 1, max: %g}" % (PLAYER, PLAYER_HP))
    spawn(app, "Mite", 20.5, 20)
    set_state(app, "Playing")
    app.wait_for(STATE % "Lost", "true", frames=2 * FPS, what="the game lost")
    app.check(health(app, PLAYER) == 0, "the astronaut has %g HP" % health(app, PLAYER))


def test_kills(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")
    a = spawn(app, "Mite", 30, 20)
    b = spawn(app, "Skitter", 20, 30)
    c = spawn(app, "Mite", 30, 30)
    app.check(app.get(b, "OreHealth") == {"value": 50, "max": 50},
              "the skitter has %s" % app.get(b, "OreHealth"))
    app.exec("set %s OreHealth {value: 0, max: 12}" % a)
    app.exec("set %s OreHealth {value: -3, max: 50}" % b)
    set_state(app, "Playing")
    app.wait_for(WAVE % "kills", "2", frames=FPS, what="two kills")
    set_state(app, "Paused")
    app.check(list(critters(app)) == [c], "the critters left are %s, not %s" %
              (list(critters(app)), c))
    app.wait_frames(5)
    app.check(waves(app)["kills"] == 2, "the kills went on to %d" % waves(app)["kills"])
