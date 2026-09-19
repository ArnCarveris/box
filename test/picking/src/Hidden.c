/**
 * @file Hidden.c
 * @brief Tests that a hidden subtree leaves the spatial index, the pick and the queries.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/picking/picking_internal.h"

static ecs_world_t *w;

void Hidden_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxPicking);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Hidden_teardown(void)
{
    ecs_fini(w);
}

void Hidden_an_entity_is_not_picked(void)
{
    // ore_engine/visibility
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);

    ecs_add_id(w, ball, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), false);

    ecs_remove_id(w, ball, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, ball);
}

void Hidden_a_subtree_is_not_picked(void)
{
    // ore_engine/visibility
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Position, {0, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, child, Position, {0, 0, 0});
    ecs_set(w, child, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.collider, child);

    ecs_add_id(w, parent, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), false);

    ecs_remove_id(w, parent, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.collider, child);
}

void Hidden_the_nearest_visible_object_is_picked(void)
{
    // ore_engine/visibility
    ecs_entity_t near_ball = ecs_new(w);
    ecs_set(w, near_ball, Position, {0, 0, 0});
    ecs_set(w, near_ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t far_ball = ecs_new(w);
    ecs_set(w, far_ball, Position, {0, 0, 5});
    ecs_set(w, far_ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, near_ball);

    ecs_add_id(w, near_ball, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, far_ball);
}

void Hidden_a_root_leaves_the_index_with_empty_bounds(void)
{
    // ore_engine/visibility
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_entity_t part = ecs_entity(w, { .parent = prefab });
    ecs_set(w, part, Position, {0, 0, 0});
    ecs_set(w, part, SphereCollider, {{0, 0, 0}, 1});
    ecs_set(w, part, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_entity_t inst = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, inst, Position, {0, 0, 0});
    ecs_progress(w, 0);
    test_int(ecs_get(w, inst, RootBounds)->count, 1);

    PickHit hit;
    ecs_add_id(w, inst, Hidden);
    ecs_progress(w, 0);
    test_int(ecs_get(w, inst, RootBounds)->count, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), false);

    ecs_remove_id(w, inst, Hidden);
    ecs_progress(w, 0);
    test_int(ecs_get(w, inst, RootBounds)->count, 1);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, inst);
}

void Hidden_a_part_of_a_root_shrinks_its_bounds(void)
{
    // ore_engine/visibility
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_entity_t left = ecs_entity(w, { .parent = prefab, .name = "left" });
    ecs_set(w, left, Position, {-4, 0, 0});
    ecs_set(w, left, AABB, {{-5, -1, -1}, {-3, 1, 1}});
    ecs_entity_t right = ecs_entity(w, { .parent = prefab, .name = "right" });
    ecs_set(w, right, Position, {4, 0, 0});
    ecs_set(w, right, AABB, {{3, -1, -1}, {5, 1, 1}});
    ecs_entity_t inst = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, inst, Position, {0, 0, 0});
    ecs_progress(w, 0);
    test_int(ecs_get(w, inst, RootBounds)->count, 2);
    test_flt(ecs_get(w, inst, RootBounds)->min.x, -5);

    ecs_add_id(w, ecs_lookup_from(w, inst, "left"), Hidden);
    ecs_progress(w, 0);
    test_int(ecs_get(w, inst, RootBounds)->count, 1);
    test_flt(ecs_get(w, inst, RootBounds)->min.x, 3);

    ecs_remove_id(w, ecs_lookup_from(w, inst, "left"), Hidden);
    ecs_progress(w, 0);
    test_int(ecs_get(w, inst, RootBounds)->count, 2);
    test_flt(ecs_get(w, inst, RootBounds)->min.x, -5);
}

void Hidden_a_ray_query_misses_it(void)
{
    // ore_engine/visibility
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    box_ray_hit_t hit;
    test_bool(box_collision_ray(w, vec3_new(0, 0, -10), vec3_new(0, 0, 1),
        100, &hit), true);

    ecs_add_id(w, ball, Hidden);
    ecs_progress(w, 0);
    test_bool(box_collision_ray(w, vec3_new(0, 0, -10), vec3_new(0, 0, 1),
        100, &hit), false);
}

void Hidden_a_sphere_query_skips_it(void)
{
    // ore_engine/visibility
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    ecs_vec_t found;
    ecs_vec_init_t(NULL, &found, ecs_entity_t, 0);
    test_int(box_collision_sphere(w, vec3_new(0, 0, 0), 2, &found), 1);

    ecs_add_id(w, ball, Hidden);
    ecs_progress(w, 0);
    ecs_vec_clear(&found);
    test_int(box_collision_sphere(w, vec3_new(0, 0, 0), 2, &found), 0);

    ecs_remove_id(w, ball, Hidden);
    ecs_progress(w, 0);
    ecs_vec_clear(&found);
    test_int(box_collision_sphere(w, vec3_new(0, 0, 0), 2, &found), 1);
    ecs_vec_fini_t(NULL, &found, ecs_entity_t);
}

void Hidden_a_mover_leaves_the_dynamic_index(void)
{
    // ore_engine/visibility
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_add_id(w, ball, DynamicTransform);
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, ball);

    ecs_add_id(w, ball, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), false);

    ecs_remove_id(w, ball, Hidden);
    ecs_progress(w, 0);
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
}
