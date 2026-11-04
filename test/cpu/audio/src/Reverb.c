/**
 * @file Reverb.c
 * @brief The reverb kind of effect in an insert slot of a bus, from an impulse in an offline
 * render: its decay, damping, predelay, stereo image, density and stability, its silence, its
 * changes and its routing.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, eight seconds at 48 kHz */
#define RV_MAX (48000 * 8)

/** Frame of the click in its sound, after the start ramp of the voice */
#define RV_CLICK 300

/** Output of the last render */
static float rv_l[RV_MAX];
static float rv_r[RV_MAX];

/** The click: silence, then one sample of 1 */
static float rv_click[RV_CLICK + 1];

/** A noise of the stability case */
static float rv_noise[48000];

/** A core with a reverb on bus 1, which outputs to the master */
typedef struct rv_t {
    box_audio_core_t *core;  /**< The core */
    float *memory;           /**< Delay memory of slot 0 */
    float rate;              /**< Frames per second */
} rv_t;

/** Calls of the counting allocator */
static int32_t rv_allocs;

/** Allocator that counts its calls.
 *
 * @param size Bytes
 * @param ctx Unused
 * @return The memory
 */
static void* rv_alloc(
    size_t size,
    void *ctx)
{
    (void)ctx;
    rv_allocs ++;

    return malloc(size);
}

/** Free of the counting allocator.
 *
 * @param ptr The memory
 * @param ctx Unused
 */
static void rv_free(
    void *ptr,
    void *ctx)
{
    (void)ctx;
    free(ptr);
}

/** The parameters of a reverb.
 *
 * @param decay Seconds until -60 dB
 * @param damping High frequency loss
 * @param predelay Seconds
 * @param size Room scale
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_reverb_params_t rv_params(
    float decay,
    float damping,
    float predelay,
    float size,
    float mix)
{
    return (box_audio_reverb_params_t){ decay, damping, predelay, size, mix };
}

/** Send an effect command of a reverb in insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take the reverb off
 * @param p Parameters
 */
static void rv_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t bus,
    box_audio_reverb_params_t p)
{
    uint8_t bytes[128];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, BOX_AUDIO_EFFECT_REVERB, bus, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with bus 1 into the master, the reverb on bus 1 and the click as sound 1.
 *
 * @param rv Receives the core
 * @param rate Frames per second
 * @param p Parameters of the reverb
 * @param counted Whether the core allocates through the counting allocator
 */
static void rv_open(
    rv_t *rv,
    float rate,
    box_audio_reverb_params_t p,
    bool counted)
{
    box_audio_core_desc_t desc = { .sample_rate = rate };
    box_audio_route_t routes[] = {
        AUDIO_OUT(1, 0), AUDIO_MASTER
    };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_REVERB, rate);
    if (counted) {
        desc.alloc = rv_alloc;
        desc.free = rv_free;
        rv_allocs = 0;
    }

    rv->rate = rate;
    rv->core = box_audio_core_new(&desc);
    test_assert(rv->core != NULL);
    rv->memory = calloc((size_t)floats, sizeof(float));
    test_assert(rv->memory != NULL);
    test_assert(box_audio_core_effect_memory(rv->core, 0, rv->memory, floats));
    test_assert(!box_audio_core_effect_memory(rv->core, 0, rv->memory, floats));
    audio_order(rv->core, routes, 2);
    rv_command(rv->core, 0, 1, p);
    memset(rv_click, 0, sizeof(rv_click));
    rv_click[RV_CLICK] = 1;
    test_assert(box_audio_core_sound(rv->core, 1, rv_click, RV_CLICK + 1, 1, rate));
}

/** Free the core, then its delay memory.
 *
 * @param rv The core
 */
static void rv_close(
    rv_t *rv)
{
    box_audio_core_free(rv->core);
    free(rv->memory);
}

/** Render frames into rv_l and rv_r from a frame on, in quanta of the web.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 */
static void rv_render(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames)
{
    int32_t i;
    test_assert(at + frames <= RV_MAX);
    for (i = 0; i < frames; i += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - i < BOX_AUDIO_CORE_QUANTUM ? frames - i : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, rv_l + at + i, rv_r + at + i, n);
    }
}

