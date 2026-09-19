/**
 * @file Tree.c
 * @brief Tests for the node hierarchy of the traversal and for the walk over it.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Nodes one recorded walk may resolve */
#define TREE_LOG_MAX (256)

/** What a walk of a test recorded and what its callbacks answer */
typedef struct tree_log_t {
    box_lod_tree_t *tree;           /**< The tree being walked */
    int32_t nodes[TREE_LOG_MAX];    /**< Node the walk resolved, in order */
    uint32_t votes[TREE_LOG_MAX];   /**< Decision it took on that node */
    int32_t count;                  /**< Entries the log holds */
    int32_t visited[TREE_LOG_MAX];  /**< Node the culling was asked about, in order */
    int32_t visits;                 /**< Entries the visit log holds */
    uint64_t reject;                /**< Identity the culling rejects, 0 for none */
    int32_t open_node;              /**< Node the open callback gives children to */
    int32_t open_children;          /**< Children it gives that node */
    int32_t opened;                 /**< Times the open callback ran */
} tree_log_t;

/** Record a node the walk resolved.
 *
 * @param ctx The log
 * @param node Index of the node
 * @param vote The decision the walk took
 */
static void tree_emit(
    void *ctx,
    int32_t node,
    uint32_t vote)
{
    tree_log_t *log = ctx;
    if (log->count < TREE_LOG_MAX) {
        log->nodes[log->count] = node;
        log->votes[log->count] = vote;
        log->count ++;
    }
}

/** Record a node the walk asked about and reject the one the log names.
 *
 * @param ctx The log
 * @param node The node
 * @return True when the walk keeps the node
 */
static bool tree_visible(
    void *ctx,
    const box_lod_node_t *node)
{
    tree_log_t *log = ctx;
    if (log->visits < TREE_LOG_MAX) {
        log->visited[log->visits] = (int32_t)node->id;
        log->visits ++;
    }

    return !log->reject || node->id != log->reject;
}

/** Give one node the children the log asks for, once.
 *
 * @param ctx The log
 * @param node Index of the node
 * @return True when the node holds children afterwards
 */
static bool tree_open(
    void *ctx,
    int32_t node)
{
    tree_log_t *log = ctx;
    bool result = false;
    if (node != log->open_node) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < log->open_children; i ++) {
        int32_t child = box_lod_tree_add(log->tree, node, (uint64_t)(100 + i));

        box_lod_tree_set_bounds(log->tree, child,
            vec3_new((float)i, 0, 0), vec3_new((float)i + 1.0f, 1, 1), 0);
    }

    log->opened ++;
    box_lod_tree_refresh(log->tree, node);
    result = log->open_children > 0;
done:
    return result;
}

void Tree_a_walk_visits_every_root(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;
    int32_t i;

    box_lod_tree_init(&tree);
    for (i = 0; i < 5; i ++) {
        int32_t node = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));

        box_lod_tree_set_bounds(&tree, node,
            vec3_new((float)i * 4.0f, 0, 0), vec3_new((float)i * 4.0f + 1.0f, 1, 1), 0);
    }

    log.tree = &tree;
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -10), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.visited, 5);
    test_int(stats.drawn, 5);
    test_int(stats.proxies, 0);
    test_int(stats.rejected, 0);
    test_int(log.count, 5);
    box_lod_tree_fini(&tree);
}

void Tree_the_walk_visits_a_parent_before_its_children(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    for (i = 0; i < 3; i ++) {
        int32_t child = box_lod_tree_add(&tree, root, (uint64_t)(10 + i));

        box_lod_tree_set_bounds(&tree, child,
            vec3_new((float)i * 3.0f, 0, 0), vec3_new((float)i * 3.0f + 1.0f, 1, 1), 0);
    }

    box_lod_tree_refresh(&tree, -1);
    log.tree = &tree;
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -2), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.visited, 4);
    test_int(log.visits, 4);
    test_int(log.visited[0], 1);
    test_int(log.visited[1], 10);
    test_int(log.visited[2], 11);
    test_int(log.visited[3], 12);
    box_lod_tree_fini(&tree);
}

void Tree_a_node_under_the_threshold_becomes_a_proxy(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    for (i = 0; i < 4; i ++) {
        int32_t child = box_lod_tree_add(&tree, root, (uint64_t)(10 + i));

        box_lod_tree_set_bounds(&tree, child,
            vec3_new((float)i, 0, 0), vec3_new((float)i + 1.0f, 1, 1), 0);
    }

    box_lod_tree_refresh(&tree, -1);
    log.tree = &tree;
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -4000), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.visited, 1);
    test_int(stats.proxies, 1);
    test_int(stats.drawn, 0);
    test_int(log.count, 1);
    test_int(log.nodes[0], root);
    test_uint(log.votes[0], BOX_LOD_VOTE_COLLAPSE);
    box_lod_tree_fini(&tree);
}

