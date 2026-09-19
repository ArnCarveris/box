/**
 * @file Text.c
 * @brief Benchmarks for laying out a thousand glyphs of text in a UI tree.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/ui/ui_internal.h"

/** Paragraphs the benchmarked tree holds */
#define BENCH_TEXT_LINES (25)

/** Characters one paragraph of the benchmarked tree holds */
#define BENCH_TEXT_CHARS (40)

/** Glyphs the benchmarked tree lays out, one per character */
#define BENCH_TEXT_COUNT (BENCH_TEXT_LINES * BENCH_TEXT_CHARS)

/** Build a column of paragraphs that comes to BENCH_TEXT_COUNT glyphs.
 *
 * @param world The world
 * @return The paragraphs, the root first, owned by the caller
 */
static ecs_entity_t* bench_ui_text_tree(
    ecs_world_t *world)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_TEXT_LINES + 1);
    char value[BENCH_TEXT_CHARS + 1];

    tree[0] = ecs_new(world);
    ecs_add(world, tree[0], Ui);
    ecs_get_mut(world, tree[0], Ui)->height = 540;
    ecs_modified(world, tree[0], Ui);
    ecs_set(world, tree[0], Position, {0, 0, 0});
    ecs_set(world, tree[0], Layout, {.direction = DirectionColumn,
        .width = 960, .height = 540, .padding = 8, .spacing = 2});

    int32_t i;
    for (i = 0; i < BENCH_TEXT_LINES; i ++) {
        int32_t c;
        for (c = 0; c < BENCH_TEXT_CHARS; c ++) {
            value[c] = (char)('a' + (i + c) % 26);
        }

        value[BENCH_TEXT_CHARS] = '\0';
        tree[i + 1] = ecs_new_w_pair(world, EcsChildOf, tree[0]);
        ecs_set(world, tree[i + 1], Text, {.value = ecs_os_strdup(value)});
    }

    return tree;
}

void Text_layout_1000_glyphs(bench_t *b)
{
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_text_tree(world);
    char value[BENCH_TEXT_CHARS + 1];
    int32_t turn = 0;

    box_ui_update(world);
    bench_set_items(b, BENCH_TEXT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        int32_t i;
        for (i = 0; i < BENCH_TEXT_LINES; i ++) {
            int32_t c;
            for (c = 0; c < BENCH_TEXT_CHARS; c ++) {
                value[c] = (char)('a' + (i + c + turn) % 26);
            }

            value[BENCH_TEXT_CHARS] = '\0';
            ecs_set(world, tree[i + 1], Text, {.value = ecs_os_strdup(value)});
        }

        turn ++;
        bench_resume(b);
        box_ui_update(world);
        bench_keep(ecs_get(world, tree[1], Bounds)->width);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Text_layout_1000_glyphs_unchanged(bench_t *b)
{
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t *tree = bench_ui_text_tree(world);

    box_ui_update(world);
    bench_set_items(b, BENCH_TEXT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_get_mut(world, tree[0], UiImpl)->relayout = true;
        bench_resume(b);
        box_ui_update(world);
        bench_keep(ecs_get(world, tree[1], Bounds)->width);
    }

    ecs_os_free(tree);
    ecs_fini(world);
}

void Text_measure_1000_glyphs(bench_t *b)
{
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    char value[BENCH_TEXT_CHARS + 1];
    int32_t c;
    for (c = 0; c < BENCH_TEXT_CHARS; c ++) {
        value[c] = (char)('a' + c % 26);
    }

    value[BENCH_TEXT_CHARS] = '\0';
    box_ui_atlas_t *atlas = box_ui_atlas(world, NULL, 0);
    Text text = { .value = value };
    box_ui_extent_t extent;

    box_ui_text_run(world, atlas, &text, 0, 0, &extent);
    bench_set_items(b, BENCH_TEXT_COUNT);

    while (bench_iter(b)) {
        int32_t i;
        for (i = 0; i < BENCH_TEXT_LINES; i ++) {
            box_ui_text_run(world, atlas, &text, 0, 0, &extent);
        }

        bench_keep(extent.width);
    }

    ecs_fini(world);
}
