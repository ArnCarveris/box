/**
 * @file SynthComb.c
 * @brief The comb filter of the synth, SynthFilter.kind SynthComb: the peaks of its response to
 * white noise at the harmonics of the note, at high notes, with negative feedback, a tune, a
 * glide, damped by a low cutoff and on the stereo path, the pool of its delay lines, the
 * clamps, and the other kinds, which stay bit-identical.
 *
 * The response is measured from the same noise rendered without the filter and through it,
 * the same seed giving the same noise: H(f) = sum Y X* / sum |X|^2 over windowed segments, so
 * the peaks do not depend on the randomness of the noise.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Level of the noise, so the limiter of the master stays out of it */
#define SC_LEVEL 0.05f

/** Frames of a segment of the measure of the response, far longer than the comb rings */
#define SC_SEGMENT 32768

/** Frames the measure skips at the start, while the comb fills */
#define SC_SKIP 4800

/** Frames of a measure, 8 seconds, for enough segments that the peaks do not depend on the
 * noise */
#define SC_FRAMES (8 * AUDIO_FRAMES)

/** Input of the measure, the noise without the filter */
static float sc_in_l[SC_FRAMES];
static float sc_in_r[SC_FRAMES];

/** Output of the measure, the noise through the filter */
static float sc_out_l[SC_FRAMES];
static float sc_out_r[SC_FRAMES];

/** Window of a segment */
static double sc_window[SC_SEGMENT];

/** Create a core at AUDIO_RATE.
 *
 * @return The core
 */
static box_audio_core_t* sc_core(void)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL);

    return core;
}

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void sc_define(
    box_audio_core_t *core,
    uint32_t sound,
    const box_audio_synth_patch_t *p)
{
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, sound, p));
    box_audio_core_apply(core, w.data, w.size);
}

/** A patch of white noise held for longer than a measure through a comb filter, undamped, for a
 * note of a frequency: the noise layer has the frequency the comb takes.
 *
 * @param frequency Hz of the note
 * @param feedback Feedback of the comb
 * @return The patch
 */
static box_audio_synth_patch_t sc_patch(
    float frequency,
    float feedback)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 7;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 9, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_NOISE, .frequency = frequency, .level = SC_LEVEL };
    p.filter = BOX_AUDIO_SYNTH_COMB;
    p.cutoff = 24000;
    p.feedback = feedback;
    p.tune = 1;

    return p;
}

/** Render a second of a patch into audio_l and audio_r.
 *
 * @param p The patch
 */
static void sc_render(
    const box_audio_synth_patch_t *p)
{
    box_audio_core_t *core = sc_core();
    sc_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
}

/** Render SC_FRAMES of a patch, a second at a time through audio_l and audio_r, and set the
 * pitch of its voice to a value at a frame.
 *
 * @param p The patch
 * @param l Receives the left channel
 * @param r Receives the right channel
 * @param at Frame of the pitch, a multiple of AUDIO_FRAMES, SC_FRAMES for none
 * @param pitch The pitch
 */
static void sc_render_long(
    const box_audio_synth_patch_t *p,
    float *l,
    float *r,
    int32_t at,
    float pitch)
{
    box_audio_core_t *core = sc_core();
    int32_t i;
    sc_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    for (i = 0; i < SC_FRAMES; i += AUDIO_FRAMES) {
        if (i == at) {
            audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, pitch);
        }

        audio_render(core, 0, AUDIO_FRAMES);
        memcpy(l + i, audio_l, sizeof(audio_l));
        memcpy(r + i, audio_r, sizeof(audio_r));
    }

    box_audio_core_free(core);
}

/** Render SC_FRAMES of a patch without its filter into sc_in_l and sc_in_r, then with it into
 * sc_out_l and sc_out_r.
 *
 * @param p The patch
 */
static void sc_measure(
    const box_audio_synth_patch_t *p)
{
    box_audio_synth_patch_t dry = *p;
    dry.filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    sc_render_long(&dry, sc_in_l, sc_in_r, SC_FRAMES, 1);
    sc_render_long(p, sc_out_l, sc_out_r, SC_FRAMES, 1);
}

