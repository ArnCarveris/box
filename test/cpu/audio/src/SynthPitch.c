/**
 * @file SynthPitch.c
 * @brief The pitch envelope of synth sounds, measured on offline renders of a core: the start
 * offset and its return to the note, its curve, the release amount of a gated note, a pitch
 * envelope of a layer, the sweep of the ratio of hard sync, a pluck string, the stereo path of
 * unison, sounds without it, the clamping of the fields, and a script.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Frames of a window of the centroid, 10 periods of 200 Hz at AUDIO_RATE */
#define PITCH_WINDOW 2400

/** Windows of the centroid, after PITCH_SKIP */
#define PITCH_WINDOWS 18

/** Frames skipped before the windows of the centroid */
#define PITCH_SKIP 480

/** Samples of the renders a test compares */
static float pitch_a_l[AUDIO_FRAMES];
static float pitch_a_r[AUDIO_FRAMES];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void pitch_define(
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

/** Release a voice of a core with a single command.
 *
 * @param core The core
 * @param voice Handle of the voice
 * @param when Frame of the release
 */
static void pitch_release(
    box_audio_core_t *core,
    uint32_t voice,
    uint64_t when)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_voice_release(&w, voice, 0, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** A patch of one sine at a steady level for a second.
 *
 * @param frequency Hz
 * @return The patch
 */
static box_audio_synth_patch_t pitch_patch(
    float frequency)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 1, .release = 0.01f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = frequency, .level = 0.5f };

    return p;
}

/** Render a patch in a new core at AUDIO_RATE into audio_l and audio_r, a second, with a
 * release at a frame of a gated play.
 *
 * @param p The patch
 * @param release Frame of the release of a gated play, 0 for a play without a gate
 */
static void pitch_render(
    const box_audio_synth_patch_t *p,
    int32_t release)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL);
    pitch_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, release ? BOX_AUDIO_PLAY_GATE : 0);
    if (release) {
        pitch_release(core, 1, (uint64_t)release);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
}

/** Render a patch into pitch_a_l and pitch_a_r.
 *
 * @param p The patch
 */
static void pitch_render_a(
    const box_audio_synth_patch_t *p)
{
    pitch_render(p, 0);
    memcpy(pitch_a_l, audio_l, sizeof(pitch_a_l));
    memcpy(pitch_a_r, audio_r, sizeof(pitch_a_r));
}

/** Whether the last render is the same as the one in pitch_a_l and pitch_a_r, bit for bit.
 *
 * @return Whether it is
 */
static bool pitch_same_a(void)
{
    return memcmp(pitch_a_l, audio_l, sizeof(pitch_a_l)) == 0 &&
        memcmp(pitch_a_r, audio_r, sizeof(pitch_a_r)) == 0;
}

/** Whether every sample of a range is finite.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return Whether they are
 */
static bool pitch_finite(
    const float *x,
    int32_t from,
    int32_t to)
{
    bool result = true;
    int32_t i;
    for (i = from; i < to && result; i ++) {
        result = isfinite(x[i]);
    }

    return result;
}

/** Frequency of a short window of a render from its zero crossings.
 *
 * @param x Samples
 * @param from Start of the window in seconds
 * @param to End of the window in seconds
 * @return Hz
 */
static double pitch_at(
    const float *x,
    double from,
    double to)
{
    return audio_frequency(x, (int32_t)(from * AUDIO_RATE), (int32_t)(to * AUDIO_RATE));
}

/** Whether a frequency is within a share of the one expected, printed.
 *
 * @param what Name of the measure
 * @param f The frequency
 * @param expected The frequency expected
 * @param share The share it may be off by
 * @return Whether it is
 */
static bool pitch_near(
    const char *what,
    double f,
    double expected,
    double share)
{
    printf("%s: %.2f Hz, expected %.2f\n", what, f, expected);

    return fabs(f / expected - 1) < share;
}

/** Energy of the component of a range at a frequency of an exact bin, with a Goertzel filter.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz, a whole number of periods in n samples
 * @return The sum of the squares of the component
 */
