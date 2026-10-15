/**
 * @file spatial/bvh.c
 * @brief Bounding volume hierarchy: top down build, incremental insert and remove, walks.
 *
 * Every node holds the boxes of its two children, the box of the root child is in the tree.
 * A slot is where a child lives: 2 * node + side, or BOX_BVH_ROOT.
 */

#include "bvh.h"

/** Most bins per axis of the surface area heuristic of a top down build, which uses one bin per
 * leaf for fewer leaves */
#define BOX_BVH_BINS (16)

/** Ancestors that may still contain a moved exact item before it is inserted again */
#define BOX_BVH_GROW_LEVELS (4)

/** Depth below which a build stops weighing splits and halves the leaves */
#define BOX_BVH_MAX_SAH_DEPTH (48)

/** Margins by which a fat item may outgrow its fat bounds before it shrinks */
#define BOX_BVH_SHRINK (4.0f)

/** Entries a walk keeps on the stack before it allocates */
#define BOX_BVH_STACK (64)

/** Child reference of an item */
#define BOX_BVH_ITEM(item) (-2 - (item))

/** Node a walk still has to visit */
typedef struct box_bvh_entry_t {
    int32_t ref; /**< The node or item */
    float t;     /**< Distance at which a ray enters it, 0 for other walks */
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

/** Split of a top down build */
typedef struct box_bvh_split_t {
    float cost;   /**< Area times leaves of both sides, FLT_MAX when nothing splits */
    int32_t axis; /**< Axis of the bins */
    int32_t bins; /**< Number of bins */
    int32_t bin;  /**< Last bin of the left side */
    float origin; /**< Lowest centroid on the axis */
    float scale;  /**< Bins per unit of centroid */
} box_bvh_split_t;

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

/** Push a node or item on a stack.
 *
 * @param s The stack
 * @param ref The node or item
 * @param t Distance at which a ray enters it
 */
static void box_bvh_stack_push(
    box_bvh_stack_t *s,
    int32_t ref,
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

    s->data[s->count ++] = (box_bvh_entry_t){ .ref = ref, .t = t };
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

/** Test whether a child reference is an item.
 *
 * @param ref The reference
 * @return true for an item, false for a node
 */
static bool box_bvh_is_item(
    int32_t ref)
{
    return ref < BOX_BVH_NULL;
}

/** Get a node.
 *
 * @param tree The tree
 * @param node The node index
 * @return The node
 */
static box_bvh_node_t* box_bvh_get(
    const box_bvh_t *tree,
    int32_t node)
{
    ecs_assert(node >= 0 && node < ecs_vec_count(&tree->nodes), ECS_INVALID_PARAMETER, NULL);

    return &((box_bvh_node_t*)tree->nodes.array)[node];
}

/** Get the height of a child.
 *
 * @param tree The tree
 * @param ref The child
 * @return 0 for an item, the height of a node
 */
static int32_t box_bvh_height(
    const box_bvh_t *tree,
    int32_t ref)
{
    return box_bvh_is_item(ref) ? 0 : box_bvh_get(tree, ref)->height;
}

/** Get the box of a slot.
 *
 * @param tree The tree
 * @param slot The slot
 * @param max Receives the maximum corner
 * @return The minimum corner
 */
static float* box_bvh_slot_box(
    const box_bvh_t *tree,
    int32_t slot,
    float **max)
{
    float *result;
    if (slot == BOX_BVH_ROOT) {
        result = ECS_CONST_CAST(float*, tree->min);
        *max = ECS_CONST_CAST(float*, tree->max);
    } else {
        box_bvh_node_t *n = box_bvh_get(tree, slot >> 1);
        result = n->min[slot & 1];
        *max = n->max[slot & 1];
    }

    return result;
}

/** Get the child in a slot.
 *
 * @param tree The tree
 * @param slot The slot
 * @return The child
 */
static int32_t box_bvh_slot_ref(
    const box_bvh_t *tree,
    int32_t slot)
{
    return slot == BOX_BVH_ROOT ? tree->root : box_bvh_get(tree, slot >> 1)->child[slot & 1];
}

/** Put a child with its box in a slot and point the child back at the slot.
 *
 * @param tree The tree
 * @param slot The slot
 * @param ref The child
 * @param min Minimum corner of the child
 * @param max Maximum corner of the child
 */
static void box_bvh_put(
    box_bvh_t *tree,
    int32_t slot,
    int32_t ref,
    const vec3 min,
    const vec3 max)
{
    float *s_max, *s_min = box_bvh_slot_box(tree, slot, &s_max);
    vec3 b_min, b_max;
    glm_vec3_copy(ECS_CONST_CAST(float*, min), b_min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), b_max);
    glm_vec3_copy(b_min, s_min);
    glm_vec3_copy(b_max, s_max);
    if (slot == BOX_BVH_ROOT) {
        tree->root = ref;
    } else {
        box_bvh_get(tree, slot >> 1)->child[slot & 1] = ref;
    }

    if (box_bvh_is_item(ref)) {
        *ecs_vec_get_t(&tree->slots, int32_t, BOX_BVH_ITEM(ref)) = slot;
    } else {
        box_bvh_get(tree, ref)->parent = slot;
    }
}

/** Make room for the slot of an item.
 *
 * @param tree The tree
 * @param item The item
 */
static void box_bvh_slots_ensure(
    box_bvh_t *tree,
    int32_t item)
{
    while (ecs_vec_count(&tree->slots) <= item) {
        *ecs_vec_append_t(NULL, &tree->slots, int32_t) = BOX_BVH_NULL;
    }
}

void box_bvh_init(
    box_bvh_t *tree)
{
    ecs_vec_init_t(NULL, &tree->nodes, box_bvh_node_t, 0);
    ecs_vec_init_t(NULL, &tree->slots, int32_t, 0);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->leaves = 0;
}

void box_bvh_fini(
    box_bvh_t *tree)
{
    ecs_vec_fini_t(NULL, &tree->nodes, box_bvh_node_t);
    ecs_vec_fini_t(NULL, &tree->slots, int32_t);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->leaves = 0;
}

bool box_bvh_has(
    const box_bvh_t *tree,
    int32_t item)
{
    return item < ecs_vec_count(&tree->slots) &&
        *ecs_vec_get_t(&tree->slots, int32_t, item) != BOX_BVH_NULL;
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
    n->child[0] = BOX_BVH_NULL;
    n->child[1] = BOX_BVH_NULL;
    n->height = 1;

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

/** Compute the height of a node from its children.
 *
 * @param tree The tree
 * @param node The node
 */
static void box_bvh_refit_height(
    box_bvh_t *tree,
    int32_t node)
{
    box_bvh_node_t *n = box_bvh_get(tree, node);
    int32_t a = box_bvh_height(tree, n->child[0]), b = box_bvh_height(tree, n->child[1]);
    n->height = 1 + (a > b ? a : b);
}

/** Swap a child of a node with a child of its other child, which is a node.
 *
 * @param tree The tree
 * @param a The node
 * @param side Side of the child of a that moves down
 * @param k Side of the grandchild that moves up
 */
static void box_bvh_swap(
    box_bvh_t *tree,
    int32_t a,
    int32_t side,
    int32_t k)
{
    box_bvh_node_t *n = box_bvh_get(tree, a);
    int32_t p = n->child[1 - side];
    box_bvh_node_t *pn = box_bvh_get(tree, p);
    int32_t down = n->child[side], up = pn->child[k];
    vec3 down_min, down_max, up_min, up_max, min, max;
    glm_vec3_copy(n->min[side], down_min);
    glm_vec3_copy(n->max[side], down_max);
    glm_vec3_copy(pn->min[k], up_min);
    glm_vec3_copy(pn->max[k], up_max);
    box_bvh_put(tree, 2 * a + side, up, up_min, up_max);
    box_bvh_put(tree, 2 * p + k, down, down_min, down_max);
    box_bvh_refit_height(tree, p);
    pn = box_bvh_get(tree, p);
    box_bvh_union(pn->min[0], pn->max[0], pn->min[1], pn->max[1], min, max);
    n = box_bvh_get(tree, a);
    glm_vec3_copy(min, n->min[1 - side]);
    glm_vec3_copy(max, n->max[1 - side]);
    box_bvh_refit_height(tree, a);
}

/** Swap a child of a node with a grandchild when that lowers the area of the tree most.
 *
 * Moving child B down next to grandchild G of the other child C, and G up, changes only the
 * box of C, so the rotation with the largest drop of that area wins (b2RotateNodes).
 *
 * @param tree The tree
 * @param a The node
 */
static void box_bvh_rotate(
    box_bvh_t *tree,
    int32_t a)
{
    box_bvh_node_t *n = box_bvh_get(tree, a);
    float best = 0;
    int32_t s, k, side = -1, pick = -1;
    for (s = 0; s < 2 && n->height >= 2; s ++) {
        int32_t p = n->child[1 - s];
        if (box_bvh_is_item(p)) {
            continue;
        }

        const box_bvh_node_t *pn = box_bvh_get(tree, p);
        float area = box_bvh_area(n->min[1 - s], n->max[1 - s]);
        for (k = 0; k < 2; k ++) {
            float drop = area - box_bvh_union_area(
                n->min[s], n->max[s], pn->min[1 - k], pn->max[1 - k]);
            if (drop > best) {
                best = drop;
                side = s;
                pick = k;
            }
        }
    }

    if (side >= 0) {
        box_bvh_swap(tree, a, side, pick);
    }
}

/** Fit the ancestors of a changed node from the node up: heights, rotations and boxes.
 *
 * @param tree The tree
 * @param node The first node to fit
 * @param settle Stop at the first node whose box did not change
 */
static void box_bvh_fix(
    box_bvh_t *tree,
    int32_t node,
    bool settle)
{
    while (node != BOX_BVH_NULL) {
        vec3 min, max;
        box_bvh_refit_height(tree, node);
        box_bvh_rotate(tree, node);
        box_bvh_node_t *n = box_bvh_get(tree, node);
        int32_t slot = n->parent;
        box_bvh_union(n->min[0], n->max[0], n->min[1], n->max[1], min, max);
        float *s_max, *s_min = box_bvh_slot_box(tree, slot, &s_max);
        bool same = glm_vec3_eqv(s_min, min) && glm_vec3_eqv(s_max, max);
        glm_vec3_copy(min, s_min);
        glm_vec3_copy(max, s_max);
        node = slot == BOX_BVH_ROOT || (settle && same) ? BOX_BVH_NULL : slot >> 1;
    }
}

/** Find the slot whose child a new item grows the tree least next to.
 *
 * @param tree The tree, which is not empty
 * @param min Minimum corner of the item
 * @param max Maximum corner of the item
 * @return The slot of the sibling
 */
static int32_t box_bvh_sibling(
    const box_bvh_t *tree,
    const vec3 min,
    const vec3 max)
{
    int32_t slot = BOX_BVH_ROOT, ref = tree->root, i;
    const float *s_min = tree->min, *s_max = tree->max;
    while (!box_bvh_is_item(ref)) {
        const box_bvh_node_t *n = box_bvh_get(tree, ref);
        float combined = box_bvh_union_area(s_min, s_max, min, max);
        float cost = 2.0f * combined;
        float inherit = 2.0f * (combined - box_bvh_area(s_min, s_max));
        float child[2];
        for (i = 0; i < 2; i ++) {
            child[i] = box_bvh_union_area(n->min[i], n->max[i], min, max) + inherit;
            if (!box_bvh_is_item(n->child[i])) {
                child[i] -= box_bvh_area(n->min[i], n->max[i]);
            }
        }

        if (cost < child[0] && cost < child[1]) {
            break;
        }

        i = child[0] < child[1] ? 0 : 1;
        slot = 2 * ref + i;
        s_min = n->min[i];
        s_max = n->max[i];
        ref = n->child[i];
    }

    return slot;
}

void box_bvh_insert(
    box_bvh_t *tree,
    const vec3 min,
    const vec3 max,
    int32_t item)
{
    box_bvh_slots_ensure(tree, item);
    tree->leaves ++;
    if (tree->root == BOX_BVH_NULL) {
        box_bvh_put(tree, BOX_BVH_ROOT, BOX_BVH_ITEM(item), min, max);
        goto done;
    }

    int32_t slot = box_bvh_sibling(tree, min, max);
    int32_t node = box_bvh_alloc(tree);
    int32_t sibling = box_bvh_slot_ref(tree, slot);
    float *s_max, *s_min = box_bvh_slot_box(tree, slot, &s_max);
    vec3 n_min, n_max;
    box_bvh_union(s_min, s_max, min, max, n_min, n_max);
    box_bvh_put(tree, 2 * node, sibling, s_min, s_max);
    box_bvh_put(tree, 2 * node + 1, BOX_BVH_ITEM(item), min, max);
    box_bvh_put(tree, slot, node, n_min, n_max);
    box_bvh_fix(tree, node, false);
done:
    return;
}

void box_bvh_remove(
    box_bvh_t *tree,
    int32_t item)
{
    int32_t *slots = ecs_vec_first(&tree->slots);
    int32_t slot = slots[item];
    slots[item] = BOX_BVH_NULL;
    tree->leaves --;
    if (slot == BOX_BVH_ROOT) {
        tree->root = BOX_BVH_NULL;
        goto done;
    }

    int32_t parent = slot >> 1, side = slot & 1;
    box_bvh_node_t *p = box_bvh_get(tree, parent);
    int32_t sibling = p->child[1 - side], up = p->parent;
    vec3 min, max;
    glm_vec3_copy(p->min[1 - side], min);
    glm_vec3_copy(p->max[1 - side], max);
    box_bvh_put(tree, up, sibling, min, max);
    box_bvh_release(tree, parent);
    if (up != BOX_BVH_ROOT) {
        box_bvh_fix(tree, up >> 1, false);
    }

done:
    return;
}

void box_bvh_move(
    box_bvh_t *tree,
    int32_t item,
    const vec3 min,
    const vec3 max)
{
    int32_t slot = *ecs_vec_get_t(&tree->slots, int32_t, item), level;
    bool near = slot == BOX_BVH_ROOT;
    int32_t node = near ? BOX_BVH_NULL : slot >> 1;
    for (level = 0; level < BOX_BVH_GROW_LEVELS && node != BOX_BVH_NULL && !near; level ++) {
        int32_t up = box_bvh_get(tree, node)->parent;
        float *a_max, *a_min = box_bvh_slot_box(tree, up, &a_max);
        near = box_bvh_contains(a_min, a_max, min, max);
        node = up == BOX_BVH_ROOT ? BOX_BVH_NULL : up >> 1;
    }

    if (near) {
        float *s_max, *s_min = box_bvh_slot_box(tree, slot, &s_max);
        glm_vec3_copy(ECS_CONST_CAST(float*, min), s_min);
        glm_vec3_copy(ECS_CONST_CAST(float*, max), s_max);
        if (slot != BOX_BVH_ROOT) {
            box_bvh_fix(tree, slot >> 1, true);
        }
    } else {
        box_bvh_remove(tree, item);
        box_bvh_insert(tree, min, max, item);
    }
}

/** Grow bounds by a margin on every side and by a displacement on the side it points to.
 *
 * @param min Minimum corner of the bounds
 * @param max Maximum corner of the bounds
 * @param margin Slack on every side
 * @param ahead The displacement
 * @param fat_min Receives the minimum corner of the grown bounds
 * @param fat_max Receives the maximum corner of the grown bounds
 */
static void box_bvh_fatten(
    const vec3 min,
    const vec3 max,
    float margin,
    const vec3 ahead,
    vec3 fat_min,
    vec3 fat_max)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        fat_min[i] = min[i] - margin + (ahead[i] < 0 ? ahead[i] : 0);
        fat_max[i] = max[i] + margin + (ahead[i] > 0 ? ahead[i] : 0);
    }
}

