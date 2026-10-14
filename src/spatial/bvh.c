/**
 * @file spatial/bvh.c
 * @brief Bounding volume hierarchy: top down build, incremental insert and remove, walks.
 */

#include "bvh.h"

/** Bins of the surface area heuristic of a top down build */
#define BOX_BVH_BINS (16)

/** Ancestors a leaf without margin may grow before it is inserted again instead */
#define BOX_BVH_GROW_LEVELS (4)

/** Depth below which a build stops weighing splits and halves the leaves */
#define BOX_BVH_MAX_SAH_DEPTH (48)

/** Entries a walk keeps on the stack before it allocates */
#define BOX_BVH_STACK (64)

/** Node a walk still has to visit */
typedef struct box_bvh_entry_t {
    int32_t node; /**< The node */
    float t;      /**< Distance at which a ray enters the node, 0 for other walks */
} box_bvh_entry_t;

/** Stack of a walk, on the C stack until it outgrows it */
typedef struct box_bvh_stack_t {
    box_bvh_entry_t local[BOX_BVH_STACK]; /**< Initial storage */
    box_bvh_entry_t *data;                /**< Current storage */
    int32_t count;                        /**< Entries on the stack */
    int32_t capacity;                     /**< Entries the storage holds */
} box_bvh_stack_t;

/** Bin of a top down build */
typedef struct box_bvh_bin_t {
    vec3 min;      /**< Minimum corner of the leaves in the bin */
    vec3 max;      /**< Maximum corner of the leaves in the bin */
    int32_t count; /**< Leaves in the bin */
} box_bvh_bin_t;

/** Set up an empty stack.
 *
 * @param s The stack
 */
static void box_bvh_stack_init(
    box_bvh_stack_t *s)
{
    s->data = s->local;
    s->count = 0;
    s->capacity = BOX_BVH_STACK;
}

/** Push a node on a stack.
 *
 * @param s The stack
 * @param node The node
 * @param t Distance at which a ray enters the node
 */
static void box_bvh_stack_push(
    box_bvh_stack_t *s,
    int32_t node,
    float t)
{
    if (s->count == s->capacity) {
        int32_t capacity = s->capacity * 2;
        box_bvh_entry_t *data = ecs_os_malloc_n(box_bvh_entry_t, capacity);
        ecs_os_memcpy_n(data, s->data, box_bvh_entry_t, s->count);
        if (s->data != s->local) {
            ecs_os_free(s->data);
        }

        s->data = data;
        s->capacity = capacity;
    }

    s->data[s->count ++] = (box_bvh_entry_t){ .node = node, .t = t };
}

/** Free the storage of a stack.
 *
 * @param s The stack
 */
static void box_bvh_stack_fini(
    box_bvh_stack_t *s)
{
    if (s->data != s->local) {
        ecs_os_free(s->data);
    }
}

/** Write the union of two boxes.
 *
 * @param a_min Minimum corner of the first box
 * @param a_max Maximum corner of the first box
 * @param b_min Minimum corner of the second box
 * @param b_max Maximum corner of the second box
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
static void box_bvh_union(
    const vec3 a_min,
    const vec3 a_max,
    const vec3 b_min,
    const vec3 b_max,
    vec3 min,
    vec3 max)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        min[i] = a_min[i] < b_min[i] ? a_min[i] : b_min[i];
        max[i] = a_max[i] > b_max[i] ? a_max[i] : b_max[i];
    }
}

/** Get half the surface area of a box.
 *
 * @param min Minimum corner
 * @param max Maximum corner
 * @return The area
 */
static float box_bvh_area(
    const vec3 min,
    const vec3 max)
{
    float x = max[0] - min[0], y = max[1] - min[1], z = max[2] - min[2];

    return x * y + y * z + z * x;
}

/** Get half the surface area of the union of two boxes.
 *
 * @param a_min Minimum corner of the first box
 * @param a_max Maximum corner of the first box
 * @param b_min Minimum corner of the second box
 * @param b_max Maximum corner of the second box
 * @return The area
 */
