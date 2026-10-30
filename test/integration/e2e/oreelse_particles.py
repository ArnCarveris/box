"""Ore Else's particle effects (apps/oreelse/etc/particles) played through the command port: every
one shot effect spawns its particles and they are gone after its longest lifetime, the emitter
templates spawn while they play and stop when they are stopped, their lights follow, and the
flash lights of the explosion, the combust and the muzzle flash fade to 0.

Every case runs in a fresh apps/oreelse process with its default scene
(apps/oreelse/etc/oreelse.flecs) and a fixed session seed, and gets the App of
tools/box_client.py; the runner checks after every case that the app logged no error and quits
cleanly. The cases shrink the window and drop the shadows as oreelse_flow does and stay on the
title screen, where no effect plays. They play the effects with scripts sent to the REST
endpoint of the port (PUT /script/<name>), which call box.particle.burst_at,
oreelse.explosion() and oreelse.combust() or make instances of the templates, and count the
particles in box.particle.ParticleStats. The frames run at a fixed 1/60 s, so FPS frames are one
second of the effects.
"""
import math
import urllib.error
import urllib.parse
import urllib.request

from box_client import parse_value, srgb
from oreelse_flow import ARGS, FPS, setup  # noqa: F401 (ARGS)

APP = "apps/oreelse"

PARTICLES = "get box.particle.ParticleStats box.particle.ParticleStats.particles"
EFFECTS = "get box.particle.ParticleStats box.particle.ParticleStats.effects"

# The one shot effects: the particles their layers burst, halo layers included, and the longest
# lifetime of their layers in seconds (lifetime.max), when the instance retires
ONE_SHOTS = {
    "BoomBurst": (196, 2.5),
    "CombustBurst": (101, 2.0),
    "MissileLaunch": (27, 1.25),
    "FlameBurst": (16, 0.46),
    "SmokeBurst": (20, 2.2),
    "FootDust": (8, 0.68),
    "HitPuff": (7, 0.48),
    "ImpactDust": (12, 0.86),
    "MineBurst": (14, 0.6),
    "PoofBurst": (18, 0.77),
    "SmokePuff": (5, 1.2),
    "SparkBurst": (10, 0.42),
    "SplatBurst": (22, 0.82),
    "PressPuff": (9, 0.46),
    "PressSparks": (12, 0.36),
}

# The emitter templates: their layers (paths under the instance), the longest lifetime of their
# particles in seconds and the peak of their ParticleLight (0 for none)
EMITTERS = {
    "BeamGlow": ([""], 0.3, 4.5),
    "BeamSparks": ([""], 0.44, 5),
    "WeldSparks": ([""], 0.54, 3.4),
    "DrillDust": ([""], 0.9, 0),
    "VentSteam": ([""], 1.55, 0),
    "MissileTrail": ([""], 1.5, 0),
    "JetPlume": ([""], 0.4, 0),
    "DepositSparkle": ([".sparkle", ".sparkle_halo"], 1.8, 0),
    "CorpseEmbers": ([".embers", ".embers_halo"], 1.7, 0),
}

# Frames a burst or a stop is allowed beyond the longest lifetime
MARGIN = FPS // 2

# The flash of an explosion at scale 1 (cfg.explosionPeak, cfg.explosionRange), fading to 0
# over 1 / 4.5 s
BOOM_PEAK = 9
BOOM_RANGE = 13
BOOM_FADE = 1 / 4.5

# The flash of a combust of radius r: the peak 4.5 * sqrt(r), the range 3 + 4 * r, fading to 0
# over 1 / 7 s
COMBUST_PEAK = 4.5
COMBUST_RANGE = (3, 4)
COMBUST_FADE = 1 / 7

# The muzzle flash: peak 5, fading at 45 a second
MUZZLE_PEAK = 5
MUZZLE_FADE = 5 / 45

TOLERANCE = 0.01


def frames(seconds):
    return int(math.ceil(seconds * FPS)) + MARGIN


def script(app, name, code):
    """Run a script in the app through the REST endpoint of the port: it makes the script entity
    name, or replaces its code."""
    url = app.url("script/" + name, {"code": code})
    request = urllib.request.Request(url, method="PUT")
    try:
        with urllib.request.urlopen(request, timeout=10) as r:
            status, body = r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read().decode(errors="replace")
    app.check(status == 200 and '"error"' not in body,
              "the script %s failed (%d): %s" % (name, status, body))


def query(app, component):
    """The entities with a component and its values, by path."""
    out = app.exec("query %s" % component)
    return {row["entity"]: next(iter(row["fields"][0]["value"].values()))
            for row in parse_value(out)}


