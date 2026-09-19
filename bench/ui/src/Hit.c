/**
 * @file Hit.c
 * @brief Benchmarks for resolving the cursor against a tree of ten thousand nodes.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/ui/ui_internal.h"

/** Nodes in the benchmarked tree, the root included */
#define BENCH_HIT_COUNT (10000)

/** Children one node of the benchmarked tree holds */
#define BENCH_HIT_FANOUT (8)

/** Build a UI tree of BENCH_HIT_COUNT elements in nested rows and columns.
 *
 * @param world The world
 * @return The nodes, the root first, owned by the caller
 */
static ecs_entity_t* bench_ui_hit_tree(
    ecs_world_t *world)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_HIT_COUNT);

    tree[0] = ecs_new(world);
    ecs_add(world, tree[0], Ui);
    ecs_get_mut(world, tree[0], Ui)->height = 540;
    ecs_modified(world, tree[0], Ui);
    ecs_set(world, tree[0], Position, {0, 0, 0});
    ecs_set(world, tree[0], Layout, {.direction = DirectionColumn,
        .width = 960, .height = 540, .padding = 4, .spacing = 2});

    int32_t i;
    for (i = 1; i < BENCH_HIT_COUNT; i ++) {
        tree[i] = ecs_new_w_pair(world, EcsChildOf,
            tree[(i - 1) / BENCH_HIT_FANOUT]);
        if (i < BENCH_HIT_COUNT / BENCH_HIT_FANOUT) {
            ecs_set(world, tree[i], Layout, {
                .direction = (i & 1) ? DirectionRow : DirectionColumn,
                .padding = 2,
                .spacing = 2
            });
        } else {
            ecs_set(world, tree[i], Rect, {40, 20});
        }
    }

    return tree;
}

void Hit_hit_10k_tree(bench_t *b)
{
    // ui/ui_events
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_hit_tree(world);

    box_ui_update(world);
    ecs_entity_t view = ecs_get(world, tree[0], UiImpl)->view;
    bench_set_items(b, BENCH_HIT_COUNT);

    while (bench_iter(b)) {
        bench_keep(box_ui_hit(world, view, 0, 0));
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Hit_hit_10k_tree_beside_it(bench_t *b)
{
    // ui/ui_events
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_hit_tree(world);

    box_ui_update(world);
    ecs_entity_t view = ecs_get(world, tree[0], UiImpl)->view;
    bench_set_items(b, BENCH_HIT_COUNT);

    while (bench_iter(b)) {
        bench_keep(box_ui_hit(world, view, 2000, 2000));
    }

    ecs_os_free(tree);
    ecs_fini(world);
}
