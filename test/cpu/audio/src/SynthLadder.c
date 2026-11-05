/**
 * @file SynthLadder.c
 * @brief The transistor ladder of the synth filter, SynthFilter.model SynthFilterLadder: its
 * self-oscillation at resonance 1, the slopes of its lowpass taps, its bass compensation, its
 * multimode kinds, its stability under a fast sweep at full resonance and drive, the stereo
 * path, and the state variable filter, which stays bit-identical.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Frames of a block of the long renders */
#define SL_BLOCK 512

/** Volume of the plays that measure a peak, so the limiter of the master stays out of it */
#define SL_VOLUME 0.05f

/** Output of the long renders */
static float sl_l[SL_BLOCK], sl_r[SL_BLOCK];

/** Output of the reference of the identical case */
static float sl_ref[AUDIO_FRAMES];

/** Create a core at a rate.
 *
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* sl_core(
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
static void sl_define(
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

/** A patch of one oscillator at a steady level through a ladder.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param level Level of the oscillator
 * @param hold Seconds at the level
 * @param filter A box_audio_synth_filter_t
 * @param slope A box_audio_synth_slope_t
 * @param cutoff Hz
 * @param resonance 0..1
 * @return The patch
 */
static box_audio_synth_patch_t sl_patch(
    uint32_t wave,
    float frequency,
    float level,
    float hold,
    uint32_t filter,
    uint32_t slope,
    float cutoff,
    float resonance)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = hold, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = wave, .frequency = frequency, .level = level };
    p.filter = filter;
    p.slope = slope;
    p.cutoff = cutoff;
    p.resonance = resonance;
    p.model = BOX_AUDIO_SYNTH_LADDER;

    return p;
}

/** A patch that plucks a ladder at resonance 1 with 5 ms of noise, then holds the voice with
 * a silent sine for 1.5 seconds while the ladder rings on its own.
 *
 * @param cutoff Hz
 * @return The patch
 */