static double pitch_bin_energy(
    const float *x,
    int32_t n,
    double frequency)
{
    double c = 2 * cos(2 * AUDIO_PI * frequency / AUDIO_RATE), s0, s1 = 0, s2 = 0;
    int32_t i;
    for (i = 0; i < n; i ++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return 2 * (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/** Spectral centroid of a window of a render, in harmonics of a master of 200 Hz.
 *
 * @param x Samples of the window, PITCH_WINDOW of them
 * @return The centroid
 */
static double pitch_centroid(
    const float *x)
{
    double sum = 0, weighted = 0, e;
    int32_t k;
    for (k = 1; k * 200 < 20000; k ++) {
        e = pitch_bin_energy(x, PITCH_WINDOW, k * 200.0);
        sum += e;
        weighted += k * e;
    }

    return weighted / sum;
}

/** Component of a range at a frequency over a Hann window, the phase relative to frame 0.
 *
 * @param x Samples
 * @param from First frame of the window
 * @param window Frames of the window
 * @param frequency Hz
 * @param re Receives the real part
 * @param im Receives the imaginary part
 */
static void pitch_component(
    const float *x,
    int32_t from,
    int32_t window,
    double frequency,
    double *re,
    double *im)
{
    double r = 0, m = 0, w, ph;
    int32_t i;
    for (i = 0; i < window; i ++) {
        w = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / window);
        ph = 2 * AUDIO_PI * frequency * (double)(from + i) / AUDIO_RATE;
        r += w * x[from + i] * cos(ph);
        m -= w * x[from + i] * sin(ph);
    }

    *re = r;
    *im = m;
}

/** Cents of the fundamental of a render around a frame over a frequency, from the advance of
 * its phase between two Hann windows near a guess, which a pluck needs: its harmonics cross
 * zero more than once a period.
 *
 * @param x Samples
 * @param center Frame at the middle of the measure
 * @param guess Hz near the fundamental, within 50 Hz
 * @param frequency Hz the cents are relative to
 * @return Cents
 */
static double pitch_cents_at(
    const float *x,
    int32_t center,
    double guess,
    double frequency)
{
    int32_t gap = AUDIO_RATE / 100, window = AUDIO_RATE / 50, from;
    double r0, i0, r1, i1, phase, f;
    from = center - (gap + window) / 2;
    pitch_component(x, from, window, guess, &r0, &i0);
    pitch_component(x, from + gap, window, guess, &r1, &i1);
    phase = atan2(i1 * r0 - r1 * i0, r1 * r0 + i1 * i0);
    f = guess + phase * AUDIO_RATE / (2 * AUDIO_PI * gap);

    return 1200 * log2(f / frequency);
}

/** A sine at 220 Hz with an amount of 24 semitones and a decay of 0.1 s starts two octaves up,
 * passes an octave up halfway, linear in semitones, and is back at the note after 0.1 s. */
void SynthPitch_start_returns(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 24, .decay = 0.1f };
    pitch_render(&p, 0);
    test_assert(pitch_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(pitch_near("start", pitch_at(audio_l, 0, 0.004), 220 * 4 * pow(2, -0.54 / 12),
        0.02));
    test_assert(pitch_near("halfway", pitch_at(audio_l, 0.045, 0.055), 440, 0.02));
    test_assert(pitch_near("after the decay", pitch_at(audio_l, 0.105, 0.2), 220, 0.002));
    test_assert(pitch_near("late", pitch_at(audio_l, 0.5, 0.9), 220, 0.001));
}

/** A negative amount starts under the note, and a decay of 0 leaves the note alone. */
void SynthPitch_negative_amount(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(880);
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = -12, .decay = 0.2f };
    pitch_render(&p, 0);
    test_assert(pitch_near("under", pitch_at(audio_l, 0, 0.01), 440 * pow(2, 0.3 / 12),
        0.02));
    test_assert(pitch_near("back", pitch_at(audio_l, 0.21, 0.4), 880, 0.002));

    p.pitch_envelope.decay = 0;
    pitch_render(&p, 0);
    test_assert(pitch_near("decay 0", pitch_at(audio_l, 0, 0.05), 880, 0.005));
}

/** A curve of 6 drops faster at first: a fifth of the way into the decay the curve has
 * dropped most of the amount, the linear decay a fifth of it, and both end on the note. */
void SynthPitch_curve(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    double linear, curved, expected;
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 24, .decay = 0.1f };
    pitch_render(&p, 0);
    linear = pitch_at(audio_l, 0.015, 0.025);
    p.pitch_envelope.curve = 6;
    pitch_render(&p, 0);
    curved = pitch_at(audio_l, 0.015, 0.025);
    expected = (exp(-6 * 0.2) - exp(-6)) / (1 - exp(-6));
    test_assert(pitch_near("linear at 0.02", linear, 220 * pow(2, 24 * 0.8 / 12), 0.03));
    test_assert(pitch_near("curve 6 at 0.02", curved, 220 * pow(2, 24 * expected / 12), 0.03));
    test_assert(curved < linear * 0.6);
    test_assert(pitch_near("curve 6 after", pitch_at(audio_l, 0.105, 0.2), 220, 0.002));

    p.pitch_envelope.curve = -6;
    pitch_render(&p, 0);
    test_assert(pitch_at(audio_l, 0.015, 0.025) > linear * 1.05);
}

