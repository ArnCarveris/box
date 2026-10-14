/**
 * @file Spatial.c
 * @brief Spatial index: insert, move, remove, scene scoping, ray and overlap queries.
 *
 * A Box shape of size s at position p has the world bounds p - s/2 .. p + s/2, so most cases
 * compare query results with bounds worked out by hand or by a brute force pass.
 */

#include <box.h>
#include <bake_test.h>
#include <float.h>
#include "spatial.h"

/** Order entity ids ascending.
 *
 * @param a The first id
 * @param b The second id
 * @return Negative when a is smaller, positive when b is smaller
 */
static int entity_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t ea = *(const ecs_entity_t*)a;
    ecs_entity_t eb = *(const ecs_entity_t*)b;

    return (ea > eb) - (ea < eb);
}

/** Reject the entity passed as context.
 *
 * @param ctx Pointer to the rejected entity
 * @param e The entity
 * @param distance Distance of the hit
 * @return false for the rejected entity
 */
static bool reject_entity(
    void *ctx,
    ecs_entity_t e,
    float *distance)
{
    (void)distance;

    return e != *(ecs_entity_t*)ctx;
}

/** Create a box at x = 5 the first time the system runs.
 *
 * @param it The iterator of the system, its context receives the box
 */
static void LateCreate(
    ecs_iter_t *it)
{
    ecs_entity_t *e = it->ctx;
    if (!*e) {
        *e = ecs_new(it->world);
        ecs_set(it->world, *e, Position, {5, 0, 0});
        ecs_set(it->world, *e, Box, {1, 1, 1});
    }
}

/** Move the entity in the context to x = 5 the first time the system runs.
 *
 * @param it The iterator of the system
 */
static void LateMove(
    ecs_iter_t *it)
{
    ecs_entity_t *e = it->ctx;
    if (*e) {
        ecs_set(it->world, *e, Position, {5, 0, 0});
        *e = 0;
    }
}

void Spatial_insert(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Box, {2, 4, 6});
    ecs_progress(world, 0);

    vec3 min, max;
    ecs_entity_t scene = 1;
    test_assert(box_spatial_bounds(world, e, &scene, min, max));
    test_uint(scene, 0);
    test_flt(min[0], 0);
    test_flt(min[1], 0);
    test_flt(min[2], 0);
    test_flt(max[0], 2);
    test_flt(max[1], 4);
    test_flt(max[2], 6);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {1, 2, -10}, .dir = {0, 0, 2} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 10);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){1.5f, 3.5f, 5.5f}, (vec3){3, 5, 7}, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){2.5f, 0, 0}, (vec3){3, 1, 1}, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){1, 2, 3}, 0.1f, &out), 1);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_insert_rotated_scaled(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {10, 0, 0});
    ecs_set(world, e, Rotation, {0, GLM_PI_2f, 0});
    ecs_set(world, e, Scale, {1, 2, 1});
    ecs_set(world, e, Box, {2, 1, 6});
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(box_spatial_bounds(world, e, NULL, min, max));
    test_assert(fabsf(min[0] - 7) < 1e-4f);
    test_assert(fabsf(max[0] - 13) < 1e-4f);
    test_assert(fabsf(min[1] + 1) < 1e-4f);
    test_assert(fabsf(max[1] - 1) < 1e-4f);
    test_assert(fabsf(min[2] + 1) < 1e-4f);
    test_assert(fabsf(max[2] - 1) < 1e-4f);

    ecs_fini(world);
}

void Spatial_insert_child(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 5, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_set(world, child, Sphere, {.segments = 24, .radius = 2});
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(!box_spatial_bounds(world, parent, NULL, min, max));
    test_assert(box_spatial_bounds(world, child, NULL, min, max));
    test_assert(fabsf(min[0] + 1) < 1e-4f);
    test_assert(fabsf(max[0] - 3) < 1e-4f);
    test_assert(fabsf(min[1] - 3) < 1e-4f);
    test_assert(fabsf(max[1] - 7) < 1e-4f);

    ecs_fini(world);
}

