/**
 * @file Saturator.c
 * @brief The saturator, measured with sines in offline renders: its harmonics and DC, its level
 * across the drive, its aliasing against a clipper at the base rate, the bit crusher, the dry
 * mix, the reduction, changes of the drive, render sizes, its rest after a tail and the
 * component in a world.
 */

#include "world.h"
#include <saturate.h>
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds */
#define SA_FRAMES (AUDIO_RATE * 2)

/** Output of the last render */
static float sa_l[SA_FRAMES], sa_r[SA_FRAMES];

/** Output of a second render that a case compares */
static float sa_b_l[SA_FRAMES], sa_b_r[SA_FRAMES];

/** The input sound */
static float sa_sound[SA_FRAMES];

/** The frame of silence that a core renders before its sound plays */
static float sa_silence[2];

/** Linear level of a level in dB.
 *
 * @param db The level
 * @return The linear level
 */
static float sa_linear(
    double db)
{
    return (float)pow(10.0, db / 20.0);
}

/** Level in dB of a linear level.
 *
 * @param x The linear level
 * @return The level, -300 for silence
 */
static double sa_db(
    double x)
{
    x = x < 0 ? -x : x;

    return x > 1e-15 ? 20 * log10(x) : -300;
}

/** The parameters of a saturator.
 *
 * @param drive 0..1
 * @param asymmetry 0..1
 * @param bits 1..16
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_saturator_params_t sa_params(
    float drive,
    float asymmetry,
    float bits,
    float mix)
{
    return (box_audio_saturator_params_t){ drive, asymmetry, bits, mix };
}

/** Put a saturator into insert slot 0 of bus 1 as effect 0, or change it.
 *
 * @param core The core
 * @param p The parameters
 */
static void sa_command(
    box_audio_core_t *core,
    box_audio_saturator_params_t p)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_SATURATOR, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** A new core with bus 1 into the master.
 *
 * @return The core
 */
static box_audio_core_t* sa_core(void)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    audio_order(core, routes, 2);

    return core;
}

/** Render a frame of silence so that the effects put on before enter at once, and play
 * sa_sound on bus 1 as sound 1.
 *
 * @param core The core
 * @param frames Frames of sa_sound
 * @param loop Whether it loops
 */
static void sa_play(
    box_audio_core_t *core,
    int32_t frames,
    bool loop)
{
    box_audio_core_render(core, sa_silence, sa_silence + 1, 1);
    test_assert(box_audio_core_sound(core, 1, sa_sound, frames, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);
}

/** Render frames into a buffer from a frame on, in quanta of the web.
 *
 * @param core The core
 * @param l Left channel
 * @param r Right channel
 * @param at First frame
 * @param frames Number of frames
 */
static void sa_render(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t at,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - i < BOX_AUDIO_CORE_QUANTUM ? frames - i : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, l + at + i, r + at + i, n);
    }
}

/** Render a looping sine of a level through a saturator on bus 1, or without one.
 *
 * @param p The parameters, or NULL for no saturator
 * @param frequency Hz
 * @param level Peak level
 * @param frames Frames to render into sa_l and sa_r
 */
static void sa_tone(
    const box_audio_saturator_params_t *p,
    double frequency,
    float level,
    int32_t frames)
{
    box_audio_core_t *core = sa_core();
    if (p) {
        sa_command(core, *p);
    }

    audio_sine(sa_sound, AUDIO_RATE, frequency, level);
    sa_play(core, AUDIO_RATE, true);
    sa_render(core, sa_l, sa_r, 0, frames);
    box_audio_core_free(core);
}

/** Amplitude of a frequency in a range, from its DFT bin; the range holds whole periods.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames
 * @param frequency Hz
 * @return The amplitude
 */
static double sa_amplitude(
    const float *x,
    int32_t from,
    int32_t count,
    double frequency)
{
    double re = 0, im = 0, w = 2 * AUDIO_PI * frequency / AUDIO_RATE;
    int32_t i;
    for (i = 0; i < count; i ++) {
        re += (double)x[from + i] * cos(w * i);
        im -= (double)x[from + i] * sin(w * i);
    }

    return 2 * sqrt(re * re + im * im) / count;
}

/** Mean of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames
 * @return The mean
 */
static double sa_mean(
    const float *x,
    int32_t from,
    int32_t count)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        sum += (double)x[from + i];
    }

    return sum / count;
}

/** RMS of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames
 * @return The RMS
 */
static double sa_rms(
    const float *x,
    int32_t from,
    int32_t count)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        sum += (double)x[from + i] * (double)x[from + i];
    }

    return sqrt(sum / count);
}

