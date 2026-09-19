/**
 * @file lod/tree.c
 * @brief The node hierarchy the LOD traversal walks and the walk itself.
 *
 * A node is a place in the tree, not an entity: children are a linked list, so
 * a subtree is materialised and released without moving another node, and a
 * released index is handed out again. The walk starts at the roots and stops at
 * every node the culling of the view rejects or whose projected error is under
 * the threshold, so the leaves below such a node are never visited.
 */

#include "lod_internal.h"
#include <stdlib.h>

/** Value the free list leaves in the leaf count of a node that is not live */
#define BOX_LOD_TREE_FREE (-1)

/** Coordinate a running aggregate starts its minimum corner at */
#define BOX_LOD_TREE_HUGE (1e30f)

/** One root and the key the clustering sorts it by */
typedef struct box_lod_sort_t {
    float key;     /**< Center of the root along the axis it is sorted on */
    int32_t node;  /**< Index of the root */
} box_lod_sort_t;

/** Order two roots by their key, breaking every tie by their index.
 *
 * @param a The first root
 * @param b The second root
 * @return Negative when a sorts first, positive when b does
 */
static int box_lod_sort_cmp(
    const void *a,
    const void *b)
{
    const box_lod_sort_t *l = a;
    const box_lod_sort_t *r = b;
    int result = 0;
    if (l->key < r->key) {
        result = -1;
    } else if (l->key > r->key) {
        result = 1;
    } else {
        result = l->node < r->node ? -1 : (l->node > r->node);
    }

    return result;
}

/** The node an index names, without checking that it is live.
 *
 * @param tree The tree
 * @param node Index of the node
 * @return The node
 */
static box_lod_node_t* box_lod_node(
    const box_lod_tree_t *tree,
    int32_t node)
{
    return ecs_vec_get_t(&tree->nodes, box_lod_node_t, node);
}

void box_lod_tree_init(
    box_lod_tree_t *tree)
{
    ecs_os_init();
    memset(tree, 0, sizeof(box_lod_tree_t));
    ecs_vec_init_t(NULL, &tree->nodes, box_lod_node_t, 0);
    ecs_vec_init_t(NULL, &tree->free, int32_t, 0);
    ecs_vec_init_t(NULL, &tree->stack, int32_t, 0);
    ecs_vec_init_t(NULL, &tree->walk, int32_t, 0);
    ecs_vec_init_t(NULL, &tree->order, int32_t, 0);
    tree->root = -1;
}

void box_lod_tree_fini(
    box_lod_tree_t *tree)
{
    ecs_vec_fini_t(NULL, &tree->nodes, box_lod_node_t);
    ecs_vec_fini_t(NULL, &tree->free, int32_t);
    ecs_vec_fini_t(NULL, &tree->stack, int32_t);
    ecs_vec_fini_t(NULL, &tree->walk, int32_t);
    ecs_vec_fini_t(NULL, &tree->order, int32_t);
    tree->root = -1;
    tree->count = 0;
}

void box_lod_tree_clear(
    box_lod_tree_t *tree)
{
    ecs_vec_clear(&tree->nodes);
    ecs_vec_clear(&tree->free);
    ecs_vec_clear(&tree->stack);
    ecs_vec_clear(&tree->walk);
    ecs_vec_clear(&tree->order);
    tree->root = -1;
    tree->count = 0;
}

box_lod_node_t* box_lod_tree_get(
    const box_lod_tree_t *tree,
    int32_t node)
{
    box_lod_node_t *result = NULL;
    if (node < 0 || node >= ecs_vec_count(&tree->nodes)) {
        goto done;
    }

    result = box_lod_node(tree, node);
    if (result->leaves == BOX_LOD_TREE_FREE) {
        result = NULL;
    }

done:
    return result;
}

