/**
 * @file FrequencyShifter.c
 * @brief The frequency shifter, measured with sines in offline renders: a shift up and down and
 * its image across the band, the ring mode, the stereo mirror, its stability at full feedback,
 * a shift of 0, the dry mix, changes of the shift and the mode, render sizes, its rest after a
 * tail and the component in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds */
#define FS_FRAMES (AUDIO_RATE * 2)

/** First frame and frames of the window a case measures in, half a second after the
 * allpasses settled: whole periods of every even frequency in Hz */
#define FS_FROM (AUDIO_RATE / 2)
#define FS_COUNT (AUDIO_RATE / 2)

/** Output of the last render */
static float fs_l[FS_FRAMES], fs_r[FS_FRAMES];

/** Output of a second render that a case compares */
static float fs_b_l[FS_FRAMES], fs_b_r[FS_FRAMES];

/** The input sound */
static float fs_sound[FS_FRAMES];

/** The frame of silence that a core renders before its sound plays */
static float fs_silence[2];

/** Level in dB of a linear level.
 *
 * @param x The level
 * @return The level, -300 for silence
 */
static double fs_db(
    double x)
{
    x = x < 0 ? -x : x;

    return x > 1e-15 ? 20 * log10(x) : -300;
}

/** The parameters of a frequency shifter.
 *
 * @param shift Hz
 * @param mode A box_audio_frequency_shifter_mode_t
 * @param stereo 0..1
 * @param feedback -0.95..0.95
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_frequency_shifter_params_t fs_params(
    float shift,
    int32_t mode,
    float stereo,
    float feedback,
    float mix)
{
    return (box_audio_frequency_shifter_params_t){ shift, (float)mode, stereo, feedback, mix };
}

/** Put a frequency shifter into insert slot 0 of bus 1 as effect 0, or change it.
 *
 * @param core The core
 * @param p The parameters
 */
static void fs_command(
    box_audio_core_t *core,
    box_audio_frequency_shifter_params_t p)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_FREQUENCY_SHIFTER, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** A new core with bus 1 into the master.
 *
 * @return The core
 */
static box_audio_core_t* fs_core(void)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    audio_order(core, routes, 2);

    return core;
}

/** Render a frame of silence so that the effects put on before enter at once, and play
 * fs_sound on bus 1 as sound 1.
 *
 * @param core The core
 * @param frames Frames of fs_sound
 * @param loop Whether it loops
 */
