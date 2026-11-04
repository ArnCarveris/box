/**
 * @file Instrument.c
 * @brief Instruments: the pitch and the gain of a note, the gate and the release of the core,
 * on offline renders of a core and on a world with the null output, scripts and legato notes.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of a millisecond at AUDIO_RATE */
#define INSTRUMENT_MS 48

/** Frames of a beat at the default tempo of 120 */
#define INSTRUMENT_BEAT (AUDIO_RATE / 2)

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void instrument_define(
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

/** Send a release command.
 *
 * @param core The core
 * @param voice Handle of the voice
 * @param fade Seconds of the fade of a sample voice
 * @param when Frame of the release, 0 for now
 */
static void instrument_release(
    box_audio_core_t *core,
    uint32_t voice,
    float fade,
    uint64_t when)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_voice_release(&w, voice, fade, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** A patch of a square at 1 Hz: its first half period is a level of 1, so the output is the
 * level times its envelope for half a second.
 *
 * @param curve Curve of the decay and the release
 * @return The patch
 */
static box_audio_synth_patch_t instrument_probe(
    float curve)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.01f, .decay = 0.01f,
        .sustain = 0.5f, .hold = 0.05f, .release = 0.1f, .curve = curve };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 1, .level = 0.5f };

    return p;
}

/** Last frame of a range that is not silent.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 when all are silent
 */
static int32_t instrument_last(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t result = -1, i;
    for (i = to - 1; i >= from && result < 0; i --) {
        if (x[i] != 0) {
            result = i;
        }
    }

    return result;
}

/** First frame of a range that is not silent.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 when all are silent
 */
static int32_t instrument_first(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t result = -1, i;
    for (i = from; i < to && result < 0; i ++) {
        if (x[i] != 0) {
            result = i;
        }
    }

    return result;
}

/** Amplitude of a sine in a range, from its mean square.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The amplitude
 */
static double instrument_amplitude(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * x[i];
    }

    return sqrt(2 * sum / (to - from));
}

/** Create a synth sound of one sine.
 *
 * @param world The world
 * @param frequency Hz
 * @param envelope Its envelope
 * @return The sound
 */
static ecs_entity_t instrument_synth(
    ecs_world_t *world,
    float frequency,
    SynthEnvelope envelope)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, SynthSound, {0});
    ecs_set_ptr(world, e, SynthEnvelope, &envelope);
    ecs_set(world, e, SynthOscillator, {
        .wave = SynthSine, .frequency = frequency, .level = 0.5f, .index = 1 });

    return e;
}

/** Create an instrument with the default velocity curve.
 *
 * @param world The world
 * @param sound The sound
 * @param root MIDI note of the sound as authored
 * @param gate Whether the duration ends a note
 * @param release Seconds of the fade of a sampler
 * @return The instrument
 */
static ecs_entity_t instrument_new(
    ecs_world_t *world,
    ecs_entity_t sound,
    float root,
    bool gate,
    float release)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Instrument, { .sound = sound, .root = root, .velocity_curve = 2,
        .gate = gate, .release = release });

    return e;
}

/** A synth that sustains while a note holds: attack, decay to 1 and a short hold */
static const SynthEnvelope instrument_sustain = {
    .attack = 0.005f, .decay = 0.01f, .sustain = 1, .hold = 0.05f, .release = 0.05f };

/** The defaults of Instrument: a root of A4, a curve of 2, a gate and a release of 50 ms. */
void Instrument_defaults(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const Instrument *inst;
    ecs_add(world, e, Instrument);
    inst = ecs_get(world, e, Instrument);
    test_assert(inst != NULL);
    test_uint(inst->sound, 0);
    test_flt(inst->root, 69);
    test_flt(inst->velocity_curve, 2);
    test_bool(inst->gate, true);
    test_flt(inst->release, 0.05f);
    test_bool(box_audio_play_note(world, e, 60, 1, 1), false);
    ecs_fini(world);
}

/** Pitch 81 on a 440 Hz sine synth with root 69 plays 880 Hz, and a microtone of 69.5 plays a
 * quarter tone over 440 Hz. */
