/**
 * @file GlobalWind.c
 * @brief Tests for ParticleForces.global_wind: its default and reflection, the global wind and
 * gust field the extract packs into the block of a pool from the Wind singleton, and the C twin
 * of the gust field the integrate pass and the WindSway of the meshes share.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "simulate.h"

/** Frame delta of the tests, a power of two so the clock is exact */
#define DELTA (1.0f / 64.0f)

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

/** Create an emitter under a parent of a uniform scale with forces of a global wind share.
 *
 * @param world The world
 * @param scale Uniform scale of the parent
 * @param share ParticleForces.global_wind of the emitter
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    float scale,
    float share)
{
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Scale, {scale, scale, scale});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, ParticleEmitter, {
        .max = 100, .rate = 60, .speed = {1, 1}, .size = {0.5f, 0.5f},
        .lifetime = {1, 2}, .opacity = {1, 1}, .direction = {0, 1, 0}
    });
    ecs_set(world, e, ParticleForces, {
        .wind = {1, 0, -3}, .drag = 0.75f, .turbulence_scale = 1, .global_wind = share
    });

    return e;
}

/** Set a gusty Wind singleton toward {3, 4}, normalized to {0.6, 0.8}, at speed 10.
 *
 * @param world The world
 */
static void wind_set(
    ecs_world_t *world)
{
    ecs_singleton_set(world, Wind, {
        .direction = {3, 4}, .speed = 10, .gust = 0.25f, .gust_scale = 40
    });
}

/** Check that every global wind member of a block is 0.
 *
 * @param b The block
 */
static void check_no_global(
    const box_render_particle_block_t *b)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        test_assert(b->global_wind[i] == 0.0f);
    }

    test_assert(b->turbulence[3] == 0.0f);
    test_assert(b->wander[3] == 0.0f);
    test_assert(b->inherit[3] == 0.0f);
}

/** Check the global wind of a block against wind_set at a share and the wind of the frame.
 *
 * @param world The world
 * @param b The block
 * @param share The share times the world scale
 */
static void check_global(
    const ecs_world_t *world,
    const box_render_particle_block_t *b,
    float share)
{
    box_render_wind_t w;
    box_render_wind_frame(world, &w);
    test_flt(b->global_wind[0], 0.6f * 10 * share);
    test_flt(b->global_wind[1], 0.8f * 10 * share);
    test_flt(b->global_wind[2], 0.25f);
    test_flt(b->turbulence[3], 1.0f / 40);
    test_assert(b->wander[3] == w.offset[0]);
    test_assert(b->inherit[3] == w.offset[1]);
}

void GlobalWind_c_default(void) {
    // particle/global_wind
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleForces);
    const ParticleForces *f = ecs_get(world, e, ParticleForces);
    test_assert(f != NULL);
    test_flt(f->global_wind, 0);

    ecs_fini(world);
}

void GlobalWind_reflects(void) {
    // particle/global_wind
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t member = ecs_lookup_child(world, ecs_id(ParticleForces), "global_wind");
    test_assert(member != 0);
    const EcsMember *m = ecs_get(world, member, EcsMember);
    test_assert(m != NULL);
    test_uint(m->type, ecs_id(ecs_f32_t));
    test_int(m->offset, (int32_t)offsetof(ParticleForces, global_wind));

    test_assert(ecs_script_run(world, "forces",
        "using box.particle\n"
        "leaves { Forces: {drag: 2, global_wind: 0.75} }\n", NULL) == 0);
    const ParticleForces *f = ecs_get(world, ecs_lookup(world, "leaves"), ParticleForces);
    test_assert(f != NULL);
    test_flt(f->global_wind, 0.75f);
    test_flt(f->drag, 2);
    test_flt(f->gravity[1], -9.81f);

    ecs_fini(world);
}

