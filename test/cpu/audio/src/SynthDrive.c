/**
 * @file SynthDrive.c
 * @brief The drive of the filter of the synth: the saturation of the input and the bandpass
 * state of the state variable filter bounds a self-resonant filter, adds harmonics, works at
 * 24 dB and on the stereo path of unison, stays stable at every rate, and is linear and
 * bit-identical at drive 0.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Frames of a block of the long renders */
#define SD_BLOCK 512

/** Seconds of the long renders */
#define SD_SECONDS 5

/** Volume of the plays that measure a peak, so the limiter of the master stays out of it */
#define SD_VOLUME 0.05f

/** Output of the long renders */
static float sd_l[SD_BLOCK], sd_r[SD_BLOCK];

/** Output of the reference of the identical case */
static float sd_ref[AUDIO_FRAMES];

/** Create a core at a rate.
 *
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* sd_core(
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
static void sd_define(
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

/** A patch of one oscillator at a steady level through a filter.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param level Level of the oscillator
 * @param hold Seconds at the level
 * @param filter A box_audio_synth_filter_t
 * @param cutoff Hz
 * @param resonance 0..1
 * @param drive 0..1
 * @return The patch
 */
static box_audio_synth_patch_t sd_patch(
    uint32_t wave,
    float frequency,
    float level,
    float hold,
    uint32_t filter,
    float cutoff,
    float resonance,
    float drive)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = hold, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = wave, .frequency = frequency, .level = level };
    p.filter = filter;
    p.cutoff = cutoff;
    p.resonance = resonance;
    p.drive = drive;

    return p;
}

/** Render a patch in a core of a rate in blocks, checking that every sample is finite.
 *
 * @param p The patch
 * @param rate Frames per second
 * @param seconds Seconds to render
 * @param volume Volume of the play
 * @param peak_r Receives the peak of the right channel, NULL for none
 * @return Peak of the left channel, divided by the volume
 */
static double sd_peak(
    const box_audio_synth_patch_t *p,
    float rate,
    float seconds,
    float volume,
    double *peak_r)
{
    box_audio_core_t *core = sd_core(rate);
    int32_t frames = (int32_t)(seconds * rate), done, i;
    double left = 0, right = 0;
    sd_define(core, 1, p);
    audio_play(core, 1, 1, 0, volume, 0, 1, 0);
    for (done = 0; done < frames; done += SD_BLOCK) {
        box_audio_core_render(core, sd_l, sd_r, SD_BLOCK);
        for (i = 0; i < SD_BLOCK; i ++) {
            test_assert(isfinite(sd_l[i]) && isfinite(sd_r[i]));
            left = fabs((double)sd_l[i]) > left ? fabs((double)sd_l[i]) : left;
            right = fabs((double)sd_r[i]) > right ? fabs((double)sd_r[i]) : right;
        }
    }

    box_audio_core_free(core);
    if (peak_r) {
        *peak_r = right / (double)volume;
    }

    return left / (double)volume;
}

/** Amplitude of a frequency in a range of samples at AUDIO_RATE, from its DFT bin.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame, a whole number of periods of the frequency past from
 * @param frequency Hz
 * @return Amplitude
 */
static double sd_amplitude(
    const float *x,
    int32_t from,
    int32_t to,
    double frequency)
{
    double re = 0, im = 0, w = 2 * AUDIO_PI * frequency / AUDIO_RATE;
    int32_t i;
    for (i = from; i < to; i ++) {
        re += (double)x[i] * cos(w * (double)(i - from));
        im += (double)x[i] * sin(w * (double)(i - from));
    }

    return 2 * sqrt(re * re + im * im) / (double)(to - from);
}

/** At resonance 1 a loud saw through the filter peaks far lower with drive 1 than without,
 * at 12 and at 24 dB per octave. */
