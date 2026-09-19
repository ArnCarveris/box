/**
 * @file controller/collide.c
 * @brief The capsule move of the scene both controllers travel with.
 */

#include "controller_internal.h"

/** Height a move must fall short of before it counts as blocked upwards */
#define BOX_CONTROLLER_HIT (1e-4f)

bool box_controller_move(
    ecs_world_t *world,
    const box_controller_body_t *body,
    vec3 *foot,
    vec3 *velocity,
    float dt)
{
    vec3 start = foot[0];
    vec3 motion = vec3_scale(velocity[0], dt);
    bool grounded = false;
    if (body->enabled) {
        box_move_desc_t desc = {
            .position = start,
            .motion = motion,
            .radius = body->radius,
            .height = body->height,
            .step = body->step,
            .probe = body->probe,
            .skip = body->entity
        };

        box_move_result_t result;
        box_collision_move(world, &desc, &result);
        foot[0] = result.position;
        grounded = result.grounded;
    } else {
        foot[0] = vec3_add(start, motion);
    }

    if (velocity->y > 0 && foot->y < start.y + motion.y - BOX_CONTROLLER_HIT) {
        velocity->y = 0;
    }

    return grounded;
}