void Instrument_synth_pitch(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = instrument_synth(world, 440, instrument_sustain);
    ecs_entity_t inst = instrument_new(world, sound, 69, true, 0.05f);
    double f, quarter = 440 * pow(2.0, 0.5 / 12);
    audio_run(world, 1);
    test_assert(box_audio_play_note(world, inst, 81, 1, 2));
    audio_run(world, 30);
    f = audio_frequency(audio_tap_l, audio_tap_count - 9600, audio_tap_count);
    printf("pitch 81: %f Hz\n", f);
    test_assert(fabs(f - 880) < 0.5);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    audio_run(world, 40);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    test_assert(box_audio_play_note(world, inst, 69.5f, 1, 2));
    audio_run(world, 30);
    f = audio_frequency(audio_tap_l, audio_tap_count - 9600, audio_tap_count);
    printf("pitch 69.5: %f Hz, expected %f Hz\n", f, quarter);
    test_assert(fabs(f - quarter) < 0.5);
    test_int(box_audio_module_stats(world).notes, 2);
    ecs_fini(world);
}

/** A sampler with root 60 plays pitch 72 at twice the rate: a 480 Hz sine sounds at 960 Hz. */
void Instrument_sampler_rate(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = audio_tone(world, NULL, 480, 0.5f, AUDIO_RATE * 2);
    ecs_entity_t inst = instrument_new(world, sound, 60, true, 0.05f);
    double f;
    audio_run(world, 1);
    test_assert(box_audio_play_note(world, inst, 72, 1, 1));
    audio_run(world, 20);
    f = audio_frequency(audio_tap_l, audio_tap_count - 9600, audio_tap_count);
    printf("sampler at pitch 72: %f Hz\n", f);
    test_assert(fabs(f - 960) < 0.5);
    ecs_fini(world);
}

/** Velocity 0.5 gives a gain of 0.25 at the default curve; a sampler starts at that gain on
 * its first sample, without the ramp. */
void Instrument_velocity_gain(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = audio_dc(world, NULL, 1.0f, AUDIO_RATE * 2);
    ecs_entity_t inst = instrument_new(world, sound, 60, true, 0.05f);
    int32_t start, at;
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, inst, 60, 0.5f, 2));
    audio_run(world, 10);
    start = instrument_first(audio_tap_l, at, audio_tap_count);
    test_assert(start >= 0);
    printf("first sample %f at %d\n", (double)audio_tap_l[start], start);
    test_flt(audio_tap_l[start], 0.25f);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_set(world, inst, Instrument, { .sound = sound, .root = 60, .velocity_curve = 1,
        .gate = true, .release = 0.05f });
    audio_run(world, 60);
    test_flt(audio_level(AUDIO_FRAME), 0);
    test_assert(box_audio_play_note(world, inst, 60, 0.5f, 2));
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

/** Check a gated note of the probe patch that releases at a frame: past its hold, which ends at
 * 0.07 s, it sustains at 0.5 times its level of 0.5 until then, and releases over 0.1 s from
 * there, without a jump, as the curve says.
 *
 * @param curve Curve of the patch
 */