void SynthDrive_resonance_bounded(void) {
    // audio/synth
    uint32_t slope;
    for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
        box_audio_synth_patch_t p = sd_patch(BOX_AUDIO_SYNTH_SAW, 100, 1, 1.5f,
            BOX_AUDIO_SYNTH_LOWPASS, 800, 1, 0);
        double linear, driven;
        p.slope = slope;
        linear = sd_peak(&p, AUDIO_RATE, 1, SD_VOLUME, NULL);
        p.drive = 1;
        driven = sd_peak(&p, AUDIO_RATE, 1, SD_VOLUME, NULL);
        printf("saw at 100 Hz through a %d dB lowpass at 800 Hz and resonance 1: peak %.3f "
            "at drive 0, %.3f at drive 1\n", slope ? 24 : 12, linear, driven);
        test_assert(driven > 0.01);
        test_assert(driven < linear * 0.5);
        test_assert(driven < 2);
    }
}

/** A self-resonant, driven filter swept by its envelope over 5 seconds stays finite and
 * bounded at 44.1, 48 and 96 kHz, for each kind, slope and a light and full drive. */
void SynthDrive_sweep_stable(void) {
    // audio/synth
    const float rates[] = { 44100, 48000, 96000 };
    const float drives[] = { 0.3f, 1 };
    int32_t r, s, d, kind = 0;
    for (r = 0; r < 3; r ++) {
        for (s = 0; s < 2; s ++) {
            for (d = 0; d < 2; d ++) {
                box_audio_synth_patch_t p = sd_patch(BOX_AUDIO_SYNTH_SAW, 55, 1, SD_SECONDS,
                    BOX_AUDIO_SYNTH_LOWPASS + (uint32_t)(kind % 4), 30, 1, drives[d]);
                double peak, peak_r;
                p.slope = (uint32_t)s;
                p.filter_amount = 9;
                p.filter_envelope = (box_audio_synth_envelope_t){
                    .attack = SD_SECONDS * 0.5f, .decay = SD_SECONDS * 0.5f };
                peak = sd_peak(&p, rates[r], SD_SECONDS, SD_VOLUME, &peak_r);
                printf("kind %u, %d dB, drive %.1f, swept at %.0f Hz: peak %.3f\n",
                    p.filter, s ? 24 : 12, (double)drives[d], (double)rates[r], peak);
                test_assert(peak > 0);
                test_assert(peak < 8);
                kind ++;
            }
        }
    }
}

/** Drive 0 renders bit-identical to the linear filter of svf.h on the unfiltered voice, at
 * 12 and 24 dB per octave and for each kind. */
void SynthDrive_zero_identical(void) {
    // audio/synth
    uint32_t slope, kind;
    for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
        for (kind = BOX_AUDIO_SYNTH_LOWPASS; kind <= BOX_AUDIO_SYNTH_NOTCH; kind ++) {
            box_audio_core_t *core = sd_core(AUDIO_RATE);
            box_audio_synth_patch_t p = sd_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.25f, 0.5f,
                BOX_AUDIO_SYNTH_FILTER_NONE, 1200, 0.6f, 0);
            box_audio_svf_t svf[2];
            int32_t stages, i;
            float g;
            sd_define(core, 1, &p);
            audio_play(core, 1, 1, 0, 1, 0, 1, 0);
            audio_render(core, 0, 24000);
            memcpy(sd_ref, audio_l, 24000 * sizeof(float));
            p.filter = kind;
            p.slope = slope;
            sd_define(core, 2, &p);
            audio_play(core, 2, 2, 0, 1, 0, 1, 0);
            audio_render(core, 0, 24000);
            box_audio_core_free(core);

            stages = box_audio_svf_init(svf, slope, 0.6f);
            g = box_audio_svf_g(1200.0, AUDIO_RATE);
            for (i = 0; i < stages; i ++) {
                box_audio_svf_coefs(&svf[i], g);
            }

            for (i = 0; i < 24000; i ++) {
                float y = sd_ref[i];
                if (stages > 1) {
                    y = box_audio_svf_next(&svf[1], (int32_t)kind, y);
                }

                y = box_audio_clean(box_audio_svf_next(&svf[0], (int32_t)kind, y));
                test_assert(audio_l[i] == y);
            }
        }
    }
}

