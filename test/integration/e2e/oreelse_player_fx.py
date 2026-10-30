"""Ore Else's effects of the astronaut, mining and placement driven through the command port: the
limbs swing and the astronaut bobs and kicks up dust while it walks, the jet plume plays while it
hovers over a building and stops after, the glints of the Luminite play only with the game, a
mined deposit bursts and shrinks with its ore, a used up deposit poofs, placing and demolishing a
building poof, and the astronaut's death splats and drops it.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup: a 640x360 window without shadows, a fixed delta
of 1/60 s per frame, waits that count frames. The cases set up the inventory, the deposits, the
astronaut's position and the cursor directly, with the helpers of oreelse_buildings.py and
oreelse_mining.py. A one shot effect is found by the instances of its template that play
(box.particle.ParticleEffect) and the position of their roots, the point of the play.
"""
import math

from box_client import parse_value
from oreelse_buildings import ARGS  # noqa: F401 (ARGS)
from oreelse_buildings import (DOZE_TIME, FPS, PLAYER, SETTLE, SWING_SLACK, building_at, button,
                               click, mine, place_player, rows_of, select, set_inventory,
                               set_state, start, tile_point)
from oreelse_mining import DEPOSIT_AMOUNT, STONE_TIME, aim, amount, deposit_of, mouse

APP = "apps/oreelse"

STATE = ("eval oreelse.OreGameState.has(flecs.script.core.pair("
         "oreelse.OreGameState,oreelse.OreGameState.%s))")
JET = PLAYER + ".jet"

# The glints of a Luminite deposit, the layer of its DepositSparkle (etc/deposits.flecs)
SPARKLE = "deposits.luminite_0.sparkle"

# The limbs of the Astronaut template (etc/player.flecs): their swing in radians
LIMBS = {"leg_left": 0.55, "leg_right": 0.55, "arm_left": 0.38, "arm_right": 0.38}

# The walk (cfg.bobHeight, cfg.footDustY), the jetpack's height (cfg.hoverHeight) and the dangle of
# the limbs in the air (cfg.airDangle)
BOB = 0.07
FOOT_DUST_Y = 0.04
HOVER_HEIGHT = 3
AIR_DANGLE = 0.14

# Deposits shrink to cfg.depositScaleMin, the heights of the plays above a deposit (cfg.mineBurstY,
# cfg.depositPoofY) and a building (cfg.placePoofY, cfg.demolishPoofY)
SCALE_MIN = 0.55
MINE_Y = 0.6
DEPOSIT_POOF_Y = 0.4
PLACE_POOF_Y = 0.4
DEMOLISH_POOF_Y = 0.6

# The death: the splat above the astronaut (cfg.playerSplatLift) and its fall (cfg.deathPitch)
SPLAT_LIFT = 1.1
DEATH_PITCH = 1.5708

TOLERANCE = 1e-3


def plays(app, name):
    """The points (x, y, z) of the playing instances of an effect template, by its name."""
    result = []
    for path, value in rows_of(app, "box.particle.ParticleEffect").items():
        if str(value["effect"]).split(".")[-1] == name:
            p = app.get(path, "box.transform.Position")
            result.append((p["x"], p["y"], p["z"]))
    return result


def near(a, b, tolerance=TOLERANCE):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def wait_until(app, condition, frames, what):
    start_frame = app.frame()
    while not condition():
        app.check(app.frame() - start_frame <= frames, "expected %s within %d frames" % (
            what, frames))


def wait_play(app, name, at, frames=FPS):
    """Wait for an instance of an effect playing at a point (x, y, z)."""
    def found():
        return any(near(p, at) for p in plays(app, name))
    wait_until(app, found, frames, "%s playing at %s, it plays at %s" % (
        name, at, plays(app, name)))


def limb(app, name):
    return float(app.exec("get %s.%s box.transform.Rotation.x" % (PLAYER, name)))


def limbs(app):
    """The swing (Rotation.x) of every limb of the astronaut, read in one frame."""
    result = {}
    for row in parse_value(app.exec('query "box.transform.Rotation, oreelse.OreLimb"')):
        parent, _, name = row["entity"].rpartition(".")
        if parent == PLAYER:
            result[name] = row["fields"][0]["value"]["Rotation"]["x"]
    return result


def playback(app, emitter=JET):
    return parse_value(app.exec("get %s box.particle.ParticlePlayback.state" % emitter))


def wait_playback(app, state, frames=FPS, emitter=JET):
    app.wait_for("get %s box.particle.ParticlePlayback.state" % emitter,
                 lambda out: parse_value(out).endswith(state),
                 frames=frames, what="%s %s" % (emitter, state))


