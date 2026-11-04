/**
 * @file Equalizer.c
 * @brief The equalizer kind of effect, measured with sines in offline renders: the gain of a
 * peak and its width, the shelves, the cuts, stability, smooth changes, flat bands that pass
 * their input bit for bit and the component in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** First and end frame of the window a level is measured over, after the filters settled */
#define EQ_FROM (AUDIO_RATE / 4)
#define EQ_TO (AUDIO_RATE / 2)

/** Peak level of the measured sines, low enough that +24 dB stays under the limiter */
#define EQ_LEVEL 0.02f

/** A sine of the measurements */
static float eq_tone[AUDIO_RATE];

/** A noise of the stability case */
static float eq_noise[AUDIO_RATE];

/** Output of the renders that compare two cores */
static float eq_a_l[AUDIO_FRAMES], eq_a_r[AUDIO_FRAMES];
static float eq_b_l[AUDIO_FRAMES], eq_b_r[AUDIO_FRAMES];

/** Set a band in the params of an effect command.
 *
 * @param params The params
 * @param band Index of the band
 * @param kind A box_audio_eq_kind_t
 * @param frequency Hz
 * @param gain dB
 * @param q Q
 */
static void eq_band(
    float *params,
    int32_t band,
    box_audio_eq_kind_t kind,
    float frequency,
    float gain,
    float q)
{
    params[band * 4] = (float)kind;
    params[band * 4 + 1] = frequency;
    params[band * 4 + 2] = gain;
    params[band * 4 + 3] = q;
}

/** Send an effect command of an equalizer in insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take it off
 * @param params The params
 */
static void eq_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t bus,
    const float *params)
{
    uint8_t bytes[256];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, BOX_AUDIO_EFFECT_EQ, bus, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with bus 1 into the master and a sine of a frequency playing on it as sound
 * 1, with an equalizer on bus 1 when params is not NULL.
 *
 * @param params The params of the equalizer, NULL for none
 * @param frequency Hz of the sine
 * @param level Peak level of the sine
 * @param loop Whether the sine loops
 * @return The core
 */
static box_audio_core_t* eq_open(
    const float *params,
    double frequency,
    float level,
    bool loop)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    audio_order(core, routes, 2);
    if (params) {
        eq_command(core, 0, 1, params);
    }

    audio_sine(eq_tone, AUDIO_RATE, frequency, level);
    test_assert(box_audio_core_sound(core, 1, eq_tone, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);

    return core;
}

/** RMS of the left channel of the last render over the window.
 *
 * @return The RMS
 */
static double eq_rms(void)
{
    double sum = 0;
    int32_t i;
    for (i = EQ_FROM; i < EQ_TO; i ++) {
        sum += (double)audio_l[i] * audio_l[i];
    }

    return sqrt(sum / (double)(EQ_TO - EQ_FROM));
}

/** Measure the gain of an equalizer at a frequency against the same sine without it.
 *
 * @param params The params of the equalizer
 * @param frequency Hz
 * @return Gain in dB
 */
static double eq_gain(
    const float *params,
    double frequency)
{
    box_audio_core_t *core = eq_open(NULL, frequency, EQ_LEVEL, false);
    double dry, wet;
    audio_render(core, 0, EQ_TO);
    dry = eq_rms();
    box_audio_core_free(core);
    core = eq_open(params, frequency, EQ_LEVEL, false);
    audio_render(core, 0, EQ_TO);
    wet = eq_rms();
    box_audio_core_free(core);
    test_assert(dry > 0);

    return 20 * log10(wet / dry);
}

/** The frequencies at which a digital RBJ peak has half its gain in dB: the analog ones, w -
 * 1 / w = 1 / q, mapped through the bilinear transform prewarped at the center.
 *
 * @param center Hz
 * @param q Q
 * @param low Receives the lower one
 * @param high Receives the higher one
 */
static void eq_half_gain(
    double center,
    double q,
    double *low,
    double *high)
{
    double w = sqrt(1 + 1 / (4 * q * q)) + 1 / (2 * q);
    double t = tan(AUDIO_PI * center / AUDIO_RATE);
    *high = atan(w * t) * AUDIO_RATE / AUDIO_PI;
    *low = atan(t / w) * AUDIO_RATE / AUDIO_PI;
}

void Equalizer_peak_center(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    double boost, dip, wide;
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 1000, 12, 1);
    boost = eq_gain(params, 1000);
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 1000, -12, 1);
    dip = eq_gain(params, 1000);
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 3000, 6, 0.3f);
    wide = eq_gain(params, 3000);
    printf("peak at its center: %+.3f dB for +12, %+.3f dB for -12, %+.3f dB for +6\n",
        boost, dip, wide);
    test_assert(fabs(boost - 12) < 0.1);
    test_assert(fabs(dip + 12) < 0.1);
    test_assert(fabs(wide - 6) < 0.1);
}