int32_t box_lod_tree_add(
    box_lod_tree_t *tree,
    int32_t parent,
    uint64_t id)
{
    int32_t result;
    int32_t free_count = ecs_vec_count(&tree->free);
    if (free_count) {
        result = ecs_vec_get_t(&tree->free, int32_t, free_count - 1)[0];
        ecs_vec_remove_last(&tree->free);
    } else {
        result = ecs_vec_count(&tree->nodes);
        ecs_vec_append_t(NULL, &tree->nodes, box_lod_node_t);
    }

    box_lod_node_t *n = box_lod_node(tree, result);
    memset(n, 0, sizeof(box_lod_node_t));
    n->id = id;
    n->parent = parent;
    n->first_child = -1;
    n->leaves = 1;
    n->flags = BOX_LOD_NODE_LEAF;
    if (parent < 0) {
        n->next = tree->root;
        tree->root = result;
    } else {
        box_lod_node_t *p = box_lod_node(tree, parent);

        n->next = p->first_child;
        n->flags |= p->flags & BOX_LOD_NODE_PIN;
        p->first_child = result;
        p->flags = (p->flags & ~BOX_LOD_NODE_LEAF) | BOX_LOD_NODE_OPEN;
    }

    tree->count ++;

    return result;
}

/** Give the subtree of a node back to the free list, the node included.
 *
 * @param tree The tree
 * @param node Index of the node
 */
static void box_lod_tree_free(
    box_lod_tree_t *tree,
    int32_t node)
{
    ecs_vec_clear(&tree->stack);
    ecs_vec_append_t(NULL, &tree->stack, int32_t)[0] = node;
    while (ecs_vec_count(&tree->stack)) {
        int32_t last = ecs_vec_count(&tree->stack) - 1;
        int32_t cur = ecs_vec_get_t(&tree->stack, int32_t, last)[0];
        ecs_vec_remove_last(&tree->stack);

        box_lod_node_t *n = box_lod_node(tree, cur);
        int32_t child = n->first_child;
        while (child >= 0) {
            int32_t next = box_lod_node(tree, child)->next;

            ecs_vec_append_t(NULL, &tree->stack, int32_t)[0] = child;
            child = next;
        }

        n->leaves = BOX_LOD_TREE_FREE;
        n->first_child = -1;
        n->next = -1;
        n->flags = 0;
        ecs_vec_append_t(NULL, &tree->free, int32_t)[0] = cur;
        tree->count --;
    }
}

/** Take a node out of the child list of its parent, or out of the root list.
 *
 * @param tree The tree
 * @param node Index of the node
 */
static void box_lod_tree_unlink(
    box_lod_tree_t *tree,
    int32_t node)
{
    box_lod_node_t *n = box_lod_node(tree, node);
    int32_t *head = &tree->root;
    if (n->parent >= 0) {
        head = &box_lod_node(tree, n->parent)->first_child;
    }

    int32_t cur = head[0];
    if (cur == node) {
        head[0] = n->next;
        goto done;
    }

    while (cur >= 0) {
        box_lod_node_t *c = box_lod_node(tree, cur);
        if (c->next == node) {
            c->next = n->next;
            break;
        }

        cur = c->next;
    }

done:
    n->next = -1;
}

void box_lod_tree_remove(
    box_lod_tree_t *tree,
    int32_t node)
{
    if (!box_lod_tree_get(tree, node)) {
        goto done;
    }

    box_lod_tree_unlink(tree, node);
    box_lod_tree_free(tree, node);
done:
    return;
}

void box_lod_tree_close(
    box_lod_tree_t *tree,
    int32_t node)
{
    box_lod_node_t *n = box_lod_tree_get(tree, node);
    if (!n) {
        goto done;
    }

    int32_t child = n->first_child;
    while (child >= 0) {
        int32_t next = box_lod_node(tree, child)->next;

        box_lod_tree_free(tree, child);
        child = next;
    }

    n = box_lod_node(tree, node);
    n->first_child = -1;
    n->flags &= ~BOX_LOD_NODE_OPEN;
done:
    return;
}

void box_lod_tree_set_bounds(
    box_lod_tree_t *tree,
    int32_t node,
    vec3 min,
    vec3 max,
    float error)
{
    box_lod_node_t *n = box_lod_tree_get(tree, node);
    if (!n) {
        goto done;
    }

    vec3 size;
    n->min = vec3_min(min, max);
    n->max = vec3_max(min, max);
    n->error = error > 0.0f ? error : 0.0f;
    size = vec3_sub(n->max, n->min);
    n->weight = size.x * size.y * size.z;
done:
    return;
}

