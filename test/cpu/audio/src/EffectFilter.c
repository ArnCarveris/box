/**
 * @file EffectFilter.c
 * @brief The filter kind of effect, the state variable filter of the synth on a bus, measured
 * with sines in offline renders: its slopes, the notch, stability, smooth sweeps, the mix and
 * the component in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** First and end frame of the window a level is measured over, after the filter settled */
#define FL_FROM (AUDIO_RATE / 4)
#define FL_TO (AUDIO_RATE / 2)

/** Peak level of the measured sines */
#define FL_LEVEL 0.1f

/** A sine of the measurements */
static float fl_tone[AUDIO_RATE];

/** A noise of the stability case */
static float fl_noise[AUDIO_RATE];

/** The frame of silence that a core renders before its sine plays */
static float fl_silence[2];

/** Output of the renders that compare two cores */
static float fl_a_l[AUDIO_FRAMES], fl_a_r[AUDIO_FRAMES];
static float fl_b_l[AUDIO_FRAMES], fl_b_r[AUDIO_FRAMES];

/** The parameters of a filter.
 *
 * @param kind A box_audio_synth_filter_t
 * @param cutoff Hz
 * @param resonance 0..1
 * @param slope A box_audio_synth_slope_t
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_filter_params_t fl_params(
    box_audio_synth_filter_t kind,
    float cutoff,
    float resonance,
    box_audio_synth_slope_t slope,
    float mix)
{
    return (box_audio_filter_params_t){ (float)kind, cutoff, resonance, (float)slope, mix };
}

/** Send an effect command of a filter in insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take it off
 * @param p The parameters
 */
static void fl_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t bus,
    box_audio_filter_params_t p)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, BOX_AUDIO_EFFECT_FILTER, bus, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with bus 1 into the master and a sine playing on it as sound 1, with a
 * filter on bus 1 when p is not NULL.
 *
 * @param p The parameters of the filter, NULL for none
 * @param frequency Hz of the sine
 * @param level Peak level of the sine
 * @param loop Whether the sine loops
 * @return The core
 */
static box_audio_core_t* fl_open(
    const box_audio_filter_params_t *p,
    double frequency,
    float level,
    bool loop)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    audio_order(core, routes, 2);
    if (p) {
        fl_command(core, 0, 1, *p);
    }

    /* A frame of silence lets the effect enter at once, as an effect in place before its bus
     * plays does, so that the output does not depend on the size of the first render */
    box_audio_core_render(core, fl_silence, fl_silence + 1, 1);
    audio_sine(fl_tone, AUDIO_RATE, frequency, level);
    test_assert(box_audio_core_sound(core, 1, fl_tone, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);

    return core;
}

/** RMS of the left channel of the last render over the window.
 *
 * @return The RMS
 */
static double fl_rms(void)
{
    double sum = 0;
    int32_t i;
    for (i = FL_FROM; i < FL_TO; i ++) {
        sum += (double)audio_l[i] * audio_l[i];
    }

    return sqrt(sum / (double)(FL_TO - FL_FROM));
}

/** Measure the gain of a filter at a frequency against the same sine without it.
 *
 * @param p The parameters of the filter
 * @param frequency Hz
 * @return Gain in dB
 */
static double fl_gain(
    box_audio_filter_params_t p,
    double frequency)
{
    box_audio_core_t *core = fl_open(NULL, frequency, FL_LEVEL, false);
    double dry, wet;
    audio_render(core, 0, FL_TO);
    dry = fl_rms();
    box_audio_core_free(core);
    core = fl_open(&p, frequency, FL_LEVEL, false);
    audio_render(core, 0, FL_TO);
    wet = fl_rms();
    box_audio_core_free(core);
    test_assert(dry > 0);

    return 20 * log10(wet > 1e-12 ? wet / dry : 1e-12);
}

/** Check a filter of a slope: -3 dB at its cutoff and the slope an octave past it.
 *
 * @param kind BOX_AUDIO_SYNTH_LOWPASS or BOX_AUDIO_SYNTH_HIGHPASS
 * @param slope A box_audio_synth_slope_t
 * @param octave Expected dB an octave past the cutoff, -12 or -24
 */
