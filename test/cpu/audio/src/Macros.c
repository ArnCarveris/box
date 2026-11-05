/**
 * @file Macros.c
 * @brief AudioMacro and AudioMacroMap through the null transport at a fixed delta with the
 * macros of test/assets/audio/macros.flecs at 120 bpm: a build-up that maps one value onto a
 * cutoff in octaves and a send level, an automation lane and a modulator on its value, an
 * inverted macro, a removal without a click, two maps of one member, the limits, the errors,
 * the audio command and identical renders.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of a beat at 120 bpm */
#define MC_BEAT 24000

/** Level of the sounds of the cases */
#define MC_LEVEL 0.5f

/** Frequency of the sine the lowpass filters, 20 periods in MC_WINDOW */
#define MC_TONE 2000.0

/** Frames the levels are measured over, 10 ms */
#define MC_WINDOW 480

/** Cutoff of the build-up at a value of 0.5: the middle of 200..8000 Hz in octaves */
#define MC_CUTOFF_HALF 1264.911f

/** Create a world and run test/assets/audio/macros.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* mc_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/macros.flecs");
    test_assert(ecs_script_run(world, "macros", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** Play a looping sound on a bus of the script.
 *
 * @param world The world
 * @param sound The sound
 * @param bus Name of the bus
 */
static void mc_source(
    ecs_world_t *world,
    ecs_entity_t sound,
    const char *bus)
{
    ecs_entity_t e = ecs_lookup(world, bus);
    test_assert(e != 0);
    audio_source(world, sound, 1.0f, true, e);
}

/** Start or stop the transport.
 *
 * @param world The world
 * @param playing Whether it plays
 * @return The frame at which it starts or stops, the clock of the next frame
 */
