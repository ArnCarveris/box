"""Ore Else driven through its command port: the look of the drones and of the rocket's flight.
A drone's rotors spin up when it takes off and down when it docks, alternating directions; it
turns toward its goal at most 7 radians a second and bobs while it welds, its weld playing
WeldSparks with their light. A launch shakes the rocket during the ignition, swings the pad's
clamps out and plays FlameBurst and SmokeBurst under the rocket through the flight.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
set up as oreelse_drones.py does (a 320x180 window without shadows, the waves held off, the
buildings placed with scripts, the inventory and the rocket set through the REST endpoints). A
fixed delta of 1/60 s per frame, waits that count frames.
"""
import math

import oreelse_drones as dr
from oreelse_drones import ARGS, FPS  # noqa: F401 (ARGS)

APP = "apps/oreelse"

ROCKET = "oreelse.OreRocketState"
PARTICLES = "get box.particle.ParticleStats ParticleStats.particles"

# The look of the drones (cfg.droneRotor, droneSpinUp, droneTurn, droneBob)
ROTOR = 34
SPIN_UP = 2.5
TURN = 7
BOB = 0.07

# Seconds a rotor takes from 0 to full spin and back, 1 / SPIN_UP
SPIN_TIME = 1 / SPIN_UP

# The weld's light at its peak (WeldSparks.peak)
WELD_PEAK = 3.4

ROTORS = ["rotor_a", "rotor_b", "rotor_c", "rotor_d"]

# The launch (cfg.ignitionTime, launchShake, clampTilt) and the height of the rocket above its
# pad (OreLaunchPad.base_y of RocketPad)
IGNITION = 1.6
SHAKE = 0.045
TILT = 0.6
BASE_Y = 0.74

CLAMPS = ["clamp_a", "clamp_b", "clamp_c", "clamp_d"]

# The rocket pad goes on the tiles (4..6, 9..11)
PAD = (4, 9)

TOLERANCE = 1e-3


def part(app, parent, base):
    """The part of an instance whose IsA target is base, the part of the prefab: the parts of an
    instance are unnamed."""
    found = list(dr.rows_of(app, "(ChildOf, %s), (IsA, %s)" % (parent, base)))
    app.check(len(found) == 1, "the parts %s of %s are %s" % (base, parent, found))
    return found[0]


def rotor(app, drone, name):
    return part(app, drone, "RepairDrone.%s" % name)


def weld(app, drone):
    return part(app, drone, "RepairDrone.weld")


def speeds(app, drone):
    """The speeds of the spinners of a drone's rotors, rotor_a to rotor_d."""
    return [app.get(rotor(app, drone, r), "OreSpinner")["speed"] for r in ROTORS]


def spin_of(app, drone):
    return abs(speeds(app, drone)[0])


def check_alternating(app, drone, spin):
    """The rotors of a drone spin at spin, the odd ones the other way."""
    got = speeds(app, drone)
    want = [spin, -spin, spin, -spin]
    app.check(all(abs(g - w) < TOLERANCE for g, w in zip(got, want)),
              "the rotors of %s spin at %s, not %s" % (drone, got, want))


def weld_state(app, drone):
    return app.exec("get %s ParticlePlayback.state" % weld(app, drone)).split(".")[-1]


def weld_light(app, drone):
    return app.get(weld(app, drone), "PointLight")["intensity"]


def wrap(a):
    """An angle in -PI to PI."""
    return (a + math.pi) % (2 * math.pi) - math.pi


def flier(app, pad):
    """The drone of a pad with a target, None for none."""
    for path, d in dr.drones_of(app, pad).items():
        if d["target"]:
            return path
    return None


