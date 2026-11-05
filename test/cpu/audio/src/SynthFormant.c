/**
 * @file SynthFormant.c
 * @brief The formant filter of the synth, SynthFilter.kind SynthFormant: the spectral peaks of
 * a saw at the formants of the vowels of the table and between them, the vowel moved by an
 * LFO and by the filter envelope, its level across the vowels, the bandpasses of svf.h with
 * drive, the stereo path, the clamps, and the other kinds, which stay bit-identical.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Hz of the saw of the tests */
#define SF_SAW 110.0

/** Frames that hold a whole number of periods of SF_SAW at AUDIO_RATE: 11 */
#define SF_PERIODS 4800

/** Harmonics of the saw that the spectral envelope looks at, up to 3.3 kHz */
#define SF_HARMONICS 30

/** Level of the saw, so the limiter of the master stays out of it */
#define SF_LEVEL 0.05f

/** Output of the reference of the identical cases */
static float sf_ref[AUDIO_FRAMES];

/** Create a core at AUDIO_RATE.
 *
 * @return The core
 */
static box_audio_core_t* sf_core(void)
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
static void sf_define(
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

/** A patch of a saw at SF_SAW Hz held for a second through a formant filter.
 *
 * @param vowel The vowel, 0..4
 * @return The patch
 */
static box_audio_synth_patch_t sf_patch(
    float vowel)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 1.5f, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SAW, .frequency = (float)SF_SAW, .level = SF_LEVEL };
    p.filter = BOX_AUDIO_SYNTH_FORMANT;
    p.cutoff = 1000;
    p.vowel = vowel;

    return p;
}

/** Render a second of a patch into audio_l and audio_r.
 *
 * @param p The patch
 */
static void sf_render(
    const box_audio_synth_patch_t *p)
{
    box_audio_core_t *core = sf_core();
    sf_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
}

/** Amplitude of a frequency in a range of samples at AUDIO_RATE, from its DFT bin.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param frequency Hz
 * @return Amplitude
 */
static double sf_amplitude(
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

/** RMS of a range of samples.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The RMS
 */
static double sf_rms(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * x[i];
    }

    return sqrt(sum / (double)(to - from));
}

/** The first two spectral peaks of a saw at SF_SAW through a filter: the amplitudes of its
 * harmonics times their number, which takes out the 1 / h of the saw and leaves the response of
 * the filter, and the first two local maxima of those within 30 dB of the highest, each refined
 * by a parabola through the dB of its neighbours.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame, from plus a multiple of SF_PERIODS
 * @param f1 Receives the first peak in Hz
 * @param f2 Receives the second peak in Hz, 0 for none
 */
static void sf_peaks(
    const float *x,
    int32_t from,
    int32_t to,
    double *f1,
    double *f2)
{
    double db[SF_HARMONICS + 2], top = -1000, a, b, c, d, f;
    int32_t h, found = 0;
    for (h = 1; h <= SF_HARMONICS + 1; h ++) {
        db[h] = 20 * log10(sf_amplitude(x, from, to, SF_SAW * h) * h + 1e-12);
        top = db[h] > top ? db[h] : top;
    }

    *f1 = 0;
    *f2 = 0;
    for (h = 2; h <= SF_HARMONICS && found < 2; h ++) {
        if (db[h] > db[h - 1] && db[h] >= db[h + 1] && db[h] > top - 30) {
            a = db[h - 1];
            b = db[h];
            c = db[h + 1];
            d = a - 2 * b + c;
            d = d < 0 ? 0.5 * (a - c) / d : 0.0;
            f = ((double)h + d) * SF_SAW;
            if (found == 0) {
                *f1 = f;
            } else {
                *f2 = f;
            }

            found ++;
        }
    }
}

/** Check that a peak is within 10% of a frequency.
 *
 * @param peak The peak in Hz
 * @param frequency The formant in Hz
 */
static void sf_near(
    double peak,
    double frequency)
{
    test_assert(peak > frequency * 0.9 && peak < frequency * 1.1);
}

/** Vowel 0 puts the peaks of a saw at 110 Hz within 10% of the first two formants of a in the
 * table, 650 and 1080 Hz. */
void SynthFormant_vowel_a(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double f1, f2;
    sf_render(&p);
    sf_peaks(audio_l, 9600, 48000, &f1, &f2);
    printf("vowel 0 (a): peaks at %.1f and %.1f Hz\n", f1, f2);
    sf_near(f1, 650);
    sf_near(f2, 1080);
}

/** Vowel 2 puts them near the formants of i, 290 and 1870 Hz. */
void SynthFormant_vowel_i(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(2);
    double f1, f2;
    sf_render(&p);
    sf_peaks(audio_l, 9600, 48000, &f1, &f2);
    printf("vowel 2 (i): peaks at %.1f and %.1f Hz\n", f1, f2);
    sf_near(f1, 290);
    sf_near(f2, 1870);
}

