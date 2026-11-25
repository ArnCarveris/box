/**
 * @file Emitter.c
 * @brief AudioEmitter of box.audio through a null output: the intervals on the frame clock, the
 * first play after an interval, the shapes and their transforms, the ranges of volume and pitch,
 * determinism, skips beyond max_distance, over max_active and of sounds that are not ready,
 * playing, disable and delete, the routing, clamped values, plays that are not spatial, the
 * scene of test/assets/audio and the audio command.
 *
 * The plays are read from the one shots of the module after each frame: every play an emitter
 * sends this frame is there, with its frame, position, volume and pitch.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"
#include <stdlib.h>

/** Plays a log keeps at most */
#define EMITTER_LOG_MAX 4096

/** A play of an emitter as the module sent it */
typedef struct emitter_play_t {
    uint32_t handle;        /**< Voice handle */
    uint64_t when;          /**< Frame of the core clock it starts at */
    float position[3];      /**< World position */
    float volume;           /**< Volume before the distance */
    float pitch;            /**< Pitch ratio */
    uint8_t bus;            /**< Bus index */
    bool spatial;           /**< Whether it is spatial */
} emitter_play_t;

/** The plays of one emitter, in the order they were sent */
typedef struct emitter_log_t {
    ecs_entity_t emitter;   /**< The emitter */
    uint32_t seen;          /**< Largest handle seen */
    int32_t count;          /**< Plays in plays */
    emitter_play_t plays[EMITTER_LOG_MAX]; /**< The plays */
} emitter_log_t;

/** Logs of the cases, large for the stack */
static emitter_log_t emitter_log_a, emitter_log_b;

/** Start a log of an emitter, of the plays the module sends from now on.
 *
 * @param world The world
 * @param log The log
 * @param e The emitter
 */
static void emitter_log(
    ecs_world_t *world,
    emitter_log_t *log,
    ecs_entity_t e)
{
    log->emitter = e;
    log->seen = box_audio_module_get(world)->next_voice - 1;
    log->count = 0;
}

/** Append the plays of the emitter of a log that the module sent since the last call.
 *
 * @param world The world
 * @param log The log
 */
static void emitter_collect(
    ecs_world_t *world,
    emitter_log_t *log)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const box_audio_oneshot_t *shots = ecs_vec_first(&state->oneshots);
    uint32_t seen = log->seen;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->oneshots); i ++) {
        const box_audio_oneshot_t *s = &shots[i];
        if (s->handle <= log->seen) {
            continue;
        }

        seen = s->handle > seen ? s->handle : seen;
        if (s->emitter != log->emitter || log->count >= EMITTER_LOG_MAX) {
            continue;
        }

        log->plays[log->count ++] = (emitter_play_t){
            .handle = s->handle, .when = s->when,
            .position = { s->position[0], s->position[1], s->position[2] },
            .volume = s->volume, .pitch = s->pitch, .bus = s->bus, .spatial = s->spatial };
    }

    log->seen = seen;
}

/** Run frames of a delta and collect the plays of up to two logs after each.
 *
 * @param world The world
 * @param frames Number of frames
 * @param dt Delta of a frame
 * @param a A log, may be NULL
 * @param b A log, may be NULL
 */
static void emitter_run(
    ecs_world_t *world,
    int32_t frames,
    float dt,
    emitter_log_t *a,
    emitter_log_t *b)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        test_bool(ecs_progress(world, dt), true);
        if (a) {
            emitter_collect(world, a);
        }

        if (b) {
            emitter_collect(world, b);
        }
    }
}

/** Create an emitter with the defaults of its constructor but its sound and interval.
 *
 * @param world The world
 * @param sound The sound
 * @param min interval_min
 * @param max interval_max
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    ecs_entity_t sound,
    float min,
    float max)
{
    ecs_entity_t e = ecs_new(world);
    AudioEmitter *em = ecs_ensure(world, e, AudioEmitter);
    em->sound = sound;
    em->interval_min = min;
    em->interval_max = max;
    ecs_modified(world, e, AudioEmitter);

    return e;
}

/** Get the AudioEmitter of an entity to change it, finished with ecs_modified.
 *
 * @param world The world
 * @param e The emitter
 * @return The component
 */
static AudioEmitter* emitter_edit(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_ensure(world, e, AudioEmitter);
}

/** Get the state of an emitter.
 *
 * @param world The world
 * @param e The emitter
 * @return The state
 */
static const AudioEmitterState* emitter_state(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const AudioEmitterState *result = ecs_get(world, e, AudioEmitterState);
    test_assert(result != NULL);

    return result;
}

/** Create an AudioListener at a place.
 *
 * @param world The world
 * @param x X
 * @param y Y
 * @param z Z
 * @return The listener
 */
static ecs_entity_t emitter_listener(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_add(world, e, AudioListener);

    return e;
}

/** Distance of a play from a point.
 *
 * @param p The play
 * @param x X
 * @param y Y
 * @param z Z
 * @return The distance
 */
