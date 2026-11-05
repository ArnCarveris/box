/**
 * @file SynthModMatrix.c
 * @brief The modulation matrix of synth sounds, SynthModRoute and SynthModEnvelope: a route
 * per source kind and per target on short notes of a core, on the mono and the stereo render
 * paths, the hardwired mod and pressure that a route turns off, layer routes, the ninth route
 * that logs an error, sounds without routes that render as before, and the patch a script
 * compiles to.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Samples of the renders a test compares */
static float matrix_a[AUDIO_FRAMES];
static float matrix_b[AUDIO_FRAMES];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void matrix_define(
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

/** Play a sound at a velocity with a single command, at volume 0.5 in the center.
 *
 * @param core The core
 * @param voice Handle of the voice
 * @param sound Id of the sound
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note
 */
static void matrix_play(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound,
    float pitch,
    float velocity)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, voice, sound, 0, 0.5f, 0, pitch, velocity,
        0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
}

/** Ramp a parameter of a voice to a value at once with a single command.
 *
 * @param core The core
 * @param voice Handle of the voice
 * @param param The parameter
 * @param value The value
 */
static void matrix_express(
    box_audio_core_t *core,
    uint32_t voice,
    box_audio_param_t param,
    float value)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_voice_ramp(&w, voice, param, value, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
}

/** The state of the synth voice of a handle, which must play.
 *
 * @param core The core
 * @param handle Handle of the voice
 * @return The state
 */
static audio_synth_probe_t matrix_source(
    box_audio_core_t *core,
    uint32_t handle)
{
    audio_synth_probe_t result;
    test_assert(audio_synth_probe(core, handle, &result));

    return result;
}

/** A patch of one sine at a steady level for two seconds.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @return The patch
 */
static box_audio_synth_patch_t matrix_patch(
    uint32_t wave,
    float frequency)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.002f, .sustain = 1, .hold = 2, .release = 0.01f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = wave, .frequency = frequency,
        .level = 0.5f };

    return p;
}

/** Add a route to a patch.
 *
 * @param p The patch
 * @param source A box_audio_synth_source_kind_t
 * @param target A box_audio_synth_target_t
 * @param layer 1 + the layer it moves, 0 for every layer
 * @param amount The amount
 */
static void matrix_route(
    box_audio_synth_patch_t *p,
    uint32_t source,
    uint32_t target,
    uint32_t layer,
    float amount)
{
    p->route[p->routes ++] = (box_audio_synth_route_t){
        .source = source, .target = target, .layer = layer, .amount = amount };
}

/** Energy of a range of samples.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The energy
 */
static double matrix_energy(
    const float *x,
    int32_t from,
    int32_t to)
{
    double result = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        result += (double)x[i] * x[i];
    }

    return result;
}

/** Magnitude of one frequency of a range, from a Hann window.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param frequency Hz
 * @return The magnitude
 */
static double matrix_component(
    const float *x,
    int32_t from,
    int32_t to,
    double frequency)
{
    double re = 0, im = 0, w, ph;
    int32_t i, n = to - from;
    for (i = 0; i < n; i ++) {
        w = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / n);
        ph = 2 * AUDIO_PI * frequency * (double)i / AUDIO_RATE;
        re += w * x[from + i] * cos(ph);
        im -= w * x[from + i] * sin(ph);
    }

    return sqrt(re * re + im * im) * 2 / n;
}

/** The mod envelope routed to the cutoff sweeps it: up over its attack, down over its decay,
 * from the cutoff of the patch, while the amp envelope holds. */
void SynthModMatrix_mod_envelope_cutoff(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SAW, 110);
    box_audio_core_t *core = audio_new();
    double early, top, late;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 300;
    p.mod_envelope = (box_audio_synth_envelope_t){
        .attack = 0.2f, .decay = 0.2f, .sustain = 0, .release = 0.05f };
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_CUTOFF, 0, 4);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_RATE / 20);
    test_assert(audio_synth_cutoff(core, 1, &early, NULL));
    audio_render(core, 0, AUDIO_RATE * 3 / 20);
    test_assert(audio_synth_cutoff(core, 1, &top, NULL));
    audio_render(core, 0, AUDIO_RATE / 4);
    test_assert(audio_synth_cutoff(core, 1, &late, NULL));
    printf("mod envelope to cutoff: %f, %f, %f Hz\n", early, top, late);
    test_assert(early > 300 * 1.3 && early < 300 * 4);
    test_assert(top > 300 * 12 && top <= 300 * 16.01);
    test_assert(fabs(late - 300) < 300 * 0.01);
    test_assert(matrix_source(core, 1).menv_on);
    box_audio_core_free(core);
}

