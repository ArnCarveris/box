/**
 * @file Delay.c
 * @brief The delay kind of effect in an insert slot of a bus, from an impulse in an offline
 * render: the time of its echo, its feedback and damping, ping pong, its clamp, its silence,
 * its mix and its time changes, and the Delay component synced to the Transport in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, six seconds at 48 kHz */
#define DL_MAX (AUDIO_RATE * 6)

/** Frame of the click in its sound, after the start ramp of the voice */
#define DL_CLICK 300

/** Level of the click */
#define DL_LEVEL 0.5f

/** A level above this counts as sound when a case looks for an echo */
#define DL_THRESHOLD 1e-6f

/** A damping above 0.49 of AUDIO_RATE, which leaves the repeats undamped */
#define DL_BRIGHT 24000.0f

/** Output of the last render */
static float dl_l[DL_MAX];
static float dl_r[DL_MAX];

/** Output of a second render that a case compares */
static float dl_b_l[AUDIO_RATE * 2];
static float dl_b_r[AUDIO_RATE * 2];

/** The click: silence, then one sample of DL_LEVEL */
static float dl_click[DL_CLICK + 1];

/** A core with a delay on bus 1, which outputs to the master */
typedef struct dl_t {
    box_audio_core_t *core;  /**< The core */
    float *memory;           /**< Memory of slot 0 */
} dl_t;

/** The parameters of a delay.
 *
 * @param time Seconds
 * @param feedback Share of a repeat in the next
 * @param damping Hz
 * @param ping_pong 1 for ping pong
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_delay_params_t dl_params(
    float time,
    float feedback,
    float damping,
    float ping_pong,
    float mix)
{
    return (box_audio_delay_params_t){ time, feedback, damping, ping_pong, mix };
}

/** Send an effect command of a delay in insert slot 0 of a bus.
 *
 * @param core The core
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take the delay off
 * @param p Parameters
 */
static void dl_command(
    box_audio_core_t *core,
    uint32_t bus,
    box_audio_delay_params_t p)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_DELAY, bus, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with bus 1 into the master, the delay on bus 1 and the click as sound 1.
 *
 * @param dl Receives the core
 * @param p Parameters of the delay
 */
static void dl_open(
    dl_t *dl,
    box_audio_delay_params_t p)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = {
        AUDIO_OUT(1, 0), AUDIO_MASTER
    };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_DELAY, AUDIO_RATE);
    test_assert(floats >= (int32_t)(BOX_AUDIO_DELAY_MAX * AUDIO_RATE) * 2);
    dl->core = box_audio_core_new(&desc);
    test_assert(dl->core != NULL);
    dl->memory = malloc((size_t)floats * sizeof(float));
    test_assert(dl->memory != NULL);

    // Memory that is not clear: the delay never reads what it did not write
    audio_fill(dl->memory, floats, 0.75f);
    test_assert(box_audio_core_effect_memory(dl->core, 0, dl->memory, floats));
    audio_order(dl->core, routes, 2);
    dl_command(dl->core, 1, p);
    memset(dl_click, 0, sizeof(dl_click));
    dl_click[DL_CLICK] = DL_LEVEL;
    test_assert(box_audio_core_sound(dl->core, 1, dl_click, DL_CLICK + 1, 1, AUDIO_RATE));
}

/** Free the core, then its memory.
 *
 * @param dl The core
 */
static void dl_close(
    dl_t *dl)
{
    box_audio_core_free(dl->core);
    free(dl->memory);
}

/** Render frames into dl_l and dl_r from a frame on, in quanta of the web.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 */
static void dl_render(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames)
{
    int32_t i;
    test_assert(at + frames <= DL_MAX);
    for (i = 0; i < frames; i += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - i < BOX_AUDIO_CORE_QUANTUM ? frames - i : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, dl_l + at + i, dl_r + at + i, n);
    }
}

/** First frame of a range of a channel above DL_THRESHOLD.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 for none
 */
static int32_t dl_first(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t i, result = -1;
    for (i = from; i < to && result < 0; i ++) {
        if (fabsf(x[i]) > DL_THRESHOLD) {
            result = i;
        }
    }

    return result;
}

/** Sum of a range of a channel.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The sum
 */
static double dl_sum(
    const float *x,
    int32_t from,
    int32_t to)
{
    double result = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        result += x[i];
    }

    return result;
}

