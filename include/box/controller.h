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
 * Writes Position, Orientation and LookAt when present every frame. Movement and turning chase
 * their target speeds at the acceleration and deceleration rates. While Look is held the turn
 * follows the cursor delta and coasts to a stop once released; Fast makes yaw a third as fast.
 *
 * The wheel pushes the camera along the view. A sphere of the radius slides along the colliders
 * with the terrain entity as floor. A positive pitch looks down and a growing yaw turns right, as
 * Orientation defines. A member that is zero or less takes its default.
 */
ECS_STRUCT(CameraController, {
    float speed;          /**< Units per second at full throttle, 40 by default */
    float fast;           /**< Speed multiplier while the fast action is held, 2 by default */
    float acceleration;   /**< How fast the velocity follows a held action, 6 by default */
    float deceleration;   /**< How fast the velocity follows the stop, 18 by default */
    float sensitivity;    /**< Radians turned per point of cursor movement */
    float angular_speed;  /**< Radians per second the turn actions reach, 1.5 by default */
    float angular_acceleration;/**< How fast a turn follows a held action, 5 by default */
    float angular_deceleration;/**< How fast a released turn coasts to a stop, 15 by default */
    ecs_entity_t terrain; /**< Entity the camera stays above, 0 for none */
    float clearance;      /**< Units kept above the bounds of the terrain */
    float radius;         /**< Radius it keeps free of scene geometry, 0.5 by default */
    bool no_collision;    /**< Fly through the scene instead of colliding with it */
    float yaw;            /**< Yaw in radians, written every frame */
    float pitch;          /**< Pitch in radians, written every frame */
    vec3 velocity;        /**< Velocity in units per second, written every frame */
    vec2 angular_velocity;/**< Turn speed in radians per second, x pitch and y yaw */
});

/** First person controller of a character, driven by the same movement actions.
 *
 * Walks in XZ at the walk or run speed, falls, jumps and crouches; Fly toggles free flight. A
 * capsule of the eye height slides along colliders, steps onto ledges up to the step height and
 * falls back on the terrain entity or ground height. A member zero or less takes its default.
 */
ECS_STRUCT(FirstPersonController, {
    float walk;           /**< Units per second while walking, 4 by default */
    float run;            /**< Units per second while Run is held, 8 by default */
    float jump;           /**< Upward speed of a jump, 5 by default */
    float gravity;        /**< Downward acceleration, 18 by default */
    float eye;            /**< Height of the eye above the ground, 1.7 by default */
    float crouch;         /**< Height of the eye while crouching, 0.9 by default */
    float acceleration;   /**< How fast the velocity follows the input, 12 by default */
    float sensitivity;    /**< Radians turned per point of cursor movement */
    ecs_entity_t terrain; /**< Entity whose bounds are the ground, 0 for the height */
    float ground;         /**< Height of the ground without a terrain entity */
    float radius;         /**< Radius of the capsule it walks with, 0.35 by default */
    float step;           /**< Highest ledge it steps onto, 0.45 by default */
    bool no_collision;    /**< Walk through the scene instead of colliding with it */
    float yaw;            /**< Yaw in radians, written every frame */
    float pitch;          /**< Pitch in radians, written every frame */
    vec3 velocity;        /**< Velocity in units per second, written every frame */
    float foot;           /**< Height the foot stands at, written every frame */
    bool grounded;        /**< The character stands on the ground */
    bool flying;          /**< Free flight, toggled by the Fly action */
});

/** Zoom presets a follow camera steps through, which the ECS_STRUCT spells out */
#define BOX_FOLLOW_LEVEL_MAX (4)

/** One zoom preset of a follow camera, as an offset from its target.
 *
 * A preset whose offset is the zero vector is not authored, so the presets in
 * use are the ones before the first empty entry.
 */
ECS_STRUCT(FollowLevel, {
    vec3 offset;      /**< Offset from the target the camera sits at */
    vec3 look_offset; /**< Offset from the target of the point it looks at */
});

/** Third person controller of a camera that follows a target at an offset.
 *
 * Writes Position and LookAt every frame: the camera glides toward the target plus the offset and
 * looks at the target plus the look offset, placed without a glide the first frame. ZoomIn, ZoomOut
 * and the wheel step through the authored presets unless the cursor is on a UI element.
 *
 * Without presets the controller offset is kept. A sphere of the radius slides along colliders and
 * stays above the terrain entity. A member that is zero or less takes its default.
 */
ECS_STRUCT(FollowCamera, {
    ecs_entity_t target;  /**< Entity the camera follows, 0 keeps it where it is */
    vec3 offset;          /**< Offset it sits at while no preset is authored */
    vec3 look_offset;     /**< Offset of the point it looks at, without a preset */
    float glide;          /**< How fast it follows its place per second, 6 by default */
    FollowLevel levels[4];/**< Zoom presets, nearest first, none to use the offset */
    int32_t level;        /**< Preset in use, stepped by the zoom actions and the wheel */
    ecs_entity_t terrain; /**< Entity the camera stays above, 0 for none */
    float clearance;      /**< Units kept above the bounds of the terrain */
    float radius;         /**< Radius it keeps free of scene geometry, 0.5 by default */
    bool no_collision;    /**< Follow through the scene instead of colliding with it */
    bool placed;          /**< The camera was placed once, written by the engine */
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
