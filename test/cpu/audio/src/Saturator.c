/**
 * @file Saturator.c
 * @brief The saturator, measured with sines in offline renders: its harmonics and DC, its level
 * across the drive, its aliasing against a clipper at the base rate, the bit crusher, the dry
 * mix, the reduction, changes of the drive, render sizes, its rest after a tail and the
 * component in a world; then its modes, their harmonics, level and aliasing, the low and the
 * high cut, the tone, the sample and hold, and the tanh mode against the saturator as it was.
 */

#include "world.h"
#include <saturate.h>
#include <stdio.h>
#include <stdlib.h>

/** Exponent of 2 of the core, which sets the gain of the drive; private to the core, so
 * declared here as internal.h declares it */
double box_audio_synth_exp2(
    double x);

/** Flush a sample as the core does: NaN, infinities, huge values and denormals become 0.
 *
 * @param x The sample
 * @return The sample, or 0
 */
static float sa_clean(
    float x)
{
    float a = x < 0 ? -x : x;

    return a >= 1e-30f && a <= 1e30f ? x : 0.0f;
}

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

/** Power of the aliases of a sine of 5 kHz at 48 kHz against its fundamental in a band: the
 * bins at every multiple of 1 kHz between two but the harmonics at 5 and 15 kHz.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames, whole milliseconds
 * @param low Lowest bin in kHz
 * @param high Highest bin in kHz
 * @return The power of the aliases over the power of the fundamental
 */
static double sa_band_aliases(
    const float *x,
    int32_t from,
    int32_t count,
    int32_t low,
    int32_t high)
{
    double sum = 0, f = sa_amplitude(x, from, count, 5000);
    int32_t k;
    for (k = low; k <= high; k ++) {
        if (k != 5 && k != 15) {
            double a = sa_amplitude(x, from, count, 1000.0 * k);
            sum += a * a;
        }
    }

    return sum / (f * f);
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
    return sa_band_aliases(x, from, count, 1, 23);
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
    test_flt(params[5], 0);
    test_flt(params[6], 0);
    test_flt(params[7], 0);
    test_flt(params[8], 0);
    test_flt(params[9], 0);
    test_int(s->mode, SaturatorTanh);
    test_flt(s->low_cut, 0);
    test_flt(s->high_cut, 0);
    test_flt(s->tone, 0);
    test_flt(s->rate, 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_SATURATOR, AUDIO_RATE), 0);

    ecs_set(world, e, Saturator, { .drive = 0.7f, .bits = 16, .mix = 1,
        .mode = SaturatorFold, .low_cut = 120, .high_cut = 7000, .tone = -0.5f, .rate = 9000 });
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_SATURATOR);
    test_flt(params[4], (float)BOX_AUDIO_SATURATOR_FOLD);
    test_flt(params[5], 120);
    test_flt(params[6], 7000);
    test_flt(params[7], -0.5f);
    test_flt(params[8], 9000);
    test_assert(SaturatorTube == BOX_AUDIO_SATURATOR_TUBE);
    test_assert(SaturatorHard == BOX_AUDIO_SATURATOR_HARD);
    test_assert(SaturatorRectify == BOX_AUDIO_SATURATOR_RECTIFY);
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

/** The parameters of a saturator in a mode, without the bit crusher, the filters and the hold,
 * fully wet.
 *
 * @param mode A box_audio_saturator_mode_t
 * @param drive 0..1
 * @param asymmetry 0..1
 * @return The parameters
 */
static box_audio_saturator_params_t sa_mode(
    int32_t mode,
    float drive,
    float asymmetry)
{
    box_audio_saturator_params_t p = sa_params(drive, asymmetry, 16, 1);
    p.mode = (float)mode;

    return p;
}

/** Name of a mode.
 *
 * @param mode A box_audio_saturator_mode_t
 * @return The name
 */
static const char* sa_mode_name(
    int32_t mode)
{
    static const char *names[] = { "tanh", "tube", "hard", "fold", "rectify" };

    return mode >= 0 && mode < BOX_AUDIO_SATURATOR_MODES ? names[mode] : "?";
}

/** Amplitudes of the harmonics 1 to count of a sine of 1 kHz in a range of sa_l.
 *
 * @param h Receives the amplitudes, h[k] of harmonic k; h[0] is the mean
 * @param count Harmonics
 */