/** Render the click through a delay of a time at a mix of 0.5 and measure the frames from the
 * dry click to the first sample of its echo.
 *
 * @param time Seconds
 * @param frac Receives the share of the echo in its first frame
 * @return Frames from the click to the echo
 */
static int32_t dl_echo(
    float time,
    double *frac)
{
    dl_t dl;
    int32_t click, echo, frames = (int32_t)(time * AUDIO_RATE) + AUDIO_RATE / 4;
    dl_open(&dl, dl_params(time, 0, DL_BRIGHT, 0, 0.5f));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, frames);
    click = dl_first(dl_l, 0, frames);
    test_assert(click >= DL_CLICK);
    echo = dl_first(dl_l, click + 1, frames);
    test_assert(echo > click);
    *frac = dl_l[echo] / (dl_l[echo] + dl_l[echo + 1]);
    test_flt(dl_l[echo] + dl_l[echo + 1], dl_l[click]);
    dl_close(&dl);

    return echo - click;
}

void Delay_echo_time(void) {
    // audio/delay
    double frac;
    int32_t frames = dl_echo(0.25f, &frac);
    printf("echo of 0.25 s: %d frames\n", frames);
    test_int(frames, 12000);
    test_assert(frac > 0.9999);
    frames = dl_echo(0.1f, &frac);
    test_int(frames, (int32_t)(0.1f * AUDIO_RATE));

    // 5923.2 frames: 0.8 of the echo in its first frame, 0.2 in the next
    frames = dl_echo(5923.2f / AUDIO_RATE, &frac);
    printf("echo of 5923.2 frames: %d frames, %.4f in the first\n", frames, frac);
    test_int(frames, 5923);
    test_assert(fabs(frac - 0.8) < 0.002);
}

/** Render the click through a delay of 0.1 s and measure each repeat: its sum and its peak.
 *
 * @param damping Hz
 * @param sums Receives the sum of the left channel of 5 repeats
 * @param peaks Receives the peak of the left channel of 5 repeats
 */
static void dl_repeats(
    float damping,
    double *sums,
    float *peaks)
{
    dl_t dl;
    int32_t n, click, step = AUDIO_RATE / 10;
    dl_open(&dl, dl_params(0.1f, 0.5f, damping, 0, 1));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, AUDIO_RATE);
    click = dl_first(dl_l, 0, AUDIO_RATE);
    test_assert(click >= DL_CLICK);
    click -= step;
    for (n = 0; n < 5; n ++) {
        int32_t from = click + (n + 1) * step - 4, to = from + step;
        sums[n] = dl_sum(dl_l, from, to);
        peaks[n] = audio_peak(dl_l, from, to);
    }

    dl_close(&dl);
}

void Delay_feedback_damping(void) {
    // audio/delay
    double sums[5], dark_sums[5];
    float peaks[5], dark_peaks[5];
    int32_t n;
    dl_repeats(DL_BRIGHT, sums, peaks);
    dl_repeats(2000, dark_sums, dark_peaks);
    test_assert(peaks[0] > 0.1f);
    test_flt(dark_peaks[0], peaks[0]);
    for (n = 1; n < 5; n ++) {
        printf("repeat %d: undamped peak %.5f, damped peak %.5f sum %.5f\n", n,
            (double)(peaks[n] / peaks[n - 1]), (double)(dark_peaks[n] / dark_peaks[n - 1]),
            dark_sums[n] / dark_sums[n - 1]);

        // Undamped, each repeat is the last at the feedback
        test_assert(fabs(peaks[n] / peaks[n - 1] - 0.5) < 1e-4);
        test_assert(fabs(sums[n] / sums[n - 1] - 0.5) < 1e-4);

        // Damped, a lowpass passes the sum of a repeat but lowers and widens its peak
        test_assert(fabs(dark_sums[n] / dark_sums[n - 1] - 0.5) < 0.005);
        test_assert(dark_peaks[n] / dark_peaks[n - 1] < 0.45f);
    }
}

