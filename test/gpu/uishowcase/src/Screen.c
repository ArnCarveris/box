/**
 * @file Screen.c
 * @brief The UI showcase scene driven through the command API: widgets, keys and 3D picking.
 */

#include "showcase.h"
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Window points of the counter button */
#define SHOWCASE_COUNTER "[166, 298]"

/** Window points of the check box of the shadows toggle */
#define SHOWCASE_TOGGLE "[47, 340]"

/** Window points of the knob of the sun slider at its initial value of 2 */
#define SHOWCASE_KNOB "[166, 399]"

/** Window points a little right of the knob on the slider track */
#define SHOWCASE_KNOB_RIGHT "[206, 399]"

/** Window points far right of the slider, past its end */
#define SHOWCASE_SLIDER_PAST_RIGHT "[600, 399]"

/** Window points far left of the slider, past its start and outside the window */
#define SHOWCASE_SLIDER_PAST_LEFT "[-200, 399]"

/** Window points of the crate where the panel does not cover it */
#define SHOWCASE_CRATE "[345, 350]"

/** Window points of the panel background where it covers the crate */
#define SHOWCASE_PANEL_OVER_CRATE "[300, 360]"

/** Path of the counter button */
#define SHOWCASE_COUNTER_PATH "showcase.hud.screen.panel.counter"

/** Path of the panel, the InputBlocker of the HUD */
#define SHOWCASE_PANEL_PATH "showcase.hud.screen.panel"

void Screen_scene_loads(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    char *out = showcase_run(world, s, "action.list");
    test_assert(strstr(out, "actions.count: Count a click\n") != NULL);
    test_assert(strstr(out, "actions.select: Select the crate\n") != NULL);
    test_assert(strstr(out, "actions.shadows: Toggle the shadows of the 3D view\n") != NULL);
    test_assert(strstr(out, "actions.lamp: Switch the lamp\n") != NULL);
    test_assert(strstr(out, "actions.paint: Paint the crate\n") != NULL);
    test_assert(strstr(out, "actions.reactor: Toggle the reactor\n") != NULL);
    test_assert(strstr(out, "actions.reactor_start: Start the reactor\n") != NULL);
    test_assert(strstr(out, "actions.reactor_stop: Stop the reactor\n") != NULL);
    ecs_os_free(out);
    showcase_step(world, 5);
    showcase_expect(world, s, "logs warning 100", "");

    showcase_expect(world, s, "get showcase Showcase.mut",
        "{clicks: 0, shadows: true, sun: 2, selected: false, last: \"none\", lamp: false, "
        "paint: 0, reactor: false}");
    showcase_expect(world, s, "get showcase.world_view View.shadows.enabled", "true");
    test_assert(fabs(showcase_value(world, s, "get showcase.sun DirectionalLight.intensity") -
        2 * GLM_PI) < 1e-5);
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH ".text Text.value", "\"Clicks: 0\"");
    test_assert(ecs_lookup(world, "showcase.hud_view") > ecs_lookup(world, "showcase.world_view"));

    ecs_fini(world);
}

void Screen_hover_counter(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_hover(world, s, SHOWCASE_COUNTER, SHOWCASE_COUNTER_PATH);
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH " Button.mut.hover", "true");
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH " Rgb", "{r: 90, g: 140, b: 220}");

    showcase_hover(world, s, SHOWCASE_CRATE, "showcase.crate");
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH " Button.mut.hover", "false");
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH " Rgb", "{r: 60, g: 100, b: 170}");

    ecs_fini(world);
}

void Screen_click_counts(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    int32_t i;

    showcase_hover(world, s, SHOWCASE_COUNTER, SHOWCASE_COUNTER_PATH);
    for (i = 0; i < 3; i ++) {
        ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_COUNTER));
    }

    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.clicks", "3");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"count\"");
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH ".text Text.value", "\"Clicks: 3\"");
    showcase_expect(world, s, "get showcase Showcase.mut.selected", "false");

    ecs_fini(world);
}

void Screen_key_counts(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_hover(world, s, SHOWCASE_COUNTER, SHOWCASE_COUNTER_PATH);
    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_COUNTER));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.clicks", "1");

    ecs_os_free(showcase_run(world, s, "key.press C"));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.clicks", "2");
    showcase_expect(world, s, "get " SHOWCASE_COUNTER_PATH ".text Text.value", "\"Clicks: 2\"");

    ecs_os_free(showcase_run(world, s, "key.press C"));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.clicks", "3");

    ecs_fini(world);
}

