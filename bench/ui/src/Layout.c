/**
 * @file Layout.c
 * @brief Benchmarks for laying out a UI tree of 10k elements.
 *
 * A column holds 100 rows of 100 rects, every element with a Rect background. Cases lay out
 * the whole tree, relayout after one leaf changed, and run a frame where nothing changed.
 *
 * A leaf change stays in its row under a fixed width root; under a fitting root it widens
 * the widest row, so the root and every row resize and every element moves.
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
 * @param width Fixed width of the root, 0 for a root that fits the rows
 * @return The world
 */
static ecs_world_t* bench_layout_world(
    ecs_entity_t *leaf,
    float width)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_entity(world, { .name = "ui" });
    ecs_set(world, root, Layout, {
        .direction = DirectionColumn, .spacing = 2, .padding = { 4, 4, 4, 4 } });
    ecs_set(world, root, Rect, {1, 1});
    if (width > 0) {
        ecs_set(world, root, LayoutItem, { .width = { SizeModeFixed, width } });
    }

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
    ecs_world_t *world = bench_layout_world(NULL, 0);
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
    ecs_world_t *world = bench_layout_world(&leaf, 1000);
    float width = 4;
    while (bench_iter(b)) {
        width = width == 4 ? 12 : 4;
        ecs_set(world, leaf, Rect, {width, 6});
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Layout_one_leaf_resizes_10k(
    bench_t *b)
{
    // ui_fixes/grow_basis
    ecs_entity_t leaf = 0;
    ecs_world_t *world = bench_layout_world(&leaf, 0);
    float width = 4;
    while (bench_iter(b)) {
        width = width == 4 ? 12 : 4;
        ecs_set(world, leaf, Rect, {width, 6});
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Layout_rewrap_100_paragraphs(
    bench_t *b)
{
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, { .direction = DirectionColumn, .spacing = 4 });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 300 } });
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ecs_entity_t p = ecs_new_w_pair(world, EcsChildOf, panel);
        ecs_set(world, p, Text, { .size = 16, .font = font, .value =
            "A paragraph of a few dozen words that a panel of a few hundred points "
            "wide breaks into several lines, and breaks again when the panel resizes" });
        ecs_set(world, p, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    }

    ecs_progress(world, 0);
    float width = 300;
    bench_set_items(b, 100);
    while (bench_iter(b)) {
        width = width == 300 ? 400 : 300;
        ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, width } });
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

/** Number of entities a Position bench sets */
#define BENCH_SETS (100000)

void Layout_position_100k_roots(
    bench_t *b)
{
    // ui_fixes/layout_observer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *e = ecs_os_malloc_n(ecs_entity_t, BENCH_SETS);
    int32_t i;
    for (i = 0; i < BENCH_SETS; i ++) {
        e[i] = ecs_new(world);
        ecs_set(world, e[i], Position, {0, 0, 0});
    }

    float x = 0;
    bench_set_items(b, BENCH_SETS);
    while (bench_iter(b)) {
        x += 1;
        for (i = 0; i < BENCH_SETS; i ++) {
            ecs_set(world, e[i], Position, {x, 0, 0});
        }
    }

    ecs_os_free(e);
    ecs_fini(world);
}

void Layout_position_100k_children(
    bench_t *b)
{
    // ui_fixes/layout_observer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *e = ecs_os_malloc_n(ecs_entity_t, BENCH_SETS);
    ecs_entity_t parent = 0;
    int32_t i;
    for (i = 0; i < BENCH_SETS; i ++) {
        if (!(i % 100)) {
            parent = ecs_new(world);
            ecs_set(world, parent, Position, {0, 0, 0});
        }

        e[i] = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, e[i], Position, {0, 0, 0});
    }

    float x = 0;
    bench_set_items(b, BENCH_SETS);
    while (bench_iter(b)) {
        x += 1;
        for (i = 0; i < BENCH_SETS; i ++) {
            ecs_set(world, e[i], Position, {x, 0, 0});
        }
    }

    ecs_os_free(e);
    ecs_fini(world);
}

void Layout_idle_frame_10k(
    bench_t *b)
{
    // ui/layout
    ecs_world_t *world = bench_layout_world(NULL, 0);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