static float emitter_distance(
    const emitter_play_t *p,
    float x,
    float y,
    float z)
{
    float dx = p->position[0] - x, dy = p->position[1] - y, dz = p->position[2] - z;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/** Check that the frames between consecutive plays of a log are within a range.
 *
 * @param log The log
 * @param min Fewest frames
 * @param max Most frames
 */
static void emitter_expect_spacing(
    const emitter_log_t *log,
    uint64_t min,
    uint64_t max)
{
    int32_t i;
    for (i = 1; i < log->count; i ++) {
        uint64_t d = log->plays[i].when - log->plays[i - 1].when;
        if (d < min || d > max) {
            printf("play %d: %llu frames after the last, expected %llu..%llu\n", i,
                (unsigned long long)d, (unsigned long long)min, (unsigned long long)max);
        }

        test_assert(log->plays[i].when > log->plays[i - 1].when);
        test_assert(d >= min && d <= max);
    }
}

void Emitter_interval_range(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, 100);
    ecs_entity_t e = emitter_new(world, dc, 0.1f, 0.3f);
    uint64_t lo = UINT64_MAX, hi = 0, d;
    int32_t i;
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 600, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 30);
    test_assert(emitter_log_a.count <= 101);
    test_assert(emitter_log_a.plays[0].when >= AUDIO_RATE / 10);
    test_assert(emitter_log_a.plays[0].when <= AUDIO_RATE * 3 / 10);
    emitter_expect_spacing(&emitter_log_a, AUDIO_RATE / 10, AUDIO_RATE * 3 / 10);
    for (i = 1; i < emitter_log_a.count; i ++) {
        d = emitter_log_a.plays[i].when - emitter_log_a.plays[i - 1].when;
        lo = d < lo ? d : lo;
        hi = d > hi ? d : hi;
    }

    test_assert(lo < (uint64_t)(AUDIO_RATE * 0.13));
    test_assert(hi > (uint64_t)(AUDIO_RATE * 0.27));
    test_int(emitter_state(world, e)->plays, emitter_log_a.count);
    test_int(emitter_state(world, e)->skipped, 0);
    ecs_fini(world);
}

void Emitter_first_play_delayed(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, 100);
    ecs_entity_t e = emitter_new(world, dc, 0.5f, 0.5f);
    ecs_entity_t many[8];
    int32_t i, k;
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 1, AUDIO_DT, &emitter_log_a, NULL);
    test_int(emitter_log_a.count, 0);
    test_int(emitter_state(world, e)->plays, 0);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) == 0.0f);

    emitter_run(world, 40, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 1);
    test_uint(emitter_log_a.plays[0].when, AUDIO_RATE / 2);
    test_assert(audio_peak(audio_tap_l, 0, AUDIO_RATE / 2) == 0.0f);
    test_assert(audio_peak(audio_tap_l, AUDIO_RATE / 2, AUDIO_RATE / 2 + 100) > 0.0f);
    ecs_fini(world);

    world = audio_world();
    dc = audio_dc(world, "dc", 0.1f, 100);
    for (i = 0; i < 8; i ++) {
        many[i] = emitter_new(world, dc, 1.0f, 4.0f);
    }

    audio_run(world, 1);
    for (i = 0; i < 8; i ++) {
        test_int(emitter_state(world, many[i])->plays, 0);
        test_assert(emitter_state(world, many[i])->next >= AUDIO_RATE);
        test_assert(emitter_state(world, many[i])->next <= AUDIO_RATE * 4);
        for (k = 0; k < i; k ++) {
            test_assert(emitter_state(world, many[i])->next !=
                emitter_state(world, many[k])->next);
        }
    }

    ecs_fini(world);
}

void Emitter_sub_frame_interval(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, 280);
    ecs_entity_t e = emitter_new(world, dc, 0.01f, 0.01f);
    int32_t i;
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 40, 0.05f, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 195);
    test_assert(emitter_log_a.count <= 201);
    test_uint(emitter_log_a.plays[0].when, AUDIO_RATE / 100);
    emitter_expect_spacing(&emitter_log_a, AUDIO_RATE / 100, AUDIO_RATE / 100);
    for (i = 0; i < emitter_log_a.count; i ++) {
        int32_t at = (int32_t)emitter_log_a.plays[i].when;
        if (at + 300 >= audio_tap_count) {
            break;
        }

        test_assert(audio_peak(audio_tap_l, at - 150, at) < 1e-6f);
        test_assert(audio_peak(audio_tap_l, at, at + 20) > 0.0f);
        test_assert(audio_peak(audio_tap_l, at, at + 280) > 0.4f);
    }

    test_int(emitter_state(world, e)->skipped, 0);
    ecs_fini(world);
}

void Emitter_shape_point(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, 100);
    ecs_entity_t e = emitter_new(world, dc, 0.02f, 0.05f);
    int32_t i;
    ecs_set(world, e, Position, {3, 1, -2});
    emitter_edit(world, e)->radius = 5;
    emitter_edit(world, e)->extents[0] = 4;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 15);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_bool(emitter_log_a.plays[i].spatial, true);
        test_flt(emitter_log_a.plays[i].position[0], 3);
        test_flt(emitter_log_a.plays[i].position[1], 1);
        test_flt(emitter_log_a.plays[i].position[2], -2);
    }

    ecs_fini(world);
}

