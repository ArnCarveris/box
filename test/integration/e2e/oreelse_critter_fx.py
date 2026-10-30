"""Ore Else's critter animation, deaths and corpses driven through its command port: a walking
mite's walk cycle advances with the distance it walks, it bobs, waddles and swings its legs with
it, and while it attacks it stands on the ground, its cycle holds and its legs barely swing; a
killed mite plays its combust (or the splat without a death burst), counts as a kill and leaves
a corpse that is no critter (not in the spatial index the turrets search, not pushed), tipped
over, squashed and charred with its eyes dark and its embers glowing, that lies 4 s, sinks at 0.7
units a second and is deleted; the 101st corpse takes the place of the oldest.

The setup is oreelse_critters.py's: a fresh apps/oreelse process per case with its default scene
and a fixed session seed, a 640x360 window without shadows, a fixed delta of 1/60 s, waits that
count frames, critters spawned with oreelse.spawn_critter(), corpses laid with oreelse.corpse()
and the state written through the REST endpoint of the port. The animation is checked against
the critter's own state (OreCritter.anim, attacking) read while paused, so it holds however many
frames the commands take.
"""
import math

from box_client import parse_value, srgb
from oreelse_critters import (FPS, WAVE, critters, hold_waves, measure, place_player, position,
                              rows_of, run_script, set_state, spawn, start, wait_until, waves)

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

# The mite (etc/critters/mite.flecs): speed, radius, gait and its four legs
MITE_SPEED = 6.8
MITE_RADIUS = 0.34
MITE_STEP = 2.8
MITE_BOB = 0.13
MITE_LEGS = 4

# The walk (cfg.critterWaddle, cfg.critterAttackSwing)
WADDLE = 0.07
ATTACK_SWING = 0.35

# The corpses (cfg.corpseTime, cfg.corpseSink, cfg.corpseCap, cfg.corpseTip,
# cfg.corpseTipJitter, cfg.corpseRoll, cfg.corpseSquash, cfg.corpseColor); a corpse is deleted
# 2 r + 1 below the ground
CORPSE_TIME = 4
CORPSE_SINK = 0.7
CORPSE_CAP = 100
TIP = (1.1, 1.3)
ROLL = 0.4
SQUASH = 0.55
CHAR = dict(zip("rgba", srgb((40, 36, 34)) + (255,)))
MITE_DEPTH = 2 * MITE_RADIUS + 1

INDEX = "oreelse.OreCritterIndex"
PARTICLES = "get box.particle.ParticleStats box.particle.ParticleStats.particles"

# Tolerance of a pose against the walk cycle read back through the port
POSE_TOLERANCE = 2e-3


def under(app, path, expr):
    """The results of a query among the descendants of an entity, whose parts are unnamed
    instances of the prefab's: path -> the value of the first field."""
    return rows_of(app, "%s, ChildOf(self|up ChildOf, %s)" % (expr, path))


def legs(app, critter):
    """The legs of a critter: path -> OreLimb."""
    result = under(app, critter, "oreelse.OreLimb")
    app.check(len(result) == MITE_LEGS, "the mite %s has the legs %s" % (critter, list(result)))
    return result


def anim(app, critter):
    return app.get(critter, "oreelse.OreCritter")["anim"]


def check_pose(app, critter):
    """Check a mite's pose against its walk cycle: the bob |sin(anim)| * bob (0 while it
    attacks), the waddle sin(anim / 2) * 0.07 and every leg at sin(anim + phase) * swing (times
    0.35 while it attacks). Returns its OreCritter and its height."""
    c = app.get(critter, "oreelse.OreCritter")
    p = app.get(critter, "box.transform.Position")
    r = app.get(critter, "box.transform.Rotation")
    a = c["anim"]
    bob = 0 if c["attacking"] else abs(math.sin(a)) * MITE_BOB
    app.check(abs(p["y"] - bob) < POSE_TOLERANCE,
              "at the walk cycle %g the mite is %g up, not %g" % (a, p["y"], bob))
    waddle = math.sin(a / 2) * WADDLE
    app.check(abs(r["z"] - waddle) < POSE_TOLERANCE,
              "at the walk cycle %g the mite waddles %g, not %g" % (a, r["z"], waddle))
    scale = ATTACK_SWING if c["attacking"] else 1
    for path, limb in legs(app, critter).items():
        x = app.get(path, "box.transform.Rotation")["x"]
        want = math.sin(a + limb["phase"]) * limb["swing"] * scale
        app.check(abs(x - want) < POSE_TOLERANCE,
                  "at the walk cycle %g the leg %s swings %g, not %g" % (a, path, x, want))
    return c, p["y"]


