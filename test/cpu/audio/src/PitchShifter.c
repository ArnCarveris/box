/**
 * @file PitchShifter.c
 * @brief The pitch shifter kind of effect in an insert slot of a bus and the shimmer of the
 * reverb, in offline renders: the peak of a shifted sine an octave up and down, the stereo
 * detune, a bounded feedback, the octave in the tail of a shimmer, its bounds at the longest
 * decay, a shimmer of 0 that is bit identical to none, bit identical renders and the
 * PitchShifter component in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, eight seconds at 48 kHz */
#define PS_MAX (AUDIO_RATE * 8)

/** Level of the noise and the tones */
#define PS_LEVEL 0.25f

/** Volume of the bus of the effect, so the limiter of the master never acts */
#define PS_VOLUME 0.1f

/** Output of the last render, and of a second render that a case compares */
static float ps_l[PS_MAX];
static float ps_r[PS_MAX];

/** A sound of a case: a tone or noise */
static float ps_sound[AUDIO_RATE];

/** A core with an effect on bus 1, which outputs to the master */
typedef struct ps_t {
    box_audio_core_t *core;  /**< The core */
    float *memory;           /**< Memory of slot 0 */
} ps_t;

/** The parameters of a pitch shifter.
 *
 * @param semitones Shift
 * @param cents Detune
 * @param stereo 0..1
 * @param window Milliseconds
 * @param feedback 0..0.9
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_pitch_shifter_params_t ps_params(
    float semitones,
    float cents,
    float stereo,
    float window,
    float feedback,
    float mix)
{
    return (box_audio_pitch_shifter_params_t){ semitones, cents, stereo, window, feedback, mix };
}

/** The parameters of a reverb with a shimmer.
 *
 * @param decay Seconds until -60 dB
 * @param damping High frequency loss
 * @param shimmer Share of the feedback that is shifted
 * @param pitch Semitones of the shimmer
 * @return The parameters
 */
static box_audio_reverb_params_t ps_reverb(
    float decay,
    float damping,
    float shimmer,
    float pitch)
{
    return (box_audio_reverb_params_t){ decay, damping, 0.02f, 0.5f, 1, shimmer, pitch };
}

/** Send an effect command into insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param kind Kind
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take the effect off
 * @param p Parameters
 * @param size Bytes of the parameters
 */
static void ps_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    const void *p,
    size_t size)
{
    uint8_t bytes[128];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, p, size);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, kind, bus, 0, BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Fill ps_sound with white noise of a seed, the same on every run.
 *
 * @param frames Frames
 * @param seed Seed
 */
static void ps_noise(
    int32_t frames,
    uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 1;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        x = x * 1664525u + 1013904223u;
        ps_sound[i] = ((float)(x >> 8) / 8388608.0f - 1.0f) * PS_LEVEL;
    }
}

/** Create a core with bus 1 into the master at PS_VOLUME, memory for slot 0 for a kind, and
 * ps_sound of some frames as sound 1, which plays on bus 1.
 *
 * @param ps Receives the core
 * @param kind Kind whose memory slot 0 gets
 * @param frames Frames of the sound
 * @param loop Whether it loops
 */
static void ps_open(
    ps_t *ps,
    uint32_t kind,
    int32_t frames,
    bool loop)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(kind, AUDIO_RATE);
    ps->core = box_audio_core_new(&desc);
    test_assert(ps->core != NULL);
    test_assert(floats > 0);
    ps->memory = malloc((size_t)floats * sizeof(float));
    test_assert(ps->memory != NULL);

    // Memory that is not clear: the shifter never reads what it did not write
    audio_fill(ps->memory, floats, 0.75f);
    test_assert(box_audio_core_effect_memory(ps->core, 0, ps->memory, floats));
    audio_order(ps->core, routes, 2);
    audio_bus_set(ps->core, 1, BOX_AUDIO_PARAM_VOLUME, PS_VOLUME);
    test_assert(box_audio_core_sound(ps->core, 1, ps_sound, frames, 1, AUDIO_RATE));
    audio_play(ps->core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);
}

/** Free the core, then its memory.
 *
 * @param ps The core
 */
