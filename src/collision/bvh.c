/**
 * @file collision/bvh.c
 * @brief Dynamic bounding volume hierarchy over boxes with a user value.
 *
 * Nodes live in one vector with a free list, so a proxy keeps its index while it
 * exists. A leaf is inserted next to the sibling that grows the tree least and
 * the path to the root is refitted and rotated.
 */

#include "collision_internal.h"

/** Nodes a query walk can have waiting at once */
#define BOX_BVH_STACK (256)

/** Read a node of a tree.
 *
 * @param tree The tree
 * @param index The node
 * @return The node
 */
static box_bvh_node_t* box_bvh_get(
    const box_bvh_t *tree,
    int32_t index)
{
    ecs_vec_t *nodes = ECS_CONST_CAST(ecs_vec_t*, &tree->nodes);

    return ecs_vec_get_t(nodes, box_bvh_node_t, index);
}

/** Surface area of a box.
 *
 * @param min Minimum corner
 * @param max Maximum corner
 * @return Half the surface area of the box
 */
static float box_bvh_area(
    vec3 min,
    vec3 max)
{
    vec3 d = vec3_sub(max, min);

    return d.x * d.y + d.y * d.z + d.z * d.x;
}

/** Take a node off the free list or append one.
 *
 * @param tree The tree
 * @return The node
 */
static int32_t box_bvh_alloc(
    box_bvh_t *tree)
{
    int32_t index = tree->free;
    if (index != BOX_BVH_NULL) {
        tree->free = box_bvh_get(tree, index)->parent;
    } else {
        index = ecs_vec_count(&tree->nodes);
        ecs_vec_append_t(NULL, &tree->nodes, box_bvh_node_t);
    }

    box_bvh_node_t *node = box_bvh_get(tree, index);
    node->parent = BOX_BVH_NULL;
    node->child1 = BOX_BVH_NULL;
    node->child2 = BOX_BVH_NULL;
    node->height = 0;
    node->user = 0;

    return index;
}

/** Put a node back on the free list.
 *
 * @param tree The tree
 * @param index The node
 */
static void box_bvh_release(
    box_bvh_t *tree,
    int32_t index)
{
    box_bvh_node_t *node = box_bvh_get(tree, index);
    node->parent = tree->free;
    node->height = -1;
    tree->free = index;
}

/** Give a node the bounds of its two children.
 *
 * @param tree The tree
 * @param index The node
 */
static void box_bvh_refit(
    box_bvh_t *tree,
    int32_t index)
{
    box_bvh_node_t *node = box_bvh_get(tree, index);
    box_bvh_node_t *a = box_bvh_get(tree, node->child1);
    box_bvh_node_t *b = box_bvh_get(tree, node->child2);
    node->min = vec3_min(a->min, b->min);
    node->max = vec3_max(a->max, b->max);
    node->height = 1 + ECS_MAX(a->height, b->height);
}

/** Rotate a node so its two subtrees differ by at most one in height.
 *
 * @param tree The tree
 * @param index The node
 * @return The node that took its place
 */
static int32_t box_bvh_balance(
    box_bvh_t *tree,
    int32_t index)
{
    int32_t result = index;
    box_bvh_node_t *a = box_bvh_get(tree, index);
    if (a->child1 == BOX_BVH_NULL || a->height < 2) {
        goto done;
    }

    int32_t left = a->child1;
    int32_t right = a->child2;
    int32_t diff = box_bvh_get(tree, right)->height - box_bvh_get(tree, left)->height;
    if (diff > -2 && diff < 2) {
        goto done;
    }

    int32_t tall = diff > 0 ? right : left;
    box_bvh_node_t *c = box_bvh_get(tree, tall);
    int32_t c1 = c->child1;
    int32_t c2 = c->child2;
    int32_t keep = box_bvh_get(tree, c1)->height > box_bvh_get(tree, c2)->height ? c2 : c1;
    int32_t lift = keep == c1 ? c2 : c1;

    c->parent = a->parent;
    c->child1 = index;
    c->child2 = lift;
    if (c->parent != BOX_BVH_NULL) {
        box_bvh_node_t *p = box_bvh_get(tree, c->parent);
        if (p->child1 == index) {
            p->child1 = tall;
        } else {
            p->child2 = tall;
        }
    } else {
        tree->root = tall;
    }

    a = box_bvh_get(tree, index);
    a->parent = tall;
    if (a->child1 == tall) {
        a->child1 = keep;
    } else {
        a->child2 = keep;
    }

    box_bvh_get(tree, keep)->parent = index;
    box_bvh_get(tree, lift)->parent = tall;
    box_bvh_refit(tree, index);
    box_bvh_refit(tree, tall);
    result = tall;
done:
    return result;
}