/** Response of the filter at a frequency, from the input and the output over a range of
 * frames, in Hann windowed segments that overlap by half.
 *
 * @param x The input
 * @param y The output
 * @param from First frame
 * @param to End frame
 * @param frequency Hz
 * @param phase Receives the phase of the response, may be NULL
 * @return The magnitude
 */
static double sc_response(
    const float *x,
    const float *y,
    int32_t from,
    int32_t to,
    double frequency,
    double *phase)
{
    double w = 2 * AUDIO_PI * frequency / AUDIO_RATE, cw = cos(w), sw = sin(w);
    double cross_re = 0, cross_im = 0, power = 0;
    int32_t at, i;
    if (sc_window[SC_SEGMENT / 2] == 0) {
        for (i = 0; i < SC_SEGMENT; i ++) {
            sc_window[i] = 0.5 - 0.5 * cos(2 * AUDIO_PI * (double)i / SC_SEGMENT);
        }
    }

    for (at = from; at + SC_SEGMENT <= to; at += SC_SEGMENT / 2) {
        double xr = 0, xi = 0, yr = 0, yi = 0, c = 1, s = 0, t;
        for (i = 0; i < SC_SEGMENT; i ++) {
            double wx = sc_window[i] * (double)x[at + i], wy = sc_window[i] * (double)y[at + i];
            xr += wx * c;
            xi -= wx * s;
            yr += wy * c;
            yi -= wy * s;
            t = c * cw - s * sw;
            s = s * cw + c * sw;
            c = t;
        }

        cross_re += yr * xr + yi * xi;
        cross_im += yi * xr - yr * xi;
        power += xr * xr + xi * xi;
    }

    if (phase) {
        *phase = atan2(cross_im, cross_re);
    }

    return power > 0 ? sqrt(cross_re * cross_re + cross_im * cross_im) / power : 0.0;
}

/** Frequency of the peak of the response within 40 cents of a frequency.
 *
 * At a peak of a comb its loop is in phase, so the response is real. Its phase crosses zero
 * there steeply, while the magnitude is flat on top and wavers with the noise.
 *
 * The crossing from positive to negative is found in steps of 2 cents, then by bisection; of
 * more crossings the one with the largest response.
 *
 * @param x The input
 * @param y The output
 * @param from First frame
 * @param to End frame
 * @param frequency Hz the peak is near
 * @param peak Receives the response at the peak, may be NULL
 * @return Hz of the peak, frequency when the phase crosses zero nowhere
 */
static double sc_peak(
    const float *x,
    const float *y,
    int32_t from,
    int32_t to,
    double frequency,
    double *peak)
{
    double best = 0, top = -1, low, high, mid, phase, last = 0, h;
    int32_t i;
    for (i = -20; i <= 20; i ++) {
        h = sc_response(x, y, from, to, frequency * pow(2, 2.0 * i / 1200), &phase);
        if (i > -20 && last > 0 && phase <= 0 && h > top) {
            top = h;
            best = 2.0 * i;
        }

        last = phase;
    }

    if (top >= 0) {
        low = best - 2;
        high = best;
        for (i = 0; i < 16; i ++) {
            mid = (low + high) / 2;
            sc_response(x, y, from, to, frequency * pow(2, mid / 1200), &phase);
            if (phase > 0) {
                low = mid;
            } else {
                high = mid;
            }
        }

        best = (low + high) / 2;
    }

    if (peak) {
        *peak = sc_response(x, y, from, to, frequency * pow(2, best / 1200), NULL);
    }

    return frequency * pow(2, best / 1200);
}

/** Cents from one frequency to another.
 *
 * @param f The frequency
 * @param ref The reference
 * @return The cents
 */
static double sc_cents(
    double f,
    double ref)
{
    return 1200 * log2(f / ref);
}

/** Check that the comb has its peaks within 5 cents of multiples of a frequency.
 *
 * @param x The input
 * @param y The output
 * @param base Hz of the first multiple
 * @param step Multiples from one peak to the next, 1 for every one, 2 for the odd ones
 * @param count Peaks to check
 * @param label Label of the printout
 */