void Emitter_shape_disc(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t e = emitter_new(world, dc, 0.01f, 0.01f);
    int32_t i, inner = 0, quadrants[4] = {0};
    ecs_set(world, e, Position, {1, 2, 3});
    emitter_edit(world, e)->shape = AudioEmitterDisc;
    emitter_edit(world, e)->radius = 4;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 600, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 900);
    for (i = 0; i < emitter_log_a.count; i ++) {
        const emitter_play_t *p = &emitter_log_a.plays[i];
        float dx = p->position[0] - 1, dz = p->position[2] - 3;
        float r = sqrtf(dx * dx + dz * dz);
        test_assert(fabsf(p->position[1] - 2) < 1e-4f);
        test_assert(r <= 4.0f + 1e-4f);
        inner += r < 4.0f / sqrtf(2.0f);
        quadrants[(dx >= 0 ? 1 : 0) + (dz >= 0 ? 2 : 0)] ++;
    }

    float share = (float)inner / (float)emitter_log_a.count;
    test_assert(share > 0.42f && share < 0.58f);
    for (i = 0; i < 4; i ++) {
        test_assert(quadrants[i] > emitter_log_a.count / 6);
    }

    ecs_fini(world);
}

void Emitter_shape_sphere(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t e = emitter_new(world, dc, 0.01f, 0.01f);
    int32_t i, inner = 0, below = 0;
    ecs_set(world, e, Position, {-2, 1, 0});
    emitter_edit(world, e)->shape = AudioEmitterSphere;
    emitter_edit(world, e)->radius = 2;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 600, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 900);
    for (i = 0; i < emitter_log_a.count; i ++) {
        const emitter_play_t *p = &emitter_log_a.plays[i];
        float r = emitter_distance(p, -2, 1, 0);
        test_assert(r <= 2.0f + 1e-4f);
        inner += r < 2.0f * cbrtf(0.5f);
        below += p->position[1] < 1;
    }

    float share = (float)inner / (float)emitter_log_a.count;
    test_assert(share > 0.42f && share < 0.58f);
    share = (float)below / (float)emitter_log_a.count;
    test_assert(share > 0.42f && share < 0.58f);
    ecs_fini(world);
}

void Emitter_shape_hemisphere(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t e = emitter_new(world, dc, 0.01f, 0.01f);
    int32_t i, inner = 0, high = 0;
    ecs_set(world, e, Position, {0, 3, 0});
    emitter_edit(world, e)->shape = AudioEmitterHemisphere;
    emitter_edit(world, e)->radius = 6;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 600, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 900);
    for (i = 0; i < emitter_log_a.count; i ++) {
        const emitter_play_t *p = &emitter_log_a.plays[i];
        float r = emitter_distance(p, 0, 3, 0);
        test_assert(r <= 6.0f + 1e-4f);
        test_assert(p->position[1] >= 3.0f - 1e-5f);
        inner += r < 6.0f * cbrtf(0.5f);
        high += p->position[1] > 6.0f;
    }

    float share = (float)inner / (float)emitter_log_a.count;
    test_assert(share > 0.42f && share < 0.58f);
    test_assert(high > 0);
    ecs_fini(world);
}

void Emitter_shape_box(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t e = emitter_new(world, dc, 0.01f, 0.01f);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}, mean[3] = {0};
    const float origin[3] = {5, 0, -5}, extents[3] = {1, 2, 3};
    int32_t i, k;
    ecs_set(world, e, Position, {5, 0, -5});
    emitter_edit(world, e)->shape = AudioEmitterBox;
    emitter_edit(world, e)->extents[0] = 1;
    emitter_edit(world, e)->extents[1] = 2;
    emitter_edit(world, e)->extents[2] = 3;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 600, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 900);
    for (i = 0; i < emitter_log_a.count; i ++) {
        for (k = 0; k < 3; k ++) {
            float v = emitter_log_a.plays[i].position[k] - origin[k];
            test_assert(fabsf(v) <= extents[k] + 1e-4f);
            lo[k] = v < lo[k] ? v : lo[k];
            hi[k] = v > hi[k] ? v : hi[k];
            mean[k] += v / (float)emitter_log_a.count;
        }
    }

    for (k = 0; k < 3; k ++) {
        test_assert(lo[k] < -0.9f * extents[k]);
        test_assert(hi[k] > 0.9f * extents[k]);
        test_assert(fabsf(mean[k]) < 0.1f * extents[k]);
    }

    ecs_fini(world);
}