static void fl_slope(
    box_audio_synth_filter_t kind,
    box_audio_synth_slope_t slope,
    double octave)
{
    box_audio_filter_params_t p = fl_params(kind, 1000, 0, slope, 1);
    bool low = kind == BOX_AUDIO_SYNTH_LOWPASS;
    double at = fl_gain(p, 1000);
    double past = fl_gain(p, low ? 2000 : 500);
    double pass = fl_gain(p, low ? 100 : 10000);
    printf("%s %d dB at 1 kHz: %+.3f dB there, %+.3f dB an octave past it, %+.3f dB in the "
        "pass band\n", low ? "lowpass" : "highpass", slope ? 24 : 12, at, past, pass);
    test_assert(fabs(at + 3.01) < 0.2);
    test_assert(past < octave + 0.5 && past > octave - 1.5);
    test_assert(fabs(pass) < 0.15);
}

void EffectFilter_lowpass_12(void) {
    // audio/filter
    fl_slope(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_12, -12);
}

void EffectFilter_lowpass_24(void) {
    // audio/filter
    fl_slope(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, -24);
}

void EffectFilter_highpass_12(void) {
    // audio/filter
    fl_slope(BOX_AUDIO_SYNTH_HIGHPASS, BOX_AUDIO_SYNTH_SLOPE_12, -12);
}

void EffectFilter_highpass_24(void) {
    // audio/filter
    fl_slope(BOX_AUDIO_SYNTH_HIGHPASS, BOX_AUDIO_SYNTH_SLOPE_24, -24);
}

void EffectFilter_bandpass(void) {
    // audio/filter
    box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_BANDPASS, 1000, 0.5f,
        BOX_AUDIO_SYNTH_SLOPE_12, 1);
    double center = fl_gain(p, 1000), below = fl_gain(p, 250), above = fl_gain(p, 4000);
    printf("bandpass at 1 kHz: %+.3f dB there, %+.3f dB at 250 Hz, %+.3f dB at 4 kHz\n",
        center, below, above);
    test_assert(fabs(center) < 0.2);
    test_assert(below < -10);
    test_assert(above < -10);
}

void EffectFilter_notch(void) {
    // audio/filter
    box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_NOTCH, 1000, 0,
        BOX_AUDIO_SYNTH_SLOPE_12, 1);
    double center = fl_gain(p, 1000), below = fl_gain(p, 250), above = fl_gain(p, 4000);
    double steep;
    p.slope = (float)BOX_AUDIO_SYNTH_SLOPE_24;
    steep = fl_gain(p, 1000);
    printf("notch at 1 kHz: %+.2f dB there, %+.2f dB at 24 dB, %+.3f dB at 250 Hz, %+.3f dB "
        "at 4 kHz\n", center, steep, below, above);
    test_assert(center < -40);
    test_assert(steep < -40);
    test_assert(below > -1 && below < 0.1);
    test_assert(above > -1 && above < 0.1);
}

void EffectFilter_stable(void) {
    // audio/filter
    box_audio_filter_params_t extremes[] = {
        { 1e30f, 1e30f, 1e30f, 1e30f, 1e30f },
        { BOX_AUDIO_SYNTH_LOWPASS, AUDIO_RATE * 0.5f, 1, BOX_AUDIO_SYNTH_SLOPE_24, 1 },
        { -1e30f, -1e30f, -1e30f, -1e30f, -1e30f },
        { NAN, NAN, NAN, NAN, NAN },
        { BOX_AUDIO_SYNTH_BANDPASS, INFINITY, 1, -INFINITY, INFINITY },
        { BOX_AUDIO_SYNTH_HIGHPASS, 10, 1, BOX_AUDIO_SYNTH_SLOPE_12, 1 },
        { BOX_AUDIO_SYNTH_NOTCH, 1e9f, 1, BOX_AUDIO_SYNTH_SLOPE_24, 0.5f },
        { BOX_AUDIO_SYNTH_LOWPASS, 10, 1, BOX_AUDIO_SYNTH_SLOPE_24, 1 }
    };
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t i, block, frames = AUDIO_RATE / 4;
    float peak = 0;
    srand(13);
    for (i = 0; i < AUDIO_RATE; i ++) {
        fl_noise[i] = (float)rand() / (float)RAND_MAX * 2 - 1;
    }

    audio_order(core, routes, 2);
    test_assert(box_audio_core_sound(core, 2, fl_noise, AUDIO_RATE, 1, AUDIO_RATE));
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.001f);
    audio_play(core, 2, 2, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (block = 0; block < 48; block ++) {
        fl_command(core, 0, 1, extremes[block % 8]);
        audio_render(core, 0, frames);
        for (i = 0; i < frames; i ++) {
            test_assert(isfinite(audio_l[i]) && isfinite(audio_r[i]));
            peak = fabsf(audio_l[i]) > peak ? fabsf(audio_l[i]) : peak;
            peak = fabsf(audio_r[i]) > peak ? fabsf(audio_r[i]) : peak;
        }
    }

    fl_command(core, 0, 1, extremes[1]);
    for (block = 0; block < 8; block ++) {
        audio_render(core, 0, frames);
        for (i = 0; i < frames; i ++) {
            test_assert(isfinite(audio_l[i]) && isfinite(audio_r[i]));
            peak = fabsf(audio_l[i]) > peak ? fabsf(audio_l[i]) : peak;
        }
    }

    printf("peak of 14 seconds of noise at extreme values: %.4f\n", (double)peak);
    test_assert(peak > 0);
    test_assert(peak < BOX_AUDIO_LIMITER_THRESHOLD);
    box_audio_core_free(core);
}

