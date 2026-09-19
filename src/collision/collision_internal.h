/**
 * @file collision/collision_internal.h
 * @brief State and helpers of the box.collision module.
 *
 * The state holds the roots that were marked since the last flush, with the
 * kind of work each one needs, the entities whose collider has to be resolved
 * again and the instances that still have to be turned into roots.
 */

#ifndef BOX_COLLISION_INTERNAL_H
#define BOX_COLLISION_INTERNAL_H

#include <box.h>
#include "../transform/transform_internal.h"

/** The bounds of the root must be aggregated from its subtree again */
#define BOX_ROOT_FULL (1)

/** The root moved, so its cached local bounds only have to be transformed */
#define BOX_ROOT_MOVED (2)

/** Index of a node that is not there */
#define BOX_BVH_NULL (-1)

/** Node of a bounding volume hierarchy */
typedef struct box_bvh_node_t {
    vec3 min;       /**< Minimum corner of the enlarged box */
    vec3 max;       /**< Maximum corner of the enlarged box */
    int32_t parent; /**< Parent node, or the next free node */
    int32_t child1; /**< First child, BOX_BVH_NULL for a leaf */
    int32_t child2; /**< Second child */
    int32_t height; /**< Height of the subtree, -1 when the node is free */
    uint64_t user;  /**< Value the owner of the tree put on a leaf */
} box_bvh_node_t;

/** Bounding volume hierarchy a leaf can enter and leave at any time */
typedef struct box_bvh_t {
    ecs_vec_t nodes; /**< Nodes, with a free list threaded through them */
    int32_t root;    /**< Root node, BOX_BVH_NULL when the tree is empty */
    int32_t free;    /**< First free node */
    int32_t count;   /**< Leaves in the tree */
} box_bvh_t;

/** Test a walk over a hierarchy runs against the box of a node */
typedef enum box_bvh_kind_t {
    BoxQueryAabb = 0,    /**< Overlap with a box */
    BoxQuerySphere = 1,  /**< Overlap with a sphere */
    BoxQueryRay = 2,     /**< A ray that shortens as it hits */
    BoxQueryFrustum = 3  /**< Overlap with a plane bounded volume */
} box_bvh_kind_t;

/** One walk over a hierarchy */
typedef struct box_bvh_query_t {
    box_bvh_kind_t kind;                 /**< The test the walk runs */
    vec3 min;                            /**< Minimum corner of a box query */
    vec3 max;                            /**< Maximum corner of a box query */
    vec3 origin;                         /**< Center or origin of the query */
    vec3 dir;                            /**< Direction of a ray query */
    vec3 inv;                            /**< One over the direction, per axis */
    float radius;                        /**< Radius of a sphere query */
    float max_dist;                      /**< Reach of a ray query */
    const box_frustum_planes_t *frustum; /**< Planes of a frustum query */
} box_bvh_query_t;

/** Callback that receives the user value of a leaf a walk kept.
 *
 * @param user The value the leaf carries
 * @param ctx Context the caller of the walk passed
 * @return true to keep walking
 */
typedef bool (*box_bvh_hit_t)(
    uint64_t user,
    void *ctx);

/** One entity that moves every frame, with the collider of this frame */
typedef struct box_index_dyn_t {
    ecs_entity_t entity; /**< The entity */
    Collider collider;   /**< Its collider, resolved by the last update */
} box_index_dyn_t;

/** The two level spatial index of a world */
typedef struct box_index_t {
    box_bvh_t statics;  /**< Roots and the colliders that are not below one */
    box_bvh_t dynamics; /**< Entities that move every frame */
    ecs_map_t proxies;  /**< Entity to its leaf in statics, one based */
    ecs_map_t lists;    /**< Root entity to the vector of colliders it covers */
    ecs_vec_t movers;   /**< box_index_dyn_t the leaves of dynamics point at */
    ecs_query_t *query; /**< Entities that move every frame */
    float margin;       /**< Slack around a leaf of statics */
    bool dynamic;       /**< Index the entities that move every frame */
    bool enabled;       /**< The index is kept and answers queries */
} box_index_t;

/** Collision state of a world, owned by the CollisionState singleton */
typedef struct box_collision_state_t {
    ecs_map_t dirty;     /**< Root entity to the flags it was marked with */
    ecs_map_t colliders; /**< Entities whose collider must be resolved again */
    ecs_vec_t pass;      /**< Roots a flush took off the dirty map */
    ecs_vec_t resolve;   /**< Entities a flush took off the collider map */
    ecs_vec_t instances; /**< Instances that still need a RootBounds */
    box_index_t index;   /**< The spatial index of the world */
    bool shapes;         /**< The shape observers of box.geometry exist */
} box_collision_state_t;

/** Private singleton that owns the state of a world */
typedef struct CollisionState {
    box_collision_state_t *state; /**< Freed when the component is destroyed */
} CollisionState;

extern ECS_COMPONENT_DECLARE(CollisionState);

/** Create the state of a world.
 *
 * @return The state
 */
box_collision_state_t* box_collision_state_new(void);

/** Free the state of a world.
 *
 * @param state The state
 */
void box_collision_state_free(
    box_collision_state_t *state);

/** Mark a root as needing work before the next query.
 *
 * @param state The state
 * @param root The root entity
 * @param flags BOX_ROOT_FULL, BOX_ROOT_MOVED or both
 */
