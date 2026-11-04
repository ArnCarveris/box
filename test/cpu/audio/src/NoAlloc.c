/**
 * @file NoAlloc.c
 * @brief The render path does not call the allocator, counted with allocator hooks.
 */

#include "util.h"
#include <stdlib.h>

/** Calls of the counting allocator */
static int32_t noalloc_allocs;

/** Calls of the counting free */
static int32_t noalloc_frees;

/** Allocator that counts its calls.
 *
 * @param size Bytes
 * @param ctx Unused
 * @return The memory
 */
static void* noalloc_alloc(
    size_t size,
    void *ctx)
{
    (void)ctx;
    noalloc_allocs ++;

    return malloc(size);
}

/** Free that counts its calls.
 *
 * @param ptr The memory
 * @param ctx Unused
 */
static void noalloc_free(
    void *ptr,
    void *ctx)
{
    (void)ctx;
    noalloc_frees ++;
    free(ptr);
}

/** Create a core with the counting allocator.
 *
 * @return The core
 */
static box_audio_core_t* noalloc_core(void)
{
    box_audio_core_desc_t desc = {
        .sample_rate = AUDIO_RATE, .alloc = noalloc_alloc, .free = noalloc_free
    };
    box_audio_core_t *core;
    noalloc_allocs = 0;
    noalloc_frees = 0;
    core = box_audio_core_new(&desc);
    test_assert(core != NULL);
    test_int(noalloc_allocs, 1);

    return core;
}

void NoAlloc_render(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    audio_workload(core, audio_l, audio_r, AUDIO_FRAMES);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 1);
}

void NoAlloc_events_and_release(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    uint8_t bytes[256];
    box_audio_writer_t w;
    box_audio_event_t e;
    uint32_t i;
    audio_fill(audio_sound_a, 500, 0.1f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 500, 1, AUDIO_RATE));
    for (i = 0; i < 200; i ++) {
        audio_play(core, i, 1, 0, 0.1f, 0, 1, 0);
        audio_events(core, 0, NULL);
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_release(&w, 1));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, 4096);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, &e), 1);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

void NoAlloc_synth_voices(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 3;
    p.seed = 5;
    p.pitch_variance = 0.1f;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f,
        .curve = 4, .attack_curve = 1 };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 300;
    p.slope = BOX_AUDIO_SYNTH_SLOPE_24;
    p.filter_amount = 4.7f;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.15f, .sustain = 1, .hold = 0.05f, .release = 0.05f };
    p.resonance = 0.7f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 440, .sweep = 0.1f, .level = 0.3f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.2f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.2f, .has_envelope = 1, .envelope = { .attack = 0.001f, .decay = 0.02f },
        .pulse_width = 0.3f };
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .delay = 0.02f, .fade = 0.05f, .pitch = 20, .amplitude = 0.3f, .pulse_width = 0.2f };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 9,
        .random_phase = 1, .layer = 3, .cutoff = 1 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.02f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    for (i = 0; i < 100; i ++) {
        audio_play(core, 1000 + i, 1, 0, 0.02f, 0, 1, 0);
        audio_render(core, 0, 256);
        audio_events(core, 0, NULL);
    }

    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Commands that wait in the schedule, apply on their frame, apply late on a full schedule and
 * are cancelled by a stop, without a call of the allocator. */
void NoAlloc_scheduled(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    static uint8_t bytes[128 * 1024];
    box_audio_writer_t w;
    uint32_t i;
    audio_fill(audio_sound_a, 4000, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 1; i <= 1000; i ++) {
        uint64_t at = 1 + (uint64_t)(i * 37 % 20000);
        test_assert(box_audio_write_play(&w, i, 1, 0, 0.1f, 0, 1, BOX_AUDIO_PLAY_LOOP, at));
        test_assert(box_audio_write_voice_set(&w, i, BOX_AUDIO_PARAM_PAN, 0.5f, at + 50));
        test_assert(box_audio_write_bus_set(&w, 0, BOX_AUDIO_PARAM_VOLUME, 0.9f, at + 10));
        test_assert(box_audio_write_stop(&w, i, at + 700));
    }

    test_assert(box_audio_write_stop(&w, 5, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_assert(box_audio_core_scheduled(core) > 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_int(box_audio_core_scheduled(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_LATE, NULL), 0);

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < BOX_AUDIO_SCHEDULE_MAX + 10; i ++) {
        test_assert(box_audio_write_voice_set(&w, 7, BOX_AUDIO_PARAM_PAN, 0,
            box_audio_core_clock(core) + 100 + i));
    }

    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), BOX_AUDIO_SCHEDULE_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_LATE, NULL), 10);
    audio_render(core, AUDIO_FRAMES / 2, AUDIO_FRAMES / 2);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Effects in the insert slots of buses: reverbs on several buses, a change of their
 * parameters, moves between buses and slots, a removal whose tail fades on a live bus and one
 * whose bus left the order, and memory that grows, without a call of the allocator. */
void NoAlloc_effects(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    box_audio_route_t fewer[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_REVERB, AUDIO_RATE);
    float *memory[4], *larger, params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k;
    audio_order(core, routes, 3);
    audio_sine(audio_sound_a, AUDIO_FRAMES, 330, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    for (i = 0; i < 4; i ++) {
        memory[i] = calloc((size_t)floats, sizeof(float));
        test_assert(box_audio_core_effect_memory(core, i, memory[i], floats));
    }

    larger = calloc((size_t)floats * 2, sizeof(float));
    audio_play(core, 1, 1, 1, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 8; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 4; i ++) {
            params[0] = 0.5f + (float)(k + i) * 0.3f;
            params[1] = 0.1f * (float)k;
            params[2] = 0.01f * (float)(k % 3);
            params[3] = 0.1f * (float)((k + i) % 10);
            params[4] = 0.5f;
            test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_REVERB,
                1 + (i + k) % 2, (i + k) % BOX_AUDIO_EFFECT_SLOTS, BOX_AUDIO_BUS_NONE,
                params));
        }

        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, AUDIO_FRAMES / 8);
        audio_events(core, 0, NULL);
    }

    test_assert(box_audio_core_effect_memory(core, 3, larger, floats * 2));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
        BOX_AUDIO_BUS_NONE, NULL));
    test_assert(box_audio_write_order(&w, fewer, 2));
    test_assert(box_audio_write_effect(&w, 1, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
        BOX_AUDIO_BUS_NONE, NULL));
    test_assert(box_audio_write_effect(&w, 2, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
        BOX_AUDIO_BUS_NONE, NULL));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < 3; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
    for (i = 0; i < 4; i ++) {
        free(memory[i]);
    }

    free(larger);
}
