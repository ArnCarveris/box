/**
 * @file SynthVelocity.c
 * @brief The velocity of a note in a synth voice: the cutoff, the amount of the filter
 * envelope, the index of a modulator and the hardness of a pluck, through notes of an
 * Instrument on a world with the null output, and plays without a note on a core.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Frames of a render of a note */
#define VELOCITY_FRAMES (AUDIO_RATE / 2)

/** Frames from the start of a note to the window that is measured */
#define VELOCITY_SKIP 2400

/** Samples of the renders of a test, from the first frame of a note */
static float velocity_a[VELOCITY_FRAMES];
static float velocity_b[VELOCITY_FRAMES];
static float velocity_c[VELOCITY_FRAMES];

/** The components of a sound that a test sets on its root, after a SynthSound and an
 * envelope that sustains while the note holds.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount Value of the velocity field the test varies
 */
typedef void (*velocity_setup_t)(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount);

/** A saw at 220 Hz through a lowpass at 2000 Hz whose cutoff follows the velocity.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount SynthFilter.velocity
 */
static void velocity_filter(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount)
{
    ecs_set(world, sound, SynthOscillator, {
        .wave = SynthSaw, .frequency = 220, .level = 0.5f, .index = 1 });
    ecs_set(world, sound, SynthFilter, {
        .kind = SynthLowPass, .cutoff = 2000, .velocity = amount });
}

/** A saw at 220 Hz through a lowpass at 200 Hz that a filter envelope of 4 octaves opens and
 * closes again over 0.3 s, by an amount that follows the velocity.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount SynthFilterEnvelope.velocity
 */
static void velocity_filter_envelope(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount)
{
    ecs_set(world, sound, SynthOscillator, {
        .wave = SynthSaw, .frequency = 220, .level = 0.5f, .index = 1 });
    ecs_set(world, sound, SynthFilter, { .kind = SynthLowPass, .cutoff = 200 });
    ecs_set(world, sound, SynthFilterEnvelope, {
        .attack = 0.002f, .decay = 0.3f, .sustain = 0, .release = 0.05f, .amount = 4,
        .velocity = amount });
}

/** A sine carrier at 220 Hz with a sine modulator at 220 Hz of index 4 that follows the
 * velocity.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount SynthOscillator.velocity of the modulator
 */
static void velocity_fm(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount)
{
    ecs_entity_t m = ecs_new_w_pair(world, EcsChildOf, sound);
    ecs_set(world, sound, SynthOscillator, {
        .wave = SynthSine, .frequency = 220, .level = 0.5f, .index = 1 });
    ecs_set(world, m, SynthOscillator, {
        .wave = SynthSine, .frequency = 220, .level = 1, .modulation = SynthPhase,
        .index = 4, .velocity = amount });
}

/** A pluck at 220 Hz with a hard pick whose hardness follows the velocity.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount SynthString.velocity
 */
static void velocity_pluck(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount)
{
    ecs_set(world, sound, SynthOscillator, {
        .wave = SynthPluck, .frequency = 220, .level = 0.5f, .index = 1 });
    ecs_set(world, sound, SynthString, {
        .decay = 1, .brightness = 0.5f, .hardness = 1, .velocity = amount });
}

/** The saw of velocity_filter as a unison of 3 copies spread over the full width, which
 * renders in stereo.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount SynthFilter.velocity
 */
static void velocity_wide(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount)
{
    velocity_filter(world, sound, amount);
    ecs_set(world, sound, SynthUnison, { .voices = 3, .detune = 10, .width = 1 });
}

/** Create a synth sound with a sustaining envelope and the components of a setup.
 *
 * @param world The world
 * @param setup The setup
 * @param amount Value of the velocity field of the setup
 * @return The sound
 */
static ecs_entity_t velocity_sound(
    ecs_world_t *world,
    velocity_setup_t setup,
    float amount)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, SynthSound, {0});
    ecs_set(world, e, SynthEnvelope, {
        .attack = 0.002f, .decay = 0.01f, .sustain = 1, .hold = 0.05f, .release = 0.05f });
    setup(world, e, amount);

    return e;
}