def test_drone_rotors_and_weld(app):
    dr.start(app)
    dr.set_inventory(app, [(dr.IRON, 100)])
    dr.place_pad(app, "pad_a", 4, 4)

    # Docked, the rotors stand still and the welds are off
    found = dr.drones_of(app, "pad_a")
    for path in found:
        check_alternating(app, path, 0)
        app.check(weld_state(app, path) != "ParticlePlaying", "the docked %s welds" % path)

    # A damaged wall: one drone takes off and its rotors spin up to full speed within 0.4 s
    dr.place_wall(app, "wall_a", 8, 8, 200)
    dr.wait_until(app, lambda: flier(app, "pad_a") is not None,
                  2 * round(dr.SCAN * FPS) + dr.SETTLE, "a drone taking off")
    drone = flier(app, "pad_a")

    # Its turn is at most 7 radians a second
    dr.set_state(app, "Paused")
    yaw0, t0, spin0 = app.get(drone, "Rotation")["y"], dr.clock(app), spin_of(app, drone)
    dr.set_state(app, "Playing")
    app.wait_frames(3)
    dr.set_state(app, "Paused")
    yaw1, t1, spin1 = app.get(drone, "Rotation")["y"], dr.clock(app), spin_of(app, drone)
    dr.set_state(app, "Playing")
    app.check(abs(wrap(yaw1 - yaw0)) <= TURN * (t1 - t0) + TOLERANCE,
              "the drone turned %.4f rad in %.4f s" % (yaw1 - yaw0, t1 - t0))
    app.check(spin1 > spin0 or spin1 == ROTOR,
              "the rotors went from %g to %g after the take off" % (spin0, spin1))
    app.check(spin1 - spin0 <= SPIN_UP * ROTOR * (t1 - t0) + TOLERANCE,
              "the rotors spun up from %g to %g in %.4f s" % (spin0, spin1, t1 - t0))
    dr.wait_until(app, lambda: spin_of(app, drone) == ROTOR, round(SPIN_TIME * FPS) + dr.SETTLE,
                  "the rotors at full spin")
    check_alternating(app, drone, ROTOR)
    rotor_a = rotor(app, drone, "rotor_a")
    rot = app.get(rotor_a, "Rotation")["y"]
    app.wait_frames(2)
    app.check(app.get(rotor_a, "Rotation")["y"] != rot, "the rotor does not turn")

    # The others stay docked, their rotors still
    for path in found:
        if path != drone:
            check_alternating(app, path, 0)

    # Welding: the sparks play and their light is lit
    dr.wait_until(app, lambda: dr.drones(app)[drone]["state"] == "Repairing",
                  dr.flight_frames(14), "the drone welding")
    nozzle = weld(app, drone)
    app.wait_for("get %s ParticlePlayback.state" % nozzle,
                 lambda out: out.endswith("ParticlePlaying"), what="the weld sparks playing")
    app.wait_for(PARTICLES, lambda out: int(out) > 0, frames=FPS, what="the weld sparks")
    app.wait_for("get %s PointLight.intensity" % nozzle,
                 lambda out: abs(float(out) - WELD_PEAK) < 0.01,
                 what="the weld light at %g" % WELD_PEAK)
    for path in found:
        if path != drone:
            app.check(weld_state(app, path) != "ParticlePlaying" and weld_light(app, path) == 0,
                      "the docked %s welds" % path)

    # It faces the wall, toward which it flew from its bay
    p = app.get(drone, "Position")
    pad_x, pad_z = dr.tile_point(4, 4)
    wall_x, wall_z = dr.tile_point(8, 8)
    bay = dr.drones(app)[drone]["bay"]
    a = (bay + 0.5) * 2 * math.pi / dr.DRONES
    heading = math.atan2(wall_x - (pad_x + 1 + math.sin(a) * dr.BAY_RADIUS),
                         wall_z - (pad_z + 1 + math.cos(a) * dr.BAY_RADIUS))
    yaw = app.get(drone, "Rotation")["y"]
    app.check(abs(wrap(yaw - heading)) < 0.35,
              "the welding drone faces %.3f, not the wall at %.3f" % (yaw, heading))

    # It bobs about the cruise height
    heights = []
    for _ in range(12):
        heights.append(app.get(drone, "Position")["y"])
        app.wait_frames(4)
    low, high = min(heights), max(heights)
    app.check(high - low > BOB, "the welding drone bobs between %g and %g" % (low, high))
    app.check(abs(low - dr.CRUISE) < BOB + 0.03 and abs(high - dr.CRUISE) < BOB + 0.03,
              "the welding drone bobs between %g and %g about %g" % (low, high, dr.CRUISE))
    app.check(abs(p["y"] - dr.CRUISE) < BOB + 0.03, "the welding drone flies at %g" % p["y"])

    # The wall full, it flies home: the weld stops and its light goes out
    app.wait_for("get wall_a OreHealth.value", lambda out: float(out) >= dr.WALL_HP,
                 frames=round((dr.WALL_HP / dr.REPAIR + 2) * FPS), what="the wall repaired")
    app.wait_for("get %s ParticlePlayback.state" % nozzle,
                 lambda out: out.endswith("ParticleStopped"), what="the weld stopped")
    app.wait_for("get %s PointLight.intensity" % nozzle, "0", what="the weld light off")

    # Docked, the rotors slow down to a stop within 0.4 s
    dr.wait_until(app, lambda: dr.drones(app)[drone]["state"] == "Docked",
                  dr.flight_frames(14), "the drone docked")
    spins = []
    for _ in range(3):
        spins.append(spin_of(app, drone))
        app.wait_frames(2)
    app.check(spins[-1] < ROTOR and all(b <= a for a, b in zip(spins, spins[1:])),
              "the rotors of the docked drone spin at %s" % spins)
    dr.wait_until(app, lambda: spin_of(app, drone) == 0, round(SPIN_TIME * FPS) + dr.SETTLE,
                  "the rotors stopped")
    check_alternating(app, drone, 0)


def rocket(app):
    return app.get(ROCKET, "OreRocketState")


def bursts(app, name):
    """The playing instances of an effect: path -> (ParticleEffect, Position)."""
    result = {}
    for path, value in dr.rows_of(app, "box.particle.ParticleEffect").items():
        if str(value["effect"]).split(".")[-1] == name:
            result[path] = (value, app.get(path, "Position"))
    return result