/** Walk from a node to the root, refitting and rotating on the way.
 *
 * @param tree The tree
 * @param index The node to start at
 */
static void box_bvh_repair(
    box_bvh_t *tree,
    int32_t index)
{
    while (index != BOX_BVH_NULL) {
        index = box_bvh_balance(tree, index);
        box_bvh_refit(tree, index);
        index = box_bvh_get(tree, index)->parent;
    }
}

/** Find the leaf a new leaf shares a parent with.
 *
 * @param tree The tree
 * @param min Minimum corner of the new leaf
 * @param max Maximum corner of the new leaf
 * @return The sibling
 */
static int32_t box_bvh_sibling(
    box_bvh_t *tree,
    vec3 min,
    vec3 max)
{
    int32_t index = tree->root;
    while (box_bvh_get(tree, index)->child1 != BOX_BVH_NULL) {
        box_bvh_node_t *node = box_bvh_get(tree, index);
        int32_t c1 = node->child1;
        int32_t c2 = node->child2;
        float area = box_bvh_area(node->min, node->max);
        float merged = box_bvh_area(vec3_min(node->min, min), vec3_max(node->max, max));
        float inherit = 2.0f * (merged - area);
        float cost = 2.0f * merged;
        float costs[2];
        int32_t child[2] = { c1, c2 };
        int32_t i;
        for (i = 0; i < 2; i ++) {
            box_bvh_node_t *n = box_bvh_get(tree, child[i]);
            float grown = box_bvh_area(vec3_min(n->min, min), vec3_max(n->max, max));
            costs[i] = inherit + grown;
            if (n->child1 != BOX_BVH_NULL) {
                costs[i] -= box_bvh_area(n->min, n->max);
            }
        }

        if (cost <= costs[0] && cost <= costs[1]) {
            break;
        }

        index = costs[0] < costs[1] ? c1 : c2;
    }

    return index;
}

/** Hang a leaf in the tree next to the sibling that grows it least.
 *
 * @param tree The tree
 * @param leaf The leaf
 */
static void box_bvh_attach(
    box_bvh_t *tree,
    int32_t leaf)
{
    if (tree->root == BOX_BVH_NULL) {
        tree->root = leaf;
        box_bvh_get(tree, leaf)->parent = BOX_BVH_NULL;
        goto done;
    }

    box_bvh_node_t *l = box_bvh_get(tree, leaf);
    int32_t sibling = box_bvh_sibling(tree, l->min, l->max);
    int32_t old = box_bvh_get(tree, sibling)->parent;
    int32_t parent = box_bvh_alloc(tree);
    box_bvh_node_t *p = box_bvh_get(tree, parent);
    p->parent = old;
    p->child1 = sibling;
    p->child2 = leaf;
    if (old != BOX_BVH_NULL) {
        box_bvh_node_t *o = box_bvh_get(tree, old);
        if (o->child1 == sibling) {
            o->child1 = parent;
        } else {
            o->child2 = parent;
        }
    } else {
        tree->root = parent;
    }

    box_bvh_get(tree, sibling)->parent = parent;
    box_bvh_get(tree, leaf)->parent = parent;
    box_bvh_repair(tree, parent);
done:
    return;
}

/** Take a leaf out of the tree, leaving its sibling in place of its parent.
 *
 * @param tree The tree
 * @param leaf The leaf
 */
