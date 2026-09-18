/**
 * @file controller/controller_internal.h
 * @brief Private state of the box.controller module: the actions it binds.
 */

#ifndef BOX_CONTROLLER_INTERNAL_H
#define BOX_CONTROLLER_INTERNAL_H

#include <box.h>

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

/** Move and turn the cameras that have a controller.
 *
 * @param it Iterator over the camera controllers
 */
void BoxCameraControllerUpdate(
    ecs_iter_t *it);

#endif
