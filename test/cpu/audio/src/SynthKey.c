/**
 * @file SynthKey.c
 * @brief Key tracking of a synth voice: SynthFilter.keytrack moves the cutoff with the pitch
 * of a voice, glides included, and SynthEnvelope.key_scale shortens the decay and the release
 * of higher notes. Plays on a core, and notes of an Instrument on a world with the null output.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Frames of a segment of the spectrum, Hann windowed */
#define KEY_SEGMENT 2048

/** Segments of the spectrum averaged into a measure of a peak */
#define KEY_SEGMENTS 8

/** Frames from the start of a play to the window whose peak is measured */
#define KEY_SKIP 2400

/** Steps of the grid of frequencies of a peak per octave */
#define KEY_GRID 48

/** Frames of a millisecond */
#define KEY_MS 48

/** Samples of the renders of a test */
static float key_a[AUDIO_FRAMES];
static float key_b[AUDIO_FRAMES];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void key_define(
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

/** Noise through a resonant lowpass at 1000 Hz, whose peak is at the cutoff, for 2 seconds.
 *
 * @param keytrack Keytrack of the filter
 * @return The patch
 */
static box_audio_synth_patch_t key_noise(
    float keytrack)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.01f,
        .sustain = 1, .hold = 2, .release = 0.05f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 1000;
    p.resonance = 0.9f;
    p.keytrack = keytrack;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_NOISE, .frequency = 440, .level = 0.05f };

    return p;
}

/** A saw at 220 Hz through a lowpass at 1000 Hz for 2 seconds, as a unison of 3 copies over
 * the full width when wide, which renders in stereo.
 *
 * @param keytrack Keytrack of the filter
 * @param glide Seconds of a glide
 * @param wide Whether it is a unison with width
 * @return The patch
 */
static box_audio_synth_patch_t key_saw(
    float keytrack,
    float glide,
    bool wide)
{
    box_audio_synth_patch_t p = key_noise(keytrack);
    p.glide = glide;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 220, .level = 0.2f };
    if (wide) {
        p.layer[0].voices = 3;
        p.layer[0].detune = 10;
        p.layer[0].width = 1;
    }

    return p;
}

/** The cutoff of the synth voice of a handle that plays.
 *
 * @param core The core
 * @param handle Handle of the voice
 * @return The cutoff in Hz
 */
static double key_cutoff(
    box_audio_core_t *core,
    uint32_t handle)
{
    double result = 0;
    test_assert(audio_synth_cutoff(core, handle, &result, NULL));

    return result;
}

/** Power of a frequency in a range, averaged over Hann windowed segments of KEY_SEGMENT.
 *
 * @param x Samples
 * @param from First frame
 * @param segments Segments from it
 * @param f Hz
 * @return The power
 */
static double key_power(
    const float *x,
    int32_t from,
    int32_t segments,
    double f)
{
    double w = 2 * cos(2 * AUDIO_PI * f / AUDIO_RATE), sum = 0, s0, s1, s2, h;
    int32_t k, i;
    for (k = 0; k < segments; k ++) {
        const float *seg = x + from + k * KEY_SEGMENT;
        s1 = 0;
        s2 = 0;
        for (i = 0; i < KEY_SEGMENT; i ++) {
            h = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / (KEY_SEGMENT - 1));
            s0 = (double)seg[i] * h + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        sum += s1 * s1 + s2 * s2 - w * s1 * s2;
    }

    return sum;
}

/** Peak of the spectrum of a range in octaves over 1 Hz: the highest power on a grid of
 * KEY_GRID steps per octave from 125 Hz to 16 kHz, refined by the centroid in octaves of the
 * power within an eighth of an octave of it.
 *
 * @param x Samples
 * @param from First frame
 * @return log2 of the frequency of the peak
 */
