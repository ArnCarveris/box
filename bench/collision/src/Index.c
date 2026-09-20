/**
 * @file Index.c
 * @brief Benchmarks for the spatial index on a scene of a million colliders.
 */

#include <box.h>
#include <bake_bench.h>

/** Roots of the large scene */
#define BENCH_INDEX_ROOTS (50000)

/** Colliders below every root */
#define BENCH_INDEX_FANOUT (20)

/** Roots along one side of the grid the scene is laid out on */
#define BENCH_INDEX_SIDE (224)

/** Distance between two roots of the grid */
#define BENCH_INDEX_STEP (4.0f)

/** Entities that move every frame */
#define BENCH_INDEX_MOVERS (1000)

/** Build a scene of nested roots with a collider on every leaf.
 *
 * @param world The world
 * @param roots Number of roots to create
 * @param movers Number of leaves that move every frame
 */
static void bench_index_build(
    ecs_world_t *world,
    int32_t roots,
    int32_t movers)
{
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    int32_t i, j;
    for (i = 0; i < roots; i ++) {
        float x = (float)(i % BENCH_INDEX_SIDE) * BENCH_INDEX_STEP;
        float z = (float)(i / BENCH_INDEX_SIDE) * BENCH_INDEX_STEP;
        ecs_entity_t root = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, root, Position, {x, 0, z});
        for (j = 0; j < BENCH_INDEX_FANOUT; j ++) {
            float dx = (float)(j % 5) * 0.6f - 1.2f;
            float dy = (float)(j / 5) * 0.6f;
            ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
            ecs_set(world, child, Position, {dx, dy, 0});
            ecs_set(world, child, SphereCollider, {{0, 0, 0}, 0.25f});
            ecs_set(world, child, AABB, {
                {x + dx - 0.25f, dy - 0.25f, z - 0.25f},
                {x + dx + 0.25f, dy + 0.25f, z + 0.25f}});
        }
    }

    for (i = 0; i < movers; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)i * 0.5f, 2, 0});
        ecs_set(world, e, SphereCollider, {{0, 0, 0}, 0.25f});
    }
}

void Index_build_1m(bench_t *b)
{
    // collision/spatial_index
    bench_set_items(b, BENCH_INDEX_ROOTS * BENCH_INDEX_FANOUT);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        bench_index_build(world, BENCH_INDEX_ROOTS, 0);
        bench_resume(b);

        box_collision_update(world);
        bench_keep(ecs_count_id(world, ecs_id(Collider)));

        bench_pause(b);
        ecs_fini(world);
        bench_resume(b);
    }
}

void Index_ray_1m(bench_t *b)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_index_build(world, BENCH_INDEX_ROOTS, 0);
    box_collision_update(world);

    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float z = (float)(step ++ % BENCH_INDEX_SIDE) * BENCH_INDEX_STEP;
        box_ray_hit_t hit = {0};
        box_collision_ray(world, vec3_new(-100, 0, z), vec3_new(1, 0, 0), 2000, &hit);
        bench_keep(hit.entity);
    }

    ecs_fini(world);
}

void Index_ray_miss_1m(bench_t *b)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_index_build(world, BENCH_INDEX_ROOTS, 0);
    box_collision_update(world);

    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float z = (float)(step ++ % BENCH_INDEX_SIDE) * BENCH_INDEX_STEP + 2.0f;
        box_ray_hit_t hit = {0};
        box_collision_ray(world, vec3_new(-100, 8, z), vec3_new(1, 0, 0), 2000, &hit);
        bench_keep(hit.entity);
    }

    ecs_fini(world);
}

void Index_sphere_1m(bench_t *b)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_index_build(world, BENCH_INDEX_ROOTS, 0);
    box_collision_update(world);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float x = (float)(step ++ % BENCH_INDEX_SIDE) * BENCH_INDEX_STEP;
        ecs_vec_clear(&out);
        bench_keep(box_collision_sphere(world, vec3_new(x, 0, x), 2, &out));
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Index_frustum_1m(bench_t *b)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_index_build(world, BENCH_INDEX_ROOTS, 0);
    box_collision_update(world);

    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float x = (float)(step ++ % BENCH_INDEX_SIDE) * BENCH_INDEX_STEP;
        box_frustum_planes_t f = {{
            {1, 0, 0, 10 - x}, {-1, 0, 0, 10 + x}, {0, 1, 0, 20},
            {0, -1, 0, 20}, {0, 0, 1, 10}, {0, 0, -1, 10}
        }};

        ecs_vec_clear(&out);
        bench_keep(box_collision_frustum(world, &f, &out));
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_fini(world);
}

void Index_update_1k_movers_of_1m(bench_t *b)
{
    // collision/spatial_index
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_index_build(world, BENCH_INDEX_ROOTS, BENCH_INDEX_MOVERS);
    box_collision_update(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Position) },
            { .id = DynamicTransform, .inout = EcsInOutNone }
        }
    });

    bench_set_items(b, BENCH_INDEX_MOVERS);

    while (bench_iter(b)) {
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            Position *p = ecs_field(&it, Position, 0);
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                p[i].y += 0.01f;
            }
        }

        ecs_progress(world, 0.016f);
        bench_keep(world);
    }

    ecs_query_fini(q);
    ecs_fini(world);
}
