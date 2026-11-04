/**
 * @file Playback.c
 * @brief Tests for ParticlePlayback: every state transition, the frozen pool of a paused emitter,
 * the live particles of a stopped one finishing, reset, the playback clock and a restart.
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

/** Create an emitter that spawns 2 particles a frame with a lifetime.
 *
 * @param world The world
 * @param lifetime Seconds every particle lives
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    float lifetime)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 20;
    p->max = 1000;
    p->lifetime = (ParticleRange){lifetime, lifetime};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Set the state of the playback of an emitter, keeping its other members.
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

/** Ask for a reset of the playback of an emitter.
 *
 * @param world The world
 * @param e The emitter
 */
static void reset_set(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ParticlePlayback *p = ecs_ensure(world, e, ParticlePlayback);
    p->reset = true;
    ecs_modified(world, e, ParticlePlayback);
}

/** Get the playback clock the engine wrote on an emitter.
 *
 * @param world The world
 * @param e The emitter
 * @return The seconds
 */
static float time_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const ParticlePlayback *p = ecs_get(world, e, ParticlePlayback);
    test_assert(p != NULL);

    return p->time;
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

/** Run frames.
 *
 * @param world The world
 * @param count Frames to run
 * @return The stats of the last one
 */
static ParticleStats frames(
    ecs_world_t *world,
    int32_t count)
{
    ParticleStats result = {0};
    int32_t i;
    for (i = 0; i < count; i ++) {
        result = frame(world);
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

void Playback_without_playback_plays(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);

    ParticleStats stats = frames(world, 3);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 6);
    test_assert(!ecs_has(world, e, ParticlePlayback));

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_defaults(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    ecs_add(world, e, ParticlePlayback);

    const ParticlePlayback *p = ecs_get(world, e, ParticlePlayback);
    test_int(p->state, ParticlePlaying);
    test_flt(p->time, 0);
    test_bool(p->reset, false);

    ParticleStats stats = frame(world);
    test_int(stats.spawned, 2);
    test_flt(time_of(world, e), 0.1f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_play_to_pause(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePlaying);

    ParticleStats stats = frames(world, 3);
    test_int(stats.particles, 6);
    test_flt(time_of(world, e), 0.3f);

    state_set(world, e, ParticlePaused);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 6);
    test_flt(time_of(world, e), 0.3f);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.count, 0);
    test_uint(pool->block.delta_us, 0);
    test_flt(pool->block.acceleration[3], 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_pause_to_play(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePaused);

    ParticleStats stats = frames(world, 2);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);
    test_flt(time_of(world, e), 0);

    state_set(world, e, ParticlePlaying);
    stats = frame(world);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 2);
    test_flt(time_of(world, e), 0.1f);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.delta_us, 100000);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_play_to_stop(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePlaying);

    ParticleStats stats = frames(world, 2);
    test_int(stats.particles, 4);

    state_set(world, e, ParticleStopped);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 4);
    test_flt(time_of(world, e), 0.2f);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.count, 0);
    test_uint(pool->block.delta_us, 100000);
    test_flt(pool->block.acceleration[3], DELTA);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_stop_to_play(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticleStopped);

    ParticleStats stats = frames(world, 3);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);
    test_flt(time_of(world, e), 0);

    state_set(world, e, ParticlePlaying);
    stats = frame(world);
    test_int(stats.spawned, 2);
    test_flt(time_of(world, e), 0.1f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_pause_to_stop(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0.25f);
    state_set(world, e, ParticlePlaying);
    frames(world, 2);
    state_set(world, e, ParticlePaused);
    ParticleStats stats = frames(world, 3);
    test_int(stats.particles, 4);

    state_set(world, e, ParticleStopped);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 4);
    test_uint(pool_of(null, e)->block.delta_us, 100000);
    stats = frame(world);
    test_int(stats.particles, 2);
    test_flt(time_of(world, e), 0.2f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_stop_to_pause(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePlaying);
    frames(world, 2);
    state_set(world, e, ParticleStopped);
    frame(world);

    state_set(world, e, ParticlePaused);
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 4);
    test_uint(pool_of(null, e)->block.delta_us, 0);
    test_flt(time_of(world, e), 0.2f);

    state_set(world, e, ParticlePlaying);
    stats = frame(world);
    test_int(stats.spawned, 2);
    test_flt(time_of(world, e), 0.1f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_pause_freezes_pool(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0.3f);
    state_set(world, e, ParticlePlaying);
    ParticleStats stats = frames(world, 2);
    test_int(stats.particles, 4);

    state_set(world, e, ParticlePaused);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        stats = frame(world);
        test_int(stats.spawned, 0);
        test_int(stats.particles, 4);
        test_int(stats.simulated, 1);
        const box_render_particle_pool_t *pool = pool_of(null, e);
        test_assert(pool != NULL);
        test_uint(pool->block.count, 0);
        test_uint(pool->block.used, 4);
        test_uint(pool->block.delta_us, 0);
        test_flt(pool->block.acceleration[3], 0);
    }

    state_set(world, e, ParticlePlaying);
    stats = frame(world);
    test_int(stats.particles, 6);
    stats = frame(world);
    test_int(stats.particles, 6);
    test_flt(time_of(world, e), 0.4f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_stop_lets_particles_finish(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0.3f);
    state_set(world, e, ParticlePlaying);
    frames(world, 2);

    state_set(world, e, ParticleStopped);
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 4);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 2);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);
    stats = frames(world, 5);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_reset_clears_pool_and_clock(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePlaying);
    ParticleStats stats = frames(world, 3);
    test_int(stats.particles, 6);

    reset_set(world, e);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);
    test_flt(time_of(world, e), 0);
    test_bool(ecs_get(world, e, ParticlePlayback)->reset, false);
    test_int(ecs_get(world, e, ParticlePlayback)->state, ParticlePlaying);
    test_assert(pool_of(null, e) == NULL);

    stats = frame(world);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 2);
    test_flt(time_of(world, e), 0.1f);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_bool(pool->clear, true);
    test_uint(pool->block.first, 0);
    test_uint(pool->block.used, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_reset_while_paused(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePlaying);
    frames(world, 3);
    state_set(world, e, ParticlePaused);
    frame(world);

    reset_set(world, e);
    ParticleStats stats = frame(world);
    test_int(stats.particles, 0);
    test_flt(time_of(world, e), 0);
    test_bool(ecs_get(world, e, ParticlePlayback)->reset, false);

    stats = frames(world, 3);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 0);
    test_flt(time_of(world, e), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_time_advances_only_while_playing(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    state_set(world, e, ParticlePlaying);
    frames(world, 2);
    test_flt(time_of(world, e), 0.2f);

    state_set(world, e, ParticlePaused);
    frames(world, 4);
    test_flt(time_of(world, e), 0.2f);

    state_set(world, e, ParticlePlaying);
    frame(world);
    test_flt(time_of(world, e), 0.3f);

    state_set(world, e, ParticleStopped);
    frames(world, 4);
    test_flt(time_of(world, e), 0.3f);

    ParticlePlayback *p = ecs_ensure(world, e, ParticlePlayback);
    p->state = ParticlePaused;
    p->time = 5;
    ecs_modified(world, e, ParticlePlayback);
    frame(world);
    test_flt(time_of(world, e), 0.3f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_restart_after_stop(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 100);
    ParticleEmitter *em = ecs_get_mut(world, e, ParticleEmitter);
    em->rate = 15;
    ecs_modified(world, e, ParticleEmitter);
    state_set(world, e, ParticlePlaying);
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 1);

    state_set(world, e, ParticleStopped);
    frames(world, 2);
    state_set(world, e, ParticlePlaying);

    stats = frame(world);
    test_int(stats.spawned, 1);
    test_int(stats.particles, 2);
    test_flt(time_of(world, e), 0.1f);
    stats = frame(world);
    test_int(stats.spawned, 2);
    test_flt(time_of(world, e), 0.2f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Playback_script_control(void) {
    // particle/playback
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_assert(ecs_script_run(world, "paused",
        "using box.particle\n"
        "e {\n"
        "  ParticleEmitter: {rate: 20, lifetime: {100, 100}}\n"
        "  ParticlePlayback: {state: ParticlePaused}\n"
        "}\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);

    ParticleStats stats = frames(world, 2);
    test_int(stats.spawned, 0);

    test_assert(ecs_script_run(world, "play",
        "using box.particle\n"
        "e { ParticlePlayback: {state: ParticlePlaying} }\n", NULL) == 0);
    stats = frames(world, 2);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 4);
    test_flt(time_of(world, e), 0.2f);

    test_assert(ecs_script_run(world, "reset",
        "using box.particle\n"
        "e { ParticlePlayback: {state: ParticleStopped, reset: true} }\n", NULL) == 0);
    stats = frame(world);
    test_int(stats.particles, 0);
    test_flt(time_of(world, e), 0);

    ecs_fini(world);
    box_render_null_free(null);
}