void box_lod_tree_set_look(
    box_lod_tree_t *tree,
    int32_t node,
    const float *color,
    float metallic,
    float roughness,
    float weight)
{
    box_lod_node_t *n = box_lod_tree_get(tree, node);
    if (!n) {
        goto done;
    }

    int32_t c;
    for (c = 0; c < 4; c ++) {
        n->color[c] = color ? color[c] : 1.0f;
    }

    n->material[0] = metallic;
    n->material[1] = roughness;
    n->weight = weight > 0.0f ? weight : 1.0f;
done:
    return;
}

/** Fill the order vector with the live nodes of a subtree, parents first.
 *
 * @param tree The tree
 * @param from Index of the node to start at, negative for every root
 */
static void box_lod_tree_order(
    box_lod_tree_t *tree,
    int32_t from)
{
    ecs_vec_clear(&tree->order);
    ecs_vec_clear(&tree->stack);
    if (from >= 0) {
        ecs_vec_append_t(NULL, &tree->stack, int32_t)[0] = from;
    }

    int32_t root = from >= 0 ? -1 : tree->root;
    while (root >= 0) {
        ecs_vec_append_t(NULL, &tree->stack, int32_t)[0] = root;
        root = box_lod_node(tree, root)->next;
    }

    while (ecs_vec_count(&tree->stack)) {
        int32_t last = ecs_vec_count(&tree->stack) - 1;
        int32_t cur = ecs_vec_get_t(&tree->stack, int32_t, last)[0];
        ecs_vec_remove_last(&tree->stack);
        ecs_vec_append_t(NULL, &tree->order, int32_t)[0] = cur;

        int32_t child = box_lod_node(tree, cur)->first_child;
        while (child >= 0) {
            ecs_vec_append_t(NULL, &tree->stack, int32_t)[0] = child;
            child = box_lod_node(tree, child)->next;
        }
    }
}

float box_lod_tree_shell(
    vec3 min,
    vec3 max,
    float filled)
{
    vec3 size = vec3_sub(max, min);
    float volume = size.x * size.y * size.z;
    float area = 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
    float missed = volume - filled;
    float result = 0.0f;
    if (area > 0.0f && missed > 0.0f) {
        result = missed / area;
    }

    return result;
}

/** Size a node adds to what its parent shows, in world units.
 *
 * The size is the edge of the square with the area of the widest face of the
 * bounds, so a bolt and a long thin railing both count for what they cover and
 * not for how far they reach, and the material contrast against the parent
 * scales it: a part that carries the color of what it sits on adds a quarter of
 * its size, a part that stands out adds all of it.
 *
 * @param n The node
 * @param parent The parent of the node, NULL for a root
 * @return The contribution in world units
 */
static float box_lod_tree_contribution(
    const box_lod_node_t *n,
    const box_lod_node_t *parent)
{
    vec3 extent = vec3_sub(n->max, n->min);
    float axis[3] = { extent.x, extent.y, extent.z };
    float contrast = 0.0f;
    int32_t c;
    for (c = 0; c < 2; c ++) {
        int32_t d;
        for (d = c + 1; d < 3; d ++) {
            if (axis[d] > axis[c]) {
                float swap = axis[c];

                axis[c] = axis[d];
                axis[d] = swap;
            }
        }
    }

    for (c = 0; parent && c < 3; c ++) {
        float diff = n->color[c] - parent->color[c];

        diff = diff < 0.0f ? -diff : diff;
        contrast = diff > contrast ? diff : contrast;
    }

    contrast = contrast > 1.0f ? 1.0f : contrast;

    return sqrtf(axis[0] * axis[1]) *
        (BOX_LOD_CONTRAST_FLOOR + (1.0f - BOX_LOD_CONTRAST_FLOOR) * contrast);
}

/** Turn the weighted sums an interior node collected into its averages.
 *
 * @param n The node
 */
static void box_lod_tree_normalize(
    box_lod_node_t *n)
{
    if (!n->leaves) {
        n->min = vec3_new(0, 0, 0);
        n->max = vec3_new(0, 0, 0);
    }

    float shell = box_lod_tree_shell(n->min, n->max, n->weight);
    if (shell > n->error) {
        n->error = shell;
    }

    if (n->weight <= 0.0f) {
        goto done;
    }

    float inv = 1.0f / n->weight;
    int32_t c;
    for (c = 0; c < 4; c ++) {
        n->color[c] *= inv;
    }

    n->material[0] *= inv;
    n->material[1] *= inv;
done:
    return;
}