static float box_bvh_union_area(
    const vec3 a_min,
    const vec3 a_max,
    const vec3 b_min,
    const vec3 b_max)
{
    vec3 min, max;
    box_bvh_union(a_min, a_max, b_min, b_max, min, max);

    return box_bvh_area(min, max);
}

/** Test whether a box contains another one.
 *
 * @param o_min Minimum corner of the outer box
 * @param o_max Maximum corner of the outer box
 * @param i_min Minimum corner of the inner box
 * @param i_max Maximum corner of the inner box
 * @return true when the outer box contains the inner box
 */
static bool box_bvh_contains(
    const vec3 o_min,
    const vec3 o_max,
    const vec3 i_min,
    const vec3 i_max)
{
    return o_min[0] <= i_min[0] && o_min[1] <= i_min[1] && o_min[2] <= i_min[2] &&
        o_max[0] >= i_max[0] && o_max[1] >= i_max[1] && o_max[2] >= i_max[2];
}

box_bvh_node_t* box_bvh_get(
    const box_bvh_t *tree,
    int32_t node)
{
    return ecs_vec_get_t(&tree->nodes, box_bvh_node_t, node);
}

void box_bvh_init(
    box_bvh_t *tree)
{
    ecs_vec_init_t(NULL, &tree->nodes, box_bvh_node_t, 0);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->leaves = 0;
}

void box_bvh_fini(
    box_bvh_t *tree)
{
    ecs_vec_fini_t(NULL, &tree->nodes, box_bvh_node_t);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->leaves = 0;
}

/** Take a node from the free list or grow the node array.
 *
 * @param tree The tree
 * @return The node, whose fields the caller sets
 */
static int32_t box_bvh_alloc(
    box_bvh_t *tree)
{
    int32_t result = tree->free;
    if (result != BOX_BVH_NULL) {
        tree->free = box_bvh_get(tree, result)->parent;
    } else {
        result = ecs_vec_count(&tree->nodes);
        ecs_vec_append_t(NULL, &tree->nodes, box_bvh_node_t);
    }

    box_bvh_node_t *n = box_bvh_get(tree, result);
    n->parent = BOX_BVH_NULL;
    n->left = BOX_BVH_NULL;
    n->right = BOX_BVH_NULL;
    n->height = 0;

    return result;
}

/** Put a node on the free list.
 *
 * @param tree The tree
 * @param node The node
 */
static void box_bvh_release(
    box_bvh_t *tree,
    int32_t node)
{
    box_bvh_node_t *n = box_bvh_get(tree, node);
    n->parent = tree->free;
    n->height = -1;
    tree->free = node;
}

/** Recompute the box and height of an inner node from its children.
 *
 * @param tree The tree
 * @param node The node
 */
static void box_bvh_refit(
    box_bvh_t *tree,
    int32_t node)
{
    box_bvh_node_t *n = box_bvh_get(tree, node);
    box_bvh_node_t *l = box_bvh_get(tree, n->left);
    box_bvh_node_t *r = box_bvh_get(tree, n->right);
    box_bvh_union(l->min, l->max, r->min, r->max, n->min, n->max);
    n->height = 1 + (l->height > r->height ? l->height : r->height);
}

/** Point the parent of a node, or the root, at a node that takes its place.
 *
 * @param tree The tree
 * @param parent Parent of the old node, BOX_BVH_NULL when it was the root
 * @param old The node that is replaced
 * @param node The node that takes its place
 */
static void box_bvh_relink(
    box_bvh_t *tree,
    int32_t parent,
    int32_t old,
    int32_t node)
{
    box_bvh_get(tree, node)->parent = parent;
    if (parent == BOX_BVH_NULL) {
        tree->root = node;
    } else {
        box_bvh_node_t *p = box_bvh_get(tree, parent);
        if (p->left == old) {
            p->left = node;
        } else {
            p->right = node;
        }
    }
}

/** Rotate the taller child of a node above it.
 *
 * @param tree The tree
 * @param a The node
 * @param up The taller child, which takes the place of the node
 * @param stay The other child of the node
 * @param up_is_right The taller child is the right child of the node
 * @return The child that took the place of the node
 */