static void box_bvh_detach(
    box_bvh_t *tree,
    int32_t leaf)
{
    if (leaf == tree->root) {
        tree->root = BOX_BVH_NULL;
        goto done;
    }

    int32_t parent = box_bvh_get(tree, leaf)->parent;
    box_bvh_node_t *p = box_bvh_get(tree, parent);
    int32_t sibling = p->child1 == leaf ? p->child2 : p->child1;
    int32_t grand = p->parent;
    if (grand != BOX_BVH_NULL) {
        box_bvh_node_t *g = box_bvh_get(tree, grand);
        if (g->child1 == parent) {
            g->child1 = sibling;
        } else {
            g->child2 = sibling;
        }
    } else {
        tree->root = sibling;
    }

    box_bvh_get(tree, sibling)->parent = grand;
    box_bvh_release(tree, parent);
    box_bvh_repair(tree, grand);
done:
    return;
}

void box_bvh_init(
    box_bvh_t *tree)
{
    ecs_vec_init_t(NULL, &tree->nodes, box_bvh_node_t, 0);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->count = 0;
}

void box_bvh_fini(
    box_bvh_t *tree)
{
    ecs_vec_fini_t(NULL, &tree->nodes, box_bvh_node_t);
}

void box_bvh_clear(
    box_bvh_t *tree)
{
    ecs_vec_clear(&tree->nodes);
    tree->root = BOX_BVH_NULL;
    tree->free = BOX_BVH_NULL;
    tree->count = 0;
}

int32_t box_bvh_insert(
    box_bvh_t *tree,
    vec3 min,
    vec3 max,
    float margin,
    uint64_t user)
{
    vec3 slack = vec3_new(margin, margin, margin);
    int32_t leaf = box_bvh_alloc(tree);
    box_bvh_node_t *node = box_bvh_get(tree, leaf);
    node->min = vec3_sub(min, slack);
    node->max = vec3_add(max, slack);
    node->user = user;
    node->height = 0;
    box_bvh_attach(tree, leaf);
    tree->count ++;

    return leaf;
}

void box_bvh_remove(
    box_bvh_t *tree,
    int32_t proxy)
{
    box_bvh_detach(tree, proxy);
    box_bvh_release(tree, proxy);
    tree->count --;
}

bool box_bvh_move(
    box_bvh_t *tree,
    int32_t proxy,
    vec3 min,
    vec3 max,
    float margin)
{
    bool result = false;
    box_bvh_node_t *node = box_bvh_get(tree, proxy);
    if (min.x >= node->min.x && min.y >= node->min.y && min.z >= node->min.z &&
        max.x <= node->max.x && max.y <= node->max.y && max.z <= node->max.z)
    {
        goto done;
    }

    vec3 slack = vec3_new(margin, margin, margin);
    box_bvh_detach(tree, proxy);
    node = box_bvh_get(tree, proxy);
    node->min = vec3_sub(min, slack);
    node->max = vec3_add(max, slack);
    node->height = 0;
    box_bvh_attach(tree, proxy);
    result = true;
done:
    return result;
}

/** Test whether two boxes overlap.
 *
 * @param amin Minimum corner of the first box
 * @param amax Maximum corner of the first box
 * @param bmin Minimum corner of the second box
 * @param bmax Maximum corner of the second box
 * @return true when the boxes overlap
 */
static bool box_bvh_overlap(
    vec3 amin,
    vec3 amax,
    vec3 bmin,
    vec3 bmax)
{
    return amin.x <= bmax.x && amax.x >= bmin.x && amin.y <= bmax.y &&
        amax.y >= bmin.y && amin.z <= bmax.z && amax.z >= bmin.z;
}

/** Test whether a sphere reaches a box.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param center Center of the sphere
 * @param radius Radius of the sphere
 * @return true when the sphere reaches the box
 */
static bool box_bvh_sphere_hits(
    vec3 min,
    vec3 max,
    vec3 center,
    float radius)
{
    vec3 near = vec3_min(vec3_max(center, min), max);
    vec3 d = vec3_sub(center, near);

    return vec3_dot(d, d) <= radius * radius;
}

