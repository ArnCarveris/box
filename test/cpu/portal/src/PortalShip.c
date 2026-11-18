/**
 * @file PortalShip.c
 * @brief The ship screens of test/assets/portal on one ship: the chart of its route with the
 * marks of its waypoints and the ship, and the harbour, engine, navigation and helm screens,
 * each of which shows what its PortalShipStatus says and follows a change of it.
 */

#include "screen_test.h"

/** The ship at its mooring, and a route of three waypoints, a quay, a buoy 50 m from it and a
 * reef 200 m from it. A chart of 100 by 100 m in 200 by 200 pixels, two pixels to a metre. */
#define SHIP_SCENE \
    "freighter {\n" \
    "  PortalShipStatus: {\n" \
    "    x: 55, z: 30, heading: 270, speed: 0, rudder: 0, telegraph: 0, rpm: 0, load: 0.1,\n" \
    "    exhaust: 120, heel: 0, trim: 0, eta: -1, sails: 272, moored: true, manual: false\n" \
    "  }\n" \
    "}\n" \
    "const route: PortalWaypoint[] = [{\"QUAY\", 55, 30}, {\"BUOY\", 85, 70}, {\"REEF\", -65, -130}]\n" \
    "chart {\n" \
    "  PortalChart: {\n" \
    "    points: [{\"A\", 10, 20}, {\"B\", 90, 20}, {\"C\", 50, 80}], ship: freighter,\n" \
    "    x0: 0, z0: 0, x1: 100, z1: 100, width: 200, height: 200, dots: 3\n" \
    "  }\n" \
    "}\n" \
    "console {\n" \
    "  PortalHarbourScreen harbour(ship: freighter, route: route)\n" \
    "  PortalEngineScreen engine(ship: freighter)\n" \
    "  PortalNavigationScreen nav(ship: freighter, route: route)\n" \
    "  PortalHelmScreen helm(ship: freighter)\n" \
    "}\n"

/** Create a world with the screen and ship templates and the scene of SHIP_SCENE.
 *
 * @return The world
 */
static ecs_world_t* ship_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/ui/widgets.flecs", "box.ui.widgets");
    screen_load(world, "test/assets/portal/screen.flecs", NULL);
    screen_load(world, "test/assets/portal/ship_chart.flecs", NULL);
    screen_load(world, "test/assets/portal/harbour_screen.flecs", NULL);
    screen_load(world, "test/assets/portal/engine_screen.flecs", NULL);
    screen_load(world, "test/assets/portal/navigation_screen.flecs", NULL);
    screen_load(world, "test/assets/portal/helm_screen.flecs", NULL);
    test_assert(ecs_script(world, { .code = SHIP_SCENE }) != 0);
    screen_run(world, 4);

    return world;
}

/** Write a member of the PortalShipStatus of the ship and let the screens follow.
 *
 * @param world The world
 * @param member Name of the member
 * @param value The value, a bool as 0 or 1
 */
static void ship_set(
    ecs_world_t *world,
    const char *member,
    double value)
{
    screen_set(world, "freighter", "PortalShipStatus", member, value);
    screen_run(world, 4);
}

/** Check where a mark of a chart is, the place of its Item.
 *
 * @param world The world
 * @param path Path of the mark
 * @param x Offset of the mark from the left of the chart
 * @param y Offset of the mark from the top of the chart, negative below it
 */
static void ship_check_mark(
    ecs_world_t *world,
    const char *path,
    float x,
    float y)
{
    const LayoutItem *item = ecs_get(world, screen_e(world, path), LayoutItem);
    test_assert(item != NULL);
    test_assert(fabsf(item->x - x) < 0.01f);
    test_assert(fabsf(item->y - y) < 0.01f);
}

/** Check the color of an element.
 *
 * @param world The world
 * @param path Path of the element with the Rgb
 * @param r Red
 * @param g Green
 * @param b Blue
 */
