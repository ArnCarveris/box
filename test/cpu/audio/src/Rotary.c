/**
 * @file Rotary.c
 * @brief The rotary kind of effect in an insert slot of a bus, in offline renders: the rates
 * at which the horn and the drum modulate a tone above and below the crossover, the gradual
 * acceleration after a step of the speed, the opposite phase of the modulation of the left
 * and the right mic, the dry mix, the silence rule, bit identical renders, and the Rotary
 * component in a world.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Frames of the longest render of a case, nine seconds at 48 kHz */
#define RT_MAX (AUDIO_RATE * 9)

/** Level of the noise and the tones */
#define RT_LEVEL 0.25f

/** Volume of the bus of the effect, so the limiter of the master never acts */
#define RT_VOLUME 0.1f

/** Most envelope frames and crossings of a case */
#define RT_ENVELOPE (RT_MAX / 48)
#define RT_CROSSINGS 256

/** Output of the last render, and of a second render that a case compares */
static float rt_l[RT_MAX];
static float rt_r[RT_MAX];
static float rt_b_l[RT_MAX];
static float rt_b_r[RT_MAX];

/** A sound of a case: noise or a tone */
static float rt_sound[RT_MAX];

/** Envelopes of the left and the right channel */
static double rt_env_l[RT_ENVELOPE];
static double rt_env_r[RT_ENVELOPE];

/** Times of the crossings of an envelope in seconds */
static double rt_times[RT_CROSSINGS];

/** A core with a rotary on bus 1, which outputs to the master */
typedef struct rt_t {
    box_audio_core_t *core;  /**< The core */
    float *memory;           /**< Memory of slot 0 */
} rt_t;

/** The parameters of a rotary at the defaults of the component, at a speed.
 *
 * @param speed 0..1
 * @return The parameters
 */
static box_audio_rotary_params_t rt_params(
    float speed)
{
    return (box_audio_rotary_params_t){ .speed = speed, .horn_slow = 0.8f,
        .horn_fast = 6.7f, .drum_slow = 0.7f, .drum_fast = 5.9f, .acceleration = 1.0f,
        .balance = 0, .spread = 1.0f, .drive = 0, .mix = 1.0f };
}

/** Send a rotary command into insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take the effect off
 * @param p Parameters
 */
static void rt_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t bus,
    box_audio_rotary_params_t p)
{
    uint8_t bytes[128];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, BOX_AUDIO_EFFECT_ROTARY, bus, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Fill rt_sound with white noise of a seed, the same on every run.
 *
 * @param frames Frames
 * @param seed Seed
 */
static void rt_noise(
    int32_t frames,
    uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 1;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        x = x * 1664525u + 1013904223u;
        rt_sound[i] = ((float)(x >> 8) / 8388608.0f - 1.0f) * RT_LEVEL;
    }
}

/** Create a core with bus 1 into the master at RT_VOLUME, memory for slot 0 of a rotary, and
 * rt_sound of some frames as sound 1.
 *
 * @param rt Receives the core
 * @param frames Frames of the sound
 */
static void rt_open(
    rt_t *rt,
    int32_t frames)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_ROTARY, AUDIO_RATE);
    rt->core = box_audio_core_new(&desc);
    test_assert(rt->core != NULL);
    test_assert(floats > 0);
    rt->memory = malloc((size_t)floats * sizeof(float));
    test_assert(rt->memory != NULL);

    // Memory that is not clear: the rotary never reads what it did not write
    audio_fill(rt->memory, floats, 0.75f);
    test_assert(box_audio_core_effect_memory(rt->core, 0, rt->memory, floats));
    audio_order(rt->core, routes, 2);
    audio_bus_set(rt->core, 1, BOX_AUDIO_PARAM_VOLUME, RT_VOLUME);
    test_assert(box_audio_core_sound(rt->core, 1, rt_sound, frames, 1, AUDIO_RATE));
}

/** Free the core, then its memory.
 *
 * @param rt The core
 */
static void rt_close(
    rt_t *rt)
{
    box_audio_core_free(rt->core);
    free(rt->memory);
}

/** Render frames into two channels from a frame on, in renders of a size.
 *
 * @param core The core
 * @param l Left channel
 * @param r Right channel
 * @param at First frame
 * @param frames Number of frames
 * @param size Frames per render
 */
static void rt_render(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t at,
    int32_t frames,
    int32_t size)
{
    int32_t i;
    test_assert(at + frames <= RT_MAX);
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, l + at + i, r + at + i, n);
    }
}