/** Create an instrument whose gain does not depend on the velocity: a velocity curve of 0,
 * so a note at any velocity above 0 plays at gain 1 and only the voice hears the velocity.
 *
 * @param world The world
 * @param sound The sound
 * @return The instrument
 */
static ecs_entity_t velocity_instrument(
    ecs_world_t *world,
    ecs_entity_t sound)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Instrument, { .sound = sound, .root = 69, .velocity_curve = 0,
        .gate = true, .release = 0.05f });

    return e;
}

/** Render a note of A4 at a velocity, a beat long, on a world of its own, and copy
 * VELOCITY_FRAMES of both channels of the tap from the first frame that is not silent.
 *
 * @param setup The setup of the sound
 * @param amount Value of the velocity field of the setup
 * @param velocity Velocity of the note
 * @param out VELOCITY_FRAMES samples of the left channel
 * @param out_r VELOCITY_FRAMES samples of the right channel, or NULL
 */
static void velocity_render_stereo(
    velocity_setup_t setup,
    float amount,
    float velocity,
    float *out,
    float *out_r)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = velocity_sound(world, setup, amount);
    ecs_entity_t inst = velocity_instrument(world, sound);
    int32_t at, start;
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, inst, 69, velocity, 1));
    audio_run(world, 50);
    for (start = at; start < audio_tap_count && audio_tap_l[start] == 0; start ++) { }
    test_assert(start + VELOCITY_FRAMES <= audio_tap_count);
    memcpy(out, audio_tap_l + start, sizeof(float) * VELOCITY_FRAMES);
    if (out_r) {
        memcpy(out_r, audio_tap_r + start, sizeof(float) * VELOCITY_FRAMES);
    }

    ecs_fini(world);
}

/** Render a note of A4 at a velocity and copy VELOCITY_FRAMES of the left channel, see
 * velocity_render_stereo.
 *
 * @param setup The setup of the sound
 * @param amount Value of the velocity field of the setup
 * @param velocity Velocity of the note
 * @param out VELOCITY_FRAMES samples
 */
static void velocity_render(
    velocity_setup_t setup,
    float amount,
    float velocity,
    float *out)
{
    velocity_render_stereo(setup, amount, velocity, out, NULL);
}

/** Brightness of a range, independent of its gain: the energy of its first difference over
 * its energy, which grows with the share of high frequencies.
 *
 * @param x Samples
 * @param from First frame, at least 1
 * @param to End frame
 * @return The brightness
 */
static double velocity_bright(
    const float *x,
    int32_t from,
    int32_t to)
{
    double d = 0, e = 0, s;
    int32_t i;
    for (i = from; i < to; i ++) {
        s = (double)x[i] - (double)x[i - 1];
        d += s * s;
        e += (double)x[i] * x[i];
    }

    return e > 0 ? d / e : 0;
}

/** With SynthFilter.velocity 3 a note at velocity 0.25 is darker than at velocity 1, and at
 * velocity 1 the voice plays the authored cutoff exactly: bit-identical to a filter without
 * velocity. */