static int64_t mc_play(
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
static void mc_run_to(
    ecs_world_t *world,
    int64_t frame)
{
    while (audio_tap_count <= frame + AUDIO_FRAME) {
        audio_run(world, 1);
    }
}

/** Get the frame of a beat after the transport started at a frame.
 *
 * @param c0 The frame of beat 0
 * @param beat The beat
 * @return The frame
 */
static int32_t mc_frame(
    int64_t c0,
    double beat)
{
    return (int32_t)(c0 + llround(beat * MC_BEAT));
}

/** Mean of the left channel of the tap over MC_WINDOW frames around a frame: the constant
 * level of a bus, which the sine of MC_TONE adds nothing to over whole periods.
 *
 * @param frame The middle of the window
 * @return The mean
 */
static double mc_mean(
    int32_t frame)
{
    double sum = 0;
    int32_t i, from = frame - MC_WINDOW / 2;
    test_assert(from >= 0 && from + MC_WINDOW <= audio_tap_count);
    for (i = from; i < from + MC_WINDOW; i ++) {
        sum += audio_tap_l[i];
    }

    return sum / MC_WINDOW;
}

/** Amplitude of the sine of MC_TONE in the left channel of the tap over MC_WINDOW frames
 * around a frame, with the Goertzel algorithm; a constant level adds nothing to it.
 *
 * @param frame The middle of the window
 * @return The peak amplitude
 */
static double mc_amplitude(
    int32_t frame)
{
    double w = 2.0 * AUDIO_PI * MC_TONE / (double)AUDIO_RATE, c = 2.0 * cos(w);
    double s0, s1 = 0, s2 = 0, re, im;
    int32_t i, from = frame - MC_WINDOW / 2;
    test_assert(from >= 0 && from + MC_WINDOW <= audio_tap_count);
    for (i = 0; i < MC_WINDOW; i ++) {
        s0 = (double)audio_tap_l[from + i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    re = s1 - s2 * cos(w);
    im = s2 * sin(w);

    return 2.0 * sqrt(re * re + im * im) / (double)MC_WINDOW;
}

/** Check that the amplitude of the sine around a frame is within a third of what a lowpass of
 * 12 dB per octave at a cutoff, with a Butterworth response, lets through.
 *
 * @param frame The middle of the window
 * @param cutoff Hz
 */
static void mc_check_cutoff(
    int32_t frame,
    double cutoff)
{
    double r = MC_TONE / cutoff, expected = (double)MC_LEVEL / sqrt(1.0 + r * r * r * r);
    double measured = mc_amplitude(frame);
    if (fabs(measured - expected) > expected / 3.0 + 0.002) {
        printf("at frame %d: amplitude %f, a cutoff of %f gives %f\n", frame, measured,
            cutoff, expected);
    }

    test_assert(fabs(measured - expected) <= expected / 3.0 + 0.002);
}

/** Check that a mean level around a frame is close to a value.
 *
 * @param frame The middle of the window
 * @param expected The level
 * @param tolerance How far it may be
 */
static void mc_check_level(
    int32_t frame,
    double expected,
    double tolerance)
{
    double measured = mc_mean(frame);
    if (fabs(measured - expected) > tolerance) {
        printf("at frame %d: level %f, expected %f\n", frame, measured, expected);
    }

    test_assert(fabs(measured - expected) <= tolerance);
}

/** Play the sine on the synth bus and a constant level on fx, which only the room hears
 * through the send.
 *
 * @param world The world
 */
static void mc_build_sources(
    ecs_world_t *world)
{
    mc_source(world, audio_tone(world, NULL, MC_TONE, MC_LEVEL, AUDIO_RATE), "synth");
    mc_source(world, audio_dc(world, NULL, MC_LEVEL, AUDIO_RATE), "fx");
}

/** A curve of 0 is linear, 1 exponential and -1 logarithmic, point symmetric to each other;
 * octaves are even in pitch, a min above max inverts, and the value is clamped to 0..1. */
void Macros_curve(void) {
    // audio/macros
    const float e = (float)(7.0 / 63.0), l = (float)(56.0 / 63.0);
    float x, a, b;
    test_assert(fabsf(box_audio_macro_map_value(0.5f, 200, 8000, 0, BOX_AUDIO_MOD_OCTAVES) -
        MC_CUTOFF_HALF) < 0.05f);
    test_assert(fabsf(box_audio_macro_map_value(0.25f, 8000, 200, 0, BOX_AUDIO_MOD_OCTAVES) -
        3181.07f) < 0.05f);
    test_assert(fabsf(box_audio_macro_map_value(0.5f, 0, 0.6f, 0, BOX_AUDIO_MOD_LINEAR) -
        0.3f) < 1e-6f);
    test_assert(fabsf(box_audio_macro_map_value(0.25f, 1, 0.2f, 0, BOX_AUDIO_MOD_LINEAR) -
        0.8f) < 1e-6f);
    test_assert(fabsf(box_audio_macro_map_value(0.5f, 0, 1, 1, BOX_AUDIO_MOD_LINEAR) - e) <
        1e-5f);
    test_assert(fabsf(box_audio_macro_map_value(0.5f, 0, 1, -1, BOX_AUDIO_MOD_LINEAR) - l) <
        1e-5f);
    for (x = 0; x <= 1.0f; x += 0.125f) {
        a = box_audio_macro_map_value(x, 0, 1, 0.4f, BOX_AUDIO_MOD_LINEAR);
        b = box_audio_macro_map_value(1.0f - x, 0, 1, -0.4f, BOX_AUDIO_MOD_LINEAR);
        test_assert(fabsf(a - (1.0f - b)) < 1e-5f);
        test_assert(fabsf(box_audio_macro_map_value(x, 0, 1, 0, BOX_AUDIO_MOD_LINEAR) - x) <
            1e-6f);
    }

    test_flt(box_audio_macro_map_value(0, 200, 8000, 1, BOX_AUDIO_MOD_OCTAVES), 200.0f);
    test_assert(fabsf(box_audio_macro_map_value(1, 200, 8000, -1, BOX_AUDIO_MOD_OCTAVES) -
        8000.0f) < 0.01f);
    test_flt(box_audio_macro_map_value(-3, 2, 4, 0, BOX_AUDIO_MOD_LINEAR), 2.0f);
    test_flt(box_audio_macro_map_value(7, 2, 4, 0, BOX_AUDIO_MOD_LINEAR), 4.0f);
    test_flt(box_audio_macro_map_value(NAN, 2, 4, 0, BOX_AUDIO_MOD_LINEAR), 2.0f);
}

/** At a value of 0.5 the build-up gives the lowpass of the synth a cutoff of about 1265 Hz and
 * the send of fx a level of 0.3, replacing the authored 1000 Hz and 0.1, which the components
 * keep; a write of the value from C applies at once. */
void Macros_value(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t build = ecs_lookup(world, "build"), synth = ecs_lookup(world, "synth");
    ecs_entity_t fx = ecs_lookup(world, "fx");
    int32_t k;
    mc_build_sources(world);
    audio_run(world, 30);
    k = audio_tap_count - MC_WINDOW;
    mc_check_level(k, MC_LEVEL * 0.3, 0.002);
    mc_check_cutoff(k, MC_CUTOFF_HALF);
    test_flt(ecs_get(world, synth, AudioFilter)->cutoff, 1000.0f);
    test_flt(ecs_vec_first_t(&ecs_get(world, fx, AudioBus)->sends, AudioSend)[0].level, 0.1f);
    test_assert(box_audio_macro_slot(world, build) >= 0);
    test_int(state->macros.stats.errors, 0);
    test_int(state->macros.stats.warnings, 0);
    test_assert(state->macros.stats.maps >= 4);

    ecs_set(world, build, AudioMacro, { .value = 1.0f });
    audio_run(world, 3);
    k = audio_tap_count - MC_WINDOW;
    mc_check_level(k, MC_LEVEL * 0.6, 0.002);
    mc_check_cutoff(k, 8000.0);

    ecs_set(world, build, AudioMacro, { .value = 0.0f });
    audio_run(world, 3);
    k = audio_tap_count - MC_WINDOW;
    mc_check_level(k, 0.0, 0.002);
    mc_check_cutoff(k, 200.0);
    test_flt(ecs_get(world, synth, AudioFilter)->cutoff, 1000.0f);
    ecs_fini(world);
}

/** The lane of the build-up sweeps its value from 0 to 1 over 4 beats, and with it the send
 * from 0 to 0.6 and the cutoff from 200 to 8000 Hz in octaves, on the beat; after the pass both
 * return to what the value of the component gives. */
void Macros_automation(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t build = ecs_lookup(world, "build");
    double prev = 0, amp;
    int64_t c0;
    int32_t b;
    mc_build_sources(world);
    audio_run(world, 30);
    c0 = mc_play(world, true);
    mc_run_to(world, mc_frame(c0, 5));
    for (b = 1; b <= 3; b ++) {
        double v = (double)b / 4.0;
        mc_check_level(mc_frame(c0, b), MC_LEVEL * 0.6 * v, 0.004);
        mc_check_cutoff(mc_frame(c0, b), 200.0 * pow(40.0, v));
    }

    mc_check_level(mc_frame(c0, 0.5), MC_LEVEL * 0.6 * 0.125, 0.004);
    for (b = 0; b < 16; b ++) {
        amp = mc_amplitude(mc_frame(c0, 0.125 + 0.25 * b));
        test_assert(amp > prev);
        prev = amp;
    }

    mc_check_level(mc_frame(c0, 4.5), MC_LEVEL * 0.3, 0.002);
    mc_check_cutoff(mc_frame(c0, 4.5), MC_CUTOFF_HALF);
    test_flt(ecs_get(world, build, AudioMacro)->value, 0.5f);
    test_assert(state->automation.stats.macro_sets > 100);
    test_assert(state->automation.stats.restores >= 1);
    test_int(state->automation.stats.errors, 0);
    ecs_fini(world);
}

/** A sine modulator of depth 0.25 on the value of the build-up moves it between 0.25 and 0.75,
 * and with it the send between 0.15 and 0.45 and the cutoff between about 500 and 3200 Hz,
 * while the component keeps 0.5. */
void Macros_modulator(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    ecs_entity_t build = ecs_lookup(world, "build"), m;
    double lo = 1e30, hi = -1e30, alo = 1e30, ahi = 0, v, a;
    int32_t i, from;
    mc_build_sources(world);
    audio_run(world, 10);
    m = ecs_entity(world, { .name = "build_lfo" });
    ecs_set(world, m, AudioModulator, { .target = build, .component = ecs_id(AudioMacro),
        .member = "value", .depth = 0.25f, .rate = 2.0f, .step_count = 16, .attack = 0.01f,
        .release = 0.1f });
    audio_run(world, 60);
    from = audio_tap_count - AUDIO_RATE / 2;
    for (i = 0; i < AUDIO_RATE / 2 / MC_WINDOW - 1; i ++) {
        v = mc_mean(from + MC_WINDOW / 2 + i * MC_WINDOW);
        a = mc_amplitude(from + MC_WINDOW / 2 + i * MC_WINDOW);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
        alo = a < alo ? a : alo;
        ahi = a > ahi ? a : ahi;
    }

    if (fabs(lo - MC_LEVEL * 0.15) > 0.01 || fabs(hi - MC_LEVEL * 0.45) > 0.01) {
        printf("the send moves the level from %f to %f\n", lo, hi);
    }

    test_assert(fabs(lo - MC_LEVEL * 0.15) <= 0.01);
    test_assert(fabs(hi - MC_LEVEL * 0.45) <= 0.01);
    test_assert(ahi > 4 * alo);
    test_flt(ecs_get(world, build, AudioMacro)->value, 0.5f);
    test_int(box_audio_module_get(world)->modulators.stats.errors, 0);
    ecs_fini(world);
}

/** The calm macro, whose ranges fall as it rises: at 0.25 the lead plays at 0.8 of its level
 * and the pad through a cutoff of about 3181 Hz, and at 1 at 0.2 and through 200 Hz. */
void Macros_inverted(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    ecs_entity_t calm = ecs_lookup(world, "calm");
    int32_t k;
    mc_source(world, audio_dc(world, NULL, MC_LEVEL, AUDIO_RATE), "lead");
    mc_source(world, audio_tone(world, NULL, MC_TONE, MC_LEVEL, AUDIO_RATE), "pad");
    audio_run(world, 30);
    k = audio_tap_count - MC_WINDOW;
    mc_check_level(k, MC_LEVEL * 0.8, 0.002);
    mc_check_cutoff(k, 3181.07);
    ecs_set(world, calm, AudioMacro, { .value = 1.0f });
    audio_run(world, 3);
    k = audio_tap_count - MC_WINDOW;
    mc_check_level(k, MC_LEVEL * 0.2, 0.002);
    mc_check_cutoff(k, 200.0);
    test_flt(ecs_get(world, ecs_lookup(world, "lead"), AudioBus)->volume, 1.0f);
    test_flt(ecs_get(world, ecs_lookup(world, "pad"), AudioFilter)->cutoff, 1000.0f);
    ecs_fini(world);
}

/** Deleting the map of the lead fades it out without a click: the lead returns from the 0.8
 * of the calm macro to its authored volume of 1; disabling the macro returns the pad to its
 * authored cutoff. */
void Macros_remove_no_click(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t k, i;
    mc_source(world, audio_dc(world, NULL, MC_LEVEL, AUDIO_RATE), "lead");
    audio_run(world, 30);
    test_assert(fabsf(audio_tap_l[audio_tap_count - 1] - MC_LEVEL * 0.8f) < 1e-4f);
    k = audio_tap_count;
    ecs_delete(world, ecs_lookup(world, "calm.level"));
    audio_run(world, 30);
    test_assert(audio_max_step(audio_tap_l, k - AUDIO_RATE / 10, k + AUDIO_RATE / 4) <
        0.0005f);
    for (i = k + AUDIO_RATE / 5; i < audio_tap_count; i ++) {
        test_assert(fabsf(audio_tap_l[i] - MC_LEVEL) < 1e-5f);
    }

    test_assert(state->macros.stats.releases >= 1);

    mc_source(world, audio_tone(world, NULL, MC_TONE, MC_LEVEL, AUDIO_RATE), "pad");
    audio_run(world, 20);
    mc_check_cutoff(audio_tap_count - MC_WINDOW, 3181.07);
    ecs_enable(world, ecs_lookup(world, "calm"), false);
    audio_run(world, 30);
    mc_check_cutoff(audio_tap_count - MC_WINDOW, 1000.0);
    test_assert(fabs(mc_mean(audio_tap_count - MC_WINDOW) - MC_LEVEL) < 1e-4);
    test_assert(state->macros.stats.releases >= 2);
    ecs_fini(world);
}

/** Two macros that map one member: the map created last wins with one warning, and the other
 * takes over without a click once the newer one is deleted. */
void Macros_two_maps(void) {
    // audio/macros
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t duo = audio_bus(world, "duo", 0, 1.0f), a, b, map_a, map_b;
    int32_t k;
    audio_source(world, audio_dc(world, NULL, MC_LEVEL, AUDIO_RATE), 1.0f, true, duo);
    audio_run(world, 2);
    audio_log_start();
    a = ecs_entity(world, { .name = "first" });
    ecs_set(world, a, AudioMacro, { .value = 1.0f });
    map_a = ecs_entity(world, { .name = "volume", .parent = a });
    ecs_set(world, map_a, AudioMacroMap, { .target = duo, .component = ecs_id(AudioBus),
        .member = "volume", .min = 0, .max = 0.5f });
    b = ecs_entity(world, { .name = "second" });
    ecs_set(world, b, AudioMacro, { .value = 1.0f });
    map_b = ecs_entity(world, { .name = "volume", .parent = b });
    ecs_set(world, map_b, AudioMacroMap, { .target = duo, .component = ecs_id(AudioBus),
        .member = "volume", .min = 0, .max = 0.25f });
    audio_run(world, 30);
    test_int(audio_log_warnings, 1);
    test_int(audio_log_errors, 0);
    test_assert(fabsf(audio_tap_l[audio_tap_count - 1] - MC_LEVEL * 0.25f) < 1e-4f);
    k = audio_tap_count;
    ecs_delete(world, map_b);
    audio_run(world, 30);
    test_int(audio_log_warnings, 1);
    audio_log_stop();
    test_assert(audio_max_step(audio_tap_l, k, audio_tap_count) < 0.001f);
    test_assert(fabsf(audio_tap_l[audio_tap_count - 1] - MC_LEVEL * 0.5f) < 1e-4f);
    test_int(state->macros.stats.warnings, 1);
    ecs_fini(world);
}

/** A macro with more than 8 maps logs one error that names it, and so does a macro past the
 * 32nd; the others run. */
void Macros_limits(void) {
    // audio/macros
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t buses[BOX_AUDIO_MACRO_MAPS + 1], wide, m;
    int32_t i, runs = 0;
    char name[64];
    for (i = 0; i <= BOX_AUDIO_MACRO_MAPS; i ++) {
        snprintf(name, sizeof(name), "bus%d", i);
        buses[i] = audio_bus(world, name, 0, 1.0f);
    }

    audio_run(world, 2);
    audio_log_start();
    wide = ecs_entity(world, { .name = "wide" });
    ecs_set(world, wide, AudioMacro, { .value = 0.5f });
    for (i = 0; i <= BOX_AUDIO_MACRO_MAPS; i ++) {
        snprintf(name, sizeof(name), "map%d", i);
        m = ecs_entity(world, { .name = name, .parent = wide });
        ecs_set(world, m, AudioMacroMap, { .target = buses[i], .component = ecs_id(AudioBus),
            .member = "volume", .min = 0, .max = 1 });
    }

    audio_run(world, 2);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "wide") != NULL);
    for (i = 0; i < BOX_AUDIO_MACROS; i ++) {
        snprintf(name, sizeof(name), "macro%d", i);
        m = ecs_entity(world, { .name = name });
        ecs_set(world, m, AudioMacro, { .value = 0.5f });
    }

    audio_run(world, 2);
    test_int(audio_log_errors, 2);
    test_assert(strstr(audio_log_last, "more than 32 macros") != NULL);
    audio_run(world, 10);
    test_int(audio_log_errors, 2);
    audio_log_stop();
    test_int(state->macros.stats.errors, 2);
    for (i = 0; i < BOX_AUDIO_MACRO_MAX; i ++) {
        runs += state->macros.slots[i].entity != 0;
    }

    test_int(runs, BOX_AUDIO_MACRO_MAX);
    test_assert(box_audio_macro_slot(world, wide) >= 0);
    ecs_fini(world);
}

/** A map of an unknown member, one in octaves of a member that is no frequency, one of the
 * value of a macro and one that is no child of a macro each log one error that names them. */
void Macros_errors(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    ecs_entity_t build = ecs_lookup(world, "build"), synth = ecs_lookup(world, "synth"), m;
    audio_run(world, 2);
    audio_log_start();
    m = ecs_entity(world, { .name = "bad_member", .parent = build });
    ecs_set(world, m, AudioMacroMap, { .target = synth, .component = ecs_id(AudioFilter),
        .member = "nothing", .max = 1 });
    m = ecs_entity(world, { .name = "bad_scale", .parent = build });
    ecs_set(world, m, AudioMacroMap, { .target = synth, .component = ecs_id(AudioFilter),
        .member = "resonance", .max = 1, .scale = AudioModOctaves });
    m = ecs_entity(world, { .name = "bad_target", .parent = build });
    ecs_set(world, m, AudioMacroMap, { .target = ecs_lookup(world, "calm"),
        .component = ecs_id(AudioMacro), .member = "value", .max = 1 });
    m = ecs_entity(world, { .name = "orphan" });
    ecs_set(world, m, AudioMacroMap, { .target = synth, .component = ecs_id(AudioFilter),
        .member = "cutoff", .max = 1 });
    audio_run(world, 5);
    test_int(audio_log_errors, 4);
    test_assert(strstr(audio_log_last, "macro map") != NULL);
    audio_run(world, 10);
    test_int(audio_log_errors, 4);
    audio_log_stop();
    test_str(box_audio_macro_status(world, m), "does nothing: see the error it logged");
    test_str(box_audio_macro_status(world, build), "runs");
    ecs_fini(world);
}

/** The audio command lists the macros with their value and their maps with what they move. */
void Macros_command(void) {
    // audio/macros
    ecs_world_t *world = mc_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out;
    audio_run(world, 5);
    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(out != NULL);
    printf("%s\n", out);
    test_assert(strstr(out, "macro build: value 0.50 (runs)") != NULL);
    test_assert(strstr(out, "map build.cutoff: synth AudioFilter.cutoff 200.00..8000.00 oct "
        "= 1264.91 (runs)") != NULL);
    test_assert(strstr(out, "map build.reverb: fx AudioBus.sends[0].level 0.00..0.60 = 0.30 "
        "(runs)") != NULL);
    test_assert(strstr(out, "map calm.level: lead AudioBus.volume 1.00..0.20 = 0.80 (runs)") !=
        NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Render the script with a sound on every bus, a modulator on the calm macro and the lane of
 * the build-up through 5 beats of the transport.
 *
 * @return The hash of both channels
 */
static uint64_t mc_render(void)
{
    ecs_world_t *world = mc_world();
    ecs_entity_t m = ecs_entity(world, { .name = "calm_lfo" });
    uint64_t result;
    mc_build_sources(world);
    mc_source(world, audio_dc(world, NULL, 0.1f, AUDIO_RATE), "lead");
    mc_source(world, audio_tone(world, NULL, 300.0, 0.2f, AUDIO_RATE), "pad");
    ecs_set(world, m, AudioModulator, { .target = ecs_lookup(world, "calm"),
        .component = ecs_id(AudioMacro), .member = "value", .depth = 0.2f,
        .wave = AudioModSquare, .sync = 0.5f, .rate = 1, .step_count = 16, .attack = 0.01f,
        .release = 0.1f });
    audio_run(world, 2);
    mc_play(world, true);
    audio_run(world, 150);
    result = audio_hash(audio_tap_l, audio_tap_count) ^
        (audio_hash(audio_tap_r, audio_tap_count) * 31);
    ecs_fini(world);

    return result;
}

/** Two renders of the same macros are bit identical. */
void Macros_deterministic(void) {
    // audio/macros
    uint64_t a = mc_render(), b = mc_render();
    test_assert(a == b);
}