/** Start a looping tone of a second on bus 1 of a new core.
 *
 * @param rt Receives the core
 * @param frequency Hz, a whole number of periods per second
 */
static void rt_tone(
    rt_t *rt,
    double frequency)
{
    audio_sine(rt_sound, AUDIO_RATE, frequency, RT_LEVEL);
    rt_open(rt, AUDIO_RATE);
}

/** Get the envelope of a channel: the RMS of windows of frames, from a frame on.
 *
 * @param x The channel
 * @param from First frame
 * @param to End frame
 * @param window Frames of a window, whole periods of the tone
 * @param env Receives the envelope
 * @return Windows
 */
static int32_t rt_envelope(
    const float *x,
    int32_t from,
    int32_t to,
    int32_t window,
    double *env)
{
    int32_t n = (to - from) / window, k, i;
    test_assert(n <= RT_ENVELOPE);
    for (k = 0; k < n; k ++) {
        double sum = 0;
        for (i = 0; i < window; i ++) {
            double v = (double)x[from + k * window + i];
            sum += v * v;
        }

        env[k] = sqrt(sum / (double)window);
    }

    return n;
}

/** Find the times at which an envelope rises through its mean, with a hysteresis of a quarter
 * of its swing: it must fall below the mean by that much before the next rise counts.
 *
 * @param env The envelope
 * @param n Windows
 * @param from Frame of the first window
 * @param window Frames of a window
 * @param times Receives the times in seconds, interpolated within a window
 * @return Number of rises
 */
static int32_t rt_crossings(
    const double *env,
    int32_t n,
    int32_t from,
    int32_t window,
    double *times)
{
    double mean = 0, lo = env[0], hi = env[0], h;
    int32_t k, count = 0;
    bool armed = false;
    for (k = 0; k < n; k ++) {
        mean += env[k];
        lo = env[k] < lo ? env[k] : lo;
        hi = env[k] > hi ? env[k] : hi;
    }

    mean /= (double)n;
    h = 0.25 * (hi - lo) * 0.5;
    test_assert(hi - lo > 0.1 * hi);
    for (k = 1; k < n; k ++) {
        if (env[k] < mean - h) {
            armed = true;
        } else if (armed && env[k - 1] < mean && env[k] >= mean && count < RT_CROSSINGS) {
            double frac = (mean - env[k - 1]) / (env[k] - env[k - 1]);
            times[count ++] = ((double)from + ((double)(k - 1) + frac + 0.5) *
                (double)window) / (double)AUDIO_RATE;
            armed = false;
        }
    }

    return count;
}

/** Get the rate at which an envelope rises through its mean on average.
 *
 * @param env The envelope
 * @param n Windows
 * @param from Frame of the first window
 * @param window Frames of a window
 * @return Hz
 */
static double rt_rate(
    const double *env,
    int32_t n,
    int32_t from,
    int32_t window)
{
    int32_t count = rt_crossings(env, n, from, window, rt_times);
    test_assert(count >= 3);

    return (double)(count - 1) / (rt_times[count - 1] - rt_times[0]);
}

/** Get the correlation of two envelopes, their means taken out.
 *
 * @param a An envelope
 * @param b An envelope
 * @param n Windows
 * @return -1..1
 */
static double rt_correlation(
    const double *a,
    const double *b,
    int32_t n)
{
    double ma = 0, mb = 0, ab = 0, aa = 0, bb = 0;
    int32_t k;
    for (k = 0; k < n; k ++) {
        ma += a[k];
        mb += b[k];
    }

    ma /= (double)n;
    mb /= (double)n;
    for (k = 0; k < n; k ++) {
        ab += (a[k] - ma) * (b[k] - mb);
        aa += (a[k] - ma) * (a[k] - ma);
        bb += (b[k] - mb) * (b[k] - mb);
    }

    return aa > 0 && bb > 0 ? ab / sqrt(aa * bb) : 0;
}

/** Render three seconds of a tone through a rotary at speed 1 and get the rate at which the
 * left channel swells over the last two.
 *
 * @param frequency Hz of the tone
 * @param window Frames of a window of the envelope, whole periods of the tone
 * @return Hz
 */
static double rt_fast_rate(
    double frequency,
    int32_t window)
{
    rt_t rt;
    int32_t n;
    rt_tone(&rt, frequency);
    rt_command(rt.core, 0, 1, rt_params(1));
    audio_play(rt.core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE * 3, BOX_AUDIO_CORE_QUANTUM);
    rt_close(&rt);
    n = rt_envelope(rt_l, AUDIO_RATE, AUDIO_RATE * 3, window, rt_env_l);

    return rt_rate(rt_env_l, n, AUDIO_RATE, window);
}

