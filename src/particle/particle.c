/**
 * @file particle/particle.c
 * @brief Registers the box.particle components and hands the emitters to the render extract.
 */

#define BOX_PARTICLE_IMPL
#include <box.h>
#include "../render/render.h"

/** Construct ParticleEmitter values: a white stream of 1000 particles that rise at 1 unit per
 * second for 1 to 2 seconds.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_particle_emitter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ParticleEmitter *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ParticleEmitter){
            .max = 1000,
            .direction = {0, 1, 0},
            .speed = { 1.0f, 1.0f },
            .lifetime = { 1.0f, 2.0f },
            .size = { 0.1f, 0.1f },
            .opacity = { 1.0f, 1.0f },
            .color = {255, 255, 255}
        };
    }
}

void BoxParticleImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxParticle);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, EmitterShape);
    ECS_META_COMPONENT(world, ParticleRange);
    ECS_META_COMPONENT(world, ParticleEmitter);
    ecs_set_hooks(world, ParticleEmitter, { .ctor = box_particle_emitter_ctor });
    ECS_META_COMPONENT(world, PlaybackState);
    ECS_META_COMPONENT(world, ParticlePlayback);
    ECS_META_COMPONENT(world, ParticleBurst);
    ECS_META_COMPONENT(world, ParticleStats);
    ecs_singleton_set(world, ParticleStats, {0});

    box_render_particles_import(world);
}