static void instrument_check_gate(
    float curve)
{
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = instrument_probe(curve);
    int32_t release = 14400, n = 4800, k, last;
    double c = curve, r = exp(-c / n), end = exp(-c), a = (0 - 0.25 * end) / (1 - end);
    instrument_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    instrument_release(core, 1, 0, (uint64_t)release);
    audio_render(core, 0, AUDIO_FRAMES / 2);

    test_assert(fabsf(fabsf(audio_l[4800]) - 0.25f) < 1e-4f);
    test_assert(fabsf(fabsf(audio_l[release - 1]) - 0.25f) < 1e-4f);
    test_assert(fabsf(audio_l[release]) < fabsf(audio_l[release - 1]));
    test_assert(audio_max_step(audio_l, 100, AUDIO_FRAMES / 2) < 0.005f);
    for (k = 0; k < n; k += 480) {
        double expected = c != 0 ? a + (0.25 - a) * pow(r, k + 1) :
            0.25 * (1 - (double)(k + 1) / n);
        if (fabs(fabsf(audio_l[release + k]) - expected) > 2e-3) {
            printf("curve %f, frame %d of the release: %f, expected %f\n", c, k,
                (double)audio_l[release + k], expected);
        }

        test_assert(fabs(fabsf(audio_l[release + k]) - expected) < 2e-3);
    }

    last = instrument_last(audio_l, 0, AUDIO_FRAMES / 2);
    printf("curve %f: last sample at %d, the release ends at %d\n", c, last, release + n);
    test_assert(abs(last - (release + n - 1)) <= 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    box_audio_core_free(core);
}

/** A gated synth sustains past its hold until the release, then releases linearly over its
 * release time from its current level. */
void Instrument_gate_sustains_linear(void) {
    // audio/instrument
    instrument_check_gate(0);
}

/** A gated synth with a curve releases along the curve of its envelope from its level. */
void Instrument_gate_sustains_curved(void) {
    // audio/instrument
    instrument_check_gate(4);
}

/** A release during the attack moves the envelope into its release from where the attack
 * stands, without a jump. */
void Instrument_release_in_attack(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = instrument_probe(0);
    int32_t last;
    p.envelope.attack = 0.1f;
    instrument_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    instrument_release(core, 1, 0, 2400);
    audio_render(core, 0, 12000);
    test_assert(fabsf(fabsf(audio_l[2399]) - 0.5f * 2400 / 4800) < 1e-3f);
    test_assert(audio_max_step(audio_l, 100, 12000) < 0.002f);
    last = instrument_last(audio_l, 0, 12000);
    test_assert(abs(last - (2400 + 4800 - 1)) <= 1);
    box_audio_core_free(core);
}

/** A gated synth holds its filter envelope at its sustain past its hold, so the cutoff stays
 * open; the release closes it over its own release while the amp releases slower. */
void Instrument_gate_filter_envelope(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = {0};
    double early, held, closed;
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.005f, .sustain = 1, .hold = 0.05f, .release = 0.5f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 200;
    p.filter_amount = 5;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.005f, .sustain = 1, .hold = 0.05f, .release = 0.1f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 2000, .level = 0.5f };
    instrument_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    instrument_release(core, 1, 0, 14400);
    audio_render(core, 0, AUDIO_FRAMES);
    early = instrument_amplitude(audio_l, 960, 2400);
    held = instrument_amplitude(audio_l, 12000, 14400);
    closed = instrument_amplitude(audio_l, 14400 + 5760, 14400 + 7200);
    printf("filter: %f early, %f held, %f after its release\n", early, held, closed);
    test_assert(early > 0.3);
    test_assert(fabs(held - early) < 0.02 * early);
    test_assert(closed < 0.2 * held);
    test_assert(audio_max_step(audio_l, 100, AUDIO_FRAMES) < 0.4f);
    test_assert(instrument_last(audio_l, 0, AUDIO_FRAMES) <= 14400 + 24000);
    box_audio_core_free(core);
}

/** An envelope with a sustain of 0 still ends after its decay with the gate. */
void Instrument_gate_sustain_zero(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = instrument_probe(0);
    int32_t last;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.01f, .decay = 0.1f };
    instrument_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    audio_render(core, 0, 12000);
    last = instrument_last(audio_l, 0, 12000);
    printf("sustain 0: last sample at %d\n", last);
    test_assert(abs(last - 5280) <= INSTRUMENT_MS);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    box_audio_core_free(core);
}

/** A scheduled release lands on its exact frame: a sample voice holds its level up to the
 * frame and fades out linearly over the fade from it, whatever the render sizes; a release of
 * a voice that ended is ignored. */
void Instrument_release_exact_frame(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    int32_t fade = 480, at = 1000, k;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_ATTACK);
    instrument_release(core, 1, 0.01f, (uint64_t)at);
    audio_render(core, 0, 4000);
    test_flt(audio_l[0], 0.5f);
    test_flt(audio_l[at - 1], 0.5f);
    for (k = 0; k < fade; k ++) {
        test_assert(fabsf(audio_l[at + k] - 0.5f * (1 - (float)(k + 1) / fade)) < 1e-4f);
    }

    test_flt(audio_l[at + fade], 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    instrument_release(core, 1, 0.01f, 0);
    audio_render(core, 4000, 1000);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** The fade of a release is clamped to 5 ms..10 s, a NaN to 5 ms. */
void Instrument_release_fade_clamped(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    int32_t last;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_ATTACK);
    instrument_release(core, 1, NAN, 1000);
    audio_render(core, 0, 4000);
    last = instrument_last(audio_l, 0, 4000);
    test_assert(abs(last - (1000 + AUDIO_RAMP - 1)) <= 1);
    box_audio_core_free(core);
}

/** A sound played without the new flags starts with the ramp as before; with ATTACK it starts
 * at its gain. */