static void sc_expect_peaks(
    const float *x,
    const float *y,
    double base,
    int32_t step,
    int32_t count,
    const char *label)
{
    int32_t k;
    for (k = 0; k < count; k ++) {
        double expect = base * (double)(1 + k * step), f, h;
        f = sc_peak(x, y, SC_SKIP, SC_FRAMES, expect, &h);
        printf("%s: peak near %.1f Hz at %.2f Hz, %.2f cents, gain %.3f\n", label, expect, f,
            sc_cents(f, expect), h);
        test_assert(fabs(sc_cents(f, expect)) < 5);
        test_assert(h > 0.5);
    }
}

/** On white noise a comb at 220 Hz with feedback 0.9 has its peaks at 220 k Hz within 5
 * cents, about unity at the peaks, and dips half way between them. */
void SynthComb_peaks(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, 0.9f);
    double peak, dip;
    sc_measure(&p);
    sc_expect_peaks(sc_in_l, sc_out_l, 220, 1, 6, "220 Hz");
    peak = sc_response(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 440, NULL);
    dip = sc_response(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 550, NULL);
    printf("gain at 440 Hz %.3f, at 550 Hz %.3f\n", peak, dip);
    test_assert(peak > 0.7 && peak < 1.3);
    test_assert(dip < peak * 0.2);
}

/** At 1760 Hz, a period of 27.3 frames, the fractional delay keeps the peaks within 5 cents. */
void SynthComb_high(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(1760, 0.9f);
    sc_measure(&p);
    sc_expect_peaks(sc_in_l, sc_out_l, 1760, 1, 4, "1760 Hz");
}

/** Negative feedback has its peaks at the odd multiples of half the note, 110 Hz, and dips
 * at the harmonics of the note. */
void SynthComb_negative(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, -0.9f);
    double peak, dip;
    sc_measure(&p);
    sc_expect_peaks(sc_in_l, sc_out_l, 110, 2, 5, "negative");
    peak = sc_response(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 330, NULL);
    dip = sc_response(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 440, NULL);
    printf("negative: gain at 330 Hz %.3f, at 440 Hz %.3f\n", peak, dip);
    test_assert(dip < peak * 0.2);
}

/** A tune of 2 doubles the frequency of the peaks. */
void SynthComb_tune(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, 0.9f);
    p.tune = 2;
    sc_measure(&p);
    sc_expect_peaks(sc_in_l, sc_out_l, 440, 1, 4, "tune 2");
    test_assert(sc_response(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 220, NULL) < 0.2);
}

/** A glide of the pitch of the voice to 1.5 moves the peaks from 220 k Hz to 330 k Hz. */
void SynthComb_glide(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, 0.9f), dry = p;
    int32_t at = SC_FRAMES / 2;
    double f;
    p.glide = 0.1f;
    dry.glide = 0.1f;
    dry.filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    sc_render_long(&dry, sc_in_l, sc_in_r, at, 1.5f);
    sc_render_long(&p, sc_out_l, sc_out_r, at, 1.5f);
    f = sc_peak(sc_in_l, sc_out_l, SC_SKIP, at, 440, NULL);
    printf("glide: before at %.2f Hz\n", f);
    test_assert(fabs(sc_cents(f, 440)) < 5);
    f = sc_peak(sc_in_l, sc_out_l, at + AUDIO_FRAMES / 2, SC_FRAMES, 660, NULL);
    printf("glide: after at %.2f Hz\n", f);
    test_assert(fabs(sc_cents(f, 660)) < 5);
    f = sc_peak(sc_in_l, sc_out_l, at + AUDIO_FRAMES / 2, SC_FRAMES, 330, NULL);
    printf("glide: after at %.2f Hz\n", f);
    test_assert(fabs(sc_cents(f, 330)) < 5);
}

/** A low cutoff damps the upper peaks of the comb far more than the lower ones, and the
 * fundamental stays in tune. */
