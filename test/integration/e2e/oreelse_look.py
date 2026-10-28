"""Ore Else's render quality tiers driven through its command port: cycling the tier changes the
resolution scale of the window and the shadows and effects of the world view as cfg.quality
gives them, and the tier survives a restart.

Every case runs in a fresh apps/oreelse process with its default scene
(apps/oreelse/etc/oreelse.flecs) and a fixed session seed, and gets the App of
tools/box_client.py; the runner checks after every case that the app logged no error and quits
cleanly. The cases shrink the window and drop the shadows of the world view as oreelse_flow does
(the tier sets the resolution and distance of the shadows, not whether they render), and cycle
the tier with the Quality action, which the pause screen's button raises. The game state is a
singleton of the oreelse module, written through the REST endpoints of the port (put of
oreelse_flow).
"""
from box_client import parse_value
from oreelse_flow import ARGS, STATE, key, set_state, setup, start  # noqa: F401 (ARGS)

APP = "apps/oreelse"

TIER = "get oreelse.OreQuality OreQuality.tier"
SCALE = "get window Surface.resolution_scale"
SHADOW_RESOLUTION = "get main_view View.shadows.resolution"
SHADOW_DISTANCE = "get main_view View.shadows.max_distance"
EFFECTS = "get main_view View.effects"

# The settings of every tier (cfg.quality): resolution scale, shadow resolution, shadow
# distance, Gtao, Bloom
TIERS = {
    "Low": (2, 1024, 91, False, False),
    "Medium": (1, 2048, 140, False, True),
    "High": (1, 4096, 140, True, True),
}

# The pause screen's button cycles the tier down from High, the tier of every start
CYCLE = ["High", "Medium", "Low", "High"]


def tier_name(out):
    """The name of a tier as the command port prints it."""
    return out.strip().strip('"').split(".")[-1]


def effects(app):
    """Whether each effect of the world view runs, by the name of its entity."""
    result = {}
    for e in parse_value(app.exec(EFFECTS)):
        result[str(e["effect"]).split(".")[-1]] = e["enabled"]
    return result


def matches(app, name):
    """Whether the window and the world view have the settings of a tier."""
    scale, resolution, distance, gtao, bloom = TIERS[name]
    found = effects(app)
    return (float(app.exec(SCALE)) == scale
            and int(app.exec(SHADOW_RESOLUTION)) == resolution
            and float(app.exec(SHADOW_DISTANCE)) == distance
            and found.get("gtao") == gtao and found.get("bloom") == bloom
            and found.get("tonemap") is True)


def check_tier(app, name):
    """Wait for the tier and its settings, which the look applies before the next frame."""
    app.wait_for(TIER, lambda out: tier_name(out) == name, what="the tier %s" % name)
    app.wait_for(SCALE, lambda out: matches(app, name),
                 what="the settings of the tier %s" % name)
    scale, resolution, distance, gtao, bloom = TIERS[name]
    app.expect(SHADOW_RESOLUTION, str(resolution))
    app.check(float(app.exec(SHADOW_DISTANCE)) == distance,
              "the shadow distance is not %g at %s" % (distance, name))
    app.check(effects(app) == {"gtao": gtao, "bloom": bloom, "tonemap": True},
              "the effects of the world view are %s at %s" % (effects(app), name))
    # The tier does not turn on the shadows that setup turned off
    app.expect("get main_view View.shadows.enabled", "false")


def cycle(app):
    app.exec("action.press actions.Quality")
    app.wait_input()


def test_cycle(app):
    setup(app)
    check_tier(app, "High")

    # The button lives on the pause screen
    start(app)
    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")
    for name in CYCLE[1:]:
        cycle(app)
        check_tier(app, name)

    # The tier also cycles while playing
    key(app, "Escape")
    app.wait_for(STATE % "Playing", "true")
    cycle(app)
    check_tier(app, "Medium")


def test_restart_keeps_tier(app):
    setup(app)
    start(app)
    cycle(app)
    cycle(app)
    check_tier(app, "Low")

    set_state(app, "Lost")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    app.wait_frames(5)
    check_tier(app, "Low")
    cycle(app)
    check_tier(app, "High")