void Rotary_horn_rate(void) {
    // audio/rotary
    double rate = rt_fast_rate(3000, 48);
    test_assert(rate > 6.7 * 0.93 && rate < 6.7 * 1.07);
}

void Rotary_drum_rate(void) {
    // audio/rotary
    double rate = rt_fast_rate(200, 240);
    test_assert(rate > 5.9 * 0.93 && rate < 5.9 * 1.07);
}

/** Render a tone through a rotary at speed 0 for four seconds, then at speed 1 for five, and
 * check the rates at which the left channel swells around the step: slow before it, between
 * slow and fast in the first swell after it, within 10% of fast once the rotor settled, and
 * below 85% of fast up to a time after the step.
 *
 * @param frequency Hz of the tone
 * @param window Frames of a window of the envelope
 * @param slow Hz of the rotor at speed 0
 * @param fast Hz of the rotor at speed 1
 * @param below Seconds after the step up to which the rotor stays below 85% of fast
 * @param settled Seconds after the step from which the rotor is within 10% of fast
 */
static void rt_step(
    double frequency,
    int32_t window,
    double slow,
    double fast,
    double below,
    double settled)
{
    const double step = 4.0;
    rt_t rt;
    int32_t n, count, k, before = 0, after = 0, late = 0;
    bool first = true;
    rt_tone(&rt, frequency);
    rt_command(rt.core, 0, 1, rt_params(0));
    audio_play(rt.core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE * 4, BOX_AUDIO_CORE_QUANTUM);
    rt_command(rt.core, 0, 1, rt_params(1));
    rt_render(rt.core, rt_l, rt_r, AUDIO_RATE * 4, AUDIO_RATE * 5, BOX_AUDIO_CORE_QUANTUM);
    rt_close(&rt);
    n = rt_envelope(rt_l, AUDIO_RATE / 5, AUDIO_RATE * 9, window, rt_env_l);
    count = rt_crossings(rt_env_l, n, AUDIO_RATE / 5, window, rt_times);
    for (k = 0; k + 1 < count; k ++) {
        double start = rt_times[k], end = rt_times[k + 1], mid = 0.5 * (start + end);
        double rate = 1.0 / (end - start);
        if (end < step) {
            // Slow before the step
            test_assert(rate > slow * 0.8 && rate < slow * 1.2);
            before ++;
        } else if (start >= step && first) {
            // The first swell after the step is on its way, not at fast yet
            test_assert(rate > slow * 1.2 && rate < fast * 0.9);
            first = false;
            after ++;
        } else if (mid > step && mid < step + below) {
            test_assert(rate < fast * 0.85);
        } else if (mid > step + settled) {
            test_assert(rate > fast * 0.9 && rate < fast * 1.1);
            late ++;
        }
    }

    test_assert(before >= 1);
    test_assert(after == 1);
    test_assert(late >= 3);
}

void Rotary_acceleration_horn(void) {
    // audio/rotary
    rt_step(3000, 48, 0.8, 6.7, 0.3, 2.0);
}

void Rotary_acceleration_drum(void) {
    // audio/rotary: the drum takes three times as long as the horn
    rt_step(200, 240, 0.7, 5.9, 1.2, 3.5);
}

/** Render three seconds of a tone through a rotary at speed 1 and a spread.
 *
 * @param frequency Hz of the tone
 * @param spread 0..1
 */
static void rt_stereo(
    double frequency,
    float spread)
{
    box_audio_rotary_params_t p = rt_params(1);
    rt_t rt;
    p.spread = spread;
    rt_tone(&rt, frequency);
    rt_command(rt.core, 0, 1, p);
    audio_play(rt.core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE * 3, BOX_AUDIO_CORE_QUANTUM);
    rt_close(&rt);
}

