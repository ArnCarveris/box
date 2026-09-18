/**
 * @file box/transform.h
 * @brief Transform components: local pose, world matrix and static/dynamic tags.
 *
 * The box.transform module keeps WorldTransform equal to parent * T * R * S.
 * Static entities are updated by observers when their pose or parent changes,
 * entities with DynamicTransform on themselves or an ancestor are updated every
 * frame in PostUpdate.
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

/** Gimbal safe pose composed as Ry * Rx * Rz, wins over Rotation when both are present */
ECS_STRUCT(Orientation, {
    float pitch; /**< Angle about X */
    float yaw;   /**< Angle about Y */
    float roll;  /**< Angle about Z */
});

/** Target point, Rotation is written every frame so local +Z faces it */
ECS_STRUCT(LookAt, {
    float x; /**< Target X */
    float y; /**< Target Y */
    float z; /**< Target Z */
});

/** Local to world matrix, computed by the engine */
ECS_STRUCT(WorldTransform, {
    mat4 m; /**< The matrix */
});

/** Axis aligned bounds in world space */
ECS_STRUCT(AABB, {
    vec3 min; /**< Minimum corner */
    vec3 max; /**< Maximum corner */
});

/** Transform is recomputed every frame instead of on change, applies to descendants */
extern ECS_TAG_DECLARE(DynamicTransform);

/** The engine does not write WorldTransform, the application does */
extern ECS_TAG_DECLARE(ManualTransform);

/** Subtree moves as one piece; child poses are composed on the GPU */
extern ECS_TAG_DECLARE(RigidSubtree);

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