/** Power of the aliases of a sine of 5 kHz at 48 kHz against its fundamental: the bins at
 * every multiple of 1 kHz up to 23 kHz but the harmonics at 5 and 15 kHz, where the
 * harmonics over the Nyquist frequency fold.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames, whole milliseconds
 * @return The power of the aliases over the power of the fundamental
 */
static double sa_aliases(
    const float *x,
    int32_t from,
    int32_t count)
{
    double sum = 0, f = sa_amplitude(x, from, count, 5000);
    int32_t k;
    for (k = 1; k <= 23; k ++) {
        if (k != 5 && k != 15) {
            double a = sa_amplitude(x, from, count, 1000.0 * k);
            sum += a * a;
        }
    }

    return sum / (f * f);
}

void Saturator_harmonics(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 2;
    box_audio_saturator_params_t p = sa_params(0.8f, 0, 16, 1);
    double h1, h2, h3, h5, dc;
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    h1 = sa_amplitude(sa_l, from, count, 1000);
    h2 = sa_amplitude(sa_l, from, count, 2000);
    h3 = sa_amplitude(sa_l, from, count, 3000);
    h5 = sa_amplitude(sa_l, from, count, 5000);
    dc = sa_mean(sa_l, from, count);
    printf("symmetric, drive 0.8: 2nd %.1f dB, 3rd %.1f dB, 5th %.1f dB under the fundamental, "
        "DC %.2e\n", sa_db(h2 / h1), sa_db(h3 / h1), sa_db(h5 / h1), dc);
    test_assert(h3 / h1 > 0.1);
    test_assert(h5 / h1 > 0.03);
    test_assert(h2 / h1 < 1e-3);
    test_assert(fabs(dc) < 1e-4);
    test_assert(fabs(sa_amplitude(sa_r, from, count, 3000) - h3) < 1e-6);

    p.asymmetry = 0.5f;
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    h1 = sa_amplitude(sa_l, from, count, 1000);
    h2 = sa_amplitude(sa_l, from, count, 2000);
    h3 = sa_amplitude(sa_l, from, count, 3000);
    dc = sa_mean(sa_l, from, count);
    printf("asymmetry 0.5: 2nd %.1f dB, 3rd %.1f dB under the fundamental, DC %.2e\n",
        sa_db(h2 / h1), sa_db(h3 / h1), dc);
    test_assert(h2 / h1 > 0.01);
    test_assert(h3 / h1 > 0.05);
    test_assert(fabs(dc) < 1e-4);
}

void Saturator_level_across_drive(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 5, count = AUDIO_RATE / 5;
    float level = sa_linear(-12);
    double dry = level / sqrt(2.0), lo = 1e9, hi = -1e9;
    int32_t d;
    for (d = 1; d <= 10; d ++) {
        box_audio_saturator_params_t p = sa_params(0.1f * (float)d, 0, 16, 1);
        double change;
        sa_tone(&p, 1000, level, from + count);
        change = sa_db(sa_rms(sa_l, from, count) / dry);
        printf("a -12 dBFS sine at drive %.1f: %+.3f dB\n", 0.1 * d, change);
        test_assert(fabs(change) < 1.5);
        lo = change < lo ? change : lo;
        hi = change > hi ? change : hi;
    }

    printf("RMS from %+.3f dB to %+.3f dB\n", lo, hi);
}

void Saturator_alias_oversampled(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 5, count = AUDIO_RATE / 5;
    box_audio_saturator_params_t p = sa_params(0.8f, 0, 16, 1);
    box_audio_saturate_curve_t curve;
    float level = sa_linear(-6);
    double over, base;
    int32_t i;
    sa_tone(&p, 5000, level, from + count);
    over = sa_aliases(sa_l, from, count);

    /* The same clipper at the base rate */
    box_audio_saturate_curve(&curve, (float)pow(10.0, 0.8 * 30 / 20), 0);
    audio_sine(sa_sound, from + count, 5000, level);
    for (i = 0; i < from + count; i ++) {
        sa_b_l[i] = box_audio_saturate_clip(&curve, sa_sound[i]) * curve.comp;
    }

    base = sa_aliases(sa_b_l, from, count);
    printf("aliases of a 5 kHz sine at drive 0.8: %.1f dB oversampled, %.1f dB at the base "
        "rate\n", 10 * log10(over), 10 * log10(base));
    test_assert(over < base * 0.5);
}

