/**
 * @file Index.c
 * @brief Tests for the two level spatial index and its queries.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Index_sphere_finds_collider(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {5, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(4, 0, 0), 1, &out), 1);
    test_uint(ecs_vec_get_t(&out, ecs_entity_t, 0)[0], e);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_sphere_misses_far_collider(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {50, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 1, &out), 0);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_aabb_finds_collider(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_aabb(world, vec3_new(-2, -2, -2),
        vec3_new(-0.5f, 2, 2), &out), 1);
    test_int(box_collision_aabb(world, vec3_new(5, 5, 5),
        vec3_new(6, 6, 6), &out), 0);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_ray_finds_nearest(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {5, 0, 0});
    ecs_set(world, near, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {20, 0, 0});
    ecs_set(world, far, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    box_ray_hit_t hit = {0};
    test_bool(box_collision_ray(world, vec3_new(-10, 0, 0),
        vec3_new(1, 0, 0), 1000, &hit), true);
    test_uint(hit.entity, near);
    test_flt(hit.distance, 14);
    test_flt(hit.point.x, 4);

    ecs_fini(world);
}

void Index_ray_misses(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {5, 20, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    box_ray_hit_t hit = {0};
    test_bool(box_collision_ray(world, vec3_new(-10, 0, 0),
        vec3_new(1, 0, 0), 1000, &hit), false);
    test_uint(hit.entity, 0);

    ecs_fini(world);
}

void Index_frustum_finds_inside(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t inside = ecs_new(world);
    ecs_set(world, inside, Position, {0, 0, 0});
    ecs_set(world, inside, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t outside = ecs_new(world);
    ecs_set(world, outside, Position, {100, 0, 0});
    ecs_set(world, outside, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    box_frustum_planes_t f = {{
        {1, 0, 0, 5}, {-1, 0, 0, 5}, {0, 1, 0, 5},
        {0, -1, 0, 5}, {0, 0, 1, 5}, {0, 0, -1, 5}
    }};

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_frustum(world, &f, &out), 1);
    test_uint(ecs_vec_get_t(&out, ecs_entity_t, 0)[0], inside);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_insert_after_update(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 0);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    box_collision_update(world);

    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 1);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_remove_drops_from_index(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {1, 0, 0});
    ecs_set(world, a, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 2);

    ecs_delete(world, b);
    ecs_progress(world, 0);
    ecs_vec_clear(&out);

    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 1);
    test_uint(ecs_vec_get_t(&out, ecs_entity_t, 0)[0], a);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_no_collider_not_in_index(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_add(world, e, NoCollider);
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 0);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_move_updates_index(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 1, &out), 1);

    ecs_set(world, e, Position, {100, 0, 0});
    ecs_progress(world, 0);
    ecs_vec_clear(&out);

    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 1, &out), 0);
    test_int(box_collision_sphere(world, vec3_new(100, 0, 0), 1, &out), 1);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_root_groups_children(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t root = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, root, Position, {0, 0, 0});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, child, Position, {(float)i * 4, 0, 0});
        ecs_set(world, child, SphereCollider, {{0, 0, 0}, 1});
        ecs_set(world, child, AABB, {{(float)i * 4 - 1, -1, -1},
            {(float)i * 4 + 1, 1, 1}});
    }

    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(8, 0, 0), 1, &out), 1);
    ecs_vec_clear(&out);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 4);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_nested_root_found(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t outer = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, outer, Position, {0, 0, 0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, SphereCollider, {{0, 0, 0}, 1});
    ecs_set(world, a, AABB, {{-1, -1, -1}, {1, 1, 1}});

    ecs_entity_t inner = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, inner, EcsChildOf, outer);
    ecs_set(world, inner, Position, {20, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, SphereCollider, {{0, 0, 0}, 1});
    ecs_set(world, b, AABB, {{19, -1, -1}, {21, 1, 1}});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(20, 0, 0), 1, &out), 1);
    test_uint(ecs_vec_get_t(&out, ecs_entity_t, 0)[0], b);
    ecs_vec_clear(&out);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 2);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_dynamic_entity_found(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {3, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(3, 0, 0), 1, &out), 1);
    test_uint(ecs_vec_get_t(&out, ecs_entity_t, 0)[0], e);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_dynamic_entity_moves_each_frame(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    Position *p = ecs_get_mut(world, e, Position);
    p->x = 40;
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 1, &out), 0);
    test_int(box_collision_sphere(world, vec3_new(40, 0, 0), 1, &out), 1);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_disabled_index_finds_nothing(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = false});
    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_sphere(world, vec3_new(0, 0, 0), 100, &out), 0);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);

    ecs_fini(world);
}

void Index_random_sphere_matches_brute_force(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    uint32_t seed = 12345;
    int32_t i;
    for (i = 0; i < 400; i ++) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float y = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float z = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x, y, z});
        if (i % 3) {
            ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1.5f});
        } else {
            ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 2, 1}});
        }
    }

    ecs_progress(world, 0);

    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(Collider) }} });
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    int32_t query;
    for (query = 0; query < 20; query ++) {
        seed = seed * 1103515245u + 12345u;
        float cx = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float cy = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        vec3 center = vec3_new(cx, cy, 0);
        ecs_vec_clear(&out);
        int32_t found = box_collision_sphere(world, center, 8, &out);
        int32_t expect = 0;
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const Collider *c = ecs_field(&it, Collider, 0);
            int32_t j;
            for (j = 0; j < it.count; j ++) {
                expect += box_collider_sphere(&c[j], center, 8);
            }
        }

        test_int(found, expect);
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_query_fini(q);
    ecs_fini(world);
}

void Index_random_aabb_matches_brute_force(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    uint32_t seed = 777;
    int32_t i;
    for (i = 0; i < 400; i ++) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float y = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x, y, 0});
        ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    }

    ecs_progress(world, 0);

    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(Collider) }} });
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    int32_t query;
    for (query = 0; query < 20; query ++) {
        seed = seed * 1103515245u + 12345u;
        float cx = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float cy = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        vec3 qmin = vec3_new(cx - 6, cy - 6, -6);
        vec3 qmax = vec3_new(cx + 6, cy + 6, 6);
        ecs_vec_clear(&out);
        int32_t found = box_collision_aabb(world, qmin, qmax, &out);
        int32_t expect = 0;
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const Collider *c = ecs_field(&it, Collider, 0);
            int32_t j;
            for (j = 0; j < it.count; j ++) {
                vec3 min, max;
                box_collider_bounds(&c[j], &min, &max);
                expect += min.x <= qmax.x && max.x >= qmin.x &&
                    min.y <= qmax.y && max.y >= qmin.y &&
                    min.z <= qmax.z && max.z >= qmin.z;
            }
        }

        test_int(found, expect);
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_query_fini(q);
    ecs_fini(world);
}

void Index_random_ray_matches_brute_force(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    uint32_t seed = 99;
    int32_t i;
    for (i = 0; i < 400; i ++) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float y = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float z = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x, y, z});
        ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1.5f});
    }

    ecs_progress(world, 0);

    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(Collider) }} });
    int32_t query;
    for (query = 0; query < 30; query ++) {
        seed = seed * 1103515245u + 12345u;
        float oy = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float oz = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        vec3 origin = vec3_new(-200, oy, oz);
        vec3 dir = vec3_new(1, 0, 0);
        box_ray_hit_t hit = {0};
        box_collision_ray(world, origin, dir, 1000, &hit);

        ecs_entity_t expect = 0;
        float best = 1000;
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const Collider *c = ecs_field(&it, Collider, 0);
            int32_t j;
            for (j = 0; j < it.count; j ++) {
                float t;
                if (box_collider_ray(&c[j], origin, dir, best, &t) && t < best) {
                    best = t;
                    expect = it.entities[j];
                }
            }
        }

        test_uint(hit.entity, expect);
    }

    ecs_query_fini(q);
    ecs_fini(world);
}

void Index_random_frustum_matches_brute_force(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    uint32_t seed = 4242;
    int32_t i;
    for (i = 0; i < 400; i ++) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        seed = seed * 1103515245u + 12345u;
        float y = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x, y, 0});
        ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    }

    ecs_progress(world, 0);

    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(Collider) }} });
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    int32_t query;
    for (query = 0; query < 20; query ++) {
        seed = seed * 1103515245u + 12345u;
        float cx = (float)((seed >> 16) % 2000) * 0.05f - 50.0f;
        box_frustum_planes_t f = {{
            {1, 0, 0, 10 - cx}, {-1, 0, 0, 10 + cx}, {0, 1, 0, 12},
            {0, -1, 0, 12}, {0, 0, 1, 12}, {0, 0, -1, 12}
        }};

        ecs_vec_clear(&out);
        int32_t found = box_collision_frustum(world, &f, &out);
        int32_t expect = 0;
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const Collider *c = ecs_field(&it, Collider, 0);
            int32_t j;
            for (j = 0; j < it.count; j ++) {
                vec3 min, max;
                box_collider_bounds(&c[j], &min, &max);
                bool in = true;
                int32_t k;
                for (k = 0; k < BOX_FRUSTUM_PLANES; k ++) {
                    vec4 p = f.planes[k];
                    vec3 far = vec3_new(p.x > 0 ? max.x : min.x,
                        p.y > 0 ? max.y : min.y, p.z > 0 ? max.z : min.z);
                    if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f) {
                        in = false;
                        break;
                    }
                }

                expect += in;
            }
        }

        test_int(found, expect);
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_query_fini(q);
    ecs_fini(world);
}

void Index_many_inserts_and_removes(void)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t *entities = ecs_os_malloc_n(ecs_entity_t, 500);
    int32_t i;
    for (i = 0; i < 500; i ++) {
        entities[i] = ecs_new(world);
        ecs_set(world, entities[i], Position, {(float)i, 0, 0});
        ecs_set(world, entities[i], SphereCollider, {{0, 0, 0}, 0.4f});
    }

    ecs_progress(world, 0);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    test_int(box_collision_aabb(world, vec3_new(-1, -1, -1),
        vec3_new(1000, 1, 1), &out), 500);

    for (i = 0; i < 500; i += 2) {
        ecs_delete(world, entities[i]);
    }

    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_collision_aabb(world, vec3_new(-1, -1, -1),
        vec3_new(1000, 1, 1), &out), 250);

    for (i = 1; i < 500; i += 2) {
        ecs_set(world, entities[i], Position, {(float)i, 100, 0});
    }

    ecs_progress(world, 0);
    ecs_vec_clear(&out);
    test_int(box_collision_aabb(world, vec3_new(-1, -1, -1),
        vec3_new(1000, 1, 1), &out), 0);
    ecs_vec_clear(&out);
    test_int(box_collision_aabb(world, vec3_new(-1, 99, -1),
        vec3_new(1000, 101, 1), &out), 250);

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_os_free(entities);
    ecs_fini(world);
}
