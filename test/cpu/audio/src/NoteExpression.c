/**
 * @file NoteExpression.c
 * @brief Note.bend, Note.mod and Note.pressure: the VOICE_RAMP command of the core measured on
 * offline renders, a bend of synth, sample and pluck voices, the vibrato that mod scales, the
 * cutoff that pressure opens, the defaults that render as before, a ramp at the frame of a
 * play, cancels and legato notes; then notes of clips through the null transport: a bend that
 * ends an octave up and follows its curve, a sampler that bends, notes without expression that
 * render the same, stops and seeks that cancel the ramps, the copies of a clip and a script.
 */

#include "world.h"
#include <math.h>
#include <string.h>

/** Frames of a beat at 120 bpm */
#define EXPR_BEAT 24000

/** Samples of the renders a test compares */
static float expr_a[AUDIO_FRAMES];

/** Samples of the tap of the world a test compares with */
static float expr_tap[AUDIO_RATE * 2];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void expr_define(
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

/** Ramp a parameter of a voice with a single command.
 *
 * @param core The core
 * @param voice Handle of the voice
 * @param param The parameter
 * @param value Value at the end
 * @param frames Frames of the ramp
 * @param when Frame it starts at, 0 for now
 */
static void expr_ramp(
    box_audio_core_t *core,
    uint32_t voice,
    box_audio_param_t param,
    float value,
    uint32_t frames,
    uint64_t when)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_voice_ramp(&w, voice, param, value, frames, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** A patch of one sine at a steady level for two seconds.
 *
 * @param frequency Hz
 * @return The patch
 */
static box_audio_synth_patch_t expr_patch(
    float frequency)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 2, .release = 0.01f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = frequency, .level = 0.5f };

    return p;
}

/** Check that a frequency is within a share of the one expected.
 *
 * @param measured The measured frequency
 * @param expected The expected frequency
 * @param share The share, 0.01 for 1%
 */
static void expr_near(
    double measured,
    double expected,
    double share)
{
    test_assert(fabs(measured - expected) <= expected * share);
}

/** Ratio of the highest to the lowest frequency of windows of a render.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param window Frames of a window
 * @return The ratio
 */
static double expr_spread(
    const float *x,
    int32_t from,
    int32_t to,
    int32_t window)
{
    double lo = INFINITY, hi = 0, f;
    int32_t at;
    for (at = from; at + window <= to; at += window) {
        f = audio_frequency(x, at, at + window);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }

    return hi / lo;
}

/** Energy of the first difference of a range, which a brighter sound has more of.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The energy
 */
static double expr_edge(
    const float *x,
    int32_t from,
    int32_t to)
{
    double result = 0, d;
    int32_t i;
    for (i = from + 1; i < to; i ++) {
        d = (double)x[i] - (double)x[i - 1];
        result += d * d;
    }

    return result;
}

/** A synth voice whose bend ramps from 0 to 12 semitones over half a second ends an octave
 * up and passes the frequencies between. */
void NoteExpression_bend_octave(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = expr_patch(220);
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 12, AUDIO_RATE / 2, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    expr_near(audio_frequency(audio_l, AUDIO_RATE / 4 - 600, AUDIO_RATE / 4 + 600),
        220 * pow(2, 0.5), 0.005);
    expr_near(audio_frequency(audio_l, AUDIO_RATE * 6 / 10, AUDIO_FRAMES), 440, 0.001);
    box_audio_core_free(core);
}

/** The pitch follows a linear ramp of the bend within a control block: every upward zero
 * crossing is within BOX_AUDIO_EXPRESS_FRAMES of the phase of the exact exponential sweep. */
