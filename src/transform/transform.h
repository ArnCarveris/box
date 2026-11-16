/**
 * @file transform/transform.h
 * @brief Functions shared by the static and dynamic transform implementations.
 */

#include <box.h>
#include "transform_version.h"

/** Compute the world transforms of one depth level of the ChildOf hierarchy.
 *
 * @param world The world
 * @param query Query for dynamic ChildOf entities, grouped by depth
 * @param depth The hierarchy depth to compute
 */
void transformDynamicChildOf(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth);

/** Entries of the cache of parent world transforms, a power of two */
#define TRANSFORM_PARENT_CACHE (16384)

/** A parent and the world transform it resolved to in a run of the system */
typedef struct {
    ecs_entity_t parent;          /**< The parent */
    const WorldTransform *t;      /**< World transform of it or its nearest ancestor with one */
    int64_t stamp;                /**< Run the entry was stored in, else the entry is empty */
} transform_parent_cached_t;

/** Parent world transforms of a run of the dynamic transform system. Children of several
 * parents are interleaved in a table, and the pointers stay valid while the deferred system
 * runs. */
typedef struct {
    transform_parent_cached_t *entries; /**< TRANSFORM_PARENT_CACHE entries */
    int64_t stamp;                /**< The current run */
} transform_parent_cache_t;

/** Compute the world transforms of one depth level of the Parent hierarchy.
 *
 * @param world The world
 * @param query Query for dynamic Parent entities, grouped by depth
 * @param depth The hierarchy depth to compute
 * @param cache Parent world transforms of this run
 */
void transformDynamicParent(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth,
    transform_parent_cache_t *cache);

/** Compute the world transform of one entity from its parent and local transform.
 *
 * @param t_parent World transform of the parent, NULL for a root
 * @param t Receives the world transform
 * @param p Local position
 * @param r Local rotation, NULL for none
 * @param s Local scale, NULL for none
 */
void transformSingle(
    const WorldTransform *t_parent,
    WorldTransform *t,
    const Position *p,
    const Rotation *r,
    const Scale *s);

/** Find the world transform of the nearest ancestor that has one.
 *
 * @param world The world
 * @param parent The parent to start the search at
 * @return The world transform, NULL when no ancestor has one
 */
const WorldTransform* transformGetParentWorldTransform(
    const ecs_world_t *world,
    ecs_entity_t parent);

/** Test whether the entities of an OnRemove observer are being deleted.
 *
 * Entities deleted with their parent come without a destination table (Parent hierarchies),
 * from a table marked for deletion (ChildOf cascades), or move to an empty table while their
 * parent's ChildOf record is marked for deletion (the parent is also a relationship target).
 *
 * Work for them is wasted. The root of a deleted tree is not detected, which costs one entry.
 *
 * @param it The observer iterator
 * @return true for an OnRemove event that deletes the entities
 */
bool transformDeleting(
    const ecs_iter_t *it);

/** Stop spreading DynamicTransform to the children of entities that are no longer dynamic.
 *
 * Children that own the tag (added by the application, or by a component such as Velocity
 * that implies DynamicTransform) stay dynamic.
 *
 * @param it The iterator of an OnRemove DynamicTransform observer
 * @param deleting Whether the entities are being deleted
 */
void transformDynamicUnspread(
    ecs_iter_t *it,
    bool deleting);

/** Create a namespace of box.transform, such as static, and make it the scope.
 *
 * @param world The world
 * @param name The name of the namespace
 * @return The previous scope, for ecs_set_scope after the registrations
 */
ecs_entity_t transformScopePush(
    ecs_world_t *world,
    const char *name);

/** Register the systems and observers of dynamic transforms.
 *
 * @param world The world
 */
void TransformDynamicImport(
    ecs_world_t *world);

/** Register the systems and observers of static transforms.
 *
 * Changes of a disabled entity do not reach the observers, so an entity that is enabled again
 * computes its transform from what it has now.
 *
 * @param world The world
 */
void TransformStaticImport(
    ecs_world_t *world);