static box_audio_synth_patch_t sl_pluck(
    float cutoff)
{
    box_audio_synth_patch_t p = sl_patch(BOX_AUDIO_SYNTH_NOISE, 0, 0.5f, 1.5f,
        BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, cutoff, 1);
    p.layers = 2;
    p.layer[0].has_envelope = 1;
    p.layer[0].envelope = (box_audio_synth_envelope_t){
        .decay = 0.005f, .sustain = 0, .release = 0.001f };
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 1000, .level = 0 };

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
static double sl_peak(
    const box_audio_synth_patch_t *p,
    float rate,
    float seconds,
    float volume,
    double *peak_r)
{
    box_audio_core_t *core = sl_core(rate);
    int32_t frames = (int32_t)(seconds * rate), done, i;
    double left = 0, right = 0;
    sl_define(core, 1, p);
    audio_play(core, 1, 1, 0, volume, 0, 1, 0);
    for (done = 0; done < frames; done += SL_BLOCK) {
        box_audio_core_render(core, sl_l, sl_r, SL_BLOCK);
        for (i = 0; i < SL_BLOCK; i ++) {
            test_assert(isfinite(sl_l[i]) && isfinite(sl_r[i]));
            left = fabs((double)sl_l[i]) > left ? fabs((double)sl_l[i]) : left;
            right = fabs((double)sl_r[i]) > right ? fabs((double)sl_r[i]) : right;
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
 * @param to End frame
 * @param frequency Hz
 * @return Amplitude
 */
static double sl_amplitude(
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

/** Amplitude of a sine in a range, from its mean square.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The amplitude
 */
static double sl_rms_amplitude(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * x[i];
    }

    return sqrt(2 * sum / (double)(to - from));
}

/** Gain of a ladder for a sine at a slope and a resonance.
 *
 * @param kind A box_audio_synth_filter_t
 * @param slope A box_audio_synth_slope_t
 * @param resonance 0..1
 * @param cutoff Hz
 * @param frequency Hz of the sine, a whole number of periods in 9600 frames
 * @return Amplitude out over amplitude in
 */
static double sl_gain(
    uint32_t kind,
    uint32_t slope,
    float resonance,
    float cutoff,
    float frequency)
{
    box_audio_core_t *core = sl_core(AUDIO_RATE);
    box_audio_synth_patch_t p = sl_patch(BOX_AUDIO_SYNTH_SINE, frequency, 0.1f, 1.0f,
        kind, slope, cutoff, resonance);
    double result;
    sl_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 19200);
    result = sl_amplitude(audio_l, 9600, 19200, frequency) / 0.1;
    box_audio_core_free(core);

    return result;
}

/** Check the self-oscillation of a channel of a plucked ladder at 1 kHz: in tune within 50
 * cents, at a steady, bounded level, and a near sine.
 *
 * @param x The channel, 48000 frames at volume SL_VOLUME
 * @param name Name of the channel for the log
 */
static void sl_check_ring(
    const float *x,
    const char *name)
{
    double frequency = audio_frequency(x, 24000, 48000);
    double cents = 1200 * log2(frequency / 1000);
    double early = sl_rms_amplitude(x, 12000, 24000) / SL_VOLUME;
    double late = sl_rms_amplitude(x, 36000, 48000) / SL_VOLUME;
    double h1 = sl_amplitude(x, 24000, 48000, frequency);
    double h3 = sl_amplitude(x, 24000, 48000, 3 * frequency);
    double peak = audio_peak(x, 0, 48000) / SL_VOLUME;
    printf("%s: rings at %.2f Hz (%.1f cents), amplitude %.3f then %.3f, peak %.3f, third "
        "harmonic %.2f dB\n", name, frequency, cents, early, late, peak,
        20 * log10(h3 / h1 + 1e-12));
    test_assert(fabs(cents) < 50);
    test_assert(late > 0.05);
    test_assert(peak < 4);
    test_assert(late > early * 0.8 && late < early * 1.25);
    test_assert(h3 < h1 * 0.05);
}

/** At resonance 1 a ladder at 1 kHz plucked by 5 ms of noise rings on its own as a near sine
 * at the cutoff, at a steady and bounded level. */
void SynthLadder_self_oscillation(void) {
    // audio/synth
    box_audio_core_t *core = sl_core(AUDIO_RATE);
    box_audio_synth_patch_t p = sl_pluck(1000);
    sl_define(core, 1, &p);
    audio_play(core, 1, 1, 0, SL_VOLUME, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    sl_check_ring(audio_l, "ladder at resonance 1");
}

/** Below resonance 1 the plucked ladder at resonance 0.8 dies away instead. */
void SynthLadder_rings_out(void) {
    // audio/synth
    box_audio_core_t *core = sl_core(AUDIO_RATE);
    box_audio_synth_patch_t p = sl_pluck(1000);
    double late;
    p.resonance = 0.8f;
    sl_define(core, 1, &p);
    audio_play(core, 1, 1, 0, SL_VOLUME, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    late = audio_peak(audio_l, 24000, 48000) / SL_VOLUME;
    printf("ladder at resonance 0.8: peak %.3g after 0.5 s\n", late);
    test_assert(late < 0.001);
}

/** The 4-pole lowpass attenuates an octave past its cutoff by about 24 dB against its
 * passband, and the 2-pole tap about half as much. */
void SynthLadder_slope_24(void) {
    // audio/synth
    double pass24 = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, 0, 1000, 250);
    double stop24 = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, 0, 1000, 2000);
    double pass12 = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_12, 0, 1000, 250);
    double stop12 = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_12, 0, 1000, 2000);
    double low24 = 20 * log10(stop24 / pass24), low12 = 20 * log10(stop12 / pass12);
    printf("ladder an octave past 1 kHz against 250 Hz: %.2f dB at 24, %.2f dB at 12; "
        "passband %.2f dB\n", low24, low12, 20 * log10(pass24));
    test_assert(low24 > -28 && low24 < -20);
    test_assert(low24 / low12 > 1.7 && low24 / low12 < 2.3);
}