void Instrument_attack_flag(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 1000);
    test_assert(audio_l[0] < 0.01f);
    test_flt(audio_l[AUDIO_RAMP + 10], 0.5f);
    box_audio_core_free(core);
}

/** A sampler note fades out over the release of the instrument after its duration. */
void Instrument_sampler_release(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = audio_dc(world, NULL, 1.0f, AUDIO_RATE * 2);
    ecs_entity_t inst = instrument_new(world, sound, 60, true, 0.1f);
    int32_t at, start, end, k;
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, inst, 60, 0.5f, 0.5f));
    audio_run(world, 30);
    start = instrument_first(audio_tap_l, at, audio_tap_count);
    test_assert(start >= 0);
    for (end = start; end < audio_tap_count && audio_tap_l[end] == 0.25f; end ++) { }
    printf("sampler: plays from %d, its release starts at %d\n", start, end);
    test_assert(abs(end - (start + INSTRUMENT_BEAT / 2)) <= BOX_AUDIO_CORE_BLOCK);
    for (k = 0; k < 4800; k += 480) {
        test_assert(fabsf(audio_tap_l[end + k] - 0.25f * (1 - (float)(k + 1) / 4800)) <
            1e-3f);
    }

    test_flt(audio_peak(audio_tap_l, end + 4800, audio_tap_count), 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** gate false plays the authored length of the sound, whatever the duration. */
void Instrument_no_gate_authored(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = instrument_synth(world, 440, (SynthEnvelope){
        .attack = 0.01f, .sustain = 0.5f, .hold = 0.2f, .release = 0.05f });
    ecs_entity_t inst = instrument_new(world, sound, 69, false, 0.05f);
    int32_t at, start, last;
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, inst, 69, 1, 8));
    audio_run(world, 40);
    start = instrument_first(audio_tap_l, at, audio_tap_count);
    last = instrument_last(audio_tap_l, at, audio_tap_count);
    printf("no gate: %d frames, authored %d\n", last - start, (int32_t)(0.26f * AUDIO_RATE));
    test_assert(abs(last - start - (int32_t)(0.26f * AUDIO_RATE)) <= INSTRUMENT_MS);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** A gated synth note in a world sustains past the hold of its sound and ends one duration
 * and one release after its start. */
void Instrument_synth_duration(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = instrument_synth(world, 440, instrument_sustain);
    ecs_entity_t inst = instrument_new(world, sound, 69, true, 0.05f);
    int32_t at, start, last;
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, inst, 69, 1, 1));
    audio_run(world, 50);
    start = instrument_first(audio_tap_l, at, audio_tap_count);
    last = instrument_last(audio_tap_l, at, audio_tap_count);
    printf("gated synth: %d frames, expected %d\n", last - start, INSTRUMENT_BEAT + 2400);
    test_assert(abs(last - start - (INSTRUMENT_BEAT + 2400)) <= INSTRUMENT_MS);
    test_assert(fabs(instrument_amplitude(audio_tap_l, start + INSTRUMENT_BEAT - 4800,
        start + INSTRUMENT_BEAT - 480) - 0.5) < 0.01);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** A pitch past the limits of the core is clamped, with one warning per instrument. */
void Instrument_pitch_clamped(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = instrument_synth(world, 100, instrument_sustain);
    ecs_entity_t inst = instrument_new(world, sound, 0, true, 0.05f);
    double f;
    audio_run(world, 1);
    audio_log_start();
    test_assert(box_audio_play_note(world, inst, 60, 1, 2));
    audio_run(world, 1);
    test_assert(box_audio_play_note(world, inst, 72, 1, 2));
    audio_run(world, 10);
    audio_log_stop();
    test_int(audio_log_warnings, 1);
    f = audio_frequency(audio_tap_l, audio_tap_count - 4800, audio_tap_count);
    printf("clamped: %f Hz\n", f);
    test_assert(fabs(f - 1600) < 2);
    ecs_fini(world);
}

/** A note plays on the bus of the AudioOutput of the instrument, else on the bus of its
 * sound. */
