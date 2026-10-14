/**
 * @file Layout.c
 * @brief Benchmarks for laying out a UI tree of 10k elements.
 *
 * A column holds 100 rows of 100 rects, every element with a Rect background. Cases lay out
 * the whole tree, relayout after one leaf changed, and run a frame where nothing changed.
 */

#include <box.h>
#include <bake_bench.h>

/** Rows of the tree */
#define BENCH_ROWS (100)

/** Rects per row */
#define BENCH_COLS (100)

/** Create the tree and lay it out once.
 *
 * @param leaf Receives a leaf of the tree, NULL when not needed
 * @return The world
 */
static ecs_world_t* bench_layout_world(
    ecs_entity_t *leaf)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_entity(world, { .name = "ui" });
    ecs_set(world, root, Layout, {
        .direction = DirectionColumn, .spacing = 2, .padding = { 4, 4, 4, 4 } });
    ecs_set(world, root, Rect, {1, 1});
    int32_t r, c;
    for (r = 0; r < BENCH_ROWS; r ++) {
        ecs_entity_t row = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, row, Layout, { .direction = DirectionRow, .spacing = 1 });
        ecs_set(world, row, LayoutItem, { .width = { SizeModeGrow, 1 } });
        ecs_set(world, row, Rect, {1, 1});
        for (c = 0; c < BENCH_COLS; c ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, row);
            ecs_set(world, e, Position, {0, 0, 1});
            ecs_set(world, e, Rect, {(float)(4 + c % 7), (float)(4 + r % 5)});
            if (leaf && r == BENCH_ROWS / 2 && c == BENCH_COLS / 2) {
                *leaf = e;
            }
        }
    }

    ecs_progress(world, 0);

    return world;
}

void Layout_layout_10k(
    bench_t *b)
{
    // ui/layout
    ecs_world_t *world = bench_layout_world(NULL);
    ecs_entity_t root = ecs_lookup(world, "ui");
    bench_set_items(b, BENCH_ROWS * BENCH_COLS);
    while (bench_iter(b)) {
        ecs_modified(world, root, Layout);
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Layout_one_leaf_10k(
    bench_t *b)
{
    // ui/layout
    ecs_entity_t leaf = 0;
    ecs_world_t *world = bench_layout_world(&leaf);
    float width = 4;
    while (bench_iter(b)) {
        width = width == 4 ? 12 : 4;
        ecs_set(world, leaf, Rect, {width, 6});
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Layout_idle_frame_10k(
    bench_t *b)
{
    // ui/layout
    ecs_world_t *world = bench_layout_world(NULL);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