/** The bass compensation keeps the passband of the lowpass at resonance 0.8 within 3 dB of
 * resonance 0, at 24 and at 12 dB per octave. */
void SynthLadder_bass(void) {
    // audio/synth
    uint32_t slope;
    for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
        double open = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, slope, 0, 1000, 100);
        double resonant = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, slope, 0.8f, 1000, 100);
        double full = sl_gain(BOX_AUDIO_SYNTH_LOWPASS, slope, 1, 1000, 100);
        double loss = 20 * log10(resonant / open);
        printf("ladder %d dB, 100 Hz under a cutoff of 1 kHz: %.2f dB at resonance 0, "
            "%.2f dB at 0.8 (%.2f dB loss), %.2f dB at 1\n", slope ? 24 : 12,
            20 * log10(open), 20 * log10(resonant), loss, 20 * log10(full));
        test_assert(open > 0.85 && open < 1.15);
        test_assert(fabs(loss) < 3);
    }
}

/** The multimode kinds mix the poles: the highpass passes over its cutoff and cuts under
 * it, the bandpass passes its cutoff more than an octave or more off it, and the notch takes
 * the cutoff out, at both slopes. */
void SynthLadder_kinds(void) {
    // audio/synth
    uint32_t slope;
    for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
        double hp_low = sl_gain(BOX_AUDIO_SYNTH_HIGHPASS, slope, 0.3f, 1000, 125);
        double hp_high = sl_gain(BOX_AUDIO_SYNTH_HIGHPASS, slope, 0.3f, 1000, 8000);
        double bp_low = sl_gain(BOX_AUDIO_SYNTH_BANDPASS, slope, 0.3f, 1000, 125);
        double bp_mid = sl_gain(BOX_AUDIO_SYNTH_BANDPASS, slope, 0.3f, 1000, 1000);
        double bp_high = sl_gain(BOX_AUDIO_SYNTH_BANDPASS, slope, 0.3f, 1000, 8000);
        double notch_low = sl_gain(BOX_AUDIO_SYNTH_NOTCH, slope, 0.3f, 1000, 125);
        double notch_mid = sl_gain(BOX_AUDIO_SYNTH_NOTCH, slope, 0.3f, 1000, 1000);
        double notch_high = sl_gain(BOX_AUDIO_SYNTH_NOTCH, slope, 0.3f, 1000, 8000);
        printf("ladder %d dB at 125 Hz, 1 kHz, 8 kHz: highpass %.3f, -, %.3f; bandpass %.3f, "
            "%.3f, %.3f; notch %.3f, %.3f, %.3f\n", slope ? 24 : 12, hp_low, hp_high,
            bp_low, bp_mid, bp_high, notch_low, notch_mid, notch_high);
        test_assert(hp_high > 0.7 && hp_high < 1.4);
        test_assert(hp_low < hp_high * 0.05);
        test_assert(bp_mid > 0.5);
        test_assert(bp_low < bp_mid * 0.4);
        test_assert(bp_high < bp_mid * 0.4);
        test_assert(notch_mid < notch_low * 0.1);
        test_assert(notch_mid < notch_high * 0.1);
    }
}

/** A saw through the ladder at resonance 1 and drive 1, its cutoff swept by an LFO at 20 Hz
 * over 4 octaves either way, stays finite and bounded at 44.1, 48 and 96 kHz, for each kind
 * and slope. */
void SynthLadder_sweep_bounded(void) {
    // audio/synth
    const float rates[] = { 44100, 48000, 96000 };
    int32_t r, s;
    uint32_t kind;
    for (r = 0; r < 3; r ++) {
        for (s = 0; s < 2; s ++) {
            for (kind = BOX_AUDIO_SYNTH_LOWPASS; kind <= BOX_AUDIO_SYNTH_NOTCH; kind ++) {
                box_audio_synth_patch_t p = sl_patch(BOX_AUDIO_SYNTH_SAW, 55, 1, 2,
                    kind, (uint32_t)s, 1000, 1);
                double peak;
                p.drive = 1;
                p.lfos = 1;
                p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE,
                    .rate = 20, .cutoff = 4 };
                peak = sl_peak(&p, rates[r], 2, SL_VOLUME, NULL);
                printf("ladder kind %u, %d dB, swept at %.0f Hz: peak %.3f\n", kind,
                    s ? 24 : 12, (double)rates[r], peak);
                test_assert(peak > 0.01);
                test_assert(peak < 8);
            }
        }
    }
}

