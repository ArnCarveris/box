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
    WorldTransform buf;
    const WorldTransform *now = NULL;
    mat4 last, rest, delta;
    vec3 at;
    bool result = false;

    if (*carrier && !ecs_is_alive(world, *carrier)) {
        *carrier = 0;
    }

    if (*carrier) {
        now = box_transform_world(world, *carrier, &buf);
    }

    if (now) {
        ecs_os_memcpy(last, pose, ECS_SIZEOF(mat4));
        glm_mat4_inv(last, rest);
        glm_mat4_mul(ECS_CONST_CAST(vec4*, now->m), rest, delta);
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

        ecs_os_memcpy(pose, now->m, ECS_SIZEOF(mat4));
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
    WorldTransform buf;
    const WorldTransform *now = NULL;

    if (mover != *carrier) {
        now = mover ? box_transform_world(world, mover, &buf) : NULL;
        *carrier = now ? mover : 0;
        if (now) {
            ecs_os_memcpy(pose, now->m, ECS_SIZEOF(mat4));
        }
    }
}