static void sa_harmonics(
    double *h,
    int32_t count)
{
    const int32_t from = AUDIO_RATE / 2, frames = AUDIO_RATE / 2;
    int32_t k;
    h[0] = sa_mean(sa_l, from, frames);
    for (k = 1; k <= count; k ++) {
        h[k] = sa_amplitude(sa_l, from, frames, 1000.0 * k);
    }
}

/** Share of the power of harmonics 5 to 15 in the power of harmonics 1 to 15.
 *
 * @param h Amplitudes from sa_harmonics, 15 harmonics
 * @return The share
 */
static double sa_high_share(
    const double *h)
{
    double high = 0, all = 0;
    int32_t k;
    for (k = 1; k <= 15; k ++) {
        all += h[k] * h[k];
        if (k >= 5) {
            high += h[k] * h[k];
        }
    }

    return all > 0 ? high / all : 0;
}

void Saturator_mode_harmonics(void) {
    // audio/saturator
    box_audio_saturator_params_t p;
    double h[16], lo, hi;

    /* The tube curve makes even harmonics without a bias */
    p = sa_mode(BOX_AUDIO_SATURATOR_TUBE, 0.5f, 0);
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    sa_harmonics(h, 15);
    printf("tube, drive 0.5: 2nd %.1f dB, 3rd %.1f dB, 4th %.1f dB under the fundamental, "
        "DC %.2e\n", sa_db(h[2] / h[1]), sa_db(h[3] / h[1]), sa_db(h[4] / h[1]), h[0]);
    test_assert(h[2] / h[1] > 0.02);
    test_assert(h[4] / h[1] > 0.002);
    test_assert(fabs(h[0]) < 1e-4);

    /* The hard clipper makes a square wave of odd harmonics */
    p = sa_mode(BOX_AUDIO_SATURATOR_HARD, 0.8f, 0);
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    sa_harmonics(h, 15);
    printf("hard, drive 0.8: 2nd %.1f dB, 3rd %.1f dB, 5th %.1f dB, 7th %.1f dB under the "
        "fundamental\n", sa_db(h[2] / h[1]), sa_db(h[3] / h[1]), sa_db(h[5] / h[1]),
        sa_db(h[7] / h[1]));
    test_assert(h[3] / h[1] > 0.2);
    test_assert(h[5] / h[1] > 0.1);
    test_assert(h[7] / h[1] > 0.05);
    test_assert(h[2] / h[1] < 1e-3);
    test_assert(fabs(h[0]) < 1e-4);

    /* The folder moves the power to high harmonics, more with more drive */
    p = sa_mode(BOX_AUDIO_SATURATOR_FOLD, 0.5f, 0);
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    sa_harmonics(h, 15);
    lo = sa_high_share(h);
    p = sa_mode(BOX_AUDIO_SATURATOR_FOLD, 0.9f, 0);
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    sa_harmonics(h, 15);
    hi = sa_high_share(h);
    printf("fold: %.1f%% of the power above the 4th harmonic at drive 0.5, %.1f%% at 0.9, "
        "2nd %.1f dB\n", 100 * lo, 100 * hi, sa_db(h[2] / h[1]));
    test_assert(hi > 0.3);
    test_assert(hi > 3 * lo);
    test_assert(h[2] / h[1] < 1e-3);

    /* The half wave rectifier makes a strong 2nd harmonic, its DC taken out */
    p = sa_mode(BOX_AUDIO_SATURATOR_RECTIFY, 0.3f, 0);
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    sa_harmonics(h, 15);
    printf("rectify half wave, drive 0.3: 2nd %.1f dB, 4th %.1f dB under the fundamental, "
        "DC %.2e\n", sa_db(h[2] / h[1]), sa_db(h[4] / h[1]), h[0]);
    test_assert(h[2] / h[1] > 0.25);
    test_assert(fabs(h[0]) < 1e-4);

    /* The full wave rectifier is all octave up */
    p = sa_mode(BOX_AUDIO_SATURATOR_RECTIFY, 0.3f, 1);
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    sa_harmonics(h, 15);
    printf("rectify full wave, drive 0.3: fundamental %.1f dB under the 2nd, DC %.2e\n",
        sa_db(h[1] / h[2]), h[0]);
    test_assert(h[2] > 10 * h[1]);
    test_assert(fabs(h[0]) < 1e-4);
}

