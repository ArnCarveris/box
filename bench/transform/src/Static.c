/**
 * @file Static.c
 * @brief Benchmarks for observer driven propagation of static world transforms.
 *
 * Each iteration moves one entity and runs a frame, which recomputes the moved
 * entity and its subtree.
 */

#include "tree.h"
#include <bake_bench.h>

/** Move one entity of a static hierarchy every frame.
 *
 * @param b The benchmark
 * @param count Entities in the hierarchy
 * @param fanout Children per node
 * @param parent_component Use the Parent component instead of a ChildOf pair
 * @param leaf Move the last entity instead of the root
 */
static void bench_static_move(
    bench_t *b,
    int32_t count,
    int32_t fanout,
    bool parent_component,
    bool leaf)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = bench_tree_new(world, count, fanout, parent_component);
    ecs_entity_t moved = leaf ? tree[count - 1] : tree[0];
    ecs_entity_t last = tree[count - 1];
    float x = 0;
    ecs_progress(world, 0);

    bench_set_items(b, leaf ? 1 : count);

    while (bench_iter(b)) {
        x += 1.0f;
        ecs_set(world, moved, Position, {x, 0.2f, 0.3f});
        ecs_progress(world, 0);
        const WorldTransform *wt = ecs_get(world, last, WorldTransform);
        bench_keep(wt);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Static_deep_childof(
    bench_t *b)
{
    // testing/bench
    bench_static_move(b, BENCH_DEEP_COUNT, 1, false, false);
}

void Static_deep_parent(
    bench_t *b)
{
    // testing/bench
    bench_static_move(b, BENCH_DEEP_COUNT, 1, true, false);
}

void Static_wide_childof(
    bench_t *b)
{
    // testing/bench
    bench_static_move(b, BENCH_WIDE_COUNT, BENCH_WIDE_COUNT, false, false);
}

void Static_wide_parent(
    bench_t *b)
{
    // testing/bench
    bench_static_move(b, BENCH_WIDE_COUNT, BENCH_WIDE_COUNT, true, false);
}

void Static_deep_leaf_childof(
    bench_t *b)
{
    // testing/bench
    bench_static_move(b, BENCH_DEEP_COUNT, 1, false, true);
}

void Static_wide_leaf_childof(
    bench_t *b)
{
    // testing/bench
    bench_static_move(b, BENCH_WIDE_COUNT, BENCH_WIDE_COUNT, false, true);
}