void Emitter_shape_transform(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t box = emitter_new(world, dc, 0.01f, 0.01f);
    ecs_entity_t disc = emitter_new(world, dc, 0.01f, 0.01f);
    const float extents[3] = {1, 0.5f, 0.25f};
    float far_z = 0, far_y = 0;
    mat4 inv;
    vec3 local;
    int32_t i, k;
    ecs_set(world, box, Position, {10, 0, 0});
    ecs_set(world, box, Rotation, {0, (float)(AUDIO_PI / 2), 0});
    ecs_set(world, box, Scale, {2, 2, 2});
    emitter_edit(world, box)->shape = AudioEmitterBox;
    emitter_edit(world, box)->extents[0] = 1;
    emitter_edit(world, box)->extents[1] = 0.5f;
    emitter_edit(world, box)->extents[2] = 0.25f;
    ecs_modified(world, box, AudioEmitter);
    ecs_set(world, disc, Position, {0, 0, -10});
    ecs_set(world, disc, Rotation, {(float)(AUDIO_PI / 2), 0, 0});
    ecs_set(world, disc, Scale, {3, 3, 3});
    emitter_edit(world, disc)->shape = AudioEmitterDisc;
    emitter_edit(world, disc)->radius = 1;
    ecs_modified(world, disc, AudioEmitter);
    emitter_log(world, &emitter_log_a, box);
    emitter_log(world, &emitter_log_b, disc);
    emitter_run(world, 300, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_assert(emitter_log_a.count >= 400);
    test_assert(emitter_log_b.count >= 400);

    box_transform_mat4(ecs_get(world, box, WorldTransform), NULL, inv);
    glm_mat4_inv(inv, inv);
    for (i = 0; i < emitter_log_a.count; i ++) {
        const emitter_play_t *p = &emitter_log_a.plays[i];
        glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, p->position), 1.0f, local);
        for (k = 0; k < 3; k ++) {
            test_assert(fabsf(local[k]) <= extents[k] + 1e-3f);
        }

        test_assert(fabsf(p->position[0] - 10) <= 0.5f + 1e-3f);
        test_assert(fabsf(p->position[2]) <= 2.0f + 1e-3f);
        far_z = fabsf(p->position[2]) > far_z ? fabsf(p->position[2]) : far_z;
    }

    test_assert(far_z > 1.6f);

    for (i = 0; i < emitter_log_b.count; i ++) {
        const emitter_play_t *p = &emitter_log_b.plays[i];
        float r = sqrtf(p->position[0] * p->position[0] + p->position[1] * p->position[1]);
        test_assert(fabsf(p->position[2] + 10) < 1e-3f);
        test_assert(r <= 3.0f + 1e-3f);
        far_y = fabsf(p->position[1]) > far_y ? fabsf(p->position[1]) : far_y;
    }

    test_assert(far_y > 2.5f);
    ecs_fini(world);
}

void Emitter_shape_degenerate(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t disc = emitter_new(world, dc, 0.01f, 0.01f);
    ecs_entity_t sphere = emitter_new(world, dc, 0.01f, 0.01f);
    ecs_entity_t box = emitter_new(world, dc, 0.01f, 0.01f);
    ecs_entity_t flat = emitter_new(world, dc, 0.01f, 0.01f);
    float far_ = 0, wide = 0;
    vec3 out;
    int32_t i;
    ecs_set(world, disc, Position, {1, 1, 1});
    emitter_edit(world, disc)->shape = AudioEmitterDisc;
    ecs_modified(world, disc, AudioEmitter);
    ecs_set(world, sphere, Position, {0, 0, 0});
    emitter_edit(world, sphere)->shape = AudioEmitterSphere;
    emitter_edit(world, sphere)->radius = -2;
    ecs_modified(world, sphere, AudioEmitter);
    ecs_set(world, box, Position, {0, 0, 0});
    emitter_edit(world, box)->shape = AudioEmitterBox;
    emitter_edit(world, box)->extents[0] = -1;
    ecs_modified(world, box, AudioEmitter);
    ecs_set(world, flat, Position, {2, 2, 2});
    emitter_edit(world, flat)->shape = AudioEmitterBox;
    ecs_modified(world, flat, AudioEmitter);

    emitter_log(world, &emitter_log_a, disc);
    emitter_log(world, &emitter_log_b, sphere);
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_assert(emitter_log_a.count >= 50);
    test_assert(emitter_log_b.count >= 50);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_assert(emitter_distance(&emitter_log_a.plays[i], 1, 1, 1) < 1e-5f);
    }

    for (i = 0; i < emitter_log_b.count; i ++) {
        float r = emitter_distance(&emitter_log_b.plays[i], 0, 0, 0);
        test_assert(r <= 2.0f + 1e-4f);
        far_ = r > far_ ? r : far_;
    }

    test_assert(far_ > 1.5f);

    emitter_log(world, &emitter_log_a, box);
    emitter_log(world, &emitter_log_b, flat);
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_assert(emitter_log_a.count >= 50);
    test_assert(emitter_log_b.count >= 50);
    for (i = 0; i < emitter_log_a.count; i ++) {
        const emitter_play_t *p = &emitter_log_a.plays[i];
        test_assert(fabsf(p->position[0]) <= 1.0f + 1e-5f);
        test_flt(p->position[1], 0);
        test_flt(p->position[2], 0);
        wide = fabsf(p->position[0]) > wide ? fabsf(p->position[0]) : wide;
    }

    test_assert(wide > 0.8f);
    for (i = 0; i < emitter_log_b.count; i ++) {
        test_assert(emitter_distance(&emitter_log_b.plays[i], 2, 2, 2) < 1e-5f);
    }

    box_audio_emitter_point(AudioEmitterSphere, NAN, (vec3){0, 0, 0},
        (double[3]){0.9, 0.3, 0.6}, out);
    test_flt(out[0], 0);
    test_flt(out[1], 0);
    test_flt(out[2], 0);
    box_audio_emitter_point(AudioEmitterHemisphere, -3, (vec3){0, 0, 0},
        (double[3]){0.999, 0.999, 0.25}, out);
    test_assert(out[1] > 2.5f);
    ecs_fini(world);
}