void box_lod_tree_refresh(
    box_lod_tree_t *tree,
    int32_t node)
{
    box_lod_tree_order(tree, node);

    int32_t count = ecs_vec_count(&tree->order);
    int32_t *order = ecs_vec_first_t(&tree->order, int32_t);
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_lod_node_t *n = box_lod_node(tree, order[i]);
        if (!(n->flags & BOX_LOD_NODE_OPEN)) {
            continue;
        }

        n->leaves = 0;
        n->error = 0.0f;
        n->weight = 0.0f;
        memset(n->color, 0, sizeof(n->color));
        memset(n->material, 0, sizeof(n->material));
        n->min = vec3_new(BOX_LOD_TREE_HUGE, BOX_LOD_TREE_HUGE, BOX_LOD_TREE_HUGE);
        n->max = vec3_new(-BOX_LOD_TREE_HUGE, -BOX_LOD_TREE_HUGE, -BOX_LOD_TREE_HUGE);
    }

    for (i = count - 1; i >= 0; i --) {
        box_lod_node_t *n = box_lod_node(tree, order[i]);
        if (n->flags & BOX_LOD_NODE_OPEN) {
            box_lod_tree_normalize(n);
        }

        if (n->parent < 0) {
            continue;
        }

        if (!i && node >= 0) {
            continue;
        }

        box_lod_node_t *p = box_lod_node(tree, n->parent);
        if (!(p->flags & BOX_LOD_NODE_OPEN)) {
            continue;
        }

        float error = n->error;
        float weight = n->weight > 0.0f ? n->weight : 1.0f;
        int32_t c;

        p->min = vec3_min(p->min, n->min);
        p->max = vec3_max(p->max, n->max);
        p->leaves += n->leaves;
        p->error = error > p->error ? error : p->error;
        p->weight += weight;
        for (c = 0; c < 4; c ++) {
            p->color[c] += n->color[c] * weight;
        }

        p->material[0] += n->material[0] * weight;
        p->material[1] += n->material[1] * weight;
    }

    for (i = 0; i < count; i ++) {
        box_lod_node_t *n = box_lod_node(tree, order[i]);
        const box_lod_node_t *p = n->parent >= 0 ? box_lod_node(tree, n->parent) : NULL;

        n->contribution = box_lod_tree_contribution(n, p);
    }
}

/** Sort the roots of a tree along the axis their centers spread over most.
 *
 * @param tree The tree
 * @param out Receives one entry per root, in the order they cluster in
 * @return The number of roots
 */
static int32_t box_lod_tree_sort_roots(
    box_lod_tree_t *tree,
    box_lod_sort_t **out)
{
    int32_t result = 0;
    int32_t root = tree->root;
    while (root >= 0) {
        result ++;
        root = box_lod_node(tree, root)->next;
    }

    if (!result) {
        goto done;
    }

    box_lod_sort_t *keys = ecs_os_malloc_n(box_lod_sort_t, result);
    vec3 lo = vec3_new(BOX_LOD_TREE_HUGE, BOX_LOD_TREE_HUGE, BOX_LOD_TREE_HUGE);
    vec3 hi = vec3_new(-BOX_LOD_TREE_HUGE, -BOX_LOD_TREE_HUGE, -BOX_LOD_TREE_HUGE);
    int32_t i = 0;

    root = tree->root;
    while (root >= 0) {
        box_lod_node_t *n = box_lod_node(tree, root);
        vec3 center = vec3_scale(vec3_add(n->min, n->max), 0.5f);

        lo = vec3_min(lo, center);
        hi = vec3_max(hi, center);
        keys[i].node = root;
        keys[i].key = center.x;
        i ++;
        root = n->next;
    }

    vec3 span = vec3_sub(hi, lo);
    int32_t axis = span.y > span.x ? 1 : 0;
    if (span.z > (axis ? span.y : span.x)) {
        axis = 2;
    }

    for (i = 0; i < result; i ++) {
        box_lod_node_t *n = box_lod_node(tree, keys[i].node);
        vec3 center = vec3_scale(vec3_add(n->min, n->max), 0.5f);

        keys[i].key = axis ? (axis == 1 ? center.y : center.z) : center.x;
    }

    qsort(keys, (size_t)result, sizeof(box_lod_sort_t), box_lod_sort_cmp);
    out[0] = keys;
done:
    return result;
}