void EffectFilter_cutoff_step_no_click(void) {
    // audio/filter
    box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_LOWPASS, 150, 0,
        BOX_AUDIO_SYNTH_SLOPE_24, 1);
    float level = 0.2f, slope = (float)(2 * AUDIO_PI * 440 / AUDIO_RATE), before, after;
    box_audio_core_t *core = fl_open(&p, 440, level, true);
    audio_render(core, 0, AUDIO_RATE / 2);
    before = audio_max_step(audio_l, AUDIO_RATE / 4, AUDIO_RATE / 2);
    p.cutoff = 8000;
    fl_command(core, 0, 1, p);
    audio_render(core, AUDIO_RATE / 2, AUDIO_RATE / 2);
    after = audio_max_step(audio_l, AUDIO_RATE / 2, AUDIO_RATE);
    printf("largest step: %.5f before, %.5f after a step of the cutoff from 150 Hz to 8 kHz, "
        "%.5f for the open sine\n", (double)before, (double)after, (double)(level * slope));
    test_assert(before < level * slope * 0.2f);
    test_assert(after < level * slope * 1.25f);
    test_assert(audio_peak(audio_l, AUDIO_RATE * 3 / 4, AUDIO_RATE) > level * 0.95f);
    box_audio_core_free(core);
}

void EffectFilter_sweep_no_click(void) {
    // audio/filter
    box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_HIGHPASS, 20, 0,
        BOX_AUDIO_SYNTH_SLOPE_12, 1);
    float level = 0.2f, slope = (float)(2 * AUDIO_PI * 440 / AUDIO_RATE), step;
    box_audio_core_t *core = fl_open(&p, 440, level, true);
    int32_t frame, frames = AUDIO_RATE / AUDIO_FRAME;
    for (frame = 0; frame < frames; frame ++) {
        float t = (float)frame / (float)frames;
        p.cutoff = 20 * powf(200, t);
        fl_command(core, 0, 1, p);
        audio_render(core, frame * AUDIO_FRAME, AUDIO_FRAME);
    }

    step = audio_max_step(audio_l, AUDIO_FRAME, AUDIO_RATE);
    printf("highpass swept from 20 Hz to 4 kHz every frame: largest step %.5f, %.5f for the "
        "open sine, %.5f at the end\n", (double)step, (double)(level * slope),
        (double)audio_peak(audio_l, AUDIO_RATE - 4800, AUDIO_RATE));
    test_assert(step < level * slope * 1.25f);
    test_assert(audio_peak(audio_l, AUDIO_RATE - 4800, AUDIO_RATE) < level * 0.1f);
    box_audio_core_free(core);
}

void EffectFilter_kind_change_no_click(void) {
    // audio/filter
    box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_LOWPASS, 2000, 0.3f,
        BOX_AUDIO_SYNTH_SLOPE_12, 1);
    float level = 0.2f, slope = (float)(2 * AUDIO_PI * 440 / AUDIO_RATE), after;
    box_audio_core_t *core = fl_open(&p, 440, level, true);
    audio_render(core, 0, AUDIO_RATE / 2);
    p.kind = (float)BOX_AUDIO_SYNTH_HIGHPASS;
    p.slope = (float)BOX_AUDIO_SYNTH_SLOPE_24;
    fl_command(core, 0, 1, p);
    audio_render(core, AUDIO_RATE / 2, AUDIO_RATE / 2);
    after = audio_max_step(audio_l, AUDIO_RATE / 2, AUDIO_RATE);
    printf("largest step after a change of the kind and the slope: %.5f, %.5f for the "
        "sine\n", (double)after, (double)(level * slope));
    test_assert(after < level * slope * 1.25f);
    test_assert(audio_peak(audio_l, AUDIO_RATE * 3 / 4, AUDIO_RATE) < level * 0.1f);
    box_audio_core_free(core);
}