void Emitter_volume_pitch_ranges(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t e = emitter_new(world, dc, 0.01f, 0.02f);
    float vlo = 1e9f, vhi = 0, plo = 1e9f, phi = 0;
    int32_t i;
    AudioEmitter *em = emitter_edit(world, e);
    em->volume_min = 0.2f;
    em->volume_max = 0.6f;
    em->pitch_min = 0.5f;
    em->pitch_max = 2.0f;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 300, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 200);
    for (i = 0; i < emitter_log_a.count; i ++) {
        const emitter_play_t *p = &emitter_log_a.plays[i];
        test_bool(p->spatial, false);
        test_assert(p->volume >= 0.2f && p->volume <= 0.6f);
        test_assert(p->pitch >= 0.5f && p->pitch <= 2.0f);
        vlo = p->volume < vlo ? p->volume : vlo;
        vhi = p->volume > vhi ? p->volume : vhi;
        plo = p->pitch < plo ? p->pitch : plo;
        phi = p->pitch > phi ? p->pitch : phi;
    }

    test_assert(vlo < 0.25f && vhi > 0.55f);
    test_assert(plo < 0.6f && phi > 1.9f);

    em = emitter_edit(world, e);
    em->volume_min = em->volume_max = 0.3f;
    em->pitch_min = em->pitch_max = 1.5f;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 20);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_flt(emitter_log_a.plays[i].volume, 0.3f);
        test_flt(emitter_log_a.plays[i].pitch, 1.5f);
    }

    ecs_fini(world);
}

/** Render two seconds of a scene of emitters into a buffer.
 *
 * @param out Receives AUDIO_RATE * 2 frames of the left channel then of the right
 */
static void emitter_render_scene(
    float *out)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t tone = audio_tone(world, "tone", 880, 0.3f, AUDIO_RATE / 20);
    ecs_entity_t a = emitter_new(world, tone, 0.05f, 0.2f);
    ecs_entity_t b = emitter_new(world, tone, 0.05f, 0.2f);
    AudioEmitter *em;
    emitter_listener(world, 0, 0, 0);
    ecs_set(world, a, Position, {-3, 0, -2});
    ecs_set(world, b, Position, {4, 0, -1});
    em = emitter_edit(world, a);
    em->shape = AudioEmitterSphere;
    em->radius = 3;
    em->volume_min = 0.3f;
    em->pitch_min = 0.8f;
    em->pitch_max = 1.3f;
    em->seed = 7;
    ecs_modified(world, a, AudioEmitter);
    em = emitter_edit(world, b);
    em->shape = AudioEmitterBox;
    em->extents[0] = em->extents[1] = em->extents[2] = 2;
    em->volume_min = 0.5f;
    em->seed = 9;
    ecs_modified(world, b, AudioEmitter);
    audio_run(world, 120);
    test_assert(audio_tap_count >= AUDIO_RATE * 2);
    memcpy(out, audio_tap_l, sizeof(float) * AUDIO_RATE * 2);
    memcpy(out + AUDIO_RATE * 2, audio_tap_r, sizeof(float) * AUDIO_RATE * 2);
    ecs_fini(world);
}

void Emitter_deterministic(void) {
    // audio/emitter
    float *first = malloc(sizeof(float) * AUDIO_RATE * 4);
    float *second = malloc(sizeof(float) * AUDIO_RATE * 4);
    int32_t i, same = 0;
    emitter_render_scene(first);
    emitter_render_scene(second);
    test_assert(audio_peak(first, 0, AUDIO_RATE * 4) > 0.01f);
    test_assert(memcmp(first, second, sizeof(float) * AUDIO_RATE * 4) == 0);
    free(first);
    free(second);

    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t a = emitter_new(world, dc, 0.05f, 0.2f);
    ecs_entity_t b = emitter_new(world, dc, 0.05f, 0.2f);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, b, Position, {0, 0, 0});
    emitter_edit(world, a)->shape = AudioEmitterSphere;
    emitter_edit(world, a)->radius = 5;
    ecs_modified(world, a, AudioEmitter);
    emitter_edit(world, b)->shape = AudioEmitterSphere;
    emitter_edit(world, b)->radius = 5;
    ecs_modified(world, b, AudioEmitter);
    emitter_log(world, &emitter_log_a, a);
    emitter_log(world, &emitter_log_b, b);
    emitter_run(world, 120, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_assert(emitter_log_a.count >= 10);
    test_assert(emitter_log_b.count >= 10);
    for (i = 0; i < 10; i ++) {
        same += emitter_log_a.plays[i].when == emitter_log_b.plays[i].when;
        test_assert(emitter_distance(&emitter_log_a.plays[i], emitter_log_b.plays[i].position[0],
            emitter_log_b.plays[i].position[1], emitter_log_b.plays[i].position[2]) > 1e-4f);
    }

    test_assert(same < 10);
    ecs_fini(world);
}