def indexed(app):
    """The critters in the spatial index the turrets search, read after two frames of play: it
    is rebuilt every frame of play and empty while paused. The index has no copy hook, so the
    shell does not show its value: it is read through the REST endpoint of the port. The game is
    paused after."""
    set_state(app, "Playing")
    app.wait_frames(2)
    status, body = app.request("component/" + INDEX.replace(".", "/"), {"component": INDEX})
    app.check(status == 200, "GET %s failed (%d): %s" % (INDEX, status, body))
    count = parse_value(body)["count"]
    set_state(app, "Paused")
    return count


def corpses(app, prefab=None):
    """The corpses, of one prefab when given: path -> OreCorpse."""
    return rows_of(app, "oreelse.OreCorpse" + (", (IsA, %s)" % prefab if prefab else ""))


def effects(app):
    """The names of the templates of the effects that play."""
    return {str(v["effect"]).split(".")[-1]
            for v in rows_of(app, "box.particle.ParticleEffect").values()}


def kill(app, critter):
    """Kill a critter: 0 hit points, played until the kill counts and two frames more, so its
    corpse is there and charred. The game is paused after."""
    kills = waves(app)["kills"]
    app.exec("set %s oreelse.OreHealth {value: 0, max: 12}" % critter)
    set_state(app, "Playing")
    app.wait_for(WAVE % "kills", str(kills + 1), frames=FPS, what="the kill")
    app.wait_frames(2)
    set_state(app, "Paused")


def still_mite(app, x, z):
    """Spawn a mite that stands where it is (speed 0): its path."""
    mite = spawn(app, "Mite", x, z)
    app.exec("set %s oreelse.OreSpeed {value: 0}" % mite)
    return mite


def mat(axis, a):
    c, s = math.cos(a), math.sin(a)
    if axis == "x":
        return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == "y":
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def original_angles(r):
    """The angles of box's rotation r (Ry * Rx * Rz) in the original's Euler order
    (Rx(a) * Ry(b) * Rz(c)), b within [-PI / 2, PI / 2]."""
    m = mul(mul(mat("y", r["y"]), mat("x", r["x"])), mat("z", r["z"]))
    b = math.asin(max(-1, min(1, m[0][2])))
    a = math.atan2(-m[1][2], m[2][2])
    c = math.atan2(-m[0][1], m[0][0])
    return a, b, c