static int32_t box_bvh_rotate(
    box_bvh_t *tree,
    int32_t a,
    int32_t up,
    int32_t stay,
    bool up_is_right)
{
    box_bvh_node_t *u = box_bvh_get(tree, up);
    int32_t f = u->left, g = u->right;
    int32_t keep = box_bvh_get(tree, f)->height > box_bvh_get(tree, g)->height ? f : g;
    int32_t give = keep == f ? g : f;
    box_bvh_relink(tree, box_bvh_get(tree, a)->parent, a, up);
    u = box_bvh_get(tree, up);
    u->left = a;
    u->right = keep;
    box_bvh_node_t *n = box_bvh_get(tree, a);
    n->parent = up;
    if (up_is_right) {
        n->left = stay;
        n->right = give;
    } else {
        n->left = give;
        n->right = stay;
    }

    box_bvh_get(tree, give)->parent = a;
    box_bvh_refit(tree, a);
    box_bvh_refit(tree, up);

    return up;
}

/** Rotate a node whose children differ in height by more than one.
 *
 * @param tree The tree
 * @param a The node
 * @return The node now in the place of a
 */
static int32_t box_bvh_balance(
    box_bvh_t *tree,
    int32_t a)
{
    int32_t result = a;
    box_bvh_node_t *n = box_bvh_get(tree, a);
    if (n->left == BOX_BVH_NULL || n->height < 2) {
        goto done;
    }

    int32_t b = n->left, c = n->right;
    int32_t balance = box_bvh_get(tree, c)->height - box_bvh_get(tree, b)->height;
    if (balance > 1) {
        result = box_bvh_rotate(tree, a, c, b, true);
    } else if (balance < -1) {
        result = box_bvh_rotate(tree, a, b, c, false);
    }

done:
    return result;
}

/** Refit and balance the ancestors of a node up to the root.
 *
 * @param tree The tree
 * @param node The first ancestor to repair
 */
static void box_bvh_repair(
    box_bvh_t *tree,
    int32_t node)
{
    while (node != BOX_BVH_NULL) {
        node = box_bvh_balance(tree, node);
        box_bvh_refit(tree, node);
        node = box_bvh_get(tree, node)->parent;
    }
}

/** Find the node a new leaf grows the tree least next to.
 *
 * @param tree The tree
 * @param min Minimum corner of the leaf
 * @param max Maximum corner of the leaf
 * @return The sibling
 */
static int32_t box_bvh_sibling(
    const box_bvh_t *tree,
    const vec3 min,
    const vec3 max)
{
    int32_t node = tree->root;
    box_bvh_node_t *n = box_bvh_get(tree, node);
    while (n->left != BOX_BVH_NULL) {
        float area = box_bvh_area(n->min, n->max);
        float combined = box_bvh_union_area(n->min, n->max, min, max);
        float cost = 2.0f * combined;
        float inherit = 2.0f * (combined - area);
        box_bvh_node_t *l = box_bvh_get(tree, n->left);
        box_bvh_node_t *r = box_bvh_get(tree, n->right);
        float cost_l = box_bvh_union_area(l->min, l->max, min, max) + inherit;
        float cost_r = box_bvh_union_area(r->min, r->max, min, max) + inherit;
        if (l->left != BOX_BVH_NULL) {
            cost_l -= box_bvh_area(l->min, l->max);
        }

        if (r->left != BOX_BVH_NULL) {
            cost_r -= box_bvh_area(r->min, r->max);
        }

        if (cost < cost_l && cost < cost_r) {
            break;
        }

        node = cost_l < cost_r ? n->left : n->right;
        n = box_bvh_get(tree, node);
    }

    return node;
}