/** Vowel 0.5 puts them between those of a (650, 1080 Hz) and e (400, 1700 Hz), near the
 * interpolated 525 and 1390 Hz. */
void SynthFormant_vowel_between(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0.5f);
    double f1, f2;
    sf_render(&p);
    sf_peaks(audio_l, 9600, 48000, &f1, &f2);
    printf("vowel 0.5 (a to e): peaks at %.1f and %.1f Hz\n", f1, f2);
    test_assert(f1 > 400 && f1 < 650);
    test_assert(f2 > 1080 && f2 < 1700);
    test_assert(fabs(f1 - 525) < 0.15 * 525);
    test_assert(fabs(f2 - 1390) < 0.15 * 1390);
}

/** An LFO on the vowel moves the peaks: a sine at 0.5 Hz from its top with a depth of 2
 * vowels on vowel 0 starts at i and reaches a, clamped, from 0.5 s on. */
void SynthFormant_lfo(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double e1, e2, l1, l2;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 0.5f, .phase = 0.25f, .vowel = 2 };
    sf_render(&p);
    sf_peaks(audio_l, 0, SF_PERIODS, &e1, &e2);
    sf_peaks(audio_l, 28800, 43200, &l1, &l2);
    printf("LFO on the vowel: peaks at %.1f and %.1f Hz, then %.1f and %.1f Hz\n",
        e1, e2, l1, l2);
    test_assert(e1 < 400);
    test_assert(e2 > 1600);
    sf_near(l1, 650);
    sf_near(l2, 1080);
}

/** The filter envelope on the vowel moves the peaks, with an amount of 0: at level 1 with a
 * vowel depth of 2 it holds vowel 0 at i for 0.4 s, then its release returns it to a. */
void SynthFormant_envelope(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double e1, e2, l1, l2;
    p.filter_vowel = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.001f, .sustain = 1, .hold = 0.4f, .release = 0.1f };
    sf_render(&p);
    sf_peaks(audio_l, 4800, 4800 + 2 * SF_PERIODS, &e1, &e2);
    sf_peaks(audio_l, 28800, 43200, &l1, &l2);
    printf("filter envelope on the vowel: peaks at %.1f and %.1f Hz, then %.1f and %.1f Hz\n",
        e1, e2, l1, l2);
    sf_near(e1, 290);
    sf_near(e2, 1870);
    sf_near(l1, 650);
    sf_near(l2, 1080);
}

/** The filter envelope amount and an LFO of the cutoff leave a formant filter alone: with
 * them it renders bit-identical to without. */
void SynthFormant_cutoff_fixed(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(1.5f);
    uint64_t plain, moved;
    sf_render(&p);
    plain = audio_hash(audio_l, AUDIO_FRAMES);
    p.cutoff = 300;
    p.slope = BOX_AUDIO_SYNTH_SLOPE_24;
    p.model = BOX_AUDIO_SYNTH_LADDER;
    p.keytrack = 1;
    p.filter_velocity = 2;
    p.filter_amount = 4;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.1f, .decay = 0.3f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3, .cutoff = 3 };
    sf_render(&p);
    moved = audio_hash(audio_l, AUDIO_FRAMES);
    test_assert(plain == moved);
}

/** A saw at 110 Hz through the formant filter stays within 3 dB of its RMS without a filter
 * at every vowel from 0 to 4 in steps of 0.25. */
void SynthFormant_level(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double dry, wet, db;
    int32_t v;
    p.filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    sf_render(&p);
    dry = sf_rms(audio_l, 9600, 48000);
    for (v = 0; v <= 16; v ++) {
        p = sf_patch(0.25f * (float)v);
        sf_render(&p);
        wet = sf_rms(audio_l, 9600, 48000);
        db = 20 * log10(wet / dry);
        printf("vowel %.2f: %.2f dB against no filter\n", 0.25 * v, db);
        test_assert(fabs(db) < 3);
    }
}

/** The formant filter is the three bandpasses of svf.h at the coefficients of formant.h, each
 * driven: a voice renders bit-identical to the unfiltered voice run through them, with a
 * resonance and a drive. */
void SynthFormant_bandpasses(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(1.3f);
    box_audio_formant_t f;
    int32_t i;
    p.resonance = 0.4f;
    p.drive = 0.6f;
    p.filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    sf_render(&p);
    memcpy(sf_ref, audio_l, AUDIO_FRAMES * sizeof(float));
    p.filter = BOX_AUDIO_SYNTH_FORMANT;
    sf_render(&p);
    box_audio_formant_init(&f, 0.4f);
    box_audio_formant_vowel(&f, 1.3f, AUDIO_RATE, 0.49 * AUDIO_RATE);
    for (i = 0; i < AUDIO_FRAMES; i ++) {
        float y = box_audio_clean(box_audio_formant_next(&f, f.svf, sf_ref[i], 0.6f));
        test_assert(audio_l[i] == y);
    }
}

