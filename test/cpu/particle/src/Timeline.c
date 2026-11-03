/**
 * @file Timeline.c
 * @brief Tests for ParticleTimeline: the particles of a rate curve are the area under it at any
 * frame rate, looped across the wrap or once and then the last key, a negative value counts as
 * 0, paused, stopped and reset follow the playback clock, bursts are unchanged, the speed curve
 * scales the particles spawned now and not the live ones, and without it nothing changes.
 *
 * The particles a frame spawns are the count of the block of its pool in the packet.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "simulate.h"

/** Frame delta of most tests, a fixed clock of a whole number of microseconds */
#define DELTA (0.1f)

/** Slots of a replayed pool */
#define SLOTS (4096)

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

/** Create an emitter of a rate whose particles live long enough to never die in a test.
 *
 * @param world The world
 * @param rate Particles per second
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    float rate)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = rate;
    p->max = 4000;
    p->lifetime = (ParticleRange){100, 100};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Create an emitter of a rate with a ParticleTimeline.
 *
 * @param world The world
 * @param rate Particles per second
 * @param timeline The timeline
 * @return The emitter
 */
static ecs_entity_t timeline_new(
    ecs_world_t *world,
    float rate,
    ParticleTimeline timeline)
{
    ecs_entity_t e = emitter_new(world, rate);
    ecs_set_ptr(world, e, ParticleTimeline, &timeline);

    return e;
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

/** Get the particles an emitter spawned in the last frame.
 *
 * @param null The backend
 * @param e The emitter
 * @return The count, 0 when its pool is not in the packet
 */
static int32_t spawned_of(
    box_render_null_t *null,
    ecs_entity_t e)
{
    const box_render_particle_pool_t *pool = pool_of(null, e);

    return pool ? (int32_t)pool->block.count : 0;
}

/** Run a frame of a delta.
 *
 * @param world The world
 * @param delta Seconds of the frame
 */
static void frame(
    ecs_world_t *world,
    float delta)
{
    ecs_progress(world, delta);
    box_render_sync(world);
}

/** Run frames of a delta and add up what an emitter spawned.
 *
 * @param world The world
 * @param null The backend
 * @param e The emitter
 * @param delta Seconds of every frame
 * @param count Frames to run
 * @return The particles it spawned
 */
static int32_t spawn_frames(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t e,
    float delta,
    int32_t count)
{
    int32_t i, result = 0;
    for (i = 0; i < count; i ++) {
        frame(world, delta);
        result += spawned_of(null, e);
    }

    return result;
}

/** Check that a count of particles is within one of an expected real count, the fraction a
 * float sum may carry either way.
 *
 * @param count The count
 * @param expected The expected count
 */
static void near_count(
    int32_t count,
    double expected)
{
    test_assert(fabs((double)count - expected) <= 1.0);
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

/** Area under the part of a curve above 0 from 0 to 1, by the midpoint rule on a fine grid.
 *
 * @param curve The curve
 * @return The area
 */
static double positive_area(
    const ParticleCurve *curve)
{
    Curve view = { .kind = curve->kind, .keys = curve->keys };
    const int32_t steps = 100000;
    double result = 0;
    int32_t i;
    for (i = 0; i < steps; i ++) {
        float v = box_curve_value(&view, ((float)i + 0.5f) / (float)steps, 1.0f);
        result += v > 0 ? (double)v / (double)steps : 0.0;
    }

    return result;
}

void Timeline_c_defaults(void) {
    // particle/timeline
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleTimeline);
    const ParticleTimeline *t = ecs_get(world, e, ParticleTimeline);
    test_assert(t != NULL);
    test_flt(t->period, 0);
    test_bool(t->loop, false);
    test_int(t->rate.kind, CurveLinear);
    test_int(ecs_vec_count(&t->rate.keys), 0);
    test_int(t->speed.kind, CurveLinear);
    test_int(ecs_vec_count(&t->speed.keys), 0);
    ecs_fini(world);
}

void Timeline_script_values(void) {
    // particle/timeline
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int result = ecs_script_run(world, "timeline",
        "using box.curve\n"
        "using box.particle\n"
        "pulse {\n"
        "  ParticleEmitter: {rate: 50}\n"
        "  ParticleTimeline: {period: 2, loop: true, "
        "rate: {keys: [{0, 0}, {0.5, 1}, {1, 0}]}, "
        "speed: {kind: CurveCatmullRom, keys: [{0, 1}, {1, 2}]}}\n"
        "}\n", NULL);
    test_int(result, 0);
    ecs_entity_t e = ecs_lookup(world, "pulse");
    test_assert(e != 0);
    const ParticleTimeline *t = ecs_get(world, e, ParticleTimeline);
    test_assert(t != NULL);
    test_flt(t->period, 2);
    test_bool(t->loop, true);
    test_int(t->rate.kind, CurveLinear);
    test_int(ecs_vec_count(&t->rate.keys), 3);
    const CurveKey *rate = ecs_vec_first_t(&t->rate.keys, CurveKey);
    test_flt(rate[1].time, 0.5f);
    test_flt(rate[1].value, 1);
    test_int(t->speed.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&t->speed.keys), 2);
    const CurveKey *speed = ecs_vec_first_t(&t->speed.keys, CurveKey);
    test_flt(speed[1].value, 2);
    ecs_fini(world);
}