void Delay_ping_pong(void) {
    // audio/delay
    dl_t dl;
    int32_t n, click, step = AUDIO_RATE / 10;
    float left[4], right[4];
    dl_open(&dl, dl_params(0.1f, 0.5f, DL_BRIGHT, 1, 1));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, AUDIO_RATE);
    click = dl_first(dl_l, 0, AUDIO_RATE) - step;
    for (n = 0; n < 4; n ++) {
        int32_t from = click + (n + 1) * step - 4, to = from + step;
        left[n] = audio_peak(dl_l, from, to);
        right[n] = audio_peak(dl_r, from, to);
        printf("repeat %d: left %.5f right %.5f\n", n, (double)left[n], (double)right[n]);
    }

    test_assert(left[0] > 0.1f);
    test_assert(right[0] < DL_THRESHOLD);
    test_assert(left[1] < DL_THRESHOLD);
    test_assert(fabsf(right[1] / left[0] - 0.5f) < 1e-4f);
    test_assert(fabsf(left[2] / right[1] - 0.5f) < 1e-4f);
    test_assert(right[2] < DL_THRESHOLD);
    test_assert(left[3] < DL_THRESHOLD);
    test_assert(fabsf(right[3] / left[2] - 0.5f) < 1e-4f);
    dl_close(&dl);
}

/** Get the time a Delay entity sends.
 *
 * @param world The world
 * @param e The delay entity
 * @return Seconds of params[0] of its command
 */
static float dl_sent_time(
    ecs_world_t *world,
    ecs_entity_t e)
{
    float params[BOX_AUDIO_EFFECT_PARAMS];
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_DELAY);

    return params[0];
}

/** Set the tempo of the Transport.
 *
 * @param world The world
 * @param tempo Quarter notes per minute
 */
static void dl_tempo(
    ecs_world_t *world,
    float tempo)
{
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->tempo = tempo;
    ecs_singleton_modified(world, Transport);
}

/** Play the click on a bus of a world and measure the frames from the dry click to its echo.
 *
 * @param world The world
 * @param bus The bus, with a delay at a mix of 0.5
 * @param sound The click
 * @return Frames from the click to the echo, -1 for none
 */
static int32_t dl_world_echo(
    ecs_world_t *world,
    ecs_entity_t bus,
    ecs_entity_t sound)
{
    int32_t start = audio_tap_count, click, echo = -1;
    ecs_entity_t source = audio_source(world, sound, 1, false, bus);
    audio_run(world, 60);
    click = dl_first(audio_tap_l, start, audio_tap_count);
    if (click >= 0) {
        echo = dl_first(audio_tap_l, click + 2, audio_tap_count);
    }

    ecs_delete(world, source);

    return echo >= 0 ? echo - click : -1;
}

void Delay_sync_eighth(void) {
    // audio/delay
    ecs_world_t *world = audio_world();
    ecs_entity_t echo = audio_bus(world, "echo", 0, 1);
    ecs_entity_t click;
    ecs_set(world, echo, Delay, { .time = 0.1f, .sync = 0.5f, .feedback = 0, .mix = 0.5f,
        .damping = DL_BRIGHT });
    audio_insert(world, echo, 0, echo);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, echo), 0);
    test_flt(ecs_singleton_get(world, Transport)->tempo, 120);
    test_flt(dl_sent_time(world, echo), 0.25f);

    memset(dl_click, 0, sizeof(dl_click));
    dl_click[DL_CLICK] = DL_LEVEL;
    click = box_audio_sound_from_pcm(world, 0, dl_click, DL_CLICK + 1, 1, AUDIO_RATE);
    test_int(dl_world_echo(world, echo, click), 12000);

    // A dotted eighth
    ecs_set(world, echo, Delay, { .time = 0.1f, .sync = 0.75f, .feedback = 0, .mix = 0.5f,
        .damping = DL_BRIGHT });
    test_flt(dl_sent_time(world, echo), 0.375f);

    // Without sync the time counts
    ecs_set(world, echo, Delay, { .time = 0.1f, .sync = 0, .feedback = 0, .mix = 0.5f,
        .damping = DL_BRIGHT });
    test_flt(dl_sent_time(world, echo), 0.1f);

    // Without a Transport the tempo is 120
    ecs_set(world, echo, Delay, { .time = 0.1f, .sync = 0.5f, .feedback = 0, .mix = 0.5f,
        .damping = DL_BRIGHT });
    dl_tempo(world, 60);
    test_flt(dl_sent_time(world, echo), 0.5f);
    ecs_singleton_remove(world, Transport);
    test_flt(dl_sent_time(world, echo), 0.25f);
    ecs_fini(world);
}

