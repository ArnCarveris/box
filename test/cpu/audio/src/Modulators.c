/**
 * @file Modulators.c
 * @brief AudioModulator through the null transport at a fixed delta with the modulators of
 * test/assets/audio/modulators.flecs at 120 bpm: a wobble of a lowpass on noise locked to the
 * beat across a seek and a loop wrap, a tremolo, an auto-pan, hard steps, a follower of a drum
 * bus, modulation on top of an automation lane, a removal without a click, the limits and
 * identical renders. Then the modulators and the beat clock of the core alone: renders bit
 * identical across render sizes, a core without modulators unchanged by the beat clock, and a
 * sine LFO at 10 Hz on a cutoff without zipper noise.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of a beat at 120 bpm */
#define MT_BEAT 24000

/** Frames of a cycle of the wobble, an eighth */
#define MT_CYCLE (MT_BEAT / 2)

/** Frames of the windows the brightness of the wobble is measured over, 10 ms */
#define MT_WINDOW 480

/** Level of the noise */
#define MT_NOISE 0.25f

/** Level of the constant sounds */
#define MT_LEVEL 0.5f

/** Turns two phases of the wobble may differ by, 240 frames of an eighth */
#define MT_PHASE_TOLERANCE 0.02

/** Frames of the core renders */
#define MT_FRAMES (AUDIO_RATE * 2)

/** Output of the core renders that a case compares */
static float mt_a_l[MT_FRAMES], mt_a_r[MT_FRAMES];
static float mt_b_l[MT_FRAMES], mt_b_r[MT_FRAMES];

/** Sounds of the core cases: noise and hits */
static float mt_noise[AUDIO_RATE];
static float mt_hits[AUDIO_RATE / 2];

/** Create a world and run test/assets/audio/modulators.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* mt_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/modulators.flecs");
    test_assert(ecs_script_run(world, "modulators", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** Fill a buffer with white noise of a level, the same every time.
 *
 * @param data The buffer
 * @param frames Frames
 * @param level Peak level
 */
static void mt_fill_noise(
    float *data,
    int32_t frames,
    float level)
{
    uint32_t seed = 0x12345678u;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        seed = seed * 1664525u + 1013904223u;
        data[i] = level * ((float)(seed >> 8) / 8388608.0f - 1.0f);
    }
}

/** Fill a buffer with a hit at its start: noise that decays in about 20 ms, then silence.
 *
 * @param data The buffer
 * @param frames Frames
 */
static void mt_fill_hits(
    float *data,
    int32_t frames)
{
    int32_t i;
    mt_fill_noise(data, frames, 0.8f);
    for (i = 0; i < frames; i ++) {
        data[i] *= (float)exp(-(double)i / (0.02 * AUDIO_RATE));
    }
}

/** Play a looping sound on a bus of the script.
 *
 * @param world The world
 * @param sound The sound
 * @param bus Name of the bus
 */
static void mt_source(
    ecs_world_t *world,
    ecs_entity_t sound,
    const char *bus)
{
    ecs_entity_t e = ecs_lookup(world, bus);
    test_assert(e != 0);
    audio_source(world, sound, 1.0f, true, e);
}

/** Create a looping noise sound.
 *
 * @param world The world
 * @return The sound
 */
static ecs_entity_t mt_noise_sound(
    ecs_world_t *world)
{
    mt_fill_noise(mt_noise, AUDIO_RATE, MT_NOISE);
    return box_audio_sound_from_pcm(world, 0, mt_noise, AUDIO_RATE, 1, AUDIO_RATE);
}

/** Start or stop the transport.
 *
 * @param world The world
 * @param playing Whether it plays
 * @return The frame at which it starts or stops, the clock of the next frame
 */