void Timeline_area_at_frame_rates(void) {
    // particle/timeline
    static const float deltas[] = { 0.1f, 0.05f, 0.02f, 0.008f, 0.004f };
    ParticleCurve curves[2] = {
        { CurveLinear, BOX_CURVE_KEYS({0, 0}, {0.5f, 1}, {1, 0}) },
        { CurveCatmullRom, BOX_CURVE_KEYS({0, 0.5f}, {0.3f, 1.5f}, {0.7f, 1}, {1, 0.5f}) }
    };
    int32_t c, d;
    for (c = 0; c < 2; c ++) {
        Curve view = { .kind = curves[c].kind, .keys = curves[c].keys };
        double expected = 200.0 * (double)box_curve_area(&view, 0, 1, 1.0f);
        if (c == 0) {
            test_assert(fabs(expected - 100.0) < 1e-3);
        }

        for (d = 0; d < 5; d ++) {
            box_render_null_t *null;
            ecs_world_t *world = world_new(&null);
            ecs_entity_t e = timeline_new(world, 200, (ParticleTimeline){
                .period = 1, .rate = curves[c]
            });
            int32_t frames = (int32_t)lroundf(1.0f / deltas[d]);
            near_count(spawn_frames(world, null, e, deltas[d], frames), expected);
            ecs_fini(world);
            box_render_null_free(null);
        }
    }
}

