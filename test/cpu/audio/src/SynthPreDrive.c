/**
 * @file SynthPreDrive.c
 * @brief The drive of a synth sound, SynthDrive, between the mix of the layers and the filter:
 * the oversampled soft clipper of saturate.h adds odd harmonics, and even ones with asymmetry
 * without DC, keeps the RMS of a layer over the range of drive, aliases less than the same
 * clipper at the base rate, runs before the filter and on both channels of the wide path, and
 * a sound without it renders as before. The component in a world is the suite SynthPreDriveWorld,
 * since the world of the tests and the internals of the core do not build together.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Volume of the plays, so the limiter of the master stays out of the measures */
#define PD_VOLUME 0.25f

/** Rate of the aliasing check, at which the harmonics of 3 kHz fold between them */
#define PD_ALIAS_RATE 44100

/** Frames of a period of 300 Hz at PD_ALIAS_RATE, the spacing of the harmonics and aliases */
#define PD_ALIAS_PERIOD 147

/** Reference renders, left and right */
static float pd_l[AUDIO_FRAMES], pd_r[AUDIO_FRAMES];

/** Create a core at a rate.
 *
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* pd_core(
    float rate)
{
    box_audio_core_desc_t desc = { .sample_rate = rate };
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
static void pd_define(
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

/** A patch of one oscillator at a steady level, without a filter or a drive.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param level Level of the oscillator
 * @param hold Seconds at the level
 * @return The patch
 */
static box_audio_synth_patch_t pd_patch(
    uint32_t wave,
    float frequency,
    float level,
    float hold)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = hold, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = wave, .frequency = frequency, .level = level };

    return p;
}

/** Render a play of a patch in a new core into audio_l and audio_r, checking that every
 * sample is finite.
 *
 * @param p The patch
 * @param rate Frames per second
 * @param volume Volume of the play
 * @param frames Frames to render, at most AUDIO_FRAMES
 */
static void pd_render(
    const box_audio_synth_patch_t *p,
    float rate,
    float volume,
    int32_t frames)
{
    box_audio_core_t *core = pd_core(rate);
    int32_t i;
    pd_define(core, 1, p);
    audio_play(core, 1, 1, 0, volume, 0, 1, 0);
    audio_render(core, 0, frames);
    for (i = 0; i < frames; i ++) {
        test_assert(isfinite(audio_l[i]) && isfinite(audio_r[i]));
    }

    box_audio_core_free(core);
}

/** Amplitude of a frequency in a range, from its DFT bin; the range holds whole periods.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames
 * @param frequency Hz
 * @param rate Frames per second
 * @return The amplitude
 */