void Spatial_move_static(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 1);

    ecs_set(world, e, Position, {100, 0, 0});
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){100, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 0}, .dir = {1, 0, 0} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_flt(hit.distance, 99.5f);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_move_parent_moves_child(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 1, 0});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_entity_t grandchild = ecs_new_w_parent(world, child, NULL);
    ecs_set(world, grandchild, Position, {0, 1, 0});
    ecs_set(world, grandchild, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_set(world, parent, Position, {20, 0, 0});
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(box_spatial_bounds(world, child, NULL, min, max));
    test_flt(min[0], 19.5f);
    test_flt(min[1], 0.5f);
    test_assert(box_spatial_bounds(world, grandchild, NULL, min, max));
    test_flt(min[0], 19.5f);
    test_flt(min[1], 1.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, -1, -1}, (vec3){1, 3, 1}, &out), 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){19, -1, -1}, (vec3){21, 3, 1}, &out), 2);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_move_dynamic(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);

    int32_t i;
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    for (i = 1; i <= 50; i ++) {
        float x = (float)(i * i) * 0.1f;
        ecs_set(world, e, Position, {x, 0, 0});
        ecs_progress(world, 0);
        vec3 min, max;
        test_assert(box_spatial_bounds(world, e, NULL, min, max));
        test_assert(fabsf(min[0] - (x - 0.5f)) < 1e-3f);
        ecs_vec_clear(&out);
        test_int(box_spatial_overlap_sphere(world, 0, (vec3){x, 0, 0}, 0.1f, &out), 1);
        ecs_vec_clear(&out);
        test_int(box_spatial_overlap_sphere(world, 0, (vec3){x - 1.0f, 0, 0}, 0.1f, &out), 0);
        box_spatial_hit_t hit = {0};
        box_spatial_ray_t ray = { .origin = {x, 0, -5}, .dir = {0, 0, 1} };
        test_assert(box_spatial_ray(world, &ray, &hit));
        test_flt(hit.distance, 4.5f);
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_static_to_dynamic(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_add(world, e, DynamicTransform);
    ecs_progress(world, 0);
    ecs_set(world, e, Position, {5, 0, 0});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);

    ecs_remove(world, e, DynamicTransform);
    ecs_set(world, e, Position, {-5, 0, 0});
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){-5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_remove_shape(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {3, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_remove(world, a, Box);
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(!box_spatial_bounds(world, a, NULL, min, max));
    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {-10, 0, 0}, .dir = {1, 0, 0} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, b);

    ecs_fini(world);
}

void Spatial_delete_entity(void) {
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

    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 0);
    ecs_progress(world, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-10, -10, -10}, (vec3){10, 10, 10}, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_delete_parent(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Box, {1, 1, 1});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, child, Position, {(float)i, 2, 0});
        ecs_set(world, child, Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-20, -20, -20}, (vec3){20, 20, 20},
        &out), 11);

    ecs_delete(world, parent);
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-20, -20, -20}, (vec3){20, 20, 20}, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_prefab_not_indexed(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Position, {0, 0, 0});
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 1, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_reparent_across_scenes(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_add(world, d, DynamicTransform);
    ecs_set(world, d, Position, {2, 0, 0});
    ecs_set(world, d, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_entity_t scene = 0;
    vec3 min, max;
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_assert(box_spatial_bounds(world, e, &scene, min, max));
    test_uint(scene, a);
    test_int(box_spatial_overlap_sphere(world, a, (vec3){1, 0, 0}, 2, &out), 2);
    test_int(box_spatial_overlap_sphere(world, b, (vec3){1, 0, 0}, 2, &out), 0);

    ecs_add_pair(world, group, EcsChildOf, b);
    ecs_progress(world, 0);
    test_assert(box_spatial_bounds(world, e, &scene, min, max));
    test_uint(scene, b);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, a, (vec3){1, 0, 0}, 2, &out), 0);
    test_int(box_spatial_overlap_sphere(world, b, (vec3){1, 0, 0}, 2, &out), 2);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){1, 0, 0}, 2, &out), 0);

    ecs_remove_pair(world, group, EcsChildOf, b);
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, b, (vec3){1, 0, 0}, 2, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){1, 0, 0}, 2, &out), 2);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_reparent_parent_storage_across_scenes(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t group = ecs_new_w_parent(world, a, NULL);
    ecs_entity_t e = ecs_new_w_parent(world, group, NULL);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .scene = a, .origin = {0, 10, 0}, .dir = {0, -1, 0} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);

    ecs_set(world, group, EcsParent, {b});
    ecs_progress(world, 0);
    test_assert(!box_spatial_ray(world, &ray, &hit));
    ray.scene = b;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);

    ecs_fini(world);
}

