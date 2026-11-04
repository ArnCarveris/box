/**
 * @file Drift.c
 * @brief Tests for the drift of ParticleForces: the frame of reference that moves the live
 * particles without changing their velocities, its sources, its changes, effect layers,
 * lingering pools and the ground plane, and box_particle_burst_at from a system.
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

/** Template of the system tests: a root that bursts, a layer without a Position that emits at a
 * rate and a layer offset from the root that bursts */
#define TEMPLATES \
    "using box.particle\n" \
    "using box.transform\n" \
    "@tree ChildOf\n" \
    "prefab Puff {\n" \
    "  ParticleEmitter: {lifetime: {1, 1}}\n" \
    "  ParticleBurst: {count: 3}\n" \
    "  ring {\n" \
    "    ParticleEmitter: {rate: 60, lifetime: {0.5, 0.5}}\n" \
    "  }\n" \
    "  spark {\n" \
    "    Position: {1, 0, 0}\n" \
    "    ParticleEmitter: {lifetime: {0.5, 0.5}}\n" \
    "    ParticleBurst: {count: 2}\n" \
    "  }\n" \
    "}\n"

/** The particles of a pool as the integrate and spawn passes leave them */
typedef struct replay_t {
    box_particle_t slots[SLOTS]; /**< The slots of the pool */
    float moved[SLOTS];     /**< Seconds each particle moved */
    box_particle_t before[SLOTS]; /**< The slots before the last step */
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

/** Create an emitter at the origin that spawns a particle every frame upward in a cone.
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
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 60;
    p->spread = 40;
    p->speed = (ParticleRange){1, 3};
    p->lifetime = (ParticleRange){3, 3};
    p->seed = 7;
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Check three floats against values.
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

/** Spawn a particle into a slot as the spawn pass does, for the shapes without a table.
 *
 * @param b The block
 * @param index The spawn index of the particle
 * @param out Receives the particle
 */
static void replay_spawn(
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
    box_particle_ground_t g = {
        .mode = b->collide, .height = b->ground[0], .bounce = b->ground[1],
        .friction = b->ground[2]
    };
    float delta = b->acceleration[3];
    uint32_t i;
    test_assert(pool->capacity <= SLOTS);
    if (pool->clear) {
        memset(r->slots, 0, sizeof(r->slots));
        memset(r->moved, 0, sizeof(r->moved));
    }

    memcpy(r->before, r->slots, sizeof(r->slots));
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
            r->moved[i] += delta;
            if (!box_particle_collide(p->position, p->velocity, &g)) {
                p->remaining = 0;
            }
        }
    }

    if (b->count) {
        const uint32_t *slots = box_render_packet_data(&null->last, pool->slots);
        for (i = 0; i < b->count; i ++) {
            if (slots[i] != BOX_RENDER_PARTICLE_NO_SLOT) {
                replay_spawn(b, b->first + i, &r->slots[slots[i]]);
                r->moved[slots[i]] = 0;
            }
        }
    }
}

/** Run a frame and the passes of the pools of emitters on their replays.
 *
 * @param world The world
 * @param null The backend
 * @param e The emitters
 * @param r Their replays
 * @param count Number of emitters
 */
static void frame_replay(
    ecs_world_t *world,
    box_render_null_t *null,
    const ecs_entity_t *e,
    replay_t **r,
    int32_t count)
{
    int32_t i;
    frame(world);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = pool_of(null, e[i]);
        test_assert(pool != NULL);
        replay_step(r[i], null, pool);
    }
}

/** Count the live particles of a replay.
 *
 * @param r The replay
 * @return The particles
 */
static int32_t replay_live(
    const replay_t *r)
{
    int32_t i, result = 0;
    for (i = 0; i < SLOTS; i ++) {
        result += r->slots[i].remaining != 0;
    }

    return result;
}

/** Check that every particle that lived before the last step moved by its velocity plus the
 * drift times the delta.
 *
 * @param r The replay
 * @param drift The drift of the step
 * @return Particles checked
 */