static void fs_play(
    box_audio_core_t *core,
    int32_t frames,
    bool loop)
{
    box_audio_core_render(core, fs_silence, fs_silence + 1, 1);
    test_assert(box_audio_core_sound(core, 1, fs_sound, frames, 1, AUDIO_RATE));
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
static void fs_render(
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

/** Render a looping sine of a level through a frequency shifter on bus 1, or without one.
 *
 * @param p The parameters, or NULL for no shifter
 * @param frequency Hz, a whole number so that the loop of a second has whole periods
 * @param level Peak level
 * @param frames Frames to render into fs_l and fs_r
 */
static void fs_tone(
    const box_audio_frequency_shifter_params_t *p,
    double frequency,
    float level,
    int32_t frames)
{
    box_audio_core_t *core = fs_core();
    if (p) {
        fs_command(core, *p);
    }

    audio_sine(fs_sound, AUDIO_RATE, frequency, level);
    fs_play(core, AUDIO_RATE, true);
    fs_render(core, fs_l, fs_r, 0, frames);
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
static double fs_amplitude(
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

/** RMS of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames
 * @return The RMS
 */
static double fs_rms(
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

/** Check that a sine shifted by a shift lands on its frequency plus the shift, with its image
 * at the frequency minus the shift more than 40 dB under it, on both channels.
 *
 * @param frequency Hz of the sine, even
 * @param shift Hz, even
 */
static void fs_check_shift(
    double frequency,
    float shift)
{
    box_audio_frequency_shifter_params_t p = fs_params(shift, BOX_AUDIO_FREQUENCY_SHIFT, 0,
        0, 1);
    double peak, image, carrier;
    fs_tone(&p, frequency, 0.5f, FS_FROM + FS_COUNT);
    peak = fs_amplitude(fs_l, FS_FROM, FS_COUNT, frequency + shift);
    image = fs_amplitude(fs_l, FS_FROM, FS_COUNT, frequency - shift);
    carrier = fs_amplitude(fs_l, FS_FROM, FS_COUNT, frequency);
    printf("%.0f Hz shifted by %+.0f Hz: %.4f at %.0f Hz, image %.1f dB, carrier %.1f dB\n",
        frequency, (double)shift, peak, frequency + shift, fs_db(image / peak),
        fs_db(carrier / peak));
    test_assert(peak > 0.45 && peak < 0.55);
    test_assert(fs_db(image / peak) < -40);
    test_assert(fs_db(carrier / peak) < -40);
    test_assert(fabs(fs_amplitude(fs_r, FS_FROM, FS_COUNT, frequency + shift) - peak) < 1e-6);
}

void FrequencyShifter_shift_up(void) {
    // audio/frequency_shifter
    fs_check_shift(1000, 100);
}

void FrequencyShifter_shift_down(void) {
    // audio/frequency_shifter
    fs_check_shift(1000, -100);
}

void FrequencyShifter_image_across_band(void) {
    // audio/frequency_shifter
    const double frequencies[] = { 200, 500, 1000, 3000, 6000, 10000, 15000, 18000 };
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(frequencies) / sizeof(frequencies[0])); i ++) {
        fs_check_shift(frequencies[i], 100);
    }

    fs_check_shift(1000, 2000);
    fs_check_shift(8000, -4000);
}

void FrequencyShifter_ring(void) {
    // audio/frequency_shifter
    box_audio_frequency_shifter_params_t p = fs_params(100, BOX_AUDIO_FREQUENCY_RING, 0, 0, 1);
    double lo, hi, carrier;
    fs_tone(&p, 1000, 0.5f, FS_FROM + FS_COUNT);
    lo = fs_amplitude(fs_l, FS_FROM, FS_COUNT, 900);
    hi = fs_amplitude(fs_l, FS_FROM, FS_COUNT, 1100);
    carrier = fs_amplitude(fs_l, FS_FROM, FS_COUNT, 1000);
    printf("ring at 100 Hz: %.4f at 900 Hz, %.4f at 1100 Hz, carrier %.1f dB\n", lo, hi,
        fs_db(carrier / hi));
    test_assert(fabs(fs_db(lo / hi)) < 0.1);
    test_assert(hi > 0.2 && hi < 0.3);
    test_assert(fs_db(carrier / hi) < -60);

    /* The sign of the shift does not matter */
    p.shift = -100;
    fs_tone(&p, 1000, 0.5f, FS_FROM + FS_COUNT);
    test_assert(fabs(fs_amplitude(fs_l, FS_FROM, FS_COUNT, 900) - lo) < 1e-4);
    test_assert(fabs(fs_amplitude(fs_l, FS_FROM, FS_COUNT, 1100) - hi) < 1e-4);
}

void FrequencyShifter_stereo_opposite(void) {
    // audio/frequency_shifter
    box_audio_frequency_shifter_params_t p = fs_params(100, BOX_AUDIO_FREQUENCY_SHIFT, 1, 0, 1);
    double l_up, l_down, r_up, r_down;
    fs_tone(&p, 1000, 0.5f, FS_FROM + FS_COUNT);
    l_up = fs_amplitude(fs_l, FS_FROM, FS_COUNT, 1100);
    l_down = fs_amplitude(fs_l, FS_FROM, FS_COUNT, 900);
    r_up = fs_amplitude(fs_r, FS_FROM, FS_COUNT, 1100);
    r_down = fs_amplitude(fs_r, FS_FROM, FS_COUNT, 900);
    printf("stereo 1: left %.4f up %.4f down, right %.4f up %.4f down\n", l_up, l_down, r_up,
        r_down);
    test_assert(l_up > 0.45);
    test_assert(r_down > 0.45);
    test_assert(fs_db(l_down / l_up) < -40);
    test_assert(fs_db(r_up / r_down) < -40);

    /* Halfway the right channel is not shifted */
    p.stereo = 0.5f;
    fs_tone(&p, 1000, 0.5f, FS_FROM + FS_COUNT);
    test_assert(fs_amplitude(fs_l, FS_FROM, FS_COUNT, 1100) > 0.45);
    test_assert(fs_amplitude(fs_r, FS_FROM, FS_COUNT, 1000) > 0.45);
}

void FrequencyShifter_feedback_bounded(void) {
    // audio/frequency_shifter
    const float shifts[] = { 0.5f, -0.5f, 100, -100, 3000, -3000, 0 };
    const float feedbacks[] = { 0.95f, -0.95f };
    const float level = 0.02f;
    int32_t s, f, i;
    for (s = 0; s < (int32_t)(sizeof(shifts) / sizeof(shifts[0])); s ++) {
        for (f = 0; f < 2; f ++) {
            box_audio_frequency_shifter_params_t p = fs_params(shifts[s],
                BOX_AUDIO_FREQUENCY_SHIFT, 0.5f, feedbacks[f], 1);
            float peak;
            double early, late;
            fs_tone(&p, 440, level, FS_FRAMES);
            for (i = 0; i < FS_FRAMES; i ++) {
                test_assert(isfinite(fs_l[i]) && isfinite(fs_r[i]));
            }

            peak = audio_peak(fs_l, 0, FS_FRAMES);
            peak = audio_peak(fs_r, 0, FS_FRAMES) > peak ? audio_peak(fs_r, 0, FS_FRAMES) : peak;
            early = fs_rms(fs_l, AUDIO_RATE / 2, AUDIO_RATE / 2);
            late = fs_rms(fs_l, FS_FRAMES - AUDIO_RATE / 2, AUDIO_RATE / 2);
            printf("shift %+.1f Hz, feedback %+.2f: peak %.4f, RMS %.4f then %.4f\n",
                (double)shifts[s], (double)feedbacks[f], (double)peak, early, late);
            test_assert(peak < 25 * level);
            test_assert(late < 2 * early + 1e-6);
        }
    }
}

void FrequencyShifter_zero_passes(void) {
    // audio/frequency_shifter
    const double frequencies[] = { 100, 1000, 5000, 15000 };
    box_audio_frequency_shifter_params_t p = fs_params(0, BOX_AUDIO_FREQUENCY_SHIFT, 0, 0, 1);
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(frequencies) / sizeof(frequencies[0])); i ++) {
        double dry, wet;
        fs_tone(NULL, frequencies[i], 0.5f, FS_FROM + FS_COUNT);
        dry = fs_amplitude(fs_l, FS_FROM, FS_COUNT, frequencies[i]);
        fs_tone(&p, frequencies[i], 0.5f, FS_FROM + FS_COUNT);
        wet = fs_amplitude(fs_l, FS_FROM, FS_COUNT, frequencies[i]);
        printf("shift 0 at %.0f Hz: %+.4f dB\n", frequencies[i], fs_db(wet / dry));
        test_assert(fabs(fs_db(wet / dry)) < 0.05);
        test_assert(fabs(fs_rms(fs_l, FS_FROM, FS_COUNT) - dry / sqrt(2.0)) < 1e-3);
    }
}

void FrequencyShifter_mix_dry(void) {
    // audio/frequency_shifter
    box_audio_frequency_shifter_params_t p = fs_params(250, BOX_AUDIO_FREQUENCY_SHIFT, 1,
        0.5f, 0);
    const int32_t frames = AUDIO_RATE / 2;
    int32_t i;
    fs_tone(NULL, 440, 0.7f, frames);
    memcpy(fs_b_l, fs_l, sizeof(float) * frames);
    memcpy(fs_b_r, fs_r, sizeof(float) * frames);
    test_assert(audio_peak(fs_b_l, 0, frames) > 0.1f);
    fs_tone(&p, 440, 0.7f, frames);
    for (i = 0; i < frames; i ++) {
        test_assert(fs_l[i] == fs_b_l[i]);
        test_assert(fs_r[i] == fs_b_r[i]);
    }
}

/** Render a sine through a shifter whose params change at a quarter and at half of the
 * render, and compare the largest steps around the changes with the ones in between.
 *
 * @param a The params before and after
 * @param b The params in the middle
 * @param name What changes, for the output
 */
static void fs_check_change(
    box_audio_frequency_shifter_params_t a,
    box_audio_frequency_shifter_params_t b,
    const char *name)
{
    const int32_t seg = AUDIO_RATE / 4, ramp = AUDIO_RATE / 20;
    box_audio_core_t *core = fs_core();
    float steady_a, steady_b, up, down, steady;
    fs_command(core, a);
    audio_sine(fs_sound, AUDIO_RATE, 220, 0.25f);
    fs_play(core, AUDIO_RATE, true);
    fs_render(core, fs_l, fs_r, 0, seg);
    fs_command(core, b);
    fs_render(core, fs_l, fs_r, seg, seg);
    fs_command(core, a);
    fs_render(core, fs_l, fs_r, 2 * seg, seg);
    steady_a = audio_max_step(fs_l, seg / 2, seg);
    steady_b = audio_max_step(fs_l, seg + ramp, 2 * seg);
    up = audio_max_step(fs_l, seg, seg + ramp);
    down = audio_max_step(fs_l, 2 * seg, 2 * seg + ramp);
    steady = steady_a > steady_b ? steady_a : steady_b;
    printf("%s: largest step %.5f and %.5f steady, %.5f and %.5f at the changes\n", name,
        (double)steady_a, (double)steady_b, (double)up, (double)down);
    test_assert(up < steady * 1.1f);
    test_assert(down < steady * 1.1f);
    test_assert(audio_max_step(fs_r, seg, seg + ramp) < audio_max_step(fs_r, seg / 2, seg) *
        1.1f + audio_max_step(fs_r, seg + ramp, 2 * seg) * 1.1f);
    box_audio_core_free(core);
}

void FrequencyShifter_change_no_click(void) {
    // audio/frequency_shifter
    fs_check_change(fs_params(100, BOX_AUDIO_FREQUENCY_SHIFT, 0, 0, 1),
        fs_params(2000, BOX_AUDIO_FREQUENCY_SHIFT, 0, 0, 1), "shift 100 to 2000 Hz");
    fs_check_change(fs_params(300, BOX_AUDIO_FREQUENCY_SHIFT, 0, 0, 1),
        fs_params(-300, BOX_AUDIO_FREQUENCY_SHIFT, 1, 0.5f, 0.5f),
        "shift, stereo, feedback and mix");
}

void FrequencyShifter_mode_no_click(void) {
    // audio/frequency_shifter
    fs_check_change(fs_params(300, BOX_AUDIO_FREQUENCY_SHIFT, 0, 0, 1),
        fs_params(300, BOX_AUDIO_FREQUENCY_RING, 0, 0, 1), "shift to ring");
}

/** Render a shifter on a looping sine in pieces of a size, with a change of its params
 * halfway.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param size Frames of each render, 0 for sizes that vary
 */
static void fs_sizes(
    float *left,
    float *right,
    int32_t size)
{
    box_audio_core_t *core = fs_core();
    int32_t at = 0, k = 0;
    fs_command(core, fs_params(37.5f, BOX_AUDIO_FREQUENCY_SHIFT, 0.7f, 0.6f, 0.8f));
    audio_sine(fs_sound, AUDIO_RATE, 220, 0.4f);
    fs_play(core, AUDIO_RATE, true);
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
            fs_command(core, fs_params(-1200, BOX_AUDIO_FREQUENCY_RING, 0.2f, -0.8f, 1));
        }
    }

    box_audio_core_free(core);
}