void Timeline_loop_across_wrap(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ParticleTimeline timeline = {
        .period = 0.25f, .loop = true,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 1}) }
    };
    ecs_entity_t e = timeline_new(world, 400, timeline);
    ecs_add(world, e, ParticlePlayback);

    frame(world, DELTA);
    near_count(spawned_of(null, e), 400.0 * 0.25 * 0.08);
    frame(world, DELTA);
    near_count(spawned_of(null, e), 400.0 * 0.25 * 0.24);
    frame(world, DELTA);
    near_count(spawned_of(null, e), 400.0 * 0.25 * (0.18 + 0.02));

    spawn_frames(world, null, e, DELTA, 7);
    test_assert(fabsf(time_of(world, e) - 1.0f) < 1e-4f);
    ecs_fini(world);
    box_render_null_free(null);

    world = world_new(&null);
    e = timeline_new(world, 400, timeline);
    frame(world, 0.6f);
    near_count(spawned_of(null, e), 400.0 * 0.25 * (2 * 0.5 + 0.08));
    ecs_fini(world);
    box_render_null_free(null);

    world = world_new(&null);
    e = timeline_new(world, 400, timeline);
    near_count(spawn_frames(world, null, e, DELTA, 10), 400.0 * 0.25 * 0.5 * 4);
    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_once_holds_last_key(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = timeline_new(world, 100, (ParticleTimeline){
        .period = 0.5f,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1, 0.5f}) }
    });
    int32_t first = spawn_frames(world, null, e, DELTA, 5);
    near_count(first, 100.0 * 0.5 * 0.75);
    near_count(first + spawn_frames(world, null, e, DELTA, 10), 100.0 * (0.5 * 0.75 + 0.5));
    frame(world, DELTA);
    near_count(spawned_of(null, e), 5);
    ecs_fini(world);
    box_render_null_free(null);

    world = world_new(&null);
    e = timeline_new(world, 100, (ParticleTimeline){
        .period = 1,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {0.5f, 0}) }
    });
    near_count(spawn_frames(world, null, e, DELTA, 5), 100.0 * 0.25);
    test_int(spawn_frames(world, null, e, DELTA, 15), 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_zero_rate_spawns_nothing(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = timeline_new(world, 1000, (ParticleTimeline){
        .period = 1, .loop = true, .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}) }
    });
    ecs_entity_t b = timeline_new(world, 1000, (ParticleTimeline){
        .period = 0.3f, .rate = { CurveCatmullRom, BOX_CURVE_KEYS({0, 0}, {1, 0}) }
    });
    int32_t i;
    for (i = 0; i < 20; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, a), 0);
        test_int(spawned_of(null, b), 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_negative_keys(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = timeline_new(world, 100, (ParticleTimeline){
        .period = 1, .rate = { CurveLinear, BOX_CURVE_KEYS({0, -1}, {1, 1}) }
    });
    ecs_entity_t none = timeline_new(world, 100, (ParticleTimeline){
        .period = 1, .loop = true, .rate = { CurveLinear, BOX_CURVE_KEYS({0, -2}) }
    });

    int32_t i, total = 0, nothing = 0;
    for (i = 0; i < 5; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, e), 0);
        nothing += spawned_of(null, none);
    }

    for (i = 0; i < 5; i ++) {
        frame(world, DELTA);
        total += spawned_of(null, e);
        nothing += spawned_of(null, none);
    }

    near_count(total, 100.0 * 0.25);
    test_int(nothing, 0);
    ecs_fini(world);
    box_render_null_free(null);

    ParticleCurve dip = {
        CurveCatmullRom, BOX_CURVE_KEYS({0, 1}, {0.4f, -0.5f}, {0.7f, 0.2f}, {1, 1})
    };
    double expected = 300.0 * positive_area(&dip);
    Curve view = { .kind = dip.kind, .keys = dip.keys };
    test_assert(expected > 300.0 * (double)box_curve_area(&view, 0, 1, 1.0f) + 1.0);
    static const float deltas[] = { 0.1f, 0.02f, 0.004f };
    int32_t d;
    for (d = 0; d < 3; d ++) {
        world = world_new(&null);
        e = timeline_new(world, 300, (ParticleTimeline){ .period = 1, .rate = dip });
        near_count(spawn_frames(world, null, e, deltas[d], (int32_t)lroundf(1.0f / deltas[d])),
            expected);
        ecs_fini(world);
        box_render_null_free(null);
    }
}