def playing(app):
    """The playing effect instances by the name of their template, with their ParticleEffect."""
    result = {}
    for value in query(app, "box.particle.ParticleEffect").values():
        result[str(value["effect"]).split(".")[-1]] = value
    return result


def near(a, b):
    return abs(a - b) <= TOLERANCE * max(1, abs(b))


def test_one_shots(app):
    setup(app)
    app.expect(PARTICLES, "0")
    app.expect(EFFECTS, "0")

    # Every one shot effect at once, in a row in front of the astronaut; the explosion and the
    # combust through their helpers at scale and radius 1
    lines = ["using oreelse"]
    for i, name in enumerate(ONE_SHOTS):
        at = "{%d, 0.5, 6}" % (2 * i - len(ONE_SHOTS))
        if name == "BoomBurst":
            call = "oreelse.explosion(BoomBurst, %s, 1)" % at
        elif name == "CombustBurst":
            call = "oreelse.combust(CombustBurst, %s, 1)" % at
        else:
            call = "box.particle.burst_at(%s, %s, 1)" % (name, at)
        lines.append("const fx_%d: entity = %s" % (i, call))
    script(app, "fx_one_shots", "\n".join(lines))
    start = app.frame()

    # They spawn in the frame of the bursts, and none dies within a few frames
    total = sum(count for count, _ in ONE_SHOTS.values())
    out = app.wait_for(PARTICLES, lambda out: int(out) > 0, what="the particles of the bursts")
    app.check(int(out) == total, "the bursts spawned %s particles, not %d" % (out, total))

    # Every effect plays one instance, which retires after its longest lifetime
    found = playing(app)
    app.check(sorted(found) == sorted(ONE_SHOTS),
              "the playing effects are %s, not %s" % (sorted(found), sorted(ONE_SHOTS)))
    for name, (_, longest) in ONE_SHOTS.items():
        app.check(near(found[name]["retire"], longest),
                  "%s retires after %s s, not after its longest lifetime of %g s" %
                  (name, found[name]["retire"], longest))
        app.check(found[name]["emit"] == 0, "%s emits until %s s" % (name, found[name]["emit"]))

    # Each instance is gone, its particles dead, once its longest lifetime passed
    left = set(ONE_SHOTS)
    budget = frames(max(longest for _, longest in ONE_SHOTS.values()))
    while left:
        now = app.frame() - start
        gone = left - set(playing(app))
        for name in gone:
            app.check(now <= frames(ONE_SHOTS[name][1]),
                      "%s played for %d frames, beyond its longest lifetime of %g s" %
                      (name, now, ONE_SHOTS[name][1]))
        left -= gone
        app.check(now <= budget, "%s still play after %d frames" % (sorted(left), now))
    app.wait_for(PARTICLES, "0", frames=MARGIN)
    app.expect(EFFECTS, "0")


def check_emitter(app, name):
    """Make an instance of an emitter template, play it until it spawns, stop it and wait for its
    particles to die."""
    layers, longest, peak = EMITTERS[name]
    entity = "fx_%s" % name
    code = "%s { Position: {0, 1, 6}\n  %s: {} }" % (entity, name)
    script(app, entity, "using box.transform\n" + code)
    app.wait_for("get %s%s box.particle.ParticleEmitter.rate" % (entity, layers[0]),
                 lambda out: float(out) > 0, what="the emitter of %s" % name)

    for layer in layers:
        app.exec("set %s%s box.particle.ParticlePlayback {state: ParticlePlaying}" %
                 (entity, layer))
    app.wait_for(PARTICLES, lambda out: int(out) > 0, frames=FPS,
                 what="the particles of %s while it plays" % name)
    if peak:
        app.wait_for("get %s box.light.PointLight.intensity" % entity,
                     lambda out: near(float(out), peak), what="the light of %s at %g" % (
                         name, peak))

    for layer in layers:
        app.exec("set %s%s box.particle.ParticlePlayback {state: ParticleStopped}" %
                 (entity, layer))
    if peak:
        app.wait_for("get %s box.light.PointLight.intensity" % entity, "0",
                     what="the light of %s off once stopped" % name)
    app.wait_for(PARTICLES, "0", frames=frames(longest),
                 what="the particles of %s gone after %g s" % (name, longest))
    app.wait_frames(5)
    app.expect(PARTICLES, "0")
    for layer in layers:
        state = parse_value(app.exec("get %s%s box.particle.ParticlePlayback.state" %
                                     (entity, layer)))
        app.check(state.endswith("ParticleStopped"), "%s%s is %s, not stopped" % (
            entity, layer, state))