void Tree_a_rejected_node_stops_its_subtree(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    for (i = 0; i < 6; i ++) {
        int32_t child = box_lod_tree_add(&tree, root, (uint64_t)(10 + i));

        box_lod_tree_set_bounds(&tree, child,
            vec3_new((float)i * 3.0f, 0, 0), vec3_new((float)i * 3.0f + 1.0f, 1, 1), 0);
    }

    box_lod_tree_refresh(&tree, -1);
    log.tree = &tree;
    log.reject = 1;
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -2), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.visited, 1);
    test_int(stats.rejected, 1);
    test_int(stats.drawn, 0);
    test_int(log.count, 0);
    box_lod_tree_fini(&tree);
}

void Tree_a_closed_node_opens_on_demand(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    box_lod_tree_set_bounds(&tree, root, vec3_new(0, 0, 0), vec3_new(3, 1, 1), 0.5f);
    box_lod_tree_get(&tree, root)->flags &= ~BOX_LOD_NODE_LEAF;

    log.tree = &tree;
    log.open_node = root;
    log.open_children = 3;
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -2), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .open = tree_open, .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(log.opened, 1);
    test_int(stats.visited, 4);
    test_int(stats.drawn, 3);
    test_int(tree.count, 4);
    box_lod_tree_fini(&tree);
}

void Tree_a_node_without_children_falls_back_to_its_proxy(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    box_lod_tree_set_bounds(&tree, root, vec3_new(0, 0, 0), vec3_new(3, 1, 1), 0.5f);
    box_lod_tree_get(&tree, root)->flags &= ~BOX_LOD_NODE_LEAF;

    log.tree = &tree;
    log.open_node = -1;
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -2), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.visited, 1);
    test_int(stats.proxies, 1);
    test_int(log.count, 1);
    test_uint(log.votes[0], BOX_LOD_VOTE_COLLAPSE);
    box_lod_tree_fini(&tree);
}

void Tree_closing_a_node_releases_its_children(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    for (i = 0; i < 4; i ++) {
        int32_t child = box_lod_tree_add(&tree, root, (uint64_t)(10 + i));

        box_lod_tree_add(&tree, child, (uint64_t)(20 + i));
    }

    test_int(tree.count, 9);
    box_lod_tree_set_bounds(&tree, root, vec3_new(0, 0, 0), vec3_new(4, 1, 1), 2);
    box_lod_tree_close(&tree, root);

    test_int(tree.count, 1);
    test_int(box_lod_tree_get(&tree, root)->first_child, -1);
    test_uint(box_lod_tree_get(&tree, root)->flags & BOX_LOD_NODE_OPEN, 0);
    test_flt(box_lod_tree_get(&tree, root)->error, 2);

    int32_t again = box_lod_tree_add(&tree, root, 99);
    test_int(tree.count, 2);
    test_assert(again >= 0);
    box_lod_tree_fini(&tree);
}

void Tree_removing_a_node_drops_its_subtree(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;

    box_lod_tree_init(&tree);
    int32_t first = box_lod_tree_add(&tree, -1, 1);
    int32_t second = box_lod_tree_add(&tree, -1, 2);
    box_lod_tree_add(&tree, first, 10);
    box_lod_tree_add(&tree, first, 11);

    test_int(tree.count, 4);
    box_lod_tree_remove(&tree, first);

    test_int(tree.count, 1);
    test_assert(box_lod_tree_get(&tree, first) == NULL);
    test_int(tree.root, second);
    test_int(box_lod_tree_get(&tree, second)->next, -1);
    box_lod_tree_fini(&tree);
}

void Tree_refresh_aggregates_the_bounds_of_a_subtree(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t a = box_lod_tree_add(&tree, root, 10);
    int32_t b = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, a, vec3_new(-2, 0, 0), vec3_new(0, 2, 2), 0);
    box_lod_tree_set_bounds(&tree, b, vec3_new(4, -1, 0), vec3_new(6, 1, 1), 0);
    box_lod_tree_refresh(&tree, -1);

    const box_lod_node_t *n = box_lod_tree_get(&tree, root);
    test_flt(n->min.x, -2);
    test_flt(n->min.y, -1);
    test_flt(n->max.x, 6);
    test_flt(n->max.y, 2);
    test_int(n->leaves, 2);
    test_flt(n->weight, 12);
    test_flt(n->error, box_lod_tree_shell(n->min, n->max, 12));
    box_lod_tree_fini(&tree);
}

