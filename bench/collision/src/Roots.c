/**
 * @file Roots.c
 * @brief Benchmarks for the incremental aggregate bounds of roots.
 */

#include <box.h>
#include <bake_bench.h>

/** Roots in the scene of the benchmarks */
#define BENCH_ROOT_COUNT (100000)

/** Children below every root */
#define BENCH_ROOT_FANOUT (4)

/** Roots a frame changes */
#define BENCH_ROOT_CHANGED (1000)

/** Build a scene of roots with children that carry bounds.
 *
 * @param world The world
 * @param roots Receives the roots
 * @param first Receives the first child of every root
 * @param rigid true to mark every root as a rigid subtree
 */
static void bench_roots_build(
    ecs_world_t *world,
    ecs_entity_t *roots,
    ecs_entity_t *first,
    bool rigid)
{
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    int32_t i, j;
    for (i = 0; i < BENCH_ROOT_COUNT; i ++) {
        ecs_entity_t root = ecs_new_w_pair(world, EcsIsA, prefab);
        roots[i] = root;
        if (rigid) {
            ecs_add(world, root, RigidSubtree);
        }

        ecs_set(world, root, Position, {(float)i, 0, 0});
        for (j = 0; j < BENCH_ROOT_FANOUT; j ++) {
            ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
            ecs_set(world, child, AABB, {
                {(float)i - 1, (float)j, -1}, {(float)i + 1, (float)j + 1, 1}});
            if (!j) {
                first[i] = child;
            }
        }
    }

    ecs_progress(world, 0.016f);
}

void Roots_idle_100k(
    bench_t *b)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *roots = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_COUNT);
    ecs_entity_t *first = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_COUNT);
    bench_roots_build(world, roots, first, false);

    bench_set_items(b, BENCH_ROOT_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, roots[0], RootBounds));
    }

    ecs_os_free(first);
    ecs_os_free(roots);
    ecs_fini(world);
}

void Roots_update_1k_of_100k(
    bench_t *b)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *roots = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_COUNT);
    ecs_entity_t *first = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_COUNT);
    bench_roots_build(world, roots, first, false);

    bench_set_items(b, BENCH_ROOT_CHANGED);
    float offset = 0;

    while (bench_iter(b)) {
        offset += 0.25f;
        int32_t i;
        for (i = 0; i < BENCH_ROOT_CHANGED; i ++) {
            ecs_set(world, first[i], AABB, {
                {(float)i - 1, offset, -1}, {(float)i + 1, offset + 1, 1}});
        }

        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, roots[0], RootBounds));
    }

    ecs_os_free(first);
    ecs_os_free(roots);
    ecs_fini(world);
}

void Roots_move_1k_rigid_of_100k(
    bench_t *b)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *roots = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_COUNT);
    ecs_entity_t *first = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_COUNT);
    bench_roots_build(world, roots, first, true);

    bench_set_items(b, BENCH_ROOT_CHANGED);
    float offset = 0;

    while (bench_iter(b)) {
        offset += 0.25f;
        int32_t i;
        for (i = 0; i < BENCH_ROOT_CHANGED; i ++) {
            ecs_set(world, roots[i], Position, {(float)i, offset, 0});
        }

        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, roots[0], RootBounds));
    }

    ecs_os_free(first);
    ecs_os_free(roots);
    ecs_fini(world);
}

void Roots_instantiate_1k(
    bench_t *b)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t *roots = ecs_os_malloc_n(ecs_entity_t, BENCH_ROOT_CHANGED);

    bench_set_items(b, BENCH_ROOT_CHANGED);

    while (bench_iter(b)) {
        int32_t i, j;
        for (i = 0; i < BENCH_ROOT_CHANGED; i ++) {
            roots[i] = ecs_new_w_pair(world, EcsIsA, prefab);
            for (j = 0; j < BENCH_ROOT_FANOUT; j ++) {
                ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, roots[i]);
                ecs_set(world, child, AABB, {{-1, (float)j, -1}, {1, (float)j + 1, 1}});
            }
        }

        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, roots[0], RootBounds));

        bench_pause(b);
        for (i = 0; i < BENCH_ROOT_CHANGED; i ++) {
            ecs_delete(world, roots[i]);
        }

        ecs_progress(world, 0.016f);
        bench_resume(b);
    }

    ecs_os_free(roots);
    ecs_fini(world);
}