/** Drive adds odd harmonics to a loud sine through an open lowpass, which passes it clean at
 * drive 0. */
void SynthDrive_harmonics(void) {
    // audio/synth
    double h1[2], h3[2];
    int32_t d;
    for (d = 0; d < 2; d ++) {
        box_audio_core_t *core = sd_core(AUDIO_RATE);
        box_audio_synth_patch_t p = sd_patch(BOX_AUDIO_SYNTH_SINE, 200, 0.8f, 1,
            BOX_AUDIO_SYNTH_LOWPASS, 12000, 0, (float)d);
        sd_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 24000);
        h1[d] = sd_amplitude(audio_l, 9600, 19200, 200);
        h3[d] = sd_amplitude(audio_l, 9600, 19200, 600);
        box_audio_core_free(core);
    }

    printf("sine at 200 Hz through a lowpass at 12 kHz: third harmonic %.2f dB at drive 0, "
        "%.2f dB at drive 1 under the fundamental\n", 20 * log10(h3[0] / h1[0] + 1e-12),
        20 * log10(h3[1] / h1[1] + 1e-12));
    test_assert(h1[0] > 0.7 && h1[1] > 0.1);
    test_assert(h3[0] / h1[0] < 0.001);
    test_assert(h3[1] / h1[1] > 0.03);
}

/** With drive the 24 dB filter still cuts an octave past its cutoff about twice as much as
 * the 12 dB one for a quiet sine, which the clipper leaves almost linear. */
void SynthDrive_slope_24(void) {
    // audio/synth
    double gain[2];
    uint32_t slope;
    for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
        box_audio_core_t *core = sd_core(AUDIO_RATE);
        box_audio_synth_patch_t p = sd_patch(BOX_AUDIO_SYNTH_SINE, 2000, 0.02f, 1,
            BOX_AUDIO_SYNTH_LOWPASS, 1000, 0, 1);
        p.slope = slope;
        sd_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 24000);
        gain[slope] = 20 * log10(sd_amplitude(audio_l, 9600, 19200, 2000) / 0.02);
        box_audio_core_free(core);
    }

    printf("an octave past 1 kHz at drive 1: %.2f dB at 12, %.2f dB at 24\n", gain[0],
        gain[1]);
    test_assert(gain[0] < -10 && gain[0] > -14);
    test_assert(gain[1] / gain[0] > 1.7 && gain[1] / gain[0] < 2.3);
}

/** On the stereo path of unison with width, drive bounds the resonance of both channels. */
void SynthDrive_wide(void) {
    // audio/synth
    box_audio_synth_patch_t p = sd_patch(BOX_AUDIO_SYNTH_SAW, 100, 1, 1.5f,
        BOX_AUDIO_SYNTH_LOWPASS, 800, 1, 0);
    double linear_l, linear_r, driven_l, driven_r;
    p.slope = BOX_AUDIO_SYNTH_SLOPE_24;
    p.layer[0].voices = 3;
    p.layer[0].detune = 10;
    p.layer[0].width = 1;
    linear_l = sd_peak(&p, AUDIO_RATE, 1, SD_VOLUME, &linear_r);
    p.drive = 1;
    driven_l = sd_peak(&p, AUDIO_RATE, 1, SD_VOLUME, &driven_r);
    printf("unison saw with width at resonance 1: peaks %.3f, %.3f at drive 0, %.3f, %.3f at "
        "drive 1\n", linear_l, linear_r, driven_l, driven_r);
    test_assert(driven_l > 0.01 && driven_r > 0.01);
    test_assert(driven_l < linear_l * 0.5);
    test_assert(driven_r < linear_r * 0.5);
}
