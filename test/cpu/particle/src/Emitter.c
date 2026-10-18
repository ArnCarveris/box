/**
 * @file Emitter.c
 * @brief Tests for the ParticleEmitter values and the block the extract sends for a pool.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
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

/** Run a frame and wait until the backend received it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
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

/** Check the constructor values of an emitter.
 *
 * @param p The emitter
 */
static void check_defaults(
    const ParticleEmitter *p)
{
    test_int(p->max, 1000);
    test_flt(p->rate, 0);
    test_int(p->shape, EmitterPoint);
    test_flt(p->radius, 0);
    test_flt(p->extents[0], 0);
    test_flt(p->extents[1], 0);
    test_flt(p->extents[2], 0);
    test_uint(p->source, 0);
    test_flt(p->direction[0], 0);
    test_flt(p->direction[1], 1);
    test_flt(p->direction[2], 0);
    test_flt(p->spread, 0);
    test_flt(p->speed.min, 1);
    test_flt(p->speed.max, 1);
    test_flt(p->lifetime.min, 1);
    test_flt(p->lifetime.max, 2);
    test_flt(p->size.min, 0.1f);
    test_flt(p->size.max, 0.1f);
    test_flt(p->rotation.min, 0);
    test_flt(p->rotation.max, 0);
    test_flt(p->spin.min, 0);
    test_flt(p->spin.max, 0);
    test_flt(p->opacity.min, 1);
    test_flt(p->opacity.max, 1);
    test_flt(p->acceleration[0], 0);
    test_flt(p->acceleration[1], 0);
    test_flt(p->acceleration[2], 0);
    test_int(p->color.r, 255);
    test_int(p->color.g, 255);
    test_int(p->color.b, 255);
    test_uint(p->seed, 0);
}

void Emitter_c_defaults(void) {
    // particle/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    const ParticleEmitter *p = ecs_get(world, e, ParticleEmitter);
    test_assert(p != NULL);
    check_defaults(p);

    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_assert(stats != NULL);

    ecs_fini(world);
}

void Emitter_script_defaults(void) {
    // particle/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.particle\n"
        "plain { ParticleEmitter: {} }\n"
        "stream { ParticleEmitter: {rate: 400} }\n"
        "custom { ParticleEmitter: {max: 20, rate: 5, shape: EmitterBox, radius: 2, "
        "extents: {1, 2, 3}, direction: {1, 0, 0}, spread: 30, speed: {2, 4}, "
        "lifetime: {3, 5}, size: {0.5, 1}, rotation: {10, 20}, spin: {-90, 90}, "
        "opacity: {0.25, 0.75}, acceleration: {0, -9.8, 0}, color: {10, 20, 30}, "
        "seed: 7} }\n", NULL) == 0);

    const ParticleEmitter *p = ecs_get(world, ecs_lookup(world, "plain"), ParticleEmitter);
    test_assert(p != NULL);
    check_defaults(p);

    p = ecs_get(world, ecs_lookup(world, "stream"), ParticleEmitter);
    test_assert(p != NULL);
    test_flt(p->rate, 400);
    test_int(p->max, 1000);
    test_flt(p->direction[1], 1);
    test_flt(p->speed.min, 1);
    test_flt(p->lifetime.max, 2);

    p = ecs_get(world, ecs_lookup(world, "custom"), ParticleEmitter);
    test_assert(p != NULL);
    test_int(p->max, 20);
    test_flt(p->rate, 5);
    test_int(p->shape, EmitterBox);
    test_flt(p->radius, 2);
    test_flt(p->extents[2], 3);
    test_flt(p->direction[0], 1);
    test_flt(p->direction[1], 0);
    test_flt(p->spread, 30);
    test_flt(p->speed.max, 4);
    test_flt(p->lifetime.min, 3);
    test_flt(p->size.min, 0.5f);
    test_flt(p->rotation.max, 20);
    test_flt(p->spin.min, -90);
    test_flt(p->opacity.max, 0.75f);
    test_flt(p->acceleration[1], -9.8f);
    test_int(p->color.g, 20);
    test_uint(p->seed, 7);

    ecs_fini(world);
}

void Emitter_zero_is_literal(void) {
    // particle/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "zero",
        "using box.particle\n"
        "e { ParticleEmitter: {max: 0, direction: {0, 0, 0}, speed: {0, 0}, "
        "lifetime: {0, 0}, size: {0, 0}, opacity: {0, 0}, color: {0, 0, 0}} }\n",
        NULL) == 0);

    const ParticleEmitter *p = ecs_get(world, ecs_lookup(world, "e"), ParticleEmitter);
    test_assert(p != NULL);
    test_int(p->max, 0);
    test_flt(p->direction[1], 0);
    test_flt(p->speed.max, 0);
    test_flt(p->lifetime.max, 0);
    test_flt(p->size.max, 0);
    test_flt(p->opacity.max, 0);
    test_int(p->color.r, 0);

    ecs_fini(world);
}