void NoteExpression_bend_shape(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = expr_patch(220);
    const double n = AUDIO_RATE / 2, f0 = 220.0 / AUDIO_RATE;
    double first = -1, at, phase, phase0 = 0, inc, error, worst = 0;
    int32_t i, k = 0;
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 12, (uint32_t)n, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 1; i < AUDIO_FRAMES * 3 / 4; i ++) {
        if (!(audio_l[i - 1] < 0 && audio_l[i] >= 0)) {
            continue;
        }

        at = i - 1 + audio_l[i - 1] / (double)(audio_l[i - 1] - audio_l[i]);
        phase = at < n ? f0 * n / log(2.0) * (pow(2, at / n) - 1) :
            f0 * n / log(2.0) + 2 * f0 * (at - n);
        inc = at < n ? f0 * pow(2, at / n) : 2 * f0;
        if (first < 0) {
            first = at;
            phase0 = phase;
        } else {
            k ++;
            error = fabs(phase - phase0 - (double)k) / inc;
            worst = error > worst ? error : worst;
        }
    }

    test_assert(k > 200);
    test_assert(worst < BOX_AUDIO_EXPRESS_FRAMES);
    box_audio_core_free(core);
}

/** A ramp at the frame of a play applies after it, to the voice it starts, even when it
 * arrives first. */
void NoteExpression_ramp_after_play(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = expr_patch(220);
    uint8_t bytes[256];
    box_audio_writer_t w;
    expr_define(core, 1, &p);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_voice_ramp(&w, 1, BOX_AUDIO_PARAM_BEND, 12, 0, 1000));
    test_assert(box_audio_write_play(&w, 1, 1, 0, 1, 0, 1, 0, 1000));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    expr_near(audio_frequency(audio_l, 2400, AUDIO_FRAMES / 2), 440, 0.001);
    box_audio_core_free(core);
}

/** A sample voice bends: an octave down at once, and a later pitch keeps the bend. */
void NoteExpression_bend_sample(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    audio_sine(audio_sound_a, AUDIO_FRAMES, 440, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, -12, 0, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    expr_near(audio_frequency(audio_l, 2400, AUDIO_FRAMES / 2), 220, 0.002);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 2);
    audio_render(core, AUDIO_FRAMES / 2, AUDIO_FRAMES / 2);
    expr_near(audio_frequency(audio_l, AUDIO_FRAMES / 2 + 2400, AUDIO_FRAMES), 440, 0.002);
    box_audio_core_free(core);
}

/** A pluck string bends: its delay line follows the pitch of the voice up an octave. */
void NoteExpression_bend_pluck(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 3;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 220, .level = 0.5f,
        .decay = 4, .brightness = 0.3f, .hardness = 0.5f, .rings_free = 1 };
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 12, 0, AUDIO_RATE / 5);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    expr_near(audio_frequency(audio_l, 2400, AUDIO_RATE / 5), 220, 0.01);
    expr_near(audio_frequency(audio_l, AUDIO_RATE / 4, AUDIO_FRAMES / 2), 440, 0.01);
    box_audio_core_free(core);
}

/** Mod scales the vibrato of the LFOs: 0 removes it, 1 keeps it bit for bit. */
void NoteExpression_mod_vibrato(void) {
    // audio/expression
    box_audio_synth_patch_t p = expr_patch(220);
    box_audio_core_t *core;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 100 };

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    memcpy(expr_a, audio_l, sizeof(expr_a));
    test_assert(expr_spread(expr_a, 4800, AUDIO_FRAMES, 1200) > 1.03);

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_MOD, 1, 0, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(memcmp(expr_a, audio_l, sizeof(expr_a)) == 0);

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_MOD, 0, 0, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(expr_spread(audio_l, 4800, AUDIO_FRAMES, 1200) < 1.002);
    expr_near(audio_frequency(audio_l, 4800, AUDIO_FRAMES), 220, 0.001);
}

/** Pressure 1 opens the cutoff of a filtered note by BOX_AUDIO_SYNTH_PRESSURE_OCTAVES, which
 * brightens it. */
