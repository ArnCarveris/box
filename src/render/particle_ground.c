/**
 * @file render/particle_ground.c
 * @brief The ground of the particle extract: the plane an emitter collides with, the sampled
 * terrain its spawns stand on and its particles collide with on the GPU, and the entity it
 * follows.
 */

#include "render.h"

const ParticleCollision* box_render_particles_collision(
    const ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *transform,
    ParticleCollision *buf)
{
    const ParticleCollision *result = ecs_get(world, e, ParticleCollision);
    float y = 0;
    if (result && result->ground && result->terrain && ecs_is_alive(world, result->terrain) &&
        box_terrain_ground(world, result->terrain, transform->m[3][0], transform->m[3][2], &y,
            NULL))
    {
        *buf = *result;
        buf->height = y;
        result = buf;
    }

    return result;
}

/** Check whether an entity is a terrain the compute passes can sample.
 *
 * @param world The world
 * @param terrain The entity, 0 for none
 * @return Whether it is alive and has TerrainSampled
 */
static bool box_particles_terrain_sampled(
    const ecs_world_t *world,
    ecs_entity_t terrain)
{
    return terrain && ecs_is_alive(world, terrain) && ecs_has_id(world, terrain, TerrainSampled);
}

void box_render_particles_sampled(
    const ecs_world_t *world,
    ecs_entity_t e,
    const ParticleEmitter *emitter,
    box_render_particle_pool_t *entry)
{
    const ParticleCollision *collision = ecs_get(world, e, ParticleCollision);
    ecs_entity_t terrain = 0;
    uint32_t flags = 0;
    if (emitter->local) {
        goto done;
    }

    if (box_particles_terrain_sampled(world, emitter->ground)) {
        terrain = emitter->ground;
        flags |= BOX_RENDER_PARTICLE_SPAWN_GROUND;
    }

    if (collision && collision->ground && (!terrain || collision->terrain == terrain) &&
        box_particles_terrain_sampled(world, collision->terrain))
    {
        terrain = collision->terrain;
        flags |= BOX_RENDER_PARTICLE_COLLIDE_TERRAIN;
    }

done:
    entry->terrain = terrain;
    entry->block.sampled = flags;
}

const WorldTransform* box_render_particles_follow(
    const ecs_world_t *world,
    const ParticleEmitter *emitter,
    const WorldTransform *transform,
    WorldTransform *buf)
{
    const WorldTransform *result = transform;
    const WorldTransform *target = emitter->follow && ecs_is_alive(world, emitter->follow)
        ? ecs_get(world, emitter->follow, WorldTransform)
        : NULL;
    if (target) {
        *buf = *transform;
        buf->m[3][0] += target->m[3][0];
        buf->m[3][2] += target->m[3][2];
        if (!emitter->ground) {
            buf->m[3][1] += target->m[3][1];
        }

        result = buf;
    }

    return result;
}