def tilts(app):
    return [app.get(part(app, "rocket_pad", "RocketPad.%s" % c), "PadClamp")["tilt"]
            for c in CLAMPS]


def test_launch_fx(app):
    dr.start(app)
    dr.place(app, "rocket_pad", "RocketPad", PAD[0], PAD[1], 3, 3)
    app.wait_for("get %s OreRocketState.pad" % ROCKET, lambda out: dr.entity(out) is not None,
                 what="the rocket pad")
    dr.put(app, ROCKET, ROCKET, {"engines": 4, "fuel": 1, "cargo": 1})
    app.wait_for("get %s OreRocketState.launch" % ROCKET, "true", what="a valid rocket")
    x, z = dr.tile_point(PAD[0] + 1, PAD[1] + 1)

    # On the pad: straight, the clamps in, no flame
    r = app.get("rocket", "Rotation")
    app.check(abs(r["x"]) < TOLERANCE and abs(r["z"]) < TOLERANCE, "the idle rocket leans %s" % r)
    app.check(all(abs(t) < TOLERANCE for t in tilts(app)), "the clamps tilt %s" % tilts(app))
    app.check(not bursts(app, "FlameBurst") and not bursts(app, "SmokeBurst"),
              "flames before the launch")

    app.exec("key.press P")
    app.wait_input()
    app.wait_for("get %s OreRocketState.phase" % ROCKET, lambda out: "Ignition" in out,
                 what="the ignition")

    # The ignition shakes the rocket by (sin(t * 47), 0, cos(t * 39)) * 0.045 at the play time t
    seen = set()
    for _ in range(6):
        dr.set_state(app, "Paused")
        if "Ignition" in str(rocket(app)["phase"]):
            r = app.get("rocket", "Rotation")
            t = dr.clock(app)
            app.check(abs(r["x"]) <= SHAKE + TOLERANCE and abs(r["z"]) <= SHAKE + TOLERANCE and
                      abs(r["y"]) < TOLERANCE, "the rocket leans %s" % r)
            app.check(any(abs(r["x"] - math.sin(c * 47) * SHAKE) < TOLERANCE and
                          abs(r["z"] - math.cos(c * 39) * SHAKE) < TOLERANCE
                          for c in (t, t - dr.DT)),
                      "at %g the rocket leans %s" % (t, r))
            seen.add((round(r["x"], 5), round(r["z"], 5)))
        dr.set_state(app, "Playing")
        app.wait_frames(2)
    app.check(len(seen) > 2, "the rocket shook through %s" % seen)

    # The clamps swing out, flames and smoke play under the rocket on the pad
    dr.wait_until(app, lambda: all(abs(t - TILT) < TOLERANCE for t in tilts(app)), dr.SETTLE,
                  "the clamps swung out")
    for name in ("FlameBurst", "SmokeBurst"):
        found = bursts(app, name)
        app.check(found, "no %s in the ignition" % name)
        app.check(any(abs(p["x"] - x) < 0.01 and abs(p["z"] - z) < 0.01 and
                      abs(p["y"] - BASE_Y) < 0.01 for _, p in found.values()),
                  "the %s plays at %s, not under the rocket" % (name, list(found.values())))

    # The ascent: straight again, still burning, the newest flames under the rising rocket (the
    # particles keep playing while the game is paused, so the newest is the youngest, wherever
    # its age got to)
    app.wait_for("get %s OreRocketState.phase" % ROCKET, lambda out: "Ascent" in out,
                 frames=round((IGNITION + 0.5) * FPS), what="the ascent")
    app.wait_frames(round(FPS * 0.6))
    dr.set_state(app, "Paused")
    r = app.get("rocket", "Rotation")
    app.check(abs(r["x"]) < TOLERANCE and abs(r["z"]) < TOLERANCE,
              "the rocket leans %s in the ascent" % r)
    y = rocket(app)["y"]
    app.check(y > BASE_Y + 1, "the rocket is at %g" % y)
    for name in ("FlameBurst", "SmokeBurst"):
        found = bursts(app, name)
        newest = min(found.values(), key=lambda v: v[0]["time"]) if found else None
        app.check(newest is not None and newest[1]["y"] > BASE_Y + 0.5 and
                  0 <= y - newest[1]["y"] < 1.5 and
                  abs(newest[1]["x"] - x) < 0.01 and abs(newest[1]["z"] - z) < 0.01,
                  "the newest %s of the ascent is %s, the rocket at %g" % (name, newest, y))
    app.check(all(abs(t - TILT) < TOLERANCE for t in tilts(app)),
              "the clamps tilt %s in the ascent" % tilts(app))
    dr.set_state(app, "Playing")

    # It arrives: the clamps come back in
    app.wait_for("get %s OreRocketState.phase" % ROCKET, lambda out: "Idle" in out,
                 frames=6 * FPS, what="the arrival")
    dr.wait_until(app, lambda: all(abs(t) < TOLERANCE for t in tilts(app)), dr.SETTLE,
                  "the clamps back in")
