/**
 * @file controller/controller_internal.h
 * @brief Private state of the box.controller module: the actions it binds.
 */

#ifndef BOX_CONTROLLER_INTERNAL_H
#define BOX_CONTROLLER_INTERNAL_H

#include <box.h>

/** Pitch the controllers stop at, so a view never looks straight up or down */
#define BOX_CONTROLLER_PITCH_MAX (1.520796f)

/** Radians a view turns per point of cursor movement by default */
#define BOX_CONTROLLER_SENSITIVITY (0.003f)

/** The actions the module binds by default, looked up once at import */
typedef struct ControllerActions {
    ecs_entity_t forward; /**< Move along the view direction */
    ecs_entity_t back;    /**< Move against the view direction */
    ecs_entity_t left;    /**< Strafe left */
    ecs_entity_t right;   /**< Strafe right */
    ecs_entity_t up;      /**< Move up */
    ecs_entity_t down;    /**< Move down */
    ecs_entity_t fast;    /**< Move faster while held */
    ecs_entity_t look;    /**< Turn with the cursor while held */
    ecs_entity_t jump;    /**< Jump when the character stands on the ground */
    ecs_entity_t crouch;  /**< Lower the eye while held */
    ecs_entity_t run;     /**< Walk at the run speed while held */
    ecs_entity_t fly;     /**< Toggle free flight */
} ControllerActions;

extern ECS_COMPONENT_DECLARE(ControllerActions);

/** Create an action under the actions scope and bind it when nothing else does.
 *
 * @param world The world
 * @param name Name of the action under the actions scope
 * @param description What the action does
 * @param mask Key mask the default binding fires it with
 * @return The action entity
 */
ecs_entity_t box_controller_action(
    ecs_world_t *world,
    const char *name,
    const char *description,
    uint64_t mask);

/** Test whether an action is active this frame.
 *
 * @param world The world
 * @param action The action entity, 0 for none
 * @return Whether the action is held
 */
bool box_controller_held(
    ecs_world_t *world,
    ecs_entity_t action);

/** Keep an entity above the bounds of a terrain entity.
 *
 * @param world The world
 * @param terrain The terrain entity, 0 for none
 * @param clearance Units to keep above its bounds
 * @param p The position, clamped in place
 * @param velocity Velocity of the entity, its fall is stopped in place
 */
void box_controller_clearance(
    ecs_world_t *world,
    ecs_entity_t terrain,
    float clearance,
    Position *p,
    vec3 *velocity);

/** Create the default actions of the module and their bindings.
 *
 * @param world The world
 */
void box_controller_actions_init(
    ecs_world_t *world);

/** Value of a controller member, which takes its default when it is not positive.
 *
 * @param value The member
 * @param fallback The default
 * @return The value to use
 */
float box_controller_value(
    float value,
    float fallback);

/** Turn a view with the cursor movement of the frame.
 *
 * The view only turns while the cursor is captured or the Look action is held,
 * and the pitch stops short of straight up and straight down.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param sensitivity Radians per point of cursor movement
 * @param yaw Yaw of the view in radians, written in place
 * @param pitch Pitch of the view in radians, written in place
 */
void box_controller_look(
    ecs_world_t *world,
    const ControllerActions *actions,
    float sensitivity,
    float *yaw,
    float *pitch);

/** Collect the direction the movement actions ask for, in world space.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param yaw Yaw of the view in radians
 * @return The direction, not normalized
 */
vec3 box_controller_direction(
    ecs_world_t *world,
    const ControllerActions *actions,
    float yaw);

/** Move a velocity towards a target, smoothed over the frame.
 *
 * @param velocity The velocity, written in place
 * @param target Velocity the input asks for
 * @param rate How fast the velocity follows the target, per second
 * @param dt Seconds the frame took
 */
void box_controller_ease(
    vec3 *velocity,
    vec3 target,
    float rate,
    float dt);

/** Write the pose of a view, and the point it looks at when it has a LookAt.
 *
 * @param p Position of the entity
 * @param o Orientation, written
 * @param look LookAt of the entity, NULL when it has none
 * @param yaw Yaw of the view in radians
 * @param pitch Pitch of the view in radians
 */
void box_controller_pose(
    const Position *p,
    Orientation *o,
    LookAt *look,
    float yaw,
    float pitch);

/** Move and turn the cameras that have a controller.
 *
 * @param it Iterator over the camera controllers
 */
void BoxCameraControllerUpdate(
    ecs_iter_t *it);

/** Walk, jump and fly the characters that have a first person controller.
 *
 * @param it Iterator over the first person controllers
 */
void BoxFirstPersonUpdate(
    ecs_iter_t *it);

#endif