static int32_t check_step(
    const replay_t *r,
    const float *drift)
{
    int32_t i, c, result = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i], *q = &r->before[i];
        if (!p->remaining || !q->remaining || p->index != q->index) {
            continue;
        }

        for (c = 0; c < 3; c ++) {
            float expect = q->position[c] + (p->velocity[c] + drift[c]) * DELTA;
            test_assert(fabsf(p->position[c] - expect) < 1e-5f);
        }

        result ++;
    }

    return result;
}

/** Check that the particles of a pool with drift are those of the same pool without, moved by
 * the drift times the seconds they moved, with the same velocities.
 *
 * @param a The replay with drift
 * @param b The replay without
 * @param drift The drift
 * @param tolerance Largest difference allowed
 * @return Particles checked
 */
static int32_t check_shifted(
    const replay_t *a,
    const replay_t *b,
    const float *drift,
    float tolerance)
{
    int32_t i, c, result = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &a->slots[i], *q = &b->slots[i];
        test_uint(p->remaining, q->remaining);
        if (!p->remaining) {
            continue;
        }

        test_uint(p->index, q->index);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(p->velocity[c] - q->velocity[c]) < tolerance);
            test_assert(fabsf(p->position[c] -
                (q->position[c] + drift[c] * a->moved[i])) < tolerance);
        }

        result ++;
    }

    return result;
}