def test_walk(app):
    start(app)
    for name in LIMBS:
        app.check(limb(app, name) == 0, "%s stands at %g, not straight" % (name, limb(app, name)))

    # Walking east the legs swing opposite to each other, and each arm with the opposite leg
    app.exec("key_down D")
    app.wait_input()
    app.wait_frames(10)
    largest = {name: 0 for name in LIMBS}
    heights = []
    for _ in range(12):
        values = limbs(app)
        app.check(sorted(values) == sorted(LIMBS), "the limbs of the astronaut are %s" % values)
        for name, value in values.items():
            app.check(abs(value) <= LIMBS[name] + TOLERANCE,
                      "%s swings %g, beyond %g" % (name, value, LIMBS[name]))
            largest[name] = max(largest[name], abs(value))
        app.check(abs(values["leg_left"] + values["leg_right"]) < TOLERANCE,
                  "the legs do not swing opposite to each other: %s" % values)
        app.check(abs(values["arm_left"] / LIMBS["arm_left"] -
                      values["leg_right"] / LIMBS["leg_right"]) < 0.02,
                  "the left arm does not swing with the right leg: %s" % values)
        heights.append(app.get(PLAYER, "box.transform.Position")["y"])
        app.wait_frames(2)
    for name, value in largest.items():
        app.check(value > LIMBS[name] * 0.4, "%s swung at most %g while walking" % (name, value))
    app.check(all(-TOLERANCE <= y <= BOB + TOLERANCE for y in heights) and max(heights) > 0.01,
              "the astronaut bobbed through %s, not up to %g" % (heights, BOB))
    gait = app.get(PLAYER, "oreelse.OrePlayerGait")
    app.check(0 <= gait["cycle"] < 2 * math.pi, "the walk cycle is at %s" % gait)

    # Every half step on the ground kicks up dust at the astronaut's feet
    z = app.get(PLAYER, "box.transform.Position")["z"]
    wait_until(app, lambda: any(abs(p[1] - FOOT_DUST_Y) < TOLERANCE and abs(p[2] - z) < TOLERANCE
                                for p in plays(app, "FootDust")),
               FPS, "the foot dust of a step")

    # Standing still the limbs come back straight and the astronaut stands on the ground
    app.exec("key_up D")
    app.wait_input()
    app.wait_frames(FPS)
    for name in LIMBS:
        app.check(abs(limb(app, name)) < TOLERANCE, "%s stays at %g after the walk" % (
            name, limb(app, name)))
    app.check(app.get(PLAYER, "box.transform.Position")["y"] == 0,
              "the astronaut does not stand on the ground")


def test_jet_plume(app):
    # The glints of the Luminite stand still on the title screen and play with the game
    wait_playback(app, "ParticlePaused", emitter=SPARKLE)
    start(app)
    app.check(playback(app) == "ParticleStopped", "the jet plume is %s on the ground" %
              playback(app))
    wait_playback(app, "ParticlePlaying", emitter=SPARKLE)
    set_inventory(app, [("Wall", 1)])
    select(app, "1", "Wall")
    click(app, 6, 6)
    x, z = tile_point(6, 6)
    place_player(app, x, z)
    app.wait_for("get %s box.transform.Position.y" % PLAYER,
                 lambda out: abs(float(out) - HOVER_HEIGHT) < 1e-4,
                 frames=FPS, what="the astronaut hovering at %g" % HOVER_HEIGHT)
    wait_playback(app, "ParticlePlaying")

    # Standing in the air the limbs dangle
    app.wait_frames(10)
    for name, swing in LIMBS.items():
        app.check(abs(limb(app, name)) <= AIR_DANGLE * swing + TOLERANCE,
                  "%s hangs at %g in the air, beyond its dangle" % (name, limb(app, name)))

    # The plume and the glints stand still with the game and play again with it
    set_state(app, "Paused")
    wait_playback(app, "ParticlePaused")
    wait_playback(app, "ParticlePaused", emitter=SPARKLE)
    set_state(app, "Playing")
    wait_playback(app, "ParticlePlaying")
    wait_playback(app, "ParticlePlaying", emitter=SPARKLE)

    # Next to the wall the astronaut comes down and the plume stops
    x, z = tile_point(6, 7)
    app.exec("set %s box.transform.Position {%g, %g, %g}" % (PLAYER, x, HOVER_HEIGHT, z))
    app.wait_for("get %s box.transform.Position.y" % PLAYER, lambda out: float(out) == 0,
                 frames=FPS, what="the astronaut on the ground")
    wait_playback(app, "ParticleStopped", frames=SETTLE)


