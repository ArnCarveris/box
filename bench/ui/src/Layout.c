/**
 * @file Layout.c
 * @brief Benchmarks for laying out a UI tree of ten thousand elements.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/ui/ui_internal.h"

/** Nodes in the benchmarked tree, the root included */
#define BENCH_LAYOUT_COUNT (10000)

/** Children one node of the benchmarked tree holds */
#define BENCH_LAYOUT_FANOUT (8)

/** Build a UI tree of BENCH_LAYOUT_COUNT elements in nested rows and columns.
 *
 * Every node that has children is a row or a column that sizes itself from its
 * content, and every leaf is a rectangle the rows and the columns place.
 *
 * @param world The world
 * @return The nodes, the root first, owned by the caller
 */
static ecs_entity_t* bench_ui_layout_tree(
    ecs_world_t *world)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_LAYOUT_COUNT);

    tree[0] = ecs_new(world);
    ecs_add(world, tree[0], Ui);
    ecs_get_mut(world, tree[0], Ui)->height = 540;
    ecs_modified(world, tree[0], Ui);
    ecs_set(world, tree[0], Position, {0, 0, 0});
    ecs_set(world, tree[0], Layout, {.direction = DirectionColumn,
        .width = 960, .height = 540, .padding = 4, .spacing = 2});

    int32_t i;
    for (i = 1; i < BENCH_LAYOUT_COUNT; i ++) {
        tree[i] = ecs_new_w_pair(world, EcsChildOf,
            tree[(i - 1) / BENCH_LAYOUT_FANOUT]);
        if (i < BENCH_LAYOUT_COUNT / BENCH_LAYOUT_FANOUT) {
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

void Layout_layout_10k_tree(bench_t *b)
{
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_layout_tree(world);

    box_ui_update(world);
    bench_set_items(b, BENCH_LAYOUT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_get_mut(world, tree[0], UiImpl)->relayout = true;
        bench_resume(b);
        box_ui_update(world);
        bench_keep(ecs_get(world, tree[0], Bounds)->width);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Layout_layout_10k_unchanged(bench_t *b)
{
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_layout_tree(world);

    box_ui_update(world);
    bench_set_items(b, 1);

    while (bench_iter(b)) {
        box_ui_update(world);
        bench_keep(ecs_get(world, tree[0], Bounds)->width);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

/** Build a UI tree of BENCH_LAYOUT_COUNT elements in rows and columns of a size.
 *
 * Every node that has children is a row or a column with a box of its own, so
 * a change below it cannot resize it and the layout of that change stops
 * there, and every leaf is a rectangle the rows and the columns place.
 *
 * @param world The world
 * @return The nodes, the root first, owned by the caller
 */
static ecs_entity_t* bench_ui_fixed_tree(
    ecs_world_t *world)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_LAYOUT_COUNT);

    tree[0] = ecs_new(world);
    ecs_add(world, tree[0], Ui);
    ecs_get_mut(world, tree[0], Ui)->height = 540;
    ecs_modified(world, tree[0], Ui);
    ecs_set(world, tree[0], Position, {0, 0, 0});
    ecs_set(world, tree[0], Layout, {.direction = DirectionColumn,
        .width = 960, .height = 540, .padding = 4, .spacing = 2});

    int32_t i;
    for (i = 1; i < BENCH_LAYOUT_COUNT; i ++) {
        tree[i] = ecs_new_w_pair(world, EcsChildOf,
            tree[(i - 1) / BENCH_LAYOUT_FANOUT]);
        if (i < BENCH_LAYOUT_COUNT / BENCH_LAYOUT_FANOUT) {
            ecs_set(world, tree[i], Layout, {
                .direction = (i & 1) ? DirectionRow : DirectionColumn,
                .width = 120,
                .height = 60,
                .padding = 2,
                .spacing = 2
            });
        } else {
            ecs_set(world, tree[i], Rect, {40, 20});
        }
    }

    return tree;
}

void Layout_layout_one_moved_of_10k(bench_t *b)
{
    // ui/layout_subtree
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_fixed_tree(world);

    box_ui_update(world);
    float x = 0;

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        x += 1.0f;
        ecs_set(world, tree[BENCH_LAYOUT_COUNT - 1], Position, {x, 0, 0});
        bench_resume(b);
        box_ui_update(world);
        bench_keep(ecs_get(world, tree[0], Bounds)->width);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}