void Saturator_mode_level(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 5;
    float level = BOX_AUDIO_SATURATE_REFERENCE;
    double dry = level / sqrt(2.0);
    int32_t m, d;
    for (m = 0; m <= BOX_AUDIO_SATURATOR_MODES; m ++) {
        /* The last round is the full wave rectifier */
        int32_t mode = m < BOX_AUDIO_SATURATOR_MODES ? m : BOX_AUDIO_SATURATOR_RECTIFY;
        float asymmetry = m < BOX_AUDIO_SATURATOR_MODES ? 0.0f : 1.0f;
        double lo = 1e9, hi = -1e9;
        for (d = 0; d <= 10; d ++) {
            box_audio_saturator_params_t p = sa_mode(mode, 0.1f * (float)d, asymmetry);
            double change;
            sa_tone(&p, 1000, level, from + count);
            change = sa_db(sa_rms(sa_l, from, count) / dry);
            /* The tanh clipper keeps its compensation by the mean gain over the reference,
             * bit-identical, which keeps a -12 dBFS sine within 1.5 dB (level_across_drive)
             * and lets a -14 dBFS sine rise up to 2.5 dB at a full drive */
            test_assert(fabs(change) < (mode == BOX_AUDIO_SATURATOR_TANH ? 3 : 1.5));
            lo = change < lo ? change : lo;
            hi = change > hi ? change : hi;
        }

        printf("%s, asymmetry %.0f: a -14 dBFS sine keeps its RMS from %+.3f dB to %+.3f dB "
            "over the drive\n", sa_mode_name(mode), (double)asymmetry, lo, hi);
    }
}

/** Aliases of a 5 kHz sine at -6 dBFS through a mode up to 20 kHz, in dB against the
 * fundamental: through the saturator, the same curve at twice the rate without ADAA and the
 * same curve at the base rate.
 *
 * The bins at 21 to 23 kHz are apart: the 25 kHz harmonic, where the halfband of the
 * oversampler rolls off, folds to 23 kHz, which neither ADAA nor a higher rate takes out.
 *
 * @param mode A box_audio_saturator_mode_t
 * @param drive 0..1
 * @param over Receives the aliases of the saturator
 * @param plain Receives the aliases at twice the rate without ADAA
 * @param base Receives the aliases at the base rate
 * @param top Receives the aliases of the saturator at 21 to 23 kHz
 */
static void sa_mode_aliases(
    int32_t mode,
    float drive,
    double *over,
    double *plain,
    double *base,
    double *top)
{
    const int32_t from = AUDIO_RATE / 5, count = AUDIO_RATE / 5;
    box_audio_saturator_params_t p = sa_mode(mode, drive, 0);
    box_audio_saturate_shape_t shape;
    box_audio_oversample_t os;
    float level = sa_linear(-6);
    int32_t i;
    sa_tone(&p, 5000, level, from + count);
    *over = 10 * log10(sa_band_aliases(sa_l, from, count, 1, 20));
    *top = 10 * log10(sa_band_aliases(sa_l, from, count, 21, 23));

    box_audio_saturate_shape(&shape, mode, (float)pow(10.0, drive * 30 / 20), 0,
        BOX_AUDIO_SATURATE_REFERENCE);
    audio_sine(sa_sound, from + count, 5000, level);
    box_audio_oversample_clear(&os);
    for (i = 0; i < from + count; i ++) {
        float a, b;
        box_audio_oversample_up(&os, sa_sound[i], &a, &b);
        a = box_audio_saturate_shape_clip(&shape, a);
        b = box_audio_saturate_shape_clip(&shape, b);
        sa_b_l[i] = box_audio_oversample_down(&os, a, b) * shape.comp;
        sa_b_r[i] = box_audio_saturate_shape_clip(&shape, sa_sound[i]) * shape.comp;
    }

    *plain = 10 * log10(sa_band_aliases(sa_b_l, from, count, 1, 20));
    *base = 10 * log10(sa_band_aliases(sa_b_r, from, count, 1, 20));
}

void Saturator_mode_alias(void) {
    // audio/saturator
    const int32_t modes[] = { BOX_AUDIO_SATURATOR_HARD, BOX_AUDIO_SATURATOR_FOLD };
    const float drives[] = { 0.8f, 1 };
    double over, plain, base, top;
    int32_t m, d;
    for (m = 0; m < 2; m ++) {
        for (d = 0; d < 2; d ++) {
            sa_mode_aliases(modes[m], drives[d], &over, &plain, &base, &top);
            printf("%s, aliases of a -6 dBFS 5 kHz sine up to 20 kHz at drive %.1f: %.1f dB "
                "(%.1f dB at 21 to 23 kHz), %.1f dB at twice the rate without ADAA, %.1f dB "
                "at the base rate\n", sa_mode_name(modes[m]), (double)drives[d], over, top,
                plain, base);
            test_assert(over < -40);
            test_assert(over < base - 20);
            if (plain > -60) {
                test_assert(over < plain - 10);
            }
        }
    }
}