void box_collision_mark(
    box_collision_state_t *state,
    ecs_entity_t root,
    int32_t flags);

/** Mark an entity as needing a new collider before the next query.
 *
 * @param state The state
 * @param e The entity
 */
void box_collision_touch(
    box_collision_state_t *state,
    ecs_entity_t e);

/** Find the nearest root at or above an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The root, 0 when the entity is not below one
 */
ecs_entity_t box_collision_root_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Work out the collider of an entity in world space.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the collider
 * @return true when the entity has a collider at all
 */
bool box_collider_resolve(
    const ecs_world_t *world,
    ecs_entity_t e,
    Collider *out);

/** Find the direction and the distance that separate a capsule from a collider.
 *
 * The segment of the capsule and the surface of the collider are brought
 * together with the walk the distance tests use, and the normal points from the
 * collider towards the capsule, which is the way out of it.
 *
 * @param c The collider
 * @param a First end of the segment of the capsule in world space
 * @param b Second end of the segment of the capsule in world space
 * @param radius Radius of the capsule
 * @param normal Receives the direction that separates the two
 * @param depth Receives how far the capsule reaches into the collider
 * @return true when the capsule overlaps the collider
 */
bool box_collider_push(
    const Collider *c,
    vec3 a,
    vec3 b,
    float radius,
    vec3 *normal,
    float *depth);

/** Register the observers that recognise roots and mark them.
 *
 * @param world The world
 * @param state The state
 */
void box_collision_roots_init(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Register the observers that mark entities for a new collider.
 *
 * @param world The world
 * @param state The state
 */
void box_collision_colliders_init(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Register the observers on the primitive shapes once box.geometry exists.
 *
 * @param world The world
 * @param state The state
 */
void box_collision_shapes_init(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Turn the instances that were created since the last flush into roots.
 *
 * @param world The world, never a stage
 * @param state The state
 */
void box_collision_roots_promote(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Give every marked root the bounds of its subtree.
 *
 * @param world The world, never a stage
 * @param state The state
 */
void box_collision_roots_flush(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Give every marked entity the collider its shape asks for.
 *
 * @param world The world, never a stage
 * @param state The state
 */
void box_collision_colliders_flush(
    ecs_world_t *world,
    box_collision_state_t *state);

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

/** Drop every node of a hierarchy, keeping its memory.
 *
 * @param tree The tree
 */
void box_bvh_clear(
    box_bvh_t *tree);

/** Add a leaf to a hierarchy.
 *
 * @param tree The tree
 * @param min Minimum corner of the leaf
 * @param max Maximum corner of the leaf
 * @param margin Slack added around the box
 * @param user Value the leaf carries
 * @return The leaf, which keeps its index until it is removed
 */
int32_t box_bvh_insert(
    box_bvh_t *tree,
    vec3 min,
    vec3 max,
    float margin,
    uint64_t user);

/** Take a leaf out of a hierarchy.
 *
 * @param tree The tree
 * @param proxy The leaf
 */
void box_bvh_remove(
    box_bvh_t *tree,
    int32_t proxy);

/** Give a leaf a new box, moving it in the tree when it no longer fits.
 *
 * @param tree The tree
 * @param proxy The leaf
 * @param min Minimum corner of the new box
 * @param max Maximum corner of the new box
 * @param margin Slack added around the box when the leaf moves
 * @return true when the leaf had to move
 */
bool box_bvh_move(
    box_bvh_t *tree,
    int32_t proxy,
    vec3 min,
    vec3 max,
    float margin);

/** Hand every leaf a query keeps to a callback.
 *
 * @param tree The tree
 * @param q The query, whose reach a ray hit shortens
 * @param cb The callback
 * @param ctx Context passed to the callback
 */
void box_bvh_query(
    const box_bvh_t *tree,
    box_bvh_query_t *q,
    box_bvh_hit_t cb,
    void *ctx);

/** Fill in a ray query.
 *
 * @param q The query
 * @param origin Origin of the ray
 * @param dir Direction of the ray
 * @param max_dist Longest distance that counts as a hit
 */
void box_bvh_ray_setup(
    box_bvh_query_t *q,
    vec3 origin,
    vec3 dir,
    float max_dist);

/** Set up an empty index.
 *
 * @param index The index
 */
void box_index_init(
    box_index_t *index);

/** Free the trees and lists of an index.
 *
 * @param index The index
 */
void box_index_fini(
    box_index_t *index);

/** Get the collider list of a root, creating it when it is the first one.
 *
 * @param index The index
 * @param root The root
 * @return The list of entities
 */
ecs_vec_t* box_index_list(
    box_index_t *index,
    ecs_entity_t root);

/** Give an entity a leaf in the static tree, or move the one it has.
 *
 * @param index The index
 * @param e The entity
 * @param min Minimum corner of its box
 * @param max Maximum corner of its box
 */
void box_index_set(
    box_index_t *index,
    ecs_entity_t e,
    vec3 min,
    vec3 max);

/** Take the leaf and the collider list of an entity out of the index.
 *
 * @param index The index
 * @param e The entity
 */
void box_index_unset(
    box_index_t *index,
    ecs_entity_t e);

/** Rebuild the tree of the entities that move every frame.
 *
 * @param world The world, never a stage
 * @param state The state
 */
void box_index_movers(
    ecs_world_t *world,
    box_collision_state_t *state);

#endif