void NoteExpression_pressure_brightens(void) {
    // audio/expression
    box_audio_synth_patch_t p = {0};
    box_audio_core_t *core;
    double cutoff, plain, pressed;
    p.layers = 1;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 2, .release = 0.01f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110, .level = 0.5f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 300;
    p.resonance = 0.1f;

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(audio_synth_cutoff(core, 1, &cutoff, NULL));
    expr_near(cutoff, 300, 0.001);
    plain = expr_edge(audio_l, 4800, AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_PRESSURE, 1, 0, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(audio_synth_cutoff(core, 1, &cutoff, NULL));
    expr_near(cutoff, 300 * pow(2, BOX_AUDIO_SYNTH_PRESSURE_OCTAVES), 0.001);
    pressed = expr_edge(audio_l, 4800, AUDIO_FRAMES / 2);
    box_audio_core_free(core);
    test_assert(pressed > plain * 2);
}

/** The defaults of the expression and ramps to them render bit for bit as a voice without
 * them, with LFOs, a filter that tracks the key and a filter envelope. */
void NoteExpression_defaults_identical(void) {
    // audio/expression
    box_audio_synth_patch_t p = expr_patch(220);
    box_audio_core_t *core;
    p.layer[0].wave = BOX_AUDIO_SYNTH_SAW;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 800;
    p.keytrack = 1;
    p.filter_amount = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.1f, .decay = 0.2f,
        .sustain = 0.5f, .hold = 1, .release = 0.1f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 4,
        .pitch = 30, .cutoff = 0.5f };

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    memcpy(expr_a, audio_l, sizeof(expr_a));

    core = audio_new();
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 0, 0, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_MOD, 1, 0, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_PRESSURE, 0, 0, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 0, 4800, 1000);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_MOD, 1, 4800, 2000);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(memcmp(expr_a, audio_l, sizeof(expr_a)) == 0);
}

/** A cancel of the ramps of a voice drops its scheduled ramps, so the voice keeps its pitch. */
void NoteExpression_cancel(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = expr_patch(220);
    uint8_t bytes[64];
    box_audio_writer_t w;
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 12, 100, AUDIO_RATE / 2);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_PRESSURE, 1, 100, AUDIO_RATE / 2);
    test_int(box_audio_core_scheduled(core), 2);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_cancel(&w, BOX_AUDIO_CMD_VOICE_RAMP, 2, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 2);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_cancel(&w, BOX_AUDIO_CMD_VOICE_RAMP, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 0);
    audio_render(core, 0, AUDIO_FRAMES);
    expr_near(audio_frequency(audio_l, AUDIO_RATE * 6 / 10, AUDIO_FRAMES), 220, 0.001);
    box_audio_core_free(core);
}

/** A legato note gives the shared voice its own expression: the bend of the note it follows
 * resets, and the ramps of the new handle move the voice. */
void NoteExpression_legato(void) {
    // audio/expression
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = expr_patch(220);
    uint8_t bytes[128];
    box_audio_writer_t w;
    expr_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, 12, 0, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    expr_near(audio_frequency(audio_l, 2400, AUDIO_FRAMES / 4), 440, 0.002);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato_note(&w, 2, 1, 1, 0, 1, 0, 1, 1, 0, 0,
        BOX_AUDIO_PLAY_GATE, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, AUDIO_FRAMES / 4, AUDIO_FRAMES / 4);
    expr_near(audio_frequency(audio_l, AUDIO_FRAMES / 4 + 2400, AUDIO_FRAMES / 2), 220, 0.002);
    expr_ramp(core, 2, BOX_AUDIO_PARAM_BEND, 7, 0, 0);
    expr_ramp(core, 1, BOX_AUDIO_PARAM_BEND, -12, 0, 0);
    audio_render(core, AUDIO_FRAMES / 2, AUDIO_FRAMES / 2);
    expr_near(audio_frequency(audio_l, AUDIO_FRAMES / 2 + 2400, AUDIO_FRAMES),
        220 * pow(2, 7.0 / 12), 0.002);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}

/** Load the sounds of the suite into a world.
 *
 * @param world The world
 * @return The instrument of the sine at A3
 */
static ecs_entity_t expr_load(
    ecs_world_t *world)
{
    char *code = audio_read("test/assets/audio/note_expression.flecs");
    ecs_entity_t result;
    test_assert(code != NULL);
    test_assert(ecs_script_run(world, "note_expression", code, NULL) == 0);
    ecs_os_free(code);
    result = ecs_lookup(world, "expr_sine_inst");
    test_assert(result != 0);

    return result;
}

/** Place a clip on a new track of an instrument at beat 0.
 *
 * @param world The world
 * @param instrument The instrument
 * @param clip The clip
 * @return The placement
 */
