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
    ecs_set(world, e, Position, {0, 0, 0});
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
    ecs_set(world, e, Position, {0, 0, 0});
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
        "plain { Emitter: {} }\n"
        "stream { Emitter: {rate: 400} }\n"
        "custom { Emitter: {max: 20, rate: 5, shape: EmitterBox, radius: 2, "
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
        "e { Emitter: {max: 0, direction: {0, 0, 0}, speed: {0, 0}, "
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
    ecs_set(world, e, Position, {0, 0, 0});
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
    ecs_set(world, e, Position, {0, 0, 0});
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
    test_int(sizeof(box_render_particle_block_t), 624);

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
    ecs_set(world, e, Position, {0, 0, 0});
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
    ecs_set(world, source, Position, {0, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
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
        "using box.transform\n"
        "zone { Forces: {wind: {5, 0, 0}, drag: 1, turbulence: 2, "
        "turbulence_scale: 3} }\n"
        "plain { Forces: {} }\n"
        "e { Position: {0, 0, 0}\n"
        "    Emitter: {rate: 60, forces: zone, inherit_velocity: 0.5, "
        "color_variance: {10, 20, 30}}\n"
        "    Collision: {height: 1, die: true} }\n", NULL) == 0);

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

/** Scenes of the world scale tests: a plume under a Scale 40 hull and the same plume unscaled,
 * with ParticleForces whose gravity scales with them */
#define SCALED_PLUMES \
    "using box.particle\n" \
    "using box.transform\n" \
    "liner {\n" \
    "  Scale: {40, 40, 40}\n" \
    "  engine {\n" \
    "    Position: {0, 0, -4.2}\n" \
    "    Emitter: {rate: 60, speed: {2, 3}, radial: {0.5, 1}, size: {0.1, 0.2}, " \
    "acceleration: {0, 1, 0}, shape: EmitterSphere, radius: 0.5}\n" \
    "    Appearance: {softness: 0.25, stretch: 0.2}\n" \
    "    Forces: {gravity: {0, -2, 0}}\n" \
    "  }\n" \
    "}\n" \
    "boat {\n" \
    "  Scale: {1, 1, 1}\n" \
    "  engine {\n" \
    "    Position: {0, 0, -4.2}\n" \
    "    Emitter: {rate: 60, speed: {2, 3}, radial: {0.5, 1}, size: {0.1, 0.2}, " \
    "acceleration: {0, 1, 0}, shape: EmitterSphere, radius: 0.5}\n" \
    "    Appearance: {softness: 0.25, stretch: 0.2}\n" \
    "    Forces: {gravity: {0, -2, 0}}\n" \
    "  }\n" \
    "}\n"

