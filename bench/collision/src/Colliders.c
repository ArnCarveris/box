/**
 * @file Colliders.c
 * @brief Benchmarks for collider resolution and for the raw shape tests.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities with a collider in the scene of the benchmarks */
#define BENCH_COLLIDER_COUNT (100000)

/** Entities a frame moves */
#define BENCH_COLLIDER_MOVED (1000)

/** Shape tests one iteration runs */
#define BENCH_COLLIDER_TESTS (1000)

void Colliders_resolve_1k_of_100k(bench_t *b)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *entities = ecs_os_malloc_n(ecs_entity_t, BENCH_COLLIDER_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_COLLIDER_COUNT; i ++) {
        entities[i] = ecs_new(world);
        ecs_set(world, entities[i], Position, {(float)i, 0, 0});
        ecs_set(world, entities[i], SphereCollider, {{0, 0, 0}, 1});
    }

    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_COLLIDER_MOVED);
    float offset = 0;

    while (bench_iter(b)) {
        offset += 0.25f;
        for (i = 0; i < BENCH_COLLIDER_MOVED; i ++) {
            ecs_set(world, entities[i], Position, {(float)i, offset, 0});
        }

        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, entities[0], Collider));
    }

    ecs_os_free(entities);
    ecs_fini(world);
}

void Colliders_ray_sphere(bench_t *b)
{
    // collision/colliders
    Collider c = { ColliderSphere, {0, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    bench_set_items(b, BENCH_COLLIDER_TESTS);

    while (bench_iter(b)) {
        int32_t i, hits = 0;
        for (i = 0; i < BENCH_COLLIDER_TESTS; i ++) {
            float y = (float)(i % 32) * 0.1f - 1.6f;
            hits += box_collider_ray(&c, vec3_new(-10, y, 0),
                vec3_new(1, 0, 0), 100, NULL);
        }

        bench_keep(hits);
    }
}

void Colliders_ray_box(bench_t *b)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {1, 1, 1},
        {0, 0, 1}, {0, 1, 0}, {-1, 0, 0} };

    bench_set_items(b, BENCH_COLLIDER_TESTS);

    while (bench_iter(b)) {
        int32_t i, hits = 0;
        for (i = 0; i < BENCH_COLLIDER_TESTS; i ++) {
            float y = (float)(i % 32) * 0.1f - 1.6f;
            hits += box_collider_ray(&c, vec3_new(-10, y, 0),
                vec3_new(1, 0, 0), 100, NULL);
        }

        bench_keep(hits);
    }
}

void Colliders_capsule_box(bench_t *b)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    bench_set_items(b, BENCH_COLLIDER_TESTS);

    while (bench_iter(b)) {
        int32_t i, hits = 0;
        for (i = 0; i < BENCH_COLLIDER_TESTS; i ++) {
            float y = (float)(i % 32) * 0.1f;
            hits += box_collider_capsule(&c, vec3_new(0, y + 4, 0),
                vec3_new(0, y + 1, 0), 0.5f);
        }

        bench_keep(hits);
    }
}