void Delay_tempo_change(void) {
    // audio/delay
    ecs_world_t *world = audio_world();
    ecs_entity_t echo = audio_bus(world, "echo", 0, 1);
    ecs_entity_t tone = audio_tone(world, "tone", 440, 0.25f, AUDIO_RATE);
    ecs_entity_t click, source;
    int32_t at, i;
    float before, after;
    ecs_set(world, echo, Delay, { .sync = 0.5f, .feedback = 0.3f, .mix = 1,
        .damping = DL_BRIGHT });
    audio_insert(world, echo, 0, echo);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, echo), 0);

    // A tone through the delay, then a tempo change while it rings
    source = audio_source(world, tone, 1, true, echo);
    audio_run(world, 60);
    at = audio_tap_count;
    before = audio_max_step(audio_tap_l, at - AUDIO_FRAME * 20, at);
    test_assert(audio_peak(audio_tap_l, at - AUDIO_FRAME, at) > 0.05f);
    dl_tempo(world, 100);
    audio_run(world, 30);
    test_flt(dl_sent_time(world, echo), 0.3f);
    after = audio_max_step(audio_tap_l, at, audio_tap_count);
    printf("largest step: %.4f before, %.4f after the tempo change\n", (double)before,
        (double)after);
    test_assert(after < before * 1.5f);
    test_assert(audio_max_step(audio_tap_r, at, audio_tap_count) < before * 1.5f);

    // The crossfade: the output never falls silent while it moves
    for (i = at; i < at + (int32_t)(BOX_AUDIO_DELAY_CROSSFADE * AUDIO_RATE) * 3;
        i += AUDIO_RATE / 440 * 2)
    {
        test_assert(audio_peak(audio_tap_l, i, i + AUDIO_RATE / 440 * 2) > 0.01f);
    }

    // The echo of a click moved to 0.3 s
    ecs_delete(world, source);
    ecs_set(world, echo, Delay, { .sync = 0.5f, .feedback = 0, .mix = 0.5f,
        .damping = DL_BRIGHT });
    audio_run(world, 120);
    memset(dl_click, 0, sizeof(dl_click));
    dl_click[DL_CLICK] = DL_LEVEL;
    click = box_audio_sound_from_pcm(world, 0, dl_click, DL_CLICK + 1, 1, AUDIO_RATE);
    test_int(dl_world_echo(world, echo, click), 14400);
    dl_tempo(world, 120);
    audio_run(world, 30);
    test_int(dl_world_echo(world, echo, click), 12000);
    ecs_fini(world);
}

