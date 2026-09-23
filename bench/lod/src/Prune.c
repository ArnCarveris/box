/**
 * @file Prune.c
 * @brief Benchmarks the instances detail pruning drops out of a city of frames.
 */

#include <box.h>
#include <bake_bench.h>

/** Frames the benchmark city holds */
#define BENCH_PRUNE_ROOTS (512)

/** Frames along one row of the city */
#define BENCH_PRUNE_ROW (32)

/** World units between two frames */
#define BENCH_PRUNE_SPACING (30.0f)

/** Structural parts every frame holds */
#define BENCH_PRUNE_PARTS (6)

/** Small details every frame holds */
#define BENCH_PRUNE_DETAILS (48)

/** Height in pixels the walk is measured against */
#define BENCH_PRUNE_VIEWPORT (1080.0f)

/** Build a city of open frames with structural parts and small details.
 *
 * The frames are sparse, so their bounding box misses too much for a proxy and
 * the walk keeps descending into them; what the contribution test can drop is
 * the details, which is the difference the two cases below report.
 *
 * @param tree The tree to fill
 */
static void bench_prune_city(
    box_lod_tree_t *tree)
{
    float steel[4] = {0.2f, 0.21f, 0.23f, 1};
    float paint[4] = {0.7f, 0.2f, 0.1f, 1};
    int32_t i;

    for (i = 0; i < BENCH_PRUNE_ROOTS; i ++) {
        float x = (float)(i % BENCH_PRUNE_ROW) * BENCH_PRUNE_SPACING;
        float z = (float)(i / BENCH_PRUNE_ROW) * BENCH_PRUNE_SPACING;
        int32_t root = box_lod_tree_add(tree, -1, (uint64_t)(i + 1));
        int32_t j;

        for (j = 0; j < BENCH_PRUNE_PARTS; j ++) {
            int32_t part = box_lod_tree_add(tree, root, (uint64_t)(i * 512 + j + 1000));

            box_lod_tree_set_bounds(tree, part, vec3_new(x, (float)j * 8.0f, z),
                vec3_new(x + 12.0f, (float)j * 8.0f + 0.5f, z + 12.0f), 0);
            box_lod_tree_set_look(tree, part, steel, 0, 0.6f, 72);
        }

        for (j = 0; j < BENCH_PRUNE_DETAILS; j ++) {
            int32_t bolt = box_lod_tree_add(tree, root,
                (uint64_t)(i * 512 + j + 100000));
            float bx = x + (float)(j % 8) * 1.5f;
            float by = (float)(j / 8) * 8.0f + 1.0f;

            box_lod_tree_set_bounds(tree, bolt, vec3_new(bx, by, z),
                vec3_new(bx + 0.4f, by + 0.4f, z + 0.4f), 0);
            box_lod_tree_set_look(tree, bolt, paint, 0, 0.4f, 0.064f);
        }
    }

    box_lod_tree_refresh(tree, -1);
}

void Prune_city_without_pruning(
    bench_t *b)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    box_lod_walk_stats_t stats = {0};

    box_lod_tree_init(&tree);
    bench_prune_city(&tree);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(240, 6, -80), .fov = 60 },
        .viewport_px = BENCH_PRUNE_VIEWPORT, .threshold = 2, .hysteresis = 0.25f
    };

    bench_set_items(b, BENCH_PRUNE_ROOTS);

    while (bench_iter(b)) {
        box_lod_tree_walk(&tree, &desc, &stats);
        bench_counter(b, "visited", (double)stats.visited);
        bench_counter(b, "drawn", (double)stats.drawn);
        bench_counter(b, "pruned", (double)stats.pruned);
        bench_keep(stats.drawn);
    }

    box_lod_tree_fini(&tree);
}

void Prune_city_with_pruning(
    bench_t *b)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    box_lod_walk_stats_t stats = {0};

    box_lod_tree_init(&tree);
    bench_prune_city(&tree);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(240, 6, -80), .fov = 60 },
        .viewport_px = BENCH_PRUNE_VIEWPORT, .threshold = 2, .hysteresis = 0.25f,
        .prune_px = 2
    };

    bench_set_items(b, BENCH_PRUNE_ROOTS);

    while (bench_iter(b)) {
        box_lod_tree_walk(&tree, &desc, &stats);
        bench_counter(b, "visited", (double)stats.visited);
        bench_counter(b, "drawn", (double)stats.drawn);
        bench_counter(b, "pruned", (double)stats.pruned);
        bench_keep(stats.drawn);
    }

    box_lod_tree_fini(&tree);
}
