/**
 * @file Lifecycle.c
 * @brief Tests for ParticleStats, the scene of a pool and what deleting, removing or disabling an
 * emitter or replacing the backend does to its pool.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Frame delta of the tests, a fixed clock */
#define DELTA (1.0f / 60.0f)

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

/** Create an emitter with the constructor values, a rate and a long lifetime.
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

/** Count the resource changes of the last packet of a kind and op on a buffer.
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

void Lifecycle_stats(void) {
    // particle/stats
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    emitter_new(world, 0, 120, 1000);
    emitter_new(world, 0, 60, 1000);
    emitter_new(world, 0, 0, 1000);

    ParticleStats stats = frame(world);
    test_int(stats.emitters, 3);
    test_int(stats.simulated, 2);
    test_int(stats.spawned, 3);
    test_int(stats.particles, 3);
    test_int(stats.bytes, SMALL_PAGE);

    stats = frame(world);
    test_int(stats.emitters, 3);
    test_int(stats.simulated, 2);
    test_int(stats.spawned, 3);
    test_int(stats.particles, 6);
    test_int(ecs_vec_count(&null->last.particles), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Lifecycle_stats_count_pages(void) {
    // particle/stats
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i;
    for (i = 0; i < 17; i ++) {
        emitter_new(world, 0, 60, 1000);
    }

    emitter_new(world, 0, 60, 5000);
    ParticleStats stats = frame(world);
    test_int(stats.emitters, 18);
    test_int(stats.simulated, 18);
    test_int(stats.bytes, 2 * SMALL_PAGE + 2 * 8192 * BOX_RENDER_PARTICLE_SIZE);

    ecs_fini(world);
    box_render_null_free(null);
}

void Lifecycle_scene_membership(void) {
    // particle/scenes
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_entity_t inner = emitter_new(world, group, 60, 100);
    ecs_entity_t outer = emitter_new(world, 0, 60, 100);

    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, inner);
    test_assert(pool != NULL);
    test_uint(pool->scene, scene);
    pool = pool_of(null, outer);
    test_assert(pool != NULL);
    test_uint(pool->scene, 0);

    ecs_add_pair(world, inner, EcsChildOf, outer);
    frame(world);
    pool = pool_of(null, inner);
    test_assert(pool != NULL);
    test_uint(pool->scene, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Lifecycle_delete_emitter(void) {
    // particle/lifecycle
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 60, 100);
    ecs_entity_t other = emitter_new(world, 0, 60, 100);

    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    uint32_t page = pool->buffer;
    test_uint(pool_of(null, other)->buffer, page);

    ecs_delete(world, e);
    ParticleStats stats = frame(world);
    test_int(stats.emitters, 1);
    test_int(stats.particles, 2);
    test_assert(pool_of(null, e) == NULL);
    test_int(buffer_changes(null, BOX_RENDER_DESTROY, page), 0);

    ecs_delete(world, other);
    stats = frame(world);
    test_int(stats.emitters, 0);
    test_int(stats.particles, 0);
    test_int(stats.bytes, 0);
    test_int(ecs_vec_count(&null->last.particles), 0);
    test_int(buffer_changes(null, BOX_RENDER_DESTROY, page), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Lifecycle_remove_emitter(void) {
    // particle/lifecycle
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 60, 100);

    frame(world);
    uint32_t page = pool_of(null, e)->buffer;
    ecs_remove(world, e, ParticleEmitter);
    ParticleStats stats = frame(world);
    test_int(stats.emitters, 0);
    test_int(stats.particles, 0);
    test_int(stats.bytes, 0);
    test_assert(pool_of(null, e) == NULL);
    test_int(buffer_changes(null, BOX_RENDER_DESTROY, page), 1);

    ecs_add(world, e, ParticleEmitter);
    ecs_get_mut(world, e, ParticleEmitter)->rate = 60;
    ecs_modified(world, e, ParticleEmitter);
    stats = frame(world);
    test_int(stats.particles, 1);
    test_uint(pool_of(null, e)->block.first, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Lifecycle_disable_emitter(void) {
    // particle/lifecycle
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 60, 100);

    frame(world);
    frame(world);
    ecs_enable(world, e, false);
    ParticleStats stats = frame(world);
    test_int(stats.emitters, 0);
    test_int(stats.particles, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_enable(world, e, true);
    stats = frame(world);
    test_int(stats.emitters, 1);
    test_int(stats.particles, 1);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_bool(pool->clear, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Lifecycle_new_backend_empties_pools(void) {
    // particle/lifecycle
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, 120, 100);

    frame(world);
    test_int(frame(world).particles, 4);

    box_render_null_t *next = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(next);
    box_render_set_backend(world, &backend);
    ParticleStats stats = frame(world);
    test_int(stats.particles, 2);
    const box_render_particle_pool_t *pool = pool_of(next, e);
    test_assert(pool != NULL);
    test_bool(pool->clear, true);
    test_uint(pool->block.first, 0);
    test_int(buffer_changes(next, BOX_RENDER_CREATE, pool->buffer), 1);

    ecs_fini(world);
    box_render_null_free(null);
    box_render_null_free(next);
}