int32_t box_lod_tree_cluster(
    box_lod_tree_t *tree,
    int32_t group)
{
    int32_t result = 0;
    int32_t size = group > 1 ? group : BOX_LOD_CLUSTER_SIZE;
    box_lod_sort_t *keys = NULL;
    int32_t count = box_lod_tree_sort_roots(tree, &keys);
    if (count <= size) {
        goto done;
    }

    tree->root = -1;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_lod_node_t *n = box_lod_node(tree, keys[i].node);

        n->parent = -1;
        n->next = -1;
    }

    for (i = 0; i < count; i += size) {
        int32_t cluster = box_lod_tree_add(tree, -1, 0);
        int32_t last = i + size < count ? i + size : count;
        int32_t j;

        box_lod_node(tree, cluster)->flags |= BOX_LOD_NODE_CLUSTER;
        for (j = last - 1; j >= i; j --) {
            box_lod_node_t *n = box_lod_node(tree, keys[j].node);

            n->parent = cluster;
            n->next = box_lod_node(tree, cluster)->first_child;
            box_lod_node(tree, cluster)->first_child = keys[j].node;
            box_lod_node(tree, cluster)->flags &= ~BOX_LOD_NODE_LEAF;
            box_lod_node(tree, cluster)->flags |= BOX_LOD_NODE_OPEN;
        }

        result ++;
    }

    box_lod_tree_refresh(tree, -1);
done:
    if (keys) {
        ecs_os_free(keys);
    }

    return result;
}

int32_t box_lod_tree_uncluster(
    box_lod_tree_t *tree)
{
    int32_t result = 0;
    box_lod_tree_order(tree, -1);

    int32_t count = ecs_vec_count(&tree->order);
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t index = ecs_vec_get_t(&tree->order, int32_t, i)[0];
        box_lod_node_t *n = box_lod_node(tree, index);
        if (n->flags & BOX_LOD_NODE_CLUSTER) {
            continue;
        }

        int32_t parent = n->parent;
        if (parent >= 0 && !(box_lod_node(tree, parent)->flags & BOX_LOD_NODE_CLUSTER)) {
            continue;
        }

        n->parent = -1;
    }

    for (i = count - 1; i >= 0; i --) {
        int32_t index = ecs_vec_get_t(&tree->order, int32_t, i)[0];
        box_lod_node_t *n = box_lod_node(tree, index);
        if (!(n->flags & BOX_LOD_NODE_CLUSTER)) {
            continue;
        }

        n->leaves = BOX_LOD_TREE_FREE;
        n->first_child = -1;
        n->next = -1;
        n->flags = 0;
        ecs_vec_append_t(NULL, &tree->free, int32_t)[0] = index;
        tree->count --;
        result ++;
    }

    if (!result) {
        goto done;
    }

    tree->root = -1;
    for (i = count - 1; i >= 0; i --) {
        int32_t index = ecs_vec_get_t(&tree->order, int32_t, i)[0];
        box_lod_node_t *n = box_lod_node(tree, index);
        if (n->leaves == BOX_LOD_TREE_FREE || n->parent >= 0) {
            continue;
        }

        n->next = tree->root;
        tree->root = index;
    }

done:
    return result;
}

/** Decide what the walk does with one interior node.
 *
 * @param n The node
 * @param node Index of the node, which the level callback names it by
 * @param desc The view and its threshold
 * @param scale Pixels one world unit covers at the bounds of the node
 * @return True when the node is replaced by its proxy
 */
static bool box_lod_walk_collapse(
    box_lod_node_t *n,
    int32_t node,
    const box_lod_walk_desc_t *desc,
    float scale)
{
    float error[BOX_LOD_LEVEL_MAX] = {0};
    float error_px[BOX_LOD_LEVEL_MAX + 1] = {0};
    int32_t count = desc->levels ? desc->levels(desc->ctx, node, error) : 0;
    int32_t i;

    if (count <= 0) {
        count = 1;
        error[0] = n->error;
    }

    for (i = 0; i < count; i ++) {
        error_px[i + 1] = scale * error[i];
    }

    box_lod_select_desc_t select = {
        .error_px = error_px, .count = count + 1, .current = n->level,
        .threshold = desc->threshold, .hysteresis = desc->hysteresis
    };

    int32_t level = box_lod_select(&select);
    bool result = level > 0;
    bool finer = (n->votes & BOX_LOD_VOTE_COLLAPSE) && n->level > 0 && n->level < level;

    if (!result) {
        n->level = 0;
    } else if (!(n->votes & BOX_LOD_VOTE_DESCEND)) {
        n->level = finer ? n->level : level;
    }

    return result;
}