void Screen_toggle_shadows(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    const char *toggle = "showcase.hud.screen.panel.shadow_toggle";

    showcase_hover(world, s, SHOWCASE_TOGGLE, toggle);
    showcase_expect(world, s, "get showcase.world_view View.shadows.enabled", "true");
    test_assert(ecs_lookup(world, "showcase.hud.screen.panel.shadow_toggle.check.mark") != 0);

    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_TOGGLE));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.shadows", "false");
    showcase_expect(world, s, "get showcase.world_view View.shadows.enabled", "false");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"shadows\"");
    test_assert(ecs_lookup(world, "showcase.hud.screen.panel.shadow_toggle.check.mark") == 0);

    showcase_hover(world, s, SHOWCASE_TOGGLE, toggle);
    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_TOGGLE));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.shadows", "true");
    showcase_expect(world, s, "get showcase.world_view View.shadows.enabled", "true");
    test_assert(ecs_lookup(world, "showcase.hud.screen.panel.shadow_toggle.check.mark") != 0);

    ecs_os_free(showcase_run(world, s, "key.press H"));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase.world_view View.shadows.enabled", "false");

    ecs_fini(world);
}

void Screen_slider_drag(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    const char *slider = "showcase.hud.screen.panel.sun_slider";
    double sun = 0, raised = 0;

    showcase_hover(world, s, SHOWCASE_KNOB, slider);
    ecs_os_free(showcase_run(world, s,
        "mouse.drag left " SHOWCASE_KNOB " " SHOWCASE_KNOB_RIGHT));
    showcase_settle(world, s);
    raised = showcase_number(world, s, "sun");
    test_assert(raised > 2.5);
    test_assert(raised < 3);
    char *out = showcase_run(world, s, "get showcase.sun DirectionalLight.intensity");
    test_assert(fabs(strtod(out, NULL) - raised * GLM_PI) < 1e-4);
    ecs_os_free(out);

    showcase_hover(world, s, SHOWCASE_KNOB_RIGHT, slider);
    ecs_os_free(showcase_run(world, s,
        "mouse.drag left " SHOWCASE_KNOB_RIGHT " " SHOWCASE_SLIDER_PAST_RIGHT));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.sun", "4");
    test_assert(fabs(showcase_value(world, s, "get showcase.sun DirectionalLight.intensity") -
        4 * GLM_PI) < 1e-5);

    showcase_hover(world, s, SHOWCASE_KNOB_RIGHT, slider);
    ecs_os_free(showcase_run(world, s,
        "mouse.drag left " SHOWCASE_KNOB_RIGHT " " SHOWCASE_KNOB));
    showcase_settle(world, s);
    sun = showcase_number(world, s, "sun");
    test_assert(sun < raised);
    test_assert(sun > 1.5);

    ecs_os_free(showcase_run(world, s,
        "mouse.drag left " SHOWCASE_KNOB " " SHOWCASE_SLIDER_PAST_LEFT));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.sun", "0");
    showcase_expect(world, s, "get showcase.sun DirectionalLight.intensity", "0");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"none\"");

    ecs_fini(world);
}

void Screen_crate_click_selects(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    ecs_entity_t crate = ecs_lookup(world, "showcase.crate");

    showcase_hover(world, s, SHOWCASE_CRATE, "showcase.crate");
    test_bool(ecs_has(world, crate, Tint), false);
    char *position = showcase_run(world, s, "get camera Position");
    char *rotation = showcase_run(world, s, "get camera Rotation");
    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_CRATE));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.selected", "true");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"select\"");
    showcase_expect(world, s, "get showcase.crate Tint", "{r: 255, g: 220, b: 90, a: 150}");
    showcase_expect(world, s, "get camera Position", position);
    showcase_expect(world, s, "get camera Rotation", rotation);
    ecs_os_free(position);
    ecs_os_free(rotation);

    showcase_hover(world, s, SHOWCASE_CRATE, "showcase.crate");
    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_CRATE));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.selected", "false");
    test_bool(ecs_has(world, crate, Tint), false);

    ecs_fini(world);
}

void Screen_panel_blocks_crate(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_hover(world, s, SHOWCASE_PANEL_OVER_CRATE, SHOWCASE_PANEL_PATH);
    showcase_expect(world, s, "get box.picking.PickStatus PickStatus.hit", SHOWCASE_PANEL_PATH);
    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_PANEL_OVER_CRATE));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.selected", "false");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"none\"");

    ecs_os_free(showcase_run(world, s, "remove " SHOWCASE_PANEL_PATH " InputBlocker"));
    showcase_hover(world, s, SHOWCASE_PANEL_OVER_CRATE, "showcase.crate");
    ecs_os_free(showcase_run(world, s, "mouse.click left " SHOWCASE_PANEL_OVER_CRATE));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.selected", "true");

    ecs_fini(world);
}