/** Measure the decay time to -60 dB from the energy decay curve of the output, by the slope
 * between -5 and -25 dB.
 *
 * @param from First frame of the response
 * @param to End frame
 * @param rate Frames per second
 * @return Seconds
 */
static double rv_rt60(
    int32_t from,
    int32_t to,
    float rate)
{
    double *edc = malloc((size_t)(to - from) * sizeof(double)), sum = 0, total;
    int32_t i, at5 = -1, at25 = -1;
    for (i = to - 1; i >= from; i --) {
        sum += (double)rv_l[i] * rv_l[i] + (double)rv_r[i] * rv_r[i];
        edc[i - from] = sum;
    }

    total = edc[0];
    for (i = 0; i < to - from && at25 < 0; i ++) {
        double db = 10 * log10(edc[i] / total);
        if (at5 < 0 && db <= -5) {
            at5 = i;
        }

        if (db <= -25) {
            at25 = i;
        }
    }

    free(edc);
    test_assert(at5 >= 0 && at25 > at5);

    return 3.0 * (double)(at25 - at5) / (double)rate;
}

/** Energy of a range of both channels.
 *
 * @param from First frame
 * @param to End frame
 * @return The energy
 */
static double rv_energy(
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)rv_l[i] * rv_l[i] + (double)rv_r[i] * rv_r[i];
    }

    return sum;
}

/** Energy of the first difference of a range of both channels, the high frequencies.
 *
 * @param from First frame, above 0
 * @param to End frame
 * @return The energy
 */
static double rv_energy_high(
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        double dl = (double)rv_l[i] - rv_l[i - 1], dr = (double)rv_r[i] - rv_r[i - 1];
        sum += dl * dl + dr * dr;
    }

    return sum;
}

/** Render the response of a reverb to the click and measure its decay time.
 *
 * @param rate Frames per second
 * @param decay Decay of the reverb
 */
static void rv_decay(
    float rate,
    float decay)
{
    rv_t rv;
    int32_t frames = (int32_t)((decay * 1.6f + 0.5f) * rate);
    double rt;
    rv_open(&rv, rate, rv_params(decay, 0, 0.01f, 0.5f, 1), false);
    audio_play(rv.core, 1, 1, 1, 1, 0, 1, 0);
    rv_render(rv.core, 0, frames);
    rt = rv_rt60(RV_CLICK, frames, rate);
    printf("decay %.2f s at %.0f Hz: measured %.3f s\n", (double)decay, (double)rate, rt);
    test_assert(fabs(rt - decay) < 0.15 * decay);
    rv_close(&rv);
}

void Reverb_decay_time(void) {
    // audio/reverb
    rv_decay(48000, 0.5f);
    rv_decay(48000, 1.5f);
    rv_decay(48000, 3.0f);
    rv_decay(44100, 0.5f);
    rv_decay(44100, 1.5f);
    rv_decay(44100, 3.0f);
}

/** Share of the high frequencies in the late tail of the click at a damping.
 *
 * @param damping The damping
 * @return High frequency energy over the energy of 0.3 to 0.6 seconds
 */
static double rv_brightness(
    float damping)
{
    rv_t rv;
    double result;
    rv_open(&rv, AUDIO_RATE, rv_params(2, damping, 0.01f, 0.5f, 1), false);
    audio_play(rv.core, 1, 1, 1, 1, 0, 1, 0);
    rv_render(rv.core, 0, AUDIO_RATE);
    result = rv_energy_high(AUDIO_RATE * 3 / 10, AUDIO_RATE * 6 / 10) /
        rv_energy(AUDIO_RATE * 3 / 10, AUDIO_RATE * 6 / 10);
    rv_close(&rv);

    return result;
}