def test_walk(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")

    # A mite walking to the origin: its walk cycle advances by dt * speed * step
    mite = spawn(app, "Mite", 30, 30)
    app.exec("set %s oreelse.OreCritter {retarget: 0.5, jitter: 0, attacking: false, anim: 0}" %
             mite)
    advanced, took = measure(app, lambda: anim(app, mite))
    want = MITE_SPEED * MITE_STEP * took
    app.check(abs(advanced - want) < want * 0.02,
              "the walk cycle advanced %g in %g s of walking, not %g" % (advanced, took, want))

    # It bobs, waddles and swings its legs with the cycle, at every pause
    heights = []
    swings = set()
    for _ in range(4):
        c, y = check_pose(app, mite)
        app.check(not c["attacking"], "the walking mite attacks")
        heights.append(y)
        swings.add(round(app.get(list(legs(app, mite))[0], "box.transform.Rotation")["x"], 4))
        measure(app, lambda: 0, frames=3)
    app.check(max(heights) > MITE_BOB * 0.1,
              "the walking mite does not bob: its heights were %s" % heights)
    app.check(len(swings) > 1, "the leg of the walking mite does not swing: %s" % swings)


def test_attack(app):
    start(app)
    hold_waves(app)
    set_state(app, "Paused")

    # A mite next to the astronaut chews it: on the ground, the cycle holds, the legs at 0.35
    place_player(app, 20, 20)
    mite = spawn(app, "Mite", 20.5, 20)
    set_state(app, "Playing")
    app.wait_for("get %s oreelse.OreCritter.attacking" % mite, "true", frames=2 * FPS,
                 what="the mite attacking")
    measure(app, lambda: 0, frames=2)
    c, y = check_pose(app, mite)
    app.check(c["attacking"] and y == 0, "the attacking mite is %g up" % y)
    held, took = measure(app, lambda: anim(app, mite))
    app.check(held == 0, "the walk cycle moved %g in %g s of attacking" % (held, took))
    c, y = check_pose(app, mite)
    app.check(c["attacking"] and y == 0, "the attacking mite is %g up" % y)

    # Once the astronaut is out of reach it walks and bobs again
    place_player(app, -20, -20)
    advanced, took = measure(app, lambda: anim(app, mite))
    app.check(advanced > 0, "the walk cycle held %g s after the attack" % took)
    c, y = check_pose(app, mite)
    app.check(not c["attacking"], "the mite still attacks")


def test_corpse(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")

    # A mite standing still, turned toward the origin, is killed
    mite = still_mite(app, 20, -30)
    measure(app, lambda: 0, frames=2)
    x, z = position(app, mite)
    yaw = app.get(mite, "box.transform.Rotation")["y"]
    kill(app, mite)

    # The kill counts, the critter is gone and the combust played
    app.check(waves(app)["kills"] == 1, "the kills are %d" % waves(app)["kills"])
    app.check(not critters(app), "critters left after the kill: %s" % list(critters(app)))
    playing = effects(app)
    app.check("CombustBurst" in playing and "SplatBurst" not in playing,
              "the death played %s" % sorted(playing))
    app.check(int(app.exec(PARTICLES)) > 0, "the death spawned no particles")

    # Its corpse lies where it died, on the ground, tipped over in the original's pose, squashed
    # and charred
    found = corpses(app, "Mite")
    app.check(len(found) == 1 and len(corpses(app)) == 1, "the corpses are %s" % list(found))
    corpse = list(found)[0]
    p = app.get(corpse, "box.transform.Position")
    app.check(abs(p["x"] - x) < 1e-3 and abs(p["z"] - z) < 1e-3 and p["y"] == 0,
              "the corpse lies at %s, the mite died at %s" % (p, (x, z)))
    a, b, c = original_angles(app.get(corpse, "box.transform.Rotation"))
    app.check(TIP[0] - 1e-3 <= a <= TIP[1] + 1e-3 and abs(c) <= ROLL + 1e-3 and
              abs(b - yaw) < 1e-3,
              "the corpse is turned by %s in the original's order, the mite headed %g" %
              ((a, b, c), yaw))
    s = app.get(corpse, "box.transform.Scale")
    app.check(abs(s["x"] - 1) < 1e-4 and abs(s["y"] - SQUASH) < 1e-4 and abs(s["z"] - 1) < 1e-4,
              "the corpse is scaled %s" % s)
    app.check(app.get(corpse, "box.material.Tint") == CHAR,
              "the corpse is tinted %s" % app.get(corpse, "box.material.Tint"))
    o = app.get(corpse, "oreelse.OreCorpse")
    app.check(abs(o["depth"] - MITE_DEPTH) < 1e-4 and 0 < o["age"] < 1,
              "the fresh corpse has %s" % o)

    # Its glowing parts are dark; the embers on it glow
    embers = under(app, corpse, "CorpseEmbers")
    app.check(len(embers) == 1, "the corpse has the embers %s" % list(embers))
    ember = list(embers)[0]
    glow = under(app, corpse, "box.material.Emissive(self|up IsA)")
    specks = under(app, ember, "box.material.Emissive(self|up IsA)")
    body = {k: v for k, v in glow.items() if k not in specks}
    app.check(len(body) >= 2 and all(v["strength"] == 0 for v in body.values()),
              "the corpse's parts glow: %s" % body)
    app.check(len(specks) >= 3 and all(v["strength"] > 0 for v in specks.values()),
              "the embers' specks do not glow: %s" % specks)

    # It is no critter: not in the spatial index the turrets search, not pushed by a mite on it
    count = indexed(app)
    app.check(count == 0, "the spatial index holds %d critters" % count)
    other = still_mite(app, p["x"], p["z"])
    measure(app, lambda: 0, frames=10)
    ox, oz = position(app, other)
    app.check(abs(ox - p["x"]) < 1e-4 and abs(oz - p["z"]) < 1e-4,
              "the mite on the corpse was pushed to %s" % ((ox, oz),))
    count = indexed(app)
    app.check(count == 1, "the spatial index holds %d critters" % count)
    q = app.get(corpse, "box.transform.Position")
    app.check(q["x"] == p["x"] and q["z"] == p["z"], "the corpse moved to %s" % q)
    app.check(not under(app, corpse, "oreelse.OreCritter") and
              corpse not in critters(app), "the corpse is a critter")


def test_splat(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")

    # Without a death burst a critter splats; it still leaves a corpse
    mite = still_mite(app, 20, -30)
    app.exec("set %s oreelse.OreDeathBurst {burst: 0}" % mite)
    kill(app, mite)
    playing = effects(app)
    app.check("SplatBurst" in playing and "CombustBurst" not in playing,
              "the death played %s" % sorted(playing))
    app.check(len(corpses(app, "Mite")) == 1, "the corpses are %s" % list(corpses(app)))


def test_sink(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")
    kill(app, still_mite(app, 20, -30))
    corpse = list(corpses(app))[0]
    depth = app.get(corpse, "oreelse.OreCorpse")["depth"]

    # Before 4 s it lies on the ground
    app.exec("set %s oreelse.OreCorpse {age: %g, depth: %g}" % (corpse, CORPSE_TIME - 1, depth))
    sank, took = measure(app, lambda: app.get(corpse, "box.transform.Position")["y"])
    app.check(sank == 0 and app.get(corpse, "box.transform.Position")["y"] == 0,
              "the corpse sank %g at %g s" % (-sank, CORPSE_TIME - 1 + took))

    # After 4 s it sinks at 0.7 units a second
    app.exec("set %s oreelse.OreCorpse {age: %g, depth: %g}" % (corpse, CORPSE_TIME + 0.1, depth))
    sank, took = measure(app, lambda: app.get(corpse, "box.transform.Position")["y"])
    app.check(abs(-sank / took - CORPSE_SINK) < 0.02,
              "the corpse sank %g in %g s, not %g a second" % (-sank, took, CORPSE_SINK))

    # 2 r + 1 below the ground it is deleted
    p = app.get(corpse, "box.transform.Position")
    app.exec("set %s box.transform.Position {%g, %g, %g}" %
             (corpse, p["x"], -(depth - 0.02), p["z"]))
    set_state(app, "Playing")
    wait_until(app, lambda: not corpses(app), FPS, "the sunk corpse deleted")
    set_state(app, "Paused")
    app.check(waves(app)["kills"] == 1, "the kills are %d" % waves(app)["kills"])


def test_cap(app):
    start(app)
    hold_waves(app)
    place_player(app, -40, -40)
    set_state(app, "Paused")

    # 100 corpses laid by a script, then one of them made the oldest
    lines = ["using oreelse", "@NAME {}"]
    for i in range(CORPSE_CAP):
        lines.append("const c%d: entity = oreelse.corpse(Mite, {%d, 0, %d}, 0)" %
                     (i, -30 + (i % 10) * 2, 10 + (i // 10) * 2))
    run_script(app, "\n".join(lines))
    before = corpses(app)
    app.check(len(before) == CORPSE_CAP, "the script laid %d corpses" % len(before))
    oldest = sorted(before)[0]
    app.exec("set %s oreelse.OreCorpse {age: 2, depth: %g}" % (oldest, MITE_DEPTH))

    # The 101st corpse takes the place of the oldest
    kill(app, still_mite(app, 20, -30))
    set_state(app, "Playing")
    wait_until(app, lambda: oldest not in corpses(app), FPS, "the oldest corpse deleted")
    set_state(app, "Paused")
    after = corpses(app)
    app.check(len(after) == CORPSE_CAP, "%d corpses lie on the map" % len(after))
    app.check(set(before) - {oldest} <= set(after),
              "corpses other than the oldest are gone: %s" % sorted(set(before) - set(after)))
    new = [k for k in after if k not in before]
    app.check(len(new) == 1, "the new corpses are %s" % new)
    x, z = position(app, new[0])
    app.check(abs(x - 20) < 1e-3 and abs(z + 30) < 1e-3, "the new corpse lies at %s" % ((x, z),))
