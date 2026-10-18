/**
 * @file Burst.c
 * @brief Tests for the ParticleBurst schedule on the playback clock: at 0, a period, a repeat
 * count and endless repeats, bursts on descendants, the free slots and the playback states.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Frame delta of the tests, a fixed clock of a whole number of microseconds */
#define DELTA (0.1f)

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

/** Create an emitter with a rate, a max and particles that outlive the tests.
 *
 * @param world The world
 * @param parent Parent of the emitter, 0 for none
 * @param rate Particles per second
 * @param max Particles alive at once
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float rate,
    int32_t max)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = rate;
    p->max = max;
    p->lifetime = (ParticleRange){100, 100};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Set the state of the playback of an emitter.
 *
 * @param world The world
 * @param e The emitter
 * @param state The state
 */
static void state_set(
    ecs_world_t *world,
    ecs_entity_t e,
    PlaybackState state)
{
    ParticlePlayback *p = ecs_ensure(world, e, ParticlePlayback);
    p->state = state;
    ecs_modified(world, e, ParticlePlayback);
}

/** Run a frame and get the stats it wrote.
 *
 * @param world The world
 * @return The stats
 */
static ParticleStats frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_assert(stats != NULL);

    return *stats;
}

/** Run frames and record what each spawned.
 *
 * @param world The world
 * @param count Frames to run
 * @param out Receives the spawns of each frame, count of them
 * @return The spawns of every frame together
 */
static int32_t spawns(
    ecs_world_t *world,
    int32_t count,
    int32_t *out)
{
    int32_t i, result = 0;
    for (i = 0; i < count; i ++) {
        out[i] = frame(world).spawned;
        result += out[i];
    }

    return result;
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

void Burst_defaults(void) {
    // particle/burst
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleBurst);

    const ParticleBurst *b = ecs_get(world, e, ParticleBurst);
    test_assert(b != NULL);
    test_int(b->count, 0);
    test_flt(b->at, 0);
    test_int(b->repeat, 0);

    test_assert(ecs_script_run(world, "burst",
        "using box.particle\n"
        "b { ParticleBurst: {count: 40, at: 0.5, repeat: -1} }\n", NULL) == 0);
    b = ecs_get(world, ecs_lookup(world, "b"), ParticleBurst);
    test_assert(b != NULL);
    test_int(b->count, 40);
    test_flt(b->at, 0.5f);
    test_int(b->repeat, -1);

    ecs_fini(world);
}