void Equalizer_peak_bandwidth(void) {
    // audio/eq
    float qs[] = { 0.7f, 2, 6 };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
        double low, high, at_low, at_high, outside;
        eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 1000, 12, qs[i]);
        eq_half_gain(1000, qs[i], &low, &high);
        at_low = eq_gain(params, low);
        at_high = eq_gain(params, high);
        outside = eq_gain(params, high * 2);
        printf("q %.1f: half gain at %.1f Hz %+.3f dB and %.1f Hz %+.3f dB, %+.3f dB an "
            "octave above\n", (double)qs[i], low, at_low, high, at_high, outside);
        test_assert(fabs(at_low - 6) < 0.25);
        test_assert(fabs(at_high - 6) < 0.25);
        test_assert(outside < 6);
    }
}

void Equalizer_shelves(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    double low_below, low_above, high_above, high_below;
    eq_band(params, 0, BOX_AUDIO_EQ_LOW_SHELF, 200, 9, 0.7071f);
    low_below = eq_gain(params, 20);
    low_above = eq_gain(params, 4000);
    eq_band(params, 0, BOX_AUDIO_EQ_HIGH_SHELF, 4000, -6, 0.7071f);
    high_above = eq_gain(params, 20000);
    high_below = eq_gain(params, 200);
    printf("low shelf +9 dB at 200 Hz: %+.3f dB at 20 Hz, %+.3f dB at 4 kHz\n",
        low_below, low_above);
    printf("high shelf -6 dB at 4 kHz: %+.3f dB at 20 kHz, %+.3f dB at 200 Hz\n",
        high_above, high_below);
    test_assert(fabs(low_below - 9) < 0.25);
    test_assert(fabs(low_above) < 0.25);
    test_assert(fabs(high_above + 6) < 0.3);
    test_assert(fabs(high_below) < 0.25);
}

void Equalizer_cuts(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    double low_at, low_octave, low_pass, high_at, high_octave, high_pass;
    eq_band(params, 0, BOX_AUDIO_EQ_LOW_CUT, 400, 0, 0.7071f);
    low_at = eq_gain(params, 400);
    low_octave = eq_gain(params, 200);
    low_pass = eq_gain(params, 4000);
    eq_band(params, 0, BOX_AUDIO_EQ_HIGH_CUT, 2000, 0, 0.7071f);
    high_at = eq_gain(params, 2000);
    high_octave = eq_gain(params, 4000);
    high_pass = eq_gain(params, 200);
    printf("low cut at 400 Hz: %+.3f dB there, %+.3f dB an octave below, %+.3f dB at 4 kHz\n",
        low_at, low_octave, low_pass);
    printf("high cut at 2 kHz: %+.3f dB there, %+.3f dB an octave above, %+.3f dB at 200 "
        "Hz\n", high_at, high_octave, high_pass);
    test_assert(fabs(low_at + 3.01) < 0.2);
    test_assert(low_octave < -11.5 && low_octave > -13.5);
    test_assert(fabs(low_pass) < 0.1);
    test_assert(fabs(high_at + 3.01) < 0.2);
    test_assert(high_octave < -11.5 && high_octave > -13.5);
    test_assert(fabs(high_pass) < 0.1);
}