void GlobalWind_block_packed(void) {
    // particle/global_wind
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    wind_set(world);
    ecs_entity_t e = emitter_new(world, 1, 0.5f);
    frame(world);
    frame(world);

    /* the velocity of the Wind times the share, its gust and gust frequency and the offset of
     * the field the WindSway records get in the same frame */
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    check_global(world, &pool->block, 0.5f);
    test_assert(pool->block.wander[3] != 0.0f);
    test_assert(pool->block.inherit[3] != 0.0f);

    /* the wind of the forces stays its own */
    test_flt(pool->block.wind[0], 1);
    test_flt(pool->block.wind[2], -3);
    test_flt(pool->block.wind[3], 0.75f);

    ecs_fini(world);
    box_render_null_free(null);
}

void GlobalWind_block_follows_wind(void) {
    // particle/global_wind
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    wind_set(world);
    ecs_entity_t e = emitter_new(world, 1, 1);
    frame(world);
    frame(world);
    float x = pool_of(null, e)->block.wander[3];
    frame(world);

    /* the gust field moves along the wind by its speed every frame */
    const box_render_particle_block_t *b = &pool_of(null, e)->block;
    check_global(world, b, 1);
    test_assert(fabsf(b->wander[3] - (x + 0.6f * 10 * DELTA)) < 1e-4f);

    /* a calm Wind pulls nothing */
    ecs_singleton_set(world, Wind, {
        .direction = {3, 4}, .speed = 0, .gust = 0.25f, .gust_scale = 40
    });
    frame(world);
    check_no_global(&pool_of(null, e)->block);

    /* without the Wind singleton the block has no global wind */
    wind_set(world);
    frame(world);
    test_flt(pool_of(null, e)->block.turbulence[3], 1.0f / 40);
    ecs_singleton_remove(world, Wind);
    frame(world);
    check_no_global(&pool_of(null, e)->block);

    ecs_fini(world);
    box_render_null_free(null);
}

void GlobalWind_no_share_untouched(void) {
    // particle/global_wind
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t still = emitter_new(world, 1, 1);
    frame(world);
    frame(world);

    /* with a share but no Wind every member is 0 */
    const box_render_particle_pool_t *pool = pool_of(null, still);
    test_assert(pool != NULL);
    check_no_global(&pool->block);

    /* in a world where the Wind blows an emitter without a share is never moved by it, and
     * its block is the block it has without the Wind */
    ecs_entity_t plain = emitter_new(world, 1, 0);
    frame(world);
    frame(world);
    box_render_particle_block_t before = pool_of(null, plain)->block;
    wind_set(world);
    frame(world);
    const box_render_particle_block_t *b = &pool_of(null, plain)->block;
    check_no_global(b);
    test_assert(b->wind[0] == before.wind[0]);
    test_assert(b->wind[1] == before.wind[1]);
    test_assert(b->wind[2] == before.wind[2]);
    test_assert(b->wind[3] == before.wind[3]);

    ecs_fini(world);
    box_render_null_free(null);
}

void GlobalWind_shares_add_up(void) {
    // particle/global_wind
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    wind_set(world);
    ecs_singleton_set(world, ParticleForces, { .turbulence_scale = 1, .global_wind = 0.25f });
    ecs_entity_t zone = ecs_new(world);
    ecs_set(world, zone, ParticleForces, { .turbulence_scale = 1, .global_wind = 0.125f });
    ecs_entity_t e = emitter_new(world, 1, 0.5f);
    ecs_get_mut(world, e, ParticleEmitter)->forces = zone;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    frame(world);
    check_global(world, &pool_of(null, e)->block, 0.875f);

    /* shares that cancel out pull nothing */
    ecs_set(world, zone, ParticleForces, { .turbulence_scale = 1, .global_wind = -0.75f });
    frame(world);
    check_no_global(&pool_of(null, e)->block);

    ecs_fini(world);
    box_render_null_free(null);
}