void Reverb_damping(void) {
    // audio/reverb
    double bright = rv_brightness(0.1f), mid = rv_brightness(0.5f), dark = rv_brightness(0.9f);
    printf("high frequency share: damping 0.1 %.4f, 0.5 %.4f, 0.9 %.4f\n", bright, mid, dark);
    test_assert(mid < bright);
    test_assert(dark < mid);
    test_assert(dark < 0.5 * bright);
}

/** Frame of the first output above -120 dB.
 *
 * @param frames Frames rendered
 * @return The frame, -1 for none
 */
static int32_t rv_first(
    int32_t frames)
{
    int32_t i, result = -1;
    for (i = 0; i < frames && result < 0; i ++) {
        if (fabsf(rv_l[i]) > 1e-6f || fabsf(rv_r[i]) > 1e-6f) {
            result = i;
        }
    }

    return result;
}

void Reverb_predelay(void) {
    // audio/reverb
    float predelays[] = { 0, 0.05f, 0.2f };
    int32_t k;
    for (k = 0; k < 3; k ++) {
        rv_t rv;
        int32_t first, pre = (int32_t)(predelays[k] * AUDIO_RATE);
        rv_open(&rv, AUDIO_RATE, rv_params(1, 0.5f, predelays[k], 0.5f, 1), false);
        audio_play(rv.core, 1, 1, 1, 1, 0, 1, 0);
        rv_render(rv.core, 0, AUDIO_RATE / 2);
        first = rv_first(AUDIO_RATE / 2);
        printf("predelay %.2f s: first output %d frames after the click\n",
            (double)predelays[k], first - RV_CLICK);
        test_assert(first >= RV_CLICK + pre);
        test_assert(first < RV_CLICK + pre + AUDIO_RATE / 20);
        rv_close(&rv);
    }
}

void Reverb_stereo(void) {
    // audio/reverb
    rv_t rv;
    double lr = 0, ll = 0, rr = 0, correlation;
    int32_t i;
    rv_open(&rv, AUDIO_RATE, rv_params(1.5f, 0.5f, 0.02f, 0.5f, 1), false);
    audio_play(rv.core, 1, 1, 1, 1, 0, 1, 0);
    rv_render(rv.core, 0, AUDIO_RATE);
    for (i = AUDIO_RATE / 10; i < AUDIO_RATE; i ++) {
        lr += (double)rv_l[i] * rv_r[i];
        ll += (double)rv_l[i] * rv_l[i];
        rr += (double)rv_r[i] * rv_r[i];
    }

    correlation = lr / sqrt(ll * rr);
    printf("correlation of the channels: %.4f\n", correlation);
    test_assert(ll > 0 && rr > 0);
    test_assert(fabs(correlation) < 0.3);
    rv_close(&rv);
}

void Reverb_density(void) {
    // audio/reverb
    rv_t rv;
    int32_t from = AUDIO_RATE / 5, to = AUDIO_RATE * 3 / 5, win = AUDIO_RATE / 500, i, k;
    int32_t gaps = 0, windows = 0, lag, worst_lag = 0;
    double worst = 0;
    rv_open(&rv, AUDIO_RATE, rv_params(2, 0.3f, 0.01f, 0.5f, 1), false);
    audio_play(rv.core, 1, 1, 1, 1, 0, 1, 0);
    rv_render(rv.core, 0, AUDIO_RATE);
    for (i = from; i + win <= to; i += win) {
        double local = rv_energy(i - AUDIO_RATE / 40, i + AUDIO_RATE / 40) /
            (AUDIO_RATE / 20), here = rv_energy(i, i + win) / win;
        gaps += here < 0.01 * local;
        windows ++;
    }

    for (lag = AUDIO_RATE / 1000; lag < AUDIO_RATE / 10; lag ++) {
        double xy = 0, xx = 0, yy = 0;
        for (k = from; k < to; k ++) {
            xy += (double)rv_l[k] * rv_l[k + lag];
            xx += (double)rv_l[k] * rv_l[k];
            yy += (double)rv_l[k + lag] * rv_l[k + lag];
        }

        if (fabs(xy) / sqrt(xx * yy) > worst) {
            worst = fabs(xy) / sqrt(xx * yy);
            worst_lag = lag;
        }
    }

    printf("density: %d of %d windows of 2 ms are gaps, largest echo correlation %.4f at %d "
        "frames\n", gaps, windows, worst, worst_lag);
    test_int(gaps, 0);
    test_assert(worst < 0.3);
    rv_close(&rv);
}

