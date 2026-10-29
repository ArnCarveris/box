"""Ore Else driven through its command port: the screens of the game flow. The title, pause and
end screens each show in their state only, the title and pause screens with the same list of the
controls; the quality button of the pause screen lightens under the cursor and its clicks cycle
the render quality High, Medium, Low and back to High, which changes the settings of the view;
the end screen tells won from lost and formats the stats, the clock as m:ss; R on the end screen
lands again.

Every case runs in a fresh apps/oreelse process with its default scene and a fixed session seed,
like oreelse_flow.py, which describes the setup. The screens' texts are read from the entities of
the HUD's instance (hud.ui.screen), as oreelse_hud.py does.
"""
from oreelse_buildings import SETTLE
from oreelse_flow import ARGS, CLOCK, STATE, key, put, set_state, setup, start  # noqa: F401
from oreelse_hud import color, hud_texts, wait_hidden, wait_shown
from oreelse_look import check_tier

APP = "apps/oreelse"

SCREEN = "hud.ui.screen"
TITLE = SCREEN + ".title_screen"
PAUSE = SCREEN + ".pause_screen"
END = SCREEN + ".end_screen"
SCREENS = [TITLE, PAUSE, END]

QUALITY = PAUSE + ".quality"

ROCKET = "oreelse.OreRocketState"
WAVES = "oreelse.OreWaveState"

# The colors of the screens (palette.text, palette.soft, palette.amber, palette.luminite) and
# the face of the quality button, idle and under the cursor
TEXT = (226, 231, 238)
SOFT = (146, 154, 166)
AMBER = (240, 176, 58)
TEAL = (96, 240, 218)
FACE = (21, 23, 27)
FACE_HOVER = (38, 42, 49)

# The list of the controls the title and pause screens share, row by row
CONTROLS = [
    ("walk", "W A S D", "Walk"),
    ("run", "Shift", "Walk faster"),
    ("zoom", "Mouse wheel", "Zoom in/out"),
    ("place", "Left click", "Place selected building"),
    ("demolish", "Right click", "Demolish/mine object under mouse cursor"),
    ("inventory", "E", "Open / close the inventory"),
    ("pick", "Q", "Select the building under the cursor / deselect"),
    ("cancel", "ESC", "Cancel / Pause"),
]

# The middle of the quality button in the 640x360 window: the pause screen's panel (640x388)
# is 40 above the middle of the window, the button (138x26) at (251, 96) of it, so its middle is
# at x 320 and 125 above the middle of the window
ON_QUALITY = (320, 55)
OFF_QUALITY = (60, 300)

# The tiers the clicks of the button cycle through, from High, the tier of every start
CYCLE = [("Medium", "MEDIUM"), ("Low", "LOW"), ("High", "HIGH")]


def screens_shown(app):
    """The screens that show: the ones with a text below them."""
    texts = hud_texts(app)
    return [s for s in SCREENS if any(p.startswith(s + ".") for p in texts)]


def wait_screen(app, expected):
    """Wait until exactly one screen shows (or none, expected None)."""
    start_frame = app.frame()
    want = [expected] if expected else []
    while screens_shown(app) != want:
        app.check(app.frame() - start_frame < 60,
                  "expected the screens %s, the HUD shows %s" % (want, screens_shown(app)))


def check_controls(app, screen):
    texts = hud_texts(app)
    for name, keys, action in CONTROLS:
        row = "%s.controls.%s" % (screen, name)
        app.check(texts.get(row + ".key_label") == keys,
                  "the key of %s is %r" % (row, texts.get(row + ".key_label")))
        app.check(texts.get(row + ".action_label") == action,
                  "the action of %s is %r" % (row, texts.get(row + ".action_label")))
        app.check(color(app, row + ".key_label") == TEXT, "the key of %s is %s" %
                  (row, color(app, row + ".key_label")))
        app.check(color(app, row + ".action_label") == SOFT, "the action of %s is %s" %
                  (row, color(app, row + ".action_label")))


def move(app, cursor):
    app.exec("mouse.move [%g, %g]" % cursor)
    app.wait_input()
    app.wait_frames(SETTLE)


def pause(app):
    start(app)
    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")
    wait_screen(app, PAUSE)


def stats(app, elapsed, kills, wave, shipped):
    """Put the stats of a run over, the clock in seconds."""
    put(app, "oreelse.OreClock", "oreelse.OreClock", {"elapsed": elapsed})
    put(app, WAVES, WAVES, {"kills": kills, "wave": wave})
    put(app, ROCKET, ROCKET, {"shipped": shipped})


