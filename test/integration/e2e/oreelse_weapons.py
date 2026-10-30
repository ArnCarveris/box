"""Ore Else's weapons driven through its command port: the astronaut's automatic pistol (its
range, its timer held at 0 without a target, two hits for a mite), the gun turret (its rate at
Weapons level 2, nothing at level 0, still firing in a blackout, the lead that hits a mite
crossing in front of it), the laser (50 HP/s at level 2, 100 at level 4, nothing in a blackout,
its beam shown while it fires), the missiles (the splash that kills every mite within 5 of a
brute it hits, a missile whose target died taking another within 6), shots hitting a critter
that is not their target, and no friendly fire on walls.

The setup is oreelse_critters.py's: a fresh apps/oreelse process per case with its default scene
and a fixed session seed, a 640x360 window without shadows, a fixed delta of 1/60 s, waits that
count frames, critters spawned with oreelse.spawn_critter() and standing still (speed 0) where
the case wants them, and buildings created by scripts on chosen tiles of the corner of the map
of seed 8 without deposits (see oreelse_power.py). The astronaut's pistol is held, but in the
pistol's case, and the astronaut stands far away from the turrets. Rates are measured over the
play time of OreClock between two pauses, so they hold however many frames the commands take.
"""
import math

from box_client import parse_value
from oreelse_critters import (FPS, PLAYER, WAVE, critters, entity, health, hold_waves, measure,
                              place_player, put, rows_of, run_script, set_state, spawn, start,
                              target, tile_point, wait_until, waves)

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

POWER = "oreelse.OrePowerState"
ALLOC = "oreelse.OrePowerAlloc"

# The critters' hit points (etc/critters)
MITE_HP = 12
WALL_HP = 300

# The pistol (cfg.pistolDamage, cfg.pistolRange) and where the astronaut stands to fire it,
# beyond the deposit clusters
PISTOL_DAMAGE = 6
PISTOL_AT = (30, 30)

# The turrets (etc/buildings): the gun's damage per shot and interval, the laser's damage per
# second
GUN_DAMAGE = 2.5
GUN_INTERVAL = 0.12
LASER_DPS = 50

# The tile of the turret of a case, the tiles of the solar panels that power it (2 x 20 W, more
# than the 25 W of a laser or the 20 W of a missile turret) and where the astronaut stands, far
# away from them
TURRET = (8, 8)
PANELS = [(5, 5), (5, 6)]
FAR = (40, 40)

# Hit points of a critter that has to last a measure
SPONGE = 100000

# Frames of play of a measured rate and the tolerance of the laser's damage per second
RATE_FRAMES = 120
DPS_TOLERANCE = 0.5


def building(app, prefab, row, col):
    """Place a building directly: an instance of its prefab on a tile, with its OreBuilding, so
    it is registered in the tile grid. Returns its name."""
    x, z = tile_point(row, col)
    code = ("using box.transform\n@NAME : %s {\n  Position: {%g, 0, %g}\n"
            "  oreelse.OreBuilding: {row: %d, col: %d}\n}" % (prefab, x, z, row, col))
    name = run_script(app, code)
    app.wait_for("eval %s.has(oreelse.OreBuilding)" % name, "true")
    return name


def still(app, prefab, x, z, hp=None):
    """Spawn a critter that stands where it is (speed 0), with hp hit points when given."""
    critter = spawn(app, prefab, x, z)
    app.exec("set %s oreelse.OreSpeed {value: 0}" % critter)
    if hp is not None:
        app.exec("set %s oreelse.OreHealth {value: %g, max: %g}" % (critter, hp, hp))
    return critter


def set_levels(app, mining, weapons, construction):
    put(app, ALLOC, ALLOC, {"level": [mining, weapons, construction]})
    app.wait_for("get %s oreelse.OrePowerAlloc" % ALLOC,
                 lambda out: parse_value(out)["level"] == [mining, weapons, construction])


