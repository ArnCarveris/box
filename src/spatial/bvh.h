/**
 * @file spatial/bvh.h
 * @brief Bounding volume hierarchy of axis aligned boxes, private to box.spatial.
 *
 * A binary tree whose nodes hold the boxes of both their children, so a walk tests two children
 * from one cache line; a child is a node or an item, so a node over two items is a leaf of two.
 *
 * Built top down with SAH bins on all three axes. Inserts go to the cheapest sibling; ancestors
 * take the area rotation of Box2D v3, else balance at no area cost or 16 levels off, so boxes
 * at one spot, equal or nested, do not pile up. Coherent movers only balance.
 */

#ifndef BOX_SPATIAL_BVH_H
#define BOX_SPATIAL_BVH_H

#include <box.h>

/** No node, no child, or an item that is not in the tree */
#define BOX_BVH_NULL (-1)

/** Slot of the root child of a tree */
#define BOX_BVH_ROOT (-2)

/** Inner node of a hierarchy, 64 bytes: the boxes and the references of its two children */
typedef struct box_bvh_node_t {
    vec3 min[2];      /**< Minimum corners of the children */
    vec3 max[2];      /**< Maximum corners of the children */
    int32_t child[2]; /**< Children: a node index, or -2 - item for an item */
    int32_t parent;   /**< Slot of the node (2 * parent + side or BOX_BVH_ROOT), next free node */
    int32_t height;   /**< Nodes on the longest path down to an item, -1 for a free node */
} box_bvh_node_t;

/** A hierarchy with a free list of nodes and the slot of every item */
typedef struct box_bvh_t {
    ecs_vec_t nodes; /**< box_bvh_node_t by index */
    ecs_vec_t slots; /**< int32_t by item: slot of the item, BOX_BVH_NULL when not in the tree */
    vec3 min;        /**< Minimum corner of the root child */
    vec3 max;        /**< Maximum corner of the root child */
    int32_t root;    /**< Root child, a node or an item, BOX_BVH_NULL when the tree is empty */
    int32_t free;    /**< First free node, BOX_BVH_NULL when there is none */
    int32_t leaves;  /**< Items in the tree */
} box_bvh_t;

/** A leaf to build a hierarchy from */
typedef struct box_bvh_leaf_t {
    vec3 min;     /**< Minimum corner */
    vec3 max;     /**< Maximum corner */
    int32_t item; /**< Item of the leaf */
} box_bvh_leaf_t;

/** A ray prepared for walking a hierarchy */
typedef struct box_bvh_ray_t {
    vec3 origin; /**< Origin */
    vec3 dir;    /**< Unit direction */
    vec3 inv;    /**< One over the direction per axis, 0 where the direction is 0 */
} box_bvh_ray_t;

/** Receive an item a walk reached.
 *
 * @param item The item
 * @param ctx Context of the walk
 * @return false to stop the walk
 */
typedef bool (*box_bvh_visit_t)(
    int32_t item,
    void *ctx);

/** Receive a box of a hierarchy, a node before its children.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param depth Depth of the box, 0 for the root child
 * @param item The item of a leaf, BOX_BVH_NULL for a node
 * @param ctx Context of the walk
 * @return false to skip the children of the node
 */
typedef bool (*box_bvh_box_visit_t)(
    const vec3 min,
    const vec3 max,
    int32_t depth,
    int32_t item,
    void *ctx);

/** Set up an empty hierarchy.
 *
 * @param tree The tree
 */
void box_bvh_init(
    box_bvh_t *tree);

/** Free the nodes of a hierarchy.
 *
 * @param tree The tree
 */
void box_bvh_fini(
    box_bvh_t *tree);

/** Test whether an item is in a hierarchy.
 *
 * @param tree The tree
 * @param item The item
 * @return true when the tree holds the item
 */
bool box_bvh_has(
    const box_bvh_t *tree,
    int32_t item);

/** Insert an item next to the sibling that grows the tree least.
 *
 * @param tree The tree
 * @param min Minimum corner of the item
 * @param max Maximum corner of the item
 * @param item The item, which is not in the tree
 */
void box_bvh_insert(
    box_bvh_t *tree,
    const vec3 min,
    const vec3 max,
    int32_t item);

/** Remove an item.
 *
 * @param tree The tree
 * @param item The item, which is in the tree
 */
void box_bvh_remove(
    box_bvh_t *tree,
    int32_t item);

/** Give an item exact new bounds, keeping its place when a close ancestor still contains them.
 *
 * The ancestors are fitted to the bounds again, shrinking as well as growing; an item that
 * left every close ancestor is inserted again.
 *
 * @param tree The tree
 * @param item The item, which is in the tree
 * @param min Minimum corner of the new bounds
 * @param max Maximum corner of the new bounds
 */
void box_bvh_move(
    box_bvh_t *tree,
    int32_t item,
    const vec3 min,
    const vec3 max);

/** Give a fat item new bounds, inserting it again only when they leave it or it is too large.
 *
 * An item inserted again gets the bounds grown by the margin on every side and stretched by
 * the predicted displacement on the side it moves to. An item that still contains the bounds
 * stays, unless it outgrew that box by more than four margins (b2DynamicTree::MoveProxy).
 *
 * @param tree The tree
 * @param item The item, which is in the tree
 * @param min Minimum corner of the new bounds
 * @param max Maximum corner of the new bounds
 * @param margin Slack around the bounds on every side
 * @param ahead Predicted displacement the item reaches ahead of the bounds
 * @return true when the item was inserted again
 */
