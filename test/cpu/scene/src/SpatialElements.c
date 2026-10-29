/**
 * @file SpatialElements.c
 * @brief Spatial index: UI elements without a shape, seen only by the ray casts that ask.
 */

#include <box.h>
#include <bake_test.h>
#include "spatial.h"

void SpatialElements_hidden_from_shape_queries(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 5});
    ecs_set(world, panel, LayoutBounds, {4, 2});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-3, -3, -3}, (vec3){3, 3, 6}, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), shape);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 5}, 1, &out), 0);

    vec3 min, max;
    test_assert(box_spatial_static_bounds(world, 0, min, max));
    test_flt(max[2], 0.5f);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {1, 0.5f, 10}, .dir = {0, 0, -1} };
    test_assert(!box_spatial_ray(world, &ray, &hit));
    ray.ui = true;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, panel);
    test_flt(hit.distance, 5);

    ray.origin[0] = 0;
    ray.ui = false;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, shape);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void SpatialElements_follow_bounds_and_moves(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Position, {0, 0, 0});
    ecs_set(world, label, TextBounds, {2, 1});
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(box_spatial_bounds(world, label, NULL, min, max));
    test_flt(min[0], -1);
    test_flt(max[1], 0.5f);

    ecs_set(world, label, TextBounds, {6, 1});
    ecs_set(world, label, Position, {10, 0, 0});
    ecs_progress(world, 0);
    test_assert(box_spatial_bounds(world, label, NULL, min, max));
    test_flt(min[0], 7);
    test_flt(max[0], 13);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {12.5f, 0, 10}, .dir = {0, 0, -1}, .ui = true };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, label);

    ecs_remove(world, label, TextBounds);
    ecs_progress(world, 0);
    test_assert(!box_spatial_bounds(world, label, NULL, min, max));
    test_assert(!box_spatial_ray(world, &ray, &hit));

    ecs_fini(world);
}

void SpatialElements_delete_disabled_element(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, LayoutBounds, {4, 2});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 10}, .dir = {0, 0, -1}, .ui = true };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, panel);

    ecs_enable(world, panel, false);
    ecs_progress(world, 0);
    ecs_delete(world, panel);
    ecs_progress(world, 0);

    test_assert(!box_spatial_ray(world, &ray, &hit));

    ecs_fini(world);
}
