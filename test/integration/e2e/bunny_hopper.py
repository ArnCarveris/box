"""The bunny hopper driven through its command port: SPACE hops and double jumps, carrots that
pass the bunny count in BunnyGame and the HUD, a run without jumps ends in the game over panel,
R starts a new run, and landings play particles that fade.

Every case runs in a fresh apps/bunnyhopper process with its default scene and gets the App of
tools/box_client.py; the runner checks after every case that the app logged no error and quits
cleanly. Headless, the app steps a fixed 1 / --fps seconds per frame, and the software renderer
of the test machines draws the default 1280x1024 window with its effects at about 1 frame per
second. So every case starts with setup: the window shrinks to 640x480, still tall enough for
the game over panel, and the world view drops its effects and shadows, which takes it to about
10 frames per second, and --fps 20 makes each frame 50 ms of game time. Waits count frames, so
they hold under load: a frame budget of 20 is one second of the game.

Without jumps the first carrot reaches the bunny about 3 s into a run, the next ones every 1
to 2 s, and the third hit ends it.
"""
from box_client import mean_luma

APP = "apps/bunnyhopper"
ARGS = ["--fps", "20"]

FPS = 20
GAME = "get bunnyhopper.BunnyGame BunnyGame."
STATE = "has bunnyhopper.BunnyGameState (bunnyhopper.BunnyGameState, bunnyhopper.BunnyGameState.%s)"
PARTICLES = "get box.particle.ParticleStats ParticleStats.particles"
SCREEN = "hud.ui.screen"
SCORE = "get %s.score Text.value" % SCREEN
TITLE = "get %s.game_over.title Text.value" % SCREEN
HUD_CAMERA = "hud.ui.camera"

# A jump starts at 13 units per second against a gravity of 32: it peaks at 169 / 64 = 2.64
# above the ground and lands after 0.81 s. Only a double jump gets above JUMP_PEAK
JUMP_PEAK = 2.64
DOUBLE_PEAK = JUMP_PEAK + 0.4
JUMP_FRAMES = 2 * FPS

# Three hits end a run within about 8 s of the game
GAME_OVER_FRAMES = 20 * FPS

# A carrot passes the bunny about 3.2 s into a run
CARROT_FRAMES = 8 * FPS

# The dust of a landing lives up to 0.75 s
DUST_FRAMES = 2 * FPS

# The game over panel of a 640x480 window: 560x170, centered, 256 below the top. Its background
# blends a dark green over the sky blue clear of the capture, so its mean luminance falls from
# about 188 of 255 to about 125 with the text; a fall of at least PANEL_DARKER counts
PANEL_REGION = (40, 256, 600, 426)
PANEL_DARKER = 40


def number(out):
    return float(out)


def setup(app):
    """Shrink the window and drop the effects and shadows of the world view, so the software
    renderer keeps up."""
    app.exec("set window Surface {width: 640, height: 480}")
    app.exec("set main_view View.effects []")
    app.exec("set main_view View.shadows.enabled false")
    app.wait_for("get window SurfaceStatus.height", "480")


def key(app, name):
    app.exec("key.press %s" % name)
    app.wait_input()


def test_hop(app):
    setup(app)
    app.expect("get player Position.y", "0")
    key(app, "Space")
    app.wait_for("get player Position.y", lambda out: number(out) > 1, frames=JUMP_FRAMES,
                 what="a height above 1 after SPACE")
    app.expect("get player BunnyJump.jumps", "1")
    app.wait_for("get player Position.y", lambda out: number(out) == 0, frames=JUMP_FRAMES,
                 what="the bunny back on the ground")
    app.expect("get player BunnyJump.jumps", "0")

    key(app, "Space")
    app.wait_for("get player Position.y", lambda out: number(out) > 1, frames=JUMP_FRAMES,
                 what="a height above 1 after SPACE")
    key(app, "Space")
    app.wait_for("get player BunnyJump.jumps", "2", frames=JUMP_FRAMES)
    app.wait_for("get player Position.y", lambda out: number(out) > DOUBLE_PEAK,
                 frames=JUMP_FRAMES, what="a double jump above %g" % DOUBLE_PEAK)
    app.wait_for("get player Position.y", lambda out: number(out) == 0, frames=JUMP_FRAMES,
                 what="the bunny back on the ground")
    app.expect("get player BunnyLives.lives_left", "3")


def test_carrots_count(app):
    setup(app)
    app.expect(GAME + "carrots", "0")
    app.expect(SCORE, '"Carrots: 0"')
    app.wait_for(GAME + "carrots", lambda out: int(out) >= 1, frames=CARROT_FRAMES,
                 what="a carrot that passed the bunny")

    # The run goes on, so the text has to match the count of the same poll
    def follows(out):
        return out == '"Carrots: %d"' % app.get("bunnyhopper.BunnyGame", "BunnyGame.carrots")
    out = app.wait_for(SCORE, follows, what="the score text of BunnyGame.carrots")
    app.check(out != '"Carrots: 0"', "the score text is still %s" % out)


def test_game_over_and_restart(app):
    setup(app)
    playing = mean_luma(app.capture("playing", HUD_CAMERA), PANEL_REGION)
    app.wait_for(STATE % "Over", "true", frames=GAME_OVER_FRAMES, what="the game over state")
    app.expect("get player BunnyLives.lives_left", "0")
    app.wait_for(TITLE, '"GAME OVER"')
    over = mean_luma(app.capture("game_over", HUD_CAMERA), PANEL_REGION)
    app.check(playing - over >= PANEL_DARKER,
              "the panel region %s is %.1f bright after the game ended, %.1f while playing: "
              "less than %d darker" % (PANEL_REGION, over, playing, PANEL_DARKER))
    carrots = app.get("bunnyhopper.BunnyGame", "BunnyGame.carrots")
    app.wait_frames(FPS)
    app.expect(GAME + "carrots", str(carrots))

    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    app.expect("get player BunnyLives.lives_left", "3")
    app.expect(GAME + "carrots", "0")
    app.expect(STATE % "Over", "false")
    app.wait_for(SCORE, '"Carrots: 0"')
    app.wait_for("ls %s" % SCREEN, lambda out: "game_over" not in out.split(),
                 what="the game over panel gone")


def test_particles(app):
    setup(app)
    app.expect(PARTICLES, "0")
    key(app, "Space")
    app.wait_for("get player BunnyJump.jumps", "1", frames=JUMP_FRAMES)
    app.wait_for(PARTICLES, lambda out: int(out) > 0, frames=JUMP_FRAMES,
                 what="the particles of the landing")
    app.expect("get player BunnyJump.jumps", "0")
    app.wait_for(PARTICLES, "0", frames=DUST_FRAMES)
    app.expect("get player BunnyLives.lives_left", "3")


def test_quit(app):
    setup(app)
    app.expect("logs error", "")
    app.quit()