void Emitter_world_scale_sizes_and_speeds(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_assert(ecs_script_run(world, "plumes", SCALED_PLUMES, NULL) == 0);
    frame(world);

    const box_render_particle_pool_t *scaled = pool_of(null, ecs_lookup(world, "liner.engine"));
    const box_render_particle_pool_t *plain = pool_of(null, ecs_lookup(world, "boat.engine"));
    test_assert(scaled != NULL);
    test_assert(plain != NULL);
    test_flt(plain->block.size[0], 0.1f);
    test_flt(plain->block.size[1], 0.2f);
    test_flt(plain->block.speed[0], 2);
    test_flt(plain->block.speed[1], 3);
    test_flt(scaled->block.size[0], plain->block.size[0] * 40);
    test_flt(scaled->block.size[1], plain->block.size[1] * 40);
    test_flt(scaled->block.speed[0], plain->block.speed[0] * 40);
    test_flt(scaled->block.speed[1], plain->block.speed[1] * 40);
    test_flt(scaled->block.radial[0], plain->block.radial[0] * 40);
    test_flt(scaled->block.radial[1], plain->block.radial[1] * 40);
    test_flt(scaled->block.softness, plain->block.softness * 40);

    /* the stretch per unit of speed shrinks, so the scaled plume stretches as much */
    test_flt(plain->block.stretch, 0.2f);
    test_flt(scaled->block.stretch, plain->block.stretch / 40);

    /* the acceleration of the emitter and the gravity of its forces both scale */
    test_flt(plain->block.acceleration[1], -1);
    test_flt(scaled->block.acceleration[1], (1 - 2) * 40);

    /* the spawn volume scales through the model matrix, not its radius */
    test_flt(scaled->block.extents[3], plain->block.extents[3]);
    test_flt(scaled->block.model[0][0], 40);
    test_flt(scaled->block.model[3][2], -4.2f * 40);
    test_flt(plain->block.model[3][2], -4.2f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_world_scale_one_unchanged(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Scale, {1, 1, 1});
    ecs_set(world, parent, Rotation, {0.3f, 1.1f, -0.7f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, ParticleEmitter, {
        .max = 100, .rate = 60, .speed = {2, 3}, .radial = {0.5f, 1}, .size = {0.1f, 0.2f},
        .lifetime = {1, 2}, .opacity = {1, 1}, .direction = {0, 1, 0}, .acceleration = {0, 1, 0}
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->block.size[0] == 0.1f);
    test_assert(pool->block.size[1] == 0.2f);
    test_assert(pool->block.speed[0] == 2.0f);
    test_assert(pool->block.speed[1] == 3.0f);
    test_assert(pool->block.radial[0] == 0.5f);
    test_assert(pool->block.radial[1] == 1.0f);
    test_assert(pool->block.acceleration[1] == 1.0f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_world_scale_non_uniform(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Scale, {2, 4, 8});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, ParticleEmitter, {
        .max = 100, .rate = 60, .speed = {1, 1}, .size = {0.5f, 0.5f},
        .lifetime = {1, 2}, .opacity = {1, 1}, .shape = EmitterBox, .extents = {1, 1, 1}
    });
    frame(world);

    /* sizes and speeds take the geometric mean of the axes, the volume each axis */
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.size[0], 0.5f * 4);
    test_flt(pool->block.speed[0], 4);
    test_flt(pool->block.extents[0], 1);
    test_flt(pool->block.model[0][0], 2);
    test_flt(pool->block.model[1][1], 4);
    test_flt(pool->block.model[2][2], 8);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create an emitter under a parent of a scale with ParticleForces of every kind, as a
 * turbulent plume.
 *
 * @param world The world
 * @param x Scale of the parent along x
 * @param y Along y
 * @param z Along z
 * @return The emitter
 */
static ecs_entity_t forces_plume_new(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Scale, {x, y, z});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, ParticleEmitter, {
        .max = 100, .rate = 60, .speed = {1, 1}, .size = {0.5f, 0.5f},
        .lifetime = {1, 2}, .opacity = {1, 1}, .direction = {0, 1, 0},
        .acceleration = {0, 1, 0}
    });
    ecs_set(world, e, ParticleForces, {
        .gravity = {0, -2, 0.5f}, .wind = {1, 0, -3}, .drag = 0.75f, .turbulence = 3,
        .turbulence_scale = 2, .drift = {0.5f, 0.25f, -1}
    });
    return e;
}

/** Check the forces of a block against those of forces_plume_new under a uniform scale.
 *
 * @param b The block
 * @param s The uniform scale
 */
static void check_forces_scaled(
    const box_render_particle_block_t *b,
    float s)
{
    check_vec3(b->acceleration, 0, (1 - 2) * s, 0.5f * s);
    check_vec3(b->wind, 1 * s, 0, -3 * s);
    test_flt(b->wind[3], 0.75f);
    test_flt(b->turbulence[0], 3 * s);
    test_flt(b->turbulence[1], 2 / s);
    test_flt(b->turbulence[2], s);
    check_vec3(b->drift, 0.5f * s, 0.25f * s, -1 * s);
}

void Emitter_world_scale_forces(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t plain = forces_plume_new(world, 1, 1, 1);
    ecs_entity_t scaled = forces_plume_new(world, 10, 10, 10);
    frame(world);
    frame(world);

    /* gravity, wind, turbulence and drift times the scale, the frequency over it, drag kept */
    const box_render_particle_pool_t *a = pool_of(null, plain);
    const box_render_particle_pool_t *b = pool_of(null, scaled);
    test_assert(a != NULL);
    test_assert(b != NULL);
    check_forces_scaled(&a->block, 1);
    check_forces_scaled(&b->block, 10);

    /* the frame of reference moves 10 times as far at a tenth of the frequency, so the wander
     * field of the scaled plume has the phase of the unscaled one */
    check_vec3(b->block.wander, a->block.wander[0], a->block.wander[1], a->block.wander[2]);

    /* the wander field at a scaled position is the field of the plain plume, scaled */
    float p[3] = {0.3f, -0.7f, 1.1f}, q[3] = {3, -7, 11}, wa[3], wb[3];
    box_particle_wander(p, a->block.wander, a->block.turbulence[1], a->block.turbulence[0], wa);
    box_particle_wander(q, b->block.wander, b->block.turbulence[1], b->block.turbulence[0], wb);
    check_vec3(wb, wa[0] * 10, wa[1] * 10, wa[2] * 10);

    /* the curves of ParticleMotion scale their velocity and force, not the speed and drag */
    box_particle_motion_t m = {
        .velocity = {1, 2, 3}, .speed = 0.5f, .force = {-1, 0, 4}, .drag = 2
    };
    box_particle_motion_scaled(&m, b->block.turbulence[2]);
    check_vec3(m.velocity, 10, 20, 30);
    check_vec3(m.force, -10, 0, 40);
    test_flt(m.speed, 0.5f);
    test_flt(m.drag, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_world_scale_forces_one_unchanged(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = forces_plume_new(world, 1, 1, 1);
    ecs_set(world, ecs_get_target(world, e, EcsChildOf, 0), Rotation, {0.3f, 1.1f, -0.7f});
    frame(world);

    /* a scale of 1 leaves every value exactly as authored */
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->block.acceleration[0] == 0.0f);
    test_assert(pool->block.acceleration[1] == -1.0f);
    test_assert(pool->block.acceleration[2] == 0.5f);
    test_assert(pool->block.wind[0] == 1.0f);
    test_assert(pool->block.wind[2] == -3.0f);
    test_assert(pool->block.wind[3] == 0.75f);
    test_assert(pool->block.turbulence[0] == 3.0f);
    test_assert(pool->block.turbulence[1] == 2.0f);
    test_assert(pool->block.turbulence[2] == 1.0f);
    test_assert(pool->block.drift[0] == 0.5f);
    test_assert(pool->block.drift[1] == 0.25f);
    test_assert(pool->block.drift[2] == -1.0f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_world_scale_forces_non_uniform(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = forces_plume_new(world, 2, 4, 8);
    frame(world);

    /* the forces take the geometric mean of the axes, as the sizes and speeds */
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.speed[0], 4);
    check_forces_scaled(&pool->block, 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_world_scale_forces_linger(void) {
    // particle/emitter
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = forces_plume_new(world, 10, 10, 10);
    ecs_get_mut(world, e, ParticleEmitter)->linger = true;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    frame(world);

    /* a deleted emitter runs out its particles under the forces of its last scale */
    ecs_delete(world, e);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    check_forces_scaled(&pool->block, 10);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_follow_offsets_model(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {10, 20, 30});
    ecs_set(world, target, Rotation, {0, 1, 0});
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_get_mut(world, e, ParticleEmitter)->follow = target;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.model[0][0], 1);
    test_flt(pool->block.model[3][0], 11);
    test_flt(pool->block.model[3][1], 22);
    test_flt(pool->block.model[3][2], 33);
    test_uint(pool->terrain, 0);
    test_uint(pool->block.sampled, 0);

    ecs_set(world, target, Position, {-5, 0, 5});
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], -4);
    test_flt(pool->block.model[3][1], 2);
    test_flt(pool->block.model[3][2], 8);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_follow_ground_keeps_height(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t terrain = ecs_new(world);
    ecs_add(world, terrain, TerrainSampled);
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {10, 20, 30});
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->follow = target;
    p->ground = terrain;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 11);
    test_flt(pool->block.model[3][1], 2);
    test_flt(pool->block.model[3][2], 33);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_ground_sampled_terrain(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t terrain = ecs_new(world);
    ecs_entity_t e = emitter_new(world);
    ecs_get_mut(world, e, ParticleEmitter)->ground = terrain;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->terrain, 0);
    test_uint(pool->block.sampled, 0);

    ecs_add(world, terrain, TerrainSampled);
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->terrain, terrain);
    test_uint(pool->block.sampled, BOX_RENDER_PARTICLE_SPAWN_GROUND);

    ecs_get_mut(world, e, ParticleEmitter)->local = true;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->terrain, 0);
    test_uint(pool->block.sampled, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_collision_sampled_terrain(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t terrain = ecs_new(world);
    ecs_add(world, terrain, TerrainSampled);
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleCollision, {.ground = true, .height = 4, .terrain = terrain});
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->terrain, terrain);
    test_uint(pool->block.sampled, BOX_RENDER_PARTICLE_COLLIDE_TERRAIN);
    test_flt(pool->block.ground[0], 4);

    ecs_get_mut(world, e, ParticleEmitter)->ground = terrain;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->terrain, terrain);
    test_uint(pool->block.sampled,
        BOX_RENDER_PARTICLE_SPAWN_GROUND | BOX_RENDER_PARTICLE_COLLIDE_TERRAIN);

    ecs_set(world, e, ParticleCollision, {.ground = false, .terrain = terrain});
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.sampled, BOX_RENDER_PARTICLE_SPAWN_GROUND);

    ecs_fini(world);
    box_render_null_free(null);
}

void Emitter_collision_other_terrain_than_ground(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t ground = ecs_new(world);
    ecs_add(world, ground, TerrainSampled);
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, TerrainSampled);
    ecs_entity_t e = emitter_new(world);
    ecs_get_mut(world, e, ParticleEmitter)->ground = ground;
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, ParticleCollision, {.ground = true, .terrain = other});
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->terrain, ground);
    test_uint(pool->block.sampled, BOX_RENDER_PARTICLE_SPAWN_GROUND);

    ecs_fini(world);
    box_render_null_free(null);
}