void Reverb_stable(void) {
    // audio/reverb
    box_audio_reverb_params_t extremes[] = {
        { 1e30f, 1e30f, 1e30f, 1e30f, 1e30f },
        { 60, 0, 0.5f, 1, 1 },
        { -1e30f, -1e30f, -1e30f, -1e30f, -1e30f },
        { NAN, NAN, NAN, NAN, NAN },
        { INFINITY, -INFINITY, INFINITY, -INFINITY, INFINITY },
        { 1e9f, 0, 0, 0, 1 }
    };
    rv_t rv;
    int32_t i, block, frames = AUDIO_RATE / 2;
    float peak = 0;
    srand(7);
    for (i = 0; i < 48000; i ++) {
        rv_noise[i] = (float)rand() / (float)RAND_MAX * 2 - 1;
    }

    rv_open(&rv, AUDIO_RATE, extremes[0], false);
    test_assert(box_audio_core_sound(rv.core, 2, rv_noise, 48000, 1, AUDIO_RATE));
    audio_bus_set(rv.core, 1, BOX_AUDIO_PARAM_VOLUME, 0.001f);
    audio_play(rv.core, 2, 2, 1, BOX_AUDIO_VOLUME_MAX, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (block = 0; block < 32; block ++) {
        rv_command(rv.core, 0, 1, extremes[block % 6]);
        rv_render(rv.core, 0, frames);
        for (i = 0; i < frames; i ++) {
            test_assert(isfinite(rv_l[i]) && isfinite(rv_r[i]));
            peak = fabsf(rv_l[i]) > peak ? fabsf(rv_l[i]) : peak;
            peak = fabsf(rv_r[i]) > peak ? fabsf(rv_r[i]) : peak;
        }
    }

    rv_command(rv.core, 0, 1, extremes[1]);
    for (block = 0; block < 8; block ++) {
        rv_render(rv.core, 0, frames);
        for (i = 0; i < frames; i ++) {
            test_assert(isfinite(rv_l[i]) && isfinite(rv_r[i]));
            peak = fabsf(rv_l[i]) > peak ? fabsf(rv_l[i]) : peak;
        }
    }

    printf("peak of 20 seconds of noise at extreme values: %.4f\n", (double)peak);
    test_assert(peak > 0);
    test_assert(peak < BOX_AUDIO_LIMITER_THRESHOLD);
    rv_close(&rv);
}

void Reverb_silent_after_tail(void) {
    // audio/reverb
    rv_t rv;
    int32_t i, second;
    rv_open(&rv, AUDIO_RATE, rv_params(0.5f, 0.5f, 0.02f, 0.5f, 1), false);
    test_assert(!box_audio_core_effect_running(rv.core, 0));
    audio_play(rv.core, 1, 1, 1, 1, 0, 1, 0);
    rv_render(rv.core, 0, AUDIO_RATE / 2);
    test_assert(box_audio_core_effect_running(rv.core, 0));
    test_assert(audio_peak(rv_l, 0, AUDIO_RATE / 2) > 0.001f);
    for (second = 0; second < 4; second ++) {
        rv_render(rv.core, 0, AUDIO_RATE);
    }

    test_assert(!box_audio_core_effect_running(rv.core, 0));
    for (i = 0; i < AUDIO_RATE; i ++) {
        test_flt(rv_l[i], 0);
        test_flt(rv_r[i], 0);
    }

    audio_play(rv.core, 2, 1, 1, 1, 0, 1, 0);
    rv_render(rv.core, 0, AUDIO_RATE / 2);
    test_assert(box_audio_core_effect_running(rv.core, 0));
    test_assert(audio_peak(rv_l, 0, AUDIO_RATE / 2) > 0.001f);
    rv_close(&rv);
}

/** Largest change between neighbouring samples of both channels in a range.
 *
 * @param from First frame, above 0
 * @param to End frame
 * @return The change
 */
static float rv_step(
    int32_t from,
    int32_t to)
{
    float l = audio_max_step(rv_l, from, to), r = audio_max_step(rv_r, from, to);

    return l > r ? l : r;
}

/** Render a looping tone into a reverb, change it midway and compare the largest sample to
 * sample change after the change with the one before.
 *
 * @param before Parameters before
 * @param after Parameters after
 * @param after_step Receives the largest change after
 * @return The largest change before
 */
static float rv_change(
    box_audio_reverb_params_t before,
    box_audio_reverb_params_t after,
    float *after_step)
{
    rv_t rv;
    float result;
    rv_open(&rv, AUDIO_RATE, before, false);
    audio_sine(audio_sound_a, AUDIO_RATE, 440, 0.25f);
    test_assert(box_audio_core_sound(rv.core, 2, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(rv.core, 2, 2, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    rv_render(rv.core, 0, AUDIO_RATE);
    result = rv_step(AUDIO_RATE / 2, AUDIO_RATE);
    rv_command(rv.core, 0, 1, after);
    rv_render(rv.core, AUDIO_RATE, AUDIO_RATE);
    *after_step = rv_step(AUDIO_RATE, AUDIO_RATE * 2);
    test_assert(rv_energy(AUDIO_RATE * 3 / 2, AUDIO_RATE * 2) > 0);
    rv_close(&rv);

    return result;
}

void Reverb_live_change_no_click(void) {
    // audio/reverb
    float after, before = rv_change(rv_params(0.4f, 0, 0.02f, 0.5f, 1),
        rv_params(4, 1, 0.02f, 0.5f, 0.3f), &after);
    float dry = (float)(0.25 * 2 * AUDIO_PI * 440 / AUDIO_RATE);
    printf("largest step: %.4f before, %.4f after a change of decay, damping and mix\n",
        (double)before, (double)after);
    test_assert(after < 2 * before + dry);
}

void Reverb_size_change_no_click(void) {
    // audio/reverb
    float after, before = rv_change(rv_params(1.5f, 0.5f, 0.01f, 0.2f, 1),
        rv_params(1.5f, 0.5f, 0.15f, 0.9f, 1), &after);
    printf("largest step: %.4f before, %.4f after a change of size and predelay\n",
        (double)before, (double)after);
    test_assert(after < 2 * before);
}

/** Run a scene of a reverb bus and a bus that sends a click to it, and measure the energy of
 * the tail after the click.
 *
 * @param level Send level
 * @return Energy of the left channel of the tail
 */
static double rv_send(
    float level)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t dry = audio_bus(world, "dry", 0, 1);
    ecs_entity_t click;
    double result = 0;
    int32_t i, start;
    ecs_set(world, hall, Reverb, { .decay = 1, .damping = 0.5f, .predelay = 0.02f,
        .size = 0.5f, .mix = 1 });
    audio_insert(world, hall, 0, hall);
    audio_send(world, dry, hall, level, false);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, hall), 0);
    memset(rv_click, 0, sizeof(rv_click));
    rv_click[RV_CLICK] = 0.5f;
    click = box_audio_sound_from_pcm(world, 0, rv_click, RV_CLICK + 1, 1, AUDIO_RATE);
    start = audio_tap_count;
    audio_source(world, click, 1, false, dry);
    audio_run(world, 30);
    for (i = start + RV_CLICK + 1; i < audio_tap_count; i ++) {
        result += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    ecs_fini(world);

    return result;
}

void Reverb_send_routing(void) {
    // audio/reverb
    double none = rv_send(0), low = rv_send(0.3f), high = rv_send(0.6f);
    printf("tail energy: send 0 %g, 0.3 %g, 0.6 %g\n", none, low, high);
    test_assert(none == 0);
    test_assert(low > 0);
    test_assert(fabs(high / low - 4) < 0.01);
}

void Reverb_second_reverb_runs(void) {
    // audio/reverb
    ecs_world_t *world = audio_world();
    ecs_entity_t first = audio_bus(world, "first_room", 0, 1);
    ecs_entity_t second = audio_bus(world, "second_room", 0, 1);
    ecs_entity_t click;
    int32_t start;
    ecs_add(world, first, Reverb);
    ecs_add(world, second, Reverb);
    audio_insert(world, first, 0, first);
    audio_insert(world, second, 0, second);
    audio_log_start();
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(box_audio_module_effect_slot(world, first), 0);
    test_int(box_audio_module_effect_slot(world, second), 1);

    memset(rv_click, 0, sizeof(rv_click));
    rv_click[RV_CLICK] = 0.5f;
    click = box_audio_sound_from_pcm(world, 0, rv_click, RV_CLICK + 1, 1, AUDIO_RATE);
    start = audio_tap_count;
    audio_source(world, click, 1, false, second);
    audio_run(world, 20);
    test_assert(audio_peak(audio_tap_l, start + RV_CLICK + 1, audio_tap_count) > 0.0001f);

    ecs_delete(world, first);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, second), 1);
    ecs_fini(world);
}