static int64_t mt_play(
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
static void mt_run_to(
    ecs_world_t *world,
    int64_t frame)
{
    while (audio_tap_count <= frame + AUDIO_FRAME) {
        audio_run(world, 1);
    }
}

/** Brightness of the left channel of the tap over MT_WINDOW frames from a frame: the energy of
 * its difference over its energy, which rises with its centroid.
 *
 * @param from First frame
 * @return The brightness
 */
static double mt_brightness(
    int32_t from)
{
    double diff = 0, energy = 1e-20;
    int32_t i;
    test_assert(from >= 1 && from + MT_WINDOW <= audio_tap_count);
    for (i = from; i < from + MT_WINDOW; i ++) {
        double d = (double)audio_tap_l[i] - (double)audio_tap_l[i - 1];
        diff += d * d;
        energy += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    return diff / energy;
}

/** Measure the phase of the wobble: the brightness of whole cycles from a frame, correlated
 * with a sine of a cycle of MT_CYCLE frames that starts on a frame of the grid.
 *
 * @param from First frame
 * @param cycles Cycles measured
 * @param grid A frame at which the beat is a multiple of the sync
 * @param fit Receives how much of the variation of the brightness the cycle explains, about
 *        1.4 for a sine, may be NULL
 * @return The phase of the peak of the brightness in turns after the grid, 0..1
 */
static double mt_phase(
    int32_t from,
    int32_t cycles,
    int64_t grid,
    double *fit)
{
    int32_t n = cycles * MT_CYCLE / MT_WINDOW, k;
    double c = 0, s = 0, mean = 0, var = 0, b[256], t, phase;
    test_assert(n <= 256);
    for (k = 0; k < n; k ++) {
        b[k] = mt_brightness(from + k * MT_WINDOW);
        mean += b[k];
    }

    mean /= n;
    for (k = 0; k < n; k ++) {
        t = (double)(from + k * MT_WINDOW + MT_WINDOW / 2 - grid) / MT_CYCLE;
        c += (b[k] - mean) * cos(2 * AUDIO_PI * t);
        s += (b[k] - mean) * sin(2 * AUDIO_PI * t);
        var += (b[k] - mean) * (b[k] - mean);
    }

    if (fit) {
        *fit = 2.0 * sqrt(c * c + s * s) / n / sqrt(var / n + 1e-30);
    }

    /* The brightness peaks where cos(2 pi (t - phase)) does */
    phase = atan2(s, c) / (2 * AUDIO_PI);

    return phase < 0 ? phase + 1 : phase;
}

/** Distance of two phases in turns.
 *
 * @param a A phase
 * @param b Another phase
 * @return The distance, 0..0.5
 */
static double mt_phase_distance(
    double a,
    double b)
{
    double d = fabs(a - b);
    d -= floor(d);

    return d > 0.5 ? 1 - d : d;
}

/** The cutoff of the lowpass on the noise of the wobble bus goes up and down twice per beat,
 * synced to the transport: the brightness follows a cycle of an eighth, its top a quarter
 * cycle after the grid plus the smoothing of the filter, and a seek to a beat off the grid
 * keeps the peaks where the grid puts them. */
void Modulators_lfo_filter_seek(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    box_audio_module_t *state = box_audio_module_get(world);
    double before, after, fit, lo = 1e30, hi = 0, b;
    int64_t c0, k, grid;
    int32_t i;
    mt_source(world, mt_noise_sound(world), "wobble");
    audio_run(world, 10);
    c0 = mt_play(world, true);
    mt_run_to(world, c0 + 4 * MT_BEAT);
    before = mt_phase((int32_t)c0 + MT_BEAT, 4, c0, &fit);
    test_assert(fit > 0.7);
    for (i = 0; i < 2 * MT_CYCLE / MT_WINDOW; i ++) {
        b = mt_brightness((int32_t)c0 + MT_BEAT + i * MT_WINDOW);
        lo = b < lo ? b : lo;
        hi = b > hi ? b : hi;
    }

    test_assert(hi > 3 * lo);
    test_assert(mt_phase_distance(before, 0.25) < 0.15);

    ecs_singleton_ensure(world, Transport)->position = 10.3;
    ecs_singleton_modified(world, Transport);
    k = audio_tap_count;
    grid = k + llround((10.5 - 10.3) * MT_BEAT);
    mt_run_to(world, k + 4 * MT_BEAT);
    after = mt_phase((int32_t)grid + MT_CYCLE, 4, grid, &fit);
    if (mt_phase_distance(before, after) > MT_PHASE_TOLERANCE) {
        printf("phase %f before the seek, %f after\n", before, after);
    }

    test_assert(fit > 0.7);
    test_assert(mt_phase_distance(before, after) <= MT_PHASE_TOLERANCE);
    test_assert(state->timeline.stats.beats >= 3);
    test_flt(ecs_get(world, ecs_lookup(world, "wobble"), AudioFilter)->cutoff, 1000.0f);
    ecs_fini(world);
}

/** A loop of 1.75 beats wraps off the grid of the eighths: after the wrap the beat starts at 0
 * again, and so does the cycle of the wobble. */
void Modulators_lfo_filter_loop(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    Transport *tr;
    double before, after, fit;
    int64_t c0, wrap;
    mt_source(world, mt_noise_sound(world), "wobble");
    tr = ecs_singleton_ensure(world, Transport);
    tr->loop = true;
    tr->loop_start = 0;
    tr->loop_end = 1.75;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 10);
    c0 = mt_play(world, true);
    wrap = c0 + llround(1.75 * MT_BEAT);
    mt_run_to(world, wrap + 2 * MT_BEAT);
    before = mt_phase((int32_t)c0 + MT_CYCLE / 2, 3, c0, &fit);
    test_assert(fit > 0.7);
    after = mt_phase((int32_t)wrap + MT_CYCLE / 4, 3, wrap, &fit);
    if (mt_phase_distance(before, after) > MT_PHASE_TOLERANCE) {
        printf("phase %f before the wrap, %f after\n", before, after);
    }

    test_assert(fit > 0.7);
    test_assert(mt_phase_distance(before, after) <= MT_PHASE_TOLERANCE);
    ecs_fini(world);
}

/** A unipolar square of depth -1 on the volume of a bus cuts it for the first half of each
 * eighth and lets it through for the second. */
void Modulators_tremolo(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    int64_t c0;
    int32_t k, f;
    mt_source(world, audio_dc(world, NULL, MT_LEVEL, AUDIO_RATE), "trem");
    audio_run(world, 10);
    c0 = mt_play(world, true);
    mt_run_to(world, c0 + 3 * MT_BEAT);
    for (k = 2; k < 6; k ++) {
        f = (int32_t)c0 + k * MT_CYCLE;
        test_assert(fabsf(audio_tap_l[f + MT_CYCLE / 4]) < 0.001f);
        test_assert(fabsf(audio_tap_l[f + 3 * MT_CYCLE / 4] - MT_LEVEL) < 0.001f);
        test_assert(fabsf(audio_tap_r[f + 3 * MT_CYCLE / 4] - MT_LEVEL) < 0.001f);
    }

    test_flt(ecs_get(world, ecs_lookup(world, "trem"), AudioBus)->volume, 1.0f);
    ecs_fini(world);
}

/** A sine on the pan of a bus moves the balance: to the right at the first quarter of each
 * beat, to the left at the third, centered between them. */
void Modulators_auto_pan(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    int64_t c0;
    int32_t k, f;
    mt_source(world, audio_dc(world, NULL, MT_LEVEL, AUDIO_RATE), "pan");
    audio_run(world, 10);
    c0 = mt_play(world, true);
    mt_run_to(world, c0 + 4 * MT_BEAT);
    for (k = 1; k < 3; k ++) {
        f = (int32_t)c0 + k * MT_BEAT;
        test_assert(audio_tap_l[f + MT_BEAT / 4] < 0.01f);
        test_assert(audio_tap_r[f + MT_BEAT / 4] > MT_LEVEL * 1.3f);
        test_assert(audio_tap_r[f + 3 * MT_BEAT / 4] < 0.01f);
        test_assert(audio_tap_l[f + 3 * MT_BEAT / 4] > MT_LEVEL * 1.3f);
        test_assert(fabsf(audio_tap_l[f + MT_BEAT / 2] - audio_tap_r[f + MT_BEAT / 2]) <
            0.02f);
    }

    ecs_fini(world);
}

/** Four hard steps of a sixteenth: the level jumps at the start of each step and holds its value
 * exactly until the next one, every step 6000 frames long. */
void Modulators_steps_hold(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    const float levels[4] = { 1.0f, 0.0f, 0.75f, 0.5f };
    int32_t edges[64], count = 0, i, k, step = MT_BEAT / 4, first;
    int64_t c0;
    mt_source(world, audio_dc(world, NULL, MT_LEVEL, AUDIO_RATE), "seq");
    audio_run(world, 10);
    c0 = mt_play(world, true);
    mt_run_to(world, c0 + 4 * MT_BEAT);
    for (i = (int32_t)c0 + MT_BEAT; i < (int32_t)c0 + 4 * MT_BEAT && count < 64; i ++) {
        if (audio_tap_l[i] != audio_tap_l[i - 1] && audio_tap_l[i - 1] == audio_tap_l[i - 2]) {
            edges[count ++] = i;
        }
    }

    test_assert(count >= 8);
    for (k = 1; k < count; k ++) {
        if (edges[k] - edges[k - 1] != step) {
            printf("steps start at %d and %d\n", edges[k - 1], edges[k]);
        }

        test_int(edges[k] - edges[k - 1], step);
    }

    for (k = 0; k + 1 < count; k ++) {
        float held = audio_tap_l[edges[k + 1] - 1];
        for (i = edges[k] + BOX_AUDIO_MOD_CONTROL * 2; i < edges[k + 1]; i ++) {
            test_assert(audio_tap_l[i] == held);
        }
    }

    first = (int32_t)((((int64_t)edges[0] - c0) + step / 2) / step) % 4;
    test_assert(first >= 0);
    for (k = 0; k + 1 < count; k ++) {
        float expected = MT_LEVEL * levels[(first + k) % 4];
        test_assert(fabsf(audio_tap_l[edges[k + 1] - 1] - expected) < 1e-5f);
    }

    ecs_fini(world);
}

/** A follower of a drum bus nobody hears opens the lowpass of the pad on each hit: within
 * every half second, from one hit to the next, the pad is bright after the hit and dark again
 * before the next. */
void Modulators_follower(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    ecs_entity_t hits;
    double lo, hi, b;
    int64_t c0;
    int32_t k, i, f;
    mt_fill_hits(mt_hits, AUDIO_RATE / 2);
    hits = box_audio_sound_from_pcm(world, 0, mt_hits, AUDIO_RATE / 2, 1, AUDIO_RATE);
    mt_source(world, mt_noise_sound(world), "pad");
    mt_source(world, hits, "drums");
    c0 = audio_tap_count;
    audio_run(world, 200);
    for (k = 2; k < 6; k ++) {
        f = (int32_t)c0 + k * (AUDIO_RATE / 2);
        lo = 1e30;
        hi = 0;
        for (i = 0; i < AUDIO_RATE / 2 / MT_WINDOW; i ++) {
            b = mt_brightness(f + i * MT_WINDOW);
            lo = b < lo ? b : lo;
            hi = b > hi ? b : hi;
        }

        if (!(hi > 4 * lo)) {
            printf("hit %d: brightness from %f to %f\n", k, lo, hi);
        }

        test_assert(hi > 4 * lo);
    }

    ecs_fini(world);
}

/** The lane fades the lead from 1 to 0 over 4 beats and the modulator adds 0.2 on top of it,
 * after the lane too, while the AudioBus keeps its volume. */
void Modulators_with_automation(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    ecs_entity_t lead = ecs_lookup(world, "lead");
    int64_t c0;
    int32_t b;
    mt_source(world, audio_dc(world, NULL, MT_LEVEL, AUDIO_RATE), "lead");
    audio_run(world, 10);
    test_assert(fabsf(audio_tap_l[audio_tap_count - 1] - MT_LEVEL * 1.2f) < 0.001f);
    c0 = mt_play(world, true);
    mt_run_to(world, c0 + 5 * MT_BEAT);
    for (b = 1; b <= 3; b ++) {
        float expected = MT_LEVEL * (1.0f - (float)b / 4.0f + 0.2f);
        test_assert(fabsf(audio_tap_l[c0 + b * MT_BEAT] - expected) < 0.01f);
    }

    test_assert(fabsf(audio_tap_l[c0 + 4 * MT_BEAT + MT_BEAT / 2] - MT_LEVEL * 1.2f) < 0.001f);
    test_flt(ecs_get(world, lead, AudioBus)->volume, 1.0f);
    ecs_fini(world);
}

/** Deleting a modulator that swells a volume fades it out without a click, and the bus returns
 * to its volume; disabling one does the same. */
void Modulators_remove_no_click(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t lfo = ecs_lookup(world, "swell_lfo");
    int32_t k, i;
    mt_source(world, audio_dc(world, NULL, MT_LEVEL, AUDIO_RATE), "swell");
    audio_run(world, 30);
    test_assert(audio_max_step(audio_tap_l, audio_tap_count - AUDIO_RATE / 4,
        audio_tap_count) > 0.00005f);
    k = audio_tap_count;
    ecs_delete(world, lfo);
    audio_run(world, 30);
    test_assert(audio_max_step(audio_tap_l, k - AUDIO_RATE / 10, k + AUDIO_RATE / 4) <
        0.0005f);
    for (i = k + AUDIO_RATE / 5; i < audio_tap_count; i ++) {
        test_assert(fabsf(audio_tap_l[i] - MT_LEVEL) < 1e-6f);
    }

    test_assert(state->modulators.stats.releases >= 1);

    lfo = ecs_lookup(world, "trem_lfo");
    mt_source(world, audio_dc(world, NULL, 0.2f, AUDIO_RATE), "trem");
    audio_run(world, 30);
    k = audio_tap_count;
    ecs_enable(world, lfo, false);
    audio_run(world, 30);
    test_assert(audio_max_step(audio_tap_l, k, audio_tap_count) < 0.01f);
    for (i = k + AUDIO_RATE / 5; i < audio_tap_count; i ++) {
        test_assert(fabsf(audio_tap_l[i] - (MT_LEVEL + 0.2f)) < 1e-6f);
    }

    ecs_fini(world);
}

/** More than 64 modulators each log one error that names them, and so does a fifth modulator
 * of one member; the others run. */
void Modulators_limits(void) {
    // audio/modulators
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t buses[17], m;
    int32_t i, j;
    char name[64];
    for (i = 0; i < 17; i ++) {
        snprintf(name, sizeof(name), "bus%d", i);
        buses[i] = audio_bus(world, name, 0, 1.0f);
    }

    audio_run(world, 2);
    audio_log_start();
    for (i = 0; i < 17; i ++) {
        for (j = 0; j < 4; j ++) {
            snprintf(name, sizeof(name), "mod%d_%d", i, j);
            m = ecs_entity(world, { .name = name });
            ecs_set(world, m, AudioModulator, { .target = buses[i],
                .component = ecs_id(AudioBus), .member = "volume", .depth = 0.1f,
                .rate = 1.0f, .step_count = 16, .attack = 0.01f, .release = 0.1f });
        }
    }

    audio_run(world, 2);
    test_int(audio_log_errors, 4);
    test_assert(strstr(audio_log_last, "mod16_") != NULL);
    m = ecs_entity(world, { .name = "crowd" });
    ecs_set(world, m, AudioModulator, { .target = buses[0], .component = ecs_id(AudioBus),
        .member = "volume", .depth = 0.1f, .rate = 1.0f, .step_count = 16 });
    audio_run(world, 2);
    test_int(audio_log_errors, 5);
    test_assert(strstr(audio_log_last, "crowd") != NULL);
    audio_run(world, 10);
    test_int(audio_log_errors, 5);
    audio_log_stop();
    test_int(state->modulators.stats.errors, 5);
    test_int(box_audio_module_get(world)->modulators.slots[BOX_AUDIO_MOD_MAX - 1].entity != 0,
        1);
    ecs_fini(world);
}

/** A modulator of an unknown member, one in octaves of a member that is no frequency and one
 * keyed on an entity that is no bus each log one error that names them. */
void Modulators_errors(void) {
    // audio/modulators
    ecs_world_t *world = mt_world();
    ecs_entity_t wobble = ecs_lookup(world, "wobble"), m;
    audio_run(world, 2);
    audio_log_start();
    m = ecs_entity(world, { .name = "bad_member" });
    ecs_set(world, m, AudioModulator, { .target = wobble, .component = ecs_id(AudioFilter),
        .member = "nothing", .depth = 1, .rate = 1, .step_count = 16 });
    m = ecs_entity(world, { .name = "bad_scale" });
    ecs_set(world, m, AudioModulator, { .target = wobble, .component = ecs_id(AudioFilter),
        .member = "resonance", .scale = AudioModOctaves, .depth = 1, .rate = 1,
        .step_count = 16 });
    m = ecs_entity(world, { .name = "bad_key" });
    ecs_set(world, m, AudioModulator, { .target = wobble, .component = ecs_id(AudioFilter),
        .member = "cutoff", .source = AudioModFollower, .key = ecs_lookup(world, "wobble_lfo"),
        .depth = 1, .rate = 1, .step_count = 16, .attack = 0.01f, .release = 0.1f });
    audio_run(world, 5);
    test_int(audio_log_errors, 3);
    test_assert(strstr(audio_log_last, "modulator") != NULL);
    audio_run(world, 10);
    test_int(audio_log_errors, 3);
    audio_log_stop();
    ecs_fini(world);
}

/** Render the script with a sound on every bus through 3 beats of the transport.
 *
 * @return The hash of both channels
 */
static uint64_t mt_render(void)
{
    ecs_world_t *world = mt_world();
    uint64_t result;
    mt_fill_hits(mt_hits, AUDIO_RATE / 2);
    mt_source(world, mt_noise_sound(world), "wobble");
    mt_source(world, audio_dc(world, NULL, 0.05f, AUDIO_RATE), "trem");
    mt_source(world, audio_dc(world, NULL, 0.05f, AUDIO_RATE), "pan");
    mt_source(world, audio_dc(world, NULL, 0.05f, AUDIO_RATE), "seq");
    mt_source(world, mt_noise_sound(world), "pad");
    mt_source(world, box_audio_sound_from_pcm(world, 0, mt_hits, AUDIO_RATE / 2, 1,
        AUDIO_RATE), "drums");
    mt_source(world, audio_dc(world, NULL, 0.05f, AUDIO_RATE), "lead");
    audio_run(world, 2);
    mt_play(world, true);
    audio_run(world, 90);
    result = audio_hash(audio_tap_l, audio_tap_count) ^
        (audio_hash(audio_tap_r, audio_tap_count) * 31);
    ecs_fini(world);

    return result;
}

/** Two renders of the same modulators are bit identical. */
void Modulators_deterministic(void) {
    // audio/modulators
    uint64_t a = mt_render(), b = mt_render();
    test_assert(a == b);
}

/** A scene without modulators sends no BEAT command, so the core renders it as before. */
void Modulators_none_no_beats(void) {
    // audio/modulators
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    char *code = audio_read("test/assets/audio/automation.flecs");
    test_assert(ecs_script_run(world, "automation", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 2);
    mt_play(world, true);
    audio_run(world, 60);
    test_int(state->timeline.stats.beats, 0);
    ecs_fini(world);
}

/** Write a modulator command into a core.
 *
 * @param core The core
 * @param cmd The command
 */
static void mt_modulator(
    box_audio_core_t *core,
    const box_audio_cmd_modulator_t *cmd)
{
    uint8_t bytes[512];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_modulator(&w, cmd));
    box_audio_core_apply(core, w.data, w.size);
}

/** Write a BEAT command into a core.
 *
 * @param core The core
 * @param beat The beat
 * @param tempo Beats per second
 * @param when Frame, 0 for now
 */
static void mt_beat(
    box_audio_core_t *core,
    double beat,
    float tempo,
    uint64_t when)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_beat(&w, beat, tempo, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with a lowpass in slot 0 on bus 1, noise on bus 1, hits on bus 2 at volume 0,
 * and, when asked, five modulators of every source with a seek and a change of the filter
 * scheduled ahead.
 *
 * @param modulators Whether to add the modulators
 * @param beats Whether to send BEAT commands
 * @return The core
 */
static box_audio_core_t* mt_core(
    bool modulators,
    bool beats)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(2, 0), AUDIO_OUT(1, 0), AUDIO_MASTER };
    float params[BOX_AUDIO_EFFECT_PARAMS] = { BOX_AUDIO_SYNTH_LOWPASS, 1000, 0.2f, 0, 1, 0 };
    box_audio_cmd_modulator_t m;
    uint8_t bytes[512];
    box_audio_writer_t w;
    float silence[2];
    int32_t i;
    audio_order(core, routes, 3);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_FILTER, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_core_render(core, silence, silence + 1, 1);
    mt_fill_noise(mt_noise, AUDIO_RATE, MT_NOISE);
    mt_fill_hits(mt_hits, AUDIO_RATE / 2);
    test_assert(box_audio_core_sound(core, 1, mt_noise, AUDIO_RATE, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, mt_hits, AUDIO_RATE / 2, 1, AUDIO_RATE));
    audio_bus_set(core, 2, BOX_AUDIO_PARAM_VOLUME, 0);
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 2, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    if (beats) {
        mt_beat(core, 3.3, 2.5f, 0);
        mt_beat(core, 17.6, 2.0f, 30011);
        mt_beat(core, 0, 2.0f, 61003);
    }

    if (modulators) {
        m = (box_audio_cmd_modulator_t){ .slot = 0, .target = BOX_AUDIO_MOD_TARGET_EFFECT,
            .index = 0, .param = 1, .kind = BOX_AUDIO_EFFECT_FILTER,
            .source = BOX_AUDIO_MOD_LFO, .scale = BOX_AUDIO_MOD_OCTAVES, .depth = 2,
            .sync = 0.5f, .step_count = 1 };
        mt_modulator(core, &m);
        m = (box_audio_cmd_modulator_t){ .slot = 1, .target = BOX_AUDIO_MOD_TARGET_BUS,
            .index = 1, .param = BOX_AUDIO_PARAM_VOLUME, .source = BOX_AUDIO_MOD_LFO,
            .wave = BOX_AUDIO_MOD_SQUARE, .unipolar = 1, .depth = -0.5f, .sync = 0.25f,
            .step_count = 1 };
        mt_modulator(core, &m);
        m = (box_audio_cmd_modulator_t){ .slot = 2, .target = BOX_AUDIO_MOD_TARGET_BUS,
            .index = 1, .param = BOX_AUDIO_PARAM_PAN, .source = BOX_AUDIO_MOD_STEPS,
            .depth = 0.8f, .sync = 0.125f, .step_count = 5, .smooth = 0.3f };
        for (i = 0; i < 5; i ++) {
            m.steps[i] = (float)(i % 3) - 1.0f;
        }

        mt_modulator(core, &m);
        m = (box_audio_cmd_modulator_t){ .slot = 3, .target = BOX_AUDIO_MOD_TARGET_EFFECT,
            .index = 0, .param = 2, .kind = BOX_AUDIO_EFFECT_FILTER,
            .source = BOX_AUDIO_MOD_FOLLOWER, .key = 2, .depth = 0.6f, .attack = 0.002f,
            .release = 0.05f, .step_count = 1 };
        mt_modulator(core, &m);
        m = (box_audio_cmd_modulator_t){ .slot = 4, .target = BOX_AUDIO_MOD_TARGET_BUS,
            .index = 1, .param = BOX_AUDIO_PARAM_WIDTH, .source = BOX_AUDIO_MOD_LFO,
            .wave = BOX_AUDIO_MOD_RANDOM, .depth = 0.7f, .rate = 7, .step_count = 1 };
        mt_modulator(core, &m);
    }

    params[1] = 1500;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect_params(&w, 0, BOX_AUDIO_EFFECT_FILTER, params, 45007));
    test_assert(box_audio_write_bus_set(&w, 1, BOX_AUDIO_PARAM_VOLUME, 0.8f, 52001));
    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Render a core in blocks of a size into buffers.
 *
 * @param core The core
 * @param l Left output
 * @param r Right output
 * @param frames Frames
 * @param block Frames per render
 */
