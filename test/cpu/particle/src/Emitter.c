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

/** Create an emitter with the constructor values that spawns a particle every frame.
 *
 * @param world The world
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_get_mut(world, e, ParticleEmitter)->rate = 60;
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Check three floats of a block against values.
 *
 * @param v The floats
 * @param x The first value
 * @param y The second value
 * @param z The third value
 */
static void check_vec3(
    const float *v,
    float x,
    float y,
    float z)
{
    test_assert(fabsf(v[0] - x) < 1e-4f);
    test_assert(fabsf(v[1] - y) < 1e-4f);
    test_assert(fabsf(v[2] - z) < 1e-4f);
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
    test_flt(p->radial.min, 0);
    test_flt(p->radial.max, 0);
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
    test_flt(p->inherit_velocity, 0);
    test_int(p->color_variance.r, 0);
    test_int(p->color_variance.g, 0);
    test_int(p->color_variance.b, 0);
    test_uint(p->forces, 0);
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
        "extents: {1, 2, 3}, direction: {1, 0, 0}, spread: 30, speed: {2, 4}, radial: {1, 3}, "
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
    test_flt(p->radial.min, 1);
    test_flt(p->radial.max, 3);
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
    test_int(sizeof(box_render_particle_block_t), 512);

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

void Emitter_forces_defaults(void) {
    // particle/forces
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleForces);
    ecs_add(world, e, ParticleCollision);
    const ParticleForces *f = ecs_get(world, e, ParticleForces);
    test_assert(f != NULL);
    check_vec3(f->gravity, 0, -9.81f, 0);
    check_vec3(f->wind, 0, 0, 0);
    test_flt(f->drag, 0);
    test_flt(f->turbulence, 0);
    test_flt(f->turbulence_scale, 1);
    const ParticleCollision *c = ecs_get(world, e, ParticleCollision);
    test_assert(c != NULL);
    test_bool(c->ground, true);
    test_flt(c->height, 0);
    test_flt(c->bounce, 0.5f);
    test_flt(c->friction, 0.2f);
    test_bool(c->die, false);
    test_assert(ecs_singleton_get(world, ParticleForces) == NULL);

    ecs_fini(world);
}

