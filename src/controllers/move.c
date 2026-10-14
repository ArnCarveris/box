/**
 * @file controllers/move.c
 * @brief Moves a controlled entity through the scene, the seam where collision plugs in.
 */

#include "controllers.h"

bool box_controller_move(
    ecs_world_t *world,
    ecs_entity_t entity,
    Position *p,
    vec3 velocity,
    float dt)
{
    bool result = velocity[0] != 0 || velocity[1] != 0 || velocity[2] != 0;
    (void)world;
    (void)entity;
    p->x += velocity[0] * dt;
    p->y += velocity[1] * dt;
    p->z += velocity[2] * dt;

    return result;
}
