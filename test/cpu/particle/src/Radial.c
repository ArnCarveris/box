/**
 * @file Radial.c
 * @brief Tests for the radial speed of ParticleEmitter: particles spawn with a speed along the
 * offset of their spawn point from the origin of the emitter, for every shape, added to the
 * velocity of direction and speed, in the rotated and scaled frame of an effect instance, and
 * a radial speed of 0 changes nothing.
 *
 * The particles of a pool are replayed on the CPU from the blocks of the packets with the C twin
 * of the integrate and spawn passes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "simulate.h"

/** Frame delta of the tests, a fixed clock */
#define DELTA (1.0f / 60.0f)

/** Slots of a replayed pool, the smallest pool */
#define SLOTS (1024)

/** Template of the effect test: a layer turned a quarter turn about X, so its disc lies in the
 * XY plane, that bursts a ring */
#define TEMPLATES \
    "using box.particle\n" \
    "using box.transform\n" \
    "@tree ChildOf\n" \
    "prefab Ring {\n" \
    "  ring {\n" \
    "    Rotation: {1.5707964, 0, 0}\n" \
    "    ParticleEmitter: {max: 64, shape: EmitterDisc, radius: 0.5, speed: {0, 0}, " \
    "radial: {1, 1}, lifetime: {5, 5}, seed: 4}\n" \
    "    ParticleBurst: {count: 64}\n" \
    "  }\n" \
    "}\n"

/** The particles of a pool as the integrate and spawn passes leave them */
typedef struct replay_t {
    box_particle_t slots[SLOTS]; /**< The slots of the pool */
    float spawned[SLOTS][3]; /**< Spawn position of each particle */
    box_particle_point_t table[BOX_PARTICLE_MESH_POINTS]; /**< Mesh table of the pool */
    bool has_table;         /**< Whether the table was uploaded */
} replay_t;

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

/** Create an emitter that bursts particles once and keeps them alive, without speed.
 *
 * @param world The world
 * @param shape Its spawn shape
 * @param radial Its radial speed
 * @param count Particles of the burst
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    EmitterShape shape,
    ParticleRange radial,
    int32_t count)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->max = count;
    p->shape = shape;
    p->radius = 1;
    p->extents[0] = 1;
    p->extents[1] = 0.5f;
    p->extents[2] = 2;
    p->speed = (ParticleRange){0, 0};
    p->radial = radial;
    p->lifetime = (ParticleRange){5, 5};
    p->seed = 9;
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, ParticleBurst, {.count = count});

    return e;
}

/** Keep the mesh table a pool uploads in the last packet.
 *
 * @param r The replay
 * @param null The backend
 * @param pool The pool
 */
static void replay_table(
    replay_t *r,
    box_render_null_t *null,
    const box_render_particle_pool_t *pool)
{
    int32_t i;
    for (i = 0; pool->table && i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == pool->table && u->size <= sizeof(r->table)) {
            memcpy(r->table, box_render_packet_data(&null->last, u->data), u->size);
            r->has_table = true;
        }
    }
}

/** Spawn a particle into a slot as the spawn pass does.
 *
 * @param r The replay, with its mesh table
 * @param b The block
 * @param index The spawn index of the particle
 * @param out Receives the particle
 */