void Spatial_scene_added_and_removed(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t group = ecs_new(world);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 1, &out), 1);

    ecs_set(world, group, Scene, {0});
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 1, &out), 0);
    test_int(box_spatial_overlap_sphere(world, group, (vec3){0, 0, 0}, 1, &out), 1);

    ecs_remove(world, group, Scene);
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, group, (vec3){0, 0, 0}, 1, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 1, &out), 1);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_nested_scene(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, Scene, {0});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, inner, Scene, {0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .scene = outer, .origin = {0, 0, -10}, .dir = {0, 0, 1} };
    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    test_int(box_spatial_ray_all(world, &ray, &hits), 1);
    test_uint(ecs_vec_get_t(&hits, box_spatial_hit_t, 0)->entity, a);
    ray.scene = inner;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, b);
    ray.scene = 0;
    test_assert(!box_spatial_ray(world, &ray, &hit));

    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);
    ecs_fini(world);
}

void Spatial_scene_deleted(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_delete(world, a);
    ecs_progress(world, 0);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, a, (vec3){0, 0, 0}, 1, &out), 0);

    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t f = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, f, Position, {0, 0, 0});
    ecs_set(world, f, Box, {1, 1, 1});
    ecs_progress(world, 0);
    test_int(box_spatial_overlap_sphere(world, b, (vec3){0, 0, 0}, 1, &out), 1);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_scene_scoping(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scenes[3] = { 0, ecs_new(world), ecs_new(world) };
    ecs_entity_t shapes[3];
    int32_t i, j;
    ecs_set(world, scenes[1], Scene, {0});
    ecs_set(world, scenes[2], Scene, {0});
    for (i = 0; i < 3; i ++) {
        shapes[i] = scenes[i] ? ecs_new_w_pair(world, EcsChildOf, scenes[i]) : ecs_new(world);
        if (i == 2) {
            ecs_add(world, shapes[i], DynamicTransform);
        }

        ecs_set(world, shapes[i], Position, {0, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_vec_t out, hits;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    for (i = 0; i < 3; i ++) {
        box_spatial_hit_t hit = {0};
        box_spatial_ray_t ray = { .scene = scenes[i], .origin = {0, 0, -10}, .dir = {0, 0, 1} };
        test_assert(box_spatial_ray(world, &ray, &hit));
        test_uint(hit.entity, shapes[i]);
        ecs_vec_clear(&hits);
        test_int(box_spatial_ray_all(world, &ray, &hits), 1);
        test_uint(ecs_vec_get_t(&hits, box_spatial_hit_t, 0)->entity, shapes[i]);
        ecs_vec_clear(&out);
        test_int(box_spatial_overlap_box(world, scenes[i], (vec3){-1, -1, -1}, (vec3){1, 1, 1},
            &out), 1);
        test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), shapes[i]);
        ecs_vec_clear(&out);
        test_int(box_spatial_overlap_sphere(world, scenes[i], (vec3){0, 0, 0}, 1, &out), 1);
        test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), shapes[i]);
        for (j = 0; j < 3; j ++) {
            ecs_entity_t scene = 0;
            vec3 min, max;
            test_assert(box_spatial_bounds(world, shapes[j], &scene, min, max));
            test_assert((scene == scenes[i]) == (i == j));
        }
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);
    ecs_fini(world);
}

