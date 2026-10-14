/**
 * @file box/transform.h
 * @brief Transform components: local pose, world matrix and static/dynamic tags.
 *
 * WorldTransform is kept equal to parent * T * R * S: by observers for static
 * entities, every frame in EcsPreStore for DynamicTransform entities.
 *
 * Descendants inherit DynamicTransform through `(IsA, DynamicTransformSpread)` and lose it
 * with the root. One that owns the tag stays dynamic: tagged by the application at any time,
 * in C or in a script, or by a component that implies it, such as Velocity.
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

/** WorldTransform version, used to deduplicate static transforms. */
ECS_STRUCT(WorldTransformVersion, {
    uint64_t version; /**< Frame the static transform was last computed in */
});

/** Transform is recomputed every frame instead of on change, kept on every descendant */
extern ECS_TAG_DECLARE(DynamicTransform);

/** Prefab with DynamicTransform that the descendants of dynamic entities inherit it from */
extern ECS_DECLARE(DynamicTransformSpread);

/** The engine does not write WorldTransform, the application does */
extern ECS_TAG_DECLARE(ManualTransform);

/** Test whether the entities of a table are dynamic.
 *
 * @param world The world
 * @param table The table, NULL for none
 * @return true when the table owns DynamicTransform or inherits it, as spread descendants do
 */
bool box_transform_dynamic(
    const ecs_world_t *world,
    const ecs_table_t *table);

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