/** A release amount of -12 drops a gated note an octave over the release of its envelope, from
 * the note, linear in semitones; before the release it plays the note. */
void SynthPitch_release_amount(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    int32_t release = AUDIO_RATE * 4 / 10;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.005f, .sustain = 1, .hold = 0.1f,
        .release = 0.4f };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .release_amount = -12 };
    pitch_render(&p, release);
    test_assert(pitch_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(pitch_near("held", pitch_at(audio_l, 0.2, 0.4), 220, 0.002));
    test_assert(pitch_near("half the release", pitch_at(audio_l, 0.59, 0.61),
        220 * pow(2, -0.5), 0.02));
    test_assert(pitch_near("end of the release", pitch_at(audio_l, 0.75, 0.77),
        220 * pow(2, -0.9), 0.02));
}

/** The release starts from the offset the decay left: with an amount of 12 over a decay of a
 * second released at 0.2 s it moves from about 9.6 semitones down to the release amount. */
void SynthPitch_release_from_decay(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    int32_t release = AUDIO_RATE / 5;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.005f, .sustain = 1, .hold = 0.1f,
        .release = 0.4f };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 12, .decay = 1,
        .release_amount = 0 };
    pitch_render(&p, release);
    test_assert(pitch_near("at the release", pitch_at(audio_l, 0.19, 0.2),
        220 * pow(2, 12 * 0.805 / 12), 0.02));
    test_assert(pitch_near("half the release", pitch_at(audio_l, 0.395, 0.405),
        220 * pow(2, 9.6 * 0.5 / 12), 0.02));
}

/** A layer with its own amplitude envelope releases its pitch envelope over its own release:
 * the root envelope releases at once, the layer over 0.4 s, and halfway it is 6 semitones
 * down. */
void SynthPitch_release_own_envelope(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    int32_t release = AUDIO_RATE * 4 / 10;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 0.1f, .release = 0.01f };
    p.layer[0].has_envelope = 1;
    p.layer[0].envelope = (box_audio_synth_envelope_t){ .attack = 0.005f, .sustain = 1,
        .hold = 0.1f, .release = 0.4f };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .release_amount = -12 };
    pitch_render(&p, release);
    test_assert(pitch_near("held", pitch_at(audio_l, 0.2, 0.4), 220, 0.002));
    test_assert(pitch_near("half the release", pitch_at(audio_l, 0.59, 0.61),
        220 * pow(2, -0.5), 0.02));
}

/** A legato note retargets the pitch of a voice without restarting its pitch envelope: after
 * the decay, a fifth up plays the fifth from its start. */