bool box_bvh_move_fat(
    box_bvh_t *tree,
    int32_t item,
    const vec3 min,
    const vec3 max,
    float margin,
    const vec3 ahead)
{
    int32_t slot = *ecs_vec_get_t(&tree->slots, int32_t, item);
    float *l_max, *l_min = box_bvh_slot_box(tree, slot, &l_max);
    vec3 fat_min, fat_max, huge_min, huge_max;
    box_bvh_fatten(min, max, margin, ahead, fat_min, fat_max);
    glm_vec3_subs(fat_min, BOX_BVH_SHRINK * margin, huge_min);
    glm_vec3_adds(fat_max, BOX_BVH_SHRINK * margin, huge_max);
    bool keep = box_bvh_contains(l_min, l_max, min, max) &&
        box_bvh_contains(huge_min, huge_max, l_min, l_max);
    if (!keep) {
        box_bvh_remove(tree, item);
        box_bvh_insert(tree, fat_min, fat_max, item);
    }

    return !keep;
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
 * @param split The split whose axis and bins are used
 * @return The bin
 */
static int32_t box_bvh_bin_of(
    const box_bvh_leaf_t *leaf,
    const box_bvh_split_t *split)
{
    int32_t result = (int32_t)((box_bvh_centroid(leaf, split->axis) - split->origin) *
        split->scale);
    if (result >= split->bins) {
        result = split->bins - 1;
    } else if (result < 0) {
        result = 0;
    }

    return result;
}

/** Put a range of leaves in the bins of all three axes in one pass.
 *
 * @param leaves The leaves
 * @param count Number of leaves
 * @param splits The splits of the three axes with their origin, scale and number of bins
 * @param bins Receives the bins of the three axes
 */
static void box_bvh_fill_bins(
    const box_bvh_leaf_t *leaves,
    int32_t count,
    const box_bvh_split_t *splits,
    box_bvh_bin_t bins[3][BOX_BVH_BINS])
{
    int32_t i, a, n = splits[0].bins;
    for (a = 0; a < 3; a ++) {
        for (i = 0; i < n; i ++) {
            glm_vec3_fill(bins[a][i].min, FLT_MAX);
            glm_vec3_fill(bins[a][i].max, -FLT_MAX);
            bins[a][i].count = 0;
        }
    }

    for (i = 0; i < count; i ++) {
        const box_bvh_leaf_t *l = &leaves[i];
        for (a = 0; a < 3; a ++) {
            box_bvh_bin_t *bin = &bins[a][box_bvh_bin_of(l, &splits[a])];
            box_bvh_union(bin->min, bin->max, l->min, l->max, bin->min, bin->max);
            bin->count ++;
        }
    }
}

/** Choose the bin of one axis after which to split a range of leaves, weighing area times
 * count, and keep it when it is cheaper than the split found so far.
 *
 * @param bins The bins of the axis
 * @param count Number of leaves
 * @param split The axis, origin, scale and number of bins, which receives the cost and bin
 * @param best The cheapest split so far, replaced by a cheaper one
 */
static void box_bvh_split_axis(
    const box_bvh_bin_t *bins,
    int32_t count,
    box_bvh_split_t *split,
    box_bvh_split_t *best)
{
    float right_cost[BOX_BVH_BINS];
    int32_t i, seen = 0, n = split->bins;
    vec3 min, max;
    split->cost = FLT_MAX;
    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    for (i = n - 1; i > 0; i --) {
        box_bvh_union(min, max, bins[i].min, bins[i].max, min, max);
        seen += bins[i].count;
        right_cost[i - 1] = seen ? (float)seen * box_bvh_area(min, max) : 0;
    }

    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    seen = 0;
    for (i = 0; i < n - 1; i ++) {
        box_bvh_union(min, max, bins[i].min, bins[i].max, min, max);
        seen += bins[i].count;
        float cost = (seen ? (float)seen * box_bvh_area(min, max) : 0) + right_cost[i];
        if (seen && seen < count && cost < split->cost) {
            split->cost = cost;
            split->bin = i;
        }
    }

    if (split->cost < best->cost) {
        *best = *split;
    }
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
    int32_t result = count / 2, i, a;
    box_bvh_split_t best = { .cost = FLT_MAX }, splits[3];
    box_bvh_bin_t bins[3][BOX_BVH_BINS];
    vec3 cmin, cmax;
    if (count == 2 || depth > BOX_BVH_MAX_SAH_DEPTH) {
        goto done;
    }

    glm_vec3_fill(cmin, FLT_MAX);
    glm_vec3_fill(cmax, -FLT_MAX);
    for (i = 0; i < count; i ++) {
        for (a = 0; a < 3; a ++) {
            float c = box_bvh_centroid(&leaves[i], a);
            cmin[a] = c < cmin[a] ? c : cmin[a];
            cmax[a] = c > cmax[a] ? c : cmax[a];
        }
    }

    for (a = 0; a < 3; a ++) {
        splits[a] = (box_bvh_split_t){
            .axis = a, .origin = cmin[a], .bins = count < BOX_BVH_BINS ? count : BOX_BVH_BINS
        };
        splits[a].scale = cmax[a] > cmin[a] ? (float)splits[a].bins / (cmax[a] - cmin[a]) : 0;
    }

    box_bvh_fill_bins(leaves, count, splits, bins);
    for (a = 0; a < 3; a ++) {
        if (splits[a].scale > 0) {
            box_bvh_split_axis(bins[a], count, &splits[a], &best);
        }
    }

    if (best.cost == FLT_MAX) {
        goto done;
    }

    int32_t lo = 0, hi = count - 1;
    while (lo <= hi) {
        if (box_bvh_bin_of(&leaves[lo], &best) <= best.bin) {
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

/** Build the subtree of a range of leaves, its nodes in depth first order.
 *
 * @param tree The tree, with room for every node of the build
 * @param leaves The leaves
 * @param count Number of leaves, at least one
 * @param depth Depth of the subtree root
 * @param min Receives the minimum corner of the subtree
 * @param max Receives the maximum corner of the subtree
 * @return The subtree root, a node or an item
 */
static int32_t box_bvh_build_node(
    box_bvh_t *tree,
    box_bvh_leaf_t *leaves,
    int32_t count,
    int32_t depth,
    vec3 min,
    vec3 max)
{
    int32_t result;
    if (count == 1) {
        result = BOX_BVH_ITEM(leaves[0].item);
        glm_vec3_copy(leaves[0].min, min);
        glm_vec3_copy(leaves[0].max, max);
    } else {
        vec3 l_min, l_max, r_min, r_max;
        result = box_bvh_alloc(tree);
        int32_t half = box_bvh_partition(leaves, count, depth);
        int32_t left = box_bvh_build_node(tree, leaves, half, depth + 1, l_min, l_max);
        int32_t right = box_bvh_build_node(
            tree, leaves + half, count - half, depth + 1, r_min, r_max);
        box_bvh_put(tree, 2 * result, left, l_min, l_max);
        box_bvh_put(tree, 2 * result + 1, right, r_min, r_max);
        box_bvh_refit_height(tree, result);
        box_bvh_union(l_min, l_max, r_min, r_max, min, max);
    }

    return result;
}

void box_bvh_build(
    box_bvh_t *tree,
    box_bvh_leaf_t *leaves,
    int32_t count)
{
    int32_t i, *slots = ecs_vec_first(&tree->slots);
    for (i = 0; i < ecs_vec_count(&tree->slots); i ++) {
        slots[i] = BOX_BVH_NULL;
    }

    for (i = 0; i < count; i ++) {
        box_bvh_slots_ensure(tree, leaves[i].item);
    }

    ecs_vec_clear(&tree->nodes);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->leaves = count;
    if (count) {
        vec3 min, max;
        ecs_vec_set_min_size_t(NULL, &tree->nodes, box_bvh_node_t, count);
        int32_t root = box_bvh_build_node(tree, leaves, count, 0, min, max);
        box_bvh_put(tree, BOX_BVH_ROOT, root, min, max);
    }
}

float box_bvh_cost(
    const box_bvh_t *tree)
{
    int32_t i, count = ecs_vec_count(&tree->nodes);
    float result = 0, area = tree->root != BOX_BVH_NULL ? box_bvh_area(tree->min, tree->max) : 0;
    for (i = 0; i < count && area > 0; i ++) {
        const box_bvh_node_t *n = box_bvh_get(tree, i);
        if (n->height > 0) {
            result += box_bvh_union_area(n->min[0], n->max[0], n->min[1], n->max[1]) / area;
        }
    }

    return result;
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
    int32_t i;
    box_bvh_stack_init(&s);
    if (tree->root != BOX_BVH_NULL && box_bvh_box_box(tree->min, tree->max, min, max)) {
        box_bvh_stack_push(&s, tree->root, 0);
    }

    while (s.count && result) {
        int32_t ref = s.data[-- s.count].ref;
        if (box_bvh_is_item(ref)) {
            result = visit(BOX_BVH_ITEM(ref), ctx);
            continue;
        }

        const box_bvh_node_t *n = box_bvh_get(tree, ref);
        for (i = 0; i < 2; i ++) {
            if (box_bvh_box_box(n->min[i], n->max[i], min, max)) {
                box_bvh_stack_push(&s, n->child[i], 0);
            }
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
    int32_t i;
    box_bvh_stack_init(&s);
    if (tree->root != BOX_BVH_NULL &&
        box_bvh_sphere_box(tree->min, tree->max, center, radius))
    {
        box_bvh_stack_push(&s, tree->root, 0);
    }

    while (s.count && result) {
        int32_t ref = s.data[-- s.count].ref;
        if (box_bvh_is_item(ref)) {
            result = visit(BOX_BVH_ITEM(ref), ctx);
            continue;
        }

        const box_bvh_node_t *n = box_bvh_get(tree, ref);
        for (i = 0; i < 2; i ++) {
            if (box_bvh_sphere_box(n->min[i], n->max[i], center, radius)) {
                box_bvh_stack_push(&s, n->child[i], 0);
            }
        }
    }

    box_bvh_stack_fini(&s);

    return result;
}

/** Push the children of a node that a ray reaches, the nearer one last.
 *
 * @param n The node
 * @param ray The ray
 * @param reach Current reach of the ray
 * @param s The stack
 */
static void box_bvh_ray_children(
    const box_bvh_node_t *n,
    const box_bvh_ray_t *ray,
    float reach,
    box_bvh_stack_t *s)
{
    float tl = 0, tr = 0;
    bool hit_l = box_bvh_ray_box(ray, n->min[0], n->max[0], reach, &tl);
    bool hit_r = box_bvh_ray_box(ray, n->min[1], n->max[1], reach, &tr);
    if (hit_l && hit_r) {
        if (tl < tr) {
            box_bvh_stack_push(s, n->child[1], tr);
            box_bvh_stack_push(s, n->child[0], tl);
        } else {
            box_bvh_stack_push(s, n->child[0], tl);
            box_bvh_stack_push(s, n->child[1], tr);
        }
    } else if (hit_l) {
        box_bvh_stack_push(s, n->child[0], tl);
    } else if (hit_r) {
        box_bvh_stack_push(s, n->child[1], tr);
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
    if (tree->root != BOX_BVH_NULL &&
        box_bvh_ray_box(ray, tree->min, tree->max, *reach, &t))
    {
        box_bvh_stack_push(&s, tree->root, t);
    }

    while (s.count && result) {
        box_bvh_entry_t entry = s.data[-- s.count];
        if (entry.t > *reach) {
            continue;
        }

        if (box_bvh_is_item(entry.ref)) {
            result = visit(BOX_BVH_ITEM(entry.ref), ctx);
        } else {
            box_bvh_ray_children(box_bvh_get(tree, entry.ref), ray, *reach, &s);
        }
    }

    box_bvh_stack_fini(&s);

    return result;
}