void Equalizer_bands_in_a_row(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    double both;
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 1000, 6, 1);
    eq_band(params, 3, BOX_AUDIO_EQ_PEAK, 1000, 6, 1);
    both = eq_gain(params, 1000);
    printf("two peaks of +6 dB: %+.3f dB\n", both);
    test_assert(fabs(both - 12) < 0.1);
}

void Equalizer_stable(void) {
    // audio/eq
    float extremes[6][BOX_AUDIO_EFFECT_PARAMS];
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t i, b, block, frames = AUDIO_RATE / 4;
    float peak = 0;
    for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
        eq_band(extremes[0], b, BOX_AUDIO_EQ_PEAK, 1e9f, 1e30f, 1e30f);
        eq_band(extremes[1], b, (box_audio_eq_kind_t)(1 + b), 10, 24, 18);
        eq_band(extremes[2], b, BOX_AUDIO_EQ_HIGH_SHELF, -1e30f, -1e30f, -1e30f);
        eq_band(extremes[3], b, BOX_AUDIO_EQ_PEAK, NAN, NAN, NAN);
        eq_band(extremes[4], b, (box_audio_eq_kind_t)(2 + b), INFINITY, -INFINITY, INFINITY);
        eq_band(extremes[5], b, BOX_AUDIO_EQ_PEAK, AUDIO_RATE * 0.49f, 24, 18);
        extremes[3][b * 4] = NAN;
    }

    for (i = 0; i < 6; i ++) {
        for (b = BOX_AUDIO_EQ_BANDS * 4; b < BOX_AUDIO_EFFECT_PARAMS; b ++) {
            extremes[i][b] = 0;
        }
    }

    extremes[3][0] = (float)BOX_AUDIO_EQ_LOW_CUT;
    srand(11);
    for (i = 0; i < AUDIO_RATE; i ++) {
        eq_noise[i] = (float)rand() / (float)RAND_MAX * 2 - 1;
    }

    audio_order(core, routes, 2);
    test_assert(box_audio_core_sound(core, 2, eq_noise, AUDIO_RATE, 1, AUDIO_RATE));
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 1e-5f);
    audio_play(core, 2, 2, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (block = 0; block < 36; block ++) {
        eq_command(core, 0, 1, extremes[block % 6]);
        audio_render(core, 0, frames);
        for (i = 0; i < frames; i ++) {
            test_assert(isfinite(audio_l[i]) && isfinite(audio_r[i]));
            peak = fabsf(audio_l[i]) > peak ? fabsf(audio_l[i]) : peak;
            peak = fabsf(audio_r[i]) > peak ? fabsf(audio_r[i]) : peak;
        }
    }

    eq_command(core, 0, 1, extremes[5]);
    for (block = 0; block < 8; block ++) {
        audio_render(core, 0, frames);
        for (i = 0; i < frames; i ++) {
            test_assert(isfinite(audio_l[i]) && isfinite(audio_r[i]));
            peak = fabsf(audio_l[i]) > peak ? fabsf(audio_l[i]) : peak;
        }
    }

    printf("peak of 11 seconds of noise at extreme values: %.4f\n", (double)peak);
    test_assert(peak > 0);
    test_assert(peak < BOX_AUDIO_LIMITER_THRESHOLD);
    box_audio_core_free(core);
}