/** Render a sine through a route from LFO 2 to the pan, a sine of 2 Hz, and check that the
 * right channel is louder a quarter cycle in and the left one three quarters in.
 *
 * @param voices Unison voices of the layer, 1 for the mono path
 * @param width Width of the unison, over 0 for a stereo render
 */
static void matrix_pan(
    uint32_t voices,
    float width)
{
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 440);
    box_audio_core_t *core = audio_new();
    double l, r;
    p.layer[0].voices = voices;
    p.layer[0].detune = 5;
    p.layer[0].width = width;
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5 };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2 };
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_LFO2, BOX_AUDIO_SYNTH_TO_PAN, 0, 1);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    l = matrix_energy(audio_l, 5400, 6600);
    r = matrix_energy(audio_r, 5400, 6600);
    printf("lfo 2 to pan, %u voices, width %.1f: left %f right %f at 0.125 s\n",
        (unsigned)voices, (double)width, l, r);
    test_assert(r > l * 10);
    l = matrix_energy(audio_l, 17400, 18600);
    r = matrix_energy(audio_r, 17400, 18600);
    printf("lfo 2 to pan, %u voices, width %.1f: left %f right %f at 0.375 s\n",
        (unsigned)voices, (double)width, l, r);
    test_assert(l > r * 10);
    test_assert(audio_max_step(audio_l, 1, AUDIO_FRAMES / 2) < 0.2f);
    test_assert(audio_max_step(audio_r, 1, AUDIO_FRAMES / 2) < 0.2f);
    box_audio_core_free(core);
}

/** LFO 2 routed to the pan moves the stereo balance from side to side, on the mono path and on
 * the unison paths, mono and with width. */
void SynthModMatrix_lfo2_pan(void) {
    // audio/synth
    matrix_pan(1, 0);
    matrix_pan(3, 0);
    matrix_pan(3, 0.2f);
}

/** LFO 1 routed to the pulse width of a square layer moves its width either way of the width
 * of the layer, after the delay of the LFO. */
void SynthModMatrix_lfo1_pulse_width(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SQUARE, 220);
    box_audio_core_t *core = audio_new();
    double lo = 1, hi = 0, w;
    int32_t i;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 4,
        .delay = 0.1f };
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_PULSE_WIDTH, 1, 0.3f);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_RATE / 20);
    w = matrix_source(core, 1).width[0];
    test_assert(fabs(w - 0.5) < 1e-6);
    for (i = 0; i < 40; i ++) {
        audio_render(core, 0, AUDIO_RATE / 80);
        w = matrix_source(core, 1).width[0];
        lo = w < lo ? w : lo;
        hi = w > hi ? w : hi;
    }

    printf("lfo 1 to pulse width: %f to %f\n", lo, hi);
    test_assert(lo < 0.25 && lo >= BOX_AUDIO_SYNTH_PULSE_MIN);
    test_assert(hi > 0.75 && hi <= 1 - BOX_AUDIO_SYNTH_PULSE_MIN);
    box_audio_core_free(core);
}

/** Velocity routed to the pitch detunes loud notes: 12 semitones at velocity 1 play an octave
 * up, at 0.5 half an octave up. */
void SynthModMatrix_velocity_pitch(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 220);
    box_audio_core_t *core;
    double f;
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_VELOCITY, BOX_AUDIO_SYNTH_TO_PITCH, 0, 12);

    core = audio_new();
    matrix_define(core, 1, &p);
    matrix_play(core, 1, 1, 1, 1);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    f = audio_frequency(audio_l, 2400, AUDIO_FRAMES / 4);
    printf("velocity to pitch: %f Hz at velocity 1\n", f);
    test_assert(fabs(f - 440) < 440 * 0.002);
    box_audio_core_free(core);

    core = audio_new();
    matrix_define(core, 1, &p);
    matrix_play(core, 1, 1, 1, 0.5f);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    f = audio_frequency(audio_l, 2400, AUDIO_FRAMES / 4);
    printf("velocity to pitch: %f Hz at velocity 0.5\n", f);
    test_assert(fabs(f - 220 * sqrt(2)) < 311 * 0.002);
    box_audio_core_free(core);
}

/** Collect the random values of three notes of a patch with a seed, played at once.
 *
 * @param seed The seed
 * @param out Receives the three values
 */
