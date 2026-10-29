"""Ore Else's building animation and lights driven through its command port: a working factory's
roller drums and flywheels spin, its ram strokes, its vents steam and its work glow lights, all at
the work rate of the factory, and they stop where they are in a blackout and when its queue is
empty; a drill on a deposit spins its bit, strokes its carriage and slider and throws dust, and
stops when the deposit is gone, its bit back at rest; nothing moves while the game is paused, the
emitters paused with it; the point light of a status lamp shines with its bulb and blinks with it
in a blackout.

The setup is oreelse_power.py's: a fresh apps/oreelse process per case with its default scene and
a fixed session seed, a 640x360 window without shadows, a fixed delta of 1/60 s, waits that count
frames, buildings placed directly with scripts and the state written through the REST endpoints
of the port. The parts of a placed building are unnamed instances of the prefab's parts, found by
their components among its descendants. To measure the animation a case pauses the game, plays a
few frames and pauses again: the parts move only while playing, as does the clock, so the clock
tells how much play the frames were.
"""
import math

from oreelse_power import (DRILL, ENGINE, FACTORY, PANEL, PLAYER, crafter, deposit_of, deposits,
                           entity, lamps_of, order, orders, place, play, rows_of, run_script,
                           set_state, start, wait_lamps, wait_power)

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

DT = 1 / 60

# The factory's parts (etc/buildings/factory.flecs): four roller drums and two flywheels spinning
# at 5 radians a second, the ram stroking at 6 from its rest at 1.72 down 0.62, the three vents
# of the grille steaming and the work glow lighting up to 5.5
FACTORY_SPIN = 5
FACTORY_SPINNERS = 6
RAM = {"rest": 1.72, "travel": 0.62, "speed": 6}
FACTORY_STEAM = 3

# The drill's parts (etc/buildings/drill.flecs): the bit spinning at 7 and shaking about its
# rest at y 0, the carriage stroking at 2.4 along y and the slider at 0.9 along z, the dust
CARRIAGE = {"rest": 1.48, "travel": 0.26, "speed": 2.4}
SLIDER = {"rest": 0.12, "travel": 0.24, "speed": 0.9}
BIT_SPIN = 7
BIT_REST = 0

# The point light of a status lamp (cfg.statusLampLight) and the OK color of a lamp at level 2
STATUS_LIGHT = 0.35
OK = (124, 226, 148)

TOLERANCE = 1e-3

RESULTS = [0]


def under(app, path, expr):
    """The results of a query among the descendants of an entity, whose parts are unnamed
    instances of the prefab's: path -> the value of the first field."""
    return rows_of(app, "%s, ChildOf(self|up ChildOf, %s)" % (expr, path))


def parts(app, path, component, count):
    """The parts of a building with a component: path -> its value; there are count of them."""
    found = under(app, path, "oreelse.%s" % component)
    app.check(len(found) == count, "%s has the %s parts %s, not %d" % (
        path, component, list(found), count))
    return found


def spins(app, paths):
    """The Rotation.y of every part."""
    return {p: app.get(p, "Rotation")["y"] for p in paths}


def stroke(phase):
    return 0.5 - 0.5 * math.cos(phase)


def piston(app, path):
    """The phase of a piston and the position of its part."""
    return app.get(path, "OrePiston")["phase"], app.get(path, "Position")


def check_piston(app, path, spec, axis, phase):
    """Check that the part of a piston sits where its phase puts it."""
    p = app.get(path, "Position")
    want = spec["rest"] - spec["travel"] * stroke(phase)
    app.check(abs(p[axis] - want) < TOLERANCE,
              "at the phase %g the piston %s is at %s %g, not %g" % (phase, path, axis, p[axis],
                                                                    want))


def playback(app, emitter):
    return app.exec("get %s ParticlePlayback.state" % emitter).split(".")[-1]


def wait_playback(app, emitters, state, what):
    """Wait until every emitter is in a playback state."""
    def shows(_):
        return all(playback(app, e).endswith(state) for e in emitters)
    app.wait_for("get oreelse.OreClock OreClock.elapsed", shows, what=what)


def check_playback(app, emitters, state, what):
    for e in emitters:
        app.check(playback(app, e).endswith(state), "%s: the emitter %s is %s, not %s" % (
            what, e, playback(app, e), state))


def check_still(app, before, after, what):
    for path, value in before.items():
        app.check(abs(after[path] - value) < 1e-6,
                  "%s the part %s turned from %g to %g" % (what, path, value, after[path]))


def working_factory(app):
    """A factory with power crafting a rocket engine, the astronaut's crafter paused: its path.
    The game plays."""
    start(app)
    app.exec("set %s oreelse.OreCrafter {speed: 1, paused: true}" % PLAYER)
    for i in range(4):
        place(app, "panel_%d" % i, "SolarPanel", 4, 3 + i)
    place(app, "factory_a", "Factory", 6, 6, 2, 2)
    app.wait_for("has factory_a oreelse.OreCrafter", "true", what="the factory's crafter")
    wait_power(app, 4 * PANEL, FACTORY, False)
    app.check(order(app, ENGINE, 1) == 1, "the order of an engine failed")
    app.wait_for("get factory_a OreCrafter.item", lambda out: entity(out) == ENGINE,
                 what="the factory crafting the engine")
    return "factory_a"