void Rotary_stereo_phase(void) {
    // audio/rotary
    int32_t n, i;

    // The mics half a turn apart: the swells of the left and the right channel alternate
    rt_stereo(3000, 1);
    n = rt_envelope(rt_l, AUDIO_RATE, AUDIO_RATE * 3, 48, rt_env_l);
    rt_envelope(rt_r, AUDIO_RATE, AUDIO_RATE * 3, 48, rt_env_r);
    test_assert(rt_correlation(rt_env_l, rt_env_r, n) < -0.5);
    rt_stereo(200, 1);
    n = rt_envelope(rt_l, AUDIO_RATE, AUDIO_RATE * 3, 240, rt_env_l);
    rt_envelope(rt_r, AUDIO_RATE, AUDIO_RATE * 3, 240, rt_env_r);
    test_assert(rt_correlation(rt_env_l, rt_env_r, n) < -0.5);

    // Halfway, a quarter turn apart, they are out of phase but less
    rt_stereo(3000, 0.5f);
    n = rt_envelope(rt_l, AUDIO_RATE, AUDIO_RATE * 3, 48, rt_env_l);
    rt_envelope(rt_r, AUDIO_RATE, AUDIO_RATE * 3, 48, rt_env_r);
    test_assert(rt_correlation(rt_env_l, rt_env_r, n) < 0.5);
    test_assert(rt_correlation(rt_env_l, rt_env_r, n) > -0.5);

    // Both mics at the same place: a mono input stays mono
    rt_stereo(3000, 0);
    for (i = 0; i < AUDIO_RATE * 3; i ++) {
        test_assert(rt_l[i] == rt_r[i]);
    }

    test_assert(audio_peak(rt_l, AUDIO_RATE, AUDIO_RATE * 3) > 0.001f);
}

/** Render two seconds of a tone through a rotary on bus 1, or through no effect.
 *
 * @param p Parameters, NULL for no effect
 * @param l Receives the left channel
 * @param r Receives the right channel
 */
static void rt_dry(
    const box_audio_rotary_params_t *p,
    float *l,
    float *r)
{
    rt_t rt;
    audio_sine(rt_sound, AUDIO_RATE, 330, RT_LEVEL);
    rt_open(&rt, AUDIO_RATE);
    audio_play(rt.core, 1, 1, 1, 1, 0.3f, 1, BOX_AUDIO_PLAY_LOOP);
    if (p) {
        rt_command(rt.core, 0, 1, *p);
    }

    rt_render(rt.core, l, r, 0, AUDIO_RATE * 2, BOX_AUDIO_CORE_QUANTUM);
    rt_close(&rt);
}

void Rotary_mix_dry(void) {
    // audio/rotary
    box_audio_rotary_params_t p = rt_params(1);
    int32_t i;
    p.drive = 1;
    p.mix = 0;
    rt_dry(NULL, rt_l, rt_r);
    test_assert(audio_peak(rt_l, 0, AUDIO_RATE * 2) > 0.01f);
    rt_dry(&p, rt_b_l, rt_b_r);
    for (i = 0; i < AUDIO_RATE * 2; i ++) {
        test_assert(rt_l[i] == rt_b_l[i]);
        test_assert(rt_r[i] == rt_b_r[i]);
    }

    // Fully wet it changes the signal
    p.mix = 1;
    rt_dry(&p, rt_b_l, rt_b_r);
    for (i = 0; i < AUDIO_RATE * 2 && rt_l[i] == rt_b_l[i]; i ++) { }
    test_assert(i < AUDIO_RATE * 2);
}

void Rotary_silent_after_tail(void) {
    // audio/rotary
    box_audio_rotary_params_t p = rt_params(1);
    rt_t rt;
    p.drive = 0.5f;
    rt_noise(AUDIO_RATE / 4, 6);
    rt_open(&rt, AUDIO_RATE / 4);
    rt_command(rt.core, 0, 1, p);
    test_assert(!box_audio_core_effect_running(rt.core, 0));
    audio_play(rt.core, 1, 1, 1, 1, 0, 1, 0);
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
    test_assert(box_audio_core_effect_running(rt.core, 0));
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    test_assert(!box_audio_core_effect_running(rt.core, 0));
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    test_flt(audio_peak(rt_l, 0, AUDIO_RATE), 0);
    test_flt(audio_peak(rt_r, 0, AUDIO_RATE), 0);

    // A change of the speed while idle, then it starts again on the next sound
    rt_command(rt.core, 0, 1, rt_params(0));
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE / 2, BOX_AUDIO_CORE_QUANTUM);
    test_assert(!box_audio_core_effect_running(rt.core, 0));
    audio_play(rt.core, 2, 1, 1, 1, 0, 1, 0);
    rt_render(rt.core, rt_l, rt_r, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
    test_assert(box_audio_core_effect_running(rt.core, 0));
    test_assert(audio_peak(rt_l, 0, AUDIO_RATE / 8) > 0.001f);
    rt_close(&rt);
}

/** Render noise and a tone through a rotary on bus 1 whose speed, balance, spread, drive and
 * mix change, in renders of a size, and hash the output. The sounds loop, so it never idles.
 *
 * @param size Frames per render
 * @param drive Drive of the first command
 * @return The hash of both channels
 */
