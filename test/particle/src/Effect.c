/**
 * @file Effect.c
 * @brief Tests for the one shot effects: the pool, the scale, the flash and the retire.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/particle/particle_internal.h"

static ecs_world_t *w;
static ecs_entity_t spark;

void Effect_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    spark = ecs_entity(w, { .name = "Spark" });
    ecs_add_id(w, spark, EcsPrefab);
    ecs_set(w, spark, ParticleEmitter, {
        .max = 100,
        .rate = 0,
        .radius = 1,
        .speed = {1, 2},
        .lifetime = {0.5f, 0.5f},
        .size = {0.1f, 0.2f},
        .extents = {0.5f, 0.5f, 0.5f},
        .acceleration = {0, -1, 0}
    });

    ecs_set(w, spark, ParticleBurst, { .count = 50, .at = 0 });
}

void Effect_teardown(void)
{
    ecs_fini(w);
}

void Effect_a_play_makes_an_instance_of_the_template(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    test_assert(instance != 0);
    test_assert(ecs_has(w, instance, ParticleEmitter));
    test_uint(ecs_get(w, instance, ParticleEffect)->effect, spark);
    test_bool(ecs_has_id(w, instance, EcsDisabled), false);
    test_int(ecs_get(w, instance, ParticlePlayback)->state, ParticlePlaying);
}

void Effect_the_instance_is_placed_at_the_position(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(3, 4, 5), 1);

    const Position *p = ecs_get(w, instance, Position);
    test_assert(p != NULL);
    test_flt(p->x, 3);
    test_flt(p->y, 4);
    test_flt(p->z, 5);
}

void Effect_a_template_without_an_emitter_plays_nothing(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t empty = ecs_entity(w, { .name = "Empty" });
    ecs_add_id(w, empty, EcsPrefab);

    test_uint(box_particle_burst_at(w, empty, vec3_new(0, 0, 0), 1), 0);
    test_uint(box_particle_burst_at(w, 0, vec3_new(0, 0, 0), 1), 0);
}

void Effect_the_scale_multiplies_the_sizes_and_the_speeds(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 2);

    const ParticleEmitter *e = ecs_get(w, instance, ParticleEmitter);
    test_flt(e->radius, 2);
    test_flt(e->speed.min, 2);
    test_flt(e->speed.max, 4);
    test_flt(e->size.min, 0.2f);
    test_flt(e->size.max, 0.4f);
    test_flt(e->extents.y, 1);
    test_flt(e->acceleration.y, -2);
    test_flt(ecs_get(w, instance, ParticleEffect)->scale, 2);
}

void Effect_a_scale_of_zero_plays_the_template_as_it_is(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 0);

    test_flt(ecs_get(w, instance, ParticleEmitter)->size.min, 0.1f);
    test_flt(ecs_get(w, instance, ParticleEffect)->scale, 1);
}

void Effect_a_second_play_while_the_first_runs_makes_a_second_instance(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t first = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);
    ecs_entity_t second = box_particle_burst_at(w, spark, vec3_new(1, 0, 0), 1);

    test_assert(first != second);
    test_flt(ecs_get(w, second, Position)->x, 1);
}

void Effect_the_instance_is_retired_after_the_last_particle_dies(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);
    test_flt(ecs_get(w, instance, ParticleEffect)->retire, 0.5);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_bool(ecs_has_id(w, instance, EcsDisabled), false);

    for (i = 0; i < 2; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_bool(ecs_has_id(w, instance, EcsDisabled), true);
    test_int(ecs_get(w, instance, ParticlePlayback)->state, ParticleStopped);
}

void Effect_a_play_reuses_a_retired_instance(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t first = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_bool(ecs_has_id(w, first, EcsDisabled), true);

    ecs_entity_t second = box_particle_burst_at(w, spark, vec3_new(7, 0, 0), 1);

    test_uint(second, first);
    test_bool(ecs_has_id(w, second, EcsDisabled), false);
    test_flt(ecs_get(w, second, Position)->x, 7);
    test_flt(ecs_get(w, second, ParticleEffect)->time, 0);
    test_int(ecs_get(w, second, ParticlePlayback)->state, ParticlePlaying);
    test_bool(ecs_get(w, second, ParticlePlayback)->reset, true);
}

void Effect_a_pool_belongs_to_one_template(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t smoke = ecs_entity(w, { .name = "Smoke" });
    ecs_add_id(w, smoke, EcsPrefab);
    ecs_set(w, smoke, ParticleEmitter, {
        .max = 10, .rate = 0, .lifetime = {0.5f, 0.5f} });

    ecs_entity_t first = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(w, 0.1f);
    }

    ecs_entity_t other = box_particle_burst_at(w, smoke, vec3_new(0, 0, 0), 1);

    test_assert(other != first);
    test_uint(ecs_get(w, other, ParticleEffect)->effect, smoke);
    test_bool(ecs_has_id(w, first, EcsDisabled), true);
}

void Effect_the_emission_ends_when_the_bursts_have_gone_off(void)
{
    // ore_engine/one_shot_effects
    ecs_entity_t pulses = ecs_entity(w, { .name = "Pulses" });
    ecs_add_id(w, pulses, EcsPrefab);
    ecs_set(w, pulses, ParticleEmitter, {
        .max = 100, .rate = 0, .lifetime = {0.5f, 0.5f} });
    ecs_set(w, pulses, ParticleBurst, { .count = 10, .at = 0.2f, .repeat = 2 });

    ecs_entity_t instance = box_particle_burst_at(w, pulses, vec3_new(0, 0, 0), 1);
    const ParticleEffect *play = ecs_get(w, instance, ParticleEffect);
    test_flt(play->emit, 0.6);
    test_flt(play->retire, 1.1);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_int(ecs_get(w, instance, ParticlePlayback)->state, ParticlePlaying);

    for (i = 0; i < 2; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_int(ecs_get(w, instance, ParticlePlayback)->state, ParticleStopped);
    test_bool(ecs_has_id(w, instance, EcsDisabled), false);
}

void Effect_the_flash_fades_over_the_seconds_of_the_template(void)
{
    // ore_engine/one_shot_effects
    ecs_set(w, spark, PointLight, { .intensity = 8, .range = 6 });
    ecs_set(w, spark, EffectFlash, { .seconds = 0.4f });

    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);
    test_flt(ecs_get(w, instance, PointLight)->intensity, 8);
    test_flt(ecs_get(w, instance, PointLight)->range, 6);
    test_flt(ecs_get(w, instance, EffectFlash)->intensity, 8);

    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, instance, PointLight)->intensity, 8);

    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, instance, PointLight)->intensity, 6);

    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, instance, PointLight)->intensity, 4);
}

void Effect_the_flash_is_dark_when_its_fade_is_over(void)
{
    // ore_engine/one_shot_effects
    ecs_set(w, spark, PointLight, { .intensity = 8, .range = 6 });
    ecs_set(w, spark, EffectFlash, { .seconds = 0.2f });

    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_flt(ecs_get(w, instance, PointLight)->intensity, 0);
}

void Effect_a_replay_lights_the_flash_again(void)
{
    // ore_engine/one_shot_effects
    ecs_set(w, spark, PointLight, { .intensity = 8, .range = 6 });
    ecs_set(w, spark, EffectFlash, { .seconds = 0.2f });

    ecs_entity_t first = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_bool(ecs_has_id(w, first, EcsDisabled), true);
    test_flt(ecs_get(w, first, PointLight)->intensity, 0);

    ecs_entity_t second = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    test_uint(second, first);
    test_flt(ecs_get(w, second, PointLight)->intensity, 8);
}

void Effect_a_flash_that_outlasts_the_particles_holds_the_instance(void)
{
    // ore_engine/one_shot_effects
    ecs_set(w, spark, PointLight, { .intensity = 8, .range = 6 });
    ecs_set(w, spark, EffectFlash, { .seconds = 1.5f });

    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    test_flt(ecs_get(w, instance, ParticleEffect)->retire, 1.5);
}

void Effect_the_burst_of_the_template_reaches_the_packet(void)
{
    // ore_engine/one_shot_effects
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t instance = box_particle_burst_at(w, spark, vec3_new(0, 2, 0), 1);
    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);

    test_int(packet.particles.count, 1);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 50);
    test_flt(block->transform[7], 2);
    test_assert(instance != 0);

    box_packet_fini(&packet);
}

void Effect_a_retired_instance_is_left_out_of_the_packet(void)
{
    // ore_engine/one_shot_effects
    box_packet_t packet;
    box_packet_init(&packet);

    box_particle_burst_at(w, spark, vec3_new(0, 0, 0), 1);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(w, 0.1f);
    }

    box_particle_extract(w, &packet);

    test_int(packet.particles.count, 0);

    box_packet_fini(&packet);
}

void Effect_three_plays_reach_the_packet_as_three_blocks(void)
{
    // ore_engine/one_shot_effects
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_set(w, spark, ParticleEmitter, {
        .max = 600, .rate = 0, .shape = EmitterSphere, .radius = 0.7f,
        .lifetime = {0.7f, 1.0f}, .size = {0.1f, 0.2f} });

    box_particle_burst_at(w, spark, vec3_new(-4, 1, 0), 0.6f);
    box_particle_burst_at(w, spark, vec3_new(0, 1, 0), 1);
    box_particle_burst_at(w, spark, vec3_new(4, 1, 0), 1.8f);
    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);

    test_int(packet.particles.count, 3);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 50);
    test_flt(block->transform[3], -4);
    block = box_vec_at(&packet.particles, 1);
    test_int(block->spawn, 50);
    test_flt(block->transform[3], 0);
    block = box_vec_at(&packet.particles, 2);
    test_int(block->spawn, 50);
    test_flt(block->transform[3], 4);

    box_packet_fini(&packet);
}