void Burst_at_zero_once(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 10});

    int32_t s[6];
    test_int(spawns(world, 6, s), 10);
    test_int(s[0], 10);
    test_int(s[1], 0);
    test_int(frame(world).particles, 10);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_at_zero_repeat_ignored(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 10, .repeat = 3});
    ecs_entity_t other = emitter_new(world, 0, 0, 1000);
    ecs_set(world, other, ParticleBurst, {.count = 5, .repeat = -1});

    int32_t s[8];
    test_int(spawns(world, 8, s), 15);
    test_int(s[0], 15);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_period_once(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 5, .at = 0.5f});

    int32_t s[15], i;
    test_int(spawns(world, 15, s), 5);
    for (i = 0; i < 15; i ++) {
        test_int(s[i], i == 4 ? 5 : 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_repeat_count(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 5, .at = 0.2f, .repeat = 2});

    int32_t s[20], i;
    test_int(spawns(world, 20, s), 15);
    for (i = 0; i < 20; i ++) {
        test_int(s[i], (i == 1 || i == 3 || i == 5) ? 5 : 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_endless_repeat(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 3, .at = 0.3f, .repeat = -1});

    int32_t s[30], i;
    test_int(spawns(world, 30, s), 30);
    for (i = 0; i < 30; i ++) {
        test_int(s[i], i % 3 == 2 ? 3 : 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_alongside_rate(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 20, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 10});

    ParticleStats stats = frame(world);
    test_int(stats.spawned, 12);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.count, 12);
    test_uint(pool->block.first, 0);
    stats = frame(world);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 14);
    test_uint(pool_of(null, e)->block.first, 12);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_larger_than_free(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 8);
    ecs_set(world, e, ParticleBurst, {.count = 5, .at = 0.1f, .repeat = -1});

    ParticleStats stats = frame(world);
    test_int(stats.spawned, 5);
    stats = frame(world);
    test_int(stats.spawned, 3);
    test_int(stats.particles, 8);
    test_uint(pool_of(null, e)->block.count, 3);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 8);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_on_descendant(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, child, ParticleBurst, {.count = 7});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, ParticleBurst, {.count = 3, .at = 0.2f});
    ecs_set(world, e, ParticleBurst, {.count = 1, .at = 0.1f});

    ParticleStats stats = frame(world);
    test_int(stats.emitters, 1);
    test_int(stats.spawned, 8);
    test_int(ecs_vec_count(&null->last.particles), 1);
    test_uint(pool_of(null, e)->block.count, 8);
    stats = frame(world);
    test_int(stats.spawned, 3);
    test_int(stats.particles, 11);
    stats = frame(world);
    test_int(stats.spawned, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_nearest_emitter(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t outer = emitter_new(world, 0, 0, 1000);
    ecs_entity_t inner = emitter_new(world, outer, 0, 1000);
    ecs_entity_t layer = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, layer, ParticleBurst, {.count = 4});
    ecs_entity_t side = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, side, ParticleBurst, {.count = 6});

    ParticleStats stats = frame(world);
    test_int(stats.spawned, 10);
    const box_render_particle_pool_t *pool = pool_of(null, inner);
    test_assert(pool != NULL);
    test_uint(pool->block.count, 4);
    pool = pool_of(null, outer);
    test_assert(pool != NULL);
    test_uint(pool->block.count, 6);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_without_emitter_ignored(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, b, ParticleBurst, {.count = 4});
    ecs_set(world, parent, ParticleBurst, {.count = 4});
    emitter_new(world, 0, 0, 1000);

    ParticleStats stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_pause_holds_schedule(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 5, .at = 0.3f});
    state_set(world, e, ParticlePlaying);

    int32_t s[5];
    test_int(spawns(world, 2, s), 0);
    state_set(world, e, ParticlePaused);
    test_int(spawns(world, 5, s), 0);
    state_set(world, e, ParticlePlaying);
    test_int(frame(world).spawned, 5);
    test_flt(ecs_get(world, e, ParticlePlayback)->time, 0.3f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_stop_spawns_nothing(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 5});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, child, ParticleBurst, {.count = 5, .at = 0.1f, .repeat = -1});
    state_set(world, e, ParticleStopped);

    int32_t s[5];
    test_int(spawns(world, 5, s), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_restart_after_stop(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 5});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, child, ParticleBurst, {.count = 2, .at = 0.2f});
    state_set(world, e, ParticlePlaying);

    int32_t s[4];
    test_int(spawns(world, 4, s), 7);
    test_int(s[0], 5);
    test_int(s[1], 2);

    state_set(world, e, ParticleStopped);
    test_int(spawns(world, 3, s), 0);

    state_set(world, e, ParticlePlaying);
    test_int(spawns(world, 4, s), 7);
    test_int(s[0], 5);
    test_int(s[1], 2);
    test_int(frame(world).particles, 14);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_reset_restarts_schedule(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = 5});
    state_set(world, e, ParticlePlaying);
    test_int(frame(world).spawned, 5);
    test_int(frame(world).spawned, 0);

    ParticlePlayback *p = ecs_ensure(world, e, ParticlePlayback);
    p->reset = true;
    ecs_modified(world, e, ParticlePlayback);
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);

    stats = frame(world);
    test_int(stats.spawned, 5);
    test_int(stats.particles, 5);

    ecs_fini(world);
    box_render_null_free(null);
}

void Burst_negative_count(void) {
    // particle/burst
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 0, 1000);
    ecs_set(world, e, ParticleBurst, {.count = -5});

    ParticleStats stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
