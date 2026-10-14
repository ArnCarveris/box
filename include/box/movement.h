/**
 * @file box/movement.h
 * @brief Velocity components integrated into Position and Rotation every frame.
 *
 * Systems in EcsOnUpdate add Velocity times the world delta time to the local Position and
 * AngularVelocity times the delta time to the local Rotation of the same entity, before the
 * transform systems of EcsPreStore run. Children follow through their parent's transform.
 *
 * Systems write without notifying observers, so Velocity and AngularVelocity imply
 * DynamicTransform (also from script), which spreads to every descendant and stays when the
 * velocity is removed.
 */

#ifndef BOX_MOVEMENT_H
#define BOX_MOVEMENT_H

#include "types.h"

#ifdef BOX_MOVEMENT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Units per second added to Position */
ECS_STRUCT(Velocity, {
    float x; /**< X velocity */
    float y; /**< Y velocity */
    float z; /**< Z velocity */
});

/** Radians per second added to Rotation */
ECS_STRUCT(AngularVelocity, {
    float x; /**< Angular velocity about X */
    float y; /**< Angular velocity about Y */
    float z; /**< Angular velocity about Z */
});

/** Register the box.movement module.
 *
 * @param world The world
 */
void BoxMovementImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
