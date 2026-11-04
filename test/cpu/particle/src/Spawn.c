/**
 * @file Spawn.c
 * @brief Tests for the spawn accounting of the extract on a fixed clock: the rate, the carried
 * fraction, the full pool, the max clamp and reset, and the deaths at the lifetime.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "simulate.h"

/** Frame delta of the tests, a fixed clock */
#define DELTA (1.0f / 60.0f)

/** Create a world that renders into a null backend.
 *
 * @param null Receives the backend, freed by the caller after the world
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Create an emitter with the constructor values and a rate, max and lifetime.
 *
 * @param world The world
 * @param rate Particles per second
 * @param max Particles alive at once
 * @param lifetime Seconds every particle lives
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    float rate,
    int32_t max,
    float lifetime)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = rate;
    p->max = max;
    p->lifetime = (ParticleRange){lifetime, lifetime};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Run a frame and get the stats it wrote.
 *
 * @param world The world
 * @param delta Seconds of the frame
 * @return The stats
 */
static ParticleStats frame(
    ecs_world_t *world,
    float delta)
{
    ecs_progress(world, delta);
    box_render_sync(world);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_assert(stats != NULL);

    return *stats;
}

/** Find the pool of an emitter in the last packet.
 *
 * @param null The backend
 * @param e The emitter
 * @return The pool, NULL when the packet has none
 */
static const box_render_particle_pool_t* pool_of(
    box_render_null_t *null,
    ecs_entity_t e)
{
    const box_render_particle_pool_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.particles); i ++) {
        const box_render_particle_pool_t *p = ecs_vec_get_t(
            &null->last.particles, box_render_particle_pool_t, i);
        if (p->entity == e) {
            result = p;
        }
    }

    return result;
}

void Spawn_count_on_fixed_clock(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    emitter_new(world, 120, 1000, 100);

    int32_t i, total = 0;
    for (i = 0; i < 30; i ++) {
        ParticleStats stats = frame(world, DELTA);
        test_int(stats.spawned, 2);
        total += stats.spawned;
        test_int(stats.particles, total);
    }

    test_int(total, 60);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_carries_fraction(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    emitter_new(world, 90, 1000, 100);

    int32_t i, total = 0;
    for (i = 0; i < 60; i ++) {
        ParticleStats stats = frame(world, DELTA);
        test_int(stats.spawned, i % 2 ? 2 : 1);
        total += stats.spawned;
    }

    test_int(total, 90);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_zero_delta_spawns_nothing(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 120, 1000, 1);

    ParticleStats stats = frame(world, DELTA);
    test_int(stats.particles, 2);

    ecs_set_time_scale(world, 0);
    stats = frame(world, DELTA);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 2);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.delta_us, 0);
    test_uint(pool->block.count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_max_is_clamped(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 60, 10000000, 1);

    ParticleStats stats = frame(world, DELTA);
    test_int(stats.particles, 1);
    test_int(stats.bytes, (int64_t)BOX_PARTICLE_MAX * BOX_RENDER_PARTICLE_SIZE);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->capacity, BOX_PARTICLE_MAX);
    test_int(ecs_get(world, e, ParticleEmitter)->max, 10000000);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_full_pool_spawns_nothing(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 600, 25, 100);

    ParticleStats stats = frame(world, DELTA);
    test_int(stats.spawned, 10);
    stats = frame(world, DELTA);
    test_int(stats.spawned, 10);
    stats = frame(world, DELTA);
    test_int(stats.spawned, 5);
    test_int(stats.particles, 25);

    int32_t i;
    for (i = 0; i < 10; i ++) {
        stats = frame(world, DELTA);
        test_int(stats.spawned, 0);
        test_int(stats.particles, 25);
        const box_render_particle_pool_t *pool = pool_of(null, e);
        test_assert(pool != NULL);
        test_uint(pool->block.count, 0);
        test_uint(pool->block.used, 25);
        test_uint(pool->block.first, 25);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_full_pool_resumes_after_deaths(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    emitter_new(world, 600, 10, 0.1f);

    ParticleStats stats = frame(world, DELTA);
    test_int(stats.spawned, 10);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        stats = frame(world, DELTA);
        test_int(stats.spawned, 0);
        test_int(stats.particles, 10);
    }

    stats = frame(world, DELTA);
    test_int(stats.spawned, 10);
    test_int(stats.particles, 10);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_max_change_resets_pool(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 600, 100, 100);

    frame(world, DELTA);
    ParticleStats stats = frame(world, DELTA);
    test_int(stats.particles, 20);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_bool(pool->clear, false);

    ecs_get_mut(world, e, ParticleEmitter)->max = 5000;
    ecs_modified(world, e, ParticleEmitter);
    stats = frame(world, DELTA);
    test_int(stats.spawned, 10);
    test_int(stats.particles, 10);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_bool(pool->clear, true);
    test_uint(pool->capacity, 8192);
    test_uint(pool->block.first, 0);
    test_uint(pool->block.used, 10);

    ecs_get_mut(world, e, ParticleEmitter)->rate = 60;
    ecs_modified(world, e, ParticleEmitter);
    stats = frame(world, DELTA);
    test_int(stats.particles, 11);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_particles_die_at_lifetime(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 60, 1000, 0.05f);

    test_int(frame(world, DELTA).particles, 1);
    test_int(frame(world, DELTA).particles, 2);
    test_int(frame(world, DELTA).particles, 3);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        test_int(frame(world, DELTA).particles, 3);
        const box_render_particle_pool_t *pool = pool_of(null, e);
        test_assert(pool != NULL);
        test_uint(pool->block.used, 3);
    }

    ecs_get_mut(world, e, ParticleEmitter)->rate = 0;
    ecs_modified(world, e, ParticleEmitter);
    test_int(frame(world, DELTA).particles, 2);
    test_int(frame(world, DELTA).particles, 1);
    test_int(frame(world, DELTA).particles, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_zero_lifetime_takes_no_slot(void) {
    // particle/spawn
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 120, 1000, 0);

    ParticleStats stats = frame(world, DELTA);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Spawn_same_seed_same_accounting(void) {
    // particle/spawn
    box_render_null_t *null[2];
    ecs_world_t *world[2];
    int32_t w, i, live[2][40];
    for (w = 0; w < 2; w ++) {
        world[w] = world_new(&null[w]);
        ecs_entity_t e = ecs_new(world[w]);
        ecs_add(world[w], e, ParticleEmitter);
        ParticleEmitter *p = ecs_get_mut(world[w], e, ParticleEmitter);
        p->rate = 300;
        p->lifetime = (ParticleRange){0.1f, 0.4f};
        p->seed = 11;
        ecs_modified(world[w], e, ParticleEmitter);
        ecs_set(world[w], e, Position, {0, 0, 0});
        for (i = 0; i < 40; i ++) {
            live[w][i] = frame(world[w], DELTA).particles;
        }
    }

    for (i = 0; i < 40; i ++) {
        test_int(live[0][i], live[1][i]);
    }

    test_assert(live[0][39] > 30 && live[0][39] < 120);

    for (w = 0; w < 2; w ++) {
        ecs_fini(world[w]);
        box_render_null_free(null[w]);
    }
}
