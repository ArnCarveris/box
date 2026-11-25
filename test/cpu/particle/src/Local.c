/**
 * @file Local.c
 * @brief Tests for ParticleEmitter.local: particles kept in the space of the emitter move, turn
 * and scale with it, while gravity and the ground plane keep acting in the world, and a world
 * space emitter leaves its particles behind as before.
 *
 * The particles of a pool are replayed on the CPU from the blocks of the packets with the C twin
 * of the integrate and spawn passes, and taken to the world through the model of the block as
 * the draw does.
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

/** The particles of a pool as the integrate and spawn passes leave them */
typedef struct replay_t {
    box_particle_t slots[SLOTS]; /**< The slots of the pool */
    float moved[SLOTS];     /**< Seconds each particle moved */
    float world[SLOTS][3];  /**< World position of each particle as the last block draws it */
    float travel[SLOTS][3]; /**< World velocity of each particle as the last block draws it */
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

/** Create an emitter at the origin of its parent that spawns a particle every frame inside a
 * sphere, moving along x.
 *
 * @param world The world
 * @param parent Parent of the emitter, 0 for none
 * @param local ParticleEmitter.local
 * @param speed Speed of the particles
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    bool local,
    float speed)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 60;
    p->shape = EmitterSphere;
    p->radius = 1;
    p->direction[0] = 1;
    p->direction[1] = 0;
    p->direction[2] = 0;
    p->spread = 30;
    p->speed = (ParticleRange){speed, speed};
    p->lifetime = (ParticleRange){5, 5};
    p->seed = 11;
    p->local = local;
    ecs_modified(world, e, ParticleEmitter);

    return e;
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
    float inverse[9];
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
    box_particle_transform(b->model, cone, 0.0f, world);
    box_particle_transform(b->model, local, 0.0f, offset);
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

    if (b->local) {
        box_particle_inverse(b->model, inverse);
        memcpy(out->position, local, sizeof(local));
        box_particle_mul3(inverse, out->velocity, out->velocity);
    }

    out->remaining = box_particle_life(b->seed, index, b->life_min, b->life_span);
    out->index = index;
}

/** Run the integrate and spawn passes of a pool of the last packet on the replay, then take its
 * particles to the world as the draw does.
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
    float delta = b->acceleration[3], inverse[9];
    uint32_t i;
    test_assert(pool->capacity <= SLOTS);
    if (pool->clear) {
        memset(r->slots, 0, sizeof(r->slots));
        memset(r->moved, 0, sizeof(r->moved));
    }

    box_particle_inverse(b->model, inverse);
    for (i = 0; i < 3; i ++) {
        f.acceleration[i] = b->acceleration[i];
        f.wind[i] = b->wind[i];
        f.phase[i] = b->wander[i];
        f.drift[i] = b->drift[i];
    }

    for (i = 0; i < b->used; i ++) {
        box_particle_t *p = &r->slots[i];
        bool alive;
        if (!p->remaining) {
            continue;
        }

        p->remaining = box_particle_age(p->remaining, b->delta_us);
        if (!p->remaining) {
            continue;
        }

        if (b->local) {
            box_particle_forces_t lf;
            box_particle_motion_t lm;
            box_particle_local(b->model, inverse, p->position, p->velocity, &f, NULL, delta,
                &lf, &lm);
            box_particle_integrate(p->position, p->velocity, &lf, &lm, delta);
            alive = box_particle_collide_local(b->model, inverse, p->position, p->velocity,
                &g);
        } else {
            box_particle_integrate(p->position, p->velocity, &f, NULL, delta);
            alive = box_particle_collide(p->position, p->velocity, &g);
        }

        r->moved[i] += delta;
        if (!alive) {
            p->remaining = 0;
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

    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &r->slots[i];
        if (b->local) {
            box_particle_transform(b->model, p->position, 1.0f, r->world[i]);
            box_particle_transform(b->model, p->velocity, 0.0f, r->travel[i]);
        } else {
            memcpy(r->world[i], p->position, sizeof(r->world[i]));
            memcpy(r->travel[i], p->velocity, sizeof(r->travel[i]));
        }
    }
}

/** Run a frame and the passes of the pool of an emitter on its replay.
 *
 * @param world The world
 * @param null The backend
 * @param e The emitter
 * @param r Its replay
 */
static void frame_replay(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t e,
    replay_t *r)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    replay_step(r, null, pool);
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

/** Check that the particles that lived through the last step are where a world transform takes
 * their world position and velocity of the step before, moved by that velocity over the step.
 *
 * @param before The replay before the step
 * @param after The replay after it
 * @param m The transform of the world, columns, applied to the world points
 * @param tolerance Largest difference allowed
 * @return Particles checked
 */
static int32_t check_carried(
    const replay_t *before,
    const replay_t *after,
    const float m[4][4],
    float tolerance)
{
    int32_t i, c, result = 0;
    for (i = 0; i < SLOTS; i ++) {
        const box_particle_t *p = &after->slots[i], *q = &before->slots[i];
        float moved[3], expect[3], travel[3];
        if (!p->remaining || !q->remaining || p->index != q->index) {
            continue;
        }

        for (c = 0; c < 3; c ++) {
            moved[c] = before->world[i][c] + before->travel[i][c] * DELTA;
        }

        box_particle_transform(m, moved, 1.0f, expect);
        box_particle_transform(m, before->travel[i], 0.0f, travel);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(after->world[i][c] - expect[c]) < tolerance);
            test_assert(fabsf(after->travel[i][c] - travel[c]) < tolerance);
        }

        result ++;
    }

    return result;
}

