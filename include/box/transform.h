/**
 * @file box/transform.h
 * @brief Transform components: local pose, world matrix and static/dynamic tags.
 *
 * WorldTransform is kept equal to parent * T * R * S: by observers for static
 * entities, every PostUpdate for DynamicTransform entities. DynamicTransform
 * spreads to every descendant and is removed from them with the root.
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

/** Euler angles in radians, composed as Rx * Ry * Rz */
ECS_STRUCT(Rotation, {
    float x; /**< Angle about X */
    float y; /**< Angle about Y */
    float z; /**< Angle about Z */
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
    uint64_t version;
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
