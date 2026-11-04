/**
 * @file Linger.c
 * @brief Tests for ParticleEmitter.linger: the particles of a deleted lingering emitter keep
 * moving in a pool that spawns nothing, keeps its buffers and is freed after the longest lifetime.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Frame delta of the tests, a fixed clock of a whole number of microseconds */
#define DELTA (0.1f)

/** Bytes of a page of pools of the smallest size */
#define SMALL_PAGE (16 * 1024 * BOX_RENDER_PARTICLE_SIZE)

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

/** Create an emitter that spawns 2 particles a frame living a second.
 *
 * @param world The world
 * @param linger Whether its particles linger after it is deleted
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    bool linger)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 20;
    p->max = 100;
    p->lifetime = (ParticleRange){1, 1};
    p->linger = linger;
    ecs_modified(world, e, ParticleEmitter);

    return e;
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

/** Count the buffer changes of the last packet of an op.
 *
 * @param null The backend
 * @param op What happens to the buffer
 * @param handle Handle of the buffer
 * @return The changes
 */
static int32_t buffer_changes(
    box_render_null_t *null,
    box_render_resource_op_t op,
    uint32_t handle)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        result += r->kind == BOX_RENDER_BUFFER && r->op == op && r->handle == handle;
    }

    return result;
}

/** Count the resource changes of the last packet.
 *
 * @param null The backend
 * @return The changes
 */
static int32_t resource_changes(
    box_render_null_t *null)
{
    return ecs_vec_count(&null->last.resources);
}

void Linger_c_default(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    test_bool(ecs_get(world, e, ParticleEmitter)->linger, false);

    test_assert(ecs_script_run(world, "linger",
        "using box.particle\n"
        "e { ParticleEmitter: {linger: true} }\n", NULL) == 0);
    const ParticleEmitter *p = ecs_get(world, ecs_lookup(world, "e"), ParticleEmitter);
    test_assert(p != NULL);
    test_bool(p->linger, true);
    test_int(p->max, 1000);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_keeps_particles_after_delete(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    test_int(frames(world, 3).particles, 6);

    ecs_delete(world, e);
    ParticleStats stats = frame(world);
    test_int(stats.particles, 6);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.used, 6);
    test_uint(pool->block.delta_us, 100000);
    test_flt(pool->block.acceleration[3], DELTA);

    stats = frames(world, 5);
    test_int(stats.particles, 6);
    test_assert(pool_of(null, e) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_spawns_nothing(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    frames(world, 3);
    test_uint(pool_of(null, e)->block.first, 4);

    ecs_delete(world, e);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ParticleStats stats = frame(world);
        test_int(stats.spawned, 0);
        const box_render_particle_pool_t *pool = pool_of(null, e);
        test_assert(pool != NULL);
        test_uint(pool->block.count, 0);
        test_uint(pool->block.first, 6);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_keeps_buffers(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    frames(world, 3);
    box_render_particle_pool_t before = *pool_of(null, e);

    ecs_delete(world, e);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->buffer, before.buffer);
    test_uint(pool->offset, before.offset);
    test_uint(pool->capacity, before.capacity);
    test_bool(pool->clear, false);
    test_flt(pool->block.model[3][0], before.block.model[3][0]);
    test_uint(pool->block.seed, before.block.seed);
    test_int(resource_changes(null), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_frees_after_longest_lifetime(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    frames(world, 3);
    uint32_t page = pool_of(null, e)->buffer;

    ecs_delete(world, e);
    ParticleStats stats = frames(world, 7);
    test_int(stats.particles, 6);
    stats = frame(world);
    test_int(stats.particles, 4);
    stats = frame(world);
    test_int(stats.particles, 2);
    test_int(stats.lingering, 1);
    test_assert(pool_of(null, e) != NULL);
    test_int(buffer_changes(null, BOX_RENDER_DESTROY, page), 0);

    stats = frame(world);
    test_int(stats.particles, 0);
    test_int(stats.lingering, 0);
    test_int(stats.simulated, 0);
    test_int(stats.bytes, 0);
    test_assert(pool_of(null, e) == NULL);
    test_int(buffer_changes(null, BOX_RENDER_DESTROY, page), 1);

    stats = frame(world);
    test_int(stats.lingering, 0);
    test_int(ecs_vec_count(&null->last.particles), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_stats(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    emitter_new(world, false);
    ParticleStats stats = frames(world, 3);
    test_int(stats.emitters, 2);
    test_int(stats.lingering, 0);
    test_int(stats.particles, 12);
    test_int(stats.bytes, SMALL_PAGE);

    ecs_delete(world, e);
    stats = frame(world);
    test_int(stats.emitters, 1);
    test_int(stats.lingering, 1);
    test_int(stats.simulated, 2);
    test_int(stats.spawned, 2);
    test_int(stats.particles, 14);
    test_int(stats.bytes, SMALL_PAGE);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_without_linger_frees_at_once(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, false);
    frames(world, 3);
    uint32_t page = pool_of(null, e)->buffer;

    ecs_delete(world, e);
    ParticleStats stats = frame(world);
    test_int(stats.particles, 0);
    test_int(stats.lingering, 0);
    test_int(stats.bytes, 0);
    test_assert(pool_of(null, e) == NULL);
    test_int(buffer_changes(null, BOX_RENDER_DESTROY, page), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_remove_frees_at_once(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    frames(world, 3);

    ecs_remove(world, e, ParticleEmitter);
    ParticleStats stats = frame(world);
    test_int(stats.particles, 0);
    test_int(stats.lingering, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_page_reused_after_free(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    ecs_entity_t other = emitter_new(world, false);
    frames(world, 3);
    box_render_particle_pool_t before = *pool_of(null, e);
    test_uint(pool_of(null, other)->buffer, before.buffer);

    ecs_delete(world, e);
    ParticleStats stats = frames(world, 10);
    test_int(stats.lingering, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_entity_t next = emitter_new(world, true);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, next);
    test_assert(pool != NULL);
    test_uint(pool->buffer, before.buffer);
    test_uint(pool->offset, before.offset);
    test_int(buffer_changes(null, BOX_RENDER_CREATE, before.buffer), 0);

    ecs_delete(world, next);
    stats = frame(world);
    test_int(stats.lingering, 1);
    pool = pool_of(null, next);
    test_assert(pool != NULL);
    test_uint(pool->buffer, before.buffer);
    test_uint(pool->offset, before.offset);

    ecs_fini(world);
    box_render_null_free(null);
}

void Linger_light_goes_out(void) {
    // particle/linger
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, true);
    ecs_set(world, e, PointLight, { .intensity = 1, .range = 10 });
    ecs_set(world, e, ParticleLight, { .intensity = 3 });
    frames(world, 3);
    box_render_lights_stats_t lights;
    box_render_lights_stats(world, &lights);
    test_int(lights.lights, 1);

    ecs_delete(world, e);
    ParticleStats stats = frame(world);
    test_int(stats.lingering, 1);
    test_int(stats.particles, 6);
    box_render_lights_stats(world, &lights);
    test_int(lights.lights, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