void Equalizer_gain_step_no_click(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    float level = 0.1f, slope = (float)(2 * AUDIO_PI * 440 / AUDIO_RATE);
    float before, after;
    box_audio_core_t *core;
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 440, 0, 2);
    core = eq_open(params, 440, level, true);
    audio_render(core, 0, AUDIO_RATE / 2);
    before = audio_max_step(audio_l, AUDIO_RATE / 4, AUDIO_RATE / 2);
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 440, 12, 2);
    eq_command(core, 0, 1, params);
    audio_render(core, AUDIO_RATE / 2, AUDIO_RATE / 2);
    after = audio_max_step(audio_l, AUDIO_RATE / 2, AUDIO_RATE);
    printf("largest step: %.5f before, %.5f after a step of +12 dB, %.5f for the louder "
        "sine\n", (double)before, (double)after, (double)(4 * level * slope));
    test_assert(before <= level * slope * 1.05f);
    test_assert(after < 4 * level * slope * 1.25f);
    box_audio_core_free(core);
}

void Equalizer_automation_no_click(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    float level = 0.05f, slope = (float)(2 * AUDIO_PI * 440 / AUDIO_RATE), step, peak;
    box_audio_core_t *core;
    int32_t frame;
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 200, 0, 1);
    eq_band(params, 1, BOX_AUDIO_EQ_LOW_CUT, 20, 0, 0.7071f);
    core = eq_open(params, 440, level, true);
    for (frame = 0; frame < AUDIO_RATE / AUDIO_FRAME; frame ++) {
        float t = (float)frame / (float)(AUDIO_RATE / AUDIO_FRAME);
        eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 200 + 800 * t, 12 * t, 1 + t);
        eq_band(params, 1, BOX_AUDIO_EQ_LOW_CUT, 20 + 300 * t, 0, 0.7071f);
        eq_command(core, 0, 1, params);
        audio_render(core, frame * AUDIO_FRAME, AUDIO_FRAME);
    }

    step = audio_max_step(audio_l, AUDIO_FRAME, AUDIO_RATE);
    peak = audio_peak(audio_l, AUDIO_FRAME, AUDIO_RATE);
    printf("automation every frame: largest step %.5f, peak %.4f, %.5f for a sine of the "
        "peak\n", (double)step, (double)peak, (double)(peak * slope));
    test_assert(peak > level);
    test_assert(step < peak * slope * 1.25f);
    box_audio_core_free(core);
}

void Equalizer_flat_identical(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_core_t *a = audio_new(), *b = audio_new();
    int32_t i;
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 1000, 0, 2);
    eq_band(params, 1, BOX_AUDIO_EQ_LOW_SHELF, 100, 0, 0.7071f);
    eq_band(params, 2, BOX_AUDIO_EQ_HIGH_SHELF, 8000, 0, 4);
    eq_band(params, 3, BOX_AUDIO_EQ_OFF, 500, 12, 1);
    eq_command(a, 0, 0, params);
    eq_command(a, 1, 1, params);
    eq_command(a, 2, 2, params);
    audio_workload(a, eq_a_l, eq_a_r, AUDIO_FRAMES);
    audio_workload(b, eq_b_l, eq_b_r, AUDIO_FRAMES);
    test_assert(box_audio_core_effect_running(a, 0));
    test_assert(audio_peak(eq_a_l, 0, AUDIO_FRAMES) > 0.01f);
    test_assert(audio_hash(eq_a_l, AUDIO_FRAMES) == audio_hash(eq_b_l, AUDIO_FRAMES));
    test_assert(audio_hash(eq_a_r, AUDIO_FRAMES) == audio_hash(eq_b_r, AUDIO_FRAMES));
    for (i = 0; i < AUDIO_FRAMES; i ++) {
        test_assert(eq_a_l[i] == eq_b_l[i] && eq_a_r[i] == eq_b_r[i]);
    }

    box_audio_core_free(a);
    box_audio_core_free(b);
}