static double key_peak(
    const float *x,
    int32_t from)
{
    static double power[7 * KEY_GRID + 1];
    double sum = 0, weight = 0;
    int32_t n = 7 * KEY_GRID + 1, best = 0, i;
    for (i = 0; i < n; i ++) {
        power[i] = key_power(x, from, KEY_SEGMENTS, 125 * pow(2.0, (double)i / KEY_GRID));
        best = power[i] > power[best] ? i : best;
    }

    for (i = best - KEY_GRID / 8; i <= best + KEY_GRID / 8; i ++) {
        if (i >= 0 && i < n) {
            sum += power[i] * (log2(125.0) + (double)i / KEY_GRID);
            weight += power[i];
        }
    }

    return sum / weight;
}

/** Play a patch at a pitch on a core of its own and measure the peak of its spectrum.
 *
 * @param p The patch
 * @param pitch Pitch ratio of the play
 * @return log2 of the frequency of the peak
 */
static double key_render_peak(
    const box_audio_synth_patch_t *p,
    float pitch)
{
    box_audio_core_t *core = audio_new();
    key_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, pitch, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    return key_peak(audio_l, KEY_SKIP);
}

/** Play a patch at a pitch on a core of its own and give the cutoff of its voice after a
 * render of a frames.
 *
 * @param p The patch
 * @param pitch Pitch ratio of the play
 * @param frames Frames to render
 * @return The cutoff in Hz
 */
static double key_render_cutoff(
    const box_audio_synth_patch_t *p,
    float pitch,
    int32_t frames)
{
    box_audio_core_t *core = audio_new();
    double result;
    key_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, pitch, 0);
    audio_render(core, 0, frames);
    result = key_cutoff(core, 1);
    box_audio_core_free(core);

    return result;
}

/** With keytrack 1 the resonant peak of noise through a lowpass moves an octave up with a play
 * an octave up and an octave down with one an octave down, as the cutoff of the voice does. */
void SynthKey_keytrack_octave(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_noise(1);
    double root = key_render_peak(&p, 1), up = key_render_peak(&p, 2);
    double down = key_render_peak(&p, 0.5f);
    printf("keytrack 1: peak at %f Hz, %f Hz an octave up, %f Hz an octave down\n",
        pow(2.0, root), pow(2.0, up), pow(2.0, down));
    test_assert(fabs(root - log2(1000.0)) < 0.1);
    test_assert(fabs(up - root - 1) < 0.06);
    test_assert(fabs(down - root + 1) < 0.06);

    test_assert(fabs(key_render_cutoff(&p, 1, 4800) - 1000) < 1e-6);
    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 2000) < 1e-6);
    test_assert(fabs(key_render_cutoff(&p, 0.5f, 4800) - 500) < 1e-6);
}

/** With keytrack 0.5 a play an octave up moves the cutoff half an octave, and one two octaves
 * up an octave. */
void SynthKey_keytrack_half(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_noise(0.5f);
    double root = key_render_peak(&p, 1), up = key_render_peak(&p, 2);
    printf("keytrack 0.5: peak at %f Hz, %f Hz an octave up\n", pow(2.0, root), pow(2.0, up));
    test_assert(fabs(up - root - 0.5) < 0.06);

    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 1000 * sqrt(2.0)) < 1e-3);
    test_assert(fabs(key_render_cutoff(&p, 4, 4800) - 2000) < 1e-3);
}

/** With keytrack 0, the default, the pitch of a play does not reach the filter: noise through
 * the lowpass, which no pitch changes, renders bit-identical at any pitch. */
void SynthKey_keytrack_off(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_noise(0);
    box_audio_core_t *core = audio_new();
    key_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    memcpy(key_a, audio_l, sizeof(float) * AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    core = audio_new();
    key_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 2, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(memcmp(key_a, audio_l, sizeof(float) * AUDIO_FRAMES / 2) == 0);
    test_assert(fabs(key_cutoff(core, 1) - 1000) < 1e-6);
    box_audio_core_free(core);
}

/** Glide a voice with keytrack 1 two octaves up over 0.2 s and check its cutoff: an octave up
 * halfway, at the control rate, and two octaves up at the end.
 *
 * @param wide Whether the patch renders on the stereo path of unison
 */
static void key_glide(
    bool wide)
{
    box_audio_synth_patch_t p = key_saw(1, 0.2f, wide);
    box_audio_core_t *core = audio_new();
    double c;
    bool unison = !wide;
    key_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(audio_synth_cutoff(core, 1, &c, &unison));
    test_assert(unison == wide);
    test_assert(fabs(c - 1000) < 1e-6);

    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 4);
    audio_render(core, 0, 2400);
    c = key_cutoff(core, 1);
    printf("a quarter into the glide: %f Hz\n", c);
    test_assert(fabs(log2(c / 1000) - 0.5) < 0.01);
    audio_render(core, 0, 2400);
    c = key_cutoff(core, 1);
    printf("halfway: %f Hz\n", c);
    test_assert(fabs(log2(c / 1000) - 1) < 0.01);
    audio_render(core, 0, 4800);
    c = key_cutoff(core, 1);
    printf("after the glide: %f Hz\n", c);
    test_assert(fabs(c - 4000) < 1e-3);
    audio_render(core, 0, 4800);
    test_assert(fabs(key_cutoff(core, 1) - 4000) < 1e-3);
    box_audio_core_free(core);
}