void SynthPitch_legato_keeps(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    uint8_t bytes[64];
    box_audio_writer_t w;
    test_assert(core != NULL);
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 12, .decay = 0.1f };
    pitch_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato_at(&w, 2, 1, 1, 0, 1, 0, 1.5f, BOX_AUDIO_PLAY_GATE,
        (uint64_t)(AUDIO_RATE * 3 / 10)));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(pitch_near("before", pitch_at(audio_l, 0.2, 0.3), 220, 0.002));
    test_assert(pitch_near("legato", pitch_at(audio_l, 0.305, 0.315), 330, 0.005));
}

/** The pitch envelope of a layer moves that layer only, in place of the one of the root: a
 * layer with its own envelope of all 0 plays its note under a root of 24 semitones. */
void SynthPitch_layer(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330, .level = 0,
        .has_pitch_envelope = 1, .pitch_envelope = { .amount = 12, .decay = 0.5f } };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 24, .decay = 0.5f };
    pitch_render(&p, 0);
    test_assert(pitch_near("root layer", pitch_at(audio_l, 0, 0.01),
        220 * pow(2, 24 * 0.99 / 12), 0.02));

    p.layer[0].level = 0;
    p.layer[1].level = 0.5f;
    pitch_render(&p, 0);
    test_assert(pitch_near("own layer", pitch_at(audio_l, 0, 0.01),
        330 * pow(2, 12 * 0.99 / 12), 0.02));

    p.layer[1].pitch_envelope = (box_audio_synth_pitch_envelope_t){0};
    pitch_render(&p, 0);
    test_assert(pitch_near("layer of none", pitch_at(audio_l, 0, 0.05), 330, 0.005));

    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){0};
    p.layer[1].has_pitch_envelope = 0;
    p.layer[0].has_pitch_envelope = 1;
    p.layer[0].pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 24,
        .decay = 0.5f };
    pitch_render(&p, 0);
    test_assert(pitch_near("other layer", pitch_at(audio_l, 0, 0.05), 330, 0.005));
}

/** The sync field sweeps the spectrum of a synced saw down as the ratio returns to its own,
 * while the saw keeps its pitch; on a saw without sync it changes nothing. */
void SynthPitch_sync_sweep(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(200);
    double first, last, lo = 1e9, hi = -1e9, c;
    int32_t w;
    p.layer[0].wave = BOX_AUDIO_SYNTH_SAW;
    p.layer[0].level = 0.3f;
    p.layer[0].sync = 2;
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .sync = 24, .decay = 0.5f };
    pitch_render(&p, 0);
    test_assert(pitch_finite(audio_l, 0, AUDIO_FRAMES));
    for (w = 0; w < PITCH_WINDOWS; w ++) {
        c = pitch_centroid(audio_l + PITCH_SKIP + w * PITCH_WINDOW);
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
    }

    first = pitch_centroid(audio_l + PITCH_SKIP);
    last = pitch_centroid(audio_l + PITCH_SKIP + (PITCH_WINDOWS - 1) * PITCH_WINDOW);
    printf("centroid of sync 2 under a pitch envelope of 24 semitones: %f first, %f last, "
        "range %f\n", first, last, hi - lo);
    test_assert(hi - lo > 2);
    test_assert(first > last + 2);

    p.layer[0].sync = 0;
    pitch_render_a(&p);
    lo = 1e9;
    hi = -1e9;
    for (w = 0; w < PITCH_WINDOWS; w ++) {
        c = pitch_centroid(audio_l + PITCH_SKIP + w * PITCH_WINDOW);
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
    }

    printf("centroid of a saw without sync: range %f\n", hi - lo);
    test_assert(hi - lo < 0.1);
}

/** A pluck string follows the pitch envelope as it follows vibrato: it starts up by the
 * amount, at the offset of the decay, and rings at its note after it. */