def test_mined_deposit(app):
    start(app)
    path, d, _ = deposit_of(app, "items.Stone")
    half = DEPOSIT_AMOUNT // 2
    app.exec("set %s oreelse.OreDeposit {resource: items.Stone, amount: %d, capacity: %d, "
             "row: %d, col: %d}" % (path, half, DEPOSIT_AMOUNT, d["row"], d["col"]))
    app.wait_for("get %s oreelse.OreDeposit.amount" % path, str(half))
    aim(app, d)
    x, z = tile_point(d["row"], d["col"])

    mouse(app, True)
    app.wait_for("get %s oreelse.OreDeposit.amount" % path, str(half - 1),
                 frames=round((STONE_TIME + SWING_SLACK) * FPS), what="a stone mined")
    wait_play(app, "MineBurst", (x, MINE_Y, z))
    mouse(app, False)

    s = SCALE_MIN + (1 - SCALE_MIN) * (half - 1) / DEPOSIT_AMOUNT
    scale = app.get(path, "box.transform.Scale")
    app.check(near((scale["x"], scale["y"], scale["z"]), (s, s, s), 1e-4),
              "the deposit at %d of %d is scaled %s, not %g" %
              (half - 1, DEPOSIT_AMOUNT, scale, s))
    app.check(not plays(app, "PoofBurst"), "the deposit poofed with ore left")


def test_used_up_deposit(app):
    start(app)
    path, d, _ = deposit_of(app, "items.Stone")
    app.exec("set %s oreelse.OreDeposit {resource: items.Stone, amount: 1, capacity: %d, row: %d, "
             "col: %d}" % (path, DEPOSIT_AMOUNT, d["row"], d["col"]))
    app.wait_for("get %s oreelse.OreDeposit.amount" % path, "1")
    aim(app, d)
    x, z = tile_point(d["row"], d["col"])

    mouse(app, True)
    wait_until(app, lambda: amount(app, path) is None, round((STONE_TIME + SWING_SLACK) * FPS),
               "the last stone mined")
    wait_play(app, "PoofBurst", (x, DEPOSIT_POOF_Y, z))
    wait_play(app, "MineBurst", (x, MINE_Y, z))
    mouse(app, False)


def test_place_and_demolish(app):
    start(app)
    set_inventory(app, [("Wall", 1), ("Factory", 1)])

    # A building placed poofs on the center of its footprint
    select(app, "1", "Wall")
    click(app, 6, 6)
    app.check(building_at(app, 6, 6) is not None, "no wall placed")
    x, z = tile_point(6, 6)
    wait_play(app, "PoofBurst", (x, PLACE_POOF_Y, z))

    select(app, "0", "Factory")
    click(app, 9, 9)
    app.check(building_at(app, 9, 9) is not None, "no factory placed")
    x, z = tile_point(9, 9)
    wait_play(app, "PoofBurst", (x + 1, PLACE_POOF_Y, z + 1))

    # A building demolished poofs on the tile of the swing
    mine(app, 6, 6)
    wait_until(app, lambda: building_at(app, 6, 6) is None,
               round((DOZE_TIME + SWING_SLACK) * FPS), "the wall demolished")
    x, z = tile_point(6, 6)
    wait_play(app, "PoofBurst", (x, DEMOLISH_POOF_Y, z))
    button(app, "right", False)


def test_death(app):
    start(app)
    place_player(app, 4, 30)
    p = app.get(PLAYER, "box.transform.Position")
    app.exec("set %s oreelse.OreHealth {value: 0, max: 100}" % PLAYER)
    app.wait_for(STATE % "Lost", "true", frames=SETTLE, what="the game lost")
    r = app.get(PLAYER, "box.transform.Rotation")
    app.check(near((r["x"], r["y"], r["z"]), (DEATH_PITCH, 0, 0)),
              "the dead astronaut is turned %s, not fallen over" % r)
    wait_play(app, "SplatBurst", (p["x"], p["y"] + SPLAT_LIFT, p["z"]), frames=SETTLE)
    app.check(len(plays(app, "SplatBurst")) == 1, "the astronaut splatted %d times" %
              len(plays(app, "SplatBurst")))

    # A restart stands it up again
    app.exec("key_press R")
    app.wait_input()
    app.wait_for(STATE % "Playing", "true")
    r = app.get(PLAYER, "box.transform.Rotation")
    app.check(near((r["x"], r["y"], r["z"]), (0, math.pi, 0)),
              "after a restart the astronaut is turned %s" % r)