static void matrix_randoms(
    uint32_t seed,
    float *out)
{
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 440);
    box_audio_core_t *core = audio_new();
    uint32_t i;
    p.seed = seed;
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_RANDOM, BOX_AUDIO_SYNTH_TO_PITCH, 0, 2);
    matrix_define(core, 1, &p);
    for (i = 0; i < 3; i ++) {
        audio_play(core, i + 1, 1, 0, 0.2f, 0, 1, 0);
    }

    audio_render(core, 0, BOX_AUDIO_CORE_QUANTUM);
    for (i = 0; i < 3; i ++) {
        out[i] = matrix_source(core, i + 1).route_random;
        test_assert(out[i] >= -1 && out[i] <= 1);
    }

    box_audio_core_free(core);
}

/** The random source differs per note, is the same for the same seed and the same count of
 * plays, differs for another seed, and moves the pitch of its note by its amount. */
void SynthModMatrix_random_per_note(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 440);
    box_audio_core_t *core;
    float a[3], b[3], c[3], r;
    double f, expected;
    matrix_randoms(5, a);
    matrix_randoms(5, b);
    matrix_randoms(6, c);
    printf("random per note: %f %f %f, seed 6: %f %f %f\n", (double)a[0], (double)a[1],
        (double)a[2], (double)c[0], (double)c[1], (double)c[2]);
    test_assert(a[0] != a[1] && a[1] != a[2] && a[0] != a[2]);
    test_assert(memcmp(a, b, sizeof(a)) == 0);
    test_assert(memcmp(a, c, sizeof(a)) != 0);

    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_RANDOM, BOX_AUDIO_SYNTH_TO_PITCH, 0, 2);
    core = audio_new();
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    r = matrix_source(core, 1).route_random;
    test_flt(r, a[0]);
    f = audio_frequency(audio_l, 2400, AUDIO_FRAMES / 4);
    expected = 440 * pow(2, 2 * (double)r / 12);
    printf("random to pitch: %f Hz, expected %f\n", f, expected);
    test_assert(fabs(f - expected) < expected * 0.002);
    box_audio_core_free(core);
}

/** Pressure routed to the resonance raises the resonance and turns off the hardwired cutoff
 * that pressure opens: the cutoff stays where it is. */
void SynthModMatrix_pressure_resonance(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SAW, 110);
    box_audio_core_t *core = audio_new();
    audio_synth_probe_t src;
    double cutoff;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 300;
    p.resonance = 0.1f;
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_PRESSURE, BOX_AUDIO_SYNTH_TO_RESONANCE, 0, 0.8f);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 8);
    src = matrix_source(core, 1);
    test_flt(src.resonance, 0.1f);
    matrix_express(core, 1, BOX_AUDIO_PARAM_PRESSURE, 1);
    audio_render(core, 0, AUDIO_FRAMES / 8);
    src = matrix_source(core, 1);
    test_assert(audio_synth_cutoff(core, 1, &cutoff, NULL));
    printf("pressure to resonance: resonance %f, cutoff %f Hz\n", (double)src.resonance,
        cutoff);
    test_assert(fabs(src.resonance - 0.9f) < 1e-5f);
    test_assert(fabs(cutoff - 300) < 300 * 0.001);
    box_audio_core_free(core);
}

/** A route from mod turns off the hardwired depth of the vibrato that mod scales: at mod 0 the
 * LFO still moves the pitch, and the route moves the level by mod. */
void SynthModMatrix_mod_level(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 220);
    box_audio_core_t *core = audio_new();
    double full, half, lo = INFINITY, hi = 0, f;
    int32_t at;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 100 };
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_MOD, BOX_AUDIO_SYNTH_TO_LEVEL, 0, -0.5f);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    matrix_express(core, 1, BOX_AUDIO_PARAM_MOD, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    for (at = 4800; at + 1200 <= AUDIO_FRAMES; at += 1200) {
        f = audio_frequency(audio_l, at, at + 1200);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }

    printf("mod to level: vibrato %f to %f Hz at mod 0\n", lo, hi);
    test_assert(hi / lo > 1.03);
    full = audio_peak(audio_l, 4800, AUDIO_FRAMES);
    box_audio_core_free(core);

    core = audio_new();
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    half = audio_peak(audio_l, 4800, AUDIO_FRAMES / 2);
    printf("mod to level: peak %f at mod 0, %f at mod 1\n", full, half);
    test_assert(fabs(half / full - 0.5) < 0.01);
    box_audio_core_free(core);
}