void Saturator_bits(void) {
    // audio/saturator
    box_audio_core_t *core = sa_core();
    float levels[64];
    int32_t count = 0, i, k;
    sa_command(core, sa_params(0, 0, 4, 1));
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_sine(sa_sound, AUDIO_RATE, 97, 3);
    sa_play(core, AUDIO_RATE, true);
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE);
    for (i = AUDIO_RATE / 5; i < AUDIO_RATE; i ++) {
        for (k = 0; k < count && levels[k] != sa_l[i]; k ++) {
        }

        if (k == count && count < 64) {
            levels[count ++] = sa_l[i];
        }
    }

    printf("4 bits: %d levels\n", count);
    test_int(count, 16);
    test_assert(audio_peak(sa_l, AUDIO_RATE / 5, AUDIO_RATE) <= 0.5f);
    box_audio_core_free(core);
}

void Saturator_mix_dry(void) {
    // audio/saturator
    box_audio_saturator_params_t p = sa_params(0.8f, 0.5f, 4, 0);
    const int32_t frames = AUDIO_RATE / 2, late = BOX_AUDIO_SATURATE_LATENCY;
    int32_t i;
    sa_tone(NULL, 440, 0.7f, frames);
    memcpy(sa_b_l, sa_l, sizeof(float) * frames);
    memcpy(sa_b_r, sa_r, sizeof(float) * frames);
    test_assert(audio_peak(sa_b_l, 0, frames) > 0.1f);
    sa_tone(&p, 440, 0.7f, frames);
    for (i = 0; i < late; i ++) {
        test_flt(sa_l[i], 0);
    }

    for (i = late; i < frames; i ++) {
        test_assert(sa_l[i] == sa_b_l[i - late]);
        test_assert(sa_r[i] == sa_b_r[i - late]);
    }
}

void Saturator_reduction(void) {
    // audio/saturator
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    box_audio_core_t *core = sa_core();
    sa_command(core, sa_params(1, 0, 16, 1));
    audio_sine(sa_sound, AUDIO_RATE, 1000, sa_linear(-6));
    sa_play(core, AUDIO_RATE, true);
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE / 10);
    test_int(box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX),
        BOX_AUDIO_EFFECT_MAX);
    printf("reduction of a -6 dBFS sine at drive 1: %.2f dB\n", (double)meters[0].reduction);
    test_assert(meters[0].reduction > 10);
    test_flt(meters[1].reduction, 0);
    box_audio_core_free(core);

    /* A quiet sine at no drive passes the clipper all but linearly */
    core = sa_core();
    sa_command(core, sa_params(0, 0, 16, 1));
    audio_sine(sa_sound, AUDIO_RATE, 1000, sa_linear(-60));
    sa_play(core, AUDIO_RATE, true);
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE / 10);
    box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    test_flt(meters[0].reduction, 0);
    box_audio_core_free(core);
}

void Saturator_drive_no_click(void) {
    // audio/saturator
    const int32_t seg = AUDIO_RATE / 4, ramp = AUDIO_RATE / 20;
    box_audio_core_t *core = sa_core();
    float low, high, up, down;
    sa_command(core, sa_params(0.1f, 0, 16, 1));
    audio_sine(sa_sound, AUDIO_RATE, 220, sa_linear(-12));
    sa_play(core, AUDIO_RATE, true);
    sa_render(core, sa_l, sa_r, 0, seg);
    sa_command(core, sa_params(1, 0, 16, 1));
    sa_render(core, sa_l, sa_r, seg, seg);
    sa_command(core, sa_params(0.1f, 0, 16, 1));
    sa_render(core, sa_l, sa_r, 2 * seg, seg);
    low = audio_max_step(sa_l, seg / 2, seg);
    high = audio_max_step(sa_l, seg + ramp, 2 * seg);
    up = audio_max_step(sa_l, seg, seg + ramp);
    down = audio_max_step(sa_l, 2 * seg, 2 * seg + ramp);
    printf("largest step: %.5f at drive 0.1, %.5f at drive 1, %.5f up, %.5f down\n",
        (double)low, (double)high, (double)up, (double)down);
    test_assert(up < high * 1.1f);
    test_assert(down < high * 1.1f);
    test_assert(low < high);
    box_audio_core_free(core);
}

/** Render a saturator on a looping sine in pieces of a size, with a change of its params
 * halfway.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param size Frames of each render, 0 for sizes that vary
 */
