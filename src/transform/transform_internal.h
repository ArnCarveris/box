/**
 * @file transform/transform_internal.h
 * @brief Private helpers of the box.transform module.
 */

#ifndef BOX_TRANSFORM_INTERNAL_H
#define BOX_TRANSFORM_INTERNAL_H

#include <box.h>

/** Callback that visits one child of an entity.
 *
 * @param world The world
 * @param e The child
 * @param ctx The context the caller passed to the walk
 */
typedef void (*box_hierarchy_visit_t)(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx);

/** Get the parent of an entity in either hierarchy storage.
 *
 * @param world The world
 * @param e The entity
 * @return The parent, 0 when the entity is a root
 */
ecs_entity_t box_hierarchy_parent(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Visit every child of an entity in both hierarchy storages.
 *
 * The callback may write components of the children it visits but must not
 * create, delete or reparent entities; collect them first for that.
 *
 * @param world The world
 * @param e The entity
 * @param visit The callback
 * @param ctx Context passed to the callback
 */
void box_hierarchy_children(
    ecs_world_t *world,
    ecs_entity_t e,
    box_hierarchy_visit_t visit,
    void *ctx);

/** Append every child of an entity to a vector, in both hierarchy storages.
 *
 * @param world The world
 * @param e The entity
 * @param out Vector of ecs_entity_t that receives the children
 */
void box_hierarchy_collect(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *out);

/** Test whether an entity or one of its ancestors has an id.
 *
 * @param world The world
 * @param e The entity
 * @param id The id to look for
 * @return true when the entity or an ancestor has the id
 */
bool box_hierarchy_ancestor_has(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id);

/** Test whether a table holds entities that have a parent.
 *
 * @param world The world
 * @param table The table, NULL for none
 * @return true when the table has a ChildOf pair or a Parent component
 */
bool box_hierarchy_is_child(
    const ecs_world_t *world,
    const ecs_table_t *table);

/** Give an entity a new parent, keeping the hierarchy storage it uses.
 *
 * @param world The world
 * @param e The entity
 * @param parent The new parent, 0 to make the entity a root
 */
void box_hierarchy_set_parent(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t parent);

/** Compose a local matrix from a pose.
 *
 * @param p Position, NULL for the origin
 * @param r Rotation, NULL and no orientation for no rotation
 * @param s Scale, NULL for one
 * @param o Orientation, wins over the rotation when set
 * @return The local transform
 */
mat4 box_transform_compose(
    const Position *p,
    const Rotation *r,
    const Scale *s,
    const Orientation *o);

/** Register the observers that update static world transforms.
 *
 * @param world The world
 */
void box_transform_static_init(
    ecs_world_t *world);

/** Register the system that turns LookAt into a Rotation.
 *
 * @param world The world
 */
void box_transform_lookat_init(
    ecs_world_t *world);

/** Register the system that updates dynamic world transforms.
 *
 * @param world The world
 */
void box_transform_dynamic_init(
    ecs_world_t *world);

#endif