int32_t box_bvh_insert(
    box_bvh_t *tree,
    const vec3 min,
    const vec3 max,
    int32_t item)
{
    int32_t leaf = box_bvh_alloc(tree);
    box_bvh_node_t *l = box_bvh_get(tree, leaf);
    glm_vec3_copy(ECS_CONST_CAST(float*, min), l->min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), l->max);
    l->right = item;
    tree->leaves ++;
    if (tree->root == BOX_BVH_NULL) {
        tree->root = leaf;
        goto done;
    }

    int32_t sibling = box_bvh_sibling(tree, min, max);
    int32_t parent = box_bvh_alloc(tree);
    box_bvh_relink(tree, box_bvh_get(tree, sibling)->parent, sibling, parent);
    box_bvh_node_t *p = box_bvh_get(tree, parent);
    p->left = sibling;
    p->right = leaf;
    box_bvh_get(tree, sibling)->parent = parent;
    box_bvh_get(tree, leaf)->parent = parent;
    box_bvh_repair(tree, parent);
done:
    return leaf;
}

void box_bvh_remove(
    box_bvh_t *tree,
    int32_t leaf)
{
    int32_t parent = box_bvh_get(tree, leaf)->parent;
    tree->leaves --;
    box_bvh_release(tree, leaf);
    if (parent == BOX_BVH_NULL) {
        tree->root = BOX_BVH_NULL;
        goto done;
    }

    box_bvh_node_t *p = box_bvh_get(tree, parent);
    int32_t sibling = p->left == leaf ? p->right : p->left;
    int32_t grand = p->parent;
    box_bvh_relink(tree, grand, parent, sibling);
    box_bvh_release(tree, parent);
    box_bvh_repair(tree, grand);
done:
    return;
}

/** Grow the ancestors of a leaf without margin, or insert it again when that grows too many.
 *
 * @param tree The tree
 * @param leaf The leaf, which already has its new bounds
 * @return The leaf node
 */
static int32_t box_bvh_grow(
    box_bvh_t *tree,
    int32_t leaf)
{
    int32_t result = leaf, level, top = BOX_BVH_NULL;
    box_bvh_node_t *l = box_bvh_get(tree, leaf);
    int32_t node = l->parent;
    for (level = 0; level < BOX_BVH_GROW_LEVELS && node != BOX_BVH_NULL; level ++) {
        box_bvh_node_t *n = box_bvh_get(tree, node);
        if (box_bvh_contains(n->min, n->max, l->min, l->max)) {
            top = node;
            break;
        }

        node = n->parent;
    }

    if (top != BOX_BVH_NULL || l->parent == BOX_BVH_NULL) {
        for (node = l->parent; node != top; node = box_bvh_get(tree, node)->parent) {
            box_bvh_node_t *n = box_bvh_get(tree, node);
            box_bvh_union(n->min, n->max, l->min, l->max, n->min, n->max);
        }
    } else {
        vec3 min, max;
        int32_t item = l->right;
        glm_vec3_copy(l->min, min);
        glm_vec3_copy(l->max, max);
        box_bvh_remove(tree, leaf);
        result = box_bvh_insert(tree, min, max, item);
    }

    return result;
}

int32_t box_bvh_move(
    box_bvh_t *tree,
    int32_t leaf,
    const vec3 min,
    const vec3 max,
    float margin)
{
    int32_t result = leaf;
    box_bvh_node_t *l = box_bvh_get(tree, leaf);
    if (margin > 0) {
        if (!box_bvh_contains(l->min, l->max, min, max)) {
            vec3 fat_min, fat_max;
            int32_t item = l->right;
            glm_vec3_subs(ECS_CONST_CAST(float*, min), margin, fat_min);
            glm_vec3_adds(ECS_CONST_CAST(float*, max), margin, fat_max);
            box_bvh_remove(tree, leaf);
            result = box_bvh_insert(tree, fat_min, fat_max, item);
        }
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, min), l->min);
        glm_vec3_copy(ECS_CONST_CAST(float*, max), l->max);
        result = box_bvh_grow(tree, leaf);
    }

    return result;
}

/** Get the doubled center of a leaf on one axis.
 *
 * @param leaf The leaf
 * @param axis The axis
 * @return min + max on the axis
 */
static float box_bvh_centroid(
    const box_bvh_leaf_t *leaf,
    int32_t axis)
{
    return leaf->min[axis] + leaf->max[axis];
}

/** Find the bin of a leaf.
 *
 * @param leaf The leaf
 * @param axis The axis of the bins
 * @param origin Lowest centroid on the axis
 * @param scale Bins per unit of centroid
 * @return The bin
 */
