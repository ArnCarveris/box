/**
 * @file SpatialLayout.c
 * @brief Benchmarks for one frame of the SpatialLayout system on a fleet of laid out ships.
 *
 * Every ship has a hull with a fitted armor plate, an aligned cockpit, an array of vents, a
 * nested array of panels and a distributed row of sensors. An iteration runs SpatialLayout once.
 */

#include <box.h>
#include <bake_bench.h>
#include "spatial.h"

/** Ships of the fleet */
#define BENCH_SHIPS (100)

/** Vents in the array of every ship */
#define BENCH_VENTS (20)

/** Create a shape with a box.
 *
 * @param world The world
 * @param parent The parent, 0 for none
 * @param x Size on the X axis
 * @param y Size on the Y axis
 * @param z Size on the Z axis
 * @return The shape
 */
static ecs_entity_t bench_layout_shape(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Box, {x, y, z});

    return e;
}

/** Create the fleet and run frames until its layout is resolved.
 *
 * @param hull Receives the hull of the first ship
 * @return The world
 */
static ecs_world_t* bench_layout_world(
    ecs_entity_t *hull)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < BENCH_SHIPS; i ++) {
        ecs_entity_t ship = ecs_new(world);
        ecs_set(world, ship, Position, {(float)i * 30, 0, 0});
        ecs_entity_t body = bench_layout_shape(world, ship, 8, 3, 18);
        if (!i) {
            *hull = body;
        }

        ecs_entity_t armor = bench_layout_shape(world, ship, 1, 0.2f, 1);
        ecs_set(world, armor, Fit, {.target = body, .axes = SpatialXZ,
            .padding = {0.4f, 0, 0.8f}});
        ecs_entity_t cockpit = bench_layout_shape(world, ship, 3, 1, 4);
        ecs_set(world, cockpit, SpatialAlign, {.target = body,
            .self = {.y = SpatialMin}, .other = {.y = SpatialMax}, .offset = {0, 0.1f, 2}});

        ecs_entity_t vents = ecs_new_w_pair(world, EcsChildOf, ship);
        ecs_entity_t rib = bench_layout_shape(world, vents, 0.15f, 0.3f, 0.4f);
        bench_layout_shape(world, rib, 0.1f, 0.1f, 0.1f);
        ecs_set(world, vents, Array, {.axis = SpatialZ, .count = BENCH_VENTS, .spacing = 0.7f});
        ecs_set(world, vents, SpatialAlign, {.target = body,
            .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});

        ecs_entity_t panels = ecs_new_w_pair(world, EcsChildOf, ship);
        ecs_entity_t row = ecs_entity(world, {.name = "row", .parent = panels});
        bench_layout_shape(world, row, 1, 0.1f, 1);
        ecs_set(world, row, Array, {.axis = SpatialX, .count = 4, .spacing = 1.2f});
        ecs_set(world, panels, Array, {.axis = SpatialZ, .count = 3, .spacing = 1.2f});
        ecs_set(world, panels, SpatialAlign, {.target = body,
            .self = {.x = SpatialMin}, .other = {.x = SpatialMax}});

        ecs_entity_t sensors = ecs_new_w_pair(world, EcsChildOf, ship);
        bench_layout_shape(world, sensors, 0.5f, 0.5f, 0.5f);
        bench_layout_shape(world, sensors, 0.5f, 0.5f, 0.5f);
        bench_layout_shape(world, sensors, 0.5f, 0.5f, 0.5f);
        ecs_set(world, sensors, Distribute, {.axis = SpatialX, .span = 6});
        ecs_set(world, sensors, SpatialAlign, {.target = cockpit,
            .self = {.y = SpatialMin}, .other = {.y = SpatialMax}});
    }

    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    return world;
}

/** Count the nodes the last layout update solved and the arrays it synced, per iteration.
 *
 * @param b The case
 * @param world The world
 */
static void bench_layout_count(
    bench_t *b,
    const ecs_world_t *world)
{
    box_spatial_layout_stats_t stats = {0};
    box_spatial_layout_stats(world, &stats);
    bench_counter(b, "solved", stats.solved);
    bench_counter(b, "synced", stats.synced);
}

void SpatialLayout_unchanged_100_ships(
    bench_t *b)
{
    // engine/spatial_layout_incremental
    ecs_entity_t hull = 0;
    ecs_world_t *world = bench_layout_world(&hull);
    ecs_entity_t system = ecs_lookup(world, "box.spatial.SpatialLayout");
    bench_set_items(b, BENCH_SHIPS);
    while (bench_iter(b)) {
        ecs_run(world, system, 0, NULL);
        bench_layout_count(b, world);
    }

    ecs_fini(world);
}

void SpatialLayout_one_target_resized_100_ships(
    bench_t *b)
{
    // engine/spatial_layout_incremental
    ecs_entity_t hull = 0;
    ecs_world_t *world = bench_layout_world(&hull);
    ecs_entity_t system = ecs_lookup(world, "box.spatial.SpatialLayout");
    int32_t frame = 0;
    bench_set_items(b, BENCH_SHIPS);
    while (bench_iter(b)) {
        frame ++;
        ecs_set(world, hull, Box, {8, 3, (frame % 2) ? 20.0f : 18.0f});
        ecs_run(world, system, 0, NULL);
        bench_layout_count(b, world);
    }

    ecs_fini(world);
}