void Emitter_out_of_range_skipped(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, 100);
    ecs_entity_t e = emitter_new(world, dc, 0.1f, 0.1f);
    int64_t out_of_range = box_audio_module_stats(world).out_of_range;
    int32_t i;
    emitter_listener(world, 0, 0, 0);
    ecs_set(world, e, Position, {100, 0, 0});
    ecs_set(world, e, AudioSpatial, {1, 50, 1});
    emitter_log(world, &emitter_log_a, e);
    for (i = 0; i < 60; i ++) {
        emitter_run(world, 1, AUDIO_DT, &emitter_log_a, NULL);
        test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    }

    test_int(emitter_log_a.count, 0);
    test_int(emitter_state(world, e)->plays, 0);
    test_assert(emitter_state(world, e)->skipped >= 9);
    test_int(box_audio_module_stats(world).out_of_range - out_of_range,
        emitter_state(world, e)->skipped);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) == 0.0f);
    test_assert(audio_peak(audio_tap_r, 0, audio_tap_count) == 0.0f);

    ecs_set(world, e, Position, {5, 0, 0});
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 9);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_uint(emitter_log_a.plays[i].when % (AUDIO_RATE / 10), 0);
    }

    test_assert(audio_peak(audio_tap_r, 0, audio_tap_count) > 0.0f);
    ecs_fini(world);
}

void Emitter_max_active(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.1f, AUDIO_RATE / 2);
    ecs_entity_t e = emitter_new(world, dc, 0.05f, 0.05f);
    int32_t i, most = 0;
    emitter_edit(world, e)->max_active = 2;
    ecs_modified(world, e, AudioEmitter);
    for (i = 0; i < 180; i ++) {
        audio_run(world, 1);
        int32_t voices = ecs_singleton_get(world, AudioStatus)->voices;
        test_assert(voices <= 2);
        test_assert(emitter_state(world, e)->active <= 2);
        most = voices > most ? voices : most;
    }

    test_int(most, 2);
    test_assert(emitter_state(world, e)->skipped > 0);
    test_assert(emitter_state(world, e)->plays >= 6);

    emitter_edit(world, e)->max_active = 0;
    ecs_modified(world, e, AudioEmitter);
    audio_run(world, 60);
    test_assert(ecs_singleton_get(world, AudioStatus)->voices >= 8);
    ecs_fini(world);
}

void Emitter_playing_false(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE * 3 / 10);
    ecs_entity_t e = emitter_new(world, dc, 0.1f, 0.1f);
    int32_t count;
    uint64_t clock;
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 3);
    test_assert(ecs_singleton_get(world, AudioStatus)->voices >= 1);

    emitter_edit(world, e)->playing = false;
    ecs_modified(world, e, AudioEmitter);
    count = emitter_log_a.count;
    emitter_run(world, 2, AUDIO_DT, &emitter_log_a, NULL);
    test_int(emitter_log_a.count, count);

    test_assert(ecs_singleton_get(world, AudioStatus)->voices >= 1);
    test_assert(audio_level(AUDIO_FRAME) > 0.0f);
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_int(emitter_log_a.count, count);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(emitter_state(world, e)->active, 0);

    clock = box_audio_module_clock(world);
    emitter_edit(world, e)->playing = true;
    ecs_modified(world, e, AudioEmitter);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count > count);
    test_uint(emitter_log_a.plays[count].when, clock + AUDIO_RATE / 10);
    ecs_fini(world);
}

void Emitter_disable_delete(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE / 10);
    ecs_entity_t a = emitter_new(world, dc, 0.05f, 0.05f);
    ecs_entity_t b = emitter_new(world, dc, 0.05f, 0.05f);
    int32_t count_a, count_b;
    uint64_t clock;
    emitter_log(world, &emitter_log_a, a);
    emitter_log(world, &emitter_log_b, b);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_assert(emitter_log_a.count >= 5);
    test_assert(emitter_log_b.count >= 5);

    ecs_enable(world, a, false);
    ecs_delete(world, b);
    count_a = emitter_log_a.count;
    count_b = emitter_log_b.count;
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_int(emitter_log_a.count, count_a);
    test_int(emitter_log_b.count, count_b);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    clock = box_audio_module_clock(world);
    ecs_enable(world, a, true);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, &emitter_log_b);
    test_assert(emitter_log_a.count > count_a);
    test_uint(emitter_log_a.plays[count_a].when, clock + AUDIO_RATE / 20);
    test_int(emitter_log_b.count, count_b);
    ecs_fini(world);
}