void Timeline_paused_holds(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ParticleTimeline timeline = {
        .period = 1, .loop = true, .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 1}) }
    };
    ecs_entity_t a = timeline_new(world, 300, timeline);
    ecs_entity_t b = timeline_new(world, 300, timeline);
    ecs_add(world, a, ParticlePlayback);
    int32_t expected[6], i;
    for (i = 0; i < 3; i ++) {
        frame(world, DELTA);
        expected[i] = spawned_of(null, b);
        test_int(spawned_of(null, a), expected[i]);
    }

    state_set(world, a, ParticlePaused);
    for (i = 0; i < 3; i ++) {
        frame(world, DELTA);
        expected[3 + i] = spawned_of(null, b);
        test_int(spawned_of(null, a), 0);
        test_assert(fabsf(time_of(world, a) - 0.3f) < 1e-4f);
    }

    state_set(world, a, ParticlePlaying);
    for (i = 0; i < 3; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, a), expected[3 + i]);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_stopped_and_reset(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ParticleTimeline timeline = {
        .period = 1, .rate = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1, 0}) }
    };
    ecs_entity_t a = timeline_new(world, 300, timeline);
    int32_t i;

    state_set(world, a, ParticleStopped);
    for (i = 0; i < 3; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, a), 0);
        test_flt(time_of(world, a), 0);
    }

    state_set(world, a, ParticlePlaying);
    ecs_entity_t b = timeline_new(world, 300, timeline);
    for (i = 0; i < 4; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, a), spawned_of(null, b));
    }

    state_set(world, a, ParticleStopped);
    frame(world, DELTA);
    test_int(spawned_of(null, a), 0);
    state_set(world, a, ParticlePlaying);
    ecs_entity_t c = timeline_new(world, 300, timeline);
    for (i = 0; i < 4; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, a), spawned_of(null, c));
    }

    ParticlePlayback *p = ecs_ensure(world, a, ParticlePlayback);
    p->reset = true;
    ecs_modified(world, a, ParticlePlayback);
    frame(world, DELTA);
    test_int(spawned_of(null, a), 0);
    test_flt(time_of(world, a), 0);
    ecs_entity_t d = timeline_new(world, 300, timeline);
    int32_t sa = 0, sd = 0;
    for (i = 0; i < 5; i ++) {
        frame(world, DELTA);
        sa += spawned_of(null, a);
        sd += spawned_of(null, d);
    }

    test_assert(abs(sa - sd) <= 1);
    near_count(sa, 300.0 * 0.375);
    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_bursts_unchanged(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ParticleTimeline timeline = {
        .period = 1, .loop = true,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}) },
        .speed = { CurveLinear, BOX_CURVE_KEYS({0, 2}) }
    };
    ecs_entity_t a = timeline_new(world, 100, timeline);
    ecs_entity_t b = emitter_new(world, 0);
    ecs_set(world, a, ParticleBurst, {.count = 7, .at = 0.2f, .repeat = 2});
    ecs_set(world, b, ParticleBurst, {.count = 7, .at = 0.2f, .repeat = 2});
    ecs_entity_t first = timeline_new(world, 100, timeline);
    ecs_set(world, first, ParticleBurst, {.count = 9});
    int32_t i, total = 0;
    for (i = 0; i < 10; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, a), spawned_of(null, b));
        test_int(spawned_of(null, first), i == 0 ? 9 : 0);
        total += spawned_of(null, a);
    }

    test_int(total, 21);
    ecs_fini(world);
    box_render_null_free(null);
}

/** Spawn the particles of the block of a pool as the spawn pass does for a point emitter whose
 * cone is its direction, and age and move the live ones as the integrate pass does.
 *
 * @param slots The slots of the pool
 * @param null The backend
 * @param pool The pool in the last packet
 */