def drill_on_stone(app):
    """A drill with power on a stone deposit, mining: its path and the deposit's. The game
    plays."""
    start(app)
    row, col = deposit_of(app, "items.Stone")
    place(app, "panel_a", "SolarPanel", 4, 4)
    place(app, "drill_a", "Drill", row, col)
    wait_power(app, PANEL, DRILL, False)
    app.wait_for("get drill_a OreDrill.running", "true", what="the drill mining")
    found = [p for p, d in deposits(app).items() if (d["row"], d["col"]) == (row, col)]
    app.check(len(found) == 1, "the deposits of the tile %s are %s" % ((row, col), found))
    return "drill_a", found[0]


def cancel(app, order_id):
    """Take a unit off an order with oreelse.craft_cancel(); returns whether it was taken."""
    RESULTS[0] += 1
    name = "anim_cancel_%d" % RESULTS[0]
    run_script(app, "%s { oreelse.OreCanOrder: {value: oreelse.craft_cancel(%d)} }" %
               (name, order_id))
    return app.get(name, "OreCanOrder")["value"]


def test_factory(app):
    factory = working_factory(app)
    spinners = parts(app, factory, "OreSpinner", FACTORY_SPINNERS)
    ram = list(parts(app, factory, "OrePiston", 1))[0]
    steam = list(parts(app, factory, "OreWorkEmitter", FACTORY_STEAM))
    glow = under(app, factory, "oreelse.OreWorkLight, box.light.PointLight")
    app.check(len(glow) == 1, "the factory's lit work lights are %s" % list(glow))
    glow = list(glow)[0]
    wait_playback(app, steam, "Playing", "the vents steaming while the factory works")

    # Working at level 2 (multiplier 1): the drums and flywheels turn at 5 radians a second and
    # the ram strokes at 6
    set_state(app, "Paused")
    before = spins(app, spinners)
    phase, _ = piston(app, ram)
    played = play(app, 12)
    app.check(played > 0, "no play passed")
    after = spins(app, spinners)
    for path in spinners:
        turned = after[path] - before[path]
        app.check(abs(turned - FACTORY_SPIN * played) < FACTORY_SPIN * 2 * DT + TOLERANCE,
                  "in %.3f s of work the part %s turned %g, not %g" % (
                      played, path, turned, FACTORY_SPIN * played))
    moved, _ = piston(app, ram)
    app.check(abs(moved - phase - RAM["speed"] * played) < RAM["speed"] * 2 * DT + TOLERANCE,
              "in %.3f s of work the ram's phase went from %g to %g" % (played, phase, moved))
    check_piston(app, ram, RAM, "y", moved)
    intensity = app.get(glow, "PointLight")["intensity"]
    app.check(intensity > 0, "the work glow of a working factory is dark")
    wait_playback(app, steam, "Paused", "the steam frozen while paused")

    # In a blackout everything stops where it is
    set_state(app, "Playing")
    app.exec("rm panel_3")
    wait_power(app, 3 * PANEL, FACTORY, True)
    wait_playback(app, steam, "Stopped", "the steam stopped in a blackout")
    set_state(app, "Paused")
    before = spins(app, spinners)
    phase, at = piston(app, ram)
    play(app, 12)
    check_still(app, before, spins(app, spinners), "in a blackout")
    moved, now = piston(app, ram)
    app.check(moved == phase and abs(now["y"] - at["y"]) < 1e-6,
              "in a blackout the ram went from %g at %g to %g at %g" % (phase, at["y"], moved,
                                                                       now["y"]))
    check_playback(app, steam, "Stopped", "in a blackout")

    # With the power back it works again
    set_state(app, "Playing")
    place(app, "panel_4", "SolarPanel", 4, 7)
    wait_power(app, 4 * PANEL, FACTORY, False)
    wait_playback(app, steam, "Playing", "the vents steaming with the power back")
    set_state(app, "Paused")
    before = spins(app, spinners)
    played = play(app, 12)
    after = spins(app, spinners)
    app.check(all(after[p] - before[p] > FACTORY_SPIN * played / 2 for p in spinners),
              "with the power back the parts turned %s" % {
                  p: after[p] - before[p] for p in spinners})

    # With its queue empty it stands idle
    set_state(app, "Playing")
    app.check(cancel(app, orders(app)[0]["id"]), "the cancel of the engine was refused")
    app.wait_for("get factory_a OreCrafter.item", lambda out: entity(out) is None,
                 what="the factory idle")
    app.check(not orders(app), "the queue is not empty: %s" % orders(app))
    wait_playback(app, steam, "Stopped", "the steam stopped with the queue empty")
    set_state(app, "Paused")
    before = spins(app, spinners)
    phase, _ = piston(app, ram)
    play(app, 12)
    check_still(app, before, spins(app, spinners), "with the queue empty")
    app.check(piston(app, ram)[0] == phase, "with the queue empty the ram moved")
    app.check(crafter(app, factory)["item"] is None, "the idle factory picked up a craft")