void Saturator_low_cut(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 2;
    box_audio_saturator_params_t p = sa_params(0, 0, 16, 1);
    double off, on, h1, h3, h3_cut;

    /* The bass leaves before the clipper, the highs pass */
    sa_tone(&p, 100, sa_linear(-12), AUDIO_RATE);
    off = sa_rms(sa_l, from, count);
    p.low_cut = 800;
    sa_tone(&p, 100, sa_linear(-12), AUDIO_RATE);
    on = sa_rms(sa_l, from, count);
    printf("low cut 800 Hz: a 100 Hz sine %.1f dB", sa_db(on / off));
    test_assert(on < off * 0.1);
    p.low_cut = 0;
    sa_tone(&p, 5000, sa_linear(-12), AUDIO_RATE);
    off = sa_rms(sa_l, from, count);
    p.low_cut = 800;
    sa_tone(&p, 5000, sa_linear(-12), AUDIO_RATE);
    on = sa_rms(sa_l, from, count);
    printf(", a 5 kHz sine %+.2f dB\n", sa_db(on / off));
    test_assert(fabs(sa_db(on / off)) < 0.5);

    /* Driven, a bass that is cut before the clipper distorts much less */
    p = sa_params(0.8f, 0, 16, 1);
    sa_tone(&p, 100, sa_linear(-6), AUDIO_RATE);
    h1 = sa_amplitude(sa_l, from, count, 100);
    h3 = sa_amplitude(sa_l, from, count, 300) / h1;
    p.low_cut = 400;
    sa_tone(&p, 100, sa_linear(-6), AUDIO_RATE);
    h1 = sa_amplitude(sa_l, from, count, 100);
    h3_cut = sa_amplitude(sa_l, from, count, 300) / h1;
    printf("drive 0.8, a 100 Hz sine: 3rd %.1f dB under the fundamental, %.1f dB with a low "
        "cut of 400 Hz\n", sa_db(h3), sa_db(h3_cut));
    test_assert(h3_cut < h3 * 0.5);
}

void Saturator_high_cut(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 2;
    box_audio_saturator_params_t p = sa_params(0.8f, 0, 16, 1);
    double h1, h5, h1_cut, h5_cut;
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    h1 = sa_amplitude(sa_l, from, count, 1000);
    h5 = sa_amplitude(sa_l, from, count, 5000);
    p.high_cut = 2000;
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    h1_cut = sa_amplitude(sa_l, from, count, 1000);
    h5_cut = sa_amplitude(sa_l, from, count, 5000);
    printf("high cut 2000 Hz after the clipper: fundamental %+.2f dB, 5th harmonic %+.1f dB\n",
        sa_db(h1_cut / h1), sa_db(h5_cut / h5));
    test_assert(fabs(sa_db(h1_cut / h1)) < 1.5);
    test_assert(h5_cut < h5 * 0.2);

    /* The highest high cut leaves the harmonics of the audible band */
    p.high_cut = BOX_AUDIO_SATURATOR_CUT_MAX;
    sa_tone(&p, 1000, sa_linear(-6), AUDIO_RATE);
    h1_cut = sa_amplitude(sa_l, from, count, 1000);
    h5_cut = sa_amplitude(sa_l, from, count, 5000);
    test_assert(fabs(sa_db(h1_cut / h1)) < 0.1);
    test_assert(fabs(sa_db(h5_cut / h5)) < 0.1);
}

/** Third harmonic of a 2 kHz sine at -14 dBFS against its fundamental through a saturator at
 * drive 0.4 and a tone.
 *
 * @param tone -1..1
 * @return The ratio
 */
static double sa_tone_third(
    float tone)
{
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 2;
    box_audio_saturator_params_t p = sa_params(0.4f, 0, 16, 1);
    p.tone = tone;
    sa_tone(&p, 2000, BOX_AUDIO_SATURATE_REFERENCE, AUDIO_RATE);

    return sa_amplitude(sa_l, from, count, 6000) / sa_amplitude(sa_l, from, count, 2000);
}

