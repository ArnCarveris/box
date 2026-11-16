/**
 * @file audio/emitter.c
 * @brief AudioEmitter: one shots at random intervals, at random points of a shape, with random
 * volumes and pitches.
 *
 * Each emitter keeps the frame of the core clock of its next play. The plays before the end of
 * the frame the output renders next go out with their frame through box_audio_play_send, so
 * their spacing does not depend on the frame rate.
 *
 * A play draws from a hash of the seed, the entity and the count of plays, so fixed deltas give
 * bit identical output. The voice end events count the plays that sound, for max_active.
 */

#include "module.h"
#include <math.h>

ECS_COMPONENT_DECLARE(AudioEmitterState);

/** Longest interval in seconds, so its frames stay far inside 64 bits */
#define BOX_AUDIO_EMITTER_INTERVAL_MAX (86400.0)

/** Plays one emitter sends in one frame at most; more due in a frame are skipped */
#define BOX_AUDIO_EMITTER_FRAME_MAX (128)

/** The draws of a play, which each take a number of their own from the hash */
#define BOX_AUDIO_EMITTER_DRAW_INTERVAL 0
#define BOX_AUDIO_EMITTER_DRAW_POINT 1
#define BOX_AUDIO_EMITTER_DRAW_VOLUME 4
#define BOX_AUDIO_EMITTER_DRAW_PITCH 5
#define BOX_AUDIO_EMITTER_DRAW_START 8

/** Pi, which math.h of a strict C11 build does not define */
#define BOX_AUDIO_EMITTER_PI 3.14159265358979323846

/** Mix the bits of a value, the finalizer of splitmix64.
 *
 * @param x The value
 * @return The mixed value
 */
static uint64_t box_audio_emitter_mix(
    uint64_t x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;

    return x;
}

/** A uniform number of a draw of a play of an emitter.
 *
 * @param seed AudioEmitter.seed
 * @param e The emitter entity
 * @param count Count of the play
 * @param draw Which number of the play
 * @return The number, in [0, 1)
 */
