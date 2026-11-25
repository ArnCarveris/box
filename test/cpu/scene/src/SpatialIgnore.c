/**
 * @file SpatialIgnore.c
 * @brief Spatial index: entities under SpatialIgnore are not indexed, cost no refit while they
 * move, and are neither hit by rays and picks nor block them.
 */

#include <box.h>
#include <bake_test.h>
#include "index.h"

/** Create a prefab of a critter of four box parts, under SpatialIgnore when asked.
 *
 * @param world The world
 * @param ignore Whether the prefab has SpatialIgnore
 * @return The prefab
 */
static ecs_entity_t ignore_critter(
    ecs_world_t *world,
    bool ignore)
{
    ecs_entity_t result = ecs_new_w_id(world, EcsPrefab);
    int32_t i;
    if (ignore) {
        ecs_add(world, result, SpatialIgnore);
    }

    for (i = 0; i < 4; i ++) {
        ecs_entity_t part = ecs_new_w_pair(world, EcsChildOf, result);
        ecs_set(world, part, Position, {(float)(i % 2) * 0.4f - 0.2f, (float)(i / 2) * 0.3f,
            0});
        ecs_set(world, part, Box, {0.3f, 0.3f, 0.3f});
    }

    return result;
}

/** Create an instance of a critter prefab that turns every frame.
 *
 * @param world The world
 * @param critter The prefab
 * @param x X of the instance
 * @param z Z of the instance
 * @return The instance
 */
static ecs_entity_t ignore_spawn(
    ecs_world_t *world,
    ecs_entity_t critter,
    float x,
    float z)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsIsA, critter);
    ecs_set(world, result, Position, {x, 0, z});
    ecs_set(world, result, Rotation, {0, 0, 0});
    ecs_set(world, result, AngularVelocity, {0, 1, 0});

    return result;
}

/** Get the first child of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The first child, 0 when it has none
 */
static ecs_entity_t ignore_first_child(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        if (!result && it.count) {
            result = it.entities[0];
        }
    }

    return result;
}

/** 1600 critters of 4 parts under SpatialIgnore turn every frame while a ray is cast every
 * frame, as picking does: only the ground is indexed, no part is refit or gets a SpatialPose,
 * the parts still move, and a ray down through a critter hits the ground.
 */
void SpatialIgnore_moving_wave_not_refit(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    enum { SIDE = 40, FRAMES = 10 };
    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {0, -0.5f, 0});
    ecs_set(world, ground, Box, {400, 1, 400});

    ecs_entity_t critter = ignore_critter(world, true);
    ecs_entity_t first = 0;
    int32_t i;
    for (i = 0; i < SIDE * SIDE; i ++) {
        ecs_entity_t e = ignore_spawn(world, critter,
            (float)(i % SIDE) * 1.5f - 30.0f, (float)(i / SIDE) * 1.5f - 30.0f);
        first = first ? first : e;
    }

    ecs_progress(world, 0.016f);
    ecs_entity_t part = ignore_first_child(world, first);
    test_assert(part != 0);
    test_assert(ecs_has(world, first, SpatialIgnore));
    test_assert(box_transform_dynamic(world, ecs_get_table(world, part)));

    box_spatial_stats_t stats;
    box_spatial_stats(world, &stats);
    test_int(stats.entities, 1);
    test_assert(!ecs_has(world, part, SpatialPose));
    vec3 min, max;
    test_assert(!box_spatial_bounds(world, part, NULL, min, max));

    WorldTransform t_buf;
    WorldTransform before = *box_transform_world(world, part, &t_buf);
    for (i = 0; i < FRAMES; i ++) {
        box_spatial_hit_t hit = {0};
        box_spatial_ray_t ray = { .origin = {-30.0f, 10, -30.0f}, .dir = {0, -1, 0} };
        test_assert(box_spatial_ray(world, &ray, &hit));
        test_uint(hit.entity, ground);
        ecs_progress(world, 0.016f);
        box_spatial_stats(world, &stats);
        test_int(stats.rebound, 0);
        test_int(stats.moved, 0);
        test_int(stats.updated, 0);
        test_int(stats.entities, 1);
    }

    const WorldTransform *after = box_transform_world(world, part, &t_buf);
    test_assert(ecs_os_memcmp(before.m, after->m, sizeof(mat4)) != 0);
    test_assert(!ecs_has(world, part, SpatialPose));

    ecs_fini(world);
}

/** A moving Interactive shape among ignored critters: the pick goes through the critters to the
 * ground, the turning wave costs no refit while the shape is still, and the shape is refit and
 * picked, hovered and left again as it moves under the cursor and away.
 */
void SpatialIgnore_moving_interactive_refit_and_picked(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 20});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {0, 0, -5});
    ecs_set(world, ground, Box, {100, 100, 1});

    ecs_entity_t critter = ignore_critter(world, true);
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ignore_spawn(world, critter, (float)(i % 10) * 1.5f - 6.0f,
            (float)(i / 10) * 0.2f - 1.0f);
    }

    ignore_spawn(world, critter, 0, 0);

    ecs_entity_t mover = ecs_new(world);
    ecs_add(world, mover, DynamicTransform);
    ecs_add(world, mover, Interactive);
    ecs_set(world, mover, Position, {10, 0, 2});
    ecs_set(world, mover, Box, {1, 1, 1});

    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0.016f);
    ecs_progress(world, 0.016f);

    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, ground);
    test_uint(status->target, 0);

    box_spatial_stats_t stats;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0.016f);
        box_spatial_stats(world, &stats);
        test_int(stats.rebound, 0);
    }

    ecs_set(world, mover, Position, {0, 0, 2});
    ecs_progress(world, 0.016f);
    box_spatial_stats(world, &stats);
    test_int(stats.rebound, 1);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, mover, NULL, min, max));
    test_flt(min[0], -0.5f);
    test_flt(max[0], 0.5f);
    ecs_progress(world, 0.016f);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, mover);
    test_uint(status->target, mover);
    test_uint(status->handler, mover);
    test_assert(ecs_has(world, mover, Hovered));

    ecs_set(world, mover, Position, {10, 0, 2});
    ecs_progress(world, 0.016f);
    box_spatial_stats(world, &stats);
    test_int(stats.rebound, 1);
    ecs_progress(world, 0.016f);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, ground);
    test_uint(status->target, 0);
    test_assert(!ecs_has(world, mover, Hovered));

    ecs_fini(world);
}