void Spatial_ray_nearest(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 0, 20});
    ecs_set(world, far, Box, {4, 4, 4});
    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {0, 0, 10});
    ecs_set(world, near, Box, {2, 2, 2});
    ecs_entity_t side = ecs_new(world);
    ecs_set(world, side, Position, {5, 0, 5});
    ecs_set(world, side, Box, {1, 1, 1});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 0}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, near);
    test_flt(hit.distance, 9);

    ray.origin[0] = 1.5f;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, far);
    test_flt(hit.distance, 18);

    ray.dir[2] = -1;
    test_assert(!box_spatial_ray(world, &ray, &hit));

    ecs_fini(world);
}

void Spatial_ray_max_distance(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {10, 0, 0});
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 0}, .dir = {1, 0, 0}, .max_distance = 8.5f };
    test_assert(!box_spatial_ray(world, &ray, &hit));
    ray.max_distance = 9.5f;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);

    ecs_fini(world);
}

void Spatial_ray_from_inside(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0.5f, 0, 0}, .dir = {0, 1, 0} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 0);

    ecs_fini(world);
}

void Spatial_ray_all_sorted(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t shapes[5];
    int32_t order[5] = { 3, 0, 4, 1, 2 };
    int32_t expect[5] = { 1, 3, 4, 0, 2 };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        shapes[i] = ecs_new(world);
        if (i % 2) {
            ecs_add(world, shapes[i], DynamicTransform);
        }

        ecs_set(world, shapes[i], Position, {0, (float)order[i] * 3, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_entity_t off = ecs_new(world);
    ecs_set(world, off, Position, {3, 0, 0});
    ecs_set(world, off, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    box_spatial_ray_t ray = { .origin = {0, -10, 0}, .dir = {0, 1, 0} };
    test_int(box_spatial_ray_all(world, &ray, &hits), 5);
    for (i = 0; i < 5; i ++) {
        box_spatial_hit_t *hit = ecs_vec_get_t(&hits, box_spatial_hit_t, i);
        test_uint(hit->entity, shapes[expect[i]]);
        test_flt(hit->distance, 9.5f + (float)i * 3);
    }

    ecs_vec_clear(&hits);
    ray.max_distance = 13;
    test_int(box_spatial_ray_all(world, &ray, &hits), 2);

    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);
    ecs_fini(world);
}

void Spatial_ray_filter(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {0, 0, 5});
    ecs_set(world, near, Box, {1, 1, 1});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 0, 10});
    ecs_set(world, far, Box, {1, 1, 1});
    ecs_progress(world, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = {
        .origin = {0, 0, 0}, .dir = {0, 0, 1}, .filter = reject_entity, .ctx = &near
    };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, far);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    test_int(box_spatial_ray_all(world, &ray, &hits), 1);

    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);
    ecs_fini(world);
}

