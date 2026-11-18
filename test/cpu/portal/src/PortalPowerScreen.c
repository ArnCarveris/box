/**
 * @file PortalPowerScreen.c
 * @brief The power distribution screen and the generator screen of test/assets/portal on the
 * power of two areas and two units: what they show, and the breaker, LOAD SHED, RESTORE and
 * START / STOP buttons clicked with an aim like a walker's crosshair at the middle of the view.
 */

#include "screen_test.h"

/** The areas hall and vault (essential), two units, a grid, the power screen and the screen of
 * unit g1 side by side 2 m apart, 1.35 m up facing +z, and a camera looking at them along -z.
 * A screen is 1.42 by 0.62 m, 710 by 310 virtual pixels. */
#define POWER_SCREEN_SCENE \
    "using box.transform\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.surface\n" \
    "window { Surface: {width: 1280, height: 720} }\n" \
    "hall { PortalBreaker: {demand: 20} }\n" \
    "vault { PortalBreaker: {demand: 30, essential: true} }\n" \
    "g1 { PortalGenerator: {running: true, rpm: 1500, coolant: 78, fuel: 0.8, capacity: 60} }\n" \
    "g2 { PortalGenerator: {running: true, rpm: 1500, coolant: 78, fuel: 0.8, capacity: 60} }\n" \
    "PortalGrid grid(areas: [hall, vault], units: [g1, g2])\n" \
    "console {\n" \
    "  PortalPowerScreen power(\n" \
    "    width: 1.42, height: 0.62, grid: grid, areas: [hall, vault], names: [\"HALL\", \"VAULT\"])\n" \
    "  {\n" \
    "    Position: {0, 1.35, 0}\n" \
    "  }\n" \
    "  PortalGeneratorScreen gen(width: 1.42, height: 0.62, unit: g1, title: \"GENERATOR G1\")\n" \
    "  {\n" \
    "    Position: {2, 1.35, 0}\n" \
    "  }\n" \
    "}\n" \
    "camera {\n" \
    "  Position: {0, 1.35, 2}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "view {\n" \
    "  View: {camera: camera}\n" \
    "}\n"

/** Path of the trip button of the hall, in the first column */
#define POWER_TRIP_HALL "console.power.list.first.area_0.trip"

/** Path of the trip button of the vault, in the second column */
#define POWER_TRIP_VAULT "console.power.list.second.area_1.trip"

/** Path of the START / STOP button of unit g1 */
#define POWER_UNIT_BUTTON "console.gen.body.side.power"

/** Create the world of POWER_SCREEN_SCENE with the widgets, the screen and the power templates,
 * and let the grid tick and the layout settle.
 *
 * @return The world
 */
static ecs_world_t* power_screen_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/ui/widgets.flecs", "box.ui.widgets");
    screen_load(world, "test/assets/portal/screen.flecs", NULL);
    screen_load(world, "test/assets/portal/power.flecs", NULL);
    screen_load(world, "test/assets/portal/power_screen.flecs", NULL);
    screen_load(world, "test/assets/portal/generator_screen.flecs", NULL);
    test_assert(ecs_script(world, { .code = POWER_SCREEN_SCENE }) != 0);
    screen_run(world, 20);

    return world;
}

void PortalPowerScreen_shows_areas(void) {
    ecs_world_t *world = power_screen_world();

    test_str(screen_text(world, "console.power.header.band.caption"), "POWER // DISTRIBUTION");
    test_str(screen_text(world, "console.power.header.band.state"), "2 UNITS " MIDDLE_DOT " 50 / 120 kW");
    test_str(screen_text(world, "console.power.list.first.area_0.about.title"), "HALL");
    test_str(screen_text(world, "console.power.list.second.area_1.about.title"), "VAULT");
    test_str(screen_text(world, "console.power.list.first.area_0.about.status.word"), "LIVE");
    test_str(screen_text(world, "console.power.list.first.area_0.about.status.draw"), "20.0 kW");
    test_str(screen_text(world, POWER_TRIP_HALL ".caption"), "TRIP");
    test_str(screen_text(world, "console.power.footer.message"), "0 of 2 breakers off");

    ecs_fini(world);
}