static double box_audio_emitter_uniform(
    uint32_t seed,
    ecs_entity_t e,
    int64_t count,
    uint32_t draw)
{
    uint64_t h = box_audio_emitter_mix((uint64_t)seed + 0x9E3779B97F4A7C15ull);
    h = box_audio_emitter_mix(h ^ (uint64_t)e);
    h = box_audio_emitter_mix(h ^ (uint64_t)count);
    h = box_audio_emitter_mix(h + ((uint64_t)draw + 1) * 0x9E3779B97F4A7C15ull);

    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

/** Clamp a value, a NaN to a value of its own.
 *
 * @param value The value
 * @param lo Low end
 * @param hi High end
 * @param nan Value of a NaN
 * @return The clamped value
 */
static double box_audio_emitter_clamp(
    double value,
    double lo,
    double hi,
    double nan)
{
    double result = value;
    if (result != result) {
        result = nan;
    } else if (result < lo) {
        result = lo;
    } else if (result > hi) {
        result = hi;
    }

    return result;
}

/** Draw a value of a range whose bounds are clamped and swapped when the upper one is below
 * the lower one.
 *
 * @param min Lower bound
 * @param max Upper bound
 * @param lo Lowest value
 * @param hi Highest value
 * @param nan Value of a NaN bound
 * @param u Uniform number in [0, 1)
 * @return The value
 */
static double box_audio_emitter_draw(
    float min,
    float max,
    double lo,
    double hi,
    double nan,
    double u)
{
    double a = box_audio_emitter_clamp(min, lo, hi, nan);
    double b = box_audio_emitter_clamp(max, lo, hi, nan);
    double t;
    if (b < a) {
        t = a;
        a = b;
        b = t;
    }

    return a + (b - a) * u;
}

/** Frames of an interval of an emitter.
 *
 * @param em The emitter
 * @param e The emitter entity
 * @param count Count of the play the interval leads to
 * @param draw Which number of the play
 * @param rate Frames per second
 * @return The frames, at least 1
 */
static uint64_t box_audio_emitter_interval(
    const AudioEmitter *em,
    ecs_entity_t e,
    int64_t count,
    uint32_t draw,
    double rate)
{
    double seconds = box_audio_emitter_draw(em->interval_min, em->interval_max,
        BOX_AUDIO_EMITTER_INTERVAL_MIN, BOX_AUDIO_EMITTER_INTERVAL_MAX,
        BOX_AUDIO_EMITTER_INTERVAL_MIN, box_audio_emitter_uniform(em->seed, e, count, draw));
    double frames = floor(seconds * rate + 0.5);

    return frames >= 1 ? (uint64_t)frames : 1;
}

void box_audio_emitter_point(
    AudioEmitterShape shape,
    float radius,
    const vec3 extents,
    const double u[3],
    vec3 out)
{
    double r = radius == radius ? fabs((double)radius) : 0, rho, a, y, s;
    int32_t i;
    out[0] = out[1] = out[2] = 0;
    switch (shape) {
    case AudioEmitterDisc:
        rho = r * sqrt(u[0]);
        a = 2.0 * BOX_AUDIO_EMITTER_PI * u[1];
        out[0] = (float)(rho * cos(a));
        out[2] = (float)(rho * sin(a));
        break;
    case AudioEmitterSphere:
    case AudioEmitterHemisphere:
        rho = r * cbrt(u[0]);
        y = 1.0 - 2.0 * u[1];
        y = shape == AudioEmitterHemisphere ? fabs(y) : y;
        s = sqrt(1.0 - y * y > 0 ? 1.0 - y * y : 0);
        a = 2.0 * BOX_AUDIO_EMITTER_PI * u[2];
        out[0] = (float)(rho * s * cos(a));
        out[1] = (float)(rho * y);
        out[2] = (float)(rho * s * sin(a));
        break;
    case AudioEmitterBox:
        for (i = 0; i < 3; i ++) {
            double e = extents[i] == extents[i] ? fabs((double)extents[i]) : 0;
            out[i] = (float)(e * (2.0 * u[i] - 1.0));
        }

        break;
    case AudioEmitterPoint:
    default:
        break;
    }
}

void box_audio_oneshot_forget(
    ecs_world_t *world,
    const box_audio_module_t *state,
    const box_audio_oneshot_t *shot)
{
    AudioEmitterState *es;
    if (!shot->emitter || !ecs_is_alive(world, shot->emitter)) {
        goto done;
    }

    es = ecs_get_mut(world, shot->emitter, AudioEmitterState);
    if (es && es->serial == state->serial && es->active > 0) {
        es->active --;
    }

done:
    return;
}

/** Check whether an entity is a sound an emitter can play.
 *
 * @param world The world
 * @param sound The entity
 * @return Whether it is alive and has AudioPcm, SoundFile or SynthSound
 */
static bool box_audio_emitter_sound(
    ecs_world_t *world,
    ecs_entity_t sound)
{
    return sound && ecs_is_alive(world, sound) && (ecs_has(world, sound, AudioPcm) ||
        ecs_has(world, sound, SoundFile) || ecs_has(world, sound, SynthSound));
}

/** Send one play of an emitter, or skip it.
 *
 * @param world The world
 * @param state The state
 * @param e The emitter entity
 * @param em The emitter
 * @param es Its state
 * @param wt Its world transform, NULL for a play that is not spatial
 * @param out Its AudioOutput, NULL to play on the one of the sound
 * @param spatial Its AudioSpatial, NULL for the one of the sound
 * @param count Count of the play
 * @param when Frame of the core clock of the play
 */
static void box_audio_emitter_play(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    const AudioEmitter *em,
    AudioEmitterState *es,
    const WorldTransform *wt,
    const AudioOutput *out,
    const AudioSpatial *spatial,
    int64_t count,
    uint64_t when)
{
    box_audio_play_t play = {0};
    double u[3];
    vec3 local;
    uint32_t handle;
    int32_t i;
    if (em->max_active > 0 && es->active >= em->max_active) {
        goto skip;
    }

    if (!box_audio_emitter_sound(world, em->sound)) {
        goto skip;
    }

    play.sound = em->sound;
    play.volume = (float)box_audio_emitter_draw(em->volume_min, em->volume_max, 0,
        BOX_AUDIO_VOLUME_MAX, 0, box_audio_emitter_uniform(em->seed, e, count,
            BOX_AUDIO_EMITTER_DRAW_VOLUME));
    play.pitch = (float)box_audio_emitter_draw(em->pitch_min, em->pitch_max,
        BOX_AUDIO_PITCH_MIN, BOX_AUDIO_PITCH_MAX, 1.0, box_audio_emitter_uniform(em->seed, e,
            count, BOX_AUDIO_EMITTER_DRAW_PITCH));
    play.routed = out != NULL;
    play.output = out ? out->bus : 0;
    play.emitter = e;
    if (wt) {
        for (i = 0; i < 3; i ++) {
            u[i] = box_audio_emitter_uniform(em->seed, e, count,
                BOX_AUDIO_EMITTER_DRAW_POINT + (uint32_t)i);
        }

        box_audio_emitter_point(em->shape, em->radius, em->extents, u, local);
        glm_mat4_mulv3(ECS_CONST_CAST(vec4*, wt->m), local, 1.0f, play.position);
        play.spatial = true;
        play.has_distances = spatial != NULL;
        if (spatial) {
            play.distances = *spatial;
        }
    }

    handle = box_audio_play_send(world, state, &play, false, when);
    if (!handle) {
        goto skip;
    }

    es->plays ++;
    es->active ++;
    state->stats.emitter_plays ++;
    goto done;
skip:
    es->skipped ++;
    state->stats.emitter_skipped ++;
done:
    return;
}

/** Start the timer of an emitter: its first play comes after a fresh interval.
 *
 * @param e The emitter entity
 * @param em The emitter
 * @param es Its state
 * @param clock Frame of the core clock now
 * @param rate Frames per second
 */
static void box_audio_emitter_start(
    ecs_entity_t e,
    const AudioEmitter *em,
    AudioEmitterState *es,
    uint64_t clock,
    double rate)
{
    es->restarts ++;
    es->next = clock + box_audio_emitter_interval(em, e, es->count,
        BOX_AUDIO_EMITTER_DRAW_START + es->restarts, rate);
    es->running = true;
}

void box_audio_emitters_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting,
    float delta)
{
    double rate = t->sample_rate, window = delta > 0 ? (double)delta : 0;
    uint64_t clock, horizon, late, when;
    int32_t i, sent;
    int64_t count;
    ecs_iter_t it;
    if (!state->emitters || !(rate > 0) || waiting) {
        goto done;
    }

    clock = t->clock(t);
    if (t->kind != BOX_AUDIO_TRANSPORT_NULL) {
        window = (window < BOX_AUDIO_EMITTER_LATE_MAX ? window : BOX_AUDIO_EMITTER_LATE_MAX) +
            BOX_AUDIO_EMITTER_LOOKAHEAD;
    }

    horizon = clock + (uint64_t)ceil(window * rate) + 1;
    late = (uint64_t)ceil(BOX_AUDIO_EMITTER_LATE_MAX * rate);
    it = ecs_query_iter(world, state->emitters);
    while (ecs_query_next(&it)) {
        const AudioEmitter *em = ecs_field(&it, AudioEmitter, 0);
        AudioEmitterState *es = ecs_field(&it, AudioEmitterState, 1);
        const WorldTransform *wt = ecs_field(&it, WorldTransform, 2);
        const AudioOutput *out = ecs_field(&it, AudioOutput, 3);
        const AudioSpatial *spatial = ecs_field(&it, AudioSpatial, 4);
        bool disabled = ecs_table_has_id(world, it.table, EcsDisabled);
        bool spread = box_transform_spread(world, it.table);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            AudioEmitterState *s = &es[i];
            WorldTransform buf;
            if (s->serial != state->serial) {
                s->serial = state->serial;
                s->active = 0;
                s->running = false;
            }

            if (disabled || !em[i].playing) {
                s->running = false;
                continue;
            }

            if (!s->running) {
                box_audio_emitter_start(e, &em[i], s, clock, rate);
            }

            if (s->next >= horizon) {
                continue;
            }

            for (sent = 0; s->next < horizon; sent ++) {
                when = s->next;
                count = s->count ++;
                s->next += box_audio_emitter_interval(&em[i], e, s->count,
                    BOX_AUDIO_EMITTER_DRAW_INTERVAL, rate);
                if (sent >= BOX_AUDIO_EMITTER_FRAME_MAX || when + late < clock) {
                    s->skipped ++;
                    state->stats.emitter_skipped ++;
                    continue;
                }

                box_audio_emitter_play(world, state, e, &em[i], s,
                    spread ? box_transform_world(world, e, &buf) : wt ? &wt[i] : NULL,
                    out ? &out[i] : NULL, spatial ? &spatial[i] : NULL, count,
                    when ? when : 1);
            }
        }
    }

done:
    return;
}

void box_audio_emitter_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    state->emitters = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Emitters" }),
        .terms = {
            { .id = ecs_id(AudioEmitter), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioEmitterState), .src.id = EcsSelf },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(AudioOutput), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(AudioSpatial), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });
}