void Tree_refresh_averages_the_look_of_a_subtree(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    float red[4] = {1, 0, 0, 1};
    float blue[4] = {0, 0, 1, 1};

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    int32_t a = box_lod_tree_add(&tree, root, 10);
    int32_t b = box_lod_tree_add(&tree, root, 11);

    box_lod_tree_set_bounds(&tree, a, vec3_new(0, 0, 0), vec3_new(1, 1, 1), 0);
    box_lod_tree_set_bounds(&tree, b, vec3_new(2, 0, 0), vec3_new(3, 1, 1), 0);
    box_lod_tree_set_look(&tree, a, red, 0, 0.2f, 3);
    box_lod_tree_set_look(&tree, b, blue, 1, 0.6f, 1);
    box_lod_tree_refresh(&tree, -1);

    const box_lod_node_t *n = box_lod_tree_get(&tree, root);
    test_flt(n->color[0], 0.75f);
    test_flt(n->color[2], 0.25f);
    test_flt(n->material[0], 0.25f);
    test_flt(n->material[1], 0.3f);
    test_flt(n->weight, 4);
    box_lod_tree_fini(&tree);
}

void Tree_clustering_groups_loose_roots(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    int32_t i;

    box_lod_tree_init(&tree);
    for (i = 0; i < 16; i ++) {
        int32_t node = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));

        box_lod_tree_set_bounds(&tree, node,
            vec3_new((float)i * 10.0f, 0, 0), vec3_new((float)i * 10.0f + 2.0f, 2, 2), 0);
    }

    test_int(box_lod_tree_cluster(&tree, 4), 4);
    test_int(tree.count, 20);

    int32_t roots = 0;
    int32_t root = tree.root;
    while (root >= 0) {
        const box_lod_node_t *n = box_lod_tree_get(&tree, root);
        test_uint(n->flags & BOX_LOD_NODE_CLUSTER, BOX_LOD_NODE_CLUSTER);
        test_assert(n->id == 0);
        test_int(n->leaves, 4);
        roots ++;
        root = n->next;
    }

    test_int(roots, 4);
    box_lod_tree_fini(&tree);
}

void Tree_clustering_keeps_neighbours_together(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    int32_t i;

    box_lod_tree_init(&tree);
    for (i = 0; i < 8; i ++) {
        int32_t node = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));

        box_lod_tree_set_bounds(&tree, node,
            vec3_new(0, 0, (float)i * 10.0f), vec3_new(2, 2, (float)i * 10.0f + 2.0f), 0);
    }

    test_int(box_lod_tree_cluster(&tree, 4), 2);

    int32_t root = tree.root;
    while (root >= 0) {
        const box_lod_node_t *n = box_lod_tree_get(&tree, root);
        test_flt(n->max.z - n->min.z, 32);
        root = n->next;
    }

    box_lod_tree_fini(&tree);
}

void Tree_unclustering_gives_the_roots_back(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    int32_t i;

    box_lod_tree_init(&tree);
    for (i = 0; i < 12; i ++) {
        int32_t node = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));

        box_lod_tree_set_bounds(&tree, node,
            vec3_new((float)i * 10.0f, 0, 0), vec3_new((float)i * 10.0f + 2.0f, 2, 2), 0);
        box_lod_tree_add(&tree, node, (uint64_t)(100 + i));
    }

    box_lod_tree_cluster(&tree, 3);
    test_int(box_lod_tree_uncluster(&tree), 4);
    test_int(tree.count, 24);

    int32_t roots = 0;
    int32_t root = tree.root;
    while (root >= 0) {
        const box_lod_node_t *n = box_lod_tree_get(&tree, root);
        test_assert(n->id != 0);
        test_int(n->parent, -1);
        test_assert(n->first_child >= 0);
        roots ++;
        root = n->next;
    }

    test_int(roots, 12);
    box_lod_tree_fini(&tree);
}

