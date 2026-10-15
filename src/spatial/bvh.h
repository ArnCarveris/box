/**
 * @file spatial/bvh.h
 * @brief Bounding volume hierarchy of axis aligned boxes, private to box.spatial.
 *
 * A binary tree with one item per leaf: built top down with a binned surface area heuristic,
 * kept balanced by rotations when leaves are inserted or removed one at a time.
 */

#ifndef BOX_SPATIAL_BVH_H
#define BOX_SPATIAL_BVH_H

#include <box.h>

/** Index of a node that does not exist */
#define BOX_BVH_NULL (-1)

/** Node of a hierarchy; a leaf has no left child and holds an item in right */
typedef struct box_bvh_node_t {
    vec3 min;       /**< Minimum corner of the box around the subtree */
    vec3 max;       /**< Maximum corner of the box around the subtree */
    int32_t parent; /**< Parent node, or the next free node of a free node */
    int32_t left;   /**< First child, BOX_BVH_NULL for a leaf */
    int32_t right;  /**< Second child, or the item of a leaf */
    int32_t height; /**< 0 for a leaf, -1 for a free node */
} box_bvh_node_t;

/** A hierarchy with a free list of nodes */
typedef struct box_bvh_t {
    ecs_vec_t nodes;   /**< box_bvh_node_t by index */
    int32_t root;      /**< Root node, BOX_BVH_NULL when the tree is empty */
    int32_t free;      /**< First free node, BOX_BVH_NULL when there is none */
    int32_t leaves;    /**< Leaves in the tree */
} box_bvh_t;

/** A leaf to build a hierarchy from */
typedef struct box_bvh_leaf_t {
    vec3 min;     /**< Minimum corner */
    vec3 max;     /**< Maximum corner */
    int32_t item; /**< Item the leaf holds */
} box_bvh_leaf_t;

/** A ray prepared for walking a hierarchy */
typedef struct box_bvh_ray_t {
    vec3 origin; /**< Origin */
    vec3 dir;    /**< Unit direction */
    vec3 inv;    /**< One over the direction per axis, 0 where the direction is 0 */
} box_bvh_ray_t;

/** Receive an item a walk reached.
 *
 * @param item The item of the leaf
 * @param ctx Context of the walk
 * @return false to stop the walk
 */
typedef bool (*box_bvh_visit_t)(
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

/** Insert a leaf, descending to the sibling that grows the tree least.
 *
 * @param tree The tree
 * @param min Minimum corner of the leaf
 * @param max Maximum corner of the leaf
 * @param item Item the leaf holds
 * @return The leaf node
 */
int32_t box_bvh_insert(
    box_bvh_t *tree,
    const vec3 min,
    const vec3 max,
    int32_t item);

/** Remove a leaf.
 *
 * @param tree The tree
 * @param leaf The leaf node
 */
void box_bvh_remove(
    box_bvh_t *tree,
    int32_t leaf);

/** Give a leaf exact new bounds, keeping its place when a close ancestor still contains them.
 *
 * The leaf gets the bounds and at most a few ancestors grow, else it is inserted again.
 *
 * @param tree The tree
 * @param leaf The leaf node
 * @param min Minimum corner of the new bounds
 * @param max Maximum corner of the new bounds
 * @return The leaf node, which differs from leaf when it was inserted again
 */
int32_t box_bvh_move(
    box_bvh_t *tree,
    int32_t leaf,
    const vec3 min,
    const vec3 max);

/** Give a fat leaf new bounds, inserting it again only when they leave it or it is too large.
 *
 * A leaf inserted again gets the bounds grown by the margin on every side and stretched by
 * the predicted displacement on the side it moves to. A leaf that still contains the bounds
 * stays, unless it outgrew that box by more than four margins (b2DynamicTree::MoveProxy).
 *
 * @param tree The tree
 * @param leaf The leaf node
 * @param min Minimum corner of the new bounds
 * @param max Maximum corner of the new bounds
 * @param margin Slack around the bounds on every side
 * @param ahead Predicted displacement the leaf reaches ahead of the bounds
 * @return The leaf node, which differs from leaf when it was inserted again
 */
int32_t box_bvh_move_fat(
    box_bvh_t *tree,
    int32_t leaf,
    const vec3 min,
    const vec3 max,
    float margin,
    const vec3 ahead);

/** Replace the tree with one built top down from a set of leaves.
 *
 * @param tree The tree
 * @param leaves The leaves, reordered by the build
 * @param count Number of leaves
 * @param nodes Receives the leaf node of every item, indexed by item
 */
void box_bvh_build(
    box_bvh_t *tree,
    box_bvh_leaf_t *leaves,
    int32_t count,
    int32_t *nodes);

/** Get the surface area heuristic cost of a hierarchy: the summed area of its inner nodes over
 * the area of its root, the number of inner nodes a random ray through the root visits.
 *
 * @param tree The tree
 * @return The cost, 0 for a tree without inner nodes
 */
float box_bvh_cost(
    const box_bvh_t *tree);

/** Get a node.
 *
 * @param tree The tree
 * @param node The node index
 * @return The node
 */
box_bvh_node_t* box_bvh_get(
    const box_bvh_t *tree,
    int32_t node);

/** Visit the items of the leaves that overlap a box.
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

/** Visit the items of the leaves that overlap a sphere.
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

/** Visit the items of the leaves a ray reaches, nearest subtree first.
 *
 * The reach is read again after every visit, so a visit that shortens it prunes the walk.
 *
 * @param tree The tree
 * @param ray The ray
 * @param reach Distance along the ray beyond which leaves are skipped
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
