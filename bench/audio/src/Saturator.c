/**
 * @file Saturator.c
 * @brief Benchmarks for the saturator of the audio core in each of its modes, and with its
 * filters and its sample and hold, one item per stereo frame.
 *
 * Each iteration renders one quantum of 128 frames, so the time per item is the time per
 * sample of the effect and what feeds it.
 */

#include <core.h>
#include <bake_bench.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** A route of a bus to an output without sends */
#define BENCH_OUT(bus, output)\
    { (bus), (output), { BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE,\
        BOX_AUDIO_BUS_NONE }, 0, 0 }

/** The route of the master, last in an order */
#define BENCH_MASTER BENCH_OUT(0, BOX_AUDIO_BUS_NONE)

/** Sample rate of the benchmarks */
#define BENCH_RATE 48000

/** Frames of the sound */
#define BENCH_SOUND 48000

/** Mono sound of the benchmarks */
static float bench_sound[BENCH_SOUND];

/** Memory of the effect, when its kind needs it */
static float *bench_memory;

/** Create a core with an effect of a kind on bus 1, and voices on bus 1.
 *
 * @param kind Kind of the effect
 * @param params Parameters of the effect
 * @param size Bytes of the parameters
 * @param voices Looping voices to start
 * @return The core
 */
static box_audio_core_t* bench_saturator_core(
    uint32_t kind,
    const void *params,
    size_t size,
    uint32_t voices)
{
    static uint8_t bytes[8192];
    box_audio_route_t routes[] = { BENCH_OUT(1, 0), BENCH_MASTER };
    float fx[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    int32_t floats = box_audio_effect_floats(kind, BENCH_RATE);
    box_audio_writer_t w;
    uint32_t i;
    for (i = 0; i < BENCH_SOUND; i ++) {
        bench_sound[i] = 0.3f * (float)sin(i * 0.023) + 0.1f * (float)sin(i * 0.31);
    }

    box_audio_core_sound(core, 1, bench_sound, BENCH_SOUND, 1, BENCH_RATE);
    bench_memory = NULL;
    if (floats > 0) {
        bench_memory = calloc((size_t)floats, sizeof(float));
        box_audio_core_effect_memory(core, 0, bench_memory, floats);
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_order(&w, routes, 2);
    memcpy(fx, params, size);
    box_audio_write_effect(&w, 0, kind, 1, 0, BOX_AUDIO_BUS_NONE, fx);
    for (i = 0; i < voices; i ++) {
        box_audio_write_play(&w, i, 1, 1, 0.02f, -0.9f + 0.05f * (float)i,
            0.75f + 0.01f * (float)i, BOX_AUDIO_PLAY_LOOP, 0);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Free a core of the benchmarks and the memory of its effect.
 *
 * @param core The core
 */
static void bench_saturator_free(
    box_audio_core_t *core)
{
    box_audio_core_free(core);
    free(bench_memory);
    bench_memory = NULL;
}

/** Render quanta of a core until the benchmark ends.
 *
 * @param b The benchmark
 * @param core The core
 */
static void bench_saturator_render(
    bench_t *b,
    box_audio_core_t *core)
{
    static float left[BOX_AUDIO_CORE_QUANTUM], right[BOX_AUDIO_CORE_QUANTUM];
    box_audio_event_t events[64];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
    }

    bench_set_items(b, BOX_AUDIO_CORE_QUANTUM);
    while (bench_iter(b)) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
        box_audio_core_read_events(core, events, 64);
        bench_keep(left);
    }
}

void Saturator_tanh(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_TANH, 0, 0, 0, 0 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_tube(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_TUBE, 0, 0, 0, 0 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_hard(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_HARD, 0, 0, 0, 0 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_fold(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_FOLD, 0, 0, 0, 0 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_rectify(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_RECTIFY, 0, 0, 0, 0 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_tanh_filters(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_TANH, 150, 6000, 0.4f, 0 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_fold_filters_rate(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_FOLD, 150, 6000, 0.4f, 8000 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        1);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}

void Saturator_silent_tail(
    bench_t *b)
{
    // audio/saturator
    box_audio_saturator_params_t p = { 0.6f, 0.2f, 16, 1, (float)BOX_AUDIO_SATURATOR_HARD, 150, 6000, 0.4f, 8000 };
    box_audio_core_t *core = bench_saturator_core(BOX_AUDIO_EFFECT_SATURATOR, &p, sizeof(p),
        0);
    bench_saturator_render(b, core);
    bench_saturator_free(core);
}