void Equalizer_back_to_flat_identical(void) {
    // audio/eq
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_core_t *a, *b;
    int32_t i, settled = AUDIO_RATE / 4 + (int32_t)(BOX_AUDIO_EQ_RAMP * AUDIO_RATE) + 2;
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 600, 6, 1);
    eq_band(params, 1, BOX_AUDIO_EQ_LOW_CUT, 100, 0, 0.7071f);
    a = eq_open(params, 440, 0.2f, true);
    audio_render(a, 0, AUDIO_RATE / 4);
    eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 600, 0, 1);
    eq_band(params, 1, BOX_AUDIO_EQ_OFF, 100, 0, 0.7071f);
    eq_command(a, 0, 1, params);
    audio_render(a, AUDIO_RATE / 4, AUDIO_RATE / 4 * 3);
    memcpy(eq_a_l, audio_l, sizeof(float) * AUDIO_RATE);
    b = eq_open(NULL, 440, 0.2f, true);
    audio_render(b, 0, AUDIO_RATE);
    for (i = settled; i < AUDIO_RATE; i ++) {
        test_assert(eq_a_l[i] == audio_l[i]);
    }

    test_assert(eq_a_l[AUDIO_RATE / 8] != audio_l[AUDIO_RATE / 8]);
    box_audio_core_free(a);
    box_audio_core_free(b);
}

/** Render frames into eq_a_l and eq_a_r from a frame on, in blocks of a size.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 * @param size Frames of a block
 */
static void eq_render_blocks(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames,
    int32_t size)
{
    int32_t i;
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, eq_a_l + at + i, eq_a_r + at + i, n);
    }
}

void Equalizer_render_sizes(void) {
    // audio/eq
    static const int32_t sizes[] = { 128, 1, 7, 64, 333 };
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint64_t first = 0;
    int32_t s, half = AUDIO_RATE / 4;
    for (s = 0; s < 5; s ++) {
        box_audio_core_t *core;
        eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 800, 9, 2);
        eq_band(params, 2, BOX_AUDIO_EQ_HIGH_CUT, 3000, 0, 1);
        core = eq_open(params, 700, 0.2f, true);
        eq_render_blocks(core, 0, half, sizes[s]);
        eq_band(params, 0, BOX_AUDIO_EQ_PEAK, 1200, -9, 0.5f);
        eq_command(core, 0, 1, params);
        eq_render_blocks(core, half, half, sizes[s]);
        if (!s) {
            first = audio_hash(eq_a_l, half * 2);
        } else {
            test_assert(audio_hash(eq_a_l, half * 2) == first);
        }

        box_audio_core_free(core);
    }
}

void Equalizer_component(void) {
    // audio/eq
    ecs_world_t *world = audio_world();
    ecs_entity_t vocals = audio_bus(world, "vocals", 0, 1);
    ecs_entity_t eq = ecs_entity(world, { .name = "vocals_eq" });
    ecs_entity_t tone = audio_tone(world, "low", 60, 0.2f, AUDIO_RATE);
    Equalizer *values;
    double dry = 0, cut = 0;
    int32_t i, start;
    audio_source(world, tone, 1, true, vocals);
    audio_run(world, 30);
    start = audio_tap_count - AUDIO_RATE / 4;
    for (i = start; i < audio_tap_count; i ++) {
        dry += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    values = ecs_ensure(world, eq, Equalizer);
    test_int(values->bands[0].kind, AudioEqOff);
    test_flt(values->bands[0].frequency, 100);
    test_flt(values->bands[3].frequency, 8000);
    test_flt(values->bands[2].q, 0.7071f);
    values->bands[0].kind = AudioEqLowCut;
    values->bands[0].frequency = 240;
    ecs_modified(world, eq, Equalizer);
    audio_insert(world, vocals, 0, eq);
    audio_run(world, 30);
    test_int(box_audio_module_effect_slot(world, eq), 0);
    start = audio_tap_count - AUDIO_RATE / 4;
    for (i = start; i < audio_tap_count; i ++) {
        cut += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    printf("a 60 Hz tone through a low cut at 240 Hz: %.2f dB\n", 10 * log10(cut / dry));
    test_assert(dry > 0);
    test_assert(10 * log10(cut / dry) < -20);
    ecs_fini(world);
}