static void ps_close(
    ps_t *ps)
{
    box_audio_core_free(ps->core);
    free(ps->memory);
}

/** Render frames into ps_l and ps_r from a frame on, in renders of a size.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 * @param size Frames per render
 */
static void ps_render(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames,
    int32_t size)
{
    int32_t i;
    test_assert(at + frames <= PS_MAX);
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, ps_l + at + i, ps_r + at + i, n);
    }
}

/** Power of a range at a frequency, by the Goertzel algorithm over a Hann window.
 *
 * @param x Samples
 * @param from First frame
 * @param n Frames
 * @param frequency Hz
 * @return The power
 */
static double ps_power(
    const float *x,
    int32_t from,
    int32_t n,
    double frequency)
{
    double c = 2 * cos(2 * AUDIO_PI * frequency / AUDIO_RATE), s0, s1 = 0, s2 = 0;
    int32_t i;
    for (i = 0; i < n; i ++) {
        double w = 0.5 - 0.5 * cos(2 * AUDIO_PI * (double)i / (double)(n - 1));
        s0 = w * (double)x[from + i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/** Find the frequency of the largest power of a range between two frequencies.
 *
 * @param x Samples
 * @param from First frame
 * @param n Frames
 * @param lo Lowest frequency
 * @param hi Highest frequency
 * @param step Hz between two probes
 * @return The frequency of the peak
 */
static double ps_peak(
    const float *x,
    int32_t from,
    int32_t n,
    double lo,
    double hi,
    double step)
{
    double f, best = lo, most = -1;
    for (f = lo; f <= hi; f += step) {
        double p = ps_power(x, from, n, f);
        if (p > most) {
            most = p;
            best = f;
        }
    }

    return best;
}

/** Check that all samples of a range are finite and under a level.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param level The level
 */
static void ps_bounded(
    const float *x,
    int32_t from,
    int32_t to,
    float level)
{
    int32_t i;
    for (i = from; i < to; i ++) {
        test_assert(isfinite(x[i]));
    }

    test_assert(audio_peak(x, from, to) < level);
}

/** Shift a 440 Hz sine at a mix of 1 and measure the peak of the left channel, and the powers
 * at the peak that is expected and at 440 Hz.
 *
 * @param semitones Shift
 * @param expect Frequency expected
 * @param ratio Receives the power at expect over the power at 440 Hz
 * @return The frequency of the peak
 */
static double ps_shift_sine(
    float semitones,
    double expect,
    double *ratio)
{
    ps_t ps;
    box_audio_pitch_shifter_params_t p = ps_params(semitones, 0, 0, 40, 0, 1);
    int32_t from = AUDIO_RATE / 2, n = 32768;
    double peak;
    audio_sine(ps_sound, AUDIO_RATE, 440, PS_LEVEL);
    ps_open(&ps, BOX_AUDIO_EFFECT_PITCH_SHIFTER, AUDIO_RATE, true);
    ps_command(ps.core, 0, BOX_AUDIO_EFFECT_PITCH_SHIFTER, 1, &p, sizeof(p));
    ps_render(ps.core, 0, AUDIO_RATE * 2, BOX_AUDIO_CORE_QUANTUM);
    peak = ps_peak(ps_l, from, n, 100, 2000, 1);
    *ratio = ps_power(ps_l, from, n, expect) / ps_power(ps_l, from, n, 440);
    printf("shift %+.0f: peak at %.0f Hz, %.1f dB over 440 Hz\n", (double)semitones, peak,
        10 * log10(*ratio));
    ps_bounded(ps_l, 0, AUDIO_RATE * 2, 0.1f);
    ps_close(&ps);

    return peak;
}

void PitchShifter_octave_up(void) {
    // audio/pitch_shifter
    double ratio, peak = ps_shift_sine(12, 880, &ratio);
    test_assert(fabs(peak - 880) <= 3);
    test_assert(ratio >= 100);
}

void PitchShifter_octave_down(void) {
    // audio/pitch_shifter
    double ratio, peak = ps_shift_sine(-12, 220, &ratio);
    test_assert(fabs(peak - 220) <= 3);
    test_assert(ratio >= 100);
}

void PitchShifter_stereo_detune(void) {
    // audio/pitch_shifter
    ps_t ps;
    box_audio_pitch_shifter_params_t p = ps_params(0, 50, 1, 40, 0, 1);
    int32_t from = AUDIO_RATE / 2, n = AUDIO_RATE;
    double up = 440 * pow(2, 50.0 / 1200), down = 440 * pow(2, -50.0 / 1200), pl, pr;
    audio_sine(ps_sound, AUDIO_RATE, 440, PS_LEVEL);
    ps_open(&ps, BOX_AUDIO_EFFECT_PITCH_SHIFTER, AUDIO_RATE, true);
    ps_command(ps.core, 0, BOX_AUDIO_EFFECT_PITCH_SHIFTER, 1, &p, sizeof(p));
    ps_render(ps.core, 0, AUDIO_RATE * 2, BOX_AUDIO_CORE_QUANTUM);
    pl = ps_peak(ps_l, from, n, 400, 480, 0.5);
    pr = ps_peak(ps_r, from, n, 400, 480, 0.5);
    printf("detune 50 cents, stereo 1: left %.1f Hz (%.1f), right %.1f Hz (%.1f)\n", pl, up,
        pr, down);
    test_assert(fabs(pl - up) <= 2);
    test_assert(fabs(pr - down) <= 2);

    // A stereo of 0 shifts both channels the same way
    p = ps_params(0, 50, 0, 40, 0, 1);
    ps_command(ps.core, 0, BOX_AUDIO_EFFECT_PITCH_SHIFTER, 1, &p, sizeof(p));
    ps_render(ps.core, 0, AUDIO_RATE * 2, BOX_AUDIO_CORE_QUANTUM);
    pl = ps_peak(ps_l, from, n, 400, 480, 0.5);
    pr = ps_peak(ps_r, from, n, 400, 480, 0.5);
    test_assert(fabs(pl - up) <= 2);
    test_assert(fabs(pr - up) <= 2);
    ps_close(&ps);
}

void PitchShifter_feedback_bounded(void) {
    // audio/pitch_shifter
    const float semitones[] = { 0, 12, -12, 7, 24, -24 };
    const float windows[] = { 10, 40, 100 };
    int32_t s, w, k;
    for (k = 0; k < 2; k ++) {
        for (s = 0; s < 6; s ++) {
            for (w = 0; w < 3; w ++) {
                ps_t ps;
                box_audio_pitch_shifter_params_t p = ps_params(semitones[s], 0, 0, windows[w],
                    0.9f, 1);
                float late, early;
                if (k) {
                    ps_noise(AUDIO_RATE, 3);
                } else {
                    audio_sine(ps_sound, AUDIO_RATE, 440, PS_LEVEL);
                }

                ps_open(&ps, BOX_AUDIO_EFFECT_PITCH_SHIFTER, AUDIO_RATE, true);
                ps_command(ps.core, 0, BOX_AUDIO_EFFECT_PITCH_SHIFTER, 1, &p, sizeof(p));
                ps_render(ps.core, 0, AUDIO_RATE * 4, BOX_AUDIO_CORE_QUANTUM);

                // The wet signal stays under that of the soft clipped feedback, at a bus
                // volume of PS_VOLUME, and does not grow over the render
                ps_bounded(ps_l, 0, AUDIO_RATE * 4, 0.35f);
                ps_bounded(ps_r, 0, AUDIO_RATE * 4, 0.35f);
                early = audio_peak(ps_l, AUDIO_RATE, AUDIO_RATE * 2);
                late = audio_peak(ps_l, AUDIO_RATE * 3, AUDIO_RATE * 4);
                test_assert(late <= early * 1.5f + 1e-6f);
                ps_close(&ps);
            }
        }
    }
}

/** Play 440 Hz for half a second into a reverb with a shimmer and get the powers of its late
 * tail, from 2 to about 2.7 seconds after the tone, at 440 and 880 Hz.
 *
 * @param shimmer Share of the feedback that is shifted
 * @param p440 Receives the power at 440 Hz
 * @param p880 Receives the power at 880 Hz
 */
static void ps_shimmer_tail(
    float shimmer,
    double *p440,
    double *p880)
{
    ps_t ps;
    box_audio_reverb_params_t p = ps_reverb(6, 0.2f, shimmer, 12);
    int32_t from = AUDIO_RATE * 5 / 2, n = 32768;
    audio_sine(ps_sound, AUDIO_RATE / 2, 440, PS_LEVEL);
    ps_open(&ps, BOX_AUDIO_EFFECT_REVERB, AUDIO_RATE / 2, false);
    ps_command(ps.core, 0, BOX_AUDIO_EFFECT_REVERB, 1, &p, sizeof(p));
    ps_render(ps.core, 0, AUDIO_RATE * 4, BOX_AUDIO_CORE_QUANTUM);
    *p440 = ps_power(ps_l, from, n, 440) + ps_power(ps_r, from, n, 440);
    *p880 = ps_power(ps_l, from, n, 880) + ps_power(ps_r, from, n, 880);
    printf("shimmer %.2f: late tail %.1f dB at 880 Hz against 440 Hz\n", (double)shimmer,
        10 * log10(*p880 / *p440));
    ps_bounded(ps_l, 0, AUDIO_RATE * 4, 0.2f);
    ps_close(&ps);
}

void PitchShifter_shimmer_octave(void) {
    // audio/reverb
    double a440, a880, b440, b880;
    ps_shimmer_tail(0, &a440, &a880);
    ps_shimmer_tail(0.5f, &b440, &b880);

    // The shimmer puts energy an octave up into the late tail, which the plain reverb lacks
    test_assert(b880 > 10 * a880);
    test_assert(b880 / b440 > 10 * (a880 / a440));
    test_assert(b880 > 1e-3 * b440);
}

void PitchShifter_shimmer_bounded(void) {
    // audio/reverb
    const float shimmers[] = { 0.5f, 1, 1, 1, 1 };
    const float pitches[] = { 12, 12, 0, -12, 24 };
    int32_t s, d, i;
    ps_noise(AUDIO_RATE / 4, 5);
    for (d = 0; d < 2; d ++) {
        for (s = 0; s < 5; s ++) {
            ps_t ps;
            box_audio_reverb_params_t p = ps_reverb(BOX_AUDIO_REVERB_DECAY_MAX,
                d ? 0.5f : 0, shimmers[s], pitches[s]);
            double early, late;
            ps_open(&ps, BOX_AUDIO_EFFECT_REVERB, AUDIO_RATE / 4, false);
            ps_command(ps.core, 0, BOX_AUDIO_EFFECT_REVERB, 1, &p, sizeof(p));
            ps_render(ps.core, 0, PS_MAX, BOX_AUDIO_CORE_QUANTUM);
            ps_bounded(ps_l, 0, PS_MAX, 0.2f);
            ps_bounded(ps_r, 0, PS_MAX, 0.2f);
            early = 0;
            late = 0;
            for (i = AUDIO_RATE; i < AUDIO_RATE * 2; i ++) {
                early += (double)ps_l[i] * ps_l[i] + (double)ps_r[i] * ps_r[i];
            }

            for (i = PS_MAX - AUDIO_RATE; i < PS_MAX; i ++) {
                late += (double)ps_l[i] * ps_l[i] + (double)ps_r[i] * ps_r[i];
            }

            printf("shimmer %.1f pitch %+.0f damping %.1f at the longest decay: last second "
                "%.1f dB against the second\n", (double)shimmers[s], (double)pitches[s],
                d ? 0.5 : 0.0, 10 * log10(late / early));
            test_assert(late <= early);
            ps_close(&ps);
        }
    }
}

/** Render a click train and a tone through a reverb of some params and hash the output.
 *
 * @param params The params of the command, BOX_AUDIO_EFFECT_PARAMS floats
 * @return The hash of both channels
 */
static uint64_t ps_reverb_hash(
    const float *params)
{
    ps_t ps;
    uint64_t result;
    int32_t i;
    audio_sine(ps_sound, AUDIO_RATE / 2, 330, PS_LEVEL);
    for (i = 0; i < AUDIO_RATE / 2; i += 4801) {
        ps_sound[i] = 1;
    }

    ps_open(&ps, BOX_AUDIO_EFFECT_REVERB, AUDIO_RATE / 2, false);
    ps_command(ps.core, 0, BOX_AUDIO_EFFECT_REVERB, 1, params,
        BOX_AUDIO_EFFECT_PARAMS * sizeof(float));
    ps_render(ps.core, 0, AUDIO_RATE * 3, BOX_AUDIO_CORE_QUANTUM);
    result = audio_hash(ps_l, AUDIO_RATE * 3) ^ (audio_hash(ps_r, AUDIO_RATE * 3) * 31);
    ps_close(&ps);

    return result;
}

void PitchShifter_shimmer_zero_identical(void) {
    // audio/reverb
    float params[BOX_AUDIO_EFFECT_PARAMS] = { 2.5f, 0.4f, 0.03f, 0.6f, 1 };
    uint64_t plain, zero, other, on;

    // The params of a reverb from before the shimmer, the rest 0
    plain = ps_reverb_hash(params);
    params[5] = 0;
    params[6] = 12;
    zero = ps_reverb_hash(params);
    params[6] = -7;
    other = ps_reverb_hash(params);
    params[5] = 0.5f;
    params[6] = 12;
    on = ps_reverb_hash(params);
    test_assert(plain == zero);
    test_assert(plain == other);
    test_assert(plain != on);
}

/** Render looping noise through a pitch shifter on bus 1 and a sine through a reverb with a
 * shimmer on bus 2, whose parameters change, in renders of a size, and hash the output.
 *
 * The reverb keeps its decay and damping, whose gains it smooths once per render; its shimmer
 * moves frame by frame.
 *
 * @param size Frames per render
 * @return The hash of both channels
 */
static uint64_t ps_sized(
    int32_t size)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    int32_t a = box_audio_effect_floats(BOX_AUDIO_EFFECT_PITCH_SHIFTER, AUDIO_RATE);
    int32_t b = box_audio_effect_floats(BOX_AUDIO_EFFECT_REVERB, AUDIO_RATE), k;
    float *memory[2] = { malloc((size_t)a * sizeof(float)), malloc((size_t)b * sizeof(float)) };
    box_audio_core_t *core = box_audio_core_new(&desc);
    uint64_t result;
    test_assert(core != NULL && memory[0] != NULL && memory[1] != NULL);
    audio_fill(memory[0], a, 0.75f);
    audio_fill(memory[1], b, 0.75f);
    test_assert(box_audio_core_effect_memory(core, 0, memory[0], a));
    test_assert(box_audio_core_effect_memory(core, 1, memory[1], b));
    audio_order(core, routes, 3);
    ps_noise(AUDIO_RATE / 2, 11);
    audio_sine(ps_sound + AUDIO_RATE / 2, AUDIO_RATE / 2, 220, PS_LEVEL);
    test_assert(box_audio_core_sound(core, 1, ps_sound, AUDIO_RATE / 2, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, ps_sound + AUDIO_RATE / 2, AUDIO_RATE / 2, 1,
        AUDIO_RATE));
    audio_play(core, 1, 1, 1, 0.5f, -0.4f, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 2, 0.5f, 0.6f, 1, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 8; k ++) {
        box_audio_pitch_shifter_params_t p = ps_params(-12 + 5 * (float)k,
            k % 2 ? 15 : -30, 0.15f * (float)k, 10 + 12 * (float)k, 0.1f * (float)k, 0.7f);
        box_audio_reverb_params_t r = ps_reverb(3, 0.3f, 0.15f * (float)(k % 7),
            k % 3 ? 12 : 7);
        ps_command(core, 0, BOX_AUDIO_EFFECT_PITCH_SHIFTER, 1, &p, sizeof(p));
        ps_command(core, 1, BOX_AUDIO_EFFECT_REVERB, 2, &r, sizeof(r));
        ps_render(core, k * (AUDIO_RATE / 4), AUDIO_RATE / 4, size);
    }

    box_audio_core_free(core);
    free(memory[0]);
    free(memory[1]);
    result = audio_hash(ps_l, AUDIO_RATE * 2) ^ (audio_hash(ps_r, AUDIO_RATE * 2) * 31);

    return result;
}

void PitchShifter_render_identical(void) {
    // audio/pitch_shifter
    uint64_t a = ps_sized(BOX_AUDIO_CORE_QUANTUM), b = ps_sized(BOX_AUDIO_CORE_QUANTUM);
    uint64_t c = ps_sized(100), d = ps_sized(1000), e = ps_sized(1);
    test_assert(a == b);
    test_assert(a == c);
    test_assert(a == d);
    test_assert(a == e);
}

void PitchShifter_silent_after_tail(void) {
    // audio/pitch_shifter
    ps_t ps;
    box_audio_pitch_shifter_params_t p = ps_params(7, 0, 0, 40, 0.6f, 0.5f);
    audio_sine(ps_sound, AUDIO_RATE / 4, 440, PS_LEVEL);
    ps_open(&ps, BOX_AUDIO_EFFECT_PITCH_SHIFTER, AUDIO_RATE / 4, false);
    ps_command(ps.core, 0, BOX_AUDIO_EFFECT_PITCH_SHIFTER, 1, &p, sizeof(p));
    ps_render(ps.core, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
    test_assert(box_audio_core_effect_running(ps.core, 0));
    test_assert(audio_peak(ps_l, 0, AUDIO_RATE / 8) > 0.001f);

    // The tone ends and the cascade of the feedback decays to silence
    ps_render(ps.core, 0, AUDIO_RATE * 4, BOX_AUDIO_CORE_QUANTUM);
    test_assert(!box_audio_core_effect_running(ps.core, 0));
    ps_render(ps.core, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    test_flt(audio_peak(ps_l, 0, AUDIO_RATE), 0);
    test_flt(audio_peak(ps_r, 0, AUDIO_RATE), 0);

    // It starts again on the next sound
    audio_play(ps.core, 2, 1, 1, 1, 0, 1, 0);
    ps_render(ps.core, 0, AUDIO_RATE / 8, BOX_AUDIO_CORE_QUANTUM);
    test_assert(box_audio_core_effect_running(ps.core, 0));
    test_assert(audio_peak(ps_l, 0, AUDIO_RATE / 8) > 0.001f);
    ps_close(&ps);
}

void PitchShifter_component(void) {
    // audio/pitch_shifter
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world), f = ecs_new(world);
    const PitchShifter *ps;
    const Reverb *rv;
    float params[BOX_AUDIO_EFFECT_PARAMS];
    int32_t length;
    ecs_add(world, e, PitchShifter);
    ps = ecs_get(world, e, PitchShifter);
    test_flt(ps->semitones, 0);
    test_flt(ps->cents, 0);
    test_flt(ps->stereo, 0);
    test_flt(ps->window, 40);
    test_flt(ps->feedback, 0);
    test_flt(ps->mix, 0.5f);
    test_assert(ecs_has(world, e, AudioEffectState));
    ecs_set(world, e, PitchShifter, { .semitones = 12, .cents = -8, .stereo = 1, .window = 30,
        .feedback = 0.25f, .mix = 0.4f });
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_PITCH_SHIFTER);
    test_flt(params[0], 12);
    test_flt(params[1], -8);
    test_flt(params[2], 1);
    test_flt(params[3], 30);
    test_flt(params[4], 0.25f);
    test_flt(params[5], 0.4f);
    length = (int32_t)(BOX_AUDIO_PITCH_SHIFTER_WINDOW_MAX * 0.001f * AUDIO_RATE) + 4;
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_PITCH_SHIFTER, AUDIO_RATE), length * 2);

    ecs_add(world, f, Reverb);
    rv = ecs_get(world, f, Reverb);
    test_flt(rv->shimmer, 0);
    test_flt(rv->shimmer_pitch, 12);
    ecs_set(world, f, Reverb, { .decay = 4, .damping = 0.3f, .predelay = 0.01f, .size = 0.7f,
        .mix = 1, .shimmer = 0.6f, .shimmer_pitch = 7 });
    test_int(box_audio_module_effect_kind(world, f, params), BOX_AUDIO_EFFECT_REVERB);
    test_flt(params[4], 1);
    test_flt(params[5], 0.6f);
    test_flt(params[6], 7);
    ecs_fini(world);
}