void EffectFilter_mix(void) {
    // audio/filter
    box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_LOWPASS, 100, 0,
        BOX_AUDIO_SYNTH_SLOPE_24, 0);
    box_audio_core_t *a, *b;
    double half;
    int32_t i;
    a = fl_open(&p, 2000, 0.2f, true);
    audio_render(a, 0, AUDIO_RATE / 2);
    memcpy(fl_a_l, audio_l, sizeof(float) * (AUDIO_RATE / 2));
    memcpy(fl_a_r, audio_r, sizeof(float) * (AUDIO_RATE / 2));
    b = fl_open(NULL, 2000, 0.2f, true);
    audio_render(b, 0, AUDIO_RATE / 2);
    for (i = 0; i < AUDIO_RATE / 2; i ++) {
        test_assert(fl_a_l[i] == audio_l[i] && fl_a_r[i] == audio_r[i]);
    }

    box_audio_core_free(a);
    box_audio_core_free(b);
    p.mix = 0.5f;
    half = fl_gain(p, 2000);
    printf("half wet lowpass far under the sine: %+.3f dB\n", half);
    test_assert(fabs(half + 6.02) < 0.2);
}

/** Render frames into fl_b_l and fl_b_r from a frame on, in blocks of a size.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 * @param size Frames of a block
 */
static void fl_render_blocks(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames,
    int32_t size)
{
    int32_t i;
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, fl_b_l + at + i, fl_b_r + at + i, n);
    }
}

void EffectFilter_render_sizes(void) {
    // audio/filter
    static const int32_t sizes[] = { 128, 1, 7, 64, 333 };
    uint64_t first = 0;
    int32_t s, half = AUDIO_RATE / 4;
    for (s = 0; s < 5; s ++) {
        box_audio_filter_params_t p = fl_params(BOX_AUDIO_SYNTH_LOWPASS, 300, 0.6f,
            BOX_AUDIO_SYNTH_SLOPE_24, 1);
        box_audio_core_t *core = fl_open(&p, 700, 0.2f, true);
        fl_render_blocks(core, 0, half, sizes[s]);
        p.cutoff = 5000;
        p.kind = (float)BOX_AUDIO_SYNTH_BANDPASS;
        fl_command(core, 0, 1, p);
        fl_render_blocks(core, half, half, sizes[s]);
        if (!s) {
            first = audio_hash(fl_b_l, half * 2);
        } else {
            test_assert(audio_hash(fl_b_l, half * 2) == first);
        }

        box_audio_core_free(core);
    }
}

void EffectFilter_component(void) {
    // audio/filter
    ecs_world_t *world = audio_world();
    ecs_entity_t music = audio_bus(world, "music", 0, 1);
    ecs_entity_t filter = ecs_entity(world, { .name = "music_filter" });
    ecs_entity_t tone = audio_tone(world, "high", 3000, 0.2f, AUDIO_RATE);
    const AudioFilter *values;
    double dry = 0, wet = 0;
    int32_t i, start;
    audio_source(world, tone, 1, true, music);
    audio_run(world, 30);
    start = audio_tap_count - AUDIO_RATE / 4;
    for (i = start; i < audio_tap_count; i ++) {
        dry += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    ecs_add(world, filter, AudioFilter);
    values = ecs_get(world, filter, AudioFilter);
    test_int(values->kind, SynthLowPass);
    test_flt(values->cutoff, 1000);
    test_flt(values->resonance, 0);
    test_int(values->slope, SynthSlope12);
    test_flt(values->mix, 1);
    ecs_set(world, filter, AudioFilter, { .kind = SynthLowPass, .cutoff = 375,
        .slope = SynthSlope24, .mix = 1 });
    audio_insert(world, music, 0, filter);
    audio_run(world, 30);
    test_int(box_audio_module_effect_slot(world, filter), 0);
    start = audio_tap_count - AUDIO_RATE / 4;
    for (i = start; i < audio_tap_count; i ++) {
        wet += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    printf("a 3 kHz tone through a 24 dB lowpass at 375 Hz: %.2f dB\n",
        10 * log10(wet / dry));
    test_assert(dry > 0);
    test_assert(10 * log10(wet / dry) < -60);
    ecs_fini(world);
}