def wait_blackout(app, blackout):
    """Play until the power state is in a blackout or out of it."""
    set_state(app, "Playing")
    app.wait_for("get %s oreelse.OrePowerState.blackout" % POWER, "true" if blackout else "false",
                 frames=FPS, what="the blackout %s" % blackout)


def instances(app, prefab):
    """The instances of a prefab that are not disabled: the projectiles in flight, the beams that
    show."""
    return rows_of(app, "(IsA, %s)" % prefab)


def powered(app):
    """Place the solar panels of a case; returns their names."""
    return [building(app, "SolarPanel", row, col) for row, col in PANELS]


def turret_at():
    return tile_point(*TURRET)


def once(app, turret):
    """Let a turret fire only once: its first shot goes at once, the next one after an interval
    set far off."""
    app.exec("set %s oreelse.OreAttackInterval {value: 100000}" % turret)


def check_gun_rate(app, lost, took, what):
    """Check the hits of a gun turret over a measure: 8 to 9 shots per second at level 2."""
    hits = lost / GUN_DAMAGE
    want = took / GUN_INTERVAL
    app.check(abs(hits - round(hits)) < 1e-3, "%s the skitter lost %g HP, not a number of hits" %
              (what, lost))
    app.check(abs(hits - want) <= 1 + 1e-3,
              "%s the gun turret hit %g times in %g s (%g shots/s), not %g" %
              (what, hits, took, hits / took, want))


def test_pistol(app):
    start(app)
    hold_waves(app)
    px, pz = PISTOL_AT
    place_player(app, px, pz)
    set_state(app, "Paused")

    # A mite 10 units away is out of range: no shot, and the timer holds at 0
    far = still(app, "Mite", px + 10, pz)
    app.exec("set %s oreelse.OreAttackTimer {value: 0}" % PLAYER)
    set_state(app, "Playing")
    app.wait_frames(FPS)
    set_state(app, "Paused")
    app.check(health(app, far) == MITE_HP, "the mite 10 units away has %g HP" % health(app, far))
    app.check(not instances(app, "Tracer"), "the pistol fired at the mite 10 units away")
    timer = app.get(PLAYER, "oreelse.OreAttackTimer")["value"]
    app.check(timer == 0, "without a target the pistol's timer is at %g" % timer)

    # A mite 8 units away is shot at once: a hit takes 6 HP and the second kills it
    kills = waves(app)["kills"]
    near = still(app, "Mite", px, pz + 8)
    set_state(app, "Playing")
    wait_until(app, lambda: health(app, near) < MITE_HP, FPS, "the pistol's first hit")
    hp = health(app, near)
    app.check(abs(hp - (MITE_HP - PISTOL_DAMAGE)) < 1e-4, "after the first hit the mite has %g HP"
              % hp)
    app.wait_for(WAVE % "kills", str(kills + 1), frames=2 * FPS,
                 what="the mite killed by the second hit")
    set_state(app, "Paused")
    app.check(near not in critters(app), "the mite shot twice is still there")
    app.check(health(app, far) == MITE_HP, "the mite 10 units away has %g HP" % health(app, far))


