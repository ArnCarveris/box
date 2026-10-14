/**
 * @file box/transform.h
 * @brief Transform components: local pose, world matrix and static/dynamic tags.
 *
 * WorldTransform is kept equal to parent * T * R * S: by observers for static
 * entities, every frame in EcsPreStore for DynamicTransform entities.
 *
 * DynamicTransform spreads to every descendant. A descendant that only inherited it loses
 * it with the root; one tagged by the application or with a component that implies it
 * (`(With, DynamicTransform)`, such as Velocity) stays dynamic.
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

/** The engine does not write WorldTransform, the application does */
extern ECS_TAG_DECLARE(ManualTransform);

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