/** An Interactive shape under an ancestor with SpatialIgnore is not picked and does not block the
 * pick of the shape behind it, as the rule has no exception; without the tag it is picked and
 * blocks again.
 */
void SpatialIgnore_interactive_under_ignore_not_picked(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 20});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {0, 0, -5});
    ecs_set(world, back, Box, {100, 100, 1});
    ecs_add(world, back, Interactive);

    ecs_entity_t actor = ecs_new(world);
    ecs_add(world, actor, SpatialIgnore);
    ecs_set(world, actor, Position, {0, 0, 0});
    ecs_entity_t front = ecs_new_w_pair(world, EcsChildOf, actor);
    ecs_set(world, front, Position, {0, 0, 0});
    ecs_set(world, front, Box, {2, 2, 2});
    ecs_add(world, front, Interactive);

    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, back);
    test_uint(status->target, back);
    test_assert(!ecs_has(world, front, Hovered));

    ecs_remove(world, actor, SpatialIgnore);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, front);
    test_uint(status->target, front);
    test_assert(ecs_has(world, front, Hovered));

    ecs_fini(world);
}

/** SpatialIgnore on a parent takes its static and dynamic children out in the next update, keeps
 * new children out and makes moves free; removing it puts every child back at its current
 * place, and the tag on a shape itself works the same.
 */
void SpatialIgnore_tag_added_and_removed(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t still = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, still, Position, {0, 0, 0});
    ecs_set(world, still, Box, {1, 1, 1});
    ecs_entity_t moving = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, moving, DynamicTransform);
    ecs_set(world, moving, Position, {3, 0, 0});
    ecs_set(world, moving, Box, {1, 1, 1});
    ecs_progress(world, 0);

    vec3 min, max;
    box_spatial_stats_t stats;
    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, -5}, .dir = {0, 0, 1} };
    test_assert(box_spatial_bounds(world, still, NULL, min, max));
    test_assert(box_spatial_bounds(world, moving, NULL, min, max));
    box_spatial_stats(world, &stats);
    test_int(stats.movers, 1);
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, still);

    ecs_add(world, parent, SpatialIgnore);
    ecs_progress(world, 0);
    test_assert(!box_spatial_bounds(world, still, NULL, min, max));
    test_assert(!box_spatial_bounds(world, moving, NULL, min, max));
    box_spatial_stats(world, &stats);
    test_int(stats.movers, 0);
    test_assert(!box_spatial_ray(world, &ray, &hit));
    box_spatial_stats(world, &stats);
    test_int(stats.entities, 0);

    ecs_entity_t late = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, late, Position, {-3, 0, 0});
    ecs_set(world, late, Box, {1, 1, 1});
    ecs_progress(world, 0);
    test_assert(!box_spatial_bounds(world, late, NULL, min, max));

    ecs_set(world, moving, Position, {4, 0, 0});
    ecs_progress(world, 0);
    box_spatial_stats(world, &stats);
    test_int(stats.rebound, 0);
    test_int(stats.entities, 0);

    ecs_remove(world, parent, SpatialIgnore);
    ecs_progress(world, 0);
    test_assert(box_spatial_bounds(world, still, NULL, min, max));
    test_assert(box_spatial_bounds(world, late, NULL, min, max));
    test_assert(box_spatial_bounds(world, moving, NULL, min, max));
    test_flt(min[0], 3.5f);
    box_spatial_stats(world, &stats);
    test_int(stats.movers, 1);
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, still);
    box_spatial_stats(world, &stats);
    test_int(stats.entities, 3);

    ecs_add(world, still, SpatialIgnore);
    ecs_progress(world, 0);
    test_assert(!box_spatial_bounds(world, still, NULL, min, max));
    test_assert(box_spatial_bounds(world, late, NULL, min, max));

    ecs_fini(world);
}

/** Without SpatialIgnore the moving parts of a wave are indexed, each critter one mover whose
 * leaf is placed every frame the index is queried.
 */
void SpatialIgnore_untagged_wave_still_refit(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    enum { COUNT = 50 };
    ecs_entity_t critter = ignore_critter(world, false);
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        ignore_spawn(world, critter, (float)i * 1.5f, 0);
    }

    ecs_progress(world, 0.016f);
    box_spatial_stats_t stats;
    box_spatial_stats(world, &stats);
    test_int(stats.entities, COUNT * 4);

    vec3 min, max;
    for (i = 0; i < 3; i ++) {
        box_spatial_static_bounds(world, 0, min, max);
        ecs_progress(world, 0.016f);
        box_spatial_stats(world, &stats);
        test_int(stats.rebound, COUNT);
        test_int(stats.movers, COUNT);
    }

    ecs_fini(world);
}