void Tree_a_cluster_is_rejected_as_one(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;
    int32_t i;

    box_lod_tree_init(&tree);
    for (i = 0; i < 8; i ++) {
        int32_t node = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));

        box_lod_tree_set_bounds(&tree, node,
            vec3_new((float)i * 4.0f, 0, 0), vec3_new((float)i * 4.0f + 2.0f, 2, 2), 0);
    }

    box_lod_tree_cluster(&tree, 4);
    log.tree = &tree;
    log.reject = 0;

    int32_t root = tree.root;
    while (root >= 0) {
        box_lod_node_t *n = box_lod_tree_get(&tree, root);
        n->id = (uint64_t)(900 + root);
        root = n->next;
    }

    log.reject = (uint64_t)(900 + tree.root);
    box_lod_walk_desc_t desc = {
        .camera = { .position = vec3_new(0, 0, -6), .fov = 60 },
        .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
        .emit = tree_emit, .ctx = &log
    };

    box_lod_tree_walk(&tree, &desc, &stats);

    test_int(stats.rejected, 1);
    test_int(stats.visited, 6);
    test_int(stats.drawn, 4);
    box_lod_tree_fini(&tree);
}

void Tree_visited_nodes_do_not_grow_with_the_leaves(void)
{
    // lod/hierarchy_traversal
    int32_t per_root[2] = {8, 64};
    int32_t visited[2] = {0, 0};
    int32_t drawn[2] = {0, 0};
    int32_t leaves[2] = {0, 0};
    int32_t run;

    for (run = 0; run < 2; run ++) {
        box_lod_tree_t tree;
        tree_log_t log = {0};
        box_lod_walk_stats_t stats;
        int32_t i;

        box_lod_tree_init(&tree);
        for (i = 0; i < 512; i ++) {
            float x = (float)(i % 32) * 16.0f;
            float z = (float)(i / 32) * 16.0f + 2000.0f;
            int32_t root = box_lod_tree_add(&tree, -1, (uint64_t)(i + 1));
            int32_t j;

            float step = 4.0f / (float)per_root[run];
            for (j = 0; j < per_root[run]; j ++) {
                int32_t leaf = box_lod_tree_add(&tree, root,
                    (uint64_t)(i * 1000 + j + 10000));

                box_lod_tree_set_bounds(&tree, leaf, vec3_new(x, (float)j * step, z),
                    vec3_new(x + 4.0f, (float)j * step + step * 0.5f, z + 4.0f), 0);
            }
        }

        box_lod_tree_refresh(&tree, -1);
        while (box_lod_tree_cluster(&tree, 8) > 8) {
            continue;
        }

        log.tree = &tree;
        box_lod_walk_desc_t desc = {
            .camera = { .position = vec3_new(0, 3, 0), .fov = 60 },
            .viewport_px = 1080, .threshold = 2, .visible = tree_visible,
            .emit = tree_emit, .ctx = &log
        };

        box_lod_tree_walk(&tree, &desc, &stats);
        visited[run] = stats.visited;
        drawn[run] = stats.proxies;
        leaves[run] = 512 * per_root[run];
        box_lod_tree_fini(&tree);
    }

    test_int(leaves[1], leaves[0] * 8);
    test_assert(visited[0] > 0);
    test_int(visited[1], visited[0]);
    test_int(drawn[1], drawn[0]);
    test_assert(visited[0] < leaves[0]);
}

void Tree_the_band_holds_a_node_at_its_decision(void)
{
    // lod/hierarchy_traversal
    box_lod_tree_t tree;
    tree_log_t log = {0};
    box_lod_walk_stats_t stats;
    float steps[5] = {2000.0f, 60.0f, 40.0f, 60.0f, 2000.0f};
    int32_t proxies[5];
    int32_t i;

    box_lod_tree_init(&tree);
    int32_t root = box_lod_tree_add(&tree, -1, 1);
    for (i = 0; i < 3; i ++) {
        int32_t child = box_lod_tree_add(&tree, root, (uint64_t)(10 + i));

        box_lod_tree_set_bounds(&tree, child,
            vec3_new((float)i * 3.0f, 0, 0), vec3_new((float)i * 3.0f + 1.0f, 1, 1), 0);
    }

    box_lod_tree_refresh(&tree, -1);
    log.tree = &tree;

    for (i = 0; i < 5; i ++) {
        box_lod_walk_desc_t desc = {
            .camera = { .position = vec3_new(1, 0, -steps[i]), .fov = 60 },
            .viewport_px = 1080, .threshold = 2, .hysteresis = 0.25f,
            .visible = tree_visible, .emit = tree_emit, .ctx = &log,
            .stamp = (uint32_t)(i + 1)
        };

        log.count = 0;
        log.visits = 0;
        box_lod_tree_walk(&tree, &desc, &stats);
        proxies[i] = stats.proxies;
    }

    test_int(proxies[0], 1);
    test_int(proxies[1], 1);
    test_int(proxies[2], 0);
    test_int(proxies[3], 0);
    test_int(proxies[4], 1);
    box_lod_tree_fini(&tree);
}