/** On the stereo path of unison with width, both channels run a ladder of their own: plucked
 * at resonance 1, each rings at the cutoff. */
void SynthLadder_wide(void) {
    // audio/synth
    box_audio_core_t *core = sl_core(AUDIO_RATE);
    box_audio_synth_patch_t p = sl_pluck(1000);
    p.layer[0].voices = 3;
    p.layer[0].detune = 10;
    p.layer[0].width = 1;
    sl_define(core, 1, &p);
    audio_play(core, 1, 1, 0, SL_VOLUME, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    sl_check_ring(audio_l, "wide ladder, left");
    sl_check_ring(audio_r, "wide ladder, right");
}

/** The SVF model, set or the default, renders bit-identical to the filter of svf.h on the
 * unfiltered voice, for each kind, slope and a drive; an unknown model is clamped to it. */
void SynthLadder_svf_identical(void) {
    // audio/synth
    const uint32_t models[] = { BOX_AUDIO_SYNTH_SVF, 99 };
    uint32_t slope, kind, m;
    for (m = 0; m < 2; m ++) {
        for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
            for (kind = BOX_AUDIO_SYNTH_LOWPASS; kind <= BOX_AUDIO_SYNTH_NOTCH; kind ++) {
                box_audio_core_t *core = sl_core(AUDIO_RATE);
                box_audio_synth_patch_t p = sl_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.25f, 0.5f,
                    BOX_AUDIO_SYNTH_FILTER_NONE, slope, 1200, 0.6f);
                box_audio_svf_t svf[2];
                int32_t stages, i;
                float g;
                p.drive = 0.4f;
                sl_define(core, 1, &p);
                audio_play(core, 1, 1, 0, 1, 0, 1, 0);
                audio_render(core, 0, 24000);
                memcpy(sl_ref, audio_l, 24000 * sizeof(float));
                box_audio_core_free(core);
                core = sl_core(AUDIO_RATE);
                p.filter = kind;
                p.model = models[m];
                sl_define(core, 1, &p);
                audio_play(core, 1, 1, 0, 1, 0, 1, 0);
                audio_render(core, 0, 24000);
                box_audio_core_free(core);

                stages = box_audio_svf_init(svf, slope, 0.6f);
                g = box_audio_svf_g(1200.0, AUDIO_RATE);
                for (i = 0; i < stages; i ++) {
                    box_audio_svf_coefs(&svf[i], g);
                }

                for (i = 0; i < 24000; i ++) {
                    float y = sl_ref[i];
                    if (stages > 1) {
                        y = box_audio_svf_drive(&svf[1], (int32_t)kind, y, 0.4f);
                    }

                    y = box_audio_clean(box_audio_svf_drive(&svf[0], (int32_t)kind, y, 0.4f));
                    test_assert(audio_l[i] == y);
                }
            }
        }
    }
}

/** The ladder renders the same twice and differs from the SVF, so the model reaches the
 * voice. */
void SynthLadder_deterministic(void) {
    // audio/synth
    box_audio_synth_patch_t p = sl_patch(BOX_AUDIO_SYNTH_SAW, 110, 0.25f, 0.5f,
        BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, 800, 0.7f);
    uint64_t hash[3];
    int32_t n;
    for (n = 0; n < 3; n ++) {
        box_audio_core_t *core = sl_core(AUDIO_RATE);
        p.model = n < 2 ? BOX_AUDIO_SYNTH_LADDER : BOX_AUDIO_SYNTH_SVF;
        sl_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 24000);
        hash[n] = audio_hash(audio_l, 24000);
        box_audio_core_free(core);
    }

    test_assert(hash[0] == hash[1]);
    test_assert(hash[0] != hash[2]);
}