void Instrument_bus(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = audio_dc(world, NULL, 1.0f, AUDIO_RATE * 2);
    ecs_entity_t inst = instrument_new(world, sound, 60, true, 0.05f);
    ecs_entity_t half = audio_bus(world, "half", 0, 0.5f);
    ecs_entity_t quarter = audio_bus(world, "quarter", 0, 0.25f);
    ecs_set(world, sound, AudioOutput, { .bus = quarter });
    audio_run(world, 1);
    test_assert(box_audio_play_note(world, inst, 60, 1, 1));
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);
    audio_run(world, 30);
    ecs_set(world, inst, AudioOutput, { .bus = half });
    audio_run(world, 1);
    test_assert(box_audio_play_note(world, inst, 60, 1, 1));
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

/** The pan of a note becomes the pan of its voice. */
void Instrument_pan(void) {
    // audio/instrument
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = audio_dc(world, NULL, 1.0f, AUDIO_RATE * 2);
    ecs_entity_t inst = instrument_new(world, sound, 60, true, 0.05f);
    uint64_t start;
    audio_run(world, 1);
    start = box_audio_module_clock(world) + 1600;
    test_assert(box_audio_instrument_note(world, state, inst, 60, 0.5f, 1, start,
        start + 4800, 0, 0) != 0);
    audio_run(world, 4);
    test_flt(audio_level(AUDIO_FRAME), 0);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME) - 0.25f * 1.41421356f) < 1e-4f);
    ecs_fini(world);
}

/** Create a world and run test/assets/audio/instrument.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* instrument_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/instrument.flecs");
    test_assert(ecs_script_run(world, "instrument", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** box.audio.play_note from a script plays a note of an instrument of the script: 880 Hz for
 * pitch 81 on a synth in tune at A4, for a beat and its release. */
void Instrument_script_play_note(void) {
    // audio/instrument
    ecs_world_t *world = instrument_world();
    ecs_entity_t lead = ecs_lookup(world, "lead");
    int32_t at, start, last;
    double f;
    test_assert(lead != 0);
    test_flt(ecs_get(world, lead, Instrument)->root, 69);
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(ecs_script_run(world, "note",
        "using box.audio\n"
        "const played: bool = box.audio.play_note(lead, 81, 1.0, 1.0)\n", NULL) == 0);
    audio_run(world, 20);
    f = audio_frequency(audio_tap_l, audio_tap_count - 4800, audio_tap_count);
    printf("script note: %f Hz\n", f);
    test_assert(fabs(f - 880) < 0.5);
    audio_run(world, 30);
    start = instrument_first(audio_tap_l, at, audio_tap_count);
    last = instrument_last(audio_tap_l, at, audio_tap_count);
    test_assert(abs(last - start - (INSTRUMENT_BEAT + 2400)) <= INSTRUMENT_MS);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(box_audio_module_stats(world).notes, 1);
    ecs_fini(world);
}

/** The duration of a note follows the tempo of the Transport, also while it is stopped. */
void Instrument_tempo(void) {
    // audio/instrument
    ecs_world_t *world = instrument_world();
    ecs_entity_t lead = ecs_lookup(world, "lead");
    int32_t at, start, last;
    test_assert(lead != 0);
    ecs_singleton_set(world, Transport, { .tempo = 60, .beats_per_bar = 4, .beat_unit = 4,
        .lookahead = 0.15f });
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, lead, 69, 1, 0.5f));
    audio_run(world, 50);
    start = instrument_first(audio_tap_l, at, audio_tap_count);
    last = instrument_last(audio_tap_l, at, audio_tap_count);
    printf("half a beat at 60: %d frames\n", last - start);
    test_assert(abs(last - start - (AUDIO_RATE / 2 + 2400)) <= INSTRUMENT_MS);
    ecs_fini(world);
}

/** Two gated notes of a legato synth on one channel, the second starting on the frame where
 * the first ends, are one voice: no new attack, the pitch glides, and the voice releases at
 * the end of the second note. */