void Saturator_tone(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 2;
    const double frequencies[] = { 100, 1000, 5000 };
    box_audio_saturator_params_t p = sa_params(0, 0, 16, 1);
    double down = sa_tone_third(-1), flat = sa_tone_third(0), up = sa_tone_third(1);
    int32_t i;
    printf("a 2 kHz sine at drive 0.4: 3rd %.1f dB under the fundamental at tone -1, %.1f dB "
        "at 0, %.1f dB at 1\n", sa_db(down), sa_db(flat), sa_db(up));
    test_assert(up > flat * 1.4);
    test_assert(flat > down * 1.4);

    /* A quiet signal that the clipper leaves linear passes the tilt and its inverse as it
     * was */
    for (i = 0; i < 3; i ++) {
        double a, b;
        p.tone = 0;
        sa_tone(&p, frequencies[i], sa_linear(-40), AUDIO_RATE);
        a = sa_amplitude(sa_l, from, count, frequencies[i]);
        p.tone = 1;
        sa_tone(&p, frequencies[i], sa_linear(-40), AUDIO_RATE);
        b = sa_amplitude(sa_l, from, count, frequencies[i]);
        printf("tone 1 on a quiet %.0f Hz sine: %+.3f dB\n", frequencies[i], sa_db(b / a));
        test_assert(fabs(sa_db(b / a)) < 0.1);
    }
}

void Saturator_rate(void) {
    // audio/saturator
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 2;
    box_audio_saturator_params_t p = sa_params(0, 0, 16, 1);
    double h, image, image2;
    sa_tone(&p, 1300, sa_linear(-12), AUDIO_RATE);
    h = sa_amplitude(sa_l, from, count, 1300);
    image = sa_amplitude(sa_l, from, count, 2700);
    printf("no rate: %.1f dB at 2700 Hz", sa_db(image / h));
    test_assert(image < h * 1e-3);
    p.rate = 4000;
    sa_tone(&p, 1300, sa_linear(-12), AUDIO_RATE);
    h = sa_amplitude(sa_l, from, count, 1300);
    image = sa_amplitude(sa_l, from, count, 2700);
    image2 = sa_amplitude(sa_l, from, count, 5300);
    printf(", rate 4000 Hz: images %.1f dB at 2700 Hz and %.1f dB at 5300 Hz under the "
        "fundamental\n", sa_db(image / h), sa_db(image2 / h));
    test_assert(image > h * 0.2);
    test_assert(image2 > h * 0.1);

    /* Each held sample spans 12 frames */
    {
        int32_t i, changes = 0;
        for (i = from + 1; i < from + 1200; i ++) {
            changes += sa_l[i] != sa_l[i - 1];
        }

        test_assert(changes >= 99 && changes <= 100);
    }
}

void Saturator_tanh_identical(void) {
    // audio/saturator
    const int32_t frames = AUDIO_RATE / 2;
    const float drives[] = { 0.3f, 0.6f, 1.0f }, asymmetries[] = { 0, 0.3f, 1.0f };
    float coef = (float)(1.0 - 2.0 * 3.14159265358979323846 *
        (double)BOX_AUDIO_SATURATOR_DC / (double)AUDIO_RATE);
    int32_t k, c, i;
    sa_tone(NULL, 440, 0.5f, frames);
    memcpy(sa_b_l, sa_l, sizeof(float) * frames);
    memcpy(sa_b_r, sa_r, sizeof(float) * frames);
    for (k = 0; k < 3; k ++) {
        box_audio_saturator_params_t p = sa_params(drives[k], asymmetries[k], 16, 1);
        float gain = (float)box_audio_synth_exp2((double)(drives[k] *
            BOX_AUDIO_SATURATOR_DRIVE_DB) * 0.16609640474436813);
        int32_t mismatch = 0;
        sa_tone(&p, 440, 0.5f, frames);

        /* The saturator as it was before its modes */
        for (c = 0; c < 2; c ++) {
            const float *in = c ? sa_b_r : sa_b_l, *out = c ? sa_r : sa_l;
            box_audio_saturate_curve_t curve;
            box_audio_oversample_t os;
            box_audio_dc_t dc = {0};
            box_audio_oversample_clear(&os);
            box_audio_saturate_curve(&curve, gain, asymmetries[k] * BOX_AUDIO_SATURATOR_BIAS);
            for (i = 0; i < frames; i ++) {
                float a, b, y;
                box_audio_oversample_up(&os, in[i], &a, &b);
                a = box_audio_saturate_clip(&curve, a);
                b = box_audio_saturate_clip(&curve, b);
                y = box_audio_oversample_down(&os, a, b) * curve.comp;
                y = sa_clean(box_audio_dc_next(&dc, y, coef));
                mismatch += out[i] != y;
            }
        }

        printf("drive %.1f, asymmetry %.1f: %d samples differ from the saturator before its "
            "modes\n", (double)drives[k], (double)asymmetries[k], mismatch);
        test_int(mismatch, 0);
    }
}