static void sa_sizes(
    float *left,
    float *right,
    int32_t size)
{
    box_audio_core_t *core = sa_core();
    int32_t at = 0, k = 0;
    sa_command(core, sa_params(0.7f, 0.3f, 10, 0.7f));
    audio_sine(sa_sound, AUDIO_RATE, 220, 0.4f);
    sa_play(core, AUDIO_RATE, true);
    while (at < AUDIO_RATE) {
        int32_t n = size ? size : 1 + (k * 97) % 613;
        int32_t half = AUDIO_RATE / 2;
        n = n < AUDIO_RATE - at ? n : AUDIO_RATE - at;
        if (at < half && at + n > half) {
            n = half - at;
        }

        box_audio_core_render(core, left + at, right + at, n);
        at += n;
        k ++;
        if (at == half) {
            sa_command(core, sa_params(0.2f, 0.8f, 5, 1));
        }
    }

    box_audio_core_free(core);
}

void Saturator_render_sizes(void) {
    // audio/saturator
    sa_sizes(sa_l, sa_r, BOX_AUDIO_CORE_QUANTUM);
    sa_sizes(sa_b_l, sa_b_r, 0);
    test_assert(audio_hash(sa_l, AUDIO_RATE) == audio_hash(sa_b_l, AUDIO_RATE));
    test_assert(audio_hash(sa_r, AUDIO_RATE) == audio_hash(sa_b_r, AUDIO_RATE));
    test_assert(audio_peak(sa_l, 0, AUDIO_RATE) > 0.01f);
}

void Saturator_silent_after_tail(void) {
    // audio/saturator
    box_audio_core_t *core = sa_core();
    sa_command(core, sa_params(0.6f, 0.7f, 8, 1));
    audio_sine(sa_sound, AUDIO_RATE / 4, 330, 0.5f);
    box_audio_core_render(core, sa_silence, sa_silence + 1, 1);
    test_assert(!box_audio_core_effect_running(core, 0));
    test_assert(box_audio_core_sound(core, 1, sa_sound, AUDIO_RATE / 4, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, 0);
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE / 8);
    test_assert(box_audio_core_effect_running(core, 0));
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE);
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE);
    test_assert(!box_audio_core_effect_running(core, 0));
    test_flt(audio_peak(sa_l, AUDIO_RATE / 2, AUDIO_RATE), 0);

    /* A new sound starts it again */
    audio_play(core, 2, 1, 1, 1, 0, 1, 0);
    sa_render(core, sa_l, sa_r, 0, AUDIO_RATE / 8);
    test_assert(box_audio_core_effect_running(core, 0));
    test_assert(audio_peak(sa_l, 0, AUDIO_RATE / 8) > 0.01f);
    box_audio_core_free(core);
}

void Saturator_component(void) {
    // audio/saturator
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    float params[BOX_AUDIO_EFFECT_PARAMS];
    const Saturator *s;
    ecs_add(world, e, Saturator);
    s = ecs_get(world, e, Saturator);
    test_flt(s->drive, 0.3f);
    test_flt(s->asymmetry, 0);
    test_flt(s->bits, 16);
    test_flt(s->mix, 1);
    test_assert(ecs_has(world, e, AudioEffectState));
    ecs_set(world, e, Saturator, { .drive = 0.6f, .asymmetry = 0.2f, .bits = 8,
        .mix = 0.5f });
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_SATURATOR);
    test_flt(params[0], 0.6f);
    test_flt(params[1], 0.2f);
    test_flt(params[2], 8);
    test_flt(params[3], 0.5f);
    test_flt(params[4], 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_SATURATOR, AUDIO_RATE), 0);
    ecs_fini(world);
}

void Saturator_script(void) {
    // audio/saturator
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/saturator.flecs");
    ecs_entity_t drums, grit;
    const Saturator *s;
    test_assert(ecs_script_run(world, "saturator", code, NULL) == 0);
    ecs_os_free(code);
    drums = ecs_lookup(world, "drums");
    grit = ecs_lookup(world, "drum_grit");
    test_assert(drums && grit);
    s = ecs_get(world, grit, Saturator);
    test_assert(s != NULL);
    test_flt(s->drive, 0.8f);
    test_flt(s->asymmetry, 0.25f);
    test_flt(s->bits, 12);
    test_flt(s->mix, 0.8f);

    audio_source(world, audio_tone(world, NULL, 200, 0.5f, AUDIO_RATE), 1, true, drums);
    audio_run(world, 20);
    test_assert(box_audio_module_effect_slot(world, grit) >= 0);
    printf("the drum grit squashes by %.2f dB\n",
        (double)ecs_get(world, grit, AudioMeter)->reduction);
    test_assert(ecs_get(world, grit, AudioMeter)->reduction > 1);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.05f);
    ecs_fini(world);
}