static ecs_entity_t expr_place(
    ecs_world_t *world,
    ecs_entity_t instrument,
    ecs_entity_t clip)
{
    ecs_entity_t track = ecs_new(world);
    ecs_entity_t e;
    ecs_set(world, track, Track, { .instrument = instrument });
    e = ecs_new_w_pair(world, EcsChildOf, track);
    ecs_set(world, e, ClipPlacement, { .clip = clip });

    return e;
}

/** Start or stop the transport.
 *
 * @param world The world
 * @param playing Whether it plays
 * @return The frame of the tap at which it starts
 */
static int64_t expr_play(
    ecs_world_t *world,
    bool playing)
{
    ecs_singleton_ensure(world, Transport)->playing = playing;
    ecs_singleton_modified(world, Transport);

    return audio_tap_count;
}

/** Run frames until the tap holds a frame.
 *
 * @param world The world
 * @param frame The frame
 */
static void expr_run_to(
    ecs_world_t *world,
    int64_t frame)
{
    while (audio_tap_count <= frame + AUDIO_FRAME) {
        audio_run(world, 1);
    }
}

/** First frame of the left channel of the tap from a frame on that is not 0.
 *
 * @param from First frame
 * @return The frame, -1 for none
 */
static int64_t expr_onset(
    int64_t from)
{
    int64_t i;
    for (i = from; i < audio_tap_count; i ++) {
        if (audio_tap_l[i] != 0.0f) {
            return i;
        }
    }

    return -1;
}

/** A note whose bend goes from 0 to 12 over its length ends an octave up, and its frequency
 * follows the curve on the way. */
void NoteExpression_note_bend(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = expr_load(world), clip = ecs_new(world);
    const Note *note;
    double expected;
    int64_t c0, start;
    int32_t k;
    ecs_set(world, clip, NoteClip, { .length = 4, .notes = BOX_VEC_VIEW(Note,
        { .start = 0, .duration = 2, .pitch = 57, .velocity = 1,
          .bend = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {2, 12}) } }) });
    expr_place(world, inst, clip);
    audio_run(world, 2);
    c0 = expr_play(world, true);
    expr_run_to(world, c0 + 3 * EXPR_BEAT);
    start = expr_onset(c0);
    test_assert(start >= c0 && start < c0 + 4 * AUDIO_FRAME);
    note = ecs_vec_first(&ecs_get(world, clip, NoteClip)->notes);
    for (k = 1; k < 19; k ++) {
        int64_t at = start + (int64_t)k * 2 * EXPR_BEAT / 20;
        expected = 220 * pow(2, box_curve_value(&note->bend,
            (float)((double)(at - start) / EXPR_BEAT), 0) / 12);
        expr_near(audio_frequency(audio_tap_l, (int32_t)(at - 600), (int32_t)(at + 600)),
            expected, 0.005);
    }

    expected = audio_frequency(audio_tap_l, (int32_t)(start + 2 * EXPR_BEAT - 1200),
        (int32_t)(start + 2 * EXPR_BEAT));
    test_assert(expected > 432 && expected < 441);
    test_assert(box_audio_module_get(world)->sequencer.stats.ramps >= 2);
    ecs_fini(world);
}

/** A curved bend follows its curve: Catmull-Rom spans become ramps of at most 10 ms, and the
 * frequency of each window is that of the curve at its middle. */
void NoteExpression_note_curve(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = expr_load(world), clip = ecs_new(world);
    box_audio_module_t *state = box_audio_module_get(world);
    const Note *note;
    double expected;
    int64_t c0, start;
    int32_t k;
    ecs_set(world, clip, NoteClip, { .length = 4, .notes = BOX_VEC_VIEW(Note,
        { .start = 0, .duration = 2, .pitch = 57, .velocity = 1,
          .bend = { CurveCatmullRom, BOX_CURVE_KEYS({0, 0}, {0.5f, 7}, {1, -5}, {2, 12}) }
        }) });
    expr_place(world, inst, clip);
    audio_run(world, 2);
    c0 = expr_play(world, true);
    expr_run_to(world, c0 + 3 * EXPR_BEAT);
    start = expr_onset(c0);
    test_assert(start >= c0 && start < c0 + 4 * AUDIO_FRAME);
    test_assert(state->sequencer.stats.ramps >= 1 + 100);
    note = ecs_vec_first(&ecs_get(world, clip, NoteClip)->notes);
    for (k = 1; k < 39; k ++) {
        int64_t at = start + (int64_t)k * 2 * EXPR_BEAT / 40;
        expected = 220 * pow(2, box_curve_value(&note->bend,
            (float)((double)(at - start) / EXPR_BEAT), 0) / 12);
        expr_near(audio_frequency(audio_tap_l, (int32_t)(at - 480), (int32_t)(at + 480)),
            expected, 0.01);
    }

    ecs_fini(world);
}

