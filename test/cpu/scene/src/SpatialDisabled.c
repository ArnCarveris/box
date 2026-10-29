/**
 * @file SpatialDisabled.c
 * @brief Spatial index: queries skip disabled entities until they are enabled again, and
 * deleting one or removing its shape while disabled leaves nothing behind.
 */

#include <box.h>
#include <bake_test.h>
#include "spatial.h"

void SpatialDisabled_delete_disabled_entity(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, DynamicTransform);
    ecs_set(world, b, Position, {3, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_enable(world, a, false);
    ecs_enable(world, b, false);
    ecs_progress(world, 0);
    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 0);
    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {-10, 0, 0}, .dir = {1, 0, 0} };
    test_assert(!box_spatial_ray(world, &ray, &hit));

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void SpatialDisabled_remove_shape_disabled(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_enable(world, a, false);
    ecs_remove(world, a, Box);
    ecs_progress(world, 0);
    ecs_enable(world, a, true);
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(!box_spatial_bounds(world, a, NULL, min, max));
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 0);
    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {-10, 0, 0}, .dir = {1, 0, 0} };
    test_assert(!box_spatial_ray(world, &ray, &hit));

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void SpatialDisabled_disabled_skipped(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, DynamicTransform);
    ecs_set(world, b, Position, {3, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 2);
    ecs_vec_clear(&out);

    /* Queries skip a disabled entity right away, its bounds stay indexed */
    ecs_enable(world, a, false);
    ecs_enable(world, b, false);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, a, NULL, min, max));
    test_assert(box_spatial_bounds(world, b, NULL, min, max));
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 10, &out), 0);
    test_assert(!box_spatial_static_bounds(world, 0, min, max));
    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {-10, 0, 0}, .dir = {1, 0, 0} };
    test_assert(!box_spatial_ray(world, &ray, &hit));
    ecs_progress(world, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 0);
    test_assert(!box_spatial_ray(world, &ray, &hit));

    /* And see it again as soon as it is enabled */
    ecs_enable(world, a, true);
    ecs_enable(world, b, true);
    test_assert(box_spatial_bounds(world, b, NULL, min, max));
    test_flt(min[0], 2.5f);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 2);
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, a);
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 2);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}