/** The filter envelope routed to the pitch runs without a filter: a sine starts 12 semitones
 * up and falls back to its note over the decay of the envelope. */
void SynthModMatrix_filter_envelope_pitch(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 220);
    box_audio_core_t *core = audio_new();
    double early, late;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.001f, .decay = 0.3f, .sustain = 0, .release = 0.01f };
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_FILTER, BOX_AUDIO_SYNTH_TO_PITCH, 0, 12);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    early = audio_frequency(audio_l, 100, 1100);
    late = audio_frequency(audio_l, AUDIO_RATE * 2 / 5, AUDIO_FRAMES / 2);
    printf("filter envelope to pitch: %f Hz early, %f Hz late\n", early, late);
    test_assert(early > 400);
    test_assert(fabs(late - 220) < 220 * 0.002);
    box_audio_core_free(core);
}

/** The amp envelope of a modulator routed to its index by a route of its layer adds radians to
 * the index while the envelope is up and none once it has decayed. */
void SynthModMatrix_amp_index(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 220);
    box_audio_core_t *core = audio_new();
    audio_synth_probe_t src;
    float early, late;
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 220,
        .level = 1, .modulates = 1, .index = 0.5f, .has_envelope = 1,
        .envelope = { .attack = 0.002f, .decay = 0.2f, .sustain = 0, .release = 0.01f } };
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_AMP, BOX_AUDIO_SYNTH_TO_INDEX, 2, 3);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_RATE / 100);
    src = matrix_source(core, 1);
    early = src.index_route[1];
    audio_render(core, 0, AUDIO_RATE / 2);
    src = matrix_source(core, 1);
    late = src.index_route[1];
    printf("amp envelope to index: %f radians early, %f late\n", (double)early, (double)late);
    test_assert(early > 2.5f && early <= 3);
    test_assert(late == 0);
    test_flt(src.index_route[0], 0);
    box_audio_core_free(core);
}

/** The key routed to the unison detune widens the spread of a note an octave up by the amount,
 * and leaves the note at the root alone. */
void SynthModMatrix_key_detune(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SAW, 110);
    box_audio_core_t *core = audio_new();
    audio_synth_probe_t src;
    p.layer[0].voices = 3;
    p.layer[0].detune = 10;
    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_KEY, BOX_AUDIO_SYNTH_TO_DETUNE, 0, 30);
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.2f, 0, 1, 0);
    audio_play(core, 2, 1, 0, 0.2f, 0, 2, 0);
    audio_render(core, 0, AUDIO_RATE / 10);
    src = matrix_source(core, 1);
    test_assert(fabs(src.spread[0] - 10) < 1e-9);
    src = matrix_source(core, 2);
    printf("key to detune: %f cents an octave up\n", src.spread[0]);
    test_assert(fabs(src.spread[0] - 40) < 1e-4);
    test_assert(fabs(src.detune[0][2] / src.detune[0][0] -
        pow(2, 40.0 / 1200)) < 1e-6);
    box_audio_core_free(core);
}

/** A route of a layer moves only that layer: velocity at an amount of -1 on the level of the
 * second of two sines silences it and leaves the first, the same route on the root silences
 * both. */
void SynthModMatrix_layer_route(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SINE, 220);
    box_audio_core_t *core;
    double low, high;
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330,
        .level = 0.5f };
    core = audio_new();
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    low = matrix_component(audio_l, 2400, AUDIO_FRAMES / 4, 220);
    high = matrix_component(audio_l, 2400, AUDIO_FRAMES / 4, 330);
    printf("without the route: %f at 220 Hz, %f at 330 Hz\n", low, high);
    test_assert(high > low * 0.9);
    box_audio_core_free(core);

    matrix_route(&p, BOX_AUDIO_SYNTH_FROM_VELOCITY, BOX_AUDIO_SYNTH_TO_LEVEL, 2, -1);
    core = audio_new();
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    memcpy(matrix_a, audio_l, sizeof(float) * AUDIO_FRAMES / 4);
    low = matrix_component(matrix_a, 2400, AUDIO_FRAMES / 4, 220);
    high = matrix_component(matrix_a, 2400, AUDIO_FRAMES / 4, 330);
    printf("with the route of layer 2: %f at 220 Hz, %f at 330 Hz\n", low, high);
    test_assert(low > 0.05);
    test_assert(high < low * 0.001);
    box_audio_core_free(core);

    p.route[0].layer = 0;
    core = audio_new();
    matrix_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    test_assert(audio_peak(audio_l, 2400, AUDIO_FRAMES / 4) < 1e-6f);
    box_audio_core_free(core);
}