/** A note of a sampler bends: an octave down for a beat, then up to its pitch. */
void NoteExpression_note_sample(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = audio_tone(world, NULL, 440, 0.5f, AUDIO_RATE * 6);
    ecs_entity_t inst = ecs_new(world), clip = ecs_new(world);
    double f;
    int64_t c0, start;
    ecs_set(world, inst, Instrument, { .sound = sound, .root = 69, .velocity_curve = 1,
        .gate = true, .release = 0.005f });
    ecs_set(world, clip, NoteClip, { .length = 4, .notes = BOX_VEC_VIEW(Note,
        { .start = 0, .duration = 2, .pitch = 69, .velocity = 1,
          .bend = { CurveLinear, BOX_CURVE_KEYS({0, -12}, {1, -12}, {2, 0}) } }) });
    expr_place(world, inst, clip);
    audio_run(world, 2);
    c0 = expr_play(world, true);
    expr_run_to(world, c0 + 3 * EXPR_BEAT);
    start = expr_onset(c0);
    test_assert(start >= c0 && start < c0 + 4 * AUDIO_FRAME);
    expr_near(audio_frequency(audio_tap_l, (int32_t)(start + 2400),
        (int32_t)(start + EXPR_BEAT - 1200)), 220, 0.005);
    expr_near(audio_frequency(audio_tap_l, (int32_t)(start + EXPR_BEAT * 3 / 2 - 600),
        (int32_t)(start + EXPR_BEAT * 3 / 2 + 600)), 440 * pow(2, -0.5), 0.005);
    f = audio_frequency(audio_tap_l, (int32_t)(start + 2 * EXPR_BEAT - 1200),
        (int32_t)(start + 2 * EXPR_BEAT));
    test_assert(f > 432 && f < 441);
    ecs_fini(world);
}

/** Render a clip of a note of a beat at beat 0, and maybe a note with expression at beat 2,
 * into the tap up to beat 1.5, and get the frame at which the transport starts.
 *
 * @param expressive Whether the clip has the second note
 * @return The frame
 */
static int64_t expr_render_plain(
    bool expressive)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = expr_load(world), clip = ecs_new(world);
    int64_t c0;
    if (expressive) {
        ecs_set(world, clip, NoteClip, { .length = 4, .notes = BOX_VEC_VIEW(Note,
            { .start = 0, .duration = 1, .pitch = 57, .velocity = 1 },
            { .start = 2, .duration = 1, .pitch = 57, .velocity = 1,
              .bend = { CurveCatmullRom, BOX_CURVE_KEYS({0, 0}, {0.5f, 3}, {1, 0}) },
              .mod = { CurveLinear, BOX_CURVE_KEYS({0, 0}) },
              .pressure = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 1}) } }) });
    } else {
        ecs_set(world, clip, NoteClip, { .length = 4, .notes = BOX_VEC_VIEW(Note,
            { .start = 0, .duration = 1, .pitch = 57, .velocity = 1 }) });
    }

    expr_place(world, inst, clip);
    audio_run(world, 2);
    c0 = expr_play(world, true);
    expr_run_to(world, c0 + EXPR_BEAT * 3 / 2);
    ecs_fini(world);

    return c0;
}

/** Notes without expression render bit for bit the same whether other notes of the clip have
 * expression or not. */