void Delay_time_change_crossfade(void) {
    // audio/delay
    dl_t dl;
    int32_t at = AUDIO_RATE / 2, fade = (int32_t)(BOX_AUDIO_DELAY_CROSSFADE * AUDIO_RATE);
    int32_t i;
    float before, after;
    audio_sine(audio_sound_a, AUDIO_RATE, 440, 0.25f);
    dl_open(&dl, dl_params(0.2f, 0, DL_BRIGHT, 0, 1));
    test_assert(box_audio_core_sound(dl.core, 2, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(dl.core, 2, 2, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    dl_render(dl.core, 0, at);
    before = audio_max_step(dl_l, at / 2, at);
    dl_command(dl.core, 1, dl_params(0.2011f, 0, DL_BRIGHT, 0, 1));
    dl_render(dl.core, at, AUDIO_RATE);
    after = audio_max_step(dl_l, at, at + AUDIO_RATE);
    printf("largest step: %.5f before, %.5f after a change of the time\n", (double)before,
        (double)after);
    test_assert(after < before * 1.2f);

    // No glide: after the crossfade the tone has its pitch, and it is there midway
    test_assert(fabs(audio_frequency(dl_l, at + fade + 100, at + AUDIO_RATE) - 440) < 1);
    for (i = at; i < at + fade; i += 200) {
        test_assert(audio_peak(dl_l, i, i + 200) > 0.0f);
    }

    dl_close(&dl);
}

void Delay_time_clamped(void) {
    // audio/delay
    dl_t dl;
    ecs_world_t *world;
    ecs_entity_t e;
    int32_t click, echo, frames = (int32_t)(BOX_AUDIO_DELAY_MAX * AUDIO_RATE) + AUDIO_RATE / 2;
    dl_open(&dl, dl_params(10, 0, DL_BRIGHT, 0, 0.5f));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, frames);
    click = dl_first(dl_l, 0, frames);
    echo = dl_first(dl_l, click + 2, frames);
    printf("echo of a time of 10 s: %d frames\n", echo - click);
    test_int(echo - click, (int32_t)(BOX_AUDIO_DELAY_MAX * AUDIO_RATE));
    dl_close(&dl);

    // NaN is the low end, an echo a frame later
    dl_open(&dl, dl_params(NAN, NAN, NAN, NAN, 0.5f));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, AUDIO_RATE / 10);
    click = dl_first(dl_l, 0, AUDIO_RATE / 10);
    test_assert(click >= 0);
    test_flt(dl_l[click + 1], dl_l[click]);
    test_int(dl_first(dl_l, click + 2, AUDIO_RATE / 10), -1);
    dl_close(&dl);

    world = audio_world();
    e = ecs_new(world);
    ecs_set(world, e, Delay, { .time = 10, .feedback = 0.35f, .mix = 0.3f, .damping = 6000 });
    test_flt(dl_sent_time(world, e), BOX_AUDIO_DELAY_MAX);
    ecs_set(world, e, Delay, { .time = 0.1f, .sync = 64, .feedback = 0.35f, .mix = 0.3f,
        .damping = 6000 });
    test_flt(dl_sent_time(world, e), BOX_AUDIO_DELAY_MAX);
    ecs_fini(world);
}

void Delay_silent_after_tail(void) {
    // audio/delay
    dl_t dl;
    int32_t i, second, click, echo;
    dl_open(&dl, dl_params(0.1f, 0.6f, 6000, 1, 1));
    test_assert(!box_audio_core_effect_running(dl.core, 0));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, AUDIO_RATE / 2);
    test_assert(box_audio_core_effect_running(dl.core, 0));
    test_assert(audio_peak(dl_l, 0, AUDIO_RATE / 2) > 0.01f);
    for (second = 0; second < 4; second ++) {
        dl_render(dl.core, 0, AUDIO_RATE);
    }

    test_assert(!box_audio_core_effect_running(dl.core, 0));
    for (i = 0; i < AUDIO_RATE; i ++) {
        test_flt(dl_l[i], 0);
        test_flt(dl_r[i], 0);
    }

    // It starts again on the next sound, with nothing of the old tail in its echo
    dl_command(dl.core, 1, dl_params(0.1f, 0, 6000, 0, 0.5f));
    audio_play(dl.core, 2, 1, 1, 1, 0, 1, 0);
    dl_render(dl.core, 0, AUDIO_RATE / 20);
    test_assert(box_audio_core_effect_running(dl.core, 0));
    dl_render(dl.core, AUDIO_RATE / 20, AUDIO_RATE / 2 - AUDIO_RATE / 20);
    test_assert(!box_audio_core_effect_running(dl.core, 0));
    click = dl_first(dl_l, 0, AUDIO_RATE / 2);
    echo = dl_first(dl_l, click + 2, AUDIO_RATE / 2);
    test_int(echo - click, AUDIO_RATE / 10);
    test_int(dl_first(dl_l, echo + 2, AUDIO_RATE / 2), -1);
    dl_close(&dl);
}

/** Render a looping sine on bus 1 of a core, with a delay on bus 1 or none.
 *
 * @param delay Whether bus 1 has the delay
 * @param p Parameters of the delay
 * @param l Receives the left channel of two seconds
 * @param r Receives the right channel
 */
static void dl_tone(
    bool delay,
    box_audio_delay_params_t p,
    float *l,
    float *r)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_DELAY, AUDIO_RATE), i;
    box_audio_core_t *core = box_audio_core_new(&desc);
    float *memory = calloc((size_t)floats, sizeof(float));
    test_assert(core != NULL);
    audio_order(core, routes, 2);
    if (delay) {
        test_assert(box_audio_core_effect_memory(core, 0, memory, floats));
        dl_command(core, 1, p);
    }

    audio_sine(audio_sound_a, AUDIO_RATE, 330, 0.25f);
    test_assert(box_audio_core_sound(core, 2, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 2, 2, 1, 1, 0.3f, 1, BOX_AUDIO_PLAY_LOOP);
    for (i = 0; i < AUDIO_RATE * 2; i += BOX_AUDIO_CORE_QUANTUM) {
        box_audio_core_render(core, l + i, r + i, BOX_AUDIO_CORE_QUANTUM);
    }

    box_audio_core_free(core);
    free(memory);
}