static int32_t box_bvh_bin_of(
    const box_bvh_leaf_t *leaf,
    int32_t axis,
    float origin,
    float scale)
{
    int32_t result = (int32_t)((box_bvh_centroid(leaf, axis) - origin) * scale);
    if (result >= BOX_BVH_BINS) {
        result = BOX_BVH_BINS - 1;
    } else if (result < 0) {
        result = 0;
    }

    return result;
}

/** Choose the bin after which to split a range of leaves, weighing area times count.
 *
 * @param leaves The leaves
 * @param count Number of leaves
 * @param axis The axis of the bins
 * @param origin Lowest centroid on the axis
 * @param scale Bins per unit of centroid
 * @return The last bin of the left side
 */
static int32_t box_bvh_split_bin(
    const box_bvh_leaf_t *leaves,
    int32_t count,
    int32_t axis,
    float origin,
    float scale)
{
    box_bvh_bin_t bins[BOX_BVH_BINS];
    float right_cost[BOX_BVH_BINS];
    int32_t i, result = 0, seen = 0;
    float best = FLT_MAX;
    vec3 min, max;
    for (i = 0; i < BOX_BVH_BINS; i ++) {
        glm_vec3_fill(bins[i].min, FLT_MAX);
        glm_vec3_fill(bins[i].max, -FLT_MAX);
        bins[i].count = 0;
    }

    for (i = 0; i < count; i ++) {
        box_bvh_bin_t *bin = &bins[box_bvh_bin_of(&leaves[i], axis, origin, scale)];
        box_bvh_union(bin->min, bin->max, leaves[i].min, leaves[i].max, bin->min, bin->max);
        bin->count ++;
    }

    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    for (i = BOX_BVH_BINS - 1; i > 0; i --) {
        box_bvh_union(min, max, bins[i].min, bins[i].max, min, max);
        seen += bins[i].count;
        right_cost[i - 1] = seen ? (float)seen * box_bvh_area(min, max) : 0;
    }

    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    seen = 0;
    for (i = 0; i < BOX_BVH_BINS - 1; i ++) {
        box_bvh_union(min, max, bins[i].min, bins[i].max, min, max);
        seen += bins[i].count;
        float cost = (seen ? (float)seen * box_bvh_area(min, max) : 0) + right_cost[i];
        if (seen && seen < count && cost < best) {
            best = cost;
            result = i;
        }
    }

    return result;
}

/** Order a range of leaves into two halves for the children of a node.
 *
 * @param leaves The leaves
 * @param count Number of leaves, at least two
 * @param depth Depth of the node
 * @return Number of leaves of the left child
 */
static int32_t box_bvh_partition(
    box_bvh_leaf_t *leaves,
    int32_t count,
    int32_t depth)
{
    int32_t result = count / 2, i, axis = 0;
    vec3 cmin, cmax;
    glm_vec3_fill(cmin, FLT_MAX);
    glm_vec3_fill(cmax, -FLT_MAX);
    for (i = 0; i < count; i ++) {
        int32_t a;
        for (a = 0; a < 3; a ++) {
            float c = box_bvh_centroid(&leaves[i], a);
            cmin[a] = c < cmin[a] ? c : cmin[a];
            cmax[a] = c > cmax[a] ? c : cmax[a];
        }
    }

    for (i = 1; i < 3; i ++) {
        if (cmax[i] - cmin[i] > cmax[axis] - cmin[axis]) {
            axis = i;
        }
    }

    float extent = cmax[axis] - cmin[axis];
    if (extent <= 0 || depth > BOX_BVH_MAX_SAH_DEPTH) {
        goto done;
    }

    float scale = (float)BOX_BVH_BINS / extent;
    int32_t split = box_bvh_split_bin(leaves, count, axis, cmin[axis], scale);
    int32_t lo = 0, hi = count - 1;
    while (lo <= hi) {
        if (box_bvh_bin_of(&leaves[lo], axis, cmin[axis], scale) <= split) {
            lo ++;
        } else {
            box_bvh_leaf_t tmp = leaves[lo];
            leaves[lo] = leaves[hi];
            leaves[hi] = tmp;
            hi --;
        }
    }

    if (lo > 0 && lo < count) {
        result = lo;
    }

done:
    return result;
}

