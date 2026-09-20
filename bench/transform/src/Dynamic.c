/**
 * @file Dynamic.c
 * @brief Benchmarks for the per frame world transform system.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities in the dynamic subtree */
#define BENCH_SUBTREE_COUNT (1000)

/** Children per node of the dynamic subtree */
#define BENCH_SUBTREE_FANOUT (8)

void Dynamic_propagate_subtree(bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_SUBTREE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SUBTREE_COUNT; i ++) {
        tree[i] = ecs_new(world);
        if (i) {
            ecs_add_pair(world, tree[i], EcsChildOf,
                tree[(i - 1) / BENCH_SUBTREE_FANOUT]);
        }

        ecs_set(world, tree[i], Position, {0.1f, 0.2f, 0.3f});
        ecs_set(world, tree[i], Rotation, {0.1f, 0.2f, 0.3f});
    }

    ecs_add(world, tree[0], DynamicTransform);
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_SUBTREE_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        const WorldTransform *wt = ecs_get(world,
            tree[BENCH_SUBTREE_COUNT - 1], WorldTransform);
        bench_keep(wt);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}