static void ship_check_color(
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

void PortalShip_chart_marks(void) {
    ecs_world_t *world = ship_world();

    /* Three dots on each of the three legs, the last leg back to the first waypoint */
    test_assert(ecs_lookup(world, "chart.dot_0_0") != 0);
    test_assert(ecs_lookup(world, "chart.dot_2_2") != 0);
    test_assert(ecs_lookup(world, "chart.dot_0_3") == 0);
    test_assert(ecs_lookup(world, "chart.dot_3_0") == 0);

    /* A mark on each waypoint */
    test_assert(ecs_lookup(world, "chart.waypoint_0") != 0);
    test_assert(ecs_lookup(world, "chart.waypoint_2") != 0);
    test_assert(ecs_lookup(world, "chart.waypoint_3") == 0);
    test_flt(screen_width(world, "chart.waypoint_0"), 6);
    test_flt(screen_width(world, "chart.dot_0_0"), 2);
    test_flt(screen_width(world, "chart"), 200);

    /* y runs up from the top anchor: a place at z metres is z * 2 pixels below the top */
    ship_check_mark(world, "chart.waypoint_0", 17, -37);
    ship_check_mark(world, "chart.dot_0_0", 59, -39);
    ship_check_mark(world, "chart.dot_2_2", 39, -69);

    ecs_fini(world);
}

void PortalShip_chart_ship_mark(void) {
    ecs_world_t *world = ship_world();

    test_flt(screen_width(world, "chart.mark"), 9);
    ship_check_mark(world, "chart.mark", 55 * 2 - 4.5f, 4.5f - 30 * 2);

    /* The mark follows the ship */
    ship_set(world, "x", 25);
    ship_set(world, "z", 60);
    ship_check_mark(world, "chart.mark", 25 * 2 - 4.5f, 4.5f - 60 * 2);

    ecs_fini(world);
}

void PortalShip_harbour(void) {
    ecs_world_t *world = ship_world();

    test_str(screen_text(world, "console.harbour.header.band.caption"), "HARBOUR // TRAFFIC");
    test_str(screen_text(world, "console.harbour.header.band.state"), "SAILS IN 4:32");
    test_str(screen_text(world, "console.harbour.body.side.state.amount"), "MOORED");
    test_str(screen_text(world, "console.harbour.body.side.speed.amount"), "0.0 kn");
    test_str(screen_text(world, "console.harbour.body.side.heading.amount"), "270" DEGREE);
    test_str(screen_text(world, "console.harbour.body.side.steering.amount"), "AUTOPILOT");
    test_assert(ecs_lookup(world, "console.harbour.body.chart.waypoint_2") != 0);
    test_assert(ecs_lookup(world, "console.harbour.body.chart.mark") != 0);

    ship_set(world, "moored", false);
    ship_set(world, "speed", 8.4);
    ship_set(world, "heading", 45);
    ship_set(world, "manual", true);
    test_str(screen_text(world, "console.harbour.header.band.state"), "HELM MANUAL");
    test_str(screen_text(world, "console.harbour.body.side.state.amount"), "UNDERWAY");
    test_str(screen_text(world, "console.harbour.body.side.speed.amount"), "8.4 kn");
    test_str(screen_text(world, "console.harbour.body.side.heading.amount"), "45" DEGREE);
    test_str(screen_text(world, "console.harbour.body.side.steering.amount"), "HELM MANUAL");

    ecs_fini(world);
}

void PortalShip_engine(void) {
    ecs_world_t *world = ship_world();

    test_str(screen_text(world, "console.engine.header.band.state"), "STOPPED");
    test_str(screen_text(world, "console.engine.body.side.telegraph.amount"), "STOP");
    test_str(screen_text(world, "console.engine.body.gauges.first.speed.head.amount"), "0 rpm");
    test_str(screen_text(world, "console.engine.body.gauges.first.load.head.amount"), "10%");

    ship_set(world, "telegraph", 2);
    ship_set(world, "rpm", 95);
    ship_set(world, "load", 0.85);
    ship_set(world, "exhaust", 410);
    test_str(screen_text(world, "console.engine.header.band.state"), "RUNNING");
    test_str(screen_text(world, "console.engine.body.side.telegraph.amount"), "HALF AHEAD");
    test_str(screen_text(world, "console.engine.body.gauges.first.speed.head.amount"), "95 rpm");
    test_str(screen_text(world, "console.engine.body.gauges.first.load.head.amount"), "85%");
    test_str(screen_text(world, "console.engine.body.gauges.second.exhaust.head.amount"),
        "410 " DEGREE "C");

    /* A loaded engine turns amber */
    ship_check_color(world, "console.engine.body.gauges.first.load.head.amount", 255, 200, 80);

    ecs_fini(world);
}

void PortalShip_navigation(void) {
    ecs_world_t *world = ship_world();

    test_str(screen_text(world, "console.nav.header.band.state"), "MOORED");
    test_str(screen_text(world, "console.nav.body.side.eta.amount"), "--:--");
    test_str(screen_text(world, "console.nav.body.waypoints.row_0.caption"), "QUAY");
    test_str(screen_text(world, "console.nav.body.waypoints.row_0.amount"), "0 m");
    test_str(screen_text(world, "console.nav.body.waypoints.row_1.caption"), "BUOY");
    test_str(screen_text(world, "console.nav.body.waypoints.row_1.amount"), "50 m");
    test_str(screen_text(world, "console.nav.body.waypoints.row_2.amount"), "200 m");

    /* Underway, 125 s from the dock, at the buoy */
    ship_set(world, "moored", false);
    ship_set(world, "eta", 125);
    ship_set(world, "x", 85);
    ship_set(world, "z", 70);
    test_str(screen_text(world, "console.nav.header.band.state"), "UNDERWAY");
    test_str(screen_text(world, "console.nav.body.side.eta.amount"), "2:05");
    test_str(screen_text(world, "console.nav.body.waypoints.row_0.amount"), "50 m");
    test_str(screen_text(world, "console.nav.body.waypoints.row_1.amount"), "0 m");

    ecs_fini(world);
}

void PortalShip_helm(void) {
    ecs_world_t *world = ship_world();

    test_str(screen_text(world, "console.helm.header.band.state"), "AUTOPILOT");
    test_str(screen_text(world, "console.helm.body.gauges.rudder.head.amount"),
        "MIDSHIPS 0" DEGREE);
    test_str(screen_text(world, "console.helm.body.side.telegraph.amount"), "STOP");
    test_str(screen_text(world, "console.helm.body.side.steering.amount"), "AUTOPILOT");
    test_assert(fabsf(screen_width(world, "console.helm.body.gauges.rudder.track.fill") - 235) < 0.1f);

    ship_set(world, "rudder", -12.4);
    ship_set(world, "heel", 3.2);
    ship_set(world, "trim", -1.5);
    ship_set(world, "telegraph", 3);
    ship_set(world, "manual", true);
    test_str(screen_text(world, "console.helm.header.band.state"), "HELM MANUAL");
    test_str(screen_text(world, "console.helm.body.gauges.rudder.head.amount"), "PORT 12" DEGREE);
    test_str(screen_text(world, "console.helm.body.gauges.heel.head.amount"), "3.2" DEGREE);
    test_str(screen_text(world, "console.helm.body.gauges.trim.head.amount"), "-1.5" DEGREE);
    test_str(screen_text(world, "console.helm.body.side.telegraph.amount"), "FULL AHEAD");
    test_str(screen_text(world, "console.helm.body.side.steering.amount"), "HELM MANUAL");
    test_assert(fabsf(screen_width(world, "console.helm.body.gauges.rudder.track.fill") - 151.7f) < 0.2f);

    ecs_fini(world);
}
