/**
 * @file Prune.c
 * @brief Tests for the contribution of a node and for the pruning it decides.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Nodes one recorded walk may resolve */
#define PRUNE_LOG_MAX (64)

/** What a walk of a test recorded */
typedef struct prune_log_t {
    int32_t nodes[PRUNE_LOG_MAX];  /**< Node the walk resolved, in order */
    uint32_t votes[PRUNE_LOG_MAX]; /**< Decision it took on that node */
    int32_t count;                 /**< Entries the log holds */
} prune_log_t;

/** Record a node the walk resolved.
 *
 * @param ctx The log
 * @param node Index of the node
 * @param vote The decision the walk took
 */
static void prune_emit(
    void *ctx,
    int32_t node,
    uint32_t vote)
{
    prune_log_t *log = ctx;
    if (log->count < PRUNE_LOG_MAX) {
        log->nodes[log->count] = node;
        log->votes[log->count] = vote;
        log->count ++;
    }
}

void Prune_the_contribution_is_the_widest_face_of_a_node(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t a = box_lod_tree_add(&tree, root, 10);
    int32_t b = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, a, vec3_new(0, 0, 0), vec3_new(2, 3, 0.5f), 0);
    box_lod_tree_set_bounds(&tree, b, vec3_new(8, 0, 0), vec3_new(12, 0.1f, 0.1f), 0);
    box_lod_tree_set_look(&tree, a, grey, 0, 0.5f, 3);
    box_lod_tree_set_look(&tree, b, grey, 0, 0.5f, 0.04f);
    box_lod_tree_refresh(&tree, -1);

    test_flt(box_lod_tree_get(&tree, a)->contribution,
        sqrtf(3.0f * 2.0f) * BOX_LOD_CONTRAST_FLOOR);
    test_flt(box_lod_tree_get(&tree, b)->contribution,
        sqrtf(4.0f * 0.1f) * BOX_LOD_CONTRAST_FLOOR);
    box_lod_tree_fini(&tree);
}

void Prune_a_contrasting_material_contributes_all_of_it(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    float dark[4] = {0, 0, 0, 1};
    float bright[4] = {1, 1, 1, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t sign = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_bounds(&tree, sign, vec3_new(2, 2, 1), vec3_new(4, 4, 1.2f), 0);
    box_lod_tree_set_look(&tree, wall, dark, 0, 0.5f, 10000);
    box_lod_tree_set_look(&tree, sign, bright, 0, 0.5f, 1);
    box_lod_tree_refresh(&tree, -1);

    const box_lod_node_t *n = box_lod_tree_get(&tree, sign);
    test_assert(n->contribution > 1.99f);
    test_assert(n->contribution <= 2.0f);
    test_assert(box_lod_tree_get(&tree, wall)->contribution <
        10.0f * (BOX_LOD_CONTRAST_FLOOR + 0.01f));
    box_lod_tree_fini(&tree);
}

void Prune_a_node_under_the_threshold_is_dropped(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t bolt = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_bounds(&tree, bolt, vec3_new(4, 4, 1), vec3_new(4.4f, 4.4f, 1.4f), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.064f);
    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(5, 5, -100), .fov = 60 },
        .viewport_px = 1080, .threshold = 0, .prune_px = 2,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 1);
    test_int(stats.drawn, 1);
    test_int(log.count, 3);
    test_uint(log.votes[0], BOX_LOD_VOTE_DESCEND);
    test_int(log.nodes[1], wall);
    test_uint(log.votes[1], BOX_LOD_VOTE_DRAW);
    test_int(log.nodes[2], bolt);
    test_uint(log.votes[2], BOX_LOD_VOTE_PRUNE);
    box_lod_tree_fini(&tree);
}

void Prune_pruning_a_parent_prunes_its_descendants(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t trim = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    for (i = 0; i < 4; i ++) {
        int32_t bolt = box_lod_tree_add(&tree, trim, (uint64_t)(20 + i));

        box_lod_tree_set_bounds(&tree, bolt, vec3_new((float)i * 0.1f, 4, 1),
            vec3_new((float)i * 0.1f + 0.08f, 4.08f, 1.08f), 0);
        box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.0005f);
    }

    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(5, 5, -100), .fov = 60 },
        .viewport_px = 1080, .threshold = 0, .prune_px = 2,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 1);
    test_int(stats.drawn, 1);
    test_int(stats.visited, 3);
    test_int(log.nodes[2], trim);
    test_uint(log.votes[2], BOX_LOD_VOTE_PRUNE);
    box_lod_tree_fini(&tree);
}

