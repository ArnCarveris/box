/**
 * @file controllers/ride.c
 * @brief Carries a controlled body with the mover it stands on: a walker on the deck of a
 * moving ship goes and turns with it.
 */

#include "controllers.h"
#include "../spatial/index.h"
#include <math.h>

/** Get the yaw a transform turns about the vertical axis.
 *
 * @param m The transform
 * @return The yaw in radians, as Rotation.y turns
 */
static float box_ride_yaw(
    mat4 m)
{
    return atan2f(-m[0][2], m[0][0]);
}

bool box_controller_ride(
    const ecs_world_t *world,
    ecs_entity_t *carrier,
    float *pose,
    Position *p,
    Rotation *r)
{
    mat4 now, last, rest, delta;
    vec3 at;
    bool result = false;
    bool placed = false;

    if (*carrier && !ecs_is_alive(world, *carrier)) {
        *carrier = 0;
    }

    if (*carrier) {
        placed = box_controller_pose(world, *carrier, now);
    }

    if (placed) {
        ecs_os_memcpy(last, pose, ECS_SIZEOF(mat4));
        glm_mat4_inv(last, rest);
        glm_mat4_mul(now, rest, delta);
        glm_mat4_mulv3(delta, (vec3){ p->x, p->y, p->z }, 1.0f, at);
        result = at[0] != p->x || at[1] != p->y || at[2] != p->z;
        p->x = at[0];
        p->y = at[1];
        p->z = at[2];

        if (r) {
            float yaw = box_ride_yaw(delta);
            result = result || yaw != 0;
            r->y += yaw;
        }

        ecs_os_memcpy(pose, now, ECS_SIZEOF(mat4));
    }

    return result;
}

void box_controller_board(
    const ecs_world_t *world,
    ecs_entity_t shape,
    ecs_entity_t *carrier,
    float *pose)
{
    ecs_entity_t mover = shape ? box_spatial_mover_of(world, shape) : 0;
    mat4 now;

    if (mover != *carrier) {
        bool placed = mover && box_controller_pose(world, mover, now);
        *carrier = placed ? mover : 0;
        if (placed) {
            ecs_os_memcpy(pose, now, ECS_SIZEOF(mat4));
        }
    }
}