void Drift_c_default(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleForces);
    const ParticleForces *f = ecs_get(world, e, ParticleForces);
    check_vec3(f->drift, 0, 0, 0);

    test_assert(ecs_script_run(world, "forces",
        "using box.particle\n"
        "zone { ParticleForces: {drift: {1, 2, 3}} }\n", NULL) == 0);
    f = ecs_get(world, ecs_lookup(world, "zone"), ParticleForces);
    check_vec3(f->drift, 1, 2, 3);
    check_vec3(f->gravity, 0, -9.81f, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_moves_without_changing_velocity(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    float drift[3] = {2, 0, -1};
    ecs_entity_t e[2] = { emitter_new(world), emitter_new(world) };
    ecs_set(world, e[0], ParticleForces, {.drift = {2, 0, -1}});
    ecs_set(world, e[1], ParticleForces, {.drift = {0, 0, 0}});
    replay_t *r[2] = { ecs_os_calloc_t(replay_t), ecs_os_calloc_t(replay_t) };
    int32_t i, checked = 0;
    for (i = 0; i < 40; i ++) {
        frame_replay(world, null, e, r, 2);
        checked += check_step(r[0], drift);
    }

    const box_render_particle_block_t *b = &pool_of(null, e[0])->block;
    check_vec3(b->drift, 2, 0, -1);
    check_vec3(b->acceleration, 0, 0, 0);
    check_vec3(pool_of(null, e[1])->block.drift, 0, 0, 0);
    test_assert(checked > 100);
    test_assert(replay_live(r[0]) >= 38);
    test_int(replay_live(r[0]) + replay_live(r[1]),
        ecs_singleton_get(world, ParticleStats)->particles);
    test_int(check_shifted(r[0], r[1], drift, 1e-4f), replay_live(r[0]));
    for (i = 0; i < SLOTS; i ++) {
        test_flt(r[0]->slots[i].velocity[0], r[1]->slots[i].velocity[0]);
        test_flt(r[0]->slots[i].velocity[1], r[1]->slots[i].velocity[1]);
        test_flt(r[0]->slots[i].velocity[2], r[1]->slots[i].velocity[2]);
    }

    ecs_os_free(r[0]);
    ecs_os_free(r[1]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_with_gravity_and_drag(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    float drift[3] = {3, 0.5f, 1};
    ecs_entity_t e[2] = { emitter_new(world), emitter_new(world) };
    ecs_set(world, e[0], ParticleForces, {
        .gravity = {0, -9.81f, 0}, .wind = {1, 0, 0}, .drag = 0.5f, .drift = {3, 0.5f, 1}
    });
    ecs_set(world, e[1], ParticleForces, {
        .gravity = {0, -9.81f, 0}, .wind = {1, 0, 0}, .drag = 0.5f
    });
    replay_t *r[2] = { ecs_os_calloc_t(replay_t), ecs_os_calloc_t(replay_t) };
    int32_t i;
    for (i = 0; i < 90; i ++) {
        frame_replay(world, null, e, r, 2);
    }

    test_assert(replay_live(r[0]) >= 88);
    test_int(check_shifted(r[0], r[1], drift, 1e-3f), replay_live(r[0]));

    ecs_os_free(r[0]);
    ecs_os_free(r[1]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_turbulence_moves_with_frame(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    float drift[3] = {5, 0, -2};
    ecs_entity_t e[2] = { emitter_new(world), emitter_new(world) };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_get_mut(world, e[i], ParticleEmitter)->rate = 0;
        ecs_modified(world, e[i], ParticleEmitter);
        ecs_set(world, e[i], ParticleBurst, {.count = 30});
    }

    ecs_set(world, e[0], ParticleForces, {
        .turbulence = 4, .turbulence_scale = 2, .drift = {5, 0, -2}
    });
    ecs_set(world, e[1], ParticleForces, { .turbulence = 4, .turbulence_scale = 2 });
    replay_t *r[2] = { ecs_os_calloc_t(replay_t), ecs_os_calloc_t(replay_t) };
    float phase[3];
    box_particle_phase(7, phase);
    frame_replay(world, null, e, r, 2);
    check_vec3(pool_of(null, e[0])->block.wander, phase[0], phase[1], phase[2]);
    for (i = 1; i < 60; i ++) {
        frame_replay(world, null, e, r, 2);
    }

    check_vec3(pool_of(null, e[1])->block.wander, phase[0], phase[1], phase[2]);
    const float *shifted = pool_of(null, e[0])->block.wander;
    test_assert(fabsf(shifted[0] - (phase[0] - 2 * 5 * 58 * DELTA)) < 1e-3f);
    test_assert(fabsf(shifted[1] - phase[1]) < 1e-5f);
    test_assert(fabsf(shifted[2] - (phase[2] + 2 * 2 * 58 * DELTA)) < 1e-3f);
    test_int(replay_live(r[0]), 30);
    test_int(check_shifted(r[0], r[1], drift, 2e-3f), 30);

    ecs_os_free(r[0]);
    ecs_os_free(r[1]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_frame_phase_wraps(void) {
    // particle/drift
    float phase[3] = {1, 2, 3}, out[3], w1[3], w2[3];
    double offset[3] = {0, 0, 0};
    int32_t c;
    box_particle_frame_phase(phase, offset, 1.5f, out);
    check_vec3(out, 1, 2, 3);

    offset[0] = 2;
    offset[1] = -1;
    box_particle_frame_phase(phase, offset, 1.5f, out);
    check_vec3(out, -2, 3.5f, 3);

    float near_[3] = {1e5f, 0, 0}, pos[3] = {0.25f, -0.5f, 0.75f}, at[3];
    offset[0] = 1e5;
    offset[1] = 0;
    box_particle_frame_phase(phase, offset, 1, out);
    test_assert(fabsf(out[0]) < BOX_PARTICLE_WANDER_PERIOD + 4);
    for (c = 0; c < 3; c ++) {
        at[c] = pos[c] + near_[c];
    }

    box_particle_wander(pos, phase, 1, 3, w1);
    box_particle_wander(at, out, 1, 3, w2);
    test_assert(fabsf(w1[0] - w2[0]) < 0.05f);
    test_assert(fabsf(w1[1] - w2[1]) < 0.05f);
    test_assert(fabsf(w1[2] - w2[2]) < 0.05f);
}

void Drift_sources_add_up(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_singleton_set(world, ParticleForces, { .drift = {1, 0, 0}, .turbulence_scale = 1 });
    ecs_entity_t zone = ecs_new(world);
    ecs_set(world, zone, ParticleForces, { .drift = {0, 2, 0}, .turbulence_scale = 1 });
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleForces, { .drift = {0, 0, 3}, .turbulence_scale = 1 });
    ecs_get_mut(world, e, ParticleEmitter)->forces = zone;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    check_vec3(pool_of(null, e)->block.drift, 1, 2, 3);
    check_vec3(pool_of(null, e)->block.acceleration, 0, 0, 0);

    ecs_remove(world, e, ParticleForces);
    frame(world);
    check_vec3(pool_of(null, e)->block.drift, 1, 2, 0);

    ecs_singleton_remove(world, ParticleForces);
    frame(world);
    check_vec3(pool_of(null, e)->block.drift, 0, 2, 0);

    ecs_get_mut(world, e, ParticleEmitter)->forces = 0;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    check_vec3(pool_of(null, e)->block.drift, 0, 0, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_change_moves_from_next_frame(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    float first[3] = {1, 0, 0}, second[3] = {0, 0, -2};
    ecs_entity_t e[1] = { emitter_new(world) };
    ecs_set(world, e[0], ParticleForces, {.drift = {1, 0, 0}});
    replay_t *r[1] = { ecs_os_calloc_t(replay_t) };
    int32_t i;
    for (i = 0; i < 20; i ++) {
        frame_replay(world, null, e, r, 1);
        check_step(r[0], first);
    }

    int32_t live = replay_live(r[0]);
    test_assert(live >= 18);
    ecs_set(world, e[0], ParticleForces, {.drift = {0, 0, -2}});
    frame_replay(world, null, e, r, 1);
    check_vec3(pool_of(null, e[0])->block.drift, 0, 0, -2);
    test_int(check_step(r[0], second), live);

    ParticleForces *f = ecs_get_mut(world, e[0], ParticleForces);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, ecs_id(ParticleForces), f);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "drift"), 0);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_set_float(&cur, 4), 0);
    test_int(ecs_meta_next(&cur), 0);
    test_int(ecs_meta_set_float(&cur, 0), 0);
    test_int(ecs_meta_next(&cur), 0);
    test_int(ecs_meta_set_float(&cur, 0), 0);
    test_int(ecs_meta_pop(&cur), 0);
    test_int(ecs_meta_pop(&cur), 0);
    ecs_modified(world, e[0], ParticleForces);
    float third[3] = {4, 0, 0};
    live = replay_live(r[0]);
    frame_replay(world, null, e, r, 1);
    check_vec3(pool_of(null, e[0])->block.drift, 4, 0, 0);
    test_int(check_step(r[0], third), live);

    ecs_os_free(r[0]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_effect_layers_drift(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t zone = ecs_new(world);
    ecs_set(world, zone, ParticleForces, {.drift = {2, 0, 0}});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, prefab, ParticleEmitter);
    p->lifetime = (ParticleRange){2, 2};
    p->spread = 60;
    p->forces = zone;
    ecs_modified(world, prefab, ParticleEmitter);
    ecs_set(world, prefab, ParticleBurst, {.count = 8});

    vec3 at = {1, 0, 3};
    ecs_entity_t root = box_particle_burst_at(world, prefab, at, 1);
    test_assert(root != 0);
    ecs_entity_t e[1] = { root };
    replay_t *r[1] = { ecs_os_calloc_t(replay_t) };
    float first[3] = {2, 0, 0}, second[3] = {0, -1, 4};
    int32_t i;
    frame_replay(world, null, e, r, 1);
    test_int(ecs_singleton_get(world, ParticleStats)->effects, 1);
    check_vec3(pool_of(null, root)->block.drift, 2, 0, 0);
    test_int(replay_live(r[0]), 8);
    for (i = 0; i < 10; i ++) {
        frame_replay(world, null, e, r, 1);
        test_int(check_step(r[0], first), 8);
    }

    ecs_set(world, zone, ParticleForces, {.drift = {0, -1, 4}});
    frame_replay(world, null, e, r, 1);
    check_vec3(pool_of(null, root)->block.drift, 0, -1, 4);
    test_int(check_step(r[0], second), 8);

    ecs_entity_t other = box_particle_burst_at(world, prefab, at, 1);
    test_assert(other != 0 && other != root);
    frame(world);
    check_vec3(pool_of(null, other)->block.drift, 0, -1, 4);

    ecs_os_free(r[0]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_lingering_follows_change(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t zone = ecs_new(world);
    ecs_set(world, zone, ParticleForces, {.drift = {1, 0, 0}, .gravity = {0, -1, 0}});
    ecs_entity_t e[1] = { emitter_new(world) };
    ecs_set(world, e[0], ParticleForces, {.drift = {0, 1, 0}});
    ParticleEmitter *p = ecs_get_mut(world, e[0], ParticleEmitter);
    p->forces = zone;
    p->linger = true;
    p->acceleration[2] = 2;
    ecs_modified(world, e[0], ParticleEmitter);
    replay_t *r[1] = { ecs_os_calloc_t(replay_t) };
    int32_t i;
    for (i = 0; i < 10; i ++) {
        frame_replay(world, null, e, r, 1);
    }

    int32_t live = replay_live(r[0]);
    test_assert(live >= 9);
    ecs_delete(world, e[0]);
    float first[3] = {1, 1, 0}, second[3] = {0, 1, -3};
    frame_replay(world, null, e, r, 1);
    test_int(ecs_singleton_get(world, ParticleStats)->lingering, 1);
    test_uint(pool_of(null, e[0])->block.count, 0);
    check_vec3(pool_of(null, e[0])->block.drift, 1, 1, 0);
    check_vec3(pool_of(null, e[0])->block.acceleration, 0, -1, 2);
    test_int(check_step(r[0], first), live);

    ecs_set(world, zone, ParticleForces, {.drift = {0, 0, -3}, .gravity = {0, -1, 0}});
    frame_replay(world, null, e, r, 1);
    check_vec3(pool_of(null, e[0])->block.drift, 0, 1, -3);
    check_vec3(pool_of(null, e[0])->block.acceleration, 0, -1, 2);
    test_int(check_step(r[0], second), live);

    ecs_os_free(r[0]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Drift_slides_on_ground(void) {
    // particle/drift
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t e[1] = { ecs_new(world) };
    ecs_add(world, e[0], ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e[0], ParticleEmitter);
    p->max = 1;
    p->rate = 60;
    p->direction[1] = 0;
    p->lifetime = (ParticleRange){10, 10};
    ecs_modified(world, e[0], ParticleEmitter);
    ecs_set(world, e[0], ParticleForces, {.gravity = {0, -9.81f, 0}, .drift = {2, 0, 0}});
    ecs_add(world, e[0], ParticleCollision);
    replay_t *r[1] = { ecs_os_calloc_t(replay_t) };
    int32_t i;
    for (i = 0; i < 61; i ++) {
        frame_replay(world, null, e, r, 1);
        test_assert(r[0]->slots[0].position[1] >= 0);
    }

    test_uint(pool_of(null, e[0])->block.collide, BOX_PARTICLE_GROUND_BOUNCE);
    test_int(replay_live(r[0]), 1);
    test_flt(r[0]->slots[0].position[1], 0);
    test_assert(fabsf(r[0]->slots[0].position[0] - 2) < 1e-3f);
    test_assert(fabsf(r[0]->slots[0].velocity[0]) < 1e-6f);

    ecs_os_free(r[0]);
    ecs_fini(world);
    box_render_null_free(null);
}

/** Frame in which the burst system of a test plays again */
#define BURST_AGAIN (70)

/** What the burst system of a test plays */
typedef struct burst_ctx_t {
    ecs_entity_t effect;    /**< The template */
    ecs_entity_t root;      /**< Root of the instance the burst returned */
    int32_t frames;         /**< Frames the system ran */
    bool stage;             /**< Burst on the stage of the system, else on its world */
} burst_ctx_t;

/** Play the template at (2, 0, 1) in the first frame the system runs and again in the frame
 * BURST_AGAIN, after the first play went back to the pool.
 *
 * @param it The iterator of the system
 */
static void BurstTwice(
    ecs_iter_t *it)
{
    burst_ctx_t *ctx = it->ctx;
    vec3 at = {2, 0, 1};
    if (!ctx->frames || ctx->frames == BURST_AGAIN) {
        ctx->root = box_particle_burst_at(
            ctx->stage ? it->stage : it->world, ctx->effect, at, 1);
    }

    ctx->frames ++;
}

/** Check that the pools of the same layer of two instances in two worlds match.
 *
 * @param a_null The backend of the first world
 * @param a The layer in the first world
 * @param b_null The backend of the second world
 * @param b The layer in the second world
 */
static void check_same_pool(
    box_render_null_t *a_null,
    ecs_entity_t a,
    box_render_null_t *b_null,
    ecs_entity_t b)
{
    const box_render_particle_pool_t *pa = pool_of(a_null, a), *pb = pool_of(b_null, b);
    int32_t c;
    test_assert((pa != NULL) == (pb != NULL));
    if (pa) {
        for (c = 0; c < 4; c ++) {
            test_assert(fabsf(pa->block.model[3][c] - pb->block.model[3][c]) < 1e-5f);
        }

        test_uint(pa->block.count, pb->block.count);
        test_uint(pa->block.first, pb->block.first);
        test_uint(pa->block.used, pb->block.used);
    }
}

/** Check that bursts from an EcsOnUpdate system play as those outside a system, a new
 * instance and one that went back to the pool.
 *
 * @param stage Burst on the stage of the system, else on its world
 */
static void burst_from_system(
    bool stage)
{
    box_render_null_t *a_null, *b_null;
    ecs_world_t *a = world_new(&a_null);
    ecs_world_t *b = world_new(&b_null);
    test_assert(ecs_script_run(a, "templates", TEMPLATES, NULL) == 0);
    test_assert(ecs_script_run(b, "templates", TEMPLATES, NULL) == 0);

    burst_ctx_t ctx = { .effect = ecs_lookup(b, "Puff"), .stage = stage };
    test_assert(ctx.effect != 0);
    ecs_system(b, {
        .entity = ecs_entity(b, { .name = "BurstTwice" }),
        .phase = EcsOnUpdate,
        .run = BurstTwice,
        .ctx = &ctx
    });

    vec3 at = {2, 0, 1};
    ecs_entity_t a_root = box_particle_burst_at(a, ecs_lookup(a, "Puff"), at, 1);
    test_assert(a_root != 0);
    ecs_entity_t b_first = 0;
    int32_t i;
    for (i = 0; i < 2 * BURST_AGAIN; i ++) {
        if (i == BURST_AGAIN) {
            test_int(ecs_singleton_get(a, ParticleStats)->effects, 0);
            test_int(ecs_singleton_get(b, ParticleStats)->effects, 0);
            test_assert(box_particle_burst_at(a, ecs_lookup(a, "Puff"), at, 1) == a_root);
        }

        frame(a);
        frame(b);
        if (!i) {
            b_first = ctx.root;
            test_assert(ctx.root != 0);
            test_assert(ecs_is_alive(b, ctx.root));
            test_assert(ecs_has_pair(b, ctx.root, EcsIsA, ctx.effect));
            test_assert(pool_of(b_null, ctx.root) != NULL);
            test_assert(pool_of(a_null, ecs_lookup_child(a, a_root, "ring")) != NULL);
            test_assert(pool_of(a_null, ecs_lookup_child(a, a_root, "spark")) != NULL);
            check_vec3(pool_of(a_null, ecs_lookup_child(a, a_root, "ring"))->block.model[3],
                2, 0, 1);
        }

        const ParticleStats *sa = ecs_singleton_get(a, ParticleStats);
        const ParticleStats *sb = ecs_singleton_get(b, ParticleStats);
        test_int(sb->effects, sa->effects);
        test_int(sb->particles, sa->particles);
        test_int(sb->spawned, sa->spawned);
        test_int(sb->simulated, sa->simulated);
        if (i % BURST_AGAIN < 50) {
            test_int(sb->effects, 1);
        }

        if (!sa->effects) {
            continue;
        }

        check_same_pool(a_null, a_root, b_null, ctx.root);
        check_same_pool(a_null, ecs_lookup_child(a, a_root, "ring"),
            b_null, ecs_lookup_child(b, ctx.root, "ring"));
        check_same_pool(a_null, ecs_lookup_child(a, a_root, "spark"),
            b_null, ecs_lookup_child(b, ctx.root, "spark"));
        test_flt(ecs_get(b, ctx.root, ParticleEffect)->time,
            ecs_get(a, a_root, ParticleEffect)->time);
    }

    test_int(ctx.frames, 2 * BURST_AGAIN);
    test_assert(ctx.root == b_first);
    test_int(ecs_singleton_get(b, ParticleStats)->effects, 0);

    ecs_fini(a);
    box_render_null_free(a_null);
    ecs_fini(b);
    box_render_null_free(b_null);
}

void Drift_burst_from_system(void) {
    // particle/effect
    burst_from_system(true);
}

void Drift_burst_from_system_world(void) {
    // particle/effect
    burst_from_system(false);
}