/** Decide whether a node contributes too little to be worth drawing.
 *
 * A root has nothing to be insignificant against and a node the author pinned
 * is never dropped, which holds for everything below it because a child
 * inherits the pin of its parent.
 *
 * @param n The node
 * @param desc The view and its threshold
 * @param scale Pixels one world unit covers at the bounds of the node
 * @return True when the node and its subtree are dropped
 */
static bool box_lod_walk_prune(
    const box_lod_node_t *n,
    const box_lod_walk_desc_t *desc,
    float scale)
{
    return desc->prune_px > 0.0f && n->parent >= 0 &&
        !(n->flags & BOX_LOD_NODE_PIN) && scale * n->contribution < desc->prune_px;
}

/** Record a decision on a node and hand it to the caller.
 *
 * @param desc The walk
 * @param n The node
 * @param node Index of the node
 * @param vote The BOX_LOD_VOTE bit the walk decided on
 */
static void box_lod_walk_emit(
    const box_lod_walk_desc_t *desc,
    box_lod_node_t *n,
    int32_t node,
    uint32_t vote)
{
    n->votes |= vote;
    if (desc->emit) {
        desc->emit(desc->ctx, node, vote);
    }
}

void box_lod_tree_walk(
    box_lod_tree_t *tree,
    const box_lod_walk_desc_t *desc,
    box_lod_walk_stats_t *out)
{
    box_lod_walk_stats_t stats = {0};
    uint32_t stamp = desc->stamp ? desc->stamp : ++ tree->stamp;
    int32_t root = tree->root;

    ecs_vec_clear(&tree->walk);
    while (root >= 0) {
        ecs_vec_append_t(NULL, &tree->walk, int32_t)[0] = root;
        root = box_lod_node(tree, root)->next;
    }

    while (ecs_vec_count(&tree->walk)) {
        int32_t last = ecs_vec_count(&tree->walk) - 1;
        int32_t cur = ecs_vec_get_t(&tree->walk, int32_t, last)[0];
        ecs_vec_remove_last(&tree->walk);

        box_lod_node_t *n = box_lod_node(tree, cur);
        if (n->stamp != stamp) {
            n->stamp = stamp;
            n->votes = 0;
        }

        stats.visited ++;
        if (desc->visible && !desc->visible(desc->ctx, n)) {
            stats.rejected ++;
            continue;
        }

        box_lod_bounds_t bounds = { .min = n->min, .max = n->max };
        float scale = box_lod_error_scale(&bounds, &desc->camera, desc->viewport_px);
        bool prune = box_lod_walk_prune(n, desc, scale);
        if (n->flags & BOX_LOD_NODE_LEAF) {
            stats.pruned += prune;
            stats.drawn += !prune;
            box_lod_walk_emit(desc, n, cur,
                prune ? BOX_LOD_VOTE_PRUNE : BOX_LOD_VOTE_DRAW);
            continue;
        }

        bool collapse = box_lod_walk_collapse(n, cur, desc, scale);
        if (!collapse && desc->hidden && desc->hidden(desc->ctx, n)) {
            collapse = true;
            stats.occluded ++;
        }

        if (!collapse && !prune && !(n->flags & BOX_LOD_NODE_OPEN)) {
            collapse = !desc->open || !desc->open(desc->ctx, cur);
            n = box_lod_node(tree, cur);
        }

        if (collapse) {
            stats.proxies ++;
            box_lod_walk_emit(desc, n, cur, BOX_LOD_VOTE_COLLAPSE);
            continue;
        }

        if (prune) {
            stats.pruned ++;
            box_lod_walk_emit(desc, n, cur, BOX_LOD_VOTE_PRUNE);
            continue;
        }

        box_lod_walk_emit(desc, n, cur, BOX_LOD_VOTE_DESCEND);
        int32_t child = n->first_child;
        while (child >= 0) {
            ecs_vec_append_t(NULL, &tree->walk, int32_t)[0] = child;
            child = box_lod_node(tree, child)->next;
        }
    }

    if (out) {
        out[0] = stats;
    }
}