static double pd_amplitude(
    const float *x,
    int32_t from,
    int32_t count,
    double frequency,
    double rate)
{
    double re = 0, im = 0, w = 2 * AUDIO_PI * frequency / rate;
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
static double pd_mean(
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
static double pd_rms(
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

/** Power of the aliases of a tone of 3 kHz at PD_ALIAS_RATE against its fundamental: the
 * bins at every multiple of 300 Hz up to 21.9 kHz but the harmonics, the multiples of 3 kHz,
 * where the harmonics over the Nyquist frequency fold.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames, whole periods of 300 Hz
 * @return The power of the aliases over the power of the fundamental
 */
static double pd_aliases(
    const float *x,
    int32_t from,
    int32_t count)
{
    double sum = 0, f = pd_amplitude(x, from, count, 3000, PD_ALIAS_RATE);
    int32_t k;
    for (k = 1; k <= 73; k ++) {
        if (k % 10) {
            double a = pd_amplitude(x, from, count, 300.0 * k, PD_ALIAS_RATE);
            sum += a * a;
        }
    }

    return sum / (f * f);
}

/** A ratio in dB.
 *
 * @param ratio The ratio
 * @return 20 log10 of it
 */
static double pd_db(
    double ratio)
{
    return 20 * log10(ratio + 1e-12);
}

/** Run a channel through the drive offline, as a voice does: the clipper of saturate.h at
 * twice the rate, then the DC blocker when the drive has an asymmetry.
 *
 * @param x Samples, driven in place
 * @param frames Frames
 * @param drive 0..1
 * @param asymmetry 0..1
 */
static void pd_drive(
    float *x,
    int32_t frames,
    float drive,
    float asymmetry)
{
    box_audio_saturate_curve_t curve;
    box_audio_oversample_t os;
    box_audio_dc_t dc = {0};
    float coef = (float)(1.0 - 2.0 * AUDIO_PI * (double)BOX_AUDIO_SYNTH_DRIVE_DC /
        (double)AUDIO_RATE);
    int32_t i;
    box_audio_synth_drive_curve(&curve, drive, asymmetry);
    box_audio_oversample_clear(&os);
    for (i = 0; i < frames; i ++) {
        float y = box_audio_saturate_next(&os, &curve, x[i]);
        if (asymmetry > 0) {
            y = box_audio_dc_next(&dc, y, coef);
        }

        x[i] = box_audio_clean(y);
    }
}

/** A sine layer through drive 1 has odd harmonics only and no DC; with an asymmetry of 0.5
 * it has a 2nd harmonic as well, and still no DC. */
void SynthPreDrive_harmonics(void) {
    // audio/synth
    const int32_t from = AUDIO_RATE / 2, count = AUDIO_RATE / 4;
    box_audio_synth_patch_t p = pd_patch(BOX_AUDIO_SYNTH_SINE, 200, 1, 1);
    double h1, h2, h3, h4, h5, dc;
    p.has_drive = 1;
    p.pre_drive = 1;
    pd_render(&p, AUDIO_RATE, PD_VOLUME, from + count);
    h1 = pd_amplitude(audio_l, from, count, 200, AUDIO_RATE);
    h2 = pd_amplitude(audio_l, from, count, 400, AUDIO_RATE);
    h3 = pd_amplitude(audio_l, from, count, 600, AUDIO_RATE);
    h4 = pd_amplitude(audio_l, from, count, 800, AUDIO_RATE);
    h5 = pd_amplitude(audio_l, from, count, 1000, AUDIO_RATE);
    dc = pd_mean(audio_l, from, count);
    printf("sine at drive 1: 2nd %.1f dB, 3rd %.1f dB, 4th %.1f dB, 5th %.1f dB under the "
        "fundamental, DC %.2e\n", pd_db(h2 / h1), pd_db(h3 / h1), pd_db(h4 / h1),
        pd_db(h5 / h1), dc);
    test_assert(h1 > 0.05);
    test_assert(h3 / h1 > 0.1);
    test_assert(h5 / h1 > 0.03);
    test_assert(h2 / h1 < 1e-3);
    test_assert(h4 / h1 < 1e-3);
    test_assert(fabs(dc) < 1e-4);

    p.pre_asymmetry = 0.5f;
    pd_render(&p, AUDIO_RATE, PD_VOLUME, from + count);
    h1 = pd_amplitude(audio_l, from, count, 200, AUDIO_RATE);
    h2 = pd_amplitude(audio_l, from, count, 400, AUDIO_RATE);
    h3 = pd_amplitude(audio_l, from, count, 600, AUDIO_RATE);
    dc = pd_mean(audio_l, from, count);
    printf("asymmetry 0.5: 2nd %.1f dB, 3rd %.1f dB under the fundamental, DC %.2e\n",
        pd_db(h2 / h1), pd_db(h3 / h1), dc);
    test_assert(h2 / h1 > 0.01);
    test_assert(h3 / h1 > 0.05);
    test_assert(fabs(dc) < 1e-4);
}

/** A sine layer at level 1 keeps its RMS within 1.5 dB from drive 0.1 to 1, symmetric and
 * with an asymmetry of 0.5; a saw, whose crest the compensation does not know, is printed. */
void SynthPreDrive_level_across_drive(void) {
    // audio/synth
    const int32_t from = AUDIO_RATE / 4, count = AUDIO_RATE / 4;
    const float asymmetries[] = { 0, 0.5f };
    box_audio_synth_patch_t p = pd_patch(BOX_AUDIO_SYNTH_SINE, 200, 1, 1);
    double dry, saw, lo = 1e9, hi = -1e9;
    int32_t a, d;
    pd_render(&p, AUDIO_RATE, PD_VOLUME, from + count);
    dry = pd_rms(audio_l, from, count);
    test_assert(dry > 0.1);
    for (a = 0; a < 2; a ++) {
        for (d = 1; d <= 10; d ++) {
            double change;
            p.has_drive = 1;
            p.pre_drive = 0.1f * (float)d;
            p.pre_asymmetry = asymmetries[a];
            pd_render(&p, AUDIO_RATE, PD_VOLUME, from + count);
            change = pd_db(pd_rms(audio_l, from, count) / dry);
            printf("a sine at level 1, drive %.1f, asymmetry %.1f: %+.3f dB\n", 0.1 * d,
                (double)asymmetries[a], change);
            test_assert(fabs(change) < 1.5);
            lo = change < lo ? change : lo;
            hi = change > hi ? change : hi;
        }
    }

    printf("RMS from %+.3f dB to %+.3f dB\n", lo, hi);

    p = pd_patch(BOX_AUDIO_SYNTH_SAW, 200, 1, 1);
    pd_render(&p, AUDIO_RATE, PD_VOLUME, from + count);
    saw = pd_rms(audio_l, from, count);
    for (d = 1; d <= 10; d += 3) {
        p.has_drive = 1;
        p.pre_drive = 0.1f * (float)d;
        pd_render(&p, AUDIO_RATE, PD_VOLUME, from + count);
        printf("a saw at level 1, drive %.1f: %+.3f dB\n", 0.1 * d,
            pd_db(pd_rms(audio_l, from, count) / saw));
    }
}

/** A saw of 3 kHz through drive 1 at 44.1 kHz aliases less than the same clipper at the base
 * rate, computed here on the undriven saw at its level in the voice. */
void SynthPreDrive_alias_oversampled(void) {
    // audio/synth
    const int32_t from = PD_ALIAS_PERIOD * 60, count = PD_ALIAS_PERIOD * 120;
    box_audio_synth_patch_t p = pd_patch(BOX_AUDIO_SYNTH_SAW, 3000, 1, 1);
    box_audio_saturate_curve_t curve;
    double over, base;
    int32_t i;
    pd_render(&p, PD_ALIAS_RATE, PD_VOLUME, from + count);

    box_audio_synth_drive_curve(&curve, 1, 0);
    for (i = 0; i < from + count; i ++) {
        pd_l[i] = box_audio_saturate_clip(&curve, audio_l[i] / PD_VOLUME) * curve.comp;
    }

    base = pd_aliases(pd_l, from, count);
    p.has_drive = 1;
    p.pre_drive = 1;
    pd_render(&p, PD_ALIAS_RATE, PD_VOLUME, from + count);
    over = pd_aliases(audio_l, from, count);
    printf("aliases of a 3 kHz saw at drive 1: %.1f dB oversampled, %.1f dB at the base "
        "rate\n", 10 * log10(over), 10 * log10(base));
    test_assert(over < base * 0.5);
}

/** The mix is driven before the filter: a driven saw through a lowpass is the undriven saw,
 * driven offline, then filtered by the stage of svf.h. */
void SynthPreDrive_before_filter(void) {
    // audio/synth
    const int32_t frames = AUDIO_RATE / 2;
    box_audio_synth_patch_t p = pd_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.5f, 1);
    box_audio_svf_t svf[2];
    int32_t stages, i;
    double diff = 0;
    pd_render(&p, AUDIO_RATE, 1, frames);
    memcpy(pd_l, audio_l, (size_t)frames * sizeof(float));
    p.has_drive = 1;
    p.pre_drive = 0.7f;
    p.pre_asymmetry = 0.2f;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 1200;
    p.resonance = 0.6f;
    pd_render(&p, AUDIO_RATE, 1, frames);

    pd_drive(pd_l, frames, 0.7f, 0.2f);
    stages = box_audio_svf_init(svf, BOX_AUDIO_SYNTH_SLOPE_12, 0.6f);
    box_audio_svf_coefs(&svf[0], box_audio_svf_g(1200.0, AUDIO_RATE));
    test_int(stages, 1);
    for (i = 0; i < frames; i ++) {
        float y = box_audio_clean(box_audio_svf_next(&svf[0], BOX_AUDIO_SYNTH_LOWPASS,
            pd_l[i]));
        double d = fabs((double)audio_l[i] - (double)y);
        diff = d > diff ? d : diff;
    }

    printf("driven saw through a lowpass against the offline drive and filter: %.2e\n", diff);
    test_assert(diff < 1e-5);
    test_assert(audio_peak(audio_l, 0, frames) > 0.1f);
}

/** On the stereo path of unison with width both channels are driven alike, each with its own
 * state: each matches its undriven channel driven offline. */
void SynthPreDrive_wide(void) {
    // audio/synth
    const int32_t frames = AUDIO_RATE / 2;
    box_audio_synth_patch_t p = pd_patch(BOX_AUDIO_SYNTH_SINE, 200, 0.25f, 1);
    double diff_l = 0, diff_r = 0, apart = 0, h3;
    int32_t i;
    p.layer[0].voices = 3;
    p.layer[0].detune = 20;
    p.layer[0].width = 1;
    pd_render(&p, AUDIO_RATE, 1, frames);
    memcpy(pd_l, audio_l, (size_t)frames * sizeof(float));
    memcpy(pd_r, audio_r, (size_t)frames * sizeof(float));
    p.has_drive = 1;
    p.pre_drive = 0.5f;
    p.pre_asymmetry = 0.3f;
    pd_render(&p, AUDIO_RATE, 1, frames);

    pd_drive(pd_l, frames, 0.5f, 0.3f);
    pd_drive(pd_r, frames, 0.5f, 0.3f);
    for (i = 0; i < frames; i ++) {
        double dl = fabs((double)audio_l[i] - (double)pd_l[i]);
        double dr = fabs((double)audio_r[i] - (double)pd_r[i]);
        diff_l = dl > diff_l ? dl : diff_l;
        diff_r = dr > diff_r ? dr : diff_r;
        apart += fabs((double)audio_l[i] - (double)audio_r[i]);
    }

    h3 = pd_amplitude(audio_r, AUDIO_RATE / 4, AUDIO_RATE / 4, 600, AUDIO_RATE);
    printf("wide unison through drive 0.5: off by %.2e left, %.2e right, channels apart by "
        "%.3f, 3rd harmonic right %.4f\n", diff_l, diff_r, apart / frames, h3);
    test_assert(diff_l < 1e-5);
    test_assert(diff_r < 1e-5);
    test_assert(apart / frames > 0.001);
    test_assert(h3 > 1e-3);
}

/** Without a drive a patch renders bit-identical whatever its drive values, on the mono path,
 * with a filter, with FM, with a pluck and on the wide path. */
void SynthPreDrive_without_identical(void) {
    // audio/synth
    const int32_t frames = AUDIO_RATE / 2;
    box_audio_synth_patch_t p[4];
    int32_t n;
    p[0] = pd_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.5f, 0.3f);
    p[1] = pd_patch(BOX_AUDIO_SYNTH_SQUARE, 220, 0.4f, 0.3f);
    p[1].filter = BOX_AUDIO_SYNTH_LOWPASS;
    p[1].cutoff = 900;
    p[1].resonance = 0.5f;
    p[1].drive = 0.4f;
    p[2] = pd_patch(BOX_AUDIO_SYNTH_SINE, 220, 0.5f, 0.3f);
    p[2].layers = 2;
    p[2].layer[1] = (box_audio_synth_layer_t){ .frequency = 440, .level = 1, .modulates = 1,
        .index = 2 };
    p[3] = pd_patch(BOX_AUDIO_SYNTH_PLUCK, 110, 0.5f, 0.3f);
    p[3].layer[0].voices = 3;
    p[3].layer[0].detune = 10;
    p[3].layer[0].width = 1;
    for (n = 0; n < 4; n ++) {
        box_audio_synth_patch_t q = p[n];
        pd_render(&q, AUDIO_RATE, PD_VOLUME, frames);
        memcpy(pd_l, audio_l, (size_t)frames * sizeof(float));
        memcpy(pd_r, audio_r, (size_t)frames * sizeof(float));
        q.pre_drive = 0.8f;
        q.pre_asymmetry = 0.5f;
        pd_render(&q, AUDIO_RATE, PD_VOLUME, frames);
        test_assert(audio_peak(audio_l, 0, frames) > 0.01f);
        test_assert(memcmp(pd_l, audio_l, (size_t)frames * sizeof(float)) == 0);
        test_assert(memcmp(pd_r, audio_r, (size_t)frames * sizeof(float)) == 0);
    }
}

/** Plucks, unison and FM pass through the drive: each driven render is finite, heard and
 * differs from the undriven one, and the voice still ends with its envelope. */
void SynthPreDrive_layers_pass(void) {
    // audio/synth
    const int32_t frames = AUDIO_RATE / 2;
    box_audio_synth_patch_t p[3];
    int32_t n;
    p[0] = pd_patch(BOX_AUDIO_SYNTH_PLUCK, 110, 0.8f, 0.3f);
    p[1] = pd_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.5f, 0.3f);
    p[1].layer[0].voices = 5;
    p[1].layer[0].detune = 25;
    p[2] = pd_patch(BOX_AUDIO_SYNTH_SINE, 220, 0.8f, 0.3f);
    p[2].layers = 2;
    p[2].layer[1] = (box_audio_synth_layer_t){ .frequency = 440, .level = 1, .modulates = 1,
        .index = 2 };
    for (n = 0; n < 3; n ++) {
        box_audio_core_t *core;
        double dry, driven;
        pd_render(&p[n], AUDIO_RATE, PD_VOLUME, frames);
        memcpy(pd_l, audio_l, (size_t)frames * sizeof(float));
        dry = pd_rms(pd_l, 0, AUDIO_RATE / 4);
        p[n].has_drive = 1;
        p[n].pre_drive = 0.8f;
        p[n].pre_asymmetry = 0.2f;
        pd_render(&p[n], AUDIO_RATE, PD_VOLUME, frames);
        driven = pd_rms(audio_l, 0, AUDIO_RATE / 4);
        printf("patch %d: RMS %.4f undriven, %.4f driven\n", (int)n, dry, driven);
        test_assert(driven > 0.01);
        test_assert(memcmp(pd_l, audio_l, (size_t)frames * sizeof(float)) != 0);

        core = pd_core(AUDIO_RATE);
        pd_define(core, 1, &p[n]);
        audio_play(core, 1, 1, 0, PD_VOLUME, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        test_int(box_audio_core_voice_count(core), 0);
        box_audio_core_free(core);
    }
}

/** The patch clamps the drive: NaN and values out of 0..1, and a flag other than 1. */
void SynthPreDrive_clamped(void) {
    // audio/synth
    const int32_t frames = AUDIO_RATE / 4;
    box_audio_synth_patch_t p = pd_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.5f, 0.2f);
    box_audio_synth_patch_t q = p;
    p.has_drive = 7;
    p.pre_drive = 5;
    p.pre_asymmetry = NAN;
    pd_render(&p, AUDIO_RATE, PD_VOLUME, frames);
    memcpy(pd_l, audio_l, (size_t)frames * sizeof(float));
    q.has_drive = 1;
    q.pre_drive = 1;
    q.pre_asymmetry = 0;
    pd_render(&q, AUDIO_RATE, PD_VOLUME, frames);
    test_assert(memcmp(pd_l, audio_l, (size_t)frames * sizeof(float)) == 0);
}

