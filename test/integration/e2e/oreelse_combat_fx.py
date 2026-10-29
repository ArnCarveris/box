"""Ore Else's combat effects (src/combat_fx.c) driven through its command port: a firing laser
shows its beam from the lens of its head, plays the glow at the lens and the sparks where the beam
lands, set back from its end, with their lights at their peaks, pauses them with the game and stops
them, lights out and beam gone, when it stops firing; a gun shot throws sparks at the muzzle and
flashes the muzzle light, which fades to 0, and its hit on a critter bursts into sparks and a puff;
a missile launch plays MissileLaunch turned to the aim and the missile turret's flash, the missile
points along its velocity, trails lingering smoke that pauses with the game and explodes with its
flash; shots that reach the ground throw up dust and sparks, missiles explode scaled by their
splash; a destroyed wall explodes.

The setup is oreelse_weapons.py's: a fresh apps/oreelse process per case with its default scene
and a fixed session seed, a 640x360 window without shadows, a fixed delta of 1/60 s, waits that
count frames, critters spawned with oreelse.spawn_critter() and standing still (speed 0) with hit
points to spare, and buildings created by scripts on chosen tiles of the corner of the map of
seed 8 without deposits. The astronaut's pistol is held and it stands far away from the turrets.
The effects are read from the turrets' parts (head.fx, hit_fx), the box.particle.ParticleEffect
of the effect instances that play and box.particle.ParticleStats.
"""
import math

from oreelse_critters import (FPS, buildings, health, hold_waves, place_player, rows_of,
                              run_script, set_state, start, wait_until)
from oreelse_weapons import FAR, TURRET, building, instances, once, powered, still, turret_at

APP = "apps/oreelse"
SEED = 8
ARGS = ["--seed", str(SEED), "--fps", "20"]

PARTICLES = "get box.particle.ParticleStats ParticleStats.particles"

# The skitter's and the brute's radius (etc/critters), the aim of the beam (cfg.turretAimPerRadius)
# and the set back of its sparks (cfg.turretHitInset)
SKITTER_RADIUS = 0.56
AIM_PER_RADIUS = 0.9
HIT_INSET = 0.9

# The laser turret's head and lens (etc/buildings/laser_turret.flecs) and the peaks of the lights
# of its BeamGlow and BeamSparks (etc/particles)
LASER_HEAD = (0, 1.68, 0.04)
LASER_LENS = (0, 0.02, 0.82)
GLOW_PEAK = 4.5
SPARKS_PEAK = 5

# The gun turret's muzzle (its OreMuzzle: height, forward) and its flash: peak 5, fading at 45 a
# second; the missile turret's flash: peak 6, fading at 20 a second
GUN_MUZZLE = (1.7, 1.06)
GUN_FLASH = (5, 5 / 45)
MISSILE_FLASH = (6, 6 / 20)

# The explosions (cfg.explosionPeak, cfg.explosionRange): a missile's 0.2 above where it ends,
# scaled clamp(splash / 5, 0.65, 1.25); a destroyed building's 0.55 above the ground at 0.8
BOOM_PEAK = 9
BOOM_RANGE = 13
BLAST_LIFT = 0.2
WRECK_Y = 0.55
WRECK_SCALE = 0.8

# A shot that reaches the ground plays its dust and sparks at this height (cfg.impactY)
IMPACT_Y = 0.05

# Hit points of a critter that has to last the case
SPONGE = 100000

# Frames an effect is allowed beyond its time
MARGIN = FPS // 2

TOLERANCE = 0.01


def frames(seconds):
    return int(math.ceil(seconds * FPS)) + MARGIN


def near(a, b, tolerance=TOLERANCE):
    return abs(a - b) <= tolerance * max(1, abs(b))


def light(app, path):
    return float(app.exec("get %s PointLight.intensity" % path))


def state_of(app, path):
    """The playback state of an emitter, ParticlePlaying, ParticlePaused or ParticleStopped."""
    return app.exec("get %s ParticlePlayback.state" % path).strip().split(".")[-1]