void Local_c_default(void) {
    // particle/local
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    test_bool(ecs_get(world, e, ParticleEmitter)->local, false);
    ecs_fini(world);
}

void Local_in_block(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t e = emitter_new(world, parent, true, 1);
    ecs_entity_t w = emitter_new(world, parent, false, 1);
    ecs_get_mut(world, e, ParticleEmitter)->inherit_velocity = 1;
    ecs_modified(world, e, ParticleEmitter);
    ecs_get_mut(world, w, ParticleEmitter)->inherit_velocity = 1;
    ecs_modified(world, w, ParticleEmitter);
    ecs_progress(world, DELTA);
    box_render_sync(world);

    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.local, 1);
    test_flt(pool->block.inherit[0], 0);
    test_flt(pool->block.inherit[1], 0);
    test_flt(pool->block.inherit[2], 0);
    pool = pool_of(null, w);
    test_assert(pool != NULL);
    test_uint(pool->block.local, 0);
    test_assert(fabsf(pool->block.inherit[0] - 60.0f) < 1e-2f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Local_moves_with_parent(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t e = emitter_new(world, parent, true, 2);
    replay_t *r = ecs_os_calloc_t(replay_t), *before = ecs_os_calloc_t(replay_t);
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {5, 2, -1, 1}};
    int32_t i;
    for (i = 0; i < 30; i ++) {
        frame_replay(world, null, e, r);
    }

    test_assert(replay_live(r) >= 29);
    *before = *r;
    ecs_set(world, parent, Position, {5, 2, -1});
    frame_replay(world, null, e, r);
    test_assert(check_carried(before, r, m, 1e-4f) >= 29);

    ecs_os_free(before);
    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_turns_with_parent(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t e = emitter_new(world, parent, true, 2);
    ecs_set(world, e, Position, {2, 0, 0});
    replay_t *r = ecs_os_calloc_t(replay_t), *before = ecs_os_calloc_t(replay_t);
    float m[4][4];
    int32_t i, turned = 0;
    for (i = 0; i < 30; i ++) {
        frame_replay(world, null, e, r);
    }

    *before = *r;
    ecs_set(world, parent, Rotation, {0, 1.5707964f, 0});
    frame_replay(world, null, e, r);
    mat4 parent_m;
    box_transform_mat4(ecs_get(world, parent, WorldTransform), NULL, parent_m);
    memcpy(m, parent_m, sizeof(m));
    test_assert(fabsf(m[0][0]) < 1e-5f);
    test_assert(check_carried(before, r, m, 1e-4f) >= 29);

    // The particles went a quarter turn around the parent, not stayed where they were
    for (i = 0; i < SLOTS; i ++) {
        float dx = r->world[i][0] - before->world[i][0];
        float dz = r->world[i][2] - before->world[i][2];
        if (r->slots[i].remaining && before->slots[i].remaining &&
            dx * dx + dz * dz > 1.0f)
        {
            turned ++;
        }
    }

    test_assert(turned >= 29);

    ecs_os_free(before);
    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_scales_with_parent(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t e = emitter_new(world, parent, true, 0);
    replay_t *r = ecs_os_calloc_t(replay_t), *before = ecs_os_calloc_t(replay_t);
    float m[4][4] = {{2, 0, 0, 0}, {0, 2, 0, 0}, {0, 0, 2, 0}, {-1, 0, 0, 1}};
    int32_t i;
    for (i = 0; i < 30; i ++) {
        frame_replay(world, null, e, r);
    }

    *before = *r;
    ecs_set(world, parent, Scale, {2, 2, 2});
    frame_replay(world, null, e, r);
    test_assert(fabsf(pool_of(null, e)->block.size[0] - 0.2f) < 1e-6f);
    test_assert(check_carried(before, r, m, 1e-4f) >= 29);

    ecs_os_free(before);
    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_gravity_points_down(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, true, 0);
    ecs_set(world, e, Position, {3, 4, 0});
    ecs_set(world, e, Rotation, {0.7f, 0.4f, 1.2f});
    ecs_set(world, e, ParticleForces, {.gravity = {0, -9.81f, 0}, .turbulence_scale = 1});
    replay_t *r = ecs_os_calloc_t(replay_t);
    int32_t i, checked = 0;
    for (i = 0; i < 40; i ++) {
        frame_replay(world, null, e, r);
    }

    for (i = 0; i < SLOTS; i ++) {
        if (!r->slots[i].remaining || r->moved[i] < DELTA) {
            continue;
        }

        test_assert(fabsf(r->travel[i][0]) < 1e-3f);
        test_assert(fabsf(r->travel[i][2]) < 1e-3f);
        test_assert(fabsf(r->travel[i][1] + 9.81f * r->moved[i]) < 1e-3f);
        checked ++;
    }

    test_assert(checked >= 30);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_ground_in_world(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, true, 1);
    ecs_set(world, e, Position, {0, 1, 0});
    ecs_set(world, e, Rotation, {0.5f, 0, 2.5f});
    ecs_set(world, e, ParticleForces, {.gravity = {0, -9.81f, 0}, .turbulence_scale = 1});
    ecs_set(world, e, ParticleCollision, {.ground = true, .height = 0.5f, .bounce = 0.5f,
        .friction = 0.2f});
    replay_t *r = ecs_os_calloc_t(replay_t);
    int32_t i, landed = 0;
    for (i = 0; i < 120; i ++) {
        frame_replay(world, null, e, r);
    }

    for (i = 0; i < SLOTS; i ++) {
        if (!r->slots[i].remaining || r->moved[i] <= 0) {
            continue;
        }

        test_assert(r->world[i][1] >= 0.5f - 1e-4f);
        landed += r->moved[i] > 1.0f && fabsf(r->world[i][1] - 0.5f) < 0.05f;
    }

    test_assert(landed > 0);

    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_still_matches_world(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = emitter_new(world, 0, true, 2);
    ecs_entity_t b = emitter_new(world, 0, false, 2);
    ecs_entity_t both[2] = {a, b};
    replay_t *r[2] = {ecs_os_calloc_t(replay_t), ecs_os_calloc_t(replay_t)};
    int32_t i, k, c, checked = 0;
    for (k = 0; k < 2; k ++) {
        ecs_set(world, both[k], Position, {3, 1, -2});
        ecs_set(world, both[k], Rotation, {0.3f, 1.1f, -0.4f});
        ecs_set(world, both[k], Scale, {1.5f, 1.5f, 1.5f});
        ecs_set(world, both[k], ParticleForces, {.gravity = {0, -2, 0}, .wind = {1, 0, 0},
            .drag = 0.5f, .turbulence = 1, .turbulence_scale = 1});
    }

    for (i = 0; i < 60; i ++) {
        ecs_progress(world, DELTA);
        box_render_sync(world);
        for (k = 0; k < 2; k ++) {
            const box_render_particle_pool_t *pool = pool_of(null, both[k]);
            test_assert(pool != NULL);
            replay_step(r[k], null, pool);
        }
    }

    for (i = 0; i < SLOTS; i ++) {
        if (!r[0]->slots[i].remaining) {
            continue;
        }

        test_assert(r[1]->slots[i].remaining == r[0]->slots[i].remaining);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(r[0]->world[i][c] - r[1]->world[i][c]) < 1e-3f);
            test_assert(fabsf(r[0]->travel[i][c] - r[1]->travel[i][c]) < 1e-3f);
        }

        checked ++;
    }

    test_assert(checked >= 59);

    ecs_os_free(r[0]);
    ecs_os_free(r[1]);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_world_space_unchanged(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t e = emitter_new(world, parent, false, 2);
    replay_t *r = ecs_os_calloc_t(replay_t), *before = ecs_os_calloc_t(replay_t);
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    int32_t i;
    for (i = 0; i < 30; i ++) {
        frame_replay(world, null, e, r);
    }

    *before = *r;
    ecs_set(world, parent, Position, {5, 2, -1});
    ecs_set(world, parent, Rotation, {0, 1.5707964f, 0});
    frame_replay(world, null, e, r);
    test_uint(pool_of(null, e)->block.local, 0);

    // The live particles stay where they were in the world, moved by their velocity only
    test_assert(check_carried(before, r, m, 1e-5f) >= 29);

    ecs_os_free(before);
    ecs_os_free(r);
    ecs_fini(world);
    box_render_null_free(null);
}

void Local_change_empties_pool(void) {
    // particle/local
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 0, false, 2);
    int32_t i;
    ecs_get_mut(world, e, ParticleEmitter)->rate = 120;
    ecs_modified(world, e, ParticleEmitter);
    for (i = 0; i < 30; i ++) {
        ecs_progress(world, DELTA);
        box_render_sync(world);
    }

    test_assert(ecs_singleton_get(world, ParticleStats)->particles >= 58);
    ecs_get_mut(world, e, ParticleEmitter)->local = true;
    ecs_modified(world, e, ParticleEmitter);
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_bool(pool->clear, true);
    test_uint(pool->block.local, 1);
    test_assert(ecs_singleton_get(world, ParticleStats)->particles <= 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Local_script_value(void) {
    // particle/local
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "test", "using box.particle\n"
        "e { Emitter: {local: true} }\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);
    test_bool(ecs_get(world, e, ParticleEmitter)->local, true);
    ecs_fini(world);
}