void FrequencyShifter_render_sizes(void) {
    // audio/frequency_shifter
    fs_sizes(fs_l, fs_r, BOX_AUDIO_CORE_QUANTUM);
    fs_sizes(fs_b_l, fs_b_r, 0);
    test_assert(audio_hash(fs_l, AUDIO_RATE) == audio_hash(fs_b_l, AUDIO_RATE));
    test_assert(audio_hash(fs_r, AUDIO_RATE) == audio_hash(fs_b_r, AUDIO_RATE));
    test_assert(audio_peak(fs_l, 0, AUDIO_RATE) > 0.01f);
}

void FrequencyShifter_deterministic(void) {
    // audio/frequency_shifter
    fs_sizes(fs_l, fs_r, BOX_AUDIO_CORE_QUANTUM);
    fs_sizes(fs_b_l, fs_b_r, BOX_AUDIO_CORE_QUANTUM);
    test_assert(audio_hash(fs_l, AUDIO_RATE) == audio_hash(fs_b_l, AUDIO_RATE));
    test_assert(audio_hash(fs_r, AUDIO_RATE) == audio_hash(fs_b_r, AUDIO_RATE));
    test_assert(audio_hash(fs_l, AUDIO_RATE) != audio_hash(fs_r, AUDIO_RATE));
}

