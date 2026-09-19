/**
 * @file Walk.c
 * @brief Benchmarks the traversal of a large hierarchy against its leaf count.
 */

#include <box.h>
#include <bake_bench.h>

/** Roots the benchmark city holds */
#define BENCH_WALK_ROOTS (4096)

/** Roots along one row of the city */
#define BENCH_WALK_ROW (64)

/** World units between two roots */
#define BENCH_WALK_SPACING (18.0f)

/** Roots one cluster node of the stacked hierarchy groups */
#define BENCH_WALK_GROUP (8)

/** Roots the clustering stops stacking at */
#define BENCH_WALK_TOP (16)

/** Height in pixels the walk is measured against */
#define BENCH_WALK_VIEWPORT (1080.0f)

/** Build a city of roots whose leaves stack inside a fixed box.
 *
 * The leaves of a root always fill the same part of the same box, so a run with
 * more leaves per root describes the same city in more pieces and the walk has
 * to make the same decisions.
 *
 * @param tree The tree to fill
 * @param per_root Leaves every root holds
 */
static void bench_walk_city(
    box_lod_tree_t *tree,
    int32_t per_root)
{
    int32_t i;
    for (i = 0; i < BENCH_WALK_ROOTS; i ++) {
        float x = (float)(i % BENCH_WALK_ROW) * BENCH_WALK_SPACING;
        float z = (float)(i / BENCH_WALK_ROW) * BENCH_WALK_SPACING;
        float step = 12.0f / (float)per_root;
        int32_t root = box_lod_tree_add(tree, -1, (uint64_t)(i + 1));
        int32_t j;

        for (j = 0; j < per_root; j ++) {
            int32_t leaf = box_lod_tree_add(tree, root, (uint64_t)(i * 4096 + j + 100000));

            box_lod_tree_set_bounds(tree, leaf, vec3_new(x, (float)j * step, z),
                vec3_new(x + 8.0f, (float)j * step + step * 0.6f, z + 8.0f), 0);
        }
    }

    box_lod_tree_refresh(tree, -1);
    while (box_lod_tree_cluster(tree, BENCH_WALK_GROUP) > BENCH_WALK_TOP) {
        continue;
    }
}

void Walk_city_32k_leaves(bench_t *b)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    box_lod_walk_stats_t stats = {0};

    box_lod_tree_init(&tree);
    bench_walk_city(&tree, 8);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(560, 4, -2000), .fov = 60 },
        .viewport_px = BENCH_WALK_VIEWPORT, .threshold = 2, .hysteresis = 0.25f
    };

    bench_set_items(b, BENCH_WALK_ROOTS);

    while (bench_iter(b)) {
        box_lod_tree_walk(&tree, &desc, &stats);
        bench_counter(b, "leaves", (double)BENCH_WALK_ROOTS * 8.0);
        bench_counter(b, "visited", (double)stats.visited);
        bench_counter(b, "proxies", (double)stats.proxies);
        bench_counter(b, "drawn", (double)stats.drawn);
        bench_keep(stats.visited);
    }

    box_lod_tree_fini(&tree);
}

void Walk_city_256k_leaves(bench_t *b)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    box_lod_walk_stats_t stats = {0};

    box_lod_tree_init(&tree);
    bench_walk_city(&tree, 64);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(560, 4, -2000), .fov = 60 },
        .viewport_px = BENCH_WALK_VIEWPORT, .threshold = 2, .hysteresis = 0.25f
    };

    bench_set_items(b, BENCH_WALK_ROOTS);

    while (bench_iter(b)) {
        box_lod_tree_walk(&tree, &desc, &stats);
        bench_counter(b, "leaves", (double)BENCH_WALK_ROOTS * 64.0);
        bench_counter(b, "visited", (double)stats.visited);
        bench_counter(b, "proxies", (double)stats.proxies);
        bench_counter(b, "drawn", (double)stats.drawn);
        bench_keep(stats.visited);
    }

    box_lod_tree_fini(&tree);
}

void Walk_city_256k_leaves_close(bench_t *b)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    box_lod_walk_stats_t stats = {0};

    box_lod_tree_init(&tree);
    bench_walk_city(&tree, 64);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(560, 4, 560), .fov = 60 },
        .viewport_px = BENCH_WALK_VIEWPORT, .threshold = 2, .hysteresis = 0.25f
    };

    bench_set_items(b, BENCH_WALK_ROOTS);

    while (bench_iter(b)) {
        box_lod_tree_walk(&tree, &desc, &stats);
        bench_counter(b, "leaves", (double)BENCH_WALK_ROOTS * 64.0);
        bench_counter(b, "visited", (double)stats.visited);
        bench_counter(b, "proxies", (double)stats.proxies);
        bench_counter(b, "drawn", (double)stats.drawn);
        bench_keep(stats.visited);
    }

    box_lod_tree_fini(&tree);
}

void Walk_cluster_4k_roots(bench_t *b)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;

    box_lod_tree_init(&tree);
    bench_set_items(b, BENCH_WALK_ROOTS);

    while (bench_iter(b)) {
        int32_t i;

        box_lod_tree_clear(&tree);
        for (i = 0; i < BENCH_WALK_ROOTS; i ++) {
            float x = (float)(i % BENCH_WALK_ROW) * BENCH_WALK_SPACING;
            float z = (float)(i / BENCH_WALK_ROW) * BENCH_WALK_SPACING;
            int32_t root = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));

            box_lod_tree_set_bounds(&tree, root, vec3_new(x, 0, z),
                vec3_new(x + 8.0f, 12, z + 8.0f), 0);
        }

        while (box_lod_tree_cluster(&tree, BENCH_WALK_GROUP) > BENCH_WALK_TOP) {
            continue;
        }

        bench_counter(b, "nodes", (double)tree.count);
        bench_keep(tree.count);
    }

    box_lod_tree_fini(&tree);
}