def test_screens_in_their_states(app):
    setup(app)

    # The title screen, alone
    app.expect(STATE % "Title", "true")
    wait_screen(app, TITLE)
    wait_shown(app, TITLE + ".start", "PRESS ANY KEY TO START")
    texts = hud_texts(app)
    for sign, label in (("mine", "MINE"), ("defend", "DEFEND"), ("escape", "ESCAPE")):
        path = "%s.signs.%s.label_text" % (TITLE, sign)
        app.check(texts.get(path) == label, "the sign %s reads %r" % (sign, texts.get(path)))
    app.check(texts.get(TITLE + ".goal_b") == "escape before the waves get too strong.",
              "the second line of the title screen is %r" % texts.get(TITLE + ".goal_b"))
    check_controls(app, TITLE)

    # None while playing
    start(app)
    wait_screen(app, None)

    # The pause screen, alone, with the same controls
    key(app, "Escape")
    app.wait_for(STATE % "Paused", "true")
    wait_screen(app, PAUSE)
    wait_shown(app, PAUSE + ".heading", "PAUSED")
    wait_shown(app, PAUSE + ".resume", "Press ESC to resume")
    wait_shown(app, QUALITY + ".value", "HIGH")
    check_controls(app, PAUSE)
    key(app, "Escape")
    app.wait_for(STATE % "Playing", "true")
    wait_screen(app, None)

    # The end screen when won and when lost
    set_state(app, "Won")
    wait_screen(app, END)
    wait_shown(app, END + ".heading", "YOU ESCAPED")
    wait_shown(app, END + ".again", "Press R to land again")
    set_state(app, "Lost")
    wait_shown(app, END + ".heading", "THE SWARM GOT YOU")
    wait_screen(app, END)


def test_quality_button(app):
    setup(app)
    check_tier(app, "High")
    pause(app)
    app.check(color(app, QUALITY + ".value") == AMBER, "the tier reads in %s" %
              (color(app, QUALITY + ".value"),))
    app.check(color(app, QUALITY + ".caption") == SOFT, "the caption is %s" %
              (color(app, QUALITY + ".caption"),))
    wait_shown(app, QUALITY + ".caption", "QUALITY")

    # The face lightens under the cursor
    move(app, OFF_QUALITY)
    app.check(color(app, QUALITY + ".face") == FACE, "the idle face is %s" %
              (color(app, QUALITY + ".face"),))
    move(app, ON_QUALITY)
    app.wait_for("get %s.face Rgb" % QUALITY,
                 lambda out: color(app, QUALITY + ".face") == FACE_HOVER,
                 what="the face of the button under the cursor")

    # Every click cycles the tier down and the view follows; the game stays paused
    for tier, label in CYCLE:
        app.exec("mouse.click left")
        app.wait_input()
        check_tier(app, tier)
        wait_shown(app, QUALITY + ".value", label)
        app.expect(STATE % "Paused", "true")

    move(app, OFF_QUALITY)
    app.wait_for("get %s.face Rgb" % QUALITY,
                 lambda out: color(app, QUALITY + ".face") == FACE,
                 what="the face of the button after the cursor left")


def test_stats(app):
    setup(app)
    start(app)
    set_state(app, "Lost")
    wait_screen(app, END)

    # 725 seconds are 12:05
    stats(app, 725.4, 37, 9, 25)
    wait_shown(app, END + ".stats", "37 critters killed - survived 12:05 to wave 9")
    wait_shown(app, END + ".score", "25 Luminite shipped")
    app.check(color(app, END + ".score") == TEAL, "the score is %s" %
              (color(app, END + ".score"),))
    app.check(color(app, END + ".stats") == TEXT, "the stats are %s" %
              (color(app, END + ".stats"),))
    app.check(color(app, END + ".again") == SOFT, "the last line is %s" %
              (color(app, END + ".again"),))

    # Less than a minute, and a full hour
    stats(app, 5.9, 0, 0, 0)
    wait_shown(app, END + ".stats", "0 critters killed - survived 0:05 to wave 0")
    wait_shown(app, END + ".score", "0 Luminite shipped")
    stats(app, 3600, 1200, 31, 400)
    wait_shown(app, END + ".stats", "1200 critters killed - survived 60:00 to wave 31")


def test_restart(app):
    setup(app)
    start(app)

    # R does nothing while playing
    key(app, "R")
    app.wait_frames(SETTLE)
    app.expect(STATE % "Playing", "true")

    set_state(app, "Lost")
    wait_screen(app, END)
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    wait_hidden(app, END + ".heading")
    wait_screen(app, None)
    app.check(float(app.exec(CLOCK)) < 1, "the clock of the new run is %s" % app.exec(CLOCK))

    # Also from the screen of a won run
    set_state(app, "Won")
    wait_shown(app, END + ".heading", "YOU ESCAPED")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    wait_screen(app, None)