/** On the stereo path both channels run the same bandpasses: each renders bit-identical to its
 * unfiltered channel run through them. */
void SynthFormant_wide_bandpasses(void) {
    // audio/synth
    static float ref_r[AUDIO_FRAMES];
    box_audio_synth_patch_t p = sf_patch(2.7f);
    box_audio_formant_t f;
    int32_t i;
    p.resonance = 0.2f;
    p.drive = 0.3f;
    p.layer[0].voices = 3;
    p.layer[0].detune = 15;
    p.layer[0].width = 0.8f;
    p.filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    sf_render(&p);
    memcpy(sf_ref, audio_l, AUDIO_FRAMES * sizeof(float));
    memcpy(ref_r, audio_r, AUDIO_FRAMES * sizeof(float));
    p.filter = BOX_AUDIO_SYNTH_FORMANT;
    sf_render(&p);
    box_audio_formant_init(&f, 0.2f);
    box_audio_formant_vowel(&f, 2.7f, AUDIO_RATE, 0.49 * AUDIO_RATE);
    for (i = 0; i < AUDIO_FRAMES; i ++) {
        float l = box_audio_clean(box_audio_formant_next(&f, f.svf, sf_ref[i], 0.3f));
        float r = box_audio_clean(box_audio_formant_next(&f, f.svf_r, ref_r[i], 0.3f));
        test_assert(audio_l[i] == l);
        test_assert(audio_r[i] == r);
    }
}

/** Resonance narrows the bandwidths, so the peaks stand out more against the valley between
 * them, and drive changes the output and keeps it bounded. */
void SynthFormant_resonance_drive(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double open, narrow, f1, f2;
    uint64_t linear;
    sf_render(&p);
    linear = audio_hash(audio_l, AUDIO_FRAMES);
    open = sf_amplitude(audio_l, 9600, 48000, 660) /
        (sf_amplitude(audio_l, 9600, 48000, 880) + 1e-12);
    p.resonance = 1;
    sf_render(&p);
    narrow = sf_amplitude(audio_l, 9600, 48000, 660) /
        (sf_amplitude(audio_l, 9600, 48000, 880) + 1e-12);
    sf_peaks(audio_l, 9600, 48000, &f1, &f2);
    printf("660 over 880 Hz: %.2f at resonance 0, %.2f at 1; peaks at %.1f and %.1f Hz\n",
        open, narrow, f1, f2);
    test_assert(narrow > open * 2);
    sf_near(f1, 650);

    p.resonance = 0.5f;
    p.drive = 1;
    p.layer[0].level = 1;
    sf_render(&p);
    test_assert(audio_hash(audio_l, AUDIO_FRAMES) != linear);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) < 8);
}

/** On the stereo path of unison with width both channels run the formant filter: two voices
 * without detune at width 1, one saw on each channel, each have their peaks at the formants
 * of a. */
void SynthFormant_wide(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double f1, f2;
    p.layer[0].voices = 2;
    p.layer[0].detune = 0;
    p.layer[0].width = 1;
    sf_render(&p);
    sf_peaks(audio_l, 9600, 48000, &f1, &f2);
    printf("wide, left: peaks at %.1f and %.1f Hz\n", f1, f2);
    sf_near(f1, 650);
    sf_near(f2, 1080);
    sf_peaks(audio_r, 9600, 48000, &f1, &f2);
    printf("wide, right: peaks at %.1f and %.1f Hz\n", f1, f2);
    sf_near(f1, 650);
    sf_near(f2, 1080);
}

/** On the stereo path an LFO on the vowel moves the peaks of both channels alike. */
void SynthFormant_wide_lfo(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(0);
    double f1, f2;
    int32_t c;
    p.layer[0].voices = 2;
    p.layer[0].detune = 0;
    p.layer[0].width = 1;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 0.5f, .phase = 0.25f, .vowel = 2 };
    sf_render(&p);
    for (c = 0; c < 2; c ++) {
        const float *x = c ? audio_r : audio_l;
        sf_peaks(x, 0, SF_PERIODS, &f1, &f2);
        test_assert(f1 < 400);
        test_assert(f2 > 1600);
        sf_peaks(x, 28800, 43200, &f1, &f2);
        sf_near(f1, 650);
        sf_near(f2, 1080);
    }
}

