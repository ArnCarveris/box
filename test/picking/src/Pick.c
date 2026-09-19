/**
 * @file Pick.c
 * @brief Tests for the nearest hit of a pick and for the entity it resolves to.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/picking/picking_internal.h"

static ecs_world_t *w;

void Pick_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxPicking);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Pick_teardown(void)
{
    ecs_fini(w);
}

void Pick_finds_the_object_under_the_cursor(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, ball);
    test_uint(hit.collider, ball);
    test_flt(hit.distance, 9);
    test_flt(hit.point.z, -1);
}

void Pick_misses_when_the_ray_points_away(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){1, 1}, &hit), false);
    test_uint(hit.entity, 0);
    test_uint(hit.collider, 0);
}

void Pick_takes_the_nearest_of_two_objects(void)
{
    // picking/picking
    ecs_entity_t far_ball = ecs_new(w);
    ecs_set(w, far_ball, Position, {0, 0, 5});
    ecs_set(w, far_ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t near_ball = ecs_new(w);
    ecs_set(w, near_ball, Position, {0, 0, 0});
    ecs_set(w, near_ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, near_ball);
}

void Pick_hit_carries_the_normal_of_the_collider(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t crate = ecs_new(w);
    ecs_set(w, crate, Position, {4, 0, 0});
    ecs_set(w, crate, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_flt(hit.normal.x, 0);
    test_flt(hit.normal.y, 0);
    test_flt(hit.normal.z, -1);

    vec3 origin = vec3_new(4, 0, -10);
    vec3 dir = vec3_new(0, 0, 1);
    box_ray_hit_t ray;
    test_bool(box_collision_ray(w, origin, dir, 100, &ray), true);
    const Collider *c = ecs_get(w, crate, Collider);
    vec3 normal = box_picking_normal(c, ray.point);
    test_flt(normal.x, 0);
    test_flt(normal.y, 0);
    test_flt(normal.z, -1);
}

void Pick_beyond_the_max_distance_is_a_miss(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    PickSettings *settings = ecs_singleton_ensure(w, PickSettings);
    settings->max_distance = 5;
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), false);
}

void Pick_resolves_to_the_prefab_root(void)
{
    // picking/picking
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_entity_t part = ecs_entity(w, { .parent = prefab });
    ecs_set(w, part, Position, {0, 0, 0});
    ecs_set(w, part, SphereCollider, {{0, 0, 0}, 1});
    ecs_set(w, part, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_entity_t inst = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, inst, Position, {0, 0, 0});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, inst);
    test_assert(hit.collider != inst);
}

void Pick_resolves_to_the_template_root(void)
{
    // picking/picking
    ECS_IMPORT(w, BoxScript);
    test_int(ecs_script_run(w, "test",
        "using box.transform\n"
        "using box.collision\n"
        "template Tree {\n"
        "  trunk {\n"
        "    Position: {0, 0, 0}\n"
        "    SphereCollider: {radius: 1}\n"
        "    AABB: {min: {-1, -1, -1}, max: {1, 1, 1}}\n"
        "  }\n"
        "}\n"
        "tree {\n"
        "  Position: {0, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t tree = ecs_lookup(w, "tree");
    ecs_entity_t trunk = ecs_lookup(w, "tree.trunk");
    test_assert(tree != 0);
    test_assert(trunk != 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.collider, trunk);
    test_uint(hit.entity, tree);
}

void Pick_a_pickable_descendant_is_the_target(void)
{
    // picking/picking
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_entity_t part = ecs_entity(w, { .parent = prefab });
    ecs_set(w, part, Position, {0, 0, 0});
    ecs_set(w, part, SphereCollider, {{0, 0, 0}, 1});
    ecs_set(w, part, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_add_id(w, part, Pickable);
    ecs_entity_t inst = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, inst, Position, {0, 0, 0});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.entity, hit.collider);
    test_assert(hit.entity != inst);
    test_assert(ecs_has_id(w, hit.entity, Pickable));
}

void Pick_a_pickable_between_the_hit_and_the_root_is_the_target(void)
{
    // picking/picking
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_entity_t arm = ecs_entity(w, { .parent = prefab });
    ecs_set(w, arm, Position, {0, 0, 0});
    ecs_add_id(w, arm, Pickable);
    ecs_entity_t hand = ecs_entity(w, { .parent = arm });
    ecs_set(w, hand, Position, {0, 0, 0});
    ecs_set(w, hand, SphereCollider, {{0, 0, 0}, 1});
    ecs_set(w, hand, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_entity_t inst = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, inst, Position, {0, 0, 0});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_assert(hit.entity != inst);
    test_assert(hit.entity != hit.collider);
    test_assert(ecs_has_id(w, hit.entity, Pickable));
}

void Pick_an_entity_outside_a_root_targets_itself(void)
{
    // picking/picking
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, child, Position, {0, 0, 0});
    ecs_set(w, child, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickHit hit;
    test_bool(box_picking_pick(w, 0, (vec2){0, 0}, &hit), true);
    test_uint(hit.collider, child);
    test_uint(hit.entity, child);
}

void Pick_the_view_with_the_highest_priority_picks_first(void)
{
    // picking/picking
    ecs_entity_t side = ecs_new(w);
    ecs_set(w, side, Position, {10, 0, 0});
    ecs_set(w, side, Camera, {.fov = 60});
    ecs_entity_t low = ecs_new(w);
    ecs_set(w, low, View, {.camera = side, .priority = 0, .enabled = true});
    ecs_entity_t high = ecs_new(w);
    ecs_set(w, high, View, {.camera = side, .priority = 5, .enabled = true});
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    box_pick_view_t views[BOX_PICK_VIEW_MAX];
    test_int(box_picking_views(w, state, views, BOX_PICK_VIEW_MAX), 2);
    test_uint(views[0].entity, high);
    test_uint(views[1].entity, low);
}

void Pick_a_disabled_view_does_not_pick(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {10, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t off = ecs_new(w);
    ecs_set(w, off, View, {.camera = cam, .enabled = false});
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    box_pick_view_t views[BOX_PICK_VIEW_MAX];
    test_int(box_picking_views(w, state, views, BOX_PICK_VIEW_MAX), 1);
    test_uint(views[0].entity, 0);
}

void Pick_matches_a_brute_force_pick_on_a_smaller_scene(void)
{
    // picking/scale
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    int32_t roots = 200, fanout = 20, side = 14, i, j;
    for (i = 0; i < roots; i ++) {
        float x = (float)(i % side - side / 2) * 0.6f;
        float z = (float)(i / side) * 0.6f;
        ecs_entity_t root = ecs_new_w_pair(w, EcsIsA, prefab);
        ecs_set(w, root, Position, {x, 0, z});
        for (j = 0; j < fanout; j ++) {
            float dx = (float)(j % 5) * 0.15f - 0.3f;
            float dy = (float)(j / 5) * 0.15f;
            ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
            ecs_set(w, child, Position, {dx, dy, 0});
            ecs_set(w, child, SphereCollider, {{0, 0, 0}, 0.1f});
            ecs_set(w, child, AABB, {
                {x + dx - 0.1f, dy - 0.1f, z - 0.1f},
                {x + dx + 0.1f, dy + 0.1f, z + 0.1f}});
        }
    }

    ecs_progress(w, 0);

    ecs_query_t *q = ecs_query(w, { .terms = {{ .id = ecs_id(Collider) }} });
    uint32_t seed = 4242;
    int32_t query, hits = 0;
    for (query = 0; query < 40; query ++) {
        seed = seed * 1103515245u + 12345u;
        float cx = (float)((seed >> 16) % 200) / 100.0f - 1.0f;
        seed = seed * 1103515245u + 12345u;
        float cy = (float)((seed >> 16) % 200) / 100.0f - 1.0f;
        vec2 cursor = {cx, cy};

        vec3 origin, dir;
        test_bool(box_picking_ray(w, 0, cursor, &origin, &dir), true);

        ecs_entity_t nearest = 0;
        float nearest_dist = BOX_PICK_DISTANCE;
        ecs_iter_t it = ecs_query_iter(w, q);
        while (ecs_query_next(&it)) {
            const Collider *c = ecs_field(&it, Collider, 0);
            int32_t k;
            for (k = 0; k < it.count; k ++) {
                float dist;
                if (box_collider_ray(&c[k], origin, dir, nearest_dist, &dist)) {
                    nearest = it.entities[k];
                    nearest_dist = dist;
                }
            }
        }

        ecs_entity_t expect = nearest ? box_picking_target(w, nearest) : 0;
        PickHit hit;
        bool found = box_picking_pick(w, 0, cursor, &hit);
        test_bool(found, nearest != 0);
        test_uint(hit.entity, expect);
        hits += found;
    }

    test_assert(hits > 0);

    ecs_query_fini(q);
}

void Pick_a_view_with_a_render_target_does_not_pick(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {10, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {.width = 64, .height = 64});
    ecs_entity_t off = ecs_new(w);
    ecs_set(w, off, View, {.camera = cam, .target = target, .enabled = true});
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    box_pick_view_t views[BOX_PICK_VIEW_MAX];
    test_int(box_picking_views(w, state, views, BOX_PICK_VIEW_MAX), 1);
    test_uint(views[0].entity, 0);
}