void SynthPitch_pluck(void) {
    // audio/synth
    box_audio_synth_patch_t p = {0};
    double early, late, plain;
    p.layers = 1;
    p.seed = 3;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 220, .level = 0.5f,
        .decay = 4, .brightness = 0.3f, .hardness = 0.5f, .rings_free = 1 };
    pitch_render(&p, 0);
    plain = pitch_cents_at(audio_l, AUDIO_RATE * 3 / 40, 220, 220);
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 12, .decay = 0.3f };
    pitch_render(&p, 0);
    test_assert(pitch_finite(audio_l, 0, AUDIO_FRAMES));
    early = pitch_cents_at(audio_l, AUDIO_RATE * 3 / 40, 220 * pow(2, 0.75), 220);
    late = pitch_cents_at(audio_l, AUDIO_RATE / 2, 220, 220);
    printf("pluck: %.2f cents at 0.075 s, %.2f at 0.5 s, %.2f without\n", early, late, plain);
    test_assert(fabs(plain) < 3);
    test_assert(fabs(early - 900) < 40);
    test_assert(fabs(late) < 3);
    test_assert(audio_peak(audio_l, AUDIO_FRAMES / 2, AUDIO_FRAMES) > 0.01f);
}

/** A unison layer in stereo, the wide path, takes the pitch envelope on both channels. */
void SynthPitch_wide(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    p.layer[0].voices = 3;
    p.layer[0].width = 1;
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 24, .decay = 0.1f };
    pitch_render(&p, 0);
    test_assert(pitch_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(pitch_finite(audio_r, 0, AUDIO_FRAMES));
    test_assert(pitch_near("wide left halfway", pitch_at(audio_l, 0.045, 0.055), 440, 0.02));
    test_assert(pitch_near("wide right halfway", pitch_at(audio_r, 0.045, 0.055), 440, 0.02));
    test_assert(pitch_near("wide after", pitch_at(audio_l, 0.105, 0.2), 220, 0.002));
}

/** It adds to an LFO of the pitch and to a sweep: with both, the pitch halfway through the
 * decay is the sum of the three. */
void SynthPitch_adds(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(220);
    p.layer[0].frequency_end = 440;
    p.layer[0].sweep = 1;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SQUARE, .rate = 0.1f,
        .pitch = 100 };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 24, .decay = 0.1f };
    pitch_render(&p, 0);
    test_assert(pitch_near("sum halfway", pitch_at(audio_l, 0.045, 0.055),
        220 * pow(2, (1200 + 100 + 1200 * 0.05) / 1200), 0.02));
    test_assert(pitch_near("sum after", pitch_at(audio_l, 0.2, 0.21),
        220 * pow(2, (100 + 1200 * 0.205) / 1200), 0.01));
}

/** Sounds without a pitch envelope render bit identical: a decay and a curve of a root
 * envelope without amounts, and a layer with its own envelope of all 0, on a mono patch with
 * a filter, phase modulation, a sweep and LFOs, on a wide unison patch and on a pluck. */
void SynthPitch_none_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p = {0}, q;
    p.layers = 3;
    p.seed = 3;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.1f, .sustain = 0.6f, .hold = 0.5f, .release = 0.1f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 3000;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 220, .sweep = 0.3f, .level = 0.3f, .sync = 2 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_TRIANGLE, .frequency = 220,
        .level = 0.3f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330,
        .level = 1, .modulates = 2, .index = 1.5f, .feedback = 0.3f };
    pitch_render_a(&p);
    q = p;
    q.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .decay = 0.3f, .curve = 4 };
    q.layer[1].has_pitch_envelope = 1;
    pitch_render(&q, 0);
    test_assert(pitch_same_a());

    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 20, .hard_sync = 12 };
    pitch_render_a(&p);
    q = p;
    q.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .decay = 0.3f, .curve = 4 };
    q.layer[2].has_pitch_envelope = 1;
    pitch_render(&q, 0);
    test_assert(pitch_same_a());

    p.layer[0].voices = 5;
    p.layer[0].detune = 30;
    p.layer[0].width = 1;
    pitch_render_a(&p);
    q = p;
    q.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .decay = 0.3f };
    pitch_render(&q, 0);
    test_assert(pitch_same_a());

    p = (box_audio_synth_patch_t){0};
    p.layers = 1;
    p.seed = 9;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 220, .level = 0.5f,
        .decay = 1, .brightness = 0.5f, .hardness = 0.5f, .rings_free = 1 };
    pitch_render_a(&p);
    q = p;
    q.layer[0].has_pitch_envelope = 1;
    q.layer[0].pitch_envelope.decay = 1;
    pitch_render(&q, 0);
    test_assert(pitch_same_a());
}

