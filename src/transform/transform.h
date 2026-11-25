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

/** Compute the world transforms of one depth level of the Parent hierarchy.
 *
 * @param world The world
 * @param query Query for dynamic Parent entities, grouped by depth
 * @param depth The hierarchy depth to compute
 */
void transformDynamicParent(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth);

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

/** Entries of the cache of computed world transforms, a power of two */
#define TRANSFORM_WORLD_CACHE (16384)

/** The transform of a spread entity relative to its anchor, the nearest ancestor that keeps
 * its WorldTransform (box_transform_spread), which box_transform_world computed */
typedef struct {
    ecs_entity_t entity;          /**< The entity, 0 for an empty entry */
    ecs_entity_t anchor;          /**< The anchor, 0 when no ancestor keeps a WorldTransform */
    uint64_t epoch;               /**< Epoch of the cache it was computed in, else it is stale */
    uint64_t version;             /**< Version of the anchor it was computed at, else stale */
    WorldTransform rel;           /**< The transform relative to the anchor */
} transform_world_cached_t;

/** The transforms of spread entities relative to their anchors. A local transform that changes
 * under an anchor advances its version; a change of a hierarchy, DynamicTransform, ManualTransform,
 * a removed Position or the update of the dynamic transforms in a frame, as their local transforms
 * may be written without an event, advances the epoch. Anchors that move change neither. */
typedef struct {
    transform_world_cached_t *entries; /**< TRANSFORM_WORLD_CACHE entries, NULL until used */
    uint64_t epoch;               /**< Advances with every change of a hierarchy and frame */
    ecs_map_t versions;           /**< Anchor to its version, 0 for one not in the map */
} transform_world_cache_t;

/** Singleton through which box_transform_world finds the cache of a world */
typedef struct {
    transform_world_cache_t *cache; /**< The cache, freed with the world */
} TransformWorldCache;

extern ECS_COMPONENT_DECLARE(TransformWorldCache);

/** Drop every transform box_transform_world computed: a hierarchy, DynamicTransform or
 * ManualTransform changed, or an entity lost its Position.
 *
 * @param world The world
 */
void transformWorldCacheInvalidate(
    const ecs_world_t *world);

/** Drop the transforms box_transform_world computed under the anchors of entities whose local
 * transform changed, the entities of an observer event.
 *
 * @param it The observer iterator
 */
void transformWorldCacheLocal(
    const ecs_iter_t *it);

/** Find the world transform of the nearest ancestor that has one (box_transform_world).
 *
 * @param world The world
 * @param parent The parent to start the search at
 * @param buf Receives the transform when it is computed
 * @return The world transform, NULL when no ancestor has one
 */
const WorldTransform* transformGetParentWorldTransform(
    const ecs_world_t *world,
    ecs_entity_t parent,
    WorldTransform *buf);

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