def test_gun_turret(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    building(app, "GunTurret", *TURRET)
    tx, tz = turret_at()
    set_state(app, "Paused")
    skitter = still(app, "Skitter", tx + 6, tz, SPONGE)

    # At level 2 a shot every 0.12 s: 8.33 a second
    set_state(app, "Playing")
    wait_until(app, lambda: health(app, skitter) < SPONGE, FPS, "the gun turret's first hit")
    lost, took = measure(app, lambda: -health(app, skitter), RATE_FRAMES)
    check_gun_rate(app, lost, took, "at level 2")

    # At level 0 it does not fire; the shots in flight land first
    set_levels(app, 2, 0, 2)
    set_state(app, "Playing")
    app.wait_frames(FPS)
    lost, took = measure(app, lambda: -health(app, skitter), FPS)
    app.check(lost == 0, "at Weapons level 0 the skitter lost %g HP in %g s" % (lost, took))

    # It draws no power, so a blackout does not stop it: a laser turret far away without a solar
    # panel puts the base in a blackout
    set_levels(app, 2, 2, 2)
    building(app, "LaserTurret", 12, 3)
    hp = health(app, skitter)
    wait_blackout(app, True)
    # Back from level 0 it has no shot in flight: measure once the first one landed
    wait_until(app, lambda: health(app, skitter) < hp, FPS, "the gun turret's hit in a blackout")
    lost, took = measure(app, lambda: -health(app, skitter), RATE_FRAMES)
    app.check(app.get(POWER, "oreelse.OrePowerState")["blackout"], "the blackout ended")
    check_gun_rate(app, lost, took, "in a blackout")


def test_gun_lead(app):
    start(app)
    hold_waves(app)
    building(app, "GunTurret", *TURRET)
    tx, tz = turret_at()
    set_state(app, "Paused")

    # The mite walks north 6 units east of the turret, toward the astronaut far ahead, without
    # looking for another target: it crosses in front of the turret at 6.8 units a second
    place_player(app, tx + 6, tz + 30)
    mite = spawn(app, "Mite", tx + 6, tz - 6)
    app.exec("set %s oreelse.OreCritter {retarget: 100, jitter: 0, attacking: false}" % mite)
    app.exec("set %s oreelse.OreTarget {value: %s}" % (mite, PLAYER))
    set_state(app, "Playing")
    wait_until(app, lambda: mite not in critters(app) or health(app, mite) < MITE_HP, 2 * FPS,
               "the crossing mite hit")
    set_state(app, "Paused")
    if mite in critters(app):
        x, z = app.get(mite, "box.transform.Position")["x"], app.get(mite,
                                                                     "box.transform.Position")["z"]
        app.check(z > tz - 6 + 0.5, "the mite at %s did not walk" % ((x, z),))


def test_shot_hits_other(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    gun = building(app, "GunTurret", *TURRET)
    tx, tz = turret_at()
    set_state(app, "Paused")

    # The turret takes the skitter 9 units east and keeps it while it lives in range
    aimed = still(app, "Skitter", tx + 9, tz, SPONGE)
    set_state(app, "Playing")
    wait_until(app, lambda: target(app, gun) == aimed, FPS, "the turret targeting the skitter")
    set_state(app, "Paused")

    # A skitter in the way, closer, is hit by the shots at the other
    between = still(app, "Skitter", tx + 4.5, tz)
    hp = health(app, between)
    set_state(app, "Playing")
    wait_until(app, lambda: health(app, between) < hp, FPS, "the skitter in the way hit")
    set_state(app, "Paused")
    app.check(target(app, gun) == aimed, "the turret switched to %s" % target(app, gun))


def test_no_friendly_fire(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    building(app, "GunTurret", *TURRET)
    wall = building(app, "Wall", TURRET[0], TURRET[1] + 2)
    tx, tz = turret_at()
    set_state(app, "Paused")

    # The shots at the mite 8 units east pass over the wall 4 units east
    mite = still(app, "Mite", tx + 8, tz)
    set_state(app, "Playing")
    wait_until(app, lambda: mite not in critters(app), 3 * FPS, "the mite shot dead")
    set_state(app, "Paused")
    app.check(health(app, wall) == WALL_HP, "the wall the shots passed over has %g HP" %
              health(app, wall))


def test_laser(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    panels = powered(app)
    building(app, "LaserTurret", *TURRET)
    tx, tz = turret_at()
    set_state(app, "Paused")
    skitter = still(app, "Skitter", tx + 8, tz, SPONGE)

    # At level 2 it burns 50 HP a second, its beam showing
    set_state(app, "Playing")
    wait_until(app, lambda: health(app, skitter) < SPONGE, FPS, "the laser burning")
    app.check(len(instances(app, "LaserBeam")) == 1, "while the laser fires the beams are %s" %
              list(instances(app, "LaserBeam")))
    lost, took = measure(app, lambda: -health(app, skitter))
    app.check(abs(lost / took - LASER_DPS) < DPS_TOLERANCE,
              "at level 2 the laser dealt %g HP/s, not %g" % (lost / took, LASER_DPS))

    # At level 4 twice that
    set_levels(app, 0, 4, 2)
    lost, took = measure(app, lambda: -health(app, skitter))
    app.check(abs(lost / took - 2 * LASER_DPS) < DPS_TOLERANCE,
              "at level 4 the laser dealt %g HP/s, not %g" % (lost / took, 2 * LASER_DPS))

    # In a blackout nothing, and the beam is gone
    for p in panels:
        app.exec("rm %s" % p)
    wait_blackout(app, True)
    app.wait_frames(2)
    app.check(not instances(app, "LaserBeam"), "in a blackout the beams are %s" %
              list(instances(app, "LaserBeam")))
    lost, took = measure(app, lambda: -health(app, skitter))
    app.check(lost == 0, "in a blackout the laser dealt %g HP in %g s" % (lost, took))


def test_missile_splash(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    powered(app)
    turret = building(app, "MissileTurret", *TURRET)
    wall = building(app, "Wall", TURRET[0] + 1, TURRET[1] + 5)
    tx, tz = turret_at()
    set_state(app, "Paused")

    # The brute is the nearest: the missile flies to it. Two mites stand within 5 of where it
    # hits, one 6.5 to the side of the brute, and a wall 2 units from it
    brute = still(app, "Brute", tx + 10, tz)
    near = [still(app, "Mite", tx + 11.5, tz + 2), still(app, "Mite", tx + 12, tz - 3)]
    far = still(app, "Mite", tx + 10, tz + 6.5)

    # One missile: it fires at once, and the next is far off
    once(app, turret)
    set_state(app, "Playing")
    wait_until(app, lambda: brute not in critters(app), 2 * FPS, "the brute hit")
    app.wait_frames(2)
    set_state(app, "Paused")
    alive = critters(app)
    app.check(all(m not in alive for m in near), "the mites within 5 of the brute survived: %s" %
              [m for m in near if m in alive])
    app.check(far in alive and health(app, far) == MITE_HP,
              "the mite 6.5 from the brute was hurt or killed")
    app.check(health(app, wall) == WALL_HP, "the splash hurt the wall: %g HP" % health(app, wall))


def test_missile_reacquire(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    powered(app)
    turret = building(app, "MissileTurret", *TURRET)
    tx, tz = turret_at()
    set_state(app, "Paused")
    first = still(app, "Mite", tx + 15, tz)
    once(app, turret)

    set_state(app, "Playing")
    wait_until(app, lambda: instances(app, "Missile"), FPS, "the missile")
    set_state(app, "Paused")
    missile = list(instances(app, "Missile"))[0]
    app.check(entity(app.get(missile, "oreelse.OreTarget")["value"]) == first,
              "the missile flies to %s, not the mite" %
              app.get(missile, "oreelse.OreTarget")["value"])

    # Its target dies; another mite stands 3 ahead of it and 3 to the side, within 6
    p = app.get(missile, "box.transform.Position")
    v = app.get(missile, "oreelse.OreProjectile")
    speed = math.hypot(v["vx"], v["vz"])
    dx, dz = v["vx"] / speed, v["vz"] / speed
    other = still(app, "Mite", p["x"] + 3 * dx - 3 * dz, p["z"] + 3 * dz + 3 * dx)
    kills = waves(app)["kills"]
    app.exec("set %s oreelse.OreHealth {value: 0, max: %g}" % (first, MITE_HP))
    set_state(app, "Playing")
    wait_until(app, lambda: other not in critters(app), 2 * FPS,
               "the missile taking the other mite")
    set_state(app, "Paused")
    app.check(waves(app)["kills"] == kills + 2, "%d kills, not the two mites" %
              (waves(app)["kills"] - kills))
