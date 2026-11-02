/**
 * @file Mix.c
 * @brief Benchmarks for the audio core: 64 voices through 8 buses, one item per frame.
 *
 * Each iteration renders one quantum of 128 stereo frames, so the time per item is the time
 * per frame of the whole mix.
 */

#include <core.h>
#include <bake_bench.h>
#include <math.h>

/** Sample rate of the benchmarks */
#define BENCH_RATE 48000

/** Frames of the sounds */
#define BENCH_SOUND 48000

/** Mono sound of the benchmarks */
static float bench_mono[BENCH_SOUND];

/** Stereo sound of the benchmarks, interleaved */
static float bench_stereo[BENCH_SOUND * 2];

/** Fill the sounds, with a signal or with a silent tail of denormals.
 *
 * @param silent Whether the sounds hold only denormals
 */
static void bench_sounds(
    bool silent)
{
    int32_t i;
    for (i = 0; i < BENCH_SOUND; i ++) {
        float a = silent ? 1e-39f : 0.5f * (float)sin(i * 0.031);
        float c = silent ? -1e-39f : 0.4f * (float)sin(i * 0.017);
        bench_mono[i] = a;
        bench_stereo[i * 2] = a;
        bench_stereo[i * 2 + 1] = c;
    }
}

/** Create a core with 8 buses, 7 into the master, two of them with a send, and 64 looped
 * voices spread over them with pitches that are not 1, so every frame interpolates.
 *
 * @param voices Voices to start
 * @return The core
 */
static box_audio_core_t* bench_core(
    uint32_t voices)
{
    static uint8_t bytes[8192];
    box_audio_route_t routes[] = {
        { 7, 1, 6 }, { 6, 2, BOX_AUDIO_BUS_NONE }, { 5, 1, 4 }, { 4, 1, BOX_AUDIO_BUS_NONE },
        { 3, 2, BOX_AUDIO_BUS_NONE }, { 2, 1, BOX_AUDIO_BUS_NONE },
        { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    box_audio_writer_t w;
    uint32_t i;
    box_audio_core_sound(core, 1, bench_mono, BENCH_SOUND, 1, BENCH_RATE);
    box_audio_core_sound(core, 2, bench_stereo, BENCH_SOUND, 2, 44100);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_order(&w, routes, 8);
    box_audio_write_bus_set(&w, 7, BOX_AUDIO_PARAM_SEND, 0.3f);
    box_audio_write_bus_set(&w, 5, BOX_AUDIO_PARAM_SEND, 0.5f);
    box_audio_write_bus_set(&w, 3, BOX_AUDIO_PARAM_PAN, -0.4f);
    for (i = 0; i < voices; i ++) {
        box_audio_write_play(&w, i, 1 + i % 2, 1 + i % 7, 0.02f, -0.9f + 0.03f * (float)i,
            0.75f + 0.01f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Render quanta of a core until the benchmark ends.
 *
 * @param b The benchmark
 * @param core The core
 */
static void bench_render(
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

void Mix_voices_64_buses_8(
    bench_t *b)
{
    // audio/core
    box_audio_core_t *core;
    bench_sounds(false);
    core = bench_core(BOX_AUDIO_VOICE_MAX);
    bench_render(b, core);
    box_audio_core_free(core);
}

void Mix_silent_64_buses_8(
    bench_t *b)
{
    // audio/core
    box_audio_core_t *core;
    bench_sounds(true);
    core = bench_core(BOX_AUDIO_VOICE_MAX);
    bench_render(b, core);
    box_audio_core_free(core);
}

void Mix_idle(
    bench_t *b)
{
    // audio/core
    box_audio_core_t *core;
    bench_sounds(false);
    core = bench_core(0);
    bench_render(b, core);
    box_audio_core_free(core);
}