void PortalPowerScreen_breaker_button(void) {
    ecs_world_t *world = power_screen_world();

    screen_click(world, POWER_TRIP_HALL, 2);
    test_flt(screen_get(world, "hall", "PortalBreaker", "off"), 1);
    test_flt(screen_get(world, "vault", "PortalBreaker", "off"), 0);

    screen_run(world, 8);
    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 1);
    test_str(screen_text(world, "console.power.list.first.area_0.about.status.word"), "OFF");
    test_str(screen_text(world, POWER_TRIP_HALL ".caption"), "CLOSE");
    test_str(screen_text(world, "console.power.footer.message"), "1 of 2 breakers off");
    test_str(screen_text(world, "console.power.header.band.state"), "2 UNITS " MIDDLE_DOT " 30 / 120 kW");

    /* The button closes it again */
    screen_click(world, POWER_TRIP_HALL, 2);
    test_flt(screen_get(world, "hall", "PortalBreaker", "off"), 0);
    screen_run(world, 8);
    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 0);
    test_str(screen_text(world, POWER_TRIP_HALL ".caption"), "TRIP");

    ecs_fini(world);
}

void PortalPowerScreen_load_shed_and_restore(void) {
    ecs_world_t *world = power_screen_world();

    /* LOAD SHED keeps the essential vault */
    screen_click(world, "console.power.footer.shed", 2);
    test_flt(screen_get(world, "hall", "PortalBreaker", "off"), 1);
    test_flt(screen_get(world, "vault", "PortalBreaker", "off"), 0);

    screen_click(world, "console.power.footer.restore", 2);
    test_flt(screen_get(world, "hall", "PortalBreaker", "off"), 0);
    test_flt(screen_get(world, "vault", "PortalBreaker", "off"), 0);

    ecs_fini(world);
}

void PortalPowerScreen_no_power(void) {
    ecs_world_t *world = power_screen_world();

    screen_set(world, "g1", "PortalGenerator", "running", false);
    screen_set(world, "g2", "PortalGenerator", "running", false);
    screen_run(world, 100);

    test_str(screen_text(world, "console.power.header.band.state"), "0 UNITS " MIDDLE_DOT " 0 / 0 kW");
    test_str(screen_text(world, "console.power.list.first.area_0.about.status.word"), "NO POWER");
    test_str(screen_text(world, "console.power.footer.message"), "NO POWER " MIDDLE_DOT " START A UNIT");

    ecs_fini(world);
}

void PortalPowerScreen_generator_shows_unit(void) {
    ecs_world_t *world = power_screen_world();

    test_str(screen_text(world, "console.gen.header.band.caption"), "GENERATOR G1");
    test_str(screen_text(world, "console.gen.header.band.state"), "ONLINE");
    test_str(screen_text(world, "console.gen.body.side.state.amount"), "ONLINE");
    test_str(screen_text(world, POWER_UNIT_BUTTON ".caption"), "STOP");
    test_str(screen_text(world, "console.gen.body.gauges.second.fuel.head.amount"), "80%");

    ecs_fini(world);
}

void PortalPowerScreen_generator_stop_and_start(void) {
    ecs_world_t *world = power_screen_world();

    screen_click(world, POWER_UNIT_BUTTON, 2);
    test_flt(screen_get(world, "g1", "PortalGenerator", "running"), 0);
    test_str(screen_text(world, POWER_UNIT_BUTTON ".caption"), "START");

    /* The unit spins down, the other one still carries the areas */
    screen_run(world, 100);
    test_assert(screen_get(world, "g1", "PortalGenerator", "rpm") < 30);
    test_str(screen_text(world, "console.gen.header.band.state"), "STOPPED");
    test_str(screen_text(world, "console.gen.body.gauges.first.speed.head.amount"), "0 rpm");
    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 0);

    screen_click(world, POWER_UNIT_BUTTON, 2);
    test_flt(screen_get(world, "g1", "PortalGenerator", "running"), 1);
    screen_run(world, 100);
    test_assert(screen_get(world, "g1", "PortalGenerator", "rpm") > 1200);
    test_str(screen_text(world, "console.gen.header.band.state"), "ONLINE");
    test_str(screen_text(world, POWER_UNIT_BUTTON ".caption"), "STOP");

    ecs_fini(world);
}