/** Build the subtree of a range of leaves.
 *
 * @param tree The tree, with room for every node of the build
 * @param leaves The leaves
 * @param count Number of leaves, at least one
 * @param depth Depth of the subtree root
 * @param nodes Receives the leaf node of every item
 * @return The subtree root
 */
static int32_t box_bvh_build_node(
    box_bvh_t *tree,
    box_bvh_leaf_t *leaves,
    int32_t count,
    int32_t depth,
    int32_t *nodes)
{
    int32_t result = box_bvh_alloc(tree);
    if (count == 1) {
        box_bvh_node_t *n = box_bvh_get(tree, result);
        glm_vec3_copy(leaves[0].min, n->min);
        glm_vec3_copy(leaves[0].max, n->max);
        n->right = leaves[0].item;
        nodes[leaves[0].item] = result;
    } else {
        int32_t half = box_bvh_partition(leaves, count, depth);
        int32_t left = box_bvh_build_node(tree, leaves, half, depth + 1, nodes);
        int32_t right = box_bvh_build_node(
            tree, leaves + half, count - half, depth + 1, nodes);
        box_bvh_node_t *n = box_bvh_get(tree, result);
        n->left = left;
        n->right = right;
        box_bvh_get(tree, left)->parent = result;
        box_bvh_get(tree, right)->parent = result;
        box_bvh_refit(tree, result);
    }

    return result;
}

void box_bvh_build(
    box_bvh_t *tree,
    box_bvh_leaf_t *leaves,
    int32_t count,
    int32_t *nodes)
{
    ecs_vec_clear(&tree->nodes);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->leaves = count;
    if (count) {
        ecs_vec_set_min_size_t(NULL, &tree->nodes, box_bvh_node_t, count * 2);
        tree->root = box_bvh_build_node(tree, leaves, count, 0, nodes);
    }
}

bool box_bvh_box_box(
    const vec3 a_min,
    const vec3 a_max,
    const vec3 b_min,
    const vec3 b_max)
{
    return a_min[0] <= b_max[0] && a_max[0] >= b_min[0] &&
        a_min[1] <= b_max[1] && a_max[1] >= b_min[1] &&
        a_min[2] <= b_max[2] && a_max[2] >= b_min[2];
}

bool box_bvh_sphere_box(
    const vec3 min,
    const vec3 max,
    const vec3 center,
    float radius)
{
    float d = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float v = center[i] < min[i] ? min[i] - center[i] :
            center[i] > max[i] ? center[i] - max[i] : 0;
        d += v * v;
    }

    return d <= radius * radius;
}

bool box_bvh_ray_init(
    box_bvh_ray_t *ray,
    const vec3 origin,
    const vec3 dir)
{
    int32_t i;
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    bool result = len > 0;
    for (i = 0; i < 3; i ++) {
        ray->origin[i] = origin[i];
        ray->dir[i] = result ? dir[i] / len : 0;
        ray->inv[i] = ray->dir[i] != 0 ? 1.0f / ray->dir[i] : 0;
    }

    return result;
}

bool box_bvh_ray_box(
    const box_bvh_ray_t *ray,
    const vec3 min,
    const vec3 max,
    float reach,
    float *t)
{
    float t_min = 0, t_max = reach;
    int32_t i;
    bool result = true;
    for (i = 0; i < 3 && result; i ++) {
        if (ray->dir[i] == 0) {
            result = ray->origin[i] >= min[i] && ray->origin[i] <= max[i];
        } else {
            float t1 = (min[i] - ray->origin[i]) * ray->inv[i];
            float t2 = (max[i] - ray->origin[i]) * ray->inv[i];
            if (t1 > t2) {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }

            t_min = t1 > t_min ? t1 : t_min;
            t_max = t2 < t_max ? t2 : t_max;
            result = t_min <= t_max;
        }
    }

    *t = t_min;

    return result;
}

