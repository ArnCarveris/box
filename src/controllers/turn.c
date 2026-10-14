/**
 * @file controllers/turn.c
 * @brief The turn the camera and first person controllers share: eased keys, direct mouse look.
 */

#include "controllers.h"
#include <math.h>

/** Pitch the turn stops at, short of looking straight up or down */
#define BOX_CONTROLLER_PITCH_MAX ((float)GLM_PI_2 - 0.05f)

bool box_controller_turn(
    const box_controller_turn_t *turn,
    vec2 angular_velocity,
    Rotation *r,
    float dt)
{
    float tx = turn->pitch, ty = turn->yaw;
    float len = sqrtf(tx * tx + ty * ty);
    float rate = len > 0 ? turn->acceleration : turn->deceleration;
    float alpha = 1.0f - expf(-rate * dt);
    float dy = turn->invert_y ? -turn->delta[1] : turn->delta[1];
    float pitch = r->x, yaw = r->y;
    bool result = false;
    if (len > 0) {
        tx *= turn->speed / len;
        ty *= turn->speed * turn->yaw_scale / len;
    }

    angular_velocity[0] += (tx - angular_velocity[0]) * alpha;
    angular_velocity[1] += (ty - angular_velocity[1]) * alpha;
    yaw += angular_velocity[1] * dt;
    pitch += angular_velocity[0] * dt;
    if (turn->look) {
        yaw -= turn->delta[0] * turn->sensitivity * turn->yaw_scale;
        pitch -= dy * turn->sensitivity;
    }

    if (pitch > BOX_CONTROLLER_PITCH_MAX) {
        pitch = BOX_CONTROLLER_PITCH_MAX;
        angular_velocity[0] = angular_velocity[0] > 0 ? 0 : angular_velocity[0];
    }

    if (pitch < -BOX_CONTROLLER_PITCH_MAX) {
        pitch = -BOX_CONTROLLER_PITCH_MAX;
        angular_velocity[0] = angular_velocity[0] < 0 ? 0 : angular_velocity[0];
    }

    result = yaw != r->y || pitch != r->x;
    r->y = yaw;
    r->x = pitch;

    return result;
}