/** Amounts over 48 semitones render as 48, NaN ones and a NaN decay as 0, and a curve over 16
 * as 16. */
void SynthPitch_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = pitch_patch(110);
    p.layer[0].sync = 2;
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 48, .decay = 0.2f,
        .curve = 16, .release_amount = -48, .sync = 48 };
    pitch_render_a(&p);
    test_assert(pitch_finite(audio_l, 0, AUDIO_FRAMES));
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 1000, .decay = 0.2f,
        .curve = 100, .release_amount = -1000, .sync = 1000 };
    pitch_render(&p, 0);
    test_assert(pitch_same_a());

    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){0};
    p.layer[0].sync = 0;
    pitch_render_a(&p);
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = NAN, .decay = NAN,
        .curve = NAN, .release_amount = NAN, .sync = NAN };
    pitch_render(&p, 0);
    test_assert(pitch_same_a());
}

/** test/assets/audio/synth_pitch.flecs compiles the pitch envelope of the root and the own one
 * of a layer, the defaults are none, a change of the component compiles the sound again, and
 * the sounds play. */
void SynthPitch_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_pitch.flecs");
    ecs_entity_t kick, lead, plain;
    const box_audio_synth_patch_t *p;
    int32_t start;
    test_assert(ecs_script_run(world, "pitch", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);

    kick = ecs_lookup(world, "Kick");
    lead = ecs_lookup(world, "SyncZap");
    plain = ecs_lookup(world, "Plain");
    test_assert(kick != 0 && lead != 0 && plain != 0);
    test_flt(ecs_get(world, kick, SynthPitchEnvelope)->amount, 36);
    test_flt(ecs_get(world, kick, SynthPitchEnvelope)->release_amount, 0);
    p = &ecs_get(world, kick, AudioSynth)->patch;
    test_int(p->layers, 1);
    test_flt(p->pitch_envelope.amount, 36);
    test_flt(p->pitch_envelope.decay, 0.08f);
    test_flt(p->pitch_envelope.curve, 6);
    test_int(p->layer[0].has_pitch_envelope, 0);

    p = &ecs_get(world, lead, AudioSynth)->patch;
    test_int(p->layers, 2);
    test_flt(p->pitch_envelope.amount, 0);
    test_int(p->layer[0].has_pitch_envelope, 0);
    test_int(p->layer[1].has_pitch_envelope, 1);
    test_flt(p->layer[1].pitch_envelope.sync, 24);
    test_flt(p->layer[1].pitch_envelope.amount, 12);
    test_flt(p->layer[1].pitch_envelope.decay, 0.3f);
    test_flt(p->layer[1].pitch_envelope.release_amount, -12);

    p = &ecs_get(world, plain, AudioSynth)->patch;
    test_flt(p->pitch_envelope.amount, 0);
    test_flt(p->pitch_envelope.decay, 0);
    test_int(p->layer[0].has_pitch_envelope, 0);

    ecs_set(world, plain, SynthPitchEnvelope, { .amount = -7, .decay = 0.2f });
    audio_run(world, 1);
    p = &ecs_get(world, plain, AudioSynth)->patch;
    test_flt(p->pitch_envelope.amount, -7);
    test_flt(p->pitch_envelope.decay, 0.2f);

    audio_run(world, 1);
    start = audio_tap_count;
    test_assert(box_audio_play(world, kick, 1.0f));
    test_assert(box_audio_play(world, lead, 1.0f));
    audio_run(world, 4);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(pitch_finite(audio_tap_l, start, audio_tap_count));
    ecs_fini(world);
}