void SynthComb_damped(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, 0.9f);
    double open_low, open_high, low, high, f;
    sc_measure(&p);
    sc_peak(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 220, &open_low);
    sc_peak(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 2200, &open_high);
    p.cutoff = 500;
    sc_measure(&p);
    f = sc_peak(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 220, &low);
    sc_peak(sc_in_l, sc_out_l, SC_SKIP, SC_FRAMES, 2200, &high);
    printf("damped: 220 Hz %.3f of %.3f at %.2f Hz, 2200 Hz %.3f of %.3f\n", low, open_low, f,
        high, open_high);
    test_assert(low > open_low * 0.4);
    test_assert(high < open_high * 0.25);
    test_assert(high < low * 0.5);
    test_assert(fabs(sc_cents(f, 220)) < 10);
}

/** On the stereo path of unison with width both channels run the comb with the same tuning:
 * two voices of noise without detune at width 1, one on each channel. */
void SynthComb_wide(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, 0.9f);
    p.layer[0].voices = 2;
    p.layer[0].detune = 0;
    p.layer[0].width = 1;
    sc_measure(&p);
    sc_expect_peaks(sc_in_l, sc_out_l, 220, 1, 4, "wide, left");
    sc_expect_peaks(sc_in_r, sc_out_r, 220, 1, 4, "wide, right");
}

/** The delay lines come from a pool of BOX_AUDIO_SYNTH_COMBS: a mono voice takes one and a
 * stereo voice two, a play that does not find them free is ignored, and they come back when
 * the voices end. */
void SynthComb_pool(void) {
    // audio/synth
    box_audio_core_t *core = sc_core();
    box_audio_synth_patch_t p = sc_patch(220, 0.5f);
    int32_t i;
    p.envelope.hold = 0.1f;
    sc_define(core, 1, &p);
    p.layer[0].voices = 2;
    p.layer[0].width = 1;
    sc_define(core, 2, &p);
    test_int(core->synths[core->sounds[1].synth - 1].combs, 1);
    test_int(core->synths[core->sounds[2].synth - 1].combs, 2);
    for (i = 0; i < BOX_AUDIO_SYNTH_COMBS; i ++) {
        audio_play(core, (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_COMBS);
    test_int(core->comb_free_count, 0);
    audio_play(core, 1000, 1, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 1);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(core->comb_free_count, BOX_AUDIO_SYNTH_COMBS);
    audio_events(core, 0, NULL);

    for (i = 0; i < BOX_AUDIO_SYNTH_COMBS / 2; i ++) {
        audio_play(core, 2000 + (uint32_t)i, 2, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_COMBS / 2);
    audio_play(core, 3000, 2, 0, 0.01f, 0, 1, 0);
    audio_play(core, 3001, 1, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 2);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(core->comb_free_count, BOX_AUDIO_SYNTH_COMBS);
    box_audio_core_free(core);
}

/** The core clamps the feedback to -0.98..0.98 and the tune to 0.125..8, a tune of 0 or NaN
 * counts as 1, and a kind past the comb is no filter. */
void SynthComb_clamped(void) {
    // audio/synth
    box_audio_core_t *core = sc_core();
    box_audio_synth_patch_t p = sc_patch(220, 3);
    const box_audio_synth_patch_t *q;
    p.tune = 100;
    sc_define(core, 1, &p);
    q = &core->synths[core->sounds[1].synth - 1].patch;
    test_assert(q->filter == BOX_AUDIO_SYNTH_COMB);
    test_assert(q->feedback == BOX_AUDIO_SYNTH_COMB_FEEDBACK_MAX);
    test_assert(q->tune == BOX_AUDIO_SYNTH_TUNE_MAX);

    p.feedback = -3;
    p.tune = 0.01f;
    sc_define(core, 2, &p);
    q = &core->synths[core->sounds[2].synth - 1].patch;
    test_assert(q->feedback == -BOX_AUDIO_SYNTH_COMB_FEEDBACK_MAX);
    test_assert(q->tune == BOX_AUDIO_SYNTH_TUNE_MIN);

    p.feedback = NAN;
    p.tune = NAN;
    sc_define(core, 3, &p);
    q = &core->synths[core->sounds[3].synth - 1].patch;
    test_assert(q->feedback == 0);
    test_assert(q->tune == 1);

    p.tune = 0;
    p.filter = BOX_AUDIO_SYNTH_COMB + 1;
    sc_define(core, 4, &p);
    q = &core->synths[core->sounds[4].synth - 1].patch;
    test_assert(q->tune == 1);
    test_assert(q->filter == BOX_AUDIO_SYNTH_FILTER_NONE);
    test_int(core->synths[core->sounds[4].synth - 1].combs, 0);
    box_audio_core_free(core);
}

/** At the highest feedback the output stays bounded, the peaks near unity, and a note at the
 * top of the range and one under the lowest comb frequency play without a fault. */
void SynthComb_bounded(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(220, 0.98f);
    float in, out;
    sc_measure(&p);
    in = audio_peak(sc_in_l, 0, SC_FRAMES);
    out = audio_peak(sc_out_l, 0, SC_FRAMES);
    printf("feedback 0.98: peak %.3f of an input of %.3f\n", (double)out, (double)in);
    test_assert(out < in * 4);
    test_assert(out > 0);

    p = sc_patch(19000, -0.98f);
    p.tune = 8;
    sc_render(&p);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) < 1);
    p = sc_patch(20, 0.98f);
    p.tune = 0.125f;
    p.cutoff = 300;
    sc_render(&p);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) < 1);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0);
}

