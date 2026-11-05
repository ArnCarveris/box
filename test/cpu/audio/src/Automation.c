/**
 * @file Automation.c
 * @brief Automation clips through the null transport at a fixed delta, with the lanes of
 * test/assets/audio/automation.flecs at 120 bpm: a lowpass sweep measured on a sine, the fade
 * of a track and the send of a bus measured on a constant level, a looping placement, a stop,
 * a seek, the errors of test/assets/audio/automation_errors.flecs and identical renders.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of a beat at 120 bpm */
#define AUTO_BEAT 24000

/** Level of the sounds of the cases */
#define AUTO_LEVEL 0.5f

/** Frequency of the sine of the sweep, 20 periods in AUTO_WINDOW */
#define AUTO_TONE 2000.0

/** Frames the amplitude of the sine is measured over, 10 ms */
#define AUTO_WINDOW 480

/** Create a world and run test/assets/audio/automation.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* auto_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/automation.flecs");
    test_assert(ecs_script_run(world, "automation", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** Start or stop the transport.
 *
 * @param world The world
 * @param playing Whether it plays
 * @return The frame at which it starts or stops, the clock of the next frame
 */
static int64_t auto_play(
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
static void auto_run_to(
    ecs_world_t *world,
    int64_t frame)
{
    while (audio_tap_count <= frame + AUDIO_FRAME) {
        audio_run(world, 1);
    }
}

/** Play a looping sound on a bus of the script.
 *
 * @param world The world
 * @param sound The sound
 * @param bus Name of the bus
 */
static void auto_source(
    ecs_world_t *world,
    ecs_entity_t sound,
    const char *bus)
{
    ecs_entity_t e = ecs_lookup(world, bus);
    test_assert(e != 0);
    audio_source(world, sound, 1.0f, true, e);
}

/** Get the frame of a beat after the transport started at a frame.
 *
 * @param c0 The frame of beat 0
 * @param beat The beat
 * @return The frame
 */
static int32_t auto_frame(
    int64_t c0,
    double beat)
{
    return (int32_t)(c0 + llround(beat * AUTO_BEAT));
}

/** Amplitude of the sine of the sweep in the left channel of the tap over AUTO_WINDOW frames
 * around a frame, with the Goertzel algorithm.
 *
 * @param frame The middle of the window
 * @return The peak amplitude
 */
static double auto_amplitude(
    int32_t frame)
{
    double w = 2.0 * AUDIO_PI * AUTO_TONE / (double)AUDIO_RATE, c = 2.0 * cos(w);
    double s0, s1 = 0, s2 = 0, re, im;
    int32_t i, from = frame - AUTO_WINDOW / 2;
    test_assert(from >= 0 && from + AUTO_WINDOW <= audio_tap_count);
    for (i = 0; i < AUTO_WINDOW; i ++) {
        s0 = (double)audio_tap_l[from + i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    re = s1 - s2 * cos(w);
    im = s2 * sin(w);

    return 2.0 * sqrt(re * re + im * im) / (double)AUTO_WINDOW;
}

/** Amplitude a lowpass of 12 dB per octave at a cutoff, with a Butterworth response, lets
 * through of the sine of the sweep.
 *
 * @param cutoff Hz
 * @return The amplitude
 */
static double auto_lowpass(
    double cutoff)
{
    double r = AUTO_TONE / cutoff;

    return (double)AUTO_LEVEL / sqrt(1.0 + r * r * r * r);
}

/** Check that a measured amplitude is within a third of what a cutoff gives.
 *
 * @param frame The middle of the window
 * @param cutoff Hz
 */
static void auto_check_cutoff(
    int32_t frame,
    double cutoff)
{
    double measured = auto_amplitude(frame), expected = auto_lowpass(cutoff);
    if (fabs(measured - expected) > expected / 3.0 + 0.002) {
        printf("at frame %d: amplitude %f, a cutoff of %f gives %f\n", frame, measured,
            cutoff, expected);
    }

    test_assert(fabs(measured - expected) <= expected / 3.0 + 0.002);
}

/** The cutoff of the sweep moves from 200 to 5000 Hz over the 4 beats of its pass: the sine
 * of 2 kHz on the synth bus comes through as the lowpass at the cutoff of the curve at that
 * frame lets it, and once the pass ended at the cutoff of the component again. The component
 * keeps its value. */
void Automation_filter_sweep(void) {
    // audio/automation
    ecs_world_t *world = auto_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t synth = ecs_lookup(world, "synth");
    double prev = 0, amp;
    int64_t c0;
    int32_t b;
    auto_source(world, audio_tone(world, NULL, AUTO_TONE, AUTO_LEVEL, AUDIO_RATE), "synth");
    audio_run(world, 30);
    auto_check_cutoff(audio_tap_count - AUDIO_FRAME, 1000.0);
    c0 = auto_play(world, true);
    auto_run_to(world, auto_frame(c0, 5));
    auto_check_cutoff(auto_frame(c0, 0.25), 200.0 + 1200.0 * 0.25);
    for (b = 1; b <= 3; b ++) {
        auto_check_cutoff(auto_frame(c0, b), 200.0 + 1200.0 * b);
    }

    for (b = 0; b < 16; b ++) {
        amp = auto_amplitude(auto_frame(c0, 0.125 + 0.25 * b));
        test_assert(amp > prev);
        prev = amp;
    }

    auto_check_cutoff(auto_frame(c0, 4.5), 1000.0);
    test_flt(ecs_get(world, synth, AudioFilter)->cutoff, 1000.0f);
    test_assert(state->automation.stats.effect_sets > 100);
    test_assert(state->automation.stats.restores >= 1);
    test_int(state->automation.stats.errors, 0);
    ecs_fini(world);
}

/** The lead track fades its own bus from 1 to 0 over 4 beats and returns to its volume after
 * the pass; the AudioBus of the track keeps a volume of 1. */
void Automation_volume_fade(void) {
    // audio/automation
    ecs_world_t *world = auto_world();
    ecs_entity_t lead = ecs_lookup(world, "lead");
    int64_t c0;
    int32_t b;
    auto_source(world, audio_dc(world, NULL, AUTO_LEVEL, AUDIO_RATE), "lead");
    audio_run(world, 10);
    test_flt(audio_tap_l[audio_tap_count - 1], AUTO_LEVEL);
    c0 = auto_play(world, true);
    auto_run_to(world, auto_frame(c0, 5));
    for (b = 1; b <= 3; b ++) {
        float expected = AUTO_LEVEL * (1.0f - (float)b / 4.0f);
        test_assert(fabsf(audio_tap_l[auto_frame(c0, b)] - expected) < 0.01f);
        test_assert(fabsf(audio_tap_r[auto_frame(c0, b)] - expected) < 0.01f);
    }

    test_assert(audio_tap_l[auto_frame(c0, 3.95)] < 0.02f);
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 4.5)] - AUTO_LEVEL) < 0.001f);
    test_flt(ecs_get(world, lead, AudioBus)->volume, 1.0f);
    ecs_fini(world);
}