def test_beams_and_welding(app):
    setup(app)
    app.expect(PARTICLES, "0")
    for name in ("BeamGlow", "BeamSparks", "WeldSparks"):
        check_emitter(app, name)


def test_dust_steam_and_trails(app):
    setup(app)
    app.expect(PARTICLES, "0")
    for name in ("DrillDust", "VentSteam", "MissileTrail"):
        check_emitter(app, name)


def test_plume_sparkle_and_embers(app):
    setup(app)
    app.expect(PARTICLES, "0")
    for name in ("JetPlume", "DepositSparkle", "CorpseEmbers"):
        check_emitter(app, name)


def flash_light(app, name):
    """The path of the light of the one playing instance of an effect with an EffectFlash."""
    found = {}
    app.wait_for(EFFECTS, lambda out: int(out) > 0, what="the play of %s" % name)
    for path in query(app, "box.particle.EffectFlash"):
        found[path] = True
    app.check(len(found) == 1, "%d lights flash, not the one of %s" % (len(found), name))
    return next(iter(found))


def check_flash(app, light, peak, range_, fade):
    """The light of a flash has the range of its play from its first frame and fades from at
    most its peak to 0."""
    first = float(app.exec("get %s box.light.PointLight.intensity" % light))
    app.check(0 <= first <= peak + TOLERANCE, "the flash is at %g, above its peak %g" % (
        first, peak))
    app.check(near(float(app.exec("get %s box.light.PointLight.range" % light)), range_),
              "the flash does not reach %g" % range_)
    app.check(near(float(app.exec("get %s box.particle.EffectFlash.intensity" % light)), peak),
              "the flash does not peak at %g" % peak)
    app.wait_for("get %s box.light.PointLight.intensity" % light, "0", frames=frames(fade),
                 what="the flash faded to 0 after %g s" % fade)
    app.wait_frames(5)
    app.expect("get %s box.light.PointLight.intensity" % light, "0")


def test_explosion_flash(app):
    setup(app)
    scale = 0.8
    script(app, "fx_boom", "using oreelse\nconst boom: entity = "
           "oreelse.explosion(BoomBurst, {0, 0.55, 6}, %g)" % scale)
    light = flash_light(app, "BoomBurst")
    app.check(app.get(light, "box.material.Rgb") == dict(zip("rgb", srgb((255, 190, 118)))),
              "the flash of the explosion is %s, not warm white" %
              app.get(light, "box.material.Rgb"))
    check_flash(app, light, BOOM_PEAK * scale, BOOM_RANGE * scale, BOOM_FADE)
    app.wait_for(EFFECTS, "0", frames=frames(ONE_SHOTS["BoomBurst"][1]))
    app.expect(PARTICLES, "0")


def test_combust_flash(app):
    setup(app)
    radius = 2
    script(app, "fx_combust", "using oreelse\nconst combust: entity = "
           "oreelse.combust(CombustBurst, {0, 0, 6}, %g)" % radius)
    light = flash_light(app, "CombustBurst")
    check_flash(app, light, COMBUST_PEAK * math.sqrt(radius),
                COMBUST_RANGE[0] + COMBUST_RANGE[1] * radius, COMBUST_FADE)
    app.wait_for(EFFECTS, "0", frames=frames(ONE_SHOTS["CombustBurst"][1]))
    app.expect(PARTICLES, "0")


def test_muzzle_flash(app):
    setup(app)
    script(app, "fx_muzzle", "using box.transform\nfx_muzzle { Position: {0, 1, 6}\n"
           "  MuzzleFlash: {} }")
    app.wait_for("get fx_muzzle box.light.PointLight.range", "4.5", what="the muzzle flash")
    app.wait_frames(3)
    app.expect("get fx_muzzle box.light.PointLight.intensity", "0")

    # Every shot plays it from the start: it lights at its peak and fades to 0, spawning nothing
    for _ in range(2):
        app.exec("set fx_muzzle box.particle.ParticlePlayback "
                 "{state: ParticlePlaying, reset: true}")
        app.wait_for("get fx_muzzle box.light.PointLight.intensity", lambda out: float(out) > 0,
                     what="the muzzle flash lit")
        first = float(app.exec("get fx_muzzle box.light.PointLight.intensity"))
        app.check(first <= MUZZLE_PEAK + TOLERANCE, "the muzzle flash is at %g, above %g" % (
            first, MUZZLE_PEAK))
        app.wait_for("get fx_muzzle box.light.PointLight.intensity", "0",
                     frames=frames(MUZZLE_FADE),
                     what="the muzzle flash faded to 0 after %g s" % MUZZLE_FADE)
        app.expect(PARTICLES, "0")