bool box_bvh_move_fat(
    box_bvh_t *tree,
    int32_t item,
    const vec3 min,
    const vec3 max,
    float margin,
    const vec3 ahead);

/** Replace the tree with one built top down from a set of leaves.
 *
 * @param tree The tree
 * @param leaves The leaves, reordered by the build
 * @param count Number of leaves
 */
void box_bvh_build(
    box_bvh_t *tree,
    box_bvh_leaf_t *leaves,
    int32_t count);

/** Get the surface area heuristic cost of a hierarchy: the summed area of its nodes over the
 * area of its root, the number of nodes a random ray through the root visits.
 *
 * @param tree The tree
 * @return The cost, 0 for a tree without nodes
 */
float box_bvh_cost(
    const box_bvh_t *tree);

/** Visit the boxes of every node and item of a hierarchy, depth first from the root.
 *
 * @param tree The tree
 * @param visit Receives the boxes
 * @param ctx Context passed to visit
 * @return Number of boxes visited
 */
int32_t box_bvh_walk(
    const box_bvh_t *tree,
    box_bvh_box_visit_t visit,
    void *ctx);

/** Visit the items whose boxes overlap a box.
 *
 * @param tree The tree
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param visit Receives the items
 * @param ctx Context passed to visit
 * @return false when visit stopped the walk
 */
bool box_bvh_overlap(
    const box_bvh_t *tree,
    const vec3 min,
    const vec3 max,
    box_bvh_visit_t visit,
    void *ctx);

/** Visit the items whose boxes, placed by a matrix, overlap a box.
 *
 * Every box of the tree is tested by its world bounds under the matrix, so an item is visited
 * whenever the world bounds of anything inside its box under that matrix overlap the box.
 *
 * @param tree The tree, whose boxes are in the frame the matrix places
 * @param pose The 16 floats of the matrix, column by column, NULL for none
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param visit Receives the items
 * @param ctx Context passed to visit
 * @return false when visit stopped the walk
 */
bool box_bvh_overlap_placed(
    const box_bvh_t *tree,
    const float *pose,
    const vec3 min,
    const vec3 max,
    box_bvh_visit_t visit,
    void *ctx);

/** Visit the items whose boxes overlap a sphere.
 *
 * @param tree The tree
 * @param center Center of the sphere
 * @param radius Radius of the sphere
 * @param visit Receives the items
 * @param ctx Context passed to visit
 * @return false when visit stopped the walk
 */
bool box_bvh_sphere(
    const box_bvh_t *tree,
    const vec3 center,
    float radius,
    box_bvh_visit_t visit,
    void *ctx);

/** Visit the items whose boxes a ray reaches, nearest subtree first.
 *
 * The reach is read again after every visit, so a visit that shortens it prunes the walk.
 *
 * @param tree The tree
 * @param ray The ray
 * @param reach Distance along the ray beyond which items are skipped
 * @param visit Receives the items
 * @param ctx Context passed to visit
 * @return false when visit stopped the walk
 */
bool box_bvh_ray(
    const box_bvh_t *tree,
    const box_bvh_ray_t *ray,
    const float *reach,
    box_bvh_visit_t visit,
    void *ctx);

/** Visit the items whose boxes, placed by a matrix, a ray reaches, nearest subtree first.
 *
 * Every box of the tree is tested by its world bounds under the matrix, so distances are
 * distances along the ray in the world. The reach is read again after every visit.
 *
 * @param tree The tree, whose boxes are in the frame the matrix places
 * @param pose The 16 floats of the matrix, column by column, NULL for none
 * @param ray The ray, in the world
 * @param reach Distance along the ray beyond which items are skipped
 * @param visit Receives the items
 * @param ctx Context passed to visit
 * @return false when visit stopped the walk
 */
bool box_bvh_ray_placed(
    const box_bvh_t *tree,
    const float *pose,
    const box_bvh_ray_t *ray,
    const float *reach,
    box_bvh_visit_t visit,
    void *ctx);

/** Prepare a ray for a walk.
 *
 * @param ray The ray
 * @param origin Origin of the ray
 * @param dir Direction of the ray, normalized here
 * @return false when the direction has no length
 */
bool box_bvh_ray_init(
    box_bvh_ray_t *ray,
    const vec3 origin,
    const vec3 dir);

/** Intersect a ray with a box.
 *
 * @param ray The ray
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param reach Longest distance that counts
 * @param t Receives the distance at which the ray enters the box, 0 from inside
 * @return true when the ray reaches the box within reach
 */
bool box_bvh_ray_box(
    const box_bvh_ray_t *ray,
    const vec3 min,
    const vec3 max,
    float reach,
    float *t);

/** Test whether a sphere overlaps a box.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param center Center of the sphere
 * @param radius Radius of the sphere
 * @return true when they overlap
 */
bool box_bvh_sphere_box(
    const vec3 min,
    const vec3 max,
    const vec3 center,
    float radius);

/** Test whether two boxes overlap.
 *
 * @param a_min Minimum corner of the first box
 * @param a_max Maximum corner of the first box
 * @param b_min Minimum corner of the second box
 * @param b_max Maximum corner of the second box
 * @return true when they overlap, touching included
 */
bool box_bvh_box_box(
    const vec3 a_min,
    const vec3 a_max,
    const vec3 b_min,
    const vec3 b_max);

#endif