/** The send of fx to the room opens over the first beat and stays open for the second; the
 * room only hears the send, and is silent again after the pass. */
void Automation_send_level(void) {
    // audio/automation
    ecs_world_t *world = auto_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t fx = ecs_lookup(world, "fx");
    int64_t c0;
    auto_source(world, audio_dc(world, NULL, AUTO_LEVEL, AUDIO_RATE), "fx");
    audio_run(world, 10);
    test_flt(audio_tap_l[audio_tap_count - 1], 0.0f);
    c0 = auto_play(world, true);
    auto_run_to(world, auto_frame(c0, 3));
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 0.5)] - AUTO_LEVEL * 0.5f) < 0.01f);
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 1.5)] - AUTO_LEVEL) < 0.01f);
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 2.5)]) < 0.001f);
    test_flt(ecs_vec_first_t(&ecs_get(world, fx, AudioBus)->sends, AudioSend)[0].level, 0.0f);
    test_assert(state->automation.stats.bus_sets > 100);
    ecs_fini(world);
}

/** A looping placement of the fade over 8 beats plays the curve twice: the same level in the
 * first and the second pass, a jump back up at the start of the second, the volume of the bus
 * after the placement. */
void Automation_loop(void) {
    // audio/automation
    ecs_world_t *world = auto_world();
    ecs_entity_t lane = ecs_lookup(world, "lead.fade_lane");
    ClipPlacement *p;
    int64_t c0;
    float a, b;
    test_assert(lane != 0);
    p = ecs_ensure(world, lane, ClipPlacement);
    p->loop = true;
    p->duration = 8;
    ecs_modified(world, lane, ClipPlacement);
    auto_source(world, audio_dc(world, NULL, AUTO_LEVEL, AUDIO_RATE), "lead");
    audio_run(world, 10);
    c0 = auto_play(world, true);
    auto_run_to(world, auto_frame(c0, 9));
    a = audio_tap_l[auto_frame(c0, 1)];
    b = audio_tap_l[auto_frame(c0, 5)];
    test_assert(fabsf(a - AUTO_LEVEL * 0.75f) < 0.01f);
    test_assert(fabsf(a - b) < 0.001f);
    test_assert(audio_tap_l[auto_frame(c0, 3.95)] < 0.02f);
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 4.1)] - AUTO_LEVEL * (1.0f - 0.1f / 4)) <
        0.01f);
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 7)] - AUTO_LEVEL * 0.25f) < 0.01f);
    test_assert(fabsf(audio_tap_l[auto_frame(c0, 8.5)] - AUTO_LEVEL) < 0.001f);
    ecs_fini(world);
}