static void replay_step(
    box_particle_t *slots,
    box_render_null_t *null,
    const box_render_particle_pool_t *pool)
{
    const box_render_particle_block_t *b = &pool->block;
    box_particle_forces_t f = {
        .drag = b->wind[3], .turbulence = b->turbulence[0], .scale = b->turbulence[1]
    };
    uint32_t i;
    int32_t k;
    test_assert(pool->capacity <= SLOTS);
    for (k = 0; k < 3; k ++) {
        f.acceleration[k] = b->acceleration[k];
        f.wind[k] = b->wind[k];
        f.phase[k] = b->wander[k];
        f.drift[k] = b->drift[k];
    }

    for (i = 0; i < b->used; i ++) {
        box_particle_t *p = &slots[i];
        if (p->remaining) {
            p->remaining = box_particle_age(p->remaining, b->delta_us);
            if (p->remaining) {
                box_particle_integrate(p->position, p->velocity, &f, NULL, b->acceleration[3]);
            }
        }
    }

    const uint32_t *spawn = box_render_packet_data(&null->last, pool->slots);
    for (i = 0; i < b->count; i ++) {
        uint32_t index = b->first + i;
        test_assert(spawn[i] != BOX_RENDER_PARTICLE_NO_SLOT);
        box_particle_t *p = &slots[spawn[i]];
        float speed = box_particle_draw(b->seed, index, BOX_PARTICLE_STREAM_SPEED,
            b->speed[0], b->speed[1]);
        memset(p, 0, sizeof(*p));
        for (k = 0; k < 3; k ++) {
            p->velocity[k] = b->direction[k] * speed;
        }

        p->remaining = box_particle_life(b->seed, index, b->life_min, b->life_span);
        p->index = index;
    }
}

void Timeline_speed_scales_new_particles(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = timeline_new(world, 10, (ParticleTimeline){
        .period = 1, .speed = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1, 3}) }
    });
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->speed = (ParticleRange){2, 2};
    ecs_modified(world, e, ParticleEmitter);
    box_particle_t *slots = ecs_os_calloc_n(box_particle_t, SLOTS);
    int32_t i;
    uint32_t s;

    for (i = 0; i < 5; i ++) {
        frame(world, DELTA);
        const box_render_particle_pool_t *pool = pool_of(null, e);
        test_assert(pool != NULL);
        test_uint(pool->block.count, 1);
        float m = 1.0f + 2.0f * 0.1f * (float)(i + 1);
        test_assert(fabsf(pool->block.speed[0] - 2.0f * m) < 1e-4f);
        test_assert(fabsf(pool->block.speed[1] - 2.0f * m) < 1e-4f);
        replay_step(slots, null, pool);
    }

    int32_t live = 0;
    for (s = 0; s < SLOTS; s ++) {
        if (slots[s].remaining) {
            float m = 1.0f + 2.0f * 0.1f * (float)(slots[s].index + 1);
            test_assert(fabsf(slots[s].velocity[1] - 2.0f * m) < 1e-4f);
            test_flt(slots[s].velocity[0], 0);
            live ++;
        }
    }

    test_int(live, 5);

    for (i = 0; i < 10; i ++) {
        frame(world, DELTA);
        replay_step(slots, null, pool_of(null, e));
    }

    test_assert(fabsf(pool_of(null, e)->block.speed[0] - 6.0f) < 1e-4f);
    for (s = 0; s < SLOTS; s ++) {
        if (slots[s].remaining && slots[s].index < 5) {
            test_assert(slots[s].velocity[1] < 5.0f);
        }
    }

    test_flt(pool_of(null, e)->block.size[0], 0.1f);
    ecs_os_free(slots);
    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_without_timeline_unchanged(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t plain = emitter_new(world, 25);
    ecs_entity_t off = timeline_new(world, 25, (ParticleTimeline){
        .period = 0, .loop = true,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}) },
        .speed = { CurveLinear, BOX_CURVE_KEYS({0, 3}) }
    });
    ecs_entity_t empty = timeline_new(world, 25, (ParticleTimeline){ .period = 1 });
    static const int32_t expected[] = { 2, 3, 2, 3, 2, 3 };
    int32_t i;
    for (i = 0; i < 6; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, plain), expected[i]);
        test_int(spawned_of(null, off), expected[i]);
        test_int(spawned_of(null, empty), expected[i]);
        test_flt(pool_of(null, off)->block.speed[0], pool_of(null, plain)->block.speed[0]);
        test_flt(pool_of(null, empty)->block.speed[1], pool_of(null, plain)->block.speed[1]);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** Errors logged while capturing */
static char timeline_errors[4096];

/** Log level before capturing */
static int32_t timeline_level;

