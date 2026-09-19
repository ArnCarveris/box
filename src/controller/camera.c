/**
 * @file controller/camera.c
 * @brief The OnUpdate system that flies a camera with the movement actions.
 */

#include "controller_internal.h"
#include <math.h>

/** Units per second the camera moves at when the controller does not say */
#define BOX_CAMERA_SPEED (10.0f)

/** Speed multiplier of the Fast action when the controller does not say */
#define BOX_CAMERA_FAST (4.0f)

/** How fast the velocity follows the input when the controller does not say */
#define BOX_CAMERA_ACCEL (8.0f)

/** Radius the camera keeps free of geometry when the controller does not say */
#define BOX_CAMERA_RADIUS (0.5f)

/** Move the velocity of a controller towards what its actions ask for.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param cc The controller, its velocity is written
 * @param dt Seconds the frame took
 */
static void box_camera_move(
    ecs_world_t *world,
    const ControllerActions *actions,
    CameraController *cc,
    float dt)
{
    vec3 dir = box_controller_direction(world, actions, cc->yaw);
    float length = vec3_length(dir);
    float speed = box_controller_value(cc->speed, BOX_CAMERA_SPEED);
    if (box_controller_held(world, actions->fast)) {
        speed *= box_controller_value(cc->fast, BOX_CAMERA_FAST);
    }

    vec3 target = vec3_new(0, 0, 0);
    if (length > 0) {
        target = vec3_scale(dir, speed / length);
    }

    box_controller_ease(&cc->velocity, target,
        box_controller_value(cc->acceleration, BOX_CAMERA_ACCEL), dt);
}

/** Move a camera through the scene, sliding along the geometry it runs into.
 *
 * @param world The world
 * @param cc The controller, its fall is stopped by a ceiling
 * @param e The entity of the camera
 * @param p The position, written
 * @param dt Seconds the frame took
 */
static void box_camera_travel(
    ecs_world_t *world,
    CameraController *cc,
    ecs_entity_t e,
    Position *p,
    float dt)
{
    float radius = box_controller_value(cc->radius, BOX_CAMERA_RADIUS);
    box_controller_body_t body = {
        .entity = e,
        .radius = radius,
        .height = radius * 2.0f,
        .enabled = !cc->no_collision
    };

    vec3 foot = vec3_new(p->x, p->y - radius, p->z);
    box_controller_move(world, &body, &foot, &cc->velocity, dt);
    p->x = foot.x;
    p->y = foot.y + radius;
    p->z = foot.z;
}

void BoxCameraControllerUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    CameraController *cc = ecs_field(it, CameraController, 0);
    Position *p = ecs_field(it, Position, 1);
    Orientation *o = ecs_field(it, Orientation, 2);
    LookAt *look = ecs_field(it, LookAt, 3);
    const ControllerActions *actions = ecs_singleton_get(world, ControllerActions);
    float dt = it->delta_time;
    int32_t i;
    for (i = 0; dt > 0 && i < it->count; i ++) {
        box_controller_look(world, actions,
            box_controller_value(cc[i].sensitivity, BOX_CONTROLLER_SENSITIVITY),
            &cc[i].yaw, &cc[i].pitch);
        box_camera_move(world, actions, &cc[i], dt);
        box_camera_travel(world, &cc[i], it->entities[i], &p[i], dt);
        box_controller_clearance(
            world, cc[i].terrain, cc[i].clearance, &p[i], &cc[i].velocity);
        box_controller_pose(
            &p[i], &o[i], look ? &look[i] : NULL, cc[i].yaw, cc[i].pitch);
    }
}