static void replay_spawn(
    const replay_t *r,
    const box_render_particle_block_t *b,
    uint32_t index,
    box_particle_t *out)
{
    float u[3], local[3], c[2], cone[3], world[3], offset[3], radial[3], len2, speed;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        u[i] = box_particle_unit(
            box_particle_hash(b->seed, index, BOX_PARTICLE_STREAM_SHAPE + (uint32_t)i));
    }

    box_particle_shape(b->shape, b->extents[3], b->extents, u, local);
    if (b->shape == EmitterMesh && b->points) {
        test_assert(r->has_table);
        const box_particle_point_t *point = &r->table[
            box_particle_hash(b->seed, index, BOX_PARTICLE_STREAM_POINT) % b->points];
        memcpy(local, point->position, sizeof(local));
    }

    c[0] = box_particle_unit(box_particle_hash(b->seed, index, BOX_PARTICLE_STREAM_DIRECTION));
    c[1] = box_particle_unit(
        box_particle_hash(b->seed, index, BOX_PARTICLE_STREAM_DIRECTION + 1u));
    box_particle_cone(b->direction, b->direction[3], c, cone);
    for (i = 0; i < 3; i ++) {
        world[i] = b->model[0][i] * cone[0] + b->model[1][i] * cone[1] +
            b->model[2][i] * cone[2];
        offset[i] = b->model[0][i] * local[0] + b->model[1][i] * local[1] +
            b->model[2][i] * local[2];
    }

    len2 = world[0] * world[0] + world[1] * world[1] + world[2] * world[2];
    speed = box_particle_draw(b->seed, index, BOX_PARTICLE_STREAM_SPEED, b->speed[0],
        b->speed[1]);
    box_particle_radial(offset, box_particle_draw(b->seed, index, BOX_PARTICLE_STREAM_RADIAL,
        b->radial[0], b->radial[1]), radial);
    for (i = 0; i < 3; i ++) {
        float dir = len2 > 0 ? world[i] / sqrtf(len2) : 0.0f;
        out->position[i] = offset[i] + b->model[3][i];
        out->velocity[i] = dir * speed + radial[i] + b->inherit[i];
    }

    out->remaining = box_particle_life(b->seed, index, b->life_min, b->life_span);
    out->index = index;
}

/** Run the integrate and spawn passes of a pool of the last packet on the replay.
 *
 * @param r The replay
 * @param null The backend
 * @param pool The pool
 */
static void replay_step(
    replay_t *r,
    box_render_null_t *null,
    const box_render_particle_pool_t *pool)
{
    const box_render_particle_block_t *b = &pool->block;
    box_particle_forces_t f = {
        .drag = b->wind[3], .turbulence = b->turbulence[0], .scale = b->turbulence[1]
    };
    float delta = b->acceleration[3];
    uint32_t i;
    test_assert(pool->capacity <= SLOTS);
    if (pool->clear) {
        memset(r->slots, 0, sizeof(r->slots));
    }

    replay_table(r, null, pool);
    for (i = 0; i < 3; i ++) {
        f.acceleration[i] = b->acceleration[i];
        f.wind[i] = b->wind[i];
        f.phase[i] = b->wander[i];
        f.drift[i] = b->drift[i];
    }

    for (i = 0; i < b->used; i ++) {
        box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        p->remaining = box_particle_age(p->remaining, b->delta_us);
        if (p->remaining) {
            box_particle_integrate(p->position, p->velocity, &f, NULL, delta);
        }
    }

    if (b->count) {
        const uint32_t *slots = box_render_packet_data(&null->last, pool->slots);
        for (i = 0; i < b->count; i ++) {
            if (slots[i] != BOX_RENDER_PARTICLE_NO_SLOT) {
                replay_spawn(r, b, b->first + i, &r->slots[slots[i]]);
                memcpy(r->spawned[slots[i]], r->slots[slots[i]].position,
                    sizeof(r->spawned[0]));
            }
        }
    }
}

/** Run frames and the passes of the pool of an emitter on its replay.
 *
 * @param world The world
 * @param null The backend
 * @param e The emitter
 * @param r Its replay
 * @param count Frames to run
 * @return The pool in the last packet
 */
static const box_render_particle_pool_t* frames_replay(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t e,
    replay_t *r,
    int32_t count)
{
    const box_render_particle_pool_t *result = NULL;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, DELTA);
        box_render_sync(world);
        result = pool_of(null, e);
        test_assert(result != NULL);
        replay_step(r, null, result);
    }

    return result;
}

/** Get the length of three floats.
 *
 * @param v The floats
 * @return The length
 */