void GlobalWind_world_scale(void) {
    // particle/global_wind
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    wind_set(world);
    ecs_entity_t plain = emitter_new(world, 1, 0.5f);
    ecs_entity_t scaled = emitter_new(world, 10, 0.5f);
    frame(world);
    frame(world);

    /* the velocity times the scale; the gust field is that of the world */
    const box_render_particle_block_t *a = &pool_of(null, plain)->block;
    const box_render_particle_block_t *b = &pool_of(null, scaled)->block;
    check_global(world, a, 0.5f);
    check_global(world, b, 5);
    test_assert(a->turbulence[3] == b->turbulence[3]);
    test_assert(a->wander[3] == b->wander[3]);
    test_assert(a->inherit[3] == b->inherit[3]);

    ecs_fini(world);
    box_render_null_free(null);
}

void GlobalWind_linger(void) {
    // particle/global_wind
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    wind_set(world);
    ecs_entity_t e = emitter_new(world, 2, 0.5f);
    ecs_get_mut(world, e, ParticleEmitter)->linger = true;
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    frame(world);

    /* a deleted emitter runs out its particles in the wind of each frame */
    ecs_delete(world, e);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    check_global(world, &pool->block, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void GlobalWind_gust_range(void) {
    // particle/global_wind
    float offset[2] = {3.5f, -12.25f};
    float lo = 2, hi = 0;
    int32_t x, z;
    for (z = -20; z <= 20; z ++) {
        for (x = -20; x <= 20; x ++) {
            float g = box_particle_wind_gust(x * 1.37f, z * 0.91f, offset, 0.2f, 0.5f);
            test_assert(g >= 0.5f - 1e-6f);
            test_assert(g <= 1.5f + 1e-6f);
            lo = g < lo ? g : lo;
            hi = g > hi ? g : hi;
        }
    }

    /* the field varies over the patches */
    test_assert(hi - lo > 0.25f);

    /* a steady wind has no gusts */
    test_flt(box_particle_wind_gust(4.2f, -1.3f, offset, 0.2f, 0), 1);

    /* the lattice points hold the hash, the field is smooth between them */
    float u[2] = {2, 5};
    test_flt(box_particle_wind_noise(u), box_particle_wind_hash(2, 5));
    float v[2] = {2.001f, 5};
    test_assert(fabsf(box_particle_wind_noise(v) - box_particle_wind_hash(2, 5)) < 1e-3f);
}

void GlobalWind_gust_period(void) {
    // particle/global_wind
    float offset[2] = {0, 0};
    float scale = 40, f = 1.0f / scale, period = (float)BOX_WIND_GUST_PERIOD * scale;
    int32_t i;
    for (i = 0; i < 16; i ++) {
        float x = 13.0f * (float)i - 70.0f, z = -9.0f * (float)i + 31.0f;
        float a = box_particle_wind_gust(x, z, offset, f, 1);
        float b = box_particle_wind_gust(x + period, z - period, offset, f, 1);
        test_assert(fabsf(a - b) < 1e-3f);
    }

    /* the field moved by the offset is the field at the point minus the offset */
    float moved[2] = {7, -5};
    test_flt(box_particle_wind_gust(17, 5, moved, f, 1),
        box_particle_wind_gust(10, 10, offset, f, 1));
}

void GlobalWind_velocity(void) {
    // particle/global_wind
    float wind[3] = {3, 4, 0.5f}, offset[2] = {1, 2}, p[3] = {12, 7, -30}, out[3];
    float factor = box_particle_wind_gust(p[0], p[2], offset, 0.05f, wind[2]);
    box_particle_global_wind(wind, offset, 0.05f, p, out);
    test_flt(out[0], 3 * factor);
    test_flt(out[1], 0);
    test_flt(out[2], 4 * factor);

    /* the height does not change the gust */
    float q[3] = {12, 100, -30}, other[3];
    box_particle_global_wind(wind, offset, 0.05f, q, other);
    test_assert(other[0] == out[0]);
    test_assert(other[2] == out[2]);

    /* without a gust frequency the Wind pulls nothing */
    box_particle_global_wind(wind, offset, 0, p, out);
    test_assert(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
}

void GlobalWind_step_drifts_with_wind(void) {
    // particle/global_wind
    float wind[3] = {6, 8, 0.25f}, offset[2] = {5, -3};
    float a[3] = {0, 10, 0}, va[3] = {0, 0, 0};
    float b[3] = {0, 10, 0}, vb[3] = {0, 0, 0};
    int32_t i;
    for (i = 0; i < 600; i ++) {
        box_particle_forces_t f = { .drag = 2, .scale = 1 };
        box_particle_forces_t plain = f;
        float g[3];
        box_particle_global_wind(wind, offset, 1.0f / 40, a, g);
        f.wind[0] += g[0];
        f.wind[1] += g[1];
        f.wind[2] += g[2];
        box_particle_integrate(a, va, &f, NULL, DELTA);
        box_particle_integrate(b, vb, &plain, NULL, DELTA);
    }

    /* the particle in the wind is carried along its direction at about its speed; the one
     * without global wind stays put, bit for bit */
    test_assert(a[0] > 0 && a[2] > 0);
    test_assert(fabsf(a[2] / a[0] - 8.0f / 6.0f) < 1e-3f);
    float speed = sqrtf(va[0] * va[0] + va[2] * va[2]);
    test_assert(speed > 10 * 0.75f - 0.1f && speed < 10 * 1.25f + 0.1f);
    test_flt(va[1], 0);
    test_assert(b[0] == 0.0f && b[1] == 10.0f && b[2] == 0.0f);
    test_assert(vb[0] == 0.0f && vb[1] == 0.0f && vb[2] == 0.0f);
}

void GlobalWind_local_space(void) {
    // particle/global_wind
    /* a local emitter turned a quarter about Y and scaled 2: the wind at the world position of
     * a particle, taken into its space, moves it as the world particle moves */
    float model[4][4] = {
        {0, 0, -2, 0}, {0, 2, 0, 0}, {2, 0, 0, 0}, {5, 1, -4, 1}
    };
    float inverse[9];
    box_particle_inverse(model, inverse);
    float wind[3] = {6, 8, 0.5f}, offset[2] = {2, 3};
    float local[3] = {1, 2, 3}, lv[3] = {0, 0, 0};
    float world[3], wv[3] = {0, 0, 0};
    box_particle_transform(model, local, 1.0f, world);
    int32_t i;
    for (i = 0; i < 30; i ++) {
        box_particle_forces_t f = { .drag = 1.5f, .scale = 1 };
        box_particle_forces_t wf = f, lf;
        box_particle_motion_t lm;
        float at[3], g[3];
        box_particle_transform(model, local, 1.0f, at);
        box_particle_global_wind(wind, offset, 1.0f / 20, at, g);
        f.wind[0] += g[0];
        f.wind[2] += g[2];
        box_particle_local(model, inverse, local, lv, &f, NULL, DELTA, &lf, &lm);
        box_particle_integrate(local, lv, &lf, &lm, DELTA);

        box_particle_global_wind(wind, offset, 1.0f / 20, world, g);
        wf.wind[0] += g[0];
        wf.wind[2] += g[2];
        box_particle_integrate(world, wv, &wf, NULL, DELTA);
    }

    float back[3];
    box_particle_transform(model, local, 1.0f, back);
    test_assert(fabsf(back[0] - world[0]) < 1e-3f);
    test_assert(fabsf(back[1] - world[1]) < 1e-3f);
    test_assert(fabsf(back[2] - world[2]) < 1e-3f);
    /* and the wind carried it from where it spawned, at {11, 5, -6} */
    test_assert(world[0] > 11.1f && world[2] > -5.9f);
    test_flt(world[1], 5);
}
