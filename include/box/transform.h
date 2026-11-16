/**
 * @file box/transform.h
 * @brief Transform components: local pose, world matrix and static/dynamic tags.
 *
 * WorldTransform = parent * T * R * S, kept by observers for static entities and every frame for
 * movers. A descendant that inherits DynamicTransform through `(IsA, DynamicTransformSpread)`
 * has a WorldTransform that is not kept up to date: read it with box_transform_world.
 *
 * Rotation and Scale imply Position, and every pose component implies WorldTransform. An entity
 * without a WorldTransform has no place in the world: it is not drawn and not picked.
 */

#ifndef BOX_TRANSFORM_H
#define BOX_TRANSFORM_H

#include "types.h"

#ifdef BOX_TRANSFORM_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Position relative to the parent */
ECS_STRUCT(Position, {
    float x; /**< X coordinate */
    float y; /**< Y coordinate */
    float z; /**< Z coordinate */
});

/** Euler angles in radians: x is pitch, y is yaw and z is roll.
 *
 * The angles are applied as yaw, then pitch, then roll, each about the axis of the
 * already rotated frame (the matrix is Ry * Rx * Rz). Combined pitch and yaw never
 * produce roll, so a camera rotated by pitch and yaw keeps a level horizon.
 */
ECS_STRUCT(Rotation, {
    float x; /**< Pitch, angle about X */
    float y; /**< Yaw, angle about Y */
    float z; /**< Roll, angle about Z */
});

/** Scale relative to the parent, defaults to one */
ECS_STRUCT(Scale, {
    float x; /**< X scale */
    float y; /**< Y scale */
    float z; /**< Z scale */
});

/** Local to world matrix, computed by the engine */
ECS_STRUCT(WorldTransform, {
    mat4 m; /**< The matrix */
});

/** Transform is recomputed every frame instead of on change, inherited by every descendant */
extern ECS_TAG_DECLARE(DynamicTransform);

/** Prefab with DynamicTransform that the descendants of dynamic entities inherit it from */
extern ECS_DECLARE(DynamicTransformSpread);

/** The engine does not write WorldTransform, the application does.
 *
 * Static descendants follow when the application writes it with ecs_set, or through a pointer
 * followed by ecs_modified; a write without an event leaves them at the old transform.
 */
extern ECS_TAG_DECLARE(ManualTransform);

/** Test whether the WorldTransform of the entities of a table is not kept: they inherit
 * DynamicTransform through `(IsA, DynamicTransformSpread)`, own no DynamicTransform nor
 * ManualTransform and have a Position.
 *
 * @param world The world
 * @param table The table, NULL for none
 * @return true when box_transform_world computes their world transform
 */
bool box_transform_spread(
    const ecs_world_t *world,
    const ecs_table_t *table);

/** Get the world transform of an entity: its WorldTransform, or for a descendant of a dynamic
 * entity whose WorldTransform is not kept (box_transform_spread) the transform of its nearest
 * ancestor with one times its local transform, computed into a buffer.
 *
 * @param world The world
 * @param e The entity, 0 for none
 * @param buf Receives the transform when it is computed
 * @return The world transform, NULL when the entity has no WorldTransform
 */
const WorldTransform* box_transform_world(
    const ecs_world_t *world,
    ecs_entity_t e,
    WorldTransform *buf);

/** Get the world transform of a row of a query result with a WorldTransform field owned by its
 * entities: the field, or box_transform_world when the table keeps none (box_transform_spread).
 *
 * @param world The world
 * @param it The query result
 * @param field The WorldTransform field, NULL when it is not set
 * @param row The row
 * @param buf Receives the transform when it is computed
 * @return The world transform, NULL when the field is not set
 */
const WorldTransform* box_transform_world_field(
    const ecs_world_t *world,
    const ecs_iter_t *it,
    const WorldTransform *field,
    int32_t row,
    WorldTransform *buf);

/** Test whether the entities of a table are dynamic.
 *
 * @param world The world
 * @param table The table, NULL for none
 * @return true when the table owns DynamicTransform or inherits it, as spread descendants do
 */
bool box_transform_dynamic(
    const ecs_world_t *world,
    const ecs_table_t *table);

/** Compute the local matrix of a pose, T * R * S, as the transform systems compose it into
 * WorldTransform: the rotation is Ry * Rx * Rz (glm_euler_yxz), each column scaled, then the
 * translation. Pure, so the render thread computes rendered transitions with it.
 *
 * @param p Local position
 * @param r Local rotation, NULL for none
 * @param s Local scale, NULL for none
 * @param out Receives the matrix
 */
void box_transform_local(
    const Position *p,
    const Rotation *r,
    const Scale *s,
    mat4 out);

/** Get the uniform scale of a world transform, the cube root of the absolute determinant of
 * its upper 3x3: the geometric mean of the axis lengths without shear. A scale within float
 * rounding of 1, as of a rotation, is 1, so an unscaled entity is unchanged bit for bit.
 *
 * @param transform The world transform
 * @return The scale, 0 for a flat transform, 1 for one that is not finite
 */
float box_transform_world_scale(
    const WorldTransform *transform);

/** Receives the entities whose WorldTransform a TransformStatic run changed, once per run
 * that changed any: every computed root and every descendant whose WorldTransform it wrote.
 *
 * An entity can be passed more than once. A root is passed also when it lost its
 * WorldTransform with its Position; ManualTransform descendants keep the transform the
 * application wrote and are not passed.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The context passed to box_transform_on_moved
 */
typedef void (*box_transform_moved_t)(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx);

/** Call a function with the entities whose static transform TransformStatic changed.
 *
 * Static transforms are computed in EcsPreStore, so a change made later in a frame reaches
 * the function in the next frame, when its transform is known. Dynamic entities are never
 * passed.
 *
 * @param world The world
 * @param callback The function
 * @param ctx Context passed to the function
 * @param ctx_free Frees the context when the world is deleted, NULL for none
 */
void box_transform_on_moved(
    ecs_world_t *world,
    box_transform_moved_t callback,
    void *ctx,
    ecs_ctx_free_t ctx_free);

/** Call an observer callback for every entity that gains or loses DynamicTransform, owned or
 * inherited (through `(IsA, DynamicTransformSpread)` or a prefab).
 *
 * One box.transform observer serves every function, which costs less than an observer per
 * module: an inherited tag reaches an observer through a forwarded event per entity.
 *
 * The function gets the iterator of that observer with ctx replaced by its own context.
 * Entities deleted with their parent are not passed: the components they lose report that.
 *
 * @param world The world
 * @param callback The function
 * @param ctx Context the function finds in it->ctx
 * @param ctx_free Frees the context when the world is deleted, NULL for none
 */
void box_transform_on_dynamic(
    ecs_world_t *world,
    ecs_iter_action_t callback,
    void *ctx,
    ecs_ctx_free_t ctx_free);

/** Register the box.transform module.
 *
 * @param world The world
 */
void BoxTransformImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
