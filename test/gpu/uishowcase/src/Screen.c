/**
 * @file Screen.c
 * @brief The UI showcase scene driven through the command API: widgets, keys and 3D picking.
 */

#include <box.h>
#include <bake_test.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Path of the showcase scene in the repository */
#define SHOWCASE_SCENE "etc/scenes/ui_showcase.flecs"

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

/** Find the scene in the working directory or one of its ancestors.
 *
 * @return The path of the scene, owned by the caller, NULL when it is not found
 */
static char* showcase_path(void) {
    char dir[4096];
    char *result = NULL, *slash = NULL;
    if (!getcwd(dir, sizeof(dir))) {
        goto done;
    }

    do {
        result = flecs_asprintf("%s/%s", dir, SHOWCASE_SCENE);
        if (!access(result, R_OK)) {
            goto done;
        }

        ecs_os_free(result);
        result = NULL;
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
        }
    } while (slash && dir[0]);

done:
    return result;
}

/** Run a command line that must succeed.
 *
 * @param world The world
 * @param session The terminal session
 * @param fmt Format of the command line
 * @return The output of the command, owned by the caller
 */
static char* showcase_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    char *line = flecs_vasprintf(fmt, args);
    va_end(args);
    bool error = true;
    char *out = box_terminal_run(world, session, line, &error);
    if (error) {
        printf("command '%s' failed: %s\n", line, out);
    }

    test_bool(error, false);
    ecs_os_free(line);

    return out;
}

/** Run a command line and compare its output.
 *
 * @param world The world
 * @param session The terminal session
 * @param line The command line
 * @param expect The output it must print
 */
static void showcase_expect(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *expect)
{
    char *out = showcase_run(world, session, "%s", line);
    test_str(out, expect);
    ecs_os_free(out);
}

/** Step a number of frames.
 *
 * @param world The world
 * @param frames The number of frames
 */
static void showcase_step(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0);
    }
}

/** Run the input commands queued so far: step until the input queue is drained and the frames
 * after it have applied the actions and the reactive updates.
 *
 * @param world The world
 * @param session The terminal session
 */
static void showcase_settle(
    ecs_world_t *world,
    ecs_entity_t session)
{
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        char *out = showcase_run(world, session, "input.wait");
        bool drained = !strcmp(out, "0");
        ecs_os_free(out);
        if (drained) {
            break;
        }

        ecs_progress(world, 0);
    }

    test_assert(i < 1000);
    showcase_step(world, 3);
}

/** Move the cursor to a point and check the entity that handles the pointer there.
 *
 * @param world The world
 * @param session The terminal session
 * @param point The window points, as [x, y]
 * @param handler Path of the handler PickStatus must report
 */
static void showcase_hover(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *point,
    const char *handler)
{
    ecs_os_free(showcase_run(world, session, "mouse.move %s", point));
    showcase_settle(world, session);
    showcase_expect(world, session, "get box.picking.PickStatus PickStatus.handler", handler);
}

/** Get a member of the state of the showcase.
 *
 * @param world The world
 * @param session The terminal session
 * @param member The member of Showcase.mut
 * @return The value as the command prints it, owned by the caller
 */
static char* showcase_state(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *member)
{
    return showcase_run(world, session, "get showcase Showcase.mut.%s", member);
}

/** Get a floating point member of the state of the showcase.
 *
 * @param world The world
 * @param session The terminal session
 * @param member The member of Showcase.mut
 * @return The value
 */
static double showcase_number(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *member)
{
    char *out = showcase_state(world, session, member);
    double result = strtod(out, NULL);
    ecs_os_free(out);

    return result;
}

/** Load the showcase scene into a new headless world and open a terminal session.
 *
 * @param session Receives the session
 * @return The world
 */
static ecs_world_t* showcase_load(
    ecs_entity_t *session)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = showcase_path();
    test_assert(path != NULL);
    ecs_entity_t script = ecs_script(world, { .filename = path });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->script != NULL);
    test_assert(loaded->error == NULL);

    showcase_step(world, 2);
    *session = box_terminal_new(world);

    return world;
}

void Screen_scene_loads(void) {
    // uishowcase/screen
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    char *out = showcase_run(world, s, "action.list");
    test_assert(strstr(out, "actions.count: Count a click\n") != NULL);
    test_assert(strstr(out, "actions.select: Select the crate\n") != NULL);
    test_assert(strstr(out, "actions.shadows: Toggle the shadows of the 3D view\n") != NULL);
    ecs_os_free(out);

    showcase_expect(world, s, "get showcase Showcase.mut",
        "{clicks: 0, shadows: true, sun: 2, selected: false, last: \"none\"}");
    showcase_expect(world, s, "get showcase.world_view View.shadows.enabled", "true");
    showcase_expect(world, s, "get showcase.sun DirectionalLight.intensity", "2");
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
    test_flt(strtod(out, NULL), raised);
    ecs_os_free(out);

    showcase_hover(world, s, SHOWCASE_KNOB_RIGHT, slider);
    ecs_os_free(showcase_run(world, s,
        "mouse.drag left " SHOWCASE_KNOB_RIGHT " " SHOWCASE_SLIDER_PAST_RIGHT));
    showcase_settle(world, s);
    showcase_expect(world, s, "get showcase Showcase.mut.sun", "4");
    showcase_expect(world, s, "get showcase.sun DirectionalLight.intensity", "4");

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
