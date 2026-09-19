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
 * entity has one. A sphere of the radius slides along the colliders of the
 * scene, which keeps the camera out of geometry, and the terrain entity is the
 * floor it falls back on. Pitch and yaw follow the Orientation convention, so a
 * positive pitch looks down, and a member that is zero or less takes its default.
 */
ECS_STRUCT(CameraController, {
    float speed;          /**< Units per second at full throttle, 10 by default */
    float fast;           /**< Speed multiplier while Fast is held, 4 by default */
    float acceleration;   /**< How fast the velocity follows the input, 8 by default */
    float sensitivity;    /**< Radians turned per point of cursor movement */
    ecs_entity_t terrain; /**< Entity the camera stays above, 0 for none */
    float clearance;      /**< Units kept above the bounds of the terrain */
    float radius;         /**< Radius it keeps free of scene geometry, 0.5 by default */
    bool no_collision;    /**< Fly through the scene instead of colliding with it */
    float yaw;            /**< Yaw in radians, written every frame */
    float pitch;          /**< Pitch in radians, written every frame */
    vec3 velocity;        /**< Velocity in units per second, written every frame */
});

/** First person controller of a character, driven by the same movement actions.
 *
 * The controller walks in the XZ plane at the walk or the run speed, falls with
 * gravity, jumps from the ground and lowers its eye while Crouch is held. The Fly
 * action toggles free flight, which moves like the camera controller. A capsule
 * of the eye height slides along the colliders of the scene, steps onto ledges
 * of at most the step height and finds the ground with a probe below its foot;
 * the terrain entity, or the ground height when there is none, is the floor it
 * falls back on. A member that is zero or less takes its default.
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
 * The system writes Position and LookAt every frame: the camera glides toward
 * the position of the target plus the offset and looks at the target plus the
 * look offset, so it lags behind a target that moves and settles when it stops.
 * It is placed without a glide the first frame it runs. The ZoomIn and ZoomOut
 * actions and the wheel step level through the presets the scene authored,
 * unless the cursor is on a UI element; a camera without presets keeps the
 * offset of the controller. A sphere of the radius slides along the colliders
 * of the scene, which keeps the camera out of geometry, and the terrain entity
 * is the floor it stays above. A member that is zero or less takes its default.
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
