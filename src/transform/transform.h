/**
 * @file transform/transform.h
 * @brief Functions shared by the static and dynamic transform implementations.
 */

#include <box.h>

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
 * Entities deleted with their parent come without a destination table (Parent hierarchies)
 * or from a table marked for deletion (ChildOf cascades), and work for them is wasted.
 *
 * The root of a deleted tree is not detected, which costs one entry.
 *
 * @param it The observer iterator
 * @return true for an OnRemove event that deletes the entities
 */
bool transformDeleting(
    const ecs_iter_t *it);

/** Register the systems and observers of dynamic transforms.
 *
 * @param world The world
 */
void TransformDynamicImport(
    ecs_world_t *world);

/** Register the systems and observers of static transforms.
 *
 * @param world The world
 */
void TransformStaticImport(
    ecs_world_t *world);