void Instrument_legato_notes(void) {
    // audio/instrument
    ecs_world_t *world = instrument_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t line = ecs_lookup(world, "line");
    uint64_t start;
    int32_t s, last, k;
    double low = 1, high = 0, amp, f;
    test_assert(line != 0);
    audio_run(world, 1);
    start = box_audio_module_clock(world) + 1600;
    test_assert(box_audio_instrument_note(world, state, line, 69, 1, 0, start,
        start + 24000, 0, 0) != 0);
    test_assert(box_audio_instrument_note(world, state, line, 76, 1, 0, start + 24000,
        start + 48000, 0, 0) != 0);
    for (k = 0; k < 75; k ++) {
        audio_run(world, 1);
        if (k >= 4 && k <= 60) {
            test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
        }
    }

    audio_run(world, 30);
    s = (int32_t)start;
    test_assert(audio_tap_count > s + 60000);
    for (k = s + 4800; k + 960 <= s + 48000; k += 960) {
        amp = instrument_amplitude(audio_tap_l, k, k + 960);
        low = amp < low ? amp : low;
        high = amp > high ? amp : high;
    }

    printf("legato level: %f to %f\n", low, high);
    test_assert(low > 0.38 && high < 0.42);
    test_assert(fabs(audio_frequency(audio_tap_l, s + 4800, s + 24000) - 440) < 1);
    f = audio_frequency(audio_tap_l, s + 24000 + 480, s + 24000 + 1440);
    printf("during the glide: %f Hz\n", f);
    test_assert(f > 445 && f < 655);
    test_assert(fabs(audio_frequency(audio_tap_l, s + 24000 + 4800, s + 48000) -
        440 * pow(2.0, 7.0 / 12)) < 1.5);
    test_assert(audio_max_step(audio_tap_l, s + 2400, s + 48000) < 0.05f);
    last = instrument_last(audio_tap_l, s, audio_tap_count);
    printf("legato: last sample at %d, the release ends at %d\n", last, s + 48000 + 4800);
    test_assert(abs(last - (s + 48000 + 4800)) <= INSTRUMENT_MS);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** A legato note after the voice released starts a new voice with its attack: the first note
 * releases from 9600 for 0.1 s, the second starts over from 0 at 16800. */
void Instrument_legato_after_release(void) {
    // audio/instrument
    ecs_world_t *world = instrument_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t line = ecs_lookup(world, "line");
    uint64_t start;
    int32_t s;
    test_assert(line != 0);
    audio_run(world, 1);
    start = box_audio_module_clock(world) + 1600;
    test_assert(box_audio_instrument_note(world, state, line, 69, 1, 0, start,
        start + 9600, 0, 0) != 0);
    test_assert(box_audio_instrument_note(world, state, line, 76, 1, 0, start + 16800,
        start + 28800, 0, 0) != 0);
    audio_run(world, 60);
    s = (int32_t)start;
    test_flt(audio_peak(audio_tap_l, s + 14400 + INSTRUMENT_MS, s + 16800), 0);
    test_assert(instrument_amplitude(audio_tap_l, s + 16800, s + 16800 + 96) < 0.05);
    test_assert(fabs(instrument_amplitude(audio_tap_l, s + 16800 + 2400, s + 16800 + 4800) -
        0.4) < 0.01);
    test_assert(fabs(audio_frequency(audio_tap_l, s + 16800 + 2400, s + 28800) -
        440 * pow(2.0, 7.0 / 12)) < 1.5);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** In the core, a legato note at the frame of the release of the note it follows glides on: the
 * release of that frame applies after the legato and finds no voice of its handle. */
void Instrument_legato_cancels_release(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = instrument_probe(0);
    uint8_t bytes[128];
    box_audio_writer_t w;
    box_audio_event_t e;
    int32_t last;
    p.glide = 0.01f;
    instrument_define(core, 1, &p);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play(&w, 7, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE, 0));
    test_assert(box_audio_write_voice_release(&w, 7, 0, 9600));
    test_assert(box_audio_write_legato_at(&w, 8, 7, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE, 9600));
    test_assert(box_audio_write_voice_release(&w, 8, 0, 14400));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, 24000);
    test_assert(fabsf(fabsf(audio_l[14399]) - 0.25f) < 1e-4f);
    test_assert(audio_max_step(audio_l, 100, 24000) < 0.002f);
    last = instrument_last(audio_l, 0, 24000);
    test_assert(abs(last - (14400 + 4800 - 1)) <= 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 8);
    box_audio_core_free(core);
}

/** A stop drops the scheduled releases and legato notes of its handle. */
void Instrument_stop_cancels_release(void) {
    // audio/instrument
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = instrument_probe(0);
    uint8_t bytes[64];
    box_audio_writer_t w;
    instrument_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_GATE);
    instrument_release(core, 1, 0, 9600);
    test_int(box_audio_core_scheduled(core), 1);
    audio_render(core, 0, 1000);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 0);
    box_audio_core_free(core);
}