void Spatial_overlap_box_and_sphere(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t shapes[9];
    int32_t i;
    for (i = 0; i < 9; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)(i % 3) * 4, (float)(i / 3) * 4, 0});
        ecs_set(world, shapes[i], Box, {2, 2, 2});
    }

    ecs_progress(world, 0);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){0.5f, 0.5f, -1}, (vec3){3.5f, 3.5f, 1},
        &out), 4);
    qsort(ecs_vec_first(&out), 4, sizeof(ecs_entity_t), entity_compare);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), shapes[0]);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 1), shapes[1]);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 2), shapes[3]);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 3), shapes[4]);

    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){4, 4, 0}, 2.9f, &out), 1);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){4, 4, 0}, 3.1f, &out), 5);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){2, 2, 0}, 1.4f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){2, 2, 0}, 1.5f, &out), 4);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_random_vs_brute_force(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    enum { COUNT = 3000, QUERIES = 300 };
    ecs_entity_t scenes[3] = { 0, ecs_new(world), ecs_new(world) };
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, COUNT);
    int32_t *scene_of = ecs_os_malloc_n(int32_t, COUNT);
    vec3 *lo = ecs_os_malloc_n(vec3, COUNT);
    vec3 *hi = ecs_os_malloc_n(vec3, COUNT);
    ecs_entity_t *expect = ecs_os_malloc_n(ecs_entity_t, COUNT);
    int32_t i, q, a;
    ecs_set(world, scenes[1], Scene, {0});
    ecs_set(world, scenes[2], Scene, {0});
    srand(1234);
    for (i = 0; i < COUNT; i ++) {
        float p[3], s[3];
        for (a = 0; a < 3; a ++) {
            p[a] = (float)rand() / (float)RAND_MAX * 100.0f - 50.0f;
            s[a] = 0.2f + (float)rand() / (float)RAND_MAX * 4.0f;
            lo[i][a] = p[a] - s[a] * 0.5f;
            hi[i][a] = p[a] + s[a] * 0.5f;
        }

        scene_of[i] = rand() % 3;
        shapes[i] = scenes[scene_of[i]] ?
            ecs_new_w_pair(world, EcsChildOf, scenes[scene_of[i]]) : ecs_new(world);
        if (i % 5 == 0) {
            ecs_add(world, shapes[i], DynamicTransform);
        }

        ecs_set(world, shapes[i], Position, {p[0], p[1], p[2]});
        ecs_set(world, shapes[i], Box, {s[0], s[1], s[2]});
    }

    ecs_progress(world, 0);
    ecs_vec_t out, hits;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    for (q = 0; q < QUERIES; q ++) {
        int32_t sc = q % 3, count = 0, nearest = -1;
        float c[3], h[3], d[3], len = 0, best = FLT_MAX, radius;
        for (a = 0; a < 3; a ++) {
            c[a] = (float)rand() / (float)RAND_MAX * 120.0f - 60.0f;
            h[a] = 0.5f + (float)rand() / (float)RAND_MAX * 10.0f;
            d[a] = (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
            len += d[a] * d[a];
        }

        len = sqrtf(len);
        for (a = 0; a < 3; a ++) {
            d[a] /= len;
        }

        radius = h[0];
        for (i = 0; i < COUNT; i ++) {
            bool hit = scene_of[i] == sc;
            for (a = 0; a < 3; a ++) {
                hit = hit && lo[i][a] <= c[a] + h[a] && hi[i][a] >= c[a] - h[a];
            }

            if (hit) {
                expect[count ++] = shapes[i];
            }
        }

        ecs_vec_clear(&out);
        test_int(box_spatial_overlap_box(world, scenes[sc], (vec3){c[0] - h[0], c[1] - h[1],
            c[2] - h[2]}, (vec3){c[0] + h[0], c[1] + h[1], c[2] + h[2]}, &out), count);
        qsort(expect, (size_t)count, sizeof(ecs_entity_t), entity_compare);
        qsort(ecs_vec_first(&out), (size_t)count, sizeof(ecs_entity_t), entity_compare);
        for (i = 0; i < count; i ++) {
            test_uint(*ecs_vec_get_t(&out, ecs_entity_t, i), expect[i]);
        }

        count = 0;
        for (i = 0; i < COUNT; i ++) {
            float dist = 0;
            for (a = 0; a < 3; a ++) {
                float v = c[a] < lo[i][a] ? lo[i][a] - c[a] : c[a] > hi[i][a] ? c[a] - hi[i][a] : 0;
                dist += v * v;
            }

            if (scene_of[i] == sc && dist <= radius * radius) {
                expect[count ++] = shapes[i];
            }
        }

        ecs_vec_clear(&out);
        test_int(box_spatial_overlap_sphere(world, scenes[sc], c, radius, &out), count);
        qsort(expect, (size_t)count, sizeof(ecs_entity_t), entity_compare);
        qsort(ecs_vec_first(&out), (size_t)count, sizeof(ecs_entity_t), entity_compare);
        for (i = 0; i < count; i ++) {
            test_uint(*ecs_vec_get_t(&out, ecs_entity_t, i), expect[i]);
        }

        count = 0;
        for (i = 0; i < COUNT; i ++) {
            float t0 = 0, t1 = FLT_MAX;
            bool hit = scene_of[i] == sc;
            for (a = 0; a < 3 && hit; a ++) {
                if (d[a] == 0) {
                    hit = c[a] >= lo[i][a] && c[a] <= hi[i][a];
                } else {
                    float ta = (lo[i][a] - c[a]) / d[a], tb = (hi[i][a] - c[a]) / d[a];
                    t0 = glm_max(t0, glm_min(ta, tb));
                    t1 = glm_min(t1, glm_max(ta, tb));
                    hit = t0 <= t1;
                }
            }

            if (hit) {
                count ++;
                if (t0 < best) {
                    best = t0;
                    nearest = i;
                }
            }
        }

        box_spatial_hit_t result = {0};
        box_spatial_ray_t ray = { .scene = scenes[sc], .origin = {c[0], c[1], c[2]},
            .dir = {d[0], d[1], d[2]} };
        test_bool(box_spatial_ray(world, &ray, &result), nearest >= 0);
        if (nearest >= 0) {
            test_uint(result.entity, shapes[nearest]);
            test_assert(fabsf(result.distance - best) < 1e-3f);
        }

        ecs_vec_clear(&hits);
        test_int(box_spatial_ray_all(world, &ray, &hits), count);
        for (i = 1; i < count; i ++) {
            test_assert(ecs_vec_get_t(&hits, box_spatial_hit_t, i - 1)->distance <=
                ecs_vec_get_t(&hits, box_spatial_hit_t, i)->distance);
        }
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);
    ecs_os_free(shapes);
    ecs_os_free(scene_of);
    ecs_os_free(lo);
    ecs_os_free(hi);
    ecs_os_free(expect);
    ecs_fini(world);
}