/** A glide moves the cutoff along with the pitch on the mono path. */
void SynthKey_keytrack_glide(void) {
    // audio/synth
    key_glide(false);
}

/** A glide moves the cutoff along with the pitch on the stereo path of unison with width. */
void SynthKey_keytrack_glide_wide(void) {
    // audio/synth
    key_glide(true);
}

/** Without a glide a change of the pitch moves the cutoff at once. */
void SynthKey_keytrack_jump(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_saw(1, 0, false);
    box_audio_core_t *core = audio_new();
    key_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(fabs(key_cutoff(core, 1) - 1000) < 1e-6);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 2);
    audio_render(core, 0, 64);
    test_assert(fabs(key_cutoff(core, 1) - 2000) < 1e-3);
    box_audio_core_free(core);
}

/** A legato note retargets the key of the voice it follows: its cutoff follows the pitch of
 * the new note. */
void SynthKey_keytrack_legato(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_saw(1, 0.05f, false);
    box_audio_core_t *core = audio_new();
    uint8_t bytes[64];
    box_audio_writer_t w;
    key_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato(&w, 2, 1, 1, 0, 1, 0, 0.5f, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, 4800);
    test_int(box_audio_core_voice_count(core), 1);
    test_assert(fabs(key_cutoff(core, 2) - 500) < 1e-3);
    box_audio_core_free(core);
}

/** Keytrack adds to the filter envelope and to an LFO of the cutoff: at the sustain of an
 * envelope of an octave, a play an octave up opens the filter two octaves. */
void SynthKey_keytrack_envelope(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_saw(1, 0, false);
    p.filter_amount = 1;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.01f,
        .sustain = 1, .hold = 2, .release = 0.05f };
    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 4000) < 1e-3);

    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 0,
        .cutoff = 1 };
    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 4000) < 1e-3);
}

/** Last frame that is not silent.
 *
 * @param x Samples
 * @param n Frames
 * @return The frame, -1 when every one is silent
 */
static int32_t key_last(
    const float *x,
    int32_t n)
{
    int32_t i;
    for (i = n - 1; i >= 0 && x[i] == 0; i --) { }

    return i;
}

/** A sine at 300 Hz with an envelope, its own when layer is set.
 *
 * @param env The envelope
 * @param layer Whether the layer has the envelope instead of the patch
 * @return The patch
 */
static box_audio_synth_patch_t key_sine(
    box_audio_synth_envelope_t env,
    bool layer)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 300, .level = 0.5f };
    if (layer) {
        p.layer[0].has_envelope = 1;
        p.layer[0].envelope = env;
    } else {
        p.envelope = env;
    }

    return p;
}

/** Play a patch at a pitch on a core of its own and give the last frame it is heard.
 *
 * @param p The patch
 * @param pitch Pitch ratio of the play
 * @return The frame
 */
static int32_t key_render_end(
    const box_audio_synth_patch_t *p,
    float pitch)
{
    box_audio_core_t *core = audio_new();
    int32_t result;
    key_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, pitch, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    result = key_last(audio_l, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);

    return result;
}