bool box_bvh_overlap(
    const box_bvh_t *tree,
    const vec3 min,
    const vec3 max,
    box_bvh_visit_t visit,
    void *ctx)
{
    box_bvh_stack_t s;
    bool result = true;
    box_bvh_stack_init(&s);
    if (tree->root != BOX_BVH_NULL) {
        box_bvh_stack_push(&s, tree->root, 0);
    }

    while (s.count && result) {
        box_bvh_node_t *n = box_bvh_get(tree, s.data[-- s.count].node);
        if (!box_bvh_box_box(n->min, n->max, min, max)) {
            continue;
        }

        if (n->left == BOX_BVH_NULL) {
            result = visit(n->right, ctx);
        } else {
            box_bvh_stack_push(&s, n->left, 0);
            box_bvh_stack_push(&s, n->right, 0);
        }
    }

    box_bvh_stack_fini(&s);

    return result;
}

bool box_bvh_sphere(
    const box_bvh_t *tree,
    const vec3 center,
    float radius,
    box_bvh_visit_t visit,
    void *ctx)
{
    box_bvh_stack_t s;
    bool result = true;
    box_bvh_stack_init(&s);
    if (tree->root != BOX_BVH_NULL) {
        box_bvh_stack_push(&s, tree->root, 0);
    }

    while (s.count && result) {
        box_bvh_node_t *n = box_bvh_get(tree, s.data[-- s.count].node);
        if (!box_bvh_sphere_box(n->min, n->max, center, radius)) {
            continue;
        }

        if (n->left == BOX_BVH_NULL) {
            result = visit(n->right, ctx);
        } else {
            box_bvh_stack_push(&s, n->left, 0);
            box_bvh_stack_push(&s, n->right, 0);
        }
    }

    box_bvh_stack_fini(&s);

    return result;
}

/** Push the children of an inner node that a ray reaches, the nearer one last.
 *
 * @param tree The tree
 * @param n The node
 * @param ray The ray
 * @param reach Current reach of the ray
 * @param s The stack
 */
static void box_bvh_ray_children(
    const box_bvh_t *tree,
    const box_bvh_node_t *n,
    const box_bvh_ray_t *ray,
    float reach,
    box_bvh_stack_t *s)
{
    box_bvh_node_t *l = box_bvh_get(tree, n->left);
    box_bvh_node_t *r = box_bvh_get(tree, n->right);
    float tl = 0, tr = 0;
    bool hit_l = box_bvh_ray_box(ray, l->min, l->max, reach, &tl);
    bool hit_r = box_bvh_ray_box(ray, r->min, r->max, reach, &tr);
    if (hit_l && hit_r) {
        if (tl < tr) {
            box_bvh_stack_push(s, n->right, tr);
            box_bvh_stack_push(s, n->left, tl);
        } else {
            box_bvh_stack_push(s, n->left, tl);
            box_bvh_stack_push(s, n->right, tr);
        }
    } else if (hit_l) {
        box_bvh_stack_push(s, n->left, tl);
    } else if (hit_r) {
        box_bvh_stack_push(s, n->right, tr);
    }
}

bool box_bvh_ray(
    const box_bvh_t *tree,
    const box_bvh_ray_t *ray,
    const float *reach,
    box_bvh_visit_t visit,
    void *ctx)
{
    box_bvh_stack_t s;
    bool result = true;
    float t = 0;
    box_bvh_stack_init(&s);
    if (tree->root != BOX_BVH_NULL) {
        box_bvh_node_t *root = box_bvh_get(tree, tree->root);
        if (box_bvh_ray_box(ray, root->min, root->max, *reach, &t)) {
            box_bvh_stack_push(&s, tree->root, t);
        }
    }

    while (s.count && result) {
        box_bvh_entry_t entry = s.data[-- s.count];
        if (entry.t > *reach) {
            continue;
        }

        box_bvh_node_t *n = box_bvh_get(tree, entry.node);
        if (n->left == BOX_BVH_NULL) {
            result = visit(n->right, ctx);
        } else {
            box_bvh_ray_children(tree, n, ray, *reach, &s);
        }
    }

    box_bvh_stack_fini(&s);

    return result;
}