void SynthVelocity_filter_cutoff(void) {
    // audio/synth
    double soft, hard;
    velocity_render(velocity_filter, 3, 0.25f, velocity_a);
    velocity_render(velocity_filter, 3, 1, velocity_b);
    velocity_render(velocity_filter, 0, 1, velocity_c);
    soft = velocity_bright(velocity_a, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    hard = velocity_bright(velocity_b, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    printf("filter velocity 3: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);
    test_assert(memcmp(velocity_b, velocity_c, sizeof(velocity_b)) == 0);
}

/** With SynthFilter.velocity 0 the velocity of a note does not change the voice. */
void SynthVelocity_filter_default(void) {
    // audio/synth
    velocity_render(velocity_filter, 0, 0.25f, velocity_a);
    velocity_render(velocity_filter, 0, 1, velocity_b);
    test_assert(memcmp(velocity_a, velocity_b, sizeof(velocity_a)) == 0);
}

/** SynthFilterEnvelope.velocity scales the sweep: at share 1 a soft note opens the filter less
 * at the top of the envelope, at share 0 the velocity does not change it. */
void SynthVelocity_filter_envelope(void) {
    // audio/synth
    double soft, hard;
    velocity_render(velocity_filter_envelope, 1, 0.25f, velocity_a);
    velocity_render(velocity_filter_envelope, 1, 1, velocity_b);
    soft = velocity_bright(velocity_a, 240, 4800);
    hard = velocity_bright(velocity_b, 240, 4800);
    printf("filter envelope velocity 1: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);

    velocity_render(velocity_filter_envelope, 0, 0.25f, velocity_a);
    velocity_render(velocity_filter_envelope, 0, 1, velocity_c);
    test_assert(memcmp(velocity_a, velocity_c, sizeof(velocity_a)) == 0);
    test_assert(memcmp(velocity_b, velocity_c, sizeof(velocity_b)) == 0);
}

/** An FM patch whose modulator has SynthOscillator.velocity 1 has fewer sidebands at a low
 * velocity: an index of 1 instead of 4. */
void SynthVelocity_fm_index(void) {
    // audio/synth
    double soft, hard;
    velocity_render(velocity_fm, 1, 0.25f, velocity_a);
    velocity_render(velocity_fm, 1, 1, velocity_b);
    soft = velocity_bright(velocity_a, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    hard = velocity_bright(velocity_b, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    printf("fm index velocity 1: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);

    velocity_render(velocity_fm, 0, 1, velocity_c);
    test_assert(memcmp(velocity_b, velocity_c, sizeof(velocity_b)) == 0);
}

/** A pluck with SynthString.velocity 1 is plucked softer, so darker, at a low velocity. */
void SynthVelocity_pluck_hardness(void) {
    // audio/synth
    double soft, hard;
    velocity_render(velocity_pluck, 1, 0.25f, velocity_a);
    velocity_render(velocity_pluck, 1, 1, velocity_b);
    soft = velocity_bright(velocity_a, 1, 4800);
    hard = velocity_bright(velocity_b, 1, 4800);
    printf("pluck velocity 1: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);

    velocity_render(velocity_pluck, 0, 1, velocity_c);
    test_assert(memcmp(velocity_b, velocity_c, sizeof(velocity_b)) == 0);
}

/** The stereo render of a unison with width takes the velocity as the mono one does: with
 * SynthFilter.velocity 3 both channels are darker at velocity 0.25, and at velocity 1 they are
 * bit-identical to the filter without velocity. */
void SynthVelocity_wide(void) {
    // audio/synth
    static float right_a[VELOCITY_FRAMES], right_b[VELOCITY_FRAMES], right_c[VELOCITY_FRAMES];
    double soft, hard;
    velocity_render_stereo(velocity_wide, 3, 0.25f, velocity_a, right_a);
    velocity_render_stereo(velocity_wide, 3, 1, velocity_b, right_b);
    velocity_render_stereo(velocity_wide, 0, 1, velocity_c, right_c);
    test_assert(memcmp(velocity_b, right_b, sizeof(velocity_b)) != 0);
    soft = velocity_bright(velocity_a, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    hard = velocity_bright(velocity_b, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    printf("wide filter velocity 3, left: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);
    soft = velocity_bright(right_a, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    hard = velocity_bright(right_b, VELOCITY_SKIP, VELOCITY_FRAMES / 2);
    printf("wide filter velocity 3, right: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);
    test_assert(memcmp(velocity_b, velocity_c, sizeof(velocity_b)) == 0);
    test_assert(memcmp(right_b, right_c, sizeof(right_b)) == 0);
}

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
static char* velocity_read(
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

/** The velocity fields set in a script: each reads back as authored, and a note of the
 * scripted instrument at velocity 0.25 is darker than one at velocity 1. */
void SynthVelocity_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = velocity_read("test/assets/audio/synth_velocity.flecs");
    ecs_entity_t saw, modulator, pluck, lead;
    int32_t at, next;
    double soft, hard;
    test_assert(ecs_script_run(world, "synth_velocity", code, NULL) == 0);
    ecs_os_free(code);
    saw = ecs_lookup(world, "velocity_saw");
    modulator = ecs_lookup(world, "velocity_saw.modulator");
    pluck = ecs_lookup(world, "velocity_pluck");
    lead = ecs_lookup(world, "velocity_lead");
    test_assert(saw != 0 && modulator != 0 && pluck != 0 && lead != 0);
    test_flt(ecs_get(world, saw, SynthFilter)->velocity, 3);
    test_flt(ecs_get(world, saw, SynthFilterEnvelope)->velocity, 0.5f);
    test_flt(ecs_get(world, saw, SynthOscillator)->velocity, 0);
    test_flt(ecs_get(world, modulator, SynthOscillator)->velocity, 0.5f);
    test_flt(ecs_get(world, pluck, SynthString)->velocity, 1);

    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, lead, 69, 0.25f, 0.5f));
    audio_run(world, 50);
    next = audio_tap_count;
    test_assert(box_audio_play_note(world, lead, 69, 1, 0.5f));
    audio_run(world, 50);
    test_assert(next + VELOCITY_FRAMES / 2 <= audio_tap_count);
    soft = velocity_bright(audio_tap_l, at + VELOCITY_SKIP, at + VELOCITY_FRAMES / 2);
    hard = velocity_bright(audio_tap_l, next + VELOCITY_SKIP, next + VELOCITY_FRAMES / 2);
    printf("scripted velocity: brightness %f at 0.25, %f at 1\n", soft, hard);
    test_assert(soft < hard * 0.5);
    ecs_fini(world);
}

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void velocity_define(
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

/** A saw with an FM modulator and a pluck through a lowpass with a filter envelope, with
 * every velocity field at a share or depth of velocity, or at 0.
 *
 * @param velocity Whether the velocity fields are set
 * @return The patch
 */
static box_audio_synth_patch_t velocity_patch(
    bool velocity)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 3;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.05f,
        .sustain = 0.6f, .hold = 0.2f, .release = 0.1f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 800;
    p.resonance = 0.3f;
    p.filter_amount = 3;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.002f, .decay = 0.2f,
        .sustain = 0.2f, .hold = 0.2f, .release = 0.1f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 220, .level = 0.3f };
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 440, .level = 1, .modulates = 1,
        .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 3 };
    p.layer[2] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 330, .level = 0.3f, .decay = 1,
        .brightness = 0.5f, .hardness = 0.9f };
    if (velocity) {
        p.filter_velocity = 3;
        p.filter_envelope_velocity = 1;
        p.layer[1].index_velocity = 1;
        p.layer[2].hardness_velocity = 1;
    }

    return p;
}

/** A play that is not a note, box_audio_write_play, plays at velocity 1: a patch with every
 * velocity field set renders bit-identical to the same patch without them, and to a play at
 * velocity 1 with a priority. */
void SynthVelocity_plain_play(void) {
    // audio/synth
    box_audio_synth_patch_t with = velocity_patch(true), without = velocity_patch(false);
    box_audio_core_t *core = audio_new();
    uint8_t bytes[64];
    box_audio_writer_t w;
    uint64_t a, b, c, d;
    velocity_define(core, 1, &with);
    velocity_define(core, 2, &without);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    a = audio_hash(audio_l, AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    core = audio_new();
    velocity_define(core, 1, &with);
    velocity_define(core, 2, &without);
    audio_play(core, 1, 2, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    b = audio_hash(audio_l, AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    core = audio_new();
    velocity_define(core, 1, &with);
    velocity_define(core, 2, &without);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    c = audio_hash(audio_l, AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    core = audio_new();
    velocity_define(core, 1, &with);
    velocity_define(core, 2, &without);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, 1, 1, 0, 1, 0, 1, 0.25f, 0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    d = audio_hash(audio_l, AUDIO_FRAMES / 2);
    box_audio_core_free(core);

    test_assert(a == b);
    test_assert(a == c);
    test_assert(a != d);
}

/** A velocity out of 0..1 is clamped by the core: 2 plays as 1, -1 and NaN as 0. */
void SynthVelocity_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = velocity_patch(true);
    const float velocity[] = { 1, 2, 0, -1, NAN };
    uint64_t hash[5];
    uint8_t bytes[64];
    box_audio_writer_t w;
    int32_t i, k;
    for (i = 0; i < 5; i ++) {
        box_audio_core_t *core = audio_new();
        velocity_define(core, 1, &p);
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        test_assert(box_audio_write_play_priority(&w, 1, 1, 0, 1, 0, 1, velocity[i], 0, 0,
            0));
        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, AUDIO_FRAMES / 4);
        for (k = 0; k < AUDIO_FRAMES / 4; k ++) {
            test_assert(isfinite(audio_l[k]));
        }

        hash[i] = audio_hash(audio_l, AUDIO_FRAMES / 4);
        box_audio_core_free(core);
    }

    test_assert(hash[0] == hash[1]);
    test_assert(hash[2] == hash[3]);
    test_assert(hash[2] == hash[4]);
    test_assert(hash[0] != hash[2]);
}

/** A saw at 220 Hz through a lowpass at 2000 Hz with SynthFilter.velocity 3, as a legato
 * sound without a glide.
 *
 * @param world The world
 * @param sound The root of the sound
 * @param amount SynthFilter.velocity
 */
static void velocity_legato(
    ecs_world_t *world,
    ecs_entity_t sound,
    float amount)
{
    velocity_filter(world, sound, amount);
    ecs_set(world, sound, SynthGlide, { .time = 0, .legato = true });
}

/** Render two gated legato notes of A4 and E5, a voice that the second retargets, at two
 * velocities on a world of its own, from the start of the first note.
 *
 * @param first Velocity of the first note
 * @param second Velocity of the second note
 * @param out VELOCITY_FRAMES samples
 */
static void velocity_render_legato(
    float first,
    float second,
    float *out)
{
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = velocity_sound(world, velocity_legato, 3);
    ecs_entity_t inst = velocity_instrument(world, sound);
    uint64_t start;
    int32_t s;
    audio_run(world, 1);
    start = box_audio_module_clock(world) + 1600;
    test_assert(box_audio_instrument_note(world, state, inst, 69, first, 0, start,
        start + VELOCITY_FRAMES / 2, 0, 0) != 0);
    test_assert(box_audio_instrument_note(world, state, inst, 76, second, 0,
        start + VELOCITY_FRAMES / 2, start + VELOCITY_FRAMES, 0, 0) != 0);
    audio_run(world, 50);
    s = (int32_t)start;
    test_assert(s + VELOCITY_FRAMES <= audio_tap_count);
    memcpy(out, audio_tap_l + s, sizeof(float) * VELOCITY_FRAMES);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** A legato note keeps the velocity of the note that started the voice: the velocity of the
 * second note does not change the voice, the velocity of the first does. */
void SynthVelocity_legato_keeps(void) {
    // audio/synth
    double soft, hard;
    velocity_render_legato(1, 0.25f, velocity_a);
    velocity_render_legato(1, 1, velocity_b);
    test_assert(memcmp(velocity_a, velocity_b, sizeof(velocity_a)) == 0);

    velocity_render_legato(0.25f, 1, velocity_c);
    soft = velocity_bright(velocity_c, VELOCITY_FRAMES / 2 + VELOCITY_SKIP,
        VELOCITY_FRAMES - VELOCITY_SKIP);
    hard = velocity_bright(velocity_b, VELOCITY_FRAMES / 2 + VELOCITY_SKIP,
        VELOCITY_FRAMES - VELOCITY_SKIP);
    printf("legato after a note at 0.25: brightness %f, after a note at 1: %f\n", soft, hard);
    test_assert(soft < hard * 0.5);
}
