/**
 * @file Emitter.c
 * @brief Tests for the ParticleEmitter component, its defaults and its shaders.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/particle/particle_internal.h"

void Emitter_defaults_are_a_white_upward_stream(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);

    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, ParticleEmitter);

    const ParticleEmitter *emitter = ecs_get(w, e, ParticleEmitter);
    test_assert(emitter != NULL);
    test_int(emitter->max, 1000);
    test_flt(emitter->rate, 100);
    test_int(emitter->shape, EmitterPoint);
    test_flt(emitter->direction.x, 0);
    test_flt(emitter->direction.y, 1);
    test_flt(emitter->direction.z, 0);
    test_flt(emitter->lifetime.min, 1);
    test_flt(emitter->lifetime.max, 2);
    test_flt(emitter->size.min, 0.1f);
    test_flt(emitter->opacity.min, 1);
    test_int(emitter->color.r, 255);
    test_int(emitter->color.g, 255);
    test_int(emitter->color.b, 255);
    test_int(emitter->seed, 1);

    ecs_fini(w);
}

void Emitter_emitter_gets_a_transform_and_its_bookkeeping(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);

    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, ParticleEmitter);

    test_assert(ecs_has(w, e, WorldTransform));
    test_assert(ecs_has(w, e, ParticleEmitterImpl));

    const ParticleEmitterImpl *impl = ecs_get(w, e, ParticleEmitterImpl);
    test_assert(impl != NULL);
    test_int(impl->spawned, 0);
    test_int(impl->points, 0);

    ecs_fini(w);
}

void Emitter_authored_members_keep_the_other_defaults(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);

    test_int(ecs_script_run(w, "test",
        "using box.particle\n"
        "puff {\n"
        "  ParticleEmitter: {rate: 400, shape: EmitterDisc, radius: 2}\n"
        "}\n", NULL), 0);

    ecs_entity_t e = ecs_lookup(w, "puff");
    test_assert(e != 0);

    const ParticleEmitter *emitter = ecs_get(w, e, ParticleEmitter);
    test_assert(emitter != NULL);
    test_flt(emitter->rate, 400);
    test_int(emitter->shape, EmitterDisc);
    test_flt(emitter->radius, 2);
    test_int(emitter->max, 1000);
    test_flt(emitter->lifetime.max, 2);
    test_int(emitter->color.r, 255);

    ecs_fini(w);
}

void Emitter_shaders_are_created_with_the_first_emitter(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);

    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, ParticleEmitter);

    ecs_entity_t sim = ecs_lookup(w, "box.particle.shaders.particle_sim");
    ecs_entity_t draw = ecs_lookup(w, "box.particle.shaders.particle_draw");
    test_assert(sim != 0);
    test_assert(draw != 0);

    const Shader *shader = ecs_get(w, sim, Shader);
    test_assert(shader != NULL);
    test_str(shader->path, "etc/shaders/particle_sim.wgsl");
    test_str(shader->macros, "PARTICLE_GROUP=64");

    ecs_fini(w);
}

void Emitter_a_world_without_an_emitter_compiles_no_shader(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);

    test_int(ecs_lookup(w, "box.particle.shaders.particle_sim"), 0);

    box_packet_t packet;
    box_packet_init(&packet);
    box_particle_extract(w, &packet);
    test_int(packet.particles.count, 0);
    test_int(packet.particle_sim_shader, BOX_SHADER_NONE);
    box_packet_fini(&packet);

    ecs_fini(w);
}