def test_drill(app):
    drill, deposit = drill_on_stone(app)
    bit = list(parts(app, drill, "OreShake", 1))[0]
    pistons = parts(app, drill, "OrePiston", 2)
    dust = list(parts(app, drill, "OreWorkEmitter", 1))
    app.check(bit in parts(app, drill, "OreSpinner", 1), "the drill's bit does not spin")
    wait_playback(app, dust, "Playing", "the dust flying while the drill mines")

    # Mining at level 2 (multiplier 1): the bit turns at 7 radians a second and shakes, the
    # carriage and the slider stroke
    set_state(app, "Paused")
    before = spins(app, [bit])
    phases = {p: piston(app, p)[0] for p in pistons}
    played = play(app, 12)
    turned = spins(app, [bit])[bit] - before[bit]
    app.check(abs(turned - BIT_SPIN * played) < BIT_SPIN * 2 * DT + TOLERANCE,
              "in %.3f s of mining the bit turned %g, not %g" % (played, turned,
                                                              BIT_SPIN * played))
    for path, value in pistons.items():
        spec, axis = (SLIDER, "z") if value["slide"] else (CARRIAGE, "y")
        moved = piston(app, path)[0]
        app.check(abs(moved - phases[path] - spec["speed"] * played) <
                  spec["speed"] * 2 * DT + TOLERANCE,
                  "in %.3f s of mining the piston %s went from %g to %g" % (
                      played, path, phases[path], moved))
        check_piston(app, path, spec, axis, moved)

    # The deposit gone, the drill stands idle, its bit back at rest and the dust stopped
    set_state(app, "Playing")
    app.exec("rm %s" % deposit)
    app.wait_for("get drill_a OreDrill.running", "false", what="the drill idle")
    wait_playback(app, dust, "Stopped", "the dust stopped without a deposit")
    set_state(app, "Paused")
    before = spins(app, [bit])
    phases = {p: piston(app, p)[0] for p in pistons}
    play(app, 12)
    check_still(app, before, spins(app, [bit]), "without a deposit")
    for path in pistons:
        app.check(piston(app, path)[0] == phases[path],
                  "without a deposit the piston %s moved" % path)
    y = app.get(bit, "Position")["y"]
    app.check(abs(y - BIT_REST) < 1e-6, "the idle drill's bit is at %g, not at rest" % y)


def test_pause(app):
    factory = working_factory(app)
    spinners = parts(app, factory, "OreSpinner", FACTORY_SPINNERS)
    ram = list(parts(app, factory, "OrePiston", 1))[0]
    steam = list(parts(app, factory, "OreWorkEmitter", FACTORY_STEAM))
    wait_playback(app, steam, "Playing", "the vents steaming while the factory works")

    # Paused, the parts stand where they are and the steam is frozen
    set_state(app, "Paused")
    before = spins(app, spinners)
    phase, at = piston(app, ram)
    app.wait_frames(12)
    check_still(app, before, spins(app, spinners), "while paused")
    moved, now = piston(app, ram)
    app.check(moved == phase and abs(now["y"] - at["y"]) < 1e-6,
              "while paused the ram went from %g to %g" % (phase, moved))
    wait_playback(app, steam, "Paused", "the steam frozen while paused")

    # Resumed, they go on
    set_state(app, "Playing")
    wait_playback(app, steam, "Playing", "the steam playing again after the pause")
    app.wait_frames(6)
    after = spins(app, spinners)
    app.check(all(after[p] != before[p] for p in spinners), "the parts did not resume")


def test_status_lamp_light(app):
    start(app)
    row, col = deposit_of(app, "items.Stone")
    place(app, "panel_a", "SolarPanel", 4, 4)
    place(app, "drill_a", "Drill", row, col)
    wait_power(app, PANEL, DRILL, False)
    wait_lamps(app, "drill_a", OK, "the drill's lamps green at level 2")
    bulbs = lamps_of(app, "drill_a")
    app.check(len(bulbs) == 2, "the drill has %d status lamps, not 2" % len(bulbs))

    # With power the lamps shine steadily
    for _ in range(3):
        for b in bulbs:
            light = app.get(b, "PointLight")["intensity"]
            app.check(abs(light - STATUS_LIGHT) < 1e-4,
                      "the status lamp %s shines at %g, not %g" % (b, light, STATUS_LIGHT))
        app.wait_frames(5)

    # In a blackout their light blinks with them
    app.exec("rm panel_a")
    wait_power(app, 0, DRILL, True)
    bulb = bulbs[0]
    app.wait_for("get %s PointLight.intensity" % bulb, lambda out: float(out) == 0,
                 frames=120, what="the dead lamp's light in its off half")
    app.wait_for("get %s PointLight.intensity" % bulb,
                 lambda out: abs(float(out) - STATUS_LIGHT) < 1e-4,
                 frames=120, what="the dead lamp's light in its on half")
    app.wait_for("get %s PointLight.intensity" % bulb, lambda out: float(out) == 0,
                 frames=120, what="the dead lamp's light off again")
