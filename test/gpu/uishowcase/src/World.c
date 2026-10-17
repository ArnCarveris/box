/**
 * @file World.c
 * @brief The world space panel of the UI showcase: buttons in the 3D scene that switch the lamp
 * and paint the crate, and a panel hidden behind a cabinet that gets no input.
 */

#include "showcase.h"
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>

/** Window points of the lamp button of the world panel */
#define WORLD_LAMP "[475, 183]"

/** Window points of the color button of the world panel */
#define WORLD_COLOR "[477, 220]"

/** Window points of the button of the service panel, which the cabinet hides */
#define WORLD_HIDDEN "[584, 322]"

/** Path of the lamp button */
#define WORLD_LAMP_PATH "showcase.control.lamp_button"

/** Path of the color button */
#define WORLD_COLOR_PATH "showcase.control.paint_button"

/** Get the intensity of the point light of the lamp.
 *
 * @param world The world
 * @param session The terminal session
 * @return The intensity
 */
static double world_lamp_intensity(
    ecs_world_t *world,
    ecs_entity_t session)
{
    return showcase_value(world, session, "get showcase.lamp PointLight.intensity");
}

void World_hover_lamp_button(void) {
    // uishowcase/world
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_hover(world, s, WORLD_LAMP, WORLD_LAMP_PATH);
    showcase_expect(world, s, "get box.picking.PickStatus PickStatus.view", "showcase.world_view");
    showcase_expect(world, s, "get box.picking.PickStatus PickStatus.target",
        WORLD_LAMP_PATH ".text");
    showcase_expect(world, s, "get " WORLD_LAMP_PATH " Button.mut.hover", "true");
    showcase_expect(world, s, "get " WORLD_LAMP_PATH " Rgb", "{r: 90, g: 140, b: 220}");

    showcase_hover(world, s, WORLD_COLOR, WORLD_COLOR_PATH);
    showcase_expect(world, s, "get " WORLD_LAMP_PATH " Button.mut.hover", "false");
    showcase_expect(world, s, "get " WORLD_LAMP_PATH " Rgb", "{r: 60, g: 100, b: 170}");
    showcase_expect(world, s, "get " WORLD_COLOR_PATH " Button.mut.hover", "true");

    ecs_fini(world);
}

void World_lamp_button_toggles_light(void) {
    // uishowcase/world
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    ecs_entity_t bulb = ecs_lookup(world, "showcase.lamp.bulb");
    test_assert(bulb != 0);
    test_flt(world_lamp_intensity(world, s), 0);
    test_bool(ecs_has(world, bulb, Emissive), false);

    showcase_hover(world, s, WORLD_LAMP, WORLD_LAMP_PATH);
    showcase_click(world, s, WORLD_LAMP);
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "true");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"lamp\"");
    test_assert(world_lamp_intensity(world, s) > 5);
    test_bool(ecs_has(world, bulb, Emissive), true);

    showcase_hover(world, s, WORLD_LAMP, WORLD_LAMP_PATH);
    showcase_click(world, s, WORLD_LAMP);
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "false");
    test_flt(world_lamp_intensity(world, s), 0);
    test_bool(ecs_has(world, bulb, Emissive), false);
    showcase_expect(world, s, "get showcase Showcase.mut.paint", "0");

    ecs_fini(world);
}

void World_lamp_key_toggles_light(void) {
    // uishowcase/world
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    ecs_entity_t bulb = ecs_lookup(world, "showcase.lamp.bulb");

    showcase_key(world, s, "L");
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "true");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"lamp\"");
    test_assert(world_lamp_intensity(world, s) > 5);
    test_bool(ecs_has(world, bulb, Emissive), true);

    showcase_key(world, s, "L");
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "false");
    test_flt(world_lamp_intensity(world, s), 0);
    test_bool(ecs_has(world, bulb, Emissive), false);

    showcase_hover(world, s, WORLD_LAMP, WORLD_LAMP_PATH);
    showcase_click(world, s, WORLD_LAMP);
    showcase_key(world, s, "L");
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "false");
    test_flt(world_lamp_intensity(world, s), 0);

    ecs_fini(world);
}

void World_color_button_cycles_crate(void) {
    // uishowcase/world
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    char *colors[3];
    int32_t i;

    colors[0] = showcase_run(world, s, "get showcase.crate Rgb");
    test_str(colors[0], "{r: 200, g: 150, b: 80}");
    for (i = 1; i < 3; i ++) {
        showcase_hover(world, s, WORLD_COLOR, WORLD_COLOR_PATH);
        showcase_click(world, s, WORLD_COLOR);
        colors[i] = showcase_run(world, s, "get showcase.crate Rgb");
    }

    showcase_expect(world, s, "get showcase Showcase.mut.paint", "2");
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"paint\"");
    test_assert(strcmp(colors[0], colors[1]) != 0);
    test_assert(strcmp(colors[1], colors[2]) != 0);
    test_assert(strcmp(colors[0], colors[2]) != 0);

    showcase_hover(world, s, WORLD_COLOR, WORLD_COLOR_PATH);
    showcase_click(world, s, WORLD_COLOR);
    showcase_expect(world, s, "get showcase Showcase.mut.paint", "0");
    showcase_expect(world, s, "get showcase.crate Rgb", colors[0]);

    showcase_key(world, s, "P");
    showcase_expect(world, s, "get showcase.crate Rgb", colors[1]);
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "false");

    for (i = 0; i < 3; i ++) {
        ecs_os_free(colors[i]);
    }

    ecs_fini(world);
}

void World_occluded_panel_ignores_click(void) {
    // uishowcase/world
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    const char *state = "{clicks: 0, shadows: true, sun: 2, selected: false, last: \"none\", "
        "lamp: false, paint: 0, reactor: false}";

    showcase_hover(world, s, WORLD_HIDDEN, "#0");
    showcase_expect(world, s, "get box.picking.PickStatus PickStatus.hit", "cabinet");
    showcase_click(world, s, WORLD_HIDDEN);
    showcase_expect(world, s, "get showcase Showcase.mut", state);
    test_flt(world_lamp_intensity(world, s), 0);
    showcase_expect(world, s, "get service.switch Button.mut.down", "false");

    ecs_os_free(showcase_run(world, s, "remove cabinet Box"));
    showcase_hover(world, s, WORLD_HIDDEN, "service.switch");
    showcase_click(world, s, WORLD_HIDDEN);
    showcase_expect(world, s, "get showcase Showcase.mut.lamp", "true");

    ecs_fini(world);
}
