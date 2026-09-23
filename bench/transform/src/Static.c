/**
 * @file Static.c
 * @brief Benchmarks for observer driven world transforms of static entities.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities in the static tree */
#define BENCH_TREE_COUNT (10000)

/** Children per node of the static tree */
#define BENCH_TREE_FANOUT (8)

void Static_set_leaf_position(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_TREE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_TREE_COUNT; i ++) {
        tree[i] = ecs_new(world);
        if (i) {
            ecs_add_pair(world, tree[i], EcsChildOf,
                tree[(i - 1) / BENCH_TREE_FANOUT]);
        }

        ecs_set(world, tree[i], Position, {0.1f, 0.2f, 0.3f});
    }

    ecs_entity_t leaf = tree[BENCH_TREE_COUNT - 1];
    float x = 0;

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        x += 1.0f;
        ecs_set(world, leaf, Position, {x, 0.2f, 0.3f});
        const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
        bench_keep(wt);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Static_set_root_position(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_TREE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_TREE_COUNT; i ++) {
        tree[i] = ecs_new(world);
        if (i) {
            ecs_add_pair(world, tree[i], EcsChildOf,
                tree[(i - 1) / BENCH_TREE_FANOUT]);
        }

        ecs_set(world, tree[i], Position, {0.1f, 0.2f, 0.3f});
    }

    float x = 0;

    bench_set_items(b, BENCH_TREE_COUNT);

    while (bench_iter(b)) {
        x += 1.0f;
        ecs_set(world, tree[0], Position, {x, 0.2f, 0.3f});
        const WorldTransform *wt = ecs_get(world,
            tree[BENCH_TREE_COUNT - 1], WorldTransform);
        bench_keep(wt);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Static_reparent(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_TREE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_TREE_COUNT; i ++) {
        tree[i] = ecs_new(world);
        if (i) {
            ecs_add_pair(world, tree[i], EcsChildOf,
                tree[(i - 1) / BENCH_TREE_FANOUT]);
        }

        ecs_set(world, tree[i], Position, {0.1f, 0.2f, 0.3f});
    }

    ecs_entity_t node = tree[1000];
    ecs_entity_t first = tree[999];
    ecs_entity_t second = tree[998];
    bool flip = false;

    while (bench_iter(b)) {
        ecs_add_pair(world, node, EcsChildOf, flip ? first : second);
        flip = !flip;
        bench_keep(flip);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}