void Spatial_random_moves_vs_brute_force(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    enum { COUNT = 2000, FRAMES = 12, QUERIES = 60 };
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, COUNT);
    vec3 *pos = ecs_os_malloc_n(vec3, COUNT);
    ecs_entity_t *expect = ecs_os_malloc_n(ecs_entity_t, COUNT);
    int32_t i, f, q, a;
    srand(4321);
    for (i = 0; i < COUNT; i ++) {
        for (a = 0; a < 3; a ++) {
            pos[i][a] = (float)rand() / (float)RAND_MAX * 100.0f - 50.0f;
        }

        shapes[i] = ecs_new(world);
        if (i % 4 == 0) {
            ecs_add(world, shapes[i], DynamicTransform);
        }

        ecs_set(world, shapes[i], Position, {pos[i][0], pos[i][1], pos[i][2]});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    for (f = 0; f < FRAMES; f ++) {
        for (i = 0; i < COUNT; i ++) {
            bool dynamic = i % 4 == 0;
            int32_t r = rand() % 100;
            if (!dynamic && r >= 10) {
                continue;
            }

            if (r < 2) {
                ecs_delete(world, shapes[i]);
                shapes[i] = ecs_new(world);
                if (dynamic) {
                    ecs_add(world, shapes[i], DynamicTransform);
                }

                ecs_set(world, shapes[i], Box, {1, 1, 1});
            }

            for (a = 0; a < 3; a ++) {
                pos[i][a] = r < 5 ? (float)rand() / (float)RAND_MAX * 100.0f - 50.0f :
                    pos[i][a] + (float)rand() / (float)RAND_MAX * 0.6f - 0.3f;
            }

            ecs_set(world, shapes[i], Position, {pos[i][0], pos[i][1], pos[i][2]});
        }

        ecs_progress(world, 0);
        for (q = 0; q < QUERIES; q ++) {
            int32_t count = 0;
            float c[3], h = 1.0f + (float)rand() / (float)RAND_MAX * 8.0f;
            for (a = 0; a < 3; a ++) {
                c[a] = (float)rand() / (float)RAND_MAX * 100.0f - 50.0f;
            }

            for (i = 0; i < COUNT; i ++) {
                bool hit = true;
                for (a = 0; a < 3; a ++) {
                    hit = hit && fabsf(pos[i][a] - c[a]) <= h + 0.5f;
                }

                if (hit) {
                    expect[count ++] = shapes[i];
                }
            }

            ecs_vec_clear(&out);
            test_int(box_spatial_overlap_box(world, 0, (vec3){c[0] - h, c[1] - h, c[2] - h},
                (vec3){c[0] + h, c[1] + h, c[2] + h}, &out), count);
            qsort(expect, (size_t)count, sizeof(ecs_entity_t), entity_compare);
            qsort(ecs_vec_first(&out), (size_t)count, sizeof(ecs_entity_t), entity_compare);
            for (i = 0; i < count; i ++) {
                test_uint(*ecs_vec_get_t(&out, ecs_entity_t, i), expect[i]);
            }
        }
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_os_free(shapes);
    ecs_os_free(pos);
    ecs_os_free(expect);
    ecs_fini(world);
}

void Spatial_rebuild_keeps_results(void) {
    // scenes/spatial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 10) * 2, (float)(i / 10) * 2, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    for (i = 0; i < 10; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 2, 30, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, -1, -1}, (vec3){100, 100, 1}, &out), 110);
    box_spatial_rebuild(world);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, -1, -1}, (vec3){100, 100, 1}, &out), 110);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, 29, -1}, (vec3){5, 31, 1}, &out), 3);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_dynamic_unnotified_move_after_idle(void) {
    // scenes/spatial_idle_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Box, {1, 1, 1});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 5, 0});
    ecs_set(world, child, Box, {1, 1, 1});
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 5, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), child);
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    ecs_get_mut(world, parent, Position)->x = 10;
    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 5, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){10, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), parent);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){10, 5, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), child);
    vec3 min, max;
    test_assert(box_spatial_bounds(world, child, NULL, min, max));
    test_flt(min[0], 9.5f);
    test_flt(max[1], 5.5f);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_static_moved_over_frames_then_queried(void) {
    // scenes/spatial_lazy_test
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {0, 10, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    int32_t i;
    for (i = 1; i <= 4; i ++) {
        ecs_set(world, a, Position, {10.0f * i, 0, 0});
        ecs_set(world, b, Position, {0, 10, 10.0f * i});
        ecs_progress(world, 0);
    }

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){10, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 10, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){40, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), a);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(
        world, 0, (vec3){-1, 9, 39}, (vec3){1, 11, 41}, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), b);

    vec3 min, max;
    test_assert(box_spatial_bounds(world, a, NULL, min, max));
    test_flt(min[0], 39.5f);
    test_flt(max[0], 40.5f);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 0}, .dir = {1, 0, 0} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, a);
    test_flt(hit.distance, 39.5f);

    ray.origin[1] = 10;
    ray.origin[2] = 0;
    ray.dir[0] = 0;
    ray.dir[2] = 1;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, b);
    test_flt(hit.distance, 39.5f);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_static_moved_then_removed_then_queried(void) {
    // scenes/spatial_lazy_test
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {0, 10, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Position, {0, 20, 0});
    ecs_set(world, c, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_set(world, a, Position, {10, 0, 0});
    ecs_set(world, b, Position, {10, 10, 0});
    ecs_set(world, c, Position, {10, 20, 0});
    ecs_progress(world, 0);
    ecs_set(world, a, Position, {20, 0, 0});
    ecs_set(world, b, Position, {20, 10, 0});
    ecs_set(world, c, Position, {20, 20, 0});
    ecs_progress(world, 0);
    ecs_remove(world, a, Box);
    ecs_delete(world, b);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_box(
        world, 0, (vec3){-100, -100, -100}, (vec3){100, 100, 100}, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), c);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){20, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){20, 10, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){20, 20, 0}, 0.1f, &out), 1);

    vec3 min, max;
    test_assert(!box_spatial_bounds(world, a, NULL, min, max));

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 0}, .dir = {1, 0, 0} };
    test_assert(!box_spatial_ray(world, &ray, &hit));
    ray.origin[1] = 20;
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, c);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_static_moved_back_then_queried(void) {
    // scenes/spatial_lazy_test
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {5, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {-5, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_set(world, a, Position, {50, 0, 0});
    ecs_progress(world, 0);
    ecs_set(world, a, Position, {80, 0, 0});
    ecs_progress(world, 0);
    ecs_set(world, a, Position, {5, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), a);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){50, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){80, 0, 0}, 0.1f, &out), 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){-5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), b);

    vec3 min, max;
    test_assert(box_spatial_bounds(world, a, NULL, min, max));
    test_flt(min[0], 4.5f);
    test_flt(max[0], 5.5f);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {0, 0, 0}, .dir = {1, 0, 0} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, a);
    test_flt(hit.distance, 4.5f);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_created_in_post_update(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = 0;
    ecs_system(world, {
        .phase = EcsPostUpdate,
        .callback = LateCreate,
        .ctx = &e,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(e != 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 0, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, -1, -1}, (vec3){1, 1, 1}, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_created_in_pre_store(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = 0;
    ecs_system(world, {
        .phase = EcsPreStore,
        .callback = LateCreate,
        .ctx = &e,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(e != 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 0, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, -1, -1}, (vec3){1, 1, 1}, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_created_in_on_store(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = 0;
    ecs_system(world, {
        .phase = EcsOnStore,
        .callback = LateCreate,
        .ctx = &e,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(e != 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 0, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_box(world, 0, (vec3){-1, -1, -1}, (vec3){1, 1, 1}, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_moved_in_post_update(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    ecs_entity_t moving = e;
    ecs_system(world, {
        .phase = EcsPostUpdate,
        .callback = LateMove,
        .ctx = &moving,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(moving, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 0, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_moved_in_pre_store(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    ecs_entity_t moving = e;
    ecs_system(world, {
        .phase = EcsPreStore,
        .callback = LateMove,
        .ctx = &moving,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(moving, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 0, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_moved_in_on_store(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_progress(world, 0);
    ecs_entity_t moving = e;
    ecs_system(world, {
        .phase = EcsOnStore,
        .callback = LateMove,
        .ctx = &moving,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(moving, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 0, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, e);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){5, 0, 0}, 0.1f, &out), 1);
    test_uint(*ecs_vec_get_t(&out, ecs_entity_t, 0), e);
    ecs_vec_clear(&out);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 0, 0}, 0.1f, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Spatial_parent_moved_in_pre_store(void) {
    // scenes/spatial_late
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 2, 0});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_progress(world, 0);
    ecs_entity_t moving = parent;
    ecs_system(world, {
        .phase = EcsPreStore,
        .callback = LateMove,
        .ctx = &moving,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(moving, 0);

    box_spatial_hit_t hit = {0};
    box_spatial_ray_t ray = { .origin = {5, 2, -10}, .dir = {0, 0, 1} };
    test_assert(box_spatial_ray(world, &ray, &hit));
    test_uint(hit.entity, child);
    test_flt(hit.distance, 9.5f);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_spatial_overlap_sphere(world, 0, (vec3){0, 2, 0}, 0.1f, &out), 0);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}
