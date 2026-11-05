/**
 * @file Modulation.c
 * @brief Benchmarks for the phaser, the chorus and the vocoder of the audio core, one item
 * per stereo frame.
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
static box_audio_core_t* bench_modulation_core(
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
static void bench_modulation_free(
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
static void bench_modulation_render(
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

void Modulation_phaser_12(
    bench_t *b)
{
    // audio/phaser
    box_audio_phaser_params_t p = { 12, 0.5f, 0.7f, 800, 0.7f, 0.5f, 0.5f };
    box_audio_core_t *core = bench_modulation_core(BOX_AUDIO_EFFECT_PHASER, &p, sizeof(p), 1);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}

void Modulation_chorus_4(
    bench_t *b)
{
    // audio/chorus
    box_audio_chorus_params_t p = { 4, 0.8f, 3, 12, 0.5f, 1 };
    box_audio_core_t *core = bench_modulation_core(BOX_AUDIO_EFFECT_CHORUS, &p, sizeof(p), 1);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}

void Modulation_silent_tail(
    bench_t *b)
{
    // audio/chorus
    box_audio_chorus_params_t p = { 4, 0.8f, 3, 12, 0.5f, 1 };
    box_audio_core_t *core = bench_modulation_core(BOX_AUDIO_EFFECT_CHORUS, &p, sizeof(p), 0);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}

/** Create a core with a vocoder on bus 1, keyed on bus 2, which runs first: a looping voice
 * on each, the one on bus 2 an octave up.
 *
 * @param bands Bands of the vocoder
 * @return The core
 */
static box_audio_core_t* bench_vocoder_core(
    float bands)
{
    static uint8_t bytes[8192];
    box_audio_route_t routes[] = { BENCH_OUT(2, 0), BENCH_OUT(1, 0), BENCH_MASTER };
    box_audio_vocoder_params_t p = { bands, 100, 8000, 1, 0.005f, 0.05f, 0.2f, 0.3f, 1 };
    float fx[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_VOCODER, BENCH_RATE);
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
    memcpy(fx, &p, sizeof(p));
    box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_VOCODER, 1, 0, 2, fx);
    box_audio_write_play(&w, 0, 1, 1, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP, 0);
    box_audio_write_play(&w, 1, 1, 2, 0.5f, 0, 2, BOX_AUDIO_PLAY_LOOP, 0);
    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Create a core with a lowpass on bus 1 and its voice, and a number of modulators of every
 * source on its cutoff, its resonance and the volume and pan of the bus, four of each, with
 * the beat clock at 128 bpm.
 *
 * @param count Modulators, at most 16
 * @return The core
 */
static box_audio_core_t* bench_modulators_core(
    uint32_t count)
{
    static uint8_t bytes[8192];
    box_audio_filter_params_t p = { BOX_AUDIO_SYNTH_LOWPASS, 1200, 0.3f, 0, 1, 0 };
    box_audio_core_t *core = bench_modulation_core(BOX_AUDIO_EFFECT_FILTER, &p, sizeof(p), 1);
    box_audio_writer_t w;
    uint32_t i;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_beat(&w, 0, 128.0f / 60.0f, 0);
    for (i = 0; i < count; i ++) {
        box_audio_cmd_modulator_t m = { .slot = i, .rate = 3.0f + (float)i,
            .sync = i % 2 ? 0.25f * (float)(1 + i % 4) : 0, .phase = 0.07f * (float)i,
            .depth = 0.2f, .step_count = 8, .smooth = 0.25f, .key = 1, .attack = 0.005f,
            .release = 0.1f, .wave = i % 6 };
        uint32_t k;
        for (k = 0; k < 8; k ++) {
            m.steps[k] = (float)((k * 5 + i) % 7) / 3.5f - 1.0f;
        }

        m.source = i % 4 == 3 ? BOX_AUDIO_MOD_FOLLOWER :
            (i % 4 == 2 ? BOX_AUDIO_MOD_STEPS : BOX_AUDIO_MOD_LFO);
        if (i / 4 == 0) {
            m.target = BOX_AUDIO_MOD_TARGET_EFFECT;
            m.kind = BOX_AUDIO_EFFECT_FILTER;
            m.param = 1;
            m.scale = BOX_AUDIO_MOD_OCTAVES;
            m.depth = 1;
        } else if (i / 4 == 1) {
            m.target = BOX_AUDIO_MOD_TARGET_EFFECT;
            m.kind = BOX_AUDIO_EFFECT_FILTER;
            m.param = 2;
        } else {
            m.target = BOX_AUDIO_MOD_TARGET_BUS;
            m.index = 1;
            m.param = i / 4 == 2 ? BOX_AUDIO_PARAM_VOLUME : BOX_AUDIO_PARAM_PAN;
        }

        box_audio_write_modulator(&w, &m);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

void Modulation_vocoder_16(
    bench_t *b)
{
    // audio/vocoder
    box_audio_core_t *core = bench_vocoder_core(16);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}

void Modulation_filter_16_modulators(
    bench_t *b)
{
    // audio/modulators
    box_audio_core_t *core = bench_modulators_core(16);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}

void Modulation_vocoder_32(
    bench_t *b)
{
    // audio/vocoder
    box_audio_core_t *core = bench_vocoder_core(32);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}

void Modulation_filter_no_modulators(
    bench_t *b)
{
    // audio/modulators
    box_audio_core_t *core = bench_modulators_core(0);
    bench_modulation_render(b, core);
    bench_modulation_free(core);
}
