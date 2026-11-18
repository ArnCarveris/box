/**
 * @file PortalPower.c
 * @brief The power of test/assets/portal/power.flecs on two areas, two generator units and four
 * lamps: a tripped breaker darkens the lamps of its area but not an emergency lamp, stopping all
 * the units darkens every area, one unit is enough, and starting a unit lights the areas again.
 */

#include "screen_test.h"

/** Two areas, a hall with a lamp and a red beacon and a vault that stays on (essential), a lamp
 * outside of any area, and two units of 60 kW for a demand of 50 kW. */
#define POWER_SCENE \
    "hall { PortalBreaker: {demand: 20} }\n" \
    "vault { PortalBreaker: {demand: 30, essential: true} }\n" \
    "g1 { PortalGenerator: {running: true, rpm: 1500, coolant: 78, fuel: 0.8, capacity: 60} }\n" \
    "g2 { PortalGenerator: {running: true, rpm: 1500, coolant: 78, fuel: 0.8, capacity: 60} }\n" \
    "PortalGrid grid(areas: [hall, vault], units: [g1, g2])\n" \
    "lamps {\n" \
    "  hall_lamp { PortalLamp: {area: hall, intensity: 10, range: 6} }\n" \
    "  hall_beacon { PortalLamp: {area: hall, intensity: 4, range: 3, emergency: true} }\n" \
    "  vault_lamp { PortalLamp: {area: vault, intensity: 8, range: 6} }\n" \
    "  yard_lamp { PortalLamp: {intensity: 5, range: 6} }\n" \
    "}\n"

/** Create the world of POWER_SCENE and let the grid tick once.
 *
 * @return The world
 */
static ecs_world_t* power_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/portal/power.flecs", NULL);
    test_assert(ecs_script(world, { .code = POWER_SCENE }) != 0);
    screen_run(world, 8);

    return world;
}

/** Get the intensity of the PointLight of a lamp.
 *
 * @param world The world
 * @param name Name of the lamp under lamps
 * @return The intensity
 */
static float power_intensity(
    ecs_world_t *world,
    const char *name)
{
    char path[64];
    snprintf(path, sizeof(path), "lamps.%s", name);

    const PointLight *light = ecs_get(world, screen_e(world, path), PointLight);
    test_assert(light != NULL);

    return light->intensity;
}

/** Check that the lamps of the scene are lit as they are written.
 *
 * @param world The world
 */
static void power_check_lit(
    ecs_world_t *world)
{
    test_flt(power_intensity(world, "hall_lamp"), 10);
    test_flt(power_intensity(world, "hall_beacon"), 4);
    test_flt(power_intensity(world, "vault_lamp"), 8);
    test_flt(power_intensity(world, "yard_lamp"), 5);
}

/** Start or stop a unit.
 *
 * @param world The world
 * @param unit Name of the unit
 * @param running Whether it runs
 */
static void power_run_unit(
    ecs_world_t *world,
    const char *unit,
    bool running)
{
    screen_set(world, unit, "PortalGenerator", "running", running);
}

void PortalPower_lit_at_start(void) {
    ecs_world_t *world = power_world();

    power_check_lit(world);
    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 0);
    test_flt(screen_get(world, "grid", "PortalPowerStatus", "up"), 2);
    test_flt(screen_get(world, "grid", "PortalPowerStatus", "capacity"), 120);
    test_flt(screen_get(world, "grid", "PortalPowerStatus", "load"), 50);

    ecs_fini(world);
}

void PortalPower_tripped_breaker_darkens_area(void) {
    ecs_world_t *world = power_world();

    screen_set(world, "hall", "PortalBreaker", "off", true);
    screen_run(world, 8);

    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 1);
    test_flt(screen_get(world, "vault", "PortalBreaker", "dark"), 0);
    test_flt(power_intensity(world, "hall_lamp"), 0);
    test_flt(power_intensity(world, "hall_beacon"), 4);
    test_flt(power_intensity(world, "vault_lamp"), 8);
    test_flt(power_intensity(world, "yard_lamp"), 5);

    /* The area draws nothing while it is off */
    test_flt(screen_get(world, "grid", "PortalPowerStatus", "demand"), 30);

    screen_set(world, "hall", "PortalBreaker", "off", false);
    screen_run(world, 8);
    power_check_lit(world);

    ecs_fini(world);
}

void PortalPower_all_units_stopped_darkens_everything(void) {
    ecs_world_t *world = power_world();

    power_run_unit(world, "g1", false);
    power_run_unit(world, "g2", false);
    screen_run(world, 100);

    test_assert(screen_get(world, "g1", "PortalGenerator", "rpm") < 1200);
    test_assert(screen_get(world, "g2", "PortalGenerator", "rpm") < 1200);
    test_flt(screen_get(world, "grid", "PortalPowerStatus", "up"), 0);
    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 1);
    test_flt(screen_get(world, "vault", "PortalBreaker", "dark"), 1);
    test_flt(power_intensity(world, "hall_lamp"), 0);
    test_flt(power_intensity(world, "vault_lamp"), 0);

    /* The beacon and the lamp outside stay lit */
    test_flt(power_intensity(world, "hall_beacon"), 4);
    test_flt(power_intensity(world, "yard_lamp"), 5);

    ecs_fini(world);
}

void PortalPower_one_unit_is_enough(void) {
    ecs_world_t *world = power_world();

    power_run_unit(world, "g1", false);
    screen_run(world, 100);

    test_flt(screen_get(world, "grid", "PortalPowerStatus", "up"), 1);
    test_flt(screen_get(world, "hall", "PortalBreaker", "dark"), 0);
    power_check_lit(world);

    ecs_fini(world);
}

void PortalPower_start_restores(void) {
    ecs_world_t *world = power_world();

    power_run_unit(world, "g1", false);
    power_run_unit(world, "g2", false);
    screen_run(world, 100);
    test_flt(power_intensity(world, "hall_lamp"), 0);

    power_run_unit(world, "g2", true);
    screen_run(world, 100);

    test_assert(screen_get(world, "g2", "PortalGenerator", "rpm") > 1200);
    test_flt(screen_get(world, "grid", "PortalPowerStatus", "up"), 1);
    power_check_lit(world);

    ecs_fini(world);
}