static float length3(
    const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/** Get the dot product of three floats with three others.
 *
 * @param a The first floats
 * @param b The second floats
 * @return The dot product
 */
static float dot3(
    const float *a,
    const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/** Check that a velocity is a speed along an offset.
 *
 * @param v The velocity
 * @param offset The offset, not zero
 * @param speed The speed
 * @param tolerance Largest difference allowed per component
 */
static void check_along(
    const float *v,
    const float *offset,
    float speed,
    float tolerance)
{
    float len = length3(offset);
    int32_t c;
    test_assert(len > 0);
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(v[c] - offset[c] / len * speed) < tolerance);
    }
}

void Radial_c_default(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    const ParticleEmitter *p = ecs_get(world, e, ParticleEmitter);
    test_flt(p->radial.min, 0);
    test_flt(p->radial.max, 0);

    test_assert(ecs_script_run(world, "radial",
        "using box.particle\n"
        "ring { ParticleEmitter: {radial: {2, 3}} }\n", NULL) == 0);
    p = ecs_get(world, ecs_lookup(world, "ring"), ParticleEmitter);
    test_flt(p->radial.min, 2);
    test_flt(p->radial.max, 3);
    test_flt(p->speed.min, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_in_block(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world, EmitterDisc, (ParticleRange){1.5f, 2.5f}, 8);
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(pool->block.radial[0], 1.5f);
    test_flt(pool->block.radial[1], 2.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_disc_ring_expands(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world, EmitterDisc, (ParticleRange){2, 2}, 200);
    ecs_set(world, e, Position, {1, 2, 3});
    replay_t *r = ecs_os_calloc_t(replay_t);
    frames_replay(world, null, e, r, 1);
    float before[SLOTS];
    int32_t i, live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        before[i] = 0;
        if (!p->remaining) {
            continue;
        }

        float h[3] = { p->position[0] - 1, 0, p->position[2] - 3 };
        before[i] = length3(h);
        test_assert(before[i] <= 1 + 1e-5f);
        test_flt(p->position[1], 2);
        test_flt(p->velocity[1], 0);
        check_along(p->velocity, h, 2, 1e-4f);
        live ++;
    }

    test_int(live, 200);
    frames_replay(world, null, e, r, 12);
    live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        float h[3] = { p->position[0] - 1, 0, p->position[2] - 3 };
        test_assert(length3(h) > before[i] + 2 * 12 * DELTA - 1e-3f);
        test_assert(fabsf(p->position[1] - 2) < 1e-5f);
        test_flt(p->velocity[1], 0);
        live ++;
    }

    test_int(live, 200);
    test_int(ecs_singleton_get(world, ParticleStats)->particles, 200);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_disc_ring_band(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world, EmitterDisc, (ParticleRange){3, 3}, 100);
    replay_t *r = ecs_os_calloc_t(replay_t);
    frames_replay(world, null, e, r, 31);
    int32_t i, live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        float h[3] = { p->position[0], 0, p->position[2] };
        float d = length3(h), d0 = length3(r->spawned[i]);
        test_assert(fabsf(d - (d0 + 3 * 30 * DELTA)) < 1e-3f);
        test_assert(d >= 1.5f - 1e-3f && d <= 2.5f + 1e-3f);
        test_flt(p->position[1], 0);
        live ++;
    }

    test_int(live, 100);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_sphere_points_outward(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world, EmitterSphere, (ParticleRange){1, 3}, 300);
    ecs_set(world, e, Position, {2, 1, -3});
    replay_t *r = ecs_os_calloc_t(replay_t);
    frames_replay(world, null, e, r, 1);
    float lo = 10, hi = 0;
    int32_t i, live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        float offset[3] = { p->position[0] - 2, p->position[1] - 1, p->position[2] + 3 };
        float speed = length3(p->velocity);
        test_assert(length3(offset) <= 1 + 1e-5f);
        test_assert(speed >= 1 - 1e-5f && speed < 3 + 1e-5f);
        test_assert(dot3(p->velocity, offset) > 0.9999f * speed * length3(offset));
        lo = speed < lo ? speed : lo;
        hi = speed > hi ? speed : hi;
        live ++;
    }

    test_int(live, 300);
    test_assert(lo < 1.2f);
    test_assert(hi > 2.8f);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_box_points_outward(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = emitter_new(world, EmitterBox, (ParticleRange){2, 2}, 100);
    replay_t *r = ecs_os_calloc_t(replay_t);
    frames_replay(world, null, e, r, 1);
    int32_t i, live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        test_assert(fabsf(p->position[0]) <= 1 && fabsf(p->position[1]) <= 0.5f &&
            fabsf(p->position[2]) <= 2);
        check_along(p->velocity, p->position, 2, 1e-4f);
        live ++;
    }

    test_int(live, 100);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_torus_mesh_ring(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Torus, {.segments = 32, .rings = 8, .radius = 1, .tube = 0.1f});
    ecs_entity_t e = emitter_new(world, EmitterMesh, (ParticleRange){3, 3}, 200);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->source = source;
    ecs_modified(world, e, ParticleEmitter);
    replay_t *r = ecs_os_calloc_t(replay_t);
    const box_render_particle_pool_t *pool = frames_replay(world, null, e, r, 1);
    test_uint(pool->block.points, BOX_PARTICLE_MESH_POINTS);
    test_bool(r->has_table, true);
    float before[SLOTS];
    int32_t i, live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *q = &r->slots[i];
        before[i] = 0;
        if (!q->remaining) {
            continue;
        }

        float h[3] = { q->position[0], 0, q->position[2] }, vh[3] = { q->velocity[0], 0,
            q->velocity[2] };
        before[i] = length3(h);
        test_assert(before[i] > 0.85f && before[i] < 1.15f);
        test_assert(fabsf(q->position[1]) < 0.11f);
        test_assert(fabsf(q->velocity[1]) < 0.35f);
        test_assert(length3(vh) > 2.9f);
        test_assert(dot3(vh, h) > 0.9999f * length3(vh) * before[i]);
        check_along(q->velocity, q->position, 3, 1e-4f);
        live ++;
    }

    test_int(live, 200);
    frames_replay(world, null, e, r, 10);
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *q = &r->slots[i];
        if (!q->remaining) {
            continue;
        }

        float h[3] = { q->position[0], 0, q->position[2] };
        test_assert(length3(h) > before[i] + 0.4f);
        test_assert(fabsf(q->position[1]) < 0.11f + 0.35f * 10 * DELTA);
    }

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_adds_to_direction_speed(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e[2] = {
        emitter_new(world, EmitterDisc, (ParticleRange){2, 2}, 100),
        emitter_new(world, EmitterDisc, (ParticleRange){0, 0}, 100)
    };
    int32_t i, k, live = 0;
    for (k = 0; k < 2; k ++) {
        ParticleEmitter *p = ecs_get_mut(world, e[k], ParticleEmitter);
        p->speed = (ParticleRange){3, 3};
        ecs_modified(world, e[k], ParticleEmitter);
    }

    replay_t *r[2] = { ecs_os_calloc_t(replay_t), ecs_os_calloc_t(replay_t) };
    ecs_progress(world, DELTA);
    box_render_sync(world);
    for (k = 0; k < 2; k ++) {
        const box_render_particle_pool_t *pool = pool_of(null, e[k]);
        test_assert(pool != NULL);
        replay_step(r[k], null, pool);
    }

    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *a = &r[0]->slots[i], *b = &r[1]->slots[i];
        test_uint(a->remaining, b->remaining);
        if (!a->remaining) {
            continue;
        }

        float h[3] = { a->position[0], 0, a->position[2] }, d[3];
        test_flt(a->position[0], b->position[0]);
        test_flt(a->position[2], b->position[2]);
        test_flt(b->velocity[0], 0);
        test_flt(b->velocity[1], 3);
        test_flt(b->velocity[2], 0);
        test_flt(a->velocity[1], 3);
        for (k = 0; k < 3; k ++) {
            d[k] = a->velocity[k] - b->velocity[k];
        }

        check_along(d, h, 2, 1e-4f);
        live ++;
    }

    test_int(live, 100);

    ecs_os_free(r[0]);
    ecs_os_free(r[1]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_rotated_scaled_effect(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_assert(ecs_script_run(world, "templates", TEMPLATES, NULL) == 0);

    vec3 at = {5, 0, 0};
    ecs_entity_t root = box_particle_burst_at(world, ecs_lookup(world, "Ring"), at, 2);
    test_assert(root != 0);
    ecs_entity_t layer = ecs_lookup_child(world, root, "ring");
    test_assert(layer != 0);
    replay_t *r = ecs_os_calloc_t(replay_t);
    const box_render_particle_pool_t *pool = frames_replay(world, null, layer, r, 1);
    const box_render_particle_block_t *b = &pool->block;
    test_flt(b->radial[0], 2);
    test_flt(b->radial[1], 2);
    test_assert(fabsf(b->model[3][0] - 5) < 1e-5f);

    float n[3] = { b->model[1][0], b->model[1][1], b->model[1][2] };
    test_assert(fabsf(length3(n) - 2) < 1e-4f);
    test_assert(fabsf(fabsf(n[2]) - 2) < 1e-4f);
    float before[SLOTS];
    int32_t i, live = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        before[i] = 0;
        if (!p->remaining) {
            continue;
        }

        float offset[3] = { p->position[0] - 5, p->position[1], p->position[2] };
        before[i] = length3(offset);
        test_assert(before[i] <= 1 + 1e-4f);
        test_assert(fabsf(offset[2]) < 1e-5f);
        test_assert(fabsf(p->velocity[2]) < 1e-5f);
        check_along(p->velocity, offset, 2, 1e-4f);
        live ++;
    }

    test_int(live, 64);
    frames_replay(world, null, layer, r, 15);
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        float offset[3] = { p->position[0] - 5, p->position[1], p->position[2] };
        test_assert(fabsf(length3(offset) - (before[i] + 2 * 15 * DELTA)) < 1e-3f);
        test_assert(fabsf(offset[2]) < 1e-4f);
    }

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Radial_zero_keeps_results(void) {
    // particle/radial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e[2];
    int32_t i, k, live = 0;
    for (k = 0; k < 2; k ++) {
        e[k] = ecs_new(world);
        ecs_set(world, e[k], Position, {1, 0, -2});
        ecs_add(world, e[k], ParticleEmitter);
        ParticleEmitter *p = ecs_get_mut(world, e[k], ParticleEmitter);
        p->max = 100;
        p->shape = EmitterSphere;
        p->radius = 1.5f;
        p->spread = 60;
        p->speed = (ParticleRange){1, 4};
        p->lifetime = (ParticleRange){5, 5};
        p->seed = 21;
        if (k == 1) {
            p->radial = (ParticleRange){0, 0};
        }

        ecs_modified(world, e[k], ParticleEmitter);
        ecs_set(world, e[k], ParticleBurst, {.count = 100});
    }

    replay_t *r = ecs_os_calloc_t(replay_t);
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const box_render_particle_pool_t *a = pool_of(null, e[0]), *b = pool_of(null, e[1]);
    test_assert(a != NULL);
    test_assert(b != NULL);
    box_render_particle_block_t ba = a->block, bb = b->block;
    ba.slot_first = bb.slot_first = 0;
    ba.args = bb.args = 0;
    ba.sort_first = bb.sort_first = 0;
    test_assert(memcmp(&ba, &bb, sizeof(ba)) == 0);
    test_flt(b->block.radial[0], 0);
    test_flt(b->block.radial[1], 0);
    replay_step(r, null, b);

    const ParticleEmitter *emitter = ecs_get(world, e[1], ParticleEmitter);
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (!p->remaining) {
            continue;
        }

        box_particle_sample_t s;
        box_particle_sample(emitter, p->index, &s);
        test_flt(s.radial, 0);
        for (k = 0; k < 3; k ++) {
            test_assert(fabsf(p->velocity[k] - s.direction[k] * s.speed) < 1e-4f);
        }

        live ++;
    }

    test_int(live, 100);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}
