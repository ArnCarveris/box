/**
 * @file controllers/pose.c
 * @brief The world transform of a mover as its Position, Rotation and Scale are now.
 *
 * The WorldTransform of a mover is written once a frame, after the controllers have run, so a
 * mover moved earlier in the frame (a Vessel) would carry its riders a frame late.
 */

#include "controllers.h"

bool box_controller_pose(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    const Position *p = ecs_get(world, e, Position);
    const Rotation *r = ecs_get(world, e, Rotation);
    const Scale *s = ecs_get(world, e, Scale);
    ecs_entity_t parent = p ? ecs_get_parent(world, e) : 0;
    WorldTransform buf;
    const WorldTransform *up = parent ? box_transform_world(world, parent, &buf) : NULL;
    const WorldTransform *own = p ? NULL : box_transform_world(world, e, &buf);

    if (p) {
        mat4 local;
        glm_translate_make(local, (vec3){ p->x, p->y, p->z });
        if (r) {
            mat4 turn;
            glm_euler_yxz((vec3){ r->x, r->y, r->z }, turn);
            glm_mat4_mul(local, turn, local);
        }

        if (s) {
            glm_scale(local, (vec3){ s->x, s->y, s->z });
        }

        if (up) {
            glm_mat4_mul(ECS_CONST_CAST(vec4*, up->m), local, out);
        } else {
            glm_mat4_copy(local, out);
        }
    } else if (own) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, own->m), out);
    }

    return p || own;
}