void Reverb_delete_fades(void) {
    // audio/reverb
    ecs_world_t *world = audio_world();
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t tone = audio_tone(world, "tone", 440, 0.25f, AUDIO_RATE / 4);
    int32_t i, at, end;
    float before, after;
    ecs_set(world, hall, Reverb, { .decay = 3, .damping = 0.3f, .predelay = 0.01f,
        .size = 0.7f, .mix = 1 });
    audio_insert(world, hall, 0, hall);
    audio_run(world, 1);
    audio_source(world, tone, 1, false, hall);
    audio_run(world, 30);
    at = audio_tap_count;
    before = audio_max_step(audio_tap_l, at - AUDIO_FRAME * 10, at);
    test_assert(audio_peak(audio_tap_l, at - AUDIO_FRAME, at) > 0.001f);
    ecs_delete(world, hall);
    audio_run(world, 1);
    end = audio_tap_count;
    test_assert(audio_peak(audio_tap_l, at, end) > 0.0001f);
    audio_run(world, 10);
    after = audio_max_step(audio_tap_l, at, audio_tap_count);
    printf("largest step: %.4f before, %.4f after the delete\n", (double)before,
        (double)after);
    test_assert(after <= before * 1.5f);
    for (i = at + (int32_t)(BOX_AUDIO_REVERB_FADE * AUDIO_RATE); i < audio_tap_count; i ++) {
        test_flt(audio_tap_l[i], 0);
        test_flt(audio_tap_r[i], 0);
    }

    ecs_fini(world);
}

void Reverb_no_alloc(void) {
    // audio/reverb
    rv_t rv;
    int32_t k;
    rv_open(&rv, AUDIO_RATE, rv_params(1.5f, 0.5f, 0.02f, 0.5f, 1), true);
    test_int(rv_allocs, 1);
    audio_sine(audio_sound_a, AUDIO_RATE, 440, 0.25f);
    test_assert(box_audio_core_sound(rv.core, 2, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(rv.core, 2, 2, 1, 1, 0, 1, 0);
    audio_play(rv.core, 3, 1, 1, 1, 0, 1, 0);
    for (k = 0; k < 6; k ++) {
        rv_command(rv.core, 0, 1, rv_params(0.5f + k, 0.1f * k, 0.01f * k, 0.15f * k, 1));
        rv_render(rv.core, 0, AUDIO_RATE / 2);
        audio_events(rv.core, 0, NULL);
    }

    rv_command(rv.core, 0, BOX_AUDIO_BUS_NONE, rv_params(1, 0.5f, 0.02f, 0.5f, 1));
    rv_render(rv.core, 0, AUDIO_RATE);
    test_assert(!box_audio_core_effect_running(rv.core, 0));
    test_int(rv_allocs, 1);
    rv_close(&rv);
}
