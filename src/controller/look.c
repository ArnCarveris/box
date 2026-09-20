/**
 * @file controller/look.c
 * @brief The view direction, the mouse look and the pose the controllers share.
 */

#include "controller_internal.h"
#include <math.h>

float box_controller_value(
    float value,
    float fallback)
{
    return value > 0 ? value : fallback;
}

bool box_controller_looking(
    ecs_world_t *world,
    const ControllerActions *actions)
{
    const Surface *surface = ecs_singleton_get(world, Surface);

    return (surface && surface->cursor_captured) ||
        box_controller_held(world, actions->look);
}

void box_controller_look(
    ecs_world_t *world,
    const ControllerActions *actions,
    float sensitivity,
    float *yaw,
    float *pitch)
{
    const Input *in = ecs_singleton_get(world, Input);
    if (!in || !box_controller_looking(world, actions)) {
        goto done;
    }

    yaw[0] += in->mouse.delta.x * sensitivity;
    pitch[0] += in->mouse.delta.y * sensitivity;
    if (pitch[0] > BOX_CONTROLLER_PITCH_MAX) {
        pitch[0] = BOX_CONTROLLER_PITCH_MAX;
    }

    if (pitch[0] < -BOX_CONTROLLER_PITCH_MAX) {
        pitch[0] = -BOX_CONTROLLER_PITCH_MAX;
    }

done:
    return;
}

vec3 box_controller_direction(
    ecs_world_t *world,
    const ControllerActions *actions,
    float yaw)
{
    vec3 forward = vec3_new(sinf(yaw), 0, cosf(yaw));
    vec3 right = vec3_new(cosf(yaw), 0, -sinf(yaw));
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

void box_controller_ease(
    vec3 *velocity,
    vec3 target,
    float rate,
    float dt)
{
    float alpha = 1.0f - expf(-rate * dt);
    velocity->x += (target.x - velocity->x) * alpha;
    velocity->y += (target.y - velocity->y) * alpha;
    velocity->z += (target.z - velocity->z) * alpha;
}

vec3 box_controller_forward(
    float yaw,
    float pitch)
{
    float cp = cosf(pitch);

    return vec3_new(sinf(yaw) * cp, -sinf(pitch), cosf(yaw) * cp);
}

void box_controller_pose(
    const Position *p,
    Orientation *o,
    LookAt *look,
    float yaw,
    float pitch)
{
    o->yaw = yaw;
    o->pitch = pitch;
    if (look) {
        vec3 forward = box_controller_forward(yaw, pitch);
        look->x = p->x + forward.x;
        look->y = p->y + forward.y;
        look->z = p->z + forward.z;
    }
}