/** Test whether a ray reaches a box within a distance.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param origin Origin of the ray
 * @param inv One over the direction of the ray, per axis
 * @param max_dist Longest distance that counts
 * @return true when the ray reaches the box
 */
static bool box_bvh_ray_hits(
    vec3 min,
    vec3 max,
    vec3 origin,
    vec3 inv,
    float max_dist)
{
    const float *lo = &min.x;
    const float *hi = &max.x;
    const float *o = &origin.x;
    const float *iv = &inv.x;
    float enter = 0.0f;
    float exit = max_dist;
    int32_t axis;
    for (axis = 0; axis < 3; axis ++) {
        float t0 = (lo[axis] - o[axis]) * iv[axis];
        float t1 = (hi[axis] - o[axis]) * iv[axis];
        float a = t0 < t1 ? t0 : t1;
        float b = t0 < t1 ? t1 : t0;
        enter = a > enter ? a : enter;
        exit = b < exit ? b : exit;
    }

    return enter <= exit;
}

/** Test whether a box is not fully outside a plane bounded volume.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param f The planes
 * @return true when the box is inside or crosses the volume
 */
static bool box_bvh_frustum_hits(
    vec3 min,
    vec3 max,
    const box_frustum_planes_t *f)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < BOX_FRUSTUM_PLANES; i ++) {
        vec4 p = f->planes[i];
        vec3 far = vec3_new(p.x > 0 ? max.x : min.x, p.y > 0 ? max.y : min.y,
            p.z > 0 ? max.z : min.z);
        if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f) {
            result = false;
            break;
        }
    }

    return result;
}

/** Walk the tree and hand every leaf whose box passes a test to a callback.
 *
 * @param tree The tree
 * @param q The query
 * @param cb The callback
 * @param ctx Context of the callback
 */
static void box_bvh_walk(
    const box_bvh_t *tree,
    box_bvh_query_t *q,
    box_bvh_hit_t cb,
    void *ctx)
{
    if (tree->root == BOX_BVH_NULL) {
        goto done;
    }

    int32_t stack[BOX_BVH_STACK];
    int32_t top = 0;
    stack[top ++] = tree->root;
    while (top) {
        box_bvh_node_t *node = box_bvh_get(tree, stack[-- top]);
        bool hit = q->kind == BoxQueryAabb
            ? box_bvh_overlap(node->min, node->max, q->min, q->max)
            : (q->kind == BoxQuerySphere
                ? box_bvh_sphere_hits(node->min, node->max, q->origin, q->radius)
                : (q->kind == BoxQueryRay
                    ? box_bvh_ray_hits(node->min, node->max, q->origin, q->inv, q->max_dist)
                    : box_bvh_frustum_hits(node->min, node->max, q->frustum)));
        if (!hit) {
            continue;
        }

        if (node->child1 == BOX_BVH_NULL) {
            if (!cb(node->user, ctx)) {
                break;
            }

            continue;
        }

        if (top + 2 > BOX_BVH_STACK) {
            continue;
        }

        stack[top ++] = node->child1;
        stack[top ++] = node->child2;
    }

done:
    return;
}

void box_bvh_query(
    const box_bvh_t *tree,
    box_bvh_query_t *q,
    box_bvh_hit_t cb,
    void *ctx)
{
    box_bvh_walk(tree, q, cb, ctx);
}

void box_bvh_ray_setup(
    box_bvh_query_t *q,
    vec3 origin,
    vec3 dir,
    float max_dist)
{
    q->kind = BoxQueryRay;
    q->origin = origin;
    q->dir = dir;
    q->max_dist = max_dist;
    q->inv = vec3_new(
        dir.x != 0.0f ? 1.0f / dir.x : 1e30f,
        dir.y != 0.0f ? 1.0f / dir.y : 1e30f,
        dir.z != 0.0f ? 1.0f / dir.z : 1e30f);
}