/** The core clamps the vowel to 0..4 and the depths to -4..4, and the moved vowel to 0..4: a
 * vowel of 3 that the filter envelope moves by 4 at its sustain level 1 settles at 4. */
void SynthFormant_clamped(void) {
    // audio/synth
    box_audio_core_t *core = sf_core();
    box_audio_synth_patch_t p = sf_patch(7);
    const box_audio_synth_patch_t *q;
    int32_t i;
    bool found = false;
    p.filter_vowel = -9;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .rate = 1, .vowel = 12 };
    sf_define(core, 1, &p);
    q = &core->synths[core->sounds[1].synth - 1].patch;
    test_assert(q->filter == BOX_AUDIO_SYNTH_FORMANT);
    test_assert(q->vowel == 4);
    test_assert(q->filter_vowel == -4);
    test_assert(q->lfo[0].vowel == 4);

    p.vowel = -1;
    p.filter_vowel = NAN;
    p.lfo[0].vowel = NAN;
    sf_define(core, 2, &p);
    q = &core->synths[core->sounds[2].synth - 1].patch;
    test_assert(q->vowel == 0);
    test_assert(q->filter_vowel == 0);
    test_assert(q->lfo[0].vowel == 0);

    p.filter = BOX_AUDIO_SYNTH_FORMANT + 1;
    sf_define(core, 3, &p);
    q = &core->synths[core->sounds[3].synth - 1].patch;
    test_assert(q->filter == BOX_AUDIO_SYNTH_FILTER_NONE);

    p = sf_patch(3);
    p.filter_vowel = 4;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.001f, .sustain = 1, .hold = 1, .release = 0.1f };
    sf_define(core, 4, &p);
    audio_play(core, 1, 4, 0, 1, 0, 1, 0);
    audio_render(core, 0, 9600);
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS; i ++) {
        const box_audio_voice_t *v = &core->voices[i];
        if (v->state == BOX_AUDIO_VOICE_PLAYING && v->handle == 1 &&
            v->source.kind == BOX_AUDIO_SOURCE_SYNTH)
        {
            found = true;
            test_assert(v->source.data.synth.formant.vowel == 4);
        }
    }

    test_assert(found);
    box_audio_core_free(core);
}

/** The vowel and its depths do nothing on the other kinds and models: with them each renders
 * bit-identical to without, on the mono and the stereo path. */
void SynthFormant_others_identical(void) {
    // audio/synth
    uint32_t kind, model;
    int32_t wide;
    for (wide = 0; wide < 2; wide ++) {
        for (model = BOX_AUDIO_SYNTH_SVF; model <= BOX_AUDIO_SYNTH_LADDER; model ++) {
            for (kind = BOX_AUDIO_SYNTH_FILTER_NONE; kind <= BOX_AUDIO_SYNTH_NOTCH; kind ++) {
                box_audio_synth_patch_t p = sf_patch(0);
                uint64_t plain, vowel;
                p.filter = kind;
                p.model = model;
                p.cutoff = 900;
                p.resonance = 0.5f;
                p.drive = 0.3f;
                p.filter_amount = 2;
                p.filter_envelope = (box_audio_synth_envelope_t){
                    .attack = 0.05f, .decay = 0.2f, .sustain = 0.3f, .hold = 0.3f,
                    .release = 0.1f };
                p.lfos = 1;
                p.lfo[0] = (box_audio_synth_lfo_t){
                    .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 4, .cutoff = 1 };
                if (wide) {
                    p.layer[0].voices = 3;
                    p.layer[0].detune = 12;
                    p.layer[0].width = 0.7f;
                }

                sf_render(&p);
                plain = audio_hash(audio_l, AUDIO_FRAMES) ^
                    (audio_hash(audio_r, AUDIO_FRAMES) * 31);
                p.vowel = 2.5f;
                p.filter_vowel = 3;
                p.lfo[0].vowel = -2;
                sf_render(&p);
                vowel = audio_hash(audio_l, AUDIO_FRAMES) ^
                    (audio_hash(audio_r, AUDIO_FRAMES) * 31);
                test_assert(plain == vowel);
            }
        }
    }
}

/** A formant filter whose vowel the LFO and the filter envelope move renders the same twice. */
void SynthFormant_deterministic(void) {
    // audio/synth
    box_audio_synth_patch_t p = sf_patch(1);
    uint64_t hash[2];
    int32_t n;
    p.filter_vowel = 1.5f;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.2f, .decay = 0.3f, .sustain = 0.2f, .release = 0.1f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 6, .vowel = 1 };
    for (n = 0; n < 2; n ++) {
        sf_render(&p);
        hash[n] = audio_hash(audio_l, AUDIO_FRAMES);
    }

    test_assert(hash[0] == hash[1]);
}
