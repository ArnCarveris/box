/**
 * @file Dynamic.c
 * @brief Benchmarks for the per frame propagation of dynamic world transforms.
 *
 * The root of each hierarchy has DynamicTransform, so every frame recomputes every
 * entity in it.
 */

#include "tree.h"
#include <bake_bench.h>

/** Run frames over a dynamic hierarchy.
 *
 * @param b The benchmark
 * @param count Entities in the hierarchy
 * @param fanout Children per node
 * @param parent_component Use the Parent component instead of a ChildOf pair
 */
static void bench_dynamic_frame(
    bench_t *b,
    int32_t count,
    int32_t fanout,
    bool parent_component)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = bench_tree_new(world, count, fanout, parent_component);
    ecs_entity_t last = tree[count - 1];
    ecs_add(world, tree[0], DynamicTransform);
    ecs_progress(world, 0);

    bench_set_items(b, count);

    while (bench_iter(b)) {
        ecs_progress(world, 0);
        const WorldTransform *wt = ecs_get(world, last, WorldTransform);
        bench_keep(wt);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Dynamic_deep_childof(
    bench_t *b)
{
    // testing/bench
    bench_dynamic_frame(b, BENCH_DEEP_COUNT, 1, false);
}

void Dynamic_deep_parent(
    bench_t *b)
{
    // testing/bench
    bench_dynamic_frame(b, BENCH_DEEP_COUNT, 1, true);
}

void Dynamic_wide_childof(
    bench_t *b)
{
    // testing/bench
    bench_dynamic_frame(b, BENCH_WIDE_COUNT, BENCH_WIDE_COUNT, false);
}

void Dynamic_wide_parent(
    bench_t *b)
{
    // testing/bench
    bench_dynamic_frame(b, BENCH_WIDE_COUNT, BENCH_WIDE_COUNT, true);
}
