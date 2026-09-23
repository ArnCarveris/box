/**
 * @file Effect.c
 * @brief Benchmarks for the one shot effects: the play and the pool it reuses.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/particle/particle_internal.h"

/** Effects the play case fires per iteration */
#define BENCH_EFFECT_COUNT (100)

/** Build the effect template both cases play.
 *
 * @param world The world
 * @return The template entity
 */
static ecs_entity_t bench_effect_template(
    ecs_world_t *world)
{
    ecs_entity_t effect = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, effect, ParticleEmitter, {
        .max = 500, .rate = 0, .shape = EmitterSphere, .radius = 0.5f,
        .speed = {1, 3}, .lifetime = {0.4f, 0.6f}, .size = {0.1f, 0.2f},
        .seed = 7 });
    ecs_set(world, effect, ParticleBurst, { .count = 200, .at = 0 });

    return effect;
}

void Effect_play_100_effects(
    bench_t *b)
{
    // ore_engine/one_shot_effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t effect = bench_effect_template(world);

    bench_set_items(b, BENCH_EFFECT_COUNT);

    while (bench_iter(b)) {
        int32_t i;
        for (i = 0; i < BENCH_EFFECT_COUNT; i ++) {
            bench_keep(box_particle_burst_at(
                world, effect, vec3_new((float)i, 1, 0), 1));
        }

        ecs_progress(world, 1.0f);
    }

    ecs_fini(world);
}

void Effect_update_100_playing_effects(
    bench_t *b)
{
    // ore_engine/one_shot_effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t effect = bench_effect_template(world);

    int32_t i;
    for (i = 0; i < BENCH_EFFECT_COUNT; i ++) {
        box_particle_burst_at(world, effect, vec3_new((float)i, 1, 0), 1);
    }

    bench_set_items(b, BENCH_EFFECT_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.0f);
        bench_keep(ecs_singleton_get(world, ParticleStats)->emitters);
    }

    ecs_fini(world);
}
