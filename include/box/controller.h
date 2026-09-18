/**
 * @file box/controller.h
 * @brief Controllers that turn named actions into camera and character movement.
 *
 * A controller reads the InputAction state of the actions the module binds by
 * default, so a scene rebinds it by editing the bindings under box.input.map.
 */

#ifndef BOX_CONTROLLER_H
#define BOX_CONTROLLER_H

#include "types.h"

#ifdef BOX_CONTROLLER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Fly controller of a camera, driven by the Forward to Down and Fast actions.
 *
 * The system writes Position and Orientation every frame, and LookAt when the
 * entity has one. Pitch and yaw follow the Orientation convention, so a positive
 * pitch looks down, and a member that is zero or less takes its default.
 */
ECS_STRUCT(CameraController, {
    float speed;          /**< Units per second at full throttle, 10 by default */
    float fast;           /**< Speed multiplier while Fast is held, 4 by default */
    float acceleration;   /**< How fast the velocity follows the input, 8 by default */
    float sensitivity;    /**< Radians turned per point of cursor movement */
    ecs_entity_t terrain; /**< Entity the camera stays above, 0 for none */
    float clearance;      /**< Units kept above the bounds of the terrain */
    float yaw;            /**< Yaw in radians, written every frame */
    float pitch;          /**< Pitch in radians, written every frame */
    vec3 velocity;        /**< Velocity in units per second, written every frame */
});

/** Register the box.controller module.
 *
 * @param world The world
 */
void BoxControllerImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