/** Render a saturator in a mode with all its filters and the hold on a looping sine in pieces
 * of a size, with a change of its params halfway.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param size Frames of each render, 0 for sizes that vary
 */
static void sa_mode_sizes(
    float *left,
    float *right,
    int32_t size)
{
    box_audio_core_t *core = sa_core();
    box_audio_saturator_params_t p = sa_mode(BOX_AUDIO_SATURATOR_FOLD, 0.6f, 0.2f);
    int32_t at = 0, k = 0;
    p.low_cut = 80;
    p.high_cut = 9000;
    p.tone = 0.4f;
    p.rate = 12000;
    p.mix = 0.8f;
    sa_command(core, p);
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
            p = sa_mode(BOX_AUDIO_SATURATOR_HARD, 0.9f, 0.5f);
            p.low_cut = 300;
            p.high_cut = 0;
            p.tone = -0.6f;
            p.rate = 6000;
            sa_command(core, p);
        }
    }

    box_audio_core_free(core);
}

void Saturator_mode_render_sizes(void) {
    // audio/saturator
    sa_mode_sizes(sa_l, sa_r, BOX_AUDIO_CORE_QUANTUM);
    sa_mode_sizes(sa_b_l, sa_b_r, 0);
    test_assert(audio_hash(sa_l, AUDIO_RATE) == audio_hash(sa_b_l, AUDIO_RATE));
    test_assert(audio_hash(sa_r, AUDIO_RATE) == audio_hash(sa_b_r, AUDIO_RATE));
    test_assert(audio_peak(sa_l, 0, AUDIO_RATE) > 0.01f);
    test_assert(audio_peak(sa_l, 0, AUDIO_RATE) < 2);
}

void Saturator_mode_silent_after_tail(void) {
    // audio/saturator
    int32_t m;
    for (m = 1; m < BOX_AUDIO_SATURATOR_MODES; m ++) {
        box_audio_core_t *core = sa_core();
        box_audio_saturator_params_t p = sa_mode(m, 0.6f, 0.4f);
        p.low_cut = 100;
        p.high_cut = 6000;
        p.tone = 0.5f;
        p.rate = 8000;
        sa_command(core, p);
        audio_sine(sa_sound, AUDIO_RATE / 4, 330, 0.5f);
        box_audio_core_render(core, sa_silence, sa_silence + 1, 1);
        test_assert(box_audio_core_sound(core, 1, sa_sound, AUDIO_RATE / 4, 1, AUDIO_RATE));
        audio_play(core, 1, 1, 1, 1, 0, 1, 0);
        sa_render(core, sa_l, sa_r, 0, AUDIO_RATE / 8);
        test_assert(box_audio_core_effect_running(core, 0));
        test_assert(audio_peak(sa_l, 0, AUDIO_RATE / 8) > 0.01f);
        sa_render(core, sa_l, sa_r, 0, AUDIO_RATE);
        sa_render(core, sa_l, sa_r, 0, AUDIO_RATE);
        printf("%s: stops after its tail\n", sa_mode_name(m));
        test_assert(!box_audio_core_effect_running(core, 0));
        test_flt(audio_peak(sa_l, AUDIO_RATE / 2, AUDIO_RATE), 0);
        box_audio_core_free(core);
    }
}

void Saturator_mode_reduction(void) {
    // audio/saturator
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    int32_t m;
    for (m = 1; m < BOX_AUDIO_SATURATOR_MODES; m ++) {
        box_audio_core_t *core = sa_core();
        sa_command(core, sa_mode(m, 1, 0));
        audio_sine(sa_sound, AUDIO_RATE, 1000, sa_linear(-6));
        sa_play(core, AUDIO_RATE, true);
        sa_render(core, sa_l, sa_r, 0, AUDIO_RATE / 10);
        box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
        printf("%s, reduction of a -6 dBFS sine at drive 1: %.2f dB\n", sa_mode_name(m),
            (double)meters[0].reduction);
        test_assert(meters[0].reduction > 10);
        box_audio_core_free(core);
    }
}