void FrequencyShifter_silent_after_tail(void) {
    // audio/frequency_shifter
    box_audio_core_t *core = fs_core();
    fs_command(core, fs_params(5, BOX_AUDIO_FREQUENCY_SHIFT, 1, 0.9f, 1));
    audio_sine(fs_sound, AUDIO_RATE / 4, 330, 0.5f);
    box_audio_core_render(core, fs_silence, fs_silence + 1, 1);
    test_assert(!box_audio_core_effect_running(core, 0));
    test_assert(box_audio_core_sound(core, 1, fs_sound, AUDIO_RATE / 4, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, 0);
    fs_render(core, fs_l, fs_r, 0, AUDIO_RATE / 8);
    test_assert(box_audio_core_effect_running(core, 0));
    fs_render(core, fs_l, fs_r, 0, AUDIO_RATE);
    fs_render(core, fs_l, fs_r, 0, AUDIO_RATE);
    fs_render(core, fs_l, fs_r, 0, AUDIO_RATE);
    test_assert(!box_audio_core_effect_running(core, 0));
    test_flt(audio_peak(fs_l, AUDIO_RATE / 2, AUDIO_RATE), 0);

    /* A new sound starts it again */
    audio_play(core, 2, 1, 1, 1, 0, 1, 0);
    fs_render(core, fs_l, fs_r, 0, AUDIO_RATE / 8);
    test_assert(box_audio_core_effect_running(core, 0));
    test_assert(audio_peak(fs_l, 0, AUDIO_RATE / 8) > 0.01f);
    box_audio_core_free(core);
}

void FrequencyShifter_component(void) {
    // audio/frequency_shifter
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    float params[BOX_AUDIO_EFFECT_PARAMS];
    const FrequencyShifter *s;
    ecs_add(world, e, FrequencyShifter);
    s = ecs_get(world, e, FrequencyShifter);
    test_flt(s->shift, 0);
    test_int(s->mode, FrequencyShift);
    test_flt(s->stereo, 0);
    test_flt(s->feedback, 0);
    test_flt(s->mix, 1);
    test_assert(ecs_has(world, e, AudioEffectState));
    ecs_set(world, e, FrequencyShifter, { .shift = -120, .mode = FrequencyRing, .stereo = 0.4f,
        .feedback = 0.6f, .mix = 0.5f });
    test_int(box_audio_module_effect_kind(world, e, params),
        BOX_AUDIO_EFFECT_FREQUENCY_SHIFTER);
    test_flt(params[0], -120);
    test_flt(params[1], BOX_AUDIO_FREQUENCY_RING);
    test_flt(params[2], 0.4f);
    test_flt(params[3], 0.6f);
    test_flt(params[4], 0.5f);
    test_flt(params[5], 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_FREQUENCY_SHIFTER, AUDIO_RATE), 0);
    ecs_fini(world);
}

void FrequencyShifter_script(void) {
    // audio/frequency_shifter
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/frequency_shifter.flecs");
    ecs_entity_t pad, bell;
    const FrequencyShifter *s;
    test_assert(ecs_script_run(world, "frequency_shifter", code, NULL) == 0);
    ecs_os_free(code);
    pad = ecs_lookup(world, "pad");
    bell = ecs_lookup(world, "pad_bell");
    test_assert(pad && bell);
    s = ecs_get(world, bell, FrequencyShifter);
    test_assert(s != NULL);
    test_flt(s->shift, 437);
    test_int(s->mode, FrequencyRing);
    test_flt(s->stereo, 0.5f);
    test_flt(s->feedback, -0.3f);
    test_flt(s->mix, 0.8f);

    audio_source(world, audio_tone(world, NULL, 200, 0.5f, AUDIO_RATE), 1, true, pad);
    audio_run(world, 20);
    test_assert(box_audio_module_effect_slot(world, bell) >= 0);
    test_assert(audio_tap_count > AUDIO_RATE / 10);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count) >
        0.05f);
    ecs_fini(world);
}