void Emitter_sound_not_ready(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);
    ecs_entity_t file = ecs_entity(world, { .name = "drip" });
    ecs_entity_t e;
    audio_log_start();
    ecs_set(world, file, SoundFile, {
        .path = ECS_CONST_CAST(char*, "test/assets/audio/missing.wav") });
    e = emitter_new(world, file, 0.05f, 0.05f);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, NULL);
    audio_log_stop();
    test_int(ecs_get(world, file, SoundStatus)->state, SoundFailed);
    test_int(emitter_log_a.count, 0);
    test_int(emitter_state(world, e)->plays, 0);
    test_assert(emitter_state(world, e)->skipped >= 8);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    emitter_edit(world, e)->sound = ecs_new(world);
    ecs_modified(world, e, AudioEmitter);
    int64_t skipped = emitter_state(world, e)->skipped;
    emitter_run(world, 10, AUDIO_DT, &emitter_log_a, NULL);
    test_int(emitter_log_a.count, 0);
    test_assert(emitter_state(world, e)->skipped > skipped);

    ecs_set(world, file, SoundFile, {
        .path = ECS_CONST_CAST(char*, "test/assets/audio/sine_16.wav") });
    emitter_edit(world, e)->sound = file;
    ecs_modified(world, e, AudioEmitter);
    emitter_run(world, 30, AUDIO_DT, &emitter_log_a, NULL);
    test_int(ecs_get(world, file, SoundStatus)->state, SoundReady);
    test_assert(emitter_log_a.count >= 5);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) > 0.1f);
    ecs_fini(world);
}

void Emitter_output_override(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t muted = audio_bus(world, "muted", 0, 0.0f);
    ecs_entity_t loud = audio_bus(world, "loud", 0, 1.0f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE * 3 / 10);
    ecs_entity_t e = emitter_new(world, dc, 0.1f, 0.1f);
    int32_t i, count, start;
    ecs_set(world, dc, AudioOutput, { .bus = muted });
    emitter_edit(world, e)->max_active = 1;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);

    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 2);
    test_assert(box_audio_module_bus_index(world, muted) != 0);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_int(emitter_log_a.plays[i].bus, box_audio_module_bus_index(world, muted));
    }

    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) == 0.0f);

    ecs_set(world, e, AudioOutput, { .bus = loud });
    count = emitter_log_a.count;
    start = audio_tap_count;
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= count + 2);
    test_assert(box_audio_module_bus_index(world, loud) != 0);
    for (i = count; i < emitter_log_a.count; i ++) {
        test_int(emitter_log_a.plays[i].bus, box_audio_module_bus_index(world, loud));
    }

    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.45f);

    ecs_set(world, e, AudioOutput, { .bus = 0 });
    count = emitter_log_a.count;
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= count + 2);
    for (i = count; i < emitter_log_a.count; i ++) {
        test_int(emitter_log_a.plays[i].bus, 0);
    }

    ecs_remove(world, e, AudioOutput);
    count = emitter_log_a.count;
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= count + 2);
    for (i = count; i < emitter_log_a.count; i ++) {
        test_int(emitter_log_a.plays[i].bus, box_audio_module_bus_index(world, muted));
    }

    ecs_fini(world);
}

void Emitter_invalid_values(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, 48);
    ecs_entity_t e = emitter_new(world, dc, -1.0f, 0.0f);
    AudioEmitter *em;
    int32_t i;

    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 95);
    test_assert(emitter_log_a.count <= 101);
    emitter_expect_spacing(&emitter_log_a, AUDIO_RATE / 100, AUDIO_RATE / 100);

    em = emitter_edit(world, e);
    em->interval_min = 0;
    em->interval_max = 0;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 60, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count <= 101);
    emitter_expect_spacing(&emitter_log_a, AUDIO_RATE / 100, AUDIO_RATE / 100);

    em = emitter_edit(world, e);
    em->interval_min = 0.2f;
    em->interval_max = 0.05f;
    em->volume_min = -1.0f;
    em->volume_max = -0.5f;
    em->pitch_min = -2.0f;
    em->pitch_max = -1.0f;
    em->max_active = -3;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 300, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 20);
    emitter_expect_spacing(&emitter_log_a, AUDIO_RATE / 20, AUDIO_RATE / 5);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_flt(emitter_log_a.plays[i].volume, 0.0f);
        test_flt(emitter_log_a.plays[i].pitch, BOX_AUDIO_PITCH_MIN);
    }

    em = emitter_edit(world, e);
    em->volume_min = 1.0f;
    em->volume_max = 100.0f;
    em->pitch_min = NAN;
    em->pitch_max = 1000.0f;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 120, AUDIO_DT, &emitter_log_a, NULL);
    test_assert(emitter_log_a.count >= 5);
    for (i = 0; i < emitter_log_a.count; i ++) {
        test_assert(emitter_log_a.plays[i].volume >= 1.0f);
        test_assert(emitter_log_a.plays[i].volume <= BOX_AUDIO_VOLUME_MAX);
        test_assert(emitter_log_a.plays[i].pitch >= 1.0f);
        test_assert(emitter_log_a.plays[i].pitch <= BOX_AUDIO_PITCH_MAX);
    }

    test_assert(ecs_singleton_get(world, AudioStatus)->voices < BOX_AUDIO_VOICE_MAX);
    ecs_fini(world);
}

