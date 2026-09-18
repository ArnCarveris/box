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
        p[i].x += cc[i].velocity.x * dt;
        p[i].y += cc[i].velocity.y * dt;
        p[i].z += cc[i].velocity.z * dt;
        box_controller_clearance(
            world, cc[i].terrain, cc[i].clearance, &p[i], &cc[i].velocity);
        box_controller_pose(
            &p[i], &o[i], look ? &look[i] : NULL, cc[i].yaw, cc[i].pitch);
    }
}