void NoteExpression_no_expression_identical(void) {
    // audio/expression
    int64_t a = expr_render_plain(false), b, frames = a + EXPR_BEAT * 3 / 2;
    test_assert(frames <= AUDIO_RATE * 2);
    test_assert(expr_onset(a) >= 0);
    memcpy(expr_tap, audio_tap_l, sizeof(float) * (size_t)frames);
    b = expr_render_plain(true);
    test_int(a, b);
    test_assert(memcmp(expr_tap, audio_tap_l, sizeof(float) * (size_t)frames) == 0);
}

/** Create a clip of a long note whose bend curves, so ramps wait for every window.
 *
 * @param world The world
 * @return The clip
 */
static ecs_entity_t expr_long_clip(
    ecs_world_t *world)
{
    ecs_entity_t clip = ecs_new(world);
    ecs_set(world, clip, NoteClip, { .length = 8, .notes = BOX_VEC_VIEW(Note,
        { .start = 0, .duration = 8, .pitch = 57, .velocity = 1,
          .bend = { CurveCatmullRom, BOX_CURVE_KEYS({0, 0}, {4, 5}, {8, 0}) },
          .pressure = { CurveBezier, BOX_CURVE_KEYS({0, 0}, {8, 1}) } }) });

    return clip;
}

/** A stop drops the ramps that were not sent and cancels the ones the core holds ahead; no
 * more ramps go out. */
void NoteExpression_stop_cancels(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = expr_load(world);
    int64_t ramps;
    expr_place(world, inst, expr_long_clip(world));
    audio_run(world, 2);
    expr_play(world, true);
    audio_run(world, 3);
    test_int(state->sequencer.stats.notes, 1);
    test_assert(ecs_vec_count(&state->sequencer.ramps) > 0);
    test_assert(state->sequencer.stats.ramps > 2);
    test_int(state->sequencer.stats.ramp_cancels, 0);
    expr_play(world, false);
    audio_run(world, 1);
    test_int(ecs_vec_count(&state->sequencer.ramps), 0);
    test_int(ecs_vec_count(&state->sequencer.expressed), 0);
    test_assert(state->sequencer.stats.ramps_dropped > 0);
    test_assert(state->sequencer.stats.ramp_cancels >= 1);
    ramps = state->sequencer.stats.ramps;
    audio_run(world, 20);
    test_int(state->sequencer.stats.ramps, ramps);
    ecs_fini(world);
}

/** A seek drops and cancels the ramps of the note it releases; the note does not start again
 * in its middle, so no more ramps go out. */
void NoteExpression_seek_cancels(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = expr_load(world);
    int64_t ramps;
    expr_place(world, inst, expr_long_clip(world));
    audio_run(world, 2);
    expr_play(world, true);
    audio_run(world, 3);
    test_assert(ecs_vec_count(&state->sequencer.ramps) > 0);
    ecs_singleton_ensure(world, Transport)->position = 4;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 1);
    test_int(ecs_vec_count(&state->sequencer.ramps), 0);
    test_assert(state->sequencer.stats.ramps_dropped > 0);
    test_assert(state->sequencer.stats.ramp_cancels >= 1);
    ramps = state->sequencer.stats.ramps;
    audio_run(world, 20);
    test_int(state->sequencer.stats.ramps, ramps);
    test_int(state->sequencer.stats.notes, 1);
    ecs_fini(world);
}

/** A deleted placement drops and cancels the ramps of its notes. */
void NoteExpression_delete_placement(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = expr_load(world);
    ecs_entity_t placement = expr_place(world, inst, expr_long_clip(world));
    int64_t ramps;
    audio_run(world, 2);
    expr_play(world, true);
    audio_run(world, 3);
    test_assert(ecs_vec_count(&state->sequencer.ramps) > 0);
    ecs_delete(world, placement);
    audio_run(world, 1);
    test_int(ecs_vec_count(&state->sequencer.ramps), 0);
    test_assert(state->sequencer.stats.ramp_cancels >= 1);
    ramps = state->sequencer.stats.ramps;
    audio_run(world, 20);
    test_int(state->sequencer.stats.ramps, ramps);
    ecs_fini(world);
}

/** Copies of a NoteClip own the keys of the curves of their notes: a clone, an edit of the
 * keys and a delete while the clip plays, and the sorted copy of the sequencer rebuilt. */