void Emitter_c_literal_spawns_nothing(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {.rate = 600});
    frame(world);

    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_int(stats->emitters, 1);
    test_int(stats->spawned, 0);
    test_int(stats->particles, 0);
    test_int(stats->bytes, 0);
    test_assert(pool_of(null, e) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_extracted_block(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 120;
    p->shape = EmitterSphere;
    p->radius = 0.5f;
    p->extents[0] = 1;
    p->extents[1] = 2;
    p->extents[2] = 3;
    p->direction[0] = 0;
    p->direction[1] = 0;
    p->direction[2] = 2;
    p->spread = 60;
    p->speed = (ParticleRange){2, 3};
    p->lifetime = (ParticleRange){0.5f, 1.5f};
    p->size = (ParticleRange){0.2f, 0.4f};
    p->rotation = (ParticleRange){10, 20};
    p->spin = (ParticleRange){30, 40};
    p->opacity = (ParticleRange){0.5f, 1};
    p->acceleration[1] = -9.8f;
    p->color = (rgb){255, 0, 0};
    p->seed = 42;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    test_uint(pool->scene, 0);
    test_uint(pool->capacity, 1024);
    test_bool(pool->clear, true);
    test_uint(pool->table, 0);
    test_assert(pool->buffer >= BOX_RENDER_BUFFER_PARTICLE_FIRST);
    test_flt(b->model[0][0], 1);
    test_flt(b->model[3][0], 0);
    test_flt(b->direction[0], 0);
    test_flt(b->direction[2], 1);
    test_assert(fabsf(b->direction[3] - 0.5f) < 1e-5f);
    test_flt(b->extents[0], 1);
    test_flt(b->extents[2], 3);
    test_flt(b->extents[3], 0.5f);
    test_flt(b->acceleration[1], -9.8f);
    test_flt(b->acceleration[3], DELTA);
    test_flt(b->color[0], 1);
    test_flt(b->color[1], 0);
    test_flt(b->speed[0], 2);
    test_flt(b->speed[1], 3);
    test_flt(b->size[0], 0.2f);
    test_flt(b->size[1], 0.4f);
    test_flt(b->rotation[1], 20);
    test_flt(b->spin[0], 30);
    test_flt(b->opacity[0], 0.5f);
    test_uint(b->shape, EmitterSphere);
    test_uint(b->seed, 42);
    test_uint(b->count, 2);
    test_uint(b->first, 0);
    test_uint(b->used, 2);
    test_uint(b->points, 0);
    test_uint(b->delta_us, 16667);
    test_uint(b->life_min, 500000);
    test_uint(b->life_span, 1000000);
    test_int(sizeof(box_render_particle_block_t), 256);

    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_bool(pool->clear, false);
    test_uint(pool->block.count, 2);
    test_uint(pool->block.first, 2);
    test_uint(pool->block.used, 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_block_follows_transform(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_add(world, e, ParticleEmitter);
    ecs_get_mut(world, e, ParticleEmitter)->rate = 60;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 1);
    test_flt(pool->block.model[3][1], 2);
    test_flt(pool->block.model[3][2], 3);

    ecs_set(world, e, Position, {4, 5, 6});
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 4);
    test_flt(pool->block.model[3][1], 5);
    test_flt(pool->block.model[3][2], 6);
    test_uint(pool->block.first, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_spawn_slots_in_packet(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_get_mut(world, e, ParticleEmitter)->rate = 180;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.count, 3);
    const uint32_t *slots = box_render_packet_data(&null->last, pool->slots);
    test_uint(slots[0], 0);
    test_uint(slots[1], 1);
    test_uint(slots[2], 2);

    frame(world);
    pool = pool_of(null, e);
    slots = box_render_packet_data(&null->last, pool->slots);
    test_uint(slots[0], 3);
    test_uint(slots[1], 4);
    test_uint(slots[2], 5);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_mesh_table_sent(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Box, {2, 2, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 60;
    p->shape = EmitterMesh;
    p->source = source;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->table >= BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST);
    test_uint(pool->block.points, BOX_PARTICLE_MESH_POINTS);
    int32_t i, uploads = 0;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == pool->table) {
            uploads ++;
            test_uint(u->size, BOX_PARTICLE_MESH_POINTS * sizeof(box_particle_point_t));
        }
    }

    test_int(uploads, 1);
    uint32_t table = pool->table;

    frame(world);
    pool = pool_of(null, e);
    test_uint(pool->table, table);
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        test_assert(ecs_vec_get_t(&null->last.uploads, box_render_upload_t, i)->buffer != table);
    }

    ecs_get_mut(world, e, ParticleEmitter)->seed = 3;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    uploads = 0;
    pool = pool_of(null, e);
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        if (ecs_vec_get_t(&null->last.uploads, box_render_upload_t, i)->buffer == pool->table) {
            uploads ++;
        }
    }

    test_int(uploads, 1);

    ecs_fini(world);
    box_render_null_free(null);
}