void Delay_mix_dry(void) {
    // audio/delay
    int32_t i;
    dl_tone(false, dl_params(0, 0, 0, 0, 0), dl_l, dl_r);
    dl_tone(true, dl_params(0.1f, 0.8f, 3000, 1, 0), dl_b_l, dl_b_r);
    test_assert(audio_peak(dl_l, 0, AUDIO_RATE * 2) > 0.1f);
    for (i = 0; i < AUDIO_RATE * 2; i ++) {
        test_assert(dl_l[i] == dl_b_l[i]);
        test_assert(dl_r[i] == dl_b_r[i]);
    }
}

/** Render a click and a tone through a delay whose time and parameters change, in renders
 * of a size, and hash the output.
 *
 * @param size Frames per render
 * @return The hash of both channels
 */
static uint64_t dl_sized(
    int32_t size)
{
    dl_t dl;
    int32_t i, k;
    audio_sine(audio_sound_a, AUDIO_RATE, 220, 0.2f);
    dl_open(&dl, dl_params(0.13f, 0.6f, 4000, 0, 0.5f));
    test_assert(box_audio_core_sound(dl.core, 2, audio_sound_a, AUDIO_RATE / 4, 1,
        AUDIO_RATE));
    audio_play(dl.core, 1, 1, 1, 1, 0, 1, 0);
    audio_play(dl.core, 2, 2, 1, 1, -0.5f, 1, 0);
    for (k = 0; k < 4; k ++) {
        for (i = 0; i < AUDIO_RATE / 4; i += size) {
            int32_t at = k * AUDIO_RATE / 4 + i;
            int32_t n = AUDIO_RATE / 4 - i < size ? AUDIO_RATE / 4 - i : size;
            box_audio_core_render(dl.core, dl_l + at, dl_r + at, n);
        }

        dl_command(dl.core, 1, dl_params(0.13f + 0.05f * (float)k, 0.6f, 4000,
            (float)(k % 2), 0.5f));
    }

    dl_close(&dl);

    return audio_hash(dl_l, AUDIO_RATE) ^ (audio_hash(dl_r, AUDIO_RATE) * 31);
}

void Delay_render_sizes(void) {
    // audio/delay
    uint64_t a = dl_sized(BOX_AUDIO_CORE_QUANTUM), b = dl_sized(100), c = dl_sized(1000);
    test_assert(a == b);
    test_assert(a == c);
}

void Delay_component(void) {
    // audio/delay
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const Delay *d;
    float params[BOX_AUDIO_EFFECT_PARAMS];
    ecs_add(world, e, Delay);
    d = ecs_get(world, e, Delay);
    test_flt(d->time, 0.25f);
    test_flt(d->sync, 0);
    test_flt(d->feedback, 0.35f);
    test_flt(d->mix, 0.3f);
    test_bool(d->ping_pong, false);
    test_flt(d->damping, 6000);
    test_assert(ecs_has(world, e, AudioEffectState));
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_DELAY);
    test_flt(params[0], 0.25f);
    test_flt(params[1], 0.35f);
    test_flt(params[2], 6000);
    test_flt(params[3], 0);
    test_flt(params[4], 0.3f);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_DELAY, AUDIO_RATE),
        ((int32_t)(BOX_AUDIO_DELAY_MAX * AUDIO_RATE) + 2) * 2);
    ecs_fini(world);
}

void Delay_script(void) {
    // audio/delay
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/delay.flecs");
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "delay_scene" }),
        .code = code
    });
    ecs_entity_t echo;
    const Delay *d;
    ecs_os_free(code);
    test_assert(script != 0);
    audio_run(world, 2);
    echo = ecs_lookup(world, "echo");
    test_assert(echo != 0);
    d = ecs_get(world, echo, Delay);
    test_assert(d != NULL);
    test_flt(d->sync, 0.75f);
    test_flt(d->feedback, 0.4f);
    test_flt(d->mix, 1);
    test_bool(d->ping_pong, true);
    test_flt(d->damping, 4000);
    test_int(box_audio_module_effect_slot(world, echo), 0);
    test_flt(dl_sent_time(world, echo), 0.375f);
    ecs_fini(world);
}
