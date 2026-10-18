"""The UI showcase driven through its command port: clicks over HTTP invoke input actions through
the screen space, world space and render-to-texture widgets, and the actions change the state,
the 3D scene and the renders of the app.

Every case runs in a fresh apps/uishowcase process with its default scene (1280x720) and gets
the App of tools/box_client.py. The runner checks after every case that the app logged no
error and quits cleanly. The coordinates are window points for 1280x720, each checked with
PickStatus before it is clicked.
"""
from box_client import mean_luma

APP = "apps/uishowcase"
ARGS = []

COUNTER = "[166, 298]"
TOGGLE = "[47, 340]"
KNOB = "[166, 399]"
KNOB_RIGHT = "[206, 399]"
CRATE = "[345, 350]"
PANEL_OVER_CRATE = "[300, 360]"
WORLD_LAMP = "[475, 183]"
TEXTURE_START = "[760, 250]"
TEXTURE_STOP = "[975, 250]"

PANEL = "showcase.hud.screen.panel"
COUNTER_PATH = PANEL + ".counter"
TOGGLE_PATH = PANEL + ".shadow_toggle"
SLIDER_PATH = PANEL + ".sun_slider"
LAMP_PATH = "showcase.control.lamp_button"
START_PATH = "showcase.screen_ui.dashboard.controls.start"
STOP_PATH = "showcase.screen_ui.dashboard.controls.stop"
STATUS_PATH = "showcase.screen_ui.dashboard.head.status"
PICK = "get box.picking.PickStatus PickStatus."

# The region of the 1280x720 capture around the lamp: the post, the ground it lights and the
# side of the crate next to it. With the lamp on its mean luminance rises by about 38 of 255; a
# rise of at least LAMP_BRIGHTER counts as brighter, well above GPU noise and well below the
# rise of the light
LAMP_REGION = (300, 280, 600, 450)
LAMP_BRIGHTER = 15


def number(out):
    return float(out)


def hover(app, point, handler):
    """Move the cursor to point and wait until PickStatus.handler is handler."""
    app.exec("mouse.move %s" % point)
    app.wait_input()
    app.wait_for(PICK + "handler", handler)


def click(app, point, handler):
    """Check the handler at point, then click it and run the input."""
    hover(app, point, handler)
    app.exec("mouse.click left %s" % point)
    app.wait_input()


def key(app, name):
    app.exec("key.press %s" % name)
    app.wait_input()


def state(member):
    return "get showcase Showcase.mut.%s" % member


def test_screen_button(app):
    hover(app, COUNTER, COUNTER_PATH)
    for _ in range(3):
        app.exec("mouse.click left %s" % COUNTER)
    app.wait_input()
    app.wait_for(state("clicks"), "3")
    app.wait_for("get %s.text Text.value" % COUNTER_PATH, '"Clicks: 3"')
    app.expect(state("last"), '"count"')

    key(app, "C")
    app.wait_for(state("clicks"), "4")
    app.wait_for("get %s.text Text.value" % COUNTER_PATH, '"Clicks: 4"')


def test_screen_toggle_and_slider(app):
    app.expect("get showcase.world_view View.shadows.enabled", "true")
    click(app, TOGGLE, TOGGLE_PATH)
    app.wait_for("get showcase.world_view View.shadows.enabled", "false")
    app.expect(state("last"), '"shadows"')

    app.expect("get showcase.sun DirectionalLight.intensity", "2")
    hover(app, KNOB, SLIDER_PATH)
    app.exec("mouse.drag left %s %s" % (KNOB, KNOB_RIGHT))
    app.wait_input()
    out = app.wait_for("get showcase.sun DirectionalLight.intensity",
                       lambda out: 2.5 < number(out) < 3, what="an intensity in (2.5, 3)")
    sun = app.get("showcase", "Showcase.mut.sun")
    app.check(abs(sun - number(out)) < 1e-4,
              "Showcase.mut.sun %g differs from the intensity %s" % (sun, out))


def test_hud_blocks_scene(app):
    hover(app, PANEL_OVER_CRATE, PANEL)
    app.expect(PICK + "hit", PANEL)
    app.exec("mouse.click left %s" % PANEL_OVER_CRATE)
    app.wait_input()
    app.wait_frames(5)
    app.expect(state("selected"), "false")
    app.expect(state("last"), '"none"')

    click(app, CRATE, "showcase.crate")
    app.wait_for(state("selected"), "true")
    app.wait_for("get showcase.crate Tint", "{r: 255, g: 220, b: 90, a: 150}")
    app.expect(state("last"), '"select"')


def test_world_lamp(app):
    hover(app, WORLD_LAMP, LAMP_PATH)
    app.expect(PICK + "view", "showcase.world_view")
    app.expect("get showcase.lamp PointLight.intensity", "0")
    dark = mean_luma(app.capture("lamp_off"), LAMP_REGION)

    app.exec("mouse.click left %s" % WORLD_LAMP)
    app.wait_input()
    app.wait_for("get showcase.lamp PointLight.intensity", lambda out: number(out) > 5,
                 what="an intensity above 5")
    app.expect(state("lamp"), "true")
    lit = mean_luma(app.capture("lamp_on"), LAMP_REGION)
    app.check(lit - dark >= LAMP_BRIGHTER,
              "the region %s around the lamp is %.1f bright with the lamp on, %.1f off: less "
              "than %d brighter" % (LAMP_REGION, lit, dark, LAMP_BRIGHTER))

    key(app, "L")
    app.wait_for("get showcase.lamp PointLight.intensity", "0")
    app.expect(state("lamp"), "false")


def test_texture_reactor(app):
    app.expect("get %s Text.value" % STATUS_PATH, '"OFFLINE"')
    hover(app, TEXTURE_START, START_PATH)
    target = app.exec(PICK + "target")
    app.check(target.startswith("showcase.screen_ui."),
              "PickStatus.target %s is not in the UI scene of the texture" % target)
    start = app.exec(PICK + "hit")
    app.check(start.startswith("showcase.screen.slice_"),
              "PickStatus.hit %s is not a slice of the screen" % start)
    app.exec("mouse.click left %s" % TEXTURE_START)
    app.wait_input()
    app.wait_for(state("reactor"), "true")
    app.wait_for("get %s Text.value" % STATUS_PATH, '"ONLINE"')

    hover(app, TEXTURE_STOP, STOP_PATH)
    stop = app.exec(PICK + "hit")
    app.check(stop.startswith("showcase.screen.slice_") and stop != start,
              "STOP is on %s, START on %s: not another slice" % (stop, start))
    app.exec("mouse.click left %s" % TEXTURE_STOP)
    app.wait_input()
    app.wait_for(state("reactor"), "false")
    app.wait_for("get %s Text.value" % STATUS_PATH, '"OFFLINE"')
    app.expect(state("last"), '"reactor_stop"')


def test_quit(app):
    app.expect("logs error", "")
    app.quit()