static void mt_render_blocks(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t frames,
    int32_t block)
{
    int32_t i;
    for (i = 0; i < frames; i += block) {
        int32_t n = frames - i < block ? frames - i : block;
        box_audio_core_render(core, l + i, r + i, n);
    }
}

/** Five modulators of every source, a seek and a tempo change of the beat clock, a change of
 * the filter and of the volume ahead: renders in blocks of 128, 1, 37 and 1000 frames are bit
 * identical, and a modulator removed from a core lets it go back to its values. */
void Modulators_render_sizes(void) {
    // audio/modulators
    const int32_t blocks[] = { 1, 37, 1000 };
    box_audio_core_t *core = mt_core(true, true);
    uint64_t a, b;
    int32_t k;
    test_int(box_audio_core_modulators(core), 5);
    mt_render_blocks(core, mt_a_l, mt_a_r, MT_FRAMES, BOX_AUDIO_CORE_QUANTUM);
    a = audio_hash(mt_a_l, MT_FRAMES) ^ (audio_hash(mt_a_r, MT_FRAMES) * 31);
    test_assert(audio_peak(mt_a_l, 0, MT_FRAMES) > 0.01f);
    test_assert(fabs(box_audio_core_beat(core, box_audio_core_clock(core)) -
        (double)(MT_FRAMES + 1 - 61003) / 24000.0) < 1e-6);
    box_audio_core_free(core);
    for (k = 0; k < 3; k ++) {
        core = mt_core(true, true);
        mt_render_blocks(core, mt_b_l, mt_b_r, MT_FRAMES, blocks[k]);
        b = audio_hash(mt_b_l, MT_FRAMES) ^ (audio_hash(mt_b_r, MT_FRAMES) * 31);
        if (a != b) {
            int32_t i;
            for (i = 0; i < MT_FRAMES && mt_a_l[i] == mt_b_l[i] && mt_a_r[i] == mt_b_r[i];
                i ++) { }
            printf("blocks of %d differ from frame %d\n", blocks[k], i);
        }

        test_assert(a == b);
        box_audio_core_free(core);
    }
}