/** The feedback and the tune do nothing on the other kinds and models: with them each renders
 * bit-identical to without, on the mono and the stereo path. */
void SynthComb_others_identical(void) {
    // audio/synth
    uint32_t kind, model;
    int32_t wide;
    for (wide = 0; wide < 2; wide ++) {
        for (model = BOX_AUDIO_SYNTH_SVF; model <= BOX_AUDIO_SYNTH_LADDER; model ++) {
            for (kind = BOX_AUDIO_SYNTH_FILTER_NONE; kind <= BOX_AUDIO_SYNTH_FORMANT; kind ++) {
                box_audio_synth_patch_t p = sc_patch(110, 0);
                uint64_t plain, comb;
                p.layer[0].wave = BOX_AUDIO_SYNTH_SAW;
                p.filter = kind;
                p.model = model;
                p.cutoff = 900;
                p.resonance = 0.5f;
                p.drive = 0.3f;
                p.vowel = 1;
                p.tune = 0;
                p.glide = 0.05f;
                p.filter_amount = 2;
                p.filter_envelope = (box_audio_synth_envelope_t){
                    .attack = 0.05f, .decay = 0.2f, .sustain = 0.3f, .hold = 0.3f,
                    .release = 0.1f };
                p.pitch_envelope = (box_audio_synth_pitch_envelope_t){
                    .amount = 5, .decay = 0.1f };
                p.lfos = 1;
                p.lfo[0] = (box_audio_synth_lfo_t){
                    .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 4, .cutoff = 1, .pitch = 20 };
                if (wide) {
                    p.layer[0].voices = 3;
                    p.layer[0].detune = 12;
                    p.layer[0].width = 0.7f;
                }

                sc_render(&p);
                plain = audio_hash(audio_l, AUDIO_FRAMES) ^
                    (audio_hash(audio_r, AUDIO_FRAMES) * 31);
                p.feedback = -0.7f;
                p.tune = 3;
                sc_render(&p);
                comb = audio_hash(audio_l, AUDIO_FRAMES) ^
                    (audio_hash(audio_r, AUDIO_FRAMES) * 31);
                test_assert(plain == comb);
            }
        }
    }
}

/** A comb whose cutoff the filter envelope and an LFO move, and whose note a pitch envelope and
 * an LFO move, renders the same twice. */
void SynthComb_deterministic(void) {
    // audio/synth
    box_audio_synth_patch_t p = sc_patch(330, 0.8f);
    uint64_t hash[2];
    int32_t n;
    p.cutoff = 3000;
    p.filter_amount = -2;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.4f, .sustain = 0.2f, .release = 0.1f };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 12, .decay = 0.2f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 6, .cutoff = 1, .pitch = 30 };
    for (n = 0; n < 2; n ++) {
        sc_render(&p);
        hash[n] = audio_hash(audio_l, AUDIO_FRAMES);
    }

    test_assert(hash[0] == hash[1]);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0);
}
