/**
 * @file Delay.c
 * @brief Benchmarks for the delay of the audio core, one item per stereo frame.
 *
 * Each iteration renders one quantum of 128 frames, so the time per item is the time per
 * sample of the delay and what feeds it.
 */

#include <core.h>
#include <bake_bench.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** A route of a bus to an output with send 0 to a bus, the other sends unused */
#define BENCH_ROUTE(bus, output, send)\
    { (bus), (output), { (send), BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE },\
        0, 0 }

/** A route of a bus to an output without sends */
#define BENCH_OUT(bus, output) BENCH_ROUTE(bus, output, BOX_AUDIO_BUS_NONE)

/** The route of the master, last in an order */
#define BENCH_MASTER BENCH_OUT(0, BOX_AUDIO_BUS_NONE)

/** Sample rate of the benchmarks */
#define BENCH_RATE 48000

/** Frames of the sound */
#define BENCH_SOUND 48000

/** Mono sound of the benchmarks */
static float bench_sound[BENCH_SOUND];

/** Delay memory of the delay */
static float *bench_memory;

/** Create a core with a delay on bus 1 and bus 2 sending to it, and voices on bus 2.
 *
 * @param voices Looping voices to start
 * @param ping_pong 1 for ping pong
 * @return The core
 */
static box_audio_core_t* bench_delay_core(
    uint32_t voices,
    float ping_pong)
{
    static uint8_t bytes[8192];
    box_audio_route_t routes[] = {
        BENCH_ROUTE(2, 0, 1), BENCH_OUT(1, 0), BENCH_MASTER
    };
    box_audio_delay_params_t params = { 0.375f, 0.5f, 6000, ping_pong, 1 };
    float fx[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_DELAY, BENCH_RATE);
    box_audio_writer_t w;
    uint32_t i;
    for (i = 0; i < BENCH_SOUND; i ++) {
        bench_sound[i] = 0.3f * (float)sin(i * 0.023) + 0.1f * (float)sin(i * 0.31);
    }

    box_audio_core_sound(core, 1, bench_sound, BENCH_SOUND, 1, BENCH_RATE);
    bench_memory = calloc((size_t)floats, sizeof(float));
    box_audio_core_effect_memory(core, 0, bench_memory, floats);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_order(&w, routes, 3);
    box_audio_write_bus_set(&w, 2, BOX_AUDIO_PARAM_SEND, 0.3f, 0);
    memcpy(fx, &params, sizeof(params));
    box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_DELAY, 1, 0, BOX_AUDIO_BUS_NONE, fx);
    for (i = 0; i < voices; i ++) {
        box_audio_write_play(&w, i, 1, 2, 0.02f, -0.9f + 0.05f * (float)i,
            0.75f + 0.01f * (float)i, BOX_AUDIO_PLAY_LOOP, 0);
    }

    box_audio_core_apply(core, w.data, w.size);
    return core;
}

/** Free a core of the benchmarks and the memory of its delay.
 *
 * @param core The core
 */
static void bench_delay_free(
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
static void bench_delay_render(
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

void Delay_one_delay(
    bench_t *b)
{
    // audio/delay
    box_audio_core_t *core = bench_delay_core(1, 0);
    bench_delay_render(b, core);
    bench_delay_free(core);
}

void Delay_one_delay_ping_pong(
    bench_t *b)
{
    // audio/delay
    box_audio_core_t *core = bench_delay_core(1, 1);
    bench_delay_render(b, core);
    bench_delay_free(core);
}

void Delay_silent_tail(
    bench_t *b)
{
    // audio/delay
    box_audio_core_t *core = bench_delay_core(0, 0);
    bench_delay_render(b, core);
    bench_delay_free(core);
}