def check_state(app, path, state, what):
    found = state_of(app, path)
    app.check(found == state, "%s is %s, not %s" % (what, found, state))


def playing(app, name):
    """The playing instances of an effect template: path -> its ParticleEffect."""
    return {path: value for path, value in rows_of(app, "box.particle.ParticleEffect").items()
            if str(value["effect"]).split(".")[-1] == name}


def point(app, path):
    p = app.get(path, "Position")
    return p["x"], p["y"], p["z"]


def close(a, b, tolerance):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def played_at(app, name, at, tolerance):
    """The instances of an effect template that play within a tolerance of a point."""
    return [path for path in playing(app, name) if close(point(app, path), at, tolerance)]


def flash_light(app):
    """The light of the one flash that plays, the EffectFlash of an explosion."""
    found = list(rows_of(app, "box.particle.EffectFlash"))
    app.check(len(found) == 1, "the flashes are %s, not one" % found)
    return found[0]


def check_boom(app, boom, scale):
    """An explosion plays at scale."""
    effect = playing(app, "BoomBurst")[boom]
    app.check(near(effect["scale"], scale), "the explosion plays at %g, not %g" % (
        effect["scale"], scale))


def test_laser_fx(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    powered(app)
    laser = building(app, "LaserTurret", *TURRET)
    tx, tz = turret_at()
    glow, sparks = laser + ".head.fx", laser + ".hit_fx"
    set_state(app, "Paused")
    skitter = still(app, "Skitter", tx + 8, tz - 3, SPONGE)

    # Before it fires its glow and sparks are stopped and dark
    check_state(app, glow, "ParticleStopped", "the glow before the laser fires")
    check_state(app, sparks, "ParticleStopped", "the sparks before the laser fires")
    app.check(light(app, glow) == 0 and light(app, sparks) == 0,
              "the lights of the laser are lit before it fires")

    # While it fires both play and light their lights at their peaks
    set_state(app, "Playing")
    wait_until(app, lambda: health(app, skitter) < SPONGE, FPS, "the laser burning")
    app.wait_for("get %s PointLight.intensity" % glow, lambda out: near(float(out), GLOW_PEAK),
                 frames=FPS, what="the glow's light at %g" % GLOW_PEAK)
    app.wait_for("get %s PointLight.intensity" % sparks,
                 lambda out: near(float(out), SPARKS_PEAK), frames=FPS,
                 what="the sparks' light at %g" % SPARKS_PEAK)
    app.check(int(app.exec(PARTICLES)) > 0, "the firing laser spawned no particles")

    # Paused they pause and hold their light, the beam hides
    set_state(app, "Paused")
    app.wait_frames(2)
    check_state(app, glow, "ParticlePaused", "the glow while paused")
    check_state(app, sparks, "ParticlePaused", "the sparks while paused")
    app.check(not instances(app, "LaserBeam"), "the beam shows while paused")

    # A frame of play later the beam runs from the lens, the head's position plus the lens turned
    # by the aim, to the skitter, and the sparks sit back from its end by the skitter's radius
    # times the inset
    set_state(app, "Playing")
    wait_until(app, lambda: instances(app, "LaserBeam"), FPS, "the beam back on")
    set_state(app, "Paused")
    beams = list(instances(app, "LaserBeam"))
    app.check(len(beams) == 1, "while the laser fires the beams are %s" % beams)
    check_state(app, glow, "ParticlePaused", "the glow while paused")
    sx, sy, sz = point(app, skitter)
    aim = math.atan2(sx - tx, sz - tz)
    head = app.get(laser + ".head", "Rotation")
    app.check(near(head["y"], aim, 1e-4), "the head aims at %g, not %g" % (head["y"], aim))
    s, c = math.sin(aim), math.cos(aim)
    lens = (tx + LASER_HEAD[0] + LASER_LENS[0] * c + LASER_LENS[2] * s,
            LASER_HEAD[1] + LASER_LENS[1],
            tz + LASER_HEAD[2] - LASER_LENS[0] * s + LASER_LENS[2] * c)
    hit = (sx, sy + SKITTER_RADIUS * AIM_PER_RADIUS, sz)
    mid = point(app, beams[0])
    want = tuple((a + b) / 2 - o for a, b, o in zip(lens, hit, (tx, 0, tz)))
    app.check(close(mid, want, 1e-3), "the beam's middle is at %s, not %s" % (mid, want))
    length = math.dist(lens, hit)
    back = min(SKITTER_RADIUS * HIT_INSET / length, 0.5)
    want = tuple(a + (b - a) * (1 - back) - o for a, b, o in zip(lens, hit, (tx, 0, tz)))
    at = point(app, sparks)
    app.check(close(at, want, 1e-3), "the sparks are at %s, not %s" % (at, want))

    # Once the skitter is out of range the laser stops firing: the glow and the sparks stop,
    # their lights go out at once and the beam is gone
    app.exec("set %s Position {%g, 0, %g}" % (skitter, tx + 30, tz))
    set_state(app, "Playing")
    app.wait_for("get %s ParticlePlayback.state" % glow,
                 lambda out: out.endswith("ParticleStopped"), frames=FPS,
                 what="the glow stopped")
    app.wait_frames(2)
    set_state(app, "Paused")
    check_state(app, sparks, "ParticleStopped", "the sparks once the laser stopped")
    app.check(light(app, glow) == 0 and light(app, sparks) == 0,
              "the lights of the laser are lit once it stopped: %g, %g" % (
                  light(app, glow), light(app, sparks)))
    app.check(not instances(app, "LaserBeam"), "the beam shows once the laser stopped")


def test_gun_fx(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    gun = building(app, "GunTurret", *TURRET)
    tx, tz = turret_at()
    flash = gun + ".head.fx"
    peak, fade = GUN_FLASH
    set_state(app, "Paused")
    skitter = still(app, "Skitter", tx + 6, tz, SPONGE)
    once(app, gun)
    app.check(light(app, flash) == 0, "the muzzle flash is lit before the shot")

    # The shot flashes the muzzle light at most at its peak and throws sparks at the muzzle
    set_state(app, "Playing")
    app.wait_for("get %s PointLight.intensity" % flash, lambda out: float(out) > 0, frames=FPS,
                 what="the muzzle flash lit")
    first = light(app, flash)
    app.check(first <= peak + TOLERANCE, "the muzzle flash is at %g, above %g" % (first, peak))
    muzzle = (tx + GUN_MUZZLE[1], GUN_MUZZLE[0], tz)
    app.check(played_at(app, "SparkBurst", muzzle, 1e-3),
              "no sparks at the muzzle %s: %s" % (muzzle, {
                  p: point(app, p) for p in playing(app, "SparkBurst")}))

    # It fades to 0 within peak / fade seconds and stays dark without another shot
    app.wait_for("get %s PointLight.intensity" % flash, "0", frames=frames(fade),
                 what="the muzzle flash faded to 0 after %g s" % fade)
    app.wait_frames(5)
    app.expect("get %s PointLight.intensity" % flash, "0")

    # The tracer's hit on the skitter bursts into sparks and a puff where it hits
    wait_until(app, lambda: health(app, skitter) < SPONGE, FPS, "the gun turret's hit")
    set_state(app, "Paused")
    puffs = playing(app, "HitPuff")
    app.check(len(puffs) == 1, "the hit puffs are %s, not one" % list(puffs))
    at = point(app, list(puffs)[0])
    sx, _, sz = point(app, skitter)
    app.check(math.hypot(at[0] - sx, at[2] - sz) <= 0.55 + 0.6,
              "the hit puff at %s is not at the skitter at %s" % (at, (sx, sz)))
    app.check(played_at(app, "SparkBurst", at, 1e-3), "no sparks at the hit %s" % (at,))
    app.check(not playing(app, "ImpactDust"), "the hit on the skitter threw up dust")


def test_missile_fx(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    powered(app)
    turret = building(app, "MissileTurret", *TURRET)
    tx, tz = turret_at()
    flash = turret + ".head.fx"
    peak, fade = MISSILE_FLASH
    set_state(app, "Paused")
    brute = still(app, "Brute", tx + 10, tz, SPONGE)
    once(app, turret)

    # The launch flashes the missile turret's muzzle light, throws sparks and blows the launch
    # back from the pods, turned to the aim of the head
    set_state(app, "Playing")
    app.wait_for("get %s PointLight.intensity" % flash, lambda out: float(out) > 0, frames=FPS,
                 what="the missile turret's flash lit")
    first = light(app, flash)
    app.check(first <= peak + TOLERANCE, "the flash is at %g, above %g" % (first, peak))
    wait_until(app, lambda: instances(app, "Missile"), FPS, "the missile")
    set_state(app, "Paused")
    app.wait_frames(2)
    aim = app.get(turret + ".head", "Rotation")["y"]
    app.check(near(aim, math.pi / 2, 1e-4), "the head aims at %g" % aim)
    launches = playing(app, "MissileLaunch")
    app.check(len(launches) == 1, "the launches are %s, not one" % list(launches))
    turned = app.get(list(launches)[0], "Rotation")
    app.check(near(turned["y"], aim, 1e-4) and turned["x"] == 0 and turned["z"] == 0,
              "the launch is turned by %s, not %g about y" % (turned, aim))
    app.check(playing(app, "SparkBurst"), "the launch threw no sparks")
    check_state(app, flash, "ParticlePaused", "the flash while paused")

    # The missile points along its velocity, in box's Euler order (Ry * Rx * Rz turns +Z to
    # (sin y cos x, -sin x, cos y cos x)); it climbs or dives toward its aim, so both angles count
    missile = list(instances(app, "Missile"))[0]
    r = app.get(missile, "Rotation")
    v = app.get(missile, "OreProjectile")
    speed = math.sqrt(v["vx"] ** 2 + v["vy"] ** 2 + v["vz"] ** 2)
    forward = (math.sin(r["y"]) * math.cos(r["x"]), -math.sin(r["x"]),
               math.cos(r["y"]) * math.cos(r["x"]))
    dot = (forward[0] * v["vx"] + forward[1] * v["vy"] + forward[2] * v["vz"]) / speed
    app.check(dot > 0.9999, "the missile turned by %s points %s, its velocity is %s" % (
        r, forward, v))
    app.check(abs(r["x"]) > 1e-3, "the missile flies level: %s" % r)

    # Its trail lingers after it, pauses with the game and plays again with it
    trail = missile + ".trail"
    app.check(app.get(trail, "ParticleEmitter")["linger"] is True, "the trail does not linger")
    check_state(app, trail, "ParticlePaused", "the trail while paused")
    set_state(app, "Playing")
    app.wait_for("get %s ParticlePlayback.state" % trail,
                 lambda out: out.endswith("ParticlePlaying"), frames=FPS,
                 what="the trail playing again")

    # The flash fades to 0
    app.wait_for("get %s PointLight.intensity" % flash, "0", frames=frames(fade),
                 what="the flash faded to 0 after %g s" % fade)

    # It hits the brute and explodes at scale 1 (splash 5), with its flash light
    wait_until(app, lambda: not instances(app, "Missile"), 2 * FPS, "the missile's hit")
    set_state(app, "Paused")
    booms = playing(app, "BoomBurst")
    app.check(len(booms) == 1, "the explosions are %s, not one" % list(booms))
    check_boom(app, list(booms)[0], 1)
    lit = flash_light(app)
    app.check(near(float(app.exec("get %s EffectFlash.intensity" % lit)), BOOM_PEAK),
              "the explosion's flash does not peak at %g" % BOOM_PEAK)
    app.check(near(float(app.exec("get %s PointLight.range" % lit)), BOOM_RANGE),
              "the explosion's flash does not reach %g" % BOOM_RANGE)
    app.check(int(app.exec(PARTICLES)) > 0, "the explosion spawned no particles")
    app.check(health(app, brute) == SPONGE - 150, "the brute has %g HP" % health(app, brute))


def test_ground_hits(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    tx, tz = turret_at()
    set_state(app, "Paused")

    # A tracer and two missiles diving into the ground, 0.3 up at 30 a second: in the first frame
    # of play they are 0.2 below it. The missiles' splashes, 2 and 30, scale their explosions to
    # the bounds 0.65 and 1.25
    shots = [("Tracer", tx, 0), ("Missile", tx + 8, 2), ("Missile", tx + 16, 30)]
    for prefab, x, splash in shots:
        code = ("using box.transform\nusing oreelse\n@NAME : %s {\n"
                "  Position: {%g, 0.3, %g}\n  Rotation: {1.5708, 0, 0}\n"
                "  OreProjectile: {vx: 0, vy: -30, vz: 0, life: 5, radius: 0.5}\n"
                "  OreDamage: {value: 1}\n" % (prefab, x, tz))
        if splash:
            code += "  OreSplash: {radius: %g}\n" % splash
        run_script(app, code + "}")
    set_state(app, "Playing")
    wait_until(app, lambda: not instances(app, "Tracer") and not instances(app, "Missile"), FPS,
               "the shots ending on the ground")
    set_state(app, "Paused")

    # The tracer throws up dust and sparks on the ground below where it ended
    dust = playing(app, "ImpactDust")
    app.check(len(dust) == 1, "the dust is %s, not one" % list(dust))
    at = point(app, list(dust)[0])
    app.check(close(at, (tx, IMPACT_Y, tz), 1e-3), "the dust is at %s" % (at,))
    app.check(played_at(app, "SparkBurst", at, 1e-3), "no sparks with the dust at %s" % (at,))
    app.check(not playing(app, "HitPuff"), "the tracer on the ground puffed")

    # The missiles explode 0.2 above where they ended, at the bounds of the scale
    end_y = 0.3 - 30 / FPS
    scales = []
    for boom, effect in playing(app, "BoomBurst").items():
        at = point(app, boom)
        app.check(abs(at[1] - (end_y + BLAST_LIFT)) < 1e-3, "an explosion is at %s" % (at,))
        scales.append(round(effect["scale"], 3))
    app.check(sorted(scales) == [0.65, 1.25], "the explosions play at %s, not 0.65 and 1.25" %
              scales)


def test_wall_explodes(app):
    start(app)
    hold_waves(app)
    place_player(app, *FAR)
    wall = building(app, "Wall", *TURRET)
    tx, tz = turret_at()
    set_state(app, "Paused")
    app.check(not playing(app, "BoomBurst"), "an explosion plays before the wall is destroyed")

    # At 0 HP it is destroyed: it is gone and explodes 0.55 above its tile at 0.8
    app.exec("set %s OreHealth {value: 0, max: 300}" % wall)
    set_state(app, "Playing")
    wait_until(app, lambda: wall not in buildings(app), FPS, "the wall destroyed")
    set_state(app, "Paused")
    booms = playing(app, "BoomBurst")
    app.check(len(booms) == 1, "the explosions are %s, not one" % list(booms))
    boom = list(booms)[0]
    check_boom(app, boom, WRECK_SCALE)
    at = point(app, boom)
    app.check(close(at, (tx, WRECK_Y, tz), 1e-3), "the wall exploded at %s, not %s" % (
        at, (tx, WRECK_Y, tz)))
    lit = flash_light(app)
    app.check(near(float(app.exec("get %s EffectFlash.intensity" % lit)),
                   BOOM_PEAK * WRECK_SCALE),
              "the flash of the wall's explosion does not peak at %g" % (BOOM_PEAK * WRECK_SCALE))
    app.check(near(float(app.exec("get %s PointLight.range" % lit)), BOOM_RANGE * WRECK_SCALE),
              "the flash of the wall's explosion does not reach %g" % (BOOM_RANGE * WRECK_SCALE))