void Prune_a_pinned_node_is_never_pruned(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t bolt = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_bounds(&tree, bolt, vec3_new(4, 4, 1), vec3_new(4.4f, 4.4f, 1.4f), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.064f);
    box_lod_tree_get(&tree, bolt)->flags |= BOX_LOD_NODE_PIN;
    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(5, 5, -100), .fov = 60 },
        .viewport_px = 1080, .threshold = 0, .prune_px = 2,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 0);
    test_int(stats.drawn, 2);
    box_lod_tree_fini(&tree);
}

void Prune_a_pin_holds_the_whole_subtree(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t trim = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    box_lod_tree_get(&tree, trim)->flags |= BOX_LOD_NODE_PIN;
    for (i = 0; i < 4; i ++) {
        int32_t bolt = box_lod_tree_add(&tree, trim, (uint64_t)(20 + i));

        box_lod_tree_set_bounds(&tree, bolt, vec3_new((float)i * 0.1f, 4, 1),
            vec3_new((float)i * 0.1f + 0.08f, 4.08f, 1.08f), 0);
        box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.0005f);
    }

    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(5, 5, -100), .fov = 60 },
        .viewport_px = 1080, .threshold = 0, .prune_px = 2,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 0);
    test_int(stats.drawn, 5);
    test_int(stats.visited, 7);
    box_lod_tree_fini(&tree);
}

void Prune_a_proxy_wins_over_pruning(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t trim = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    for (i = 0; i < 2; i ++) {
        int32_t bolt = box_lod_tree_add(&tree, trim, (uint64_t)(20 + i));

        box_lod_tree_set_bounds(&tree, bolt, vec3_new((float)i * 0.05f, 4, 1),
            vec3_new((float)i * 0.05f + 0.04f, 4.04f, 1.04f), 0);
        box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.00006f);
    }

    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(5, 5, -100), .fov = 60 },
        .viewport_px = 1080, .threshold = 0.02f, .prune_px = 2,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 0);
    test_int(stats.proxies, 1);
    test_int(log.nodes[2], trim);
    test_uint(log.votes[2], BOX_LOD_VOTE_COLLAPSE);
    box_lod_tree_fini(&tree);
}

void Prune_a_root_is_never_pruned(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);

    box_lod_tree_set_bounds(&tree, root, vec3_new(0, 0, 0), vec3_new(0.2f, 0.2f, 0.2f), 0);
    box_lod_tree_set_look(&tree, root, grey, 0, 0.5f, 0.008f);
    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -2000), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .prune_px = 2,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 0);
    test_int(stats.drawn, 1);
    box_lod_tree_fini(&tree);
}

void Prune_no_threshold_prunes_nothing(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t bolt = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_bounds(&tree, bolt, vec3_new(4, 4, 1), vec3_new(4.4f, 4.4f, 1.4f), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.064f);
    box_lod_tree_refresh(&tree, -1);

    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(5, 5, -100), .fov = 60 },
        .viewport_px = 1080, .threshold = 0,
        .emit = prune_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.pruned, 0);
    test_int(stats.drawn, 2);
    box_lod_tree_fini(&tree);
}

void Prune_the_threshold_moves_with_the_distance(void)
{
    // lod/detail_pruning
    box_lod_tree_t tree;
    prune_log_t log = {0};
    box_lod_walk_stats_t stats;
    float grey[4] = {0.2f, 0.2f, 0.2f, 1};
    float distances[4] = {10.0f, 30.0f, 200.0f, 800.0f};
    int32_t pruned[4];
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t wall = box_lod_tree_add(&tree, root, 10);
    int32_t bolt = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, wall, vec3_new(0, 0, 0), vec3_new(10, 10, 1), 0);
    box_lod_tree_set_bounds(&tree, bolt, vec3_new(4, 4, 1), vec3_new(4.4f, 4.4f, 1.4f), 0);
    box_lod_tree_set_look(&tree, wall, grey, 0, 0.5f, 100);
    box_lod_tree_set_look(&tree, bolt, grey, 0, 0.5f, 0.064f);
    box_lod_tree_refresh(&tree, -1);

    for (i = 0; i < 4; i ++) {
        box_lod_walk_desc_t desc = {
            .camera = { .position = vec3_new(5, 5, -distances[i]), .fov = 60 },
            .viewport_px = 1080, .threshold = 0, .prune_px = 2,
            .emit = prune_emit, .ctx = &log, .stamp = (uint32_t)(i + 1)
        };

        log.count = 0;
        box_lod_tree_walk(&tree, &desc, &stats);
        pruned[i] = stats.pruned;
    }

    test_int(pruned[0], 0);
    test_int(pruned[1], 0);
    test_int(pruned[2], 1);
    test_int(pruned[3], 1);
    box_lod_tree_fini(&tree);
}