/** A core with BEAT commands but no modulator renders bit identical to one without them. */
void Modulators_beats_identical(void) {
    // audio/modulators
    box_audio_core_t *core = mt_core(false, false);
    mt_render_blocks(core, mt_a_l, mt_a_r, MT_FRAMES, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
    core = mt_core(false, true);
    mt_render_blocks(core, mt_b_l, mt_b_r, MT_FRAMES, BOX_AUDIO_CORE_QUANTUM);
    test_int(box_audio_core_modulators(core), 0);
    box_audio_core_free(core);
    test_assert(audio_hash(mt_a_l, MT_FRAMES) == audio_hash(mt_b_l, MT_FRAMES));
    test_assert(audio_hash(mt_a_r, MT_FRAMES) == audio_hash(mt_b_r, MT_FRAMES));
}

/** Releasing every modulator fades them out and returns the core to what it renders without
 * them: within a hair of a core that never had them, once the filter settled. */
void Modulators_release(void) {
    // audio/modulators
    box_audio_core_t *core = mt_core(true, false);
    box_audio_cmd_modulator_t none = { .target = BOX_AUDIO_MOD_TARGET_NONE };
    int32_t i, half = AUDIO_RATE / 2;
    mt_render_blocks(core, mt_a_l, mt_a_r, half, BOX_AUDIO_CORE_QUANTUM);
    for (i = 0; i < 5; i ++) {
        none.slot = (uint32_t)i;
        mt_modulator(core, &none);
    }

    test_int(box_audio_core_modulators(core), 5);
    mt_render_blocks(core, mt_a_l + half, mt_a_r + half, MT_FRAMES - half,
        BOX_AUDIO_CORE_QUANTUM);
    test_int(box_audio_core_modulators(core), 0);
    box_audio_core_free(core);
    core = mt_core(false, false);
    mt_render_blocks(core, mt_b_l, mt_b_r, MT_FRAMES, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
    for (i = MT_FRAMES - half; i < MT_FRAMES; i ++) {
        test_assert(fabsf(mt_a_l[i] - mt_b_l[i]) < 1e-4f);
        test_assert(fabsf(mt_a_r[i] - mt_b_r[i]) < 1e-4f);
    }
}

/** A sine LFO at 10 Hz moving the cutoff of a lowpass by an octave either way of 2 kHz leaves
 * a sine of 200 Hz under it clean: nothing at the rate of the control blocks or its harmonics,
 * where zipper noise would be. */
void Modulators_no_zipper(void) {
    // audio/modulators
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    float params[BOX_AUDIO_EFFECT_PARAMS] = { BOX_AUDIO_SYNTH_LOWPASS, 2000, 0, 0, 1, 0 };
    const double zippers[] = { 1500, 3000, 6000 };
    box_audio_cmd_modulator_t m = { .slot = 0, .target = BOX_AUDIO_MOD_TARGET_EFFECT,
        .index = 0, .param = 1, .kind = BOX_AUDIO_EFFECT_FILTER, .source = BOX_AUDIO_MOD_LFO,
        .scale = BOX_AUDIO_MOD_OCTAVES, .depth = 1, .rate = 10, .step_count = 1 };
    uint8_t bytes[512];
    box_audio_writer_t w;
    float silence[2];
    double tone, w0, c, s0, s1, s2, re, im, amp;
    int32_t i, k, from = AUDIO_RATE / 2, n = AUDIO_RATE / 2;
    audio_order(core, routes, 2);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_FILTER, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_core_render(core, silence, silence + 1, 1);
    audio_sine(audio_sound_a, AUDIO_RATE, 200, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    mt_modulator(core, &m);
    audio_render(core, 0, AUDIO_FRAMES);
    tone = audio_peak(audio_l, from, from + n);
    test_assert(tone > 0.2);
    for (k = 0; k < 3; k ++) {
        w0 = 2.0 * AUDIO_PI * zippers[k] / AUDIO_RATE;
        c = 2.0 * cos(w0);
        s1 = 0;
        s2 = 0;
        for (i = 0; i < n; i ++) {
            s0 = (double)audio_l[from + i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        re = s1 - s2 * cos(w0);
        im = s2 * sin(w0);
        amp = 2.0 * sqrt(re * re + im * im) / n;
        if (amp > tone * 1e-3) {
            printf("%.0f Hz: %g against a tone of %g\n", zippers[k], amp, tone);
        }

        test_assert(amp <= tone * 1e-3);
    }

    box_audio_core_free(core);
}