/** Render a patch for half a second into a buffer.
 *
 * @param p The patch
 * @param out AUDIO_FRAMES / 2 samples
 */
static void matrix_render(
    const box_audio_synth_patch_t *p,
    float *out)
{
    box_audio_core_t *core = audio_new();
    matrix_define(core, 1, p);
    audio_play(core, 1, 1, 0, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    memcpy(out, audio_l, sizeof(float) * AUDIO_FRAMES / 2);
    box_audio_core_free(core);
}

/** A patch without routes renders bit for bit the same whatever its mod envelope and the room
 * of its routes hold, with a filter, a filter envelope, an LFO and the pressure of a note. A
 * sound with a SynthModEnvelope and no route compiles to the patch of the sound without it. */
void SynthModMatrix_no_routes_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p = matrix_patch(BOX_AUDIO_SYNTH_SAW, 110), q;
    ecs_world_t *world;
    ecs_entity_t a, b;
    AudioSynth pa, pb;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 400;
    p.resonance = 0.3f;
    p.filter_amount = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.05f, .decay = 0.2f, .sustain = 0.3f, .release = 0.05f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 20 };
    q = p;
    q.mod_envelope = (box_audio_synth_envelope_t){ .attack = 0.1f, .sustain = 1 };
    q.route[0] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_VELOCITY,
        .target = BOX_AUDIO_SYNTH_TO_PITCH, .amount = 12 };
    matrix_render(&p, matrix_a);
    matrix_render(&q, matrix_b);
    test_assert(memcmp(matrix_a, matrix_b, sizeof(float) * AUDIO_FRAMES / 2) == 0);

    world = audio_world();
    a = ecs_new(world);
    b = ecs_new(world);
    ecs_set(world, a, SynthSound, { .seed = 3 });
    ecs_set(world, a, SynthEnvelope, { .attack = 0.01f, .sustain = 1, .hold = 0.2f,
        .release = 0.05f });
    ecs_set(world, a, SynthOscillator, { .wave = SynthSaw, .frequency = 220, .level = 0.5f,
        .index = 1, .pulse_width = 0.5f, .sub_octave = 1 });
    ecs_set(world, a, SynthFilter, { .kind = SynthLowPass, .cutoff = 800 });
    ecs_set(world, a, SynthFilterEnvelope, { .attack = 0.01f, .decay = 0.1f, .amount = 2 });
    ecs_set(world, b, SynthSound, { .seed = 3 });
    ecs_set(world, b, SynthEnvelope, { .attack = 0.01f, .sustain = 1, .hold = 0.2f,
        .release = 0.05f });
    ecs_set(world, b, SynthOscillator, { .wave = SynthSaw, .frequency = 220, .level = 0.5f,
        .index = 1, .pulse_width = 0.5f, .sub_octave = 1 });
    ecs_set(world, b, SynthFilter, { .kind = SynthLowPass, .cutoff = 800 });
    ecs_set(world, b, SynthFilterEnvelope, { .attack = 0.01f, .decay = 0.1f, .amount = 2 });
    ecs_set(world, b, SynthModEnvelope, { .attack = 0.2f, .sustain = 1 });
    audio_run(world, 1);
    pa = *ecs_get(world, a, AudioSynth);
    pb = *ecs_get(world, b, AudioSynth);
    test_assert(pa.compiled && pb.compiled);
    test_int(pa.patch.routes, 0);
    test_assert(memcmp(&pa.patch, &pb.patch, sizeof(pa.patch)) == 0);
    ecs_fini(world);
}

/** A ninth SynthModRoute logs one error that names the sound and is left out of the patch of
 * 8 routes. */
void SynthModMatrix_ninth_route(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_entity(world, { .name = "nine_routes" });
    const AudioSynth *synth;
    int32_t i;
    ecs_set(world, e, SynthSound, {0});
    ecs_set(world, e, SynthEnvelope, { .attack = 0.01f, .sustain = 1, .hold = 0.2f,
        .release = 0.05f });
    ecs_set(world, e, SynthOscillator, { .wave = SynthSine, .frequency = 220, .level = 0.5f,
        .index = 1, .pulse_width = 0.5f, .sub_octave = 1 });
    for (i = 0; i < 8; i ++) {
        ecs_entity_t r = ecs_new_w_pair(world, EcsChildOf, e);
        ecs_set(world, r, SynthModRoute, { .source = SynthSourceVelocity,
            .target = SynthTargetPitch, .amount = 0.1f });
    }

    audio_log_start();
    audio_run(world, 1);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    synth = ecs_get(world, e, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.routes, 8);

    ecs_set(world, ecs_new_w_pair(world, EcsChildOf, e), SynthModRoute, {
        .source = SynthSourceKey, .target = SynthTargetCutoff, .amount = 1 });
    audio_log_start();
    audio_run(world, 1);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "nine_routes") != NULL);
    test_assert(strstr(audio_log_last, "more than 8") != NULL);
    synth = ecs_get(world, e, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.routes, 8);
    for (i = 0; i < 8; i ++) {
        test_int(synth->patch.route[i].source, BOX_AUDIO_SYNTH_FROM_VELOCITY);
    }

    ecs_fini(world);
}