void Emitter_forces_script(void) {
    // particle/forces
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    test_assert(ecs_script_run(world, "forces",
        "using box.particle\n"
        "zone { ParticleForces: {wind: {5, 0, 0}, drag: 1, turbulence: 2, "
        "turbulence_scale: 3} }\n"
        "plain { ParticleForces: {} }\n"
        "e { ParticleEmitter: {rate: 60, forces: zone, inherit_velocity: 0.5, "
        "color_variance: {10, 20, 30}}\n"
        "    ParticleCollision: {height: 1, die: true} }\n", NULL) == 0);

    ecs_entity_t zone = ecs_lookup(world, "zone"), e = ecs_lookup(world, "e");
    const ParticleForces *f = ecs_get(world, zone, ParticleForces);
    test_assert(f != NULL);
    check_vec3(f->gravity, 0, -9.81f, 0);
    check_vec3(f->wind, 5, 0, 0);
    test_flt(f->turbulence_scale, 3);
    f = ecs_get(world, ecs_lookup(world, "plain"), ParticleForces);
    test_assert(f != NULL);
    check_vec3(f->gravity, 0, -9.81f, 0);
    test_flt(f->turbulence_scale, 1);
    const ParticleEmitter *p = ecs_get(world, e, ParticleEmitter);
    test_uint(p->forces, zone);
    test_flt(p->inherit_velocity, 0.5f);
    test_int(p->color_variance.b, 30);
    const ParticleCollision *c = ecs_get(world, e, ParticleCollision);
    test_bool(c->ground, true);
    test_flt(c->height, 1);
    test_bool(c->die, true);

    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    check_vec3(pool->block.acceleration, 0, -9.81f, 0);
    check_vec3(pool->block.wind, 5, 0, 0);
    test_flt(pool->block.wind[3], 1);
    test_flt(pool->block.turbulence[0], 2);
    test_flt(pool->block.turbulence[1], 3);
    test_uint(pool->block.collide, BOX_PARTICLE_GROUND_DIE);
    test_flt(pool->block.ground[0], 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_forces_add_up(void) {
    // particle/forces
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_singleton_set(world, ParticleForces, {
        .gravity = {0, -10, 0}, .wind = {1, 0, 0}, .drag = 0.5f, .turbulence = 1,
        .turbulence_scale = 2
    });
    ecs_entity_t zone = ecs_new(world);
    ecs_set(world, zone, ParticleForces, {
        .gravity = {0, 0, 1}, .wind = {0, 2, 0}, .drag = 0.25f, .turbulence = 2,
        .turbulence_scale = 3
    });
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleForces, {
        .gravity = {1, 0, 0}, .wind = {0, 0, 4}, .drag = 1, .turbulence = 0.5f,
        .turbulence_scale = 0.5f
    });
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->forces = zone;
    p->acceleration[1] = 1;
    p->seed = 5;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    check_vec3(b->acceleration, 1, -9, 1);
    test_flt(b->acceleration[3], DELTA);
    check_vec3(b->wind, 1, 2, 4);
    test_flt(b->wind[3], 1.75f);
    test_flt(b->turbulence[0], 3.5f);
    test_flt(b->turbulence[1], 3);
    float phase[3];
    box_particle_phase(5, phase);
    check_vec3(b->wander, phase[0], phase[1], phase[2]);

    ecs_remove(world, e, ParticleForces);
    frame(world);
    b = &pool_of(null, e)->block;
    check_vec3(b->acceleration, 0, -9, 1);
    check_vec3(b->wind, 1, 2, 0);
    test_flt(b->wind[3], 0.75f);
    test_flt(b->turbulence[0], 3);
    test_flt(b->turbulence[1], 6);

    ecs_singleton_remove(world, ParticleForces);
    frame(world);
    b = &pool_of(null, e)->block;
    check_vec3(b->acceleration, 0, 1, 1);
    test_flt(b->turbulence[1], 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_forces_counted_once(void) {
    // particle/forces
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_singleton_set(world, ParticleForces, { .gravity = {0, -1, 0}, .turbulence_scale = 2 });
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleForces, { .gravity = {0, -2, 0}, .turbulence_scale = 3 });
    ecs_get_mut(world, e, ParticleEmitter)->forces = e;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    const box_render_particle_block_t *b = &pool_of(null, e)->block;
    check_vec3(b->acceleration, 0, -3, 0);
    test_flt(b->turbulence[1], 6);

    ecs_get_mut(world, e, ParticleEmitter)->forces = ecs_id(ParticleForces);
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    b = &pool_of(null, e)->block;
    check_vec3(b->acceleration, 0, -3, 0);
    test_flt(b->turbulence[1], 6);

    ecs_entity_t bare = ecs_new(world);
    ecs_get_mut(world, e, ParticleEmitter)->forces = bare;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    b = &pool_of(null, e)->block;
    check_vec3(b->acceleration, 0, -3, 0);

    ecs_entity_t gone = ecs_new(world);
    ecs_set(world, gone, ParticleForces, { .gravity = {0, -4, 0}, .turbulence_scale = 1 });
    ecs_get_mut(world, e, ParticleEmitter)->forces = gone;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    check_vec3(pool_of(null, e)->block.acceleration, 0, -7, 0);
    ecs_delete(world, gone);
    frame(world);
    check_vec3(pool_of(null, e)->block.acceleration, 0, -3, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_no_forces_no_gravity(void) {
    // particle/forces
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world);
    ecs_get_mut(world, e, ParticleEmitter)->acceleration[2] = 3;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_block_t *b = &pool_of(null, e)->block;
    check_vec3(b->acceleration, 0, 0, 3);
    check_vec3(b->wind, 0, 0, 0);
    test_flt(b->wind[3], 0);
    test_flt(b->turbulence[0], 0);
    test_flt(b->turbulence[1], 1);
    check_vec3(b->variance, 0, 0, 0);
    check_vec3(b->inherit, 0, 0, 0);
    test_uint(b->collide, BOX_PARTICLE_GROUND_NONE);

    ecs_set(world, e, ParticleForces, { .drag = -2, .turbulence_scale = 1 });
    frame(world);
    test_flt(pool_of(null, e)->block.wind[3], 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_inherit_velocity(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_get_mut(world, e, ParticleEmitter)->inherit_velocity = 0.5f;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    check_vec3(pool_of(null, e)->block.inherit, 0, 0, 0);

    ecs_set(world, e, Position, {1, 0, 0});
    frame(world);
    const box_render_particle_block_t *b = &pool_of(null, e)->block;
    test_assert(fabsf(b->inherit[0] - 0.5f / DELTA) < 1e-2f);
    test_assert(fabsf(b->inherit[1]) < 1e-4f);
    test_assert(fabsf(b->inherit[2]) < 1e-4f);

    ecs_set(world, e, Position, {1, 2, 0});
    frame(world);
    b = &pool_of(null, e)->block;
    test_assert(fabsf(b->inherit[0]) < 1e-4f);
    test_assert(fabsf(b->inherit[1] - 1.0f / DELTA) < 1e-2f);

    frame(world);
    check_vec3(pool_of(null, e)->block.inherit, 0, 0, 0);

    ecs_get_mut(world, e, ParticleEmitter)->inherit_velocity = 0;
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {5, 5, 5});
    frame(world);
    check_vec3(pool_of(null, e)->block.inherit, 0, 0, 0);

    ecs_get_mut(world, e, ParticleEmitter)->inherit_velocity = 2;
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {5, 5, 4});
    frame(world);
    b = &pool_of(null, e)->block;
    test_assert(fabsf(b->inherit[2] + 2.0f / DELTA) < 1e-2f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_color_variance_in_block(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->color = (rgb){255, 128, 0};
    p->color_variance = (rgb){10, 20, 255};
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_block_t *b = &pool_of(null, e)->block;
    check_vec3(b->color, 1, 128.0f / 255.0f, 0);
    check_vec3(b->variance, 10.0f / 255.0f, 20.0f / 255.0f, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_collision_in_block(void) {
    // particle/collision
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world);
    ecs_add(world, e, ParticleCollision);
    frame(world);
    const box_render_particle_block_t *b = &pool_of(null, e)->block;
    test_uint(b->collide, BOX_PARTICLE_GROUND_BOUNCE);
    check_vec3(b->ground, 0, 0.5f, 0.2f);

    ecs_set(world, e, ParticleCollision, {
        .ground = true, .height = 2, .bounce = 1.5f, .friction = -1
    });
    frame(world);
    b = &pool_of(null, e)->block;
    test_uint(b->collide, BOX_PARTICLE_GROUND_BOUNCE);
    check_vec3(b->ground, 2, 1, 0);

    ecs_set(world, e, ParticleCollision, { .ground = true, .height = -1, .die = true });
    frame(world);
    b = &pool_of(null, e)->block;
    test_uint(b->collide, BOX_PARTICLE_GROUND_DIE);
    test_flt(b->ground[0], -1);

    ecs_set(world, e, ParticleCollision, { .ground = false, .height = 3, .die = true });
    frame(world);
    test_uint(pool_of(null, e)->block.collide, BOX_PARTICLE_GROUND_NONE);

    ecs_remove(world, e, ParticleCollision);
    frame(world);
    test_uint(pool_of(null, e)->block.collide, BOX_PARTICLE_GROUND_NONE);

    ecs_fini(world);
    box_render_null_free(null);
}