/** A stop halfway through the fade returns the bus to the volume of its component at once,
 * and the values the lane had scheduled ahead never apply. */
void Automation_stop(void) {
    // audio/automation
    ecs_world_t *world = auto_world();
    box_audio_module_t *state = box_audio_module_get(world);
    int64_t c0, s;
    int32_t i;
    auto_source(world, audio_dc(world, NULL, AUTO_LEVEL, AUDIO_RATE), "lead");
    audio_run(world, 10);
    c0 = auto_play(world, true);
    auto_run_to(world, auto_frame(c0, 2));
    test_assert(audio_tap_l[audio_tap_count - 1] < AUTO_LEVEL * 0.6f);
    s = auto_play(world, false);
    auto_run_to(world, s + AUDIO_RATE);
    for (i = (int32_t)s + AUDIO_RATE / 50; i < (int32_t)s + AUDIO_RATE; i ++) {
        if (fabsf(audio_tap_l[i] - AUTO_LEVEL) > 0.001f) {
            printf("frame %d after the stop: %f\n", i - (int32_t)s, (double)audio_tap_l[i]);
        }

        test_assert(fabsf(audio_tap_l[i] - AUTO_LEVEL) <= 0.001f);
    }

    test_assert(state->automation.stats.cancels >= 1);
    test_assert(state->automation.stats.restores >= 1);
    ecs_fini(world);
}

/** A seek from beat 1 to beat 3 of the fade cancels what was scheduled ahead and moves the bus
 * to the level of beat 3 right away. */
void Automation_seek(void) {
    // audio/automation
    ecs_world_t *world = auto_world();
    box_audio_module_t *state = box_audio_module_get(world);
    int64_t c0, k;
    int32_t i;
    auto_source(world, audio_dc(world, NULL, AUTO_LEVEL, AUDIO_RATE), "lead");
    audio_run(world, 10);
    c0 = auto_play(world, true);
    auto_run_to(world, auto_frame(c0, 1));
    ecs_singleton_ensure(world, Transport)->position = 3;
    ecs_singleton_modified(world, Transport);
    k = audio_tap_count;
    auto_run_to(world, k + AUDIO_RATE / 4);
    for (i = (int32_t)k + AUDIO_RATE / 50; i < (int32_t)k + AUDIO_RATE / 4; i ++) {
        double beat = 3.0 + (double)(i - k) / AUTO_BEAT;
        float expected = AUTO_LEVEL * (float)(1.0 - beat / 4.0);
        if (fabsf(audio_tap_l[i] - expected) > 0.01f) {
            printf("frame %d after the seek: %f, expected %f\n", i - (int32_t)k,
                (double)audio_tap_l[i], (double)expected);
        }

        test_assert(fabsf(audio_tap_l[i] - expected) <= 0.01f);
    }

    test_assert(state->automation.stats.cancels >= 1);
    ecs_fini(world);
}

/** A clip with an unknown member, one with a member that is not continuous and one whose
 * target lacks its component each log one error that names the clip, once. */
void Automation_unknown_member(void) {
    // audio/automation
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    char *code = audio_read("test/assets/audio/automation_errors.flecs");
    test_assert(ecs_script_run(world, "automation_errors", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 2);
    audio_log_start();
    auto_play(world, true);
    audio_run(world, 10);
    test_int(audio_log_errors, 3);
    test_assert(strstr(audio_log_last, "automation clip") != NULL);
    audio_run(world, 30);
    test_int(audio_log_errors, 3);
    audio_log_stop();
    test_int(state->automation.stats.errors, 3);
    test_int(state->automation.stats.effect_sets, 0);
    ecs_fini(world);
}

/** Render the script with a sine on the synth bus and a constant level on the lead track and
 * on fx through 3 beats of the transport.
 *
 * @return The hash of the left channel
 */
static uint64_t auto_render(void)
{
    ecs_world_t *world = auto_world();
    uint64_t result;
    auto_source(world, audio_tone(world, NULL, AUTO_TONE, AUTO_LEVEL, AUDIO_RATE), "synth");
    auto_source(world, audio_dc(world, NULL, 0.2f, AUDIO_RATE), "lead");
    auto_source(world, audio_dc(world, NULL, 0.2f, AUDIO_RATE), "fx");
    audio_run(world, 2);
    auto_play(world, true);
    audio_run(world, 90);
    result = audio_hash(audio_tap_l, audio_tap_count);
    ecs_fini(world);

    return result;
}

/** Two renders of the same lanes are bit identical. */
void Automation_deterministic(void) {
    // audio/automation
    uint64_t a = auto_render(), b = auto_render();
    test_assert(a == b);
}