/** Log function before capturing */
static ecs_os_api_log_t timeline_log;

/** Whether the log was colored before capturing */
static bool timeline_colors;

/** Log function that keeps the errors.
 *
 * @param level The level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void timeline_capture(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -3 && strlen(timeline_errors) + strlen(msg) + 2 < sizeof(timeline_errors)) {
        strcat(timeline_errors, msg);
        strcat(timeline_errors, "\n");
    }
}

void Timeline_bad_curve_is_one(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t plain = emitter_new(world, 25);
    ecs_entity_t e = emitter_new(world, 25);
    ecs_set_name(world, e, "pulse");

    timeline_errors[0] = '\0';
    timeline_level = ecs_log_set_level(-1);
    timeline_colors = ecs_log_enable_colors(false);
    timeline_log = ecs_os_api.log_;
    ecs_os_api.log_ = timeline_capture;
    ecs_set(world, e, ParticleTimeline, {
        .period = 1,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {0, 2}) },
        .speed = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1.5f, 2}) }
    });
    ecs_os_api.log_ = timeline_log;
    ecs_log_enable_colors(timeline_colors);
    ecs_log_set_level(timeline_level);

    test_assert(strstr(timeline_errors, "invalid ParticleTimeline on pulse: rate key 1: time 0 "
        "is not after time 0 of key 0") != NULL);
    test_assert(strstr(timeline_errors, "invalid ParticleTimeline on pulse: speed key 1: "
        "time 1.5 is outside of 0 to 1") != NULL);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, e), spawned_of(null, plain));
        test_flt(pool_of(null, e)->block.speed[0], pool_of(null, plain)->block.speed[0]);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_max_keys_is_one(void) {
    // particle/timeline
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t plain = emitter_new(world, 25);
    ecs_entity_t e = emitter_new(world, 25);
    ecs_set_name(world, e, "pulse");

    timeline_errors[0] = '\0';
    timeline_level = ecs_log_set_level(-1);
    timeline_colors = ecs_log_enable_colors(false);
    timeline_log = ecs_os_api.log_;
    ecs_os_api.log_ = timeline_capture;
    ecs_set(world, e, ParticleTimeline, {
        .period = 1,
        .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 2}), BOX_CURVE_KEYS({0, 1}, {1, 1}) },
        .speed = { CurveLinear, BOX_CURVE_KEYS({0, 2}, {1, 2}), BOX_CURVE_KEYS({0, 3}, {1, 3}) }
    });
    ecs_os_api.log_ = timeline_log;
    ecs_log_enable_colors(timeline_colors);
    ecs_log_set_level(timeline_level);

    test_assert(strstr(timeline_errors, "invalid ParticleTimeline on pulse: rate max_keys is not "
        "allowed, the playback clock has no random draw to blend two curves by") != NULL);
    test_assert(strstr(timeline_errors, "invalid ParticleTimeline on pulse: speed max_keys is "
        "not allowed") != NULL);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        frame(world, DELTA);
        test_int(spawned_of(null, e), spawned_of(null, plain));
        test_flt(pool_of(null, e)->block.speed[0], pool_of(null, plain)->block.speed[0]);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Timeline_copies_own_keys(void) {
    // particle/timeline
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, ParticleTimeline, {
        .period = 1, .rate = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 1}) }
    });
    ecs_entity_t a = ecs_clone(world, 0, base, true);
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, base);
    ParticleTimeline *t = ecs_get_mut(world, a, ParticleTimeline);
    ecs_vec_first_t(&t->rate.keys, CurveKey)[1].value = 5;
    const ParticleTimeline *orig = ecs_get(world, base, ParticleTimeline);
    test_flt(ecs_vec_first_t(&orig->rate.keys, CurveKey)[1].value, 1);
    test_int(ecs_vec_count(&ecs_get(world, b, ParticleTimeline)->rate.keys), 2);
    ecs_delete(world, a);
    ecs_delete(world, base);
    ecs_fini(world);
}
