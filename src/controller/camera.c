/**
 * @file controller/camera.c
 * @brief The OnUpdate system that flies a camera with the movement actions.
 */

#include "controller_internal.h"
#include <math.h>

/** Pitch the controller stops at, so the camera never looks straight up or down */
#define BOX_CAMERA_PITCH_MAX (1.520796f)

/** Units per second the camera moves at when the controller does not say */
#define BOX_CAMERA_SPEED (10.0f)

/** Speed multiplier of the Fast action when the controller does not say */
#define BOX_CAMERA_FAST (4.0f)

/** How fast the velocity follows the input when the controller does not say */
#define BOX_CAMERA_ACCEL (8.0f)

/** Radians the camera turns per point of cursor movement */
#define BOX_CAMERA_SENSITIVITY (0.003f)

/** Value of a controller member, which takes its default when it is not positive.
 *
 * @param value The member
 * @param fallback The default
 * @return The value to use
 */
static float box_camera_value(
    float value,
    float fallback)
{
    return value > 0 ? value : fallback;
}

/** Test whether the cursor turns the camera this frame.
 *
 * @param world The world
 * @param actions The actions of the module
 * @return Whether the cursor is captured or the Look action is held
 */
static bool box_camera_looking(
    ecs_world_t *world,
    const ControllerActions *actions)
{
    const Surface *surface = ecs_singleton_get(world, Surface);

    return (surface && surface->cursor_captured) ||
        box_controller_held(world, actions->look);
}

/** Collect the direction the movement actions ask for, in world space.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param yaw Yaw of the camera in radians
 * @return The direction, not normalized
 */
static vec3 box_camera_direction(
    ecs_world_t *world,
    const ControllerActions *actions,
    float yaw)
{
    vec3 forward = vec3_new(sinf(yaw), 0, cosf(yaw));
    vec3 right = vec3_new(-cosf(yaw), 0, sinf(yaw));
    vec3 result = vec3_new(0, 0, 0);
    if (box_controller_held(world, actions->forward)) {
        result = vec3_add(result, forward);
    }

    if (box_controller_held(world, actions->back)) {
        result = vec3_sub(result, forward);
    }

    if (box_controller_held(world, actions->right)) {
        result = vec3_add(result, right);
    }

    if (box_controller_held(world, actions->left)) {
        result = vec3_sub(result, right);
    }

    if (box_controller_held(world, actions->up)) {
        result.y += 1.0f;
    }

    if (box_controller_held(world, actions->down)) {
        result.y -= 1.0f;
    }

    return result;
}

/** Turn the camera with the cursor movement of the frame.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param cc The controller, its yaw and pitch are written
 */
static void box_camera_look(
    ecs_world_t *world,
    const ControllerActions *actions,
    CameraController *cc)
{
    const Input *in = ecs_singleton_get(world, Input);
    if (!in || !box_camera_looking(world, actions)) {
        goto done;
    }

    float sensitivity = box_camera_value(cc->sensitivity, BOX_CAMERA_SENSITIVITY);
    cc->yaw -= in->mouse.delta.x * sensitivity;
    cc->pitch += in->mouse.delta.y * sensitivity;
    if (cc->pitch > BOX_CAMERA_PITCH_MAX) {
        cc->pitch = BOX_CAMERA_PITCH_MAX;
    }

    if (cc->pitch < -BOX_CAMERA_PITCH_MAX) {
        cc->pitch = -BOX_CAMERA_PITCH_MAX;
    }

done:
    return;
}

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
    vec3 dir = box_camera_direction(world, actions, cc->yaw);
    float length = vec3_length(dir);
    float speed = box_camera_value(cc->speed, BOX_CAMERA_SPEED);
    if (box_controller_held(world, actions->fast)) {
        speed *= box_camera_value(cc->fast, BOX_CAMERA_FAST);
    }

    vec3 target = vec3_new(0, 0, 0);
    if (length > 0) {
        target = vec3_scale(dir, speed / length);
    }

    float alpha = 1.0f - expf(
        -box_camera_value(cc->acceleration, BOX_CAMERA_ACCEL) * dt);
    cc->velocity.x += (target.x - cc->velocity.x) * alpha;
    cc->velocity.y += (target.y - cc->velocity.y) * alpha;
    cc->velocity.z += (target.z - cc->velocity.z) * alpha;
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
        box_camera_look(world, actions, &cc[i]);
        box_camera_move(world, actions, &cc[i], dt);
        p[i].x += cc[i].velocity.x * dt;
        p[i].y += cc[i].velocity.y * dt;
        p[i].z += cc[i].velocity.z * dt;
        box_controller_clearance(
            world, cc[i].terrain, cc[i].clearance, &p[i], &cc[i].velocity);
        o[i].yaw = cc[i].yaw;
        o[i].pitch = cc[i].pitch;
        if (look) {
            float cp = cosf(cc[i].pitch);
            look[i].x = p[i].x + sinf(cc[i].yaw) * cp;
            look[i].y = p[i].y - sinf(cc[i].pitch);
            look[i].z = p[i].z + cosf(cc[i].yaw) * cp;
        }
    }
}