/** Find a route of a patch by its source.
 *
 * @param p The patch
 * @param source A box_audio_synth_source_kind_t
 * @return The route
 */
static const box_audio_synth_route_t* matrix_find(
    const box_audio_synth_patch_t *p,
    uint32_t source)
{
    const box_audio_synth_route_t *result = NULL;
    uint32_t i;
    for (i = 0; i < p->routes; i ++) {
        if (p->route[i].source == source) {
            result = &p->route[i];
        }
    }

    test_assert(result != NULL);

    return result;
}

/** A script sets SynthModRoute children of the root and of a layer and a SynthModEnvelope:
 * they reach the patch, a route of the layer with its layer and the mod envelope with its
 * times; a change of a route compiles the sound again, and the sound plays in stereo. */
void SynthModMatrix_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_mod_matrix.flecs");
    const AudioSynth *synth;
    const box_audio_synth_route_t *r;
    ecs_entity_t lead, sweep;
    double l, rr;
    test_assert(ecs_script_run(world, "matrix", code, NULL) == 0);
    ecs_os_free(code);
    lead = ecs_lookup(world, "matrix_lead");
    sweep = ecs_lookup(world, "matrix_lead.sweep");
    test_assert(lead && sweep);
    test_int(ecs_get(world, sweep, SynthModRoute)->source, SynthSourceModEnvelope);
    test_int(ecs_get(world, sweep, SynthModRoute)->target, SynthTargetCutoff);
    audio_run(world, 1);
    synth = ecs_get(world, lead, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.layers, 2);
    test_int(synth->patch.routes, 3);
    r = matrix_find(&synth->patch, BOX_AUDIO_SYNTH_FROM_MOD_ENV);
    test_int(r->target, BOX_AUDIO_SYNTH_TO_CUTOFF);
    test_int(r->layer, 0);
    test_flt(r->amount, 3);
    r = matrix_find(&synth->patch, BOX_AUDIO_SYNTH_FROM_LFO1);
    test_int(r->target, BOX_AUDIO_SYNTH_TO_PAN);
    test_int(r->layer, 0);
    r = matrix_find(&synth->patch, BOX_AUDIO_SYNTH_FROM_VELOCITY);
    test_int(r->target, BOX_AUDIO_SYNTH_TO_LEVEL);
    test_int(r->layer, 2);
    test_flt(r->amount, -0.5f);
    test_flt(synth->patch.mod_envelope.attack, 0.1f);
    test_flt(synth->patch.mod_envelope.sustain, 0.3f);

    ecs_set(world, sweep, SynthModRoute, { .source = SynthSourceModEnvelope,
        .target = SynthTargetResonance, .amount = 0.5f });
    audio_run(world, 1);
    synth = ecs_get(world, lead, AudioSynth);
    test_assert(synth->compiled);
    r = matrix_find(&synth->patch, BOX_AUDIO_SYNTH_FROM_MOD_ENV);
    test_int(r->target, BOX_AUDIO_SYNTH_TO_RESONANCE);
    test_flt(r->amount, 0.5f);

    audio_source(world, lead, 0.5f, true, 0);
    audio_run(world, 30);
    test_assert(audio_tap_count > AUDIO_RATE / 4);
    l = 0;
    rr = 0;
    {
        int32_t i;
        for (i = audio_tap_count - AUDIO_RATE / 4; i < audio_tap_count; i ++) {
            l += (double)audio_tap_l[i] * audio_tap_l[i];
            rr += (double)audio_tap_r[i] * audio_tap_r[i];
        }
    }

    test_assert(l > 0 && rr > 0);
    test_assert(memcmp(audio_tap_l + audio_tap_count - 4800, audio_tap_r + audio_tap_count - 4800,
        sizeof(float) * 4800) != 0);
    ecs_fini(world);
}
