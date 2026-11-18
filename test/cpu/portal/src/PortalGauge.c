/**
 * @file PortalGauge.c
 * @brief The PortalGauge and PortalReadout of test/assets/portal/screen.flecs: the label and the
 * value they show, the bar of a fraction that stays between empty and full, and the color of a
 * gauge.
 */

#include "screen_test.h"

/** A gauge of half of 200 pixels, one over full, one under empty and one in red, and a readout */
#define GAUGE_SCENE \
    "bars {\n" \
    "  half { PortalGauge: {label: \"SPEED\", text: \"750 rpm\", fraction: 0.5, width: 200} }\n" \
    "  over { PortalGauge: {fraction: 1.7, width: 200} }\n" \
    "  under { PortalGauge: {fraction: -0.4, width: 200} }\n" \
    "  red { PortalGauge: {fraction: 0.25, width: 400, red: 255, green: 70, blue: 55} }\n" \
    "}\n" \
    "readout { PortalReadout: {label: \"TELEGRAPH\", text: \"STOP\", red: 255, green: 200, blue: 80} }\n"

/** Create a world with the screen templates and the scene of GAUGE_SCENE.
 *
 * @return The world
 */
static ecs_world_t* gauge_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/ui/widgets.flecs", "box.ui.widgets");
    screen_load(world, "test/assets/portal/screen.flecs", NULL);
    test_assert(ecs_script(world, { .code = GAUGE_SCENE }) != 0);
    screen_run(world, 2);

    return world;
}

/** Check the color of an element.
 *
 * @param world The world
 * @param path Path of the element with the Rgb
 * @param r Red
 * @param g Green
 * @param b Blue
 */
static void gauge_check_color(
    ecs_world_t *world,
    const char *path,
    int r,
    int g,
    int b)
{
    const Rgb *rgb = ecs_get(world, screen_e(world, path), Rgb);
    test_assert(rgb != NULL);
    test_int(rgb->r, r);
    test_int(rgb->g, g);
    test_int(rgb->b, b);
}

void PortalGauge_shows_label_and_value(void) {
    ecs_world_t *world = gauge_world();

    test_str(screen_text(world, "bars.half.head.caption"), "SPEED");
    test_str(screen_text(world, "bars.half.head.amount"), "750 rpm");
    test_flt(screen_width(world, "bars.half"), 200);

    ecs_fini(world);
}

void PortalGauge_bar_follows_fraction(void) {
    ecs_world_t *world = gauge_world();

    /* A fill is never quite zero wide: a bar of width 0 would not be a bar */
    test_assert(fabsf(screen_width(world, "bars.half.track.fill") - 100) < 0.1f);
    test_assert(fabsf(screen_width(world, "bars.red.track.fill") - 100) < 0.1f);
    test_flt(screen_width(world, "bars.red"), 400);

    ecs_fini(world);
}

void PortalGauge_bar_stays_in_track(void) {
    ecs_world_t *world = gauge_world();

    test_assert(fabsf(screen_width(world, "bars.over.track.fill") - 200) < 0.1f);
    test_assert(screen_width(world, "bars.under.track.fill") < 0.1f);

    ecs_fini(world);
}

void PortalGauge_color(void) {
    ecs_world_t *world = gauge_world();

    gauge_check_color(world, "bars.half.track.fill", 110, 220, 255);
    gauge_check_color(world, "bars.half.head.amount", 110, 220, 255);
    gauge_check_color(world, "bars.red.track.fill", 255, 70, 55);
    gauge_check_color(world, "bars.red.head.amount", 255, 70, 55);

    ecs_fini(world);
}

void PortalGauge_readout(void) {
    ecs_world_t *world = gauge_world();

    test_str(screen_text(world, "readout.caption"), "TELEGRAPH");
    test_str(screen_text(world, "readout.amount"), "STOP");
    gauge_check_color(world, "readout.amount", 255, 200, 80);

    ecs_fini(world);
}