void Emitter_nonspatial(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE * 2);
    ecs_entity_t e = emitter_new(world, dc, 0.5f, 0.5f);
    emitter_listener(world, 1000, 0, 0);
    ecs_set(world, dc, AudioSpatial, {1, 50, 1});
    ecs_set(world, e, AudioSpatial, {1, 50, 1});
    emitter_edit(world, e)->max_active = 1;
    emitter_edit(world, e)->shape = AudioEmitterSphere;
    emitter_edit(world, e)->radius = 10;
    ecs_modified(world, e, AudioEmitter);
    emitter_log(world, &emitter_log_a, e);
    emitter_run(world, 45, AUDIO_DT, &emitter_log_a, NULL);
    test_int(emitter_log_a.count, 1);
    test_bool(emitter_log_a.plays[0].spatial, false);
    test_flt(audio_level(AUDIO_FRAME / 2), 0.5f);
    test_flt(audio_level_right(AUDIO_FRAME / 2), 0.5f);
    test_int(emitter_state(world, e)->skipped, 0);
    ecs_fini(world);
}

void Emitter_script(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);
    char *code = audio_read("test/assets/audio/emitter.flecs");
    const char *names[] = { "cricket", "meadow", "canopy", "rain", "cellar" };
    const AudioEmitterShape shapes[] = { AudioEmitterPoint, AudioEmitterDisc,
        AudioEmitterSphere, AudioEmitterHemisphere, AudioEmitterBox };
    int32_t i;
    test_assert(ecs_script_run(world, "emitter", code, NULL) == 0);
    ecs_os_free(code);

    for (i = 0; i < 5; i ++) {
        ecs_entity_t e = ecs_lookup(world, names[i]);
        test_assert(e != 0);
        test_assert(ecs_get(world, e, AudioEmitter) != NULL);
        test_int(ecs_get(world, e, AudioEmitter)->shape, shapes[i]);
        test_bool(ecs_get(world, e, AudioEmitter)->playing, true);
    }

    test_flt(ecs_get(world, ecs_lookup(world, "cellar"), AudioEmitter)->extents[2], 3);
    test_flt(ecs_get(world, ecs_lookup(world, "cricket"), AudioEmitter)->volume_max, 1);
    audio_run(world, 180);
    test_int(ecs_get(world, ecs_lookup(world, "drip"), SoundStatus)->state, SoundReady);
    for (i = 0; i < 5; i ++) {
        const AudioEmitterState *s = emitter_state(world, ecs_lookup(world, names[i]));
        if (s->plays == 0) {
            printf("%s: no plays, %lld skipped\n", names[i], (long long)s->skipped);
        }

        test_assert(s->plays > 0);
    }

    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) > 0.01f);
    test_assert(audio_peak(audio_tap_r, 0, audio_tap_count) > 0.01f);
    ecs_fini(world);
}

void Emitter_audio_command(void) {
    // audio/emitter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t dc = audio_dc(world, "drip", 0.5f, 100);
    ecs_entity_t e = emitter_new(world, dc, 0.1f, 0.1f);
    ecs_entity_t far_ = emitter_new(world, dc, 0.1f, 0.1f);
    char line[256];
    bool error = true;
    ecs_set_name(world, e, "rain");
    ecs_set_name(world, far_, "far");
    emitter_listener(world, 0, 0, 0);
    ecs_set(world, far_, Position, {100, 0, 0});
    emitter_edit(world, e)->shape = AudioEmitterSphere;
    ecs_modified(world, e, AudioEmitter);
    audio_run(world, 30);

    char *out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    snprintf(line, sizeof(line), "\n  emitter rain: sound drip, sphere, every 0.100 to 0.100 s, "
        "%lld plays, 0 skipped", (long long)emitter_state(world, e)->plays);
    test_assert(strstr(out, line) != NULL);
    snprintf(line, sizeof(line), "\n  emitter far: sound drip, point, every 0.100 to 0.100 s, "
        "0 plays, %lld skipped", (long long)emitter_state(world, far_)->skipped);
    test_assert(strstr(out, line) != NULL);
    test_assert(emitter_state(world, e)->plays >= 4);
    test_assert(emitter_state(world, far_)->skipped >= 4);
    ecs_os_free(out);

    ecs_enable(world, e, false);
    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(strstr(out, ", disabled") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}