static uint64_t rt_sized(
    int32_t size,
    float drive)
{
    rt_t rt;
    int32_t k;
    rt_noise(AUDIO_RATE / 2, 7);
    audio_sine(rt_sound + AUDIO_RATE / 2, AUDIO_RATE / 2, 220, RT_LEVEL);
    rt_open(&rt, AUDIO_RATE);
    audio_play(rt.core, 1, 1, 1, 0.5f, -0.4f, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(rt.core, 2, 1, 1, 0.5f, 0.6f, 1.5f, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 8; k ++) {
        box_audio_rotary_params_t p = rt_params((float)(k % 3) * 0.5f);
        p.acceleration = 0.3f + 0.4f * (float)k;
        p.balance = -0.8f + 0.25f * (float)k;
        p.spread = 0.15f * (float)k;
        p.drive = k ? 0.12f * (float)k : drive;
        p.mix = 1.0f - 0.1f * (float)(k % 4);
        rt_command(rt.core, 0, 1, p);
        rt_render(rt.core, rt_l, rt_r, k * (AUDIO_RATE / 4), AUDIO_RATE / 4, size);
    }

    rt_close(&rt);

    return audio_hash(rt_l, AUDIO_RATE * 2) ^ (audio_hash(rt_r, AUDIO_RATE * 2) * 31);
}

void Rotary_render_identical(void) {
    // audio/rotary
    uint64_t a = rt_sized(BOX_AUDIO_CORE_QUANTUM, 0);
    uint64_t b = rt_sized(BOX_AUDIO_CORE_QUANTUM, 0);
    uint64_t c = rt_sized(BOX_AUDIO_CORE_QUANTUM, 0.5f), d = rt_sized(100, 0.5f);
    uint64_t e = rt_sized(1000, 0.5f), f = rt_sized(37, 0.5f);
    test_assert(a == b);
    test_assert(a != c);
    test_assert(c == d);
    test_assert(c == e);
    test_assert(c == f);
}

void Rotary_component(void) {
    // audio/rotary
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const Rotary *rt;
    float params[BOX_AUDIO_EFFECT_PARAMS];
    ecs_add(world, e, Rotary);
    rt = ecs_get(world, e, Rotary);
    test_flt(rt->speed, 0);
    test_flt(rt->horn_slow, 0.8f);
    test_flt(rt->horn_fast, 6.7f);
    test_flt(rt->drum_slow, 0.7f);
    test_flt(rt->drum_fast, 5.9f);
    test_flt(rt->acceleration, 1);
    test_flt(rt->balance, 0);
    test_flt(rt->spread, 1);
    test_flt(rt->drive, 0);
    test_flt(rt->mix, 1);
    test_assert(ecs_has(world, e, AudioEffectState));
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_ROTARY);
    test_flt(params[0], 0);
    test_flt(params[1], 0.8f);
    test_flt(params[2], 6.7f);
    test_flt(params[3], 0.7f);
    test_flt(params[4], 5.9f);
    test_flt(params[5], 1);
    test_flt(params[6], 0);
    test_flt(params[7], 1);
    test_flt(params[8], 0);
    test_flt(params[9], 1);
    test_flt(params[10], 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_ROTARY, AUDIO_RATE),
        ((int32_t)(BOX_AUDIO_ROTARY_MAX * 0.001f * AUDIO_RATE) + 2) * 2);
    ecs_fini(world);
}

void Rotary_script(void) {
    // audio/rotary
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/rotary.flecs");
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "rotary_scene" }),
        .code = code
    });
    ecs_entity_t organ, tone, source;
    const Rotary *rt;
    ecs_os_free(code);
    test_assert(script != 0);
    audio_run(world, 2);
    organ = ecs_lookup(world, "organ");
    test_assert(organ != 0);
    rt = ecs_get(world, organ, Rotary);
    test_assert(rt != NULL);
    test_flt(rt->speed, 1);
    test_flt(rt->drive, 0.3f);
    test_flt(rt->balance, 0.2f);
    test_flt(rt->horn_fast, 6.7f);
    test_assert(box_audio_module_effect_slot(world, organ) >= 0);

    // It runs on a tone and keeps it finite
    tone = audio_tone(world, "tone", 440, 0.25f, AUDIO_RATE);
    source = audio_source(world, tone, 1, true, organ);
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME * 10,
        audio_tap_count) > 0.01f);
    test_assert(audio_peak(audio_tap_r, audio_tap_count - AUDIO_FRAME * 10,
        audio_tap_count) > 0.01f);
    ecs_delete(world, source);
    ecs_fini(world);
}
