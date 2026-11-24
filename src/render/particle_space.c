/**
 * @file render/particle_space.c
 * @brief The space of the particle extract: the space a pool keeps its particles in, and the
 * model and place of a pool that rides a mover.
 */

#include "draw.h"

box_render_particle_space_t box_render_particles_space(
    const ecs_world_t *world,
    ecs_entity_t e,
    const ParticleEmitter *emitter,
    ecs_entity_t *mover)
{
    box_render_particle_space_t result = BOX_RENDER_PARTICLE_WORLD;
    *mover = 0;
    if (emitter->local) {
        result = BOX_RENDER_PARTICLE_EMITTER;
    } else if (emitter->ride) {
        ecs_entity_t m = box_draws_mover_of(world, e);
        if (m && ecs_has(world, m, WorldTransform)) {
            *mover = m;
            result = BOX_RENDER_PARTICLE_MOVER;
        }
    }

    return result;
}

void box_render_particles_ride(
    const ecs_world_t *world,
    ecs_entity_t mover,
    const WorldTransform *transform,
    box_render_particle_block_t *out)
{
    WorldTransform buf;
    mat4 turn, inverse, emitter, place;
    const WorldTransform *m = box_transform_world(world, mover, &buf);
    if (!m) {
        goto done;
    }

    glm_mat4_copy(ECS_CONST_CAST(vec4*, m->m), turn);
    glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), emitter);
    glm_vec3_sub(emitter[3], turn[3], emitter[3]);
    glm_vec3_zero(turn[3]);
    glm_mat4_inv(turn, inverse);
    glm_mat4_mul(inverse, emitter, place);
    ecs_os_memcpy_t(out->place, place, mat4);
    ecs_os_memcpy_t(out->model, m->m, mat4);
done:
    return;
}