/** Check the ends of a decay of 0.4 s at the root, an octave up and an octave down.
 *
 * @param layer Whether the layer has the envelope instead of the patch
 * @param key_scale Key scale of the envelope
 * @param up Expected seconds of the decay an octave up
 * @param down Expected seconds of the decay an octave down
 */
static void key_decay(
    bool layer,
    float key_scale,
    double up,
    double down)
{
    box_audio_synth_patch_t p = key_sine((box_audio_synth_envelope_t){
        .attack = 0.002f, .decay = 0.4f, .sustain = 0, .key_scale = key_scale }, layer);
    int32_t root = key_render_end(&p, 1), hi = key_render_end(&p, 2);
    int32_t lo = key_render_end(&p, 0.5f), attack = (int32_t)(0.002 * AUDIO_RATE + 0.5);
    printf("key_scale %g: decay ends at %d, %d an octave up, %d an octave down\n",
        (double)key_scale, root, hi, lo);
    test_assert(abs(root - (attack + (int32_t)(0.4 * AUDIO_RATE))) <= KEY_MS);
    test_assert(abs(hi - (attack + (int32_t)(up * AUDIO_RATE))) <= KEY_MS);
    test_assert(abs(lo - (attack + (int32_t)(down * AUDIO_RATE))) <= KEY_MS);
}

/** key_scale 1 halves the decay an octave up and doubles it an octave down. */
void SynthKey_key_scale_decay(void) {
    // audio/synth
    key_decay(false, 1, 0.2, 0.8);
}

/** key_scale 1 on the own envelope of a layer scales its decay the same way. */
void SynthKey_key_scale_layer(void) {
    // audio/synth
    key_decay(true, 1, 0.2, 0.8);
}

/** key_scale 0.5 scales the decay by a square root of 2 per octave. */
void SynthKey_key_scale_half(void) {
    // audio/synth
    key_decay(false, 0.5f, 0.4 / sqrt(2.0), 0.4 * sqrt(2.0));
}

/** key_scale 0, the default, keeps the decay at every pitch. */
void SynthKey_key_scale_off(void) {
    // audio/synth
    key_decay(false, 0, 0.4, 0.4);
}

/** key_scale 1 halves the release an octave up and doubles it an octave down; the attack and
 * the hold stay. */
void SynthKey_key_scale_release(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_sine((box_audio_synth_envelope_t){
        .attack = 0.002f, .decay = 0.1f, .sustain = 0.5f, .hold = 0.1f, .release = 0.3f,
        .key_scale = 1 }, false);
    int32_t root = key_render_end(&p, 1), hi = key_render_end(&p, 2);
    int32_t lo = key_render_end(&p, 0.5f);
    printf("key_scale 1: release ends at %d, %d an octave up, %d an octave down\n",
        root, hi, lo);
    test_assert(abs(root - (int32_t)(0.502 * AUDIO_RATE)) <= KEY_MS);
    test_assert(abs(hi - (int32_t)(0.302 * AUDIO_RATE)) <= KEY_MS);
    test_assert(abs(lo - (int32_t)(0.902 * AUDIO_RATE)) <= KEY_MS);
}

/** A patch with an envelope, a filter envelope, an LFO of the cutoff, a modulator and a glide
 * whose pitch moves.
 *
 * @param keytrack Keytrack of the filter
 * @param key_scale Key scale of the envelopes
 * @return The patch
 */
static box_audio_synth_patch_t key_full(
    float keytrack,
    float key_scale)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 2;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.05f,
        .sustain = 0.6f, .hold = 0.2f, .release = 0.1f, .curve = 3, .key_scale = key_scale };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 800;
    p.resonance = 0.3f;
    p.slope = BOX_AUDIO_SYNTH_SLOPE_24;
    p.keytrack = keytrack;
    p.filter_amount = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.2f,
        .sustain = 0.2f, .hold = 0.2f, .release = 0.1f };
    p.glide = 0.05f;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 3,
        .cutoff = 0.5f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 220, .level = 0.3f };
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 440, .level = 1, .modulates = 1,
        .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 2, .has_envelope = 1,
        .envelope = { .attack = 0.01f, .decay = 0.3f, .sustain = 0.3f, .hold = 0.1f,
            .release = 0.1f, .key_scale = key_scale } };

    return p;
}