void NoteExpression_clip_copies(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = expr_load(world), clip = expr_long_clip(world), copy, other;
    const NoteClip *a, *b;
    const Note *na, *nb;
    NoteClip *edit;
    Note *n;
    CurveKey *key;
    copy = ecs_clone(world, 0, clip, true);
    a = ecs_get(world, clip, NoteClip);
    b = ecs_get(world, copy, NoteClip);
    test_int(ecs_vec_count(&b->notes), 1);
    na = ecs_vec_first(&a->notes);
    nb = ecs_vec_first(&b->notes);
    test_assert(na != nb);
    test_int(ecs_vec_count(&nb->bend.keys), 3);
    test_assert(ecs_vec_first(&na->bend.keys) != ecs_vec_first(&nb->bend.keys));
    test_flt(ecs_vec_get_t(&nb->bend.keys, CurveKey, 1)->value, 5);
    test_int(ecs_vec_count(&nb->mod.keys), 0);

    expr_place(world, inst, clip);
    expr_place(world, inst, copy);
    audio_run(world, 2);
    expr_play(world, true);
    audio_run(world, 3);
    test_int(state->sequencer.stats.notes, 2);

    edit = ecs_ensure(world, copy, NoteClip);
    n = ecs_vec_first(&edit->notes);
    key = ecs_vec_append_t(NULL, &n->bend.keys, CurveKey);
    *key = (CurveKey){ .time = 9, .value = 2 };
    n = ecs_vec_append_t(NULL, &edit->notes, Note);
    *n = (Note){ .start = 1, .duration = 1, .pitch = 60, .velocity = 1 };
    n->mod.keys = ecs_vec_copy_t(NULL, &BOX_CURVE_KEYS({0, 0}, {1, 1}), CurveKey);
    ecs_modified(world, copy, NoteClip);
    audio_run(world, 3);
    a = ecs_get(world, clip, NoteClip);
    test_int(ecs_vec_count(&ecs_vec_first_t(&a->notes, Note)->bend.keys), 3);

    other = ecs_clone(world, 0, copy, true);
    ecs_set(world, copy, NoteClip, { .length = 2 });
    test_int(ecs_vec_count(&ecs_get(world, copy, NoteClip)->notes), 0);
    test_int(ecs_vec_count(&ecs_get(world, other, NoteClip)->notes), 2);
    audio_run(world, 3);
    ecs_delete(world, clip);
    audio_run(world, 3);
    ecs_delete(world, other);
    audio_run(world, 3);
    expr_play(world, false);
    audio_run(world, 3);
    ecs_fini(world);
}

/** A script authors the curves of notes: their keys and kinds parse, the curves without keys
 * stay empty, and the clip plays its ramps. */
void NoteExpression_script(void) {
    // audio/expression
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = expr_load(world), clip = ecs_lookup(world, "expr_clip");
    const NoteClip *nc;
    const Note *notes;
    test_assert(clip != 0);
    nc = ecs_get(world, clip, NoteClip);
    test_assert(nc != NULL);
    test_int(ecs_vec_count(&nc->notes), 3);
    notes = ecs_vec_first(&nc->notes);
    test_int(ecs_vec_count(&notes[0].bend.keys), 2);
    test_flt(ecs_vec_get_t(&notes[0].bend.keys, CurveKey, 1)->time, 2);
    test_flt(ecs_vec_get_t(&notes[0].bend.keys, CurveKey, 1)->value, 12);
    test_int(ecs_vec_count(&notes[0].mod.keys), 0);
    test_int(ecs_vec_count(&notes[0].pressure.keys), 0);
    test_int(notes[1].mod.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&notes[1].mod.keys), 3);
    test_int(ecs_vec_count(&notes[1].pressure.keys), 1);
    test_flt(ecs_vec_first_t(&notes[1].pressure.keys, CurveKey)->value, 0.5f);
    test_flt(notes[2].velocity, 1);
    test_int(ecs_vec_count(&notes[2].bend.keys), 0);

    expr_place(world, inst, clip);
    audio_run(world, 2);
    expr_play(world, true);
    audio_run(world, 120);
    test_int(state->sequencer.stats.notes, 3);
    test_assert(state->sequencer.stats.ramps > 3);
    ecs_fini(world);
}