/** Hash of a play of a patch at the root, whose pitch changes and comes back.
 *
 * @param p The patch
 * @return The hash
 */
static uint64_t key_full_hash(
    const box_audio_synth_patch_t *p)
{
    box_audio_core_t *core = audio_new();
    uint64_t result;
    key_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 9600);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 1);
    audio_render(core, 9600, AUDIO_FRAMES - 9600);
    result = audio_hash(audio_l, AUDIO_FRAMES);
    box_audio_core_free(core);

    return result;
}

/** At the root, a pitch of 1, keytrack and key_scale change nothing: a patch with both at 1
 * renders bit-identical to the same patch with both at 0, the defaults. */
void SynthKey_defaults_identical(void) {
    // audio/synth
    box_audio_synth_patch_t off = key_full(0, 0), on = key_full(1, 1);
    test_assert(key_full_hash(&off) == key_full_hash(&on));
}

/** The core clamps keytrack and key_scale to 0..1, NaN to 0, and keeps the key_scale of the
 * filter envelope at 0. */
void SynthKey_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = key_noise(2), q;
    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 2000) < 1e-3);
    p.keytrack = NAN;
    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 1000) < 1e-6);
    p.keytrack = -1;
    test_assert(fabs(key_render_cutoff(&p, 2, 4800) - 1000) < 1e-6);

    p = key_sine((box_audio_synth_envelope_t){
        .attack = 0.002f, .decay = 0.4f, .sustain = 0, .key_scale = 5 }, false);
    q = p;
    q.envelope.key_scale = 1;
    test_int(key_render_end(&p, 2), key_render_end(&q, 2));
    p.envelope.key_scale = NAN;
    q.envelope.key_scale = 0;
    test_int(key_render_end(&p, 2), key_render_end(&q, 2));

    p = key_noise(0);
    p.filter_amount = 1;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.4f,
        .sustain = 0, .key_scale = 1 };
    q = p;
    q.filter_envelope.key_scale = 0;
    test_assert(key_render_cutoff(&p, 2, 9600) == key_render_cutoff(&q, 2, 9600));
}

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
static char* key_read(
    const char *file)
{
    char *path = box_app_locate(file);
    FILE *f;
    char *result;
    long size;
    test_assert(path != NULL);
    f = fopen(path, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    result = ecs_os_malloc((ecs_size_t)size + 1);
    test_assert(fread(result, 1, (size_t)size, f) == (size_t)size);
    result[size] = '\0';
    fclose(f);
    ecs_os_free(path);

    return result;
}

/** The key fields set in a script read back as authored, and a note of the scripted
 * instrument an octave above its root has the resonant peak an octave above that of a note
 * at its root: the key is relative to Instrument.root. */
void SynthKey_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = key_read("test/assets/audio/synth_key.flecs");
    ecs_entity_t noise, lead;
    int32_t at, next;
    double root, up;
    test_assert(ecs_script_run(world, "synth_key", code, NULL) == 0);
    ecs_os_free(code);
    noise = ecs_lookup(world, "key_noise");
    lead = ecs_lookup(world, "key_lead");
    test_assert(noise != 0 && lead != 0);
    test_flt(ecs_get(world, noise, SynthFilter)->keytrack, 1);
    test_flt(ecs_get(world, noise, SynthEnvelope)->key_scale, 0.5f);

    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, lead, 60, 1, 1));
    audio_run(world, 50);
    next = audio_tap_count;
    test_assert(box_audio_play_note(world, lead, 72, 1, 1));
    audio_run(world, 50);
    test_assert(next + KEY_SKIP + KEY_SEGMENT * KEY_SEGMENTS <= audio_tap_count);
    root = key_peak(audio_tap_l, at + KEY_SKIP);
    up = key_peak(audio_tap_l, next + KEY_SKIP);
    printf("scripted keytrack: peak at %f Hz at the root, %f Hz an octave up\n",
        pow(2.0, root), pow(2.0, up));
    test_assert(fabs(root - log2(1000.0)) < 0.1);
    test_assert(fabs(up - root - 1) < 0.06);
    ecs_fini(world);
}
