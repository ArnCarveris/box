/**
 * @file NoAlloc.c
 * @brief The render path does not call the allocator, counted with allocator hooks.
 */

#include "world.h"
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
    p.drift = 15;
    p.free_phase = 1;
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

/* Gated synth voices and sampler voices with the attack flag, their scheduled releases and
 * legato notes that follow them, without a call of the allocator. */
void NoAlloc_gated(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    static uint8_t bytes[64 * 1024];
    box_audio_synth_patch_t p = {0};
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 2;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.01f, .release = 0.05f,
        .curve = 3 };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 400;
    p.filter_amount = 3;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.02f, .sustain = 0.7f, .hold = 0.01f, .release = 0.08f };
    p.glide = 0.02f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 220,
        .level = 0.2f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 110,
        .level = 0.2f, .has_envelope = 1, .envelope = { .attack = 0.005f, .sustain = 1,
        .release = 0.02f } };
    audio_fill(audio_sound_a, 4000, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 2, &p));
    for (i = 1; i <= 200; i ++) {
        uint64_t at = 1 + (uint64_t)(i * 97 % 20000);
        test_assert(box_audio_write_play(&w, i, i % 3 ? 2 : 1, 0, 0.05f, 0, 1,
            i % 3 ? BOX_AUDIO_PLAY_GATE : BOX_AUDIO_PLAY_ATTACK, at));
        test_assert(box_audio_write_voice_release(&w, i, 0.02f, at + 3000));
        if (i % 5 == 0) {
            test_assert(box_audio_write_legato_at(&w, 1000 + i, i, 2, 0, 0.05f, 0, 1.5f,
                BOX_AUDIO_PLAY_GATE, at + 3000));
            test_assert(box_audio_write_voice_release(&w, 1000 + i, 0.02f, at + 6000));
        }
    }

    box_audio_core_apply(core, w.data, w.size);
    test_assert(box_audio_core_scheduled(core) > 0);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_scheduled(core), 0);
    audio_events(core, 0, NULL);
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

void NoAlloc_meters(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_meter_t meters[BOX_AUDIO_BUS_MAX];
    box_audio_effect_meter_t effects[BOX_AUDIO_EFFECT_MAX];
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t i;
    audio_fill(audio_sound_a, 500, 0.1f);
    test_assert(box_audio_core_sound(core, 5, audio_sound_a, 500, 1, AUDIO_RATE));
    audio_order(core, routes, 2);
    audio_play(core, 100000, 5, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (i = 0; i < 100; i ++) {
        audio_render(core, 0, 512);
        test_int(box_audio_core_meters(core, meters, BOX_AUDIO_BUS_MAX), BOX_AUDIO_BUS_MAX);
        test_int(box_audio_core_effect_meters(core, effects, BOX_AUDIO_EFFECT_MAX),
            BOX_AUDIO_EFFECT_MAX);
        test_int(meters[1].frames, 512);
    }

    audio_workload(core, audio_l, audio_r, AUDIO_FRAMES);
    box_audio_core_meters(core, meters, BOX_AUDIO_BUS_MAX);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Equalizers and filters in the insert slots of buses: every kind of band and of filter,
 * changes of their parameters every block, a change of kind and slope, moves between buses
 * and slots and removals, without a call of the allocator. */
void NoAlloc_eq_filter(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k, b;
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_EQ, AUDIO_RATE), 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_FILTER, AUDIO_RATE), 0);
    audio_order(core, routes, 3);
    audio_sine(audio_sound_a, AUDIO_FRAMES, 330, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 16; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 4; i ++) {
            bool eq = (i + k / 4) % 2 == 0;
            for (b = 0; b < BOX_AUDIO_EFFECT_PARAMS; b ++) {
                params[b] = 0;
            }

            if (eq) {
                for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
                    params[b * 4] = (float)((k + b + i) % 6);
                    params[b * 4 + 1] = 100 + 700 * (float)((k + b) % 8);
                    params[b * 4 + 2] = (float)((int32_t)(k % 7) - 3) * 4;
                    params[b * 4 + 3] = 0.5f + (float)b;
                }
            } else {
                params[0] = (float)(1 + (k + i) % 4);
                params[1] = 200 + 400 * (float)k;
                params[2] = 0.1f * (float)(k % 10);
                params[3] = (float)((k / 2) % 2);
                params[4] = 1 - 0.05f * (float)k;
            }

            test_assert(box_audio_write_effect(&w, i, eq ? BOX_AUDIO_EFFECT_EQ :
                BOX_AUDIO_EFFECT_FILTER, i == 3 ? 0 : 1 + (i + k) % 2,
                (i + k) % BOX_AUDIO_EFFECT_SLOTS, BOX_AUDIO_BUS_NONE, params));
        }

        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, AUDIO_FRAMES / 16);
        audio_events(core, 0, NULL);
    }

    for (i = 0; i < 4; i ++) {
        test_assert(box_audio_core_effect_running(core, i));
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < 4; i ++) {
        test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
            BOX_AUDIO_BUS_NONE, NULL));
    }

    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    for (i = 0; i < 4; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Compressors and gates in the insert slots of buses: keys before, after and on their own bus,
 * a key bus that leaves the order, changes of their parameters every block, moves between
 * buses and slots and removals, without a call of the allocator. */
void NoAlloc_dynamics(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(3, 0), AUDIO_OUT(1, 0), AUDIO_OUT(2, 0),
        AUDIO_MASTER };
    box_audio_route_t fewer[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k, b;
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_COMPRESSOR, AUDIO_RATE), 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_GATE, AUDIO_RATE), 0);
    audio_order(core, routes, 4);
    audio_sine(audio_sound_a, AUDIO_FRAMES, 330, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 3, 1, 3, 0.8f, 0, 0.5f, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 16; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 4; i ++) {
            bool comp = (i + k / 4) % 2 == 0;
            for (b = 0; b < BOX_AUDIO_EFFECT_PARAMS; b ++) {
                params[b] = 0;
            }

            if (comp) {
                params[0] = -40 + 2 * (float)k;
                params[1] = 1 + (float)(k % 20);
                params[2] = 0.001f * (float)(1 + k);
                params[3] = 0.02f * (float)(1 + k);
                params[4] = (float)(k % 4) * 3;
                params[5] = (float)(k % 3) * 2;
                params[6] = 1 - 0.05f * (float)k;
            } else {
                params[0] = -60 + 3 * (float)k;
                params[1] = 0.0005f * (float)(1 + k);
                params[2] = 0.01f * (float)(k % 5);
                params[3] = 0.01f * (float)(1 + k);
                params[4] = -10 * (float)(1 + k % 8);
            }

            test_assert(box_audio_write_effect(&w, i, comp ? BOX_AUDIO_EFFECT_COMPRESSOR :
                BOX_AUDIO_EFFECT_GATE, i == 3 ? 0 : 1 + (i + k) % 2,
                (i + k) % BOX_AUDIO_EFFECT_SLOTS, (i + k) % 5 == 4 ? BOX_AUDIO_BUS_NONE :
                (i + k) % 4, params));
        }

        box_audio_core_apply(core, w.data, w.size);
        if (k == 10) {
            audio_order(core, fewer, 3);
        }

        audio_render(core, 0, AUDIO_FRAMES / 16);
        audio_events(core, 0, NULL);
        box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    }

    for (i = 0; i < 4; i ++) {
        test_assert(box_audio_core_effect_running(core, i));
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < 4; i ++) {
        test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
            BOX_AUDIO_BUS_NONE, NULL));
    }

    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    for (i = 0; i < 4; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Delays in the insert slots of buses: an echo that rings, changes of the time that
 * crossfade, again while a crossfade runs, ping pong, feedback, damping and mix, a move to
 * another bus, a tail that decays to silence and starts again, and a removal, without a call
 * of the allocator. */
void NoAlloc_delay(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_DELAY, AUDIO_RATE);
    float *memory[2], params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k;
    test_assert(floats > 0);
    audio_order(core, routes, 3);
    audio_sine(audio_sound_a, AUDIO_FRAMES / 4, 330, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES / 4, 1, AUDIO_RATE));
    for (i = 0; i < 2; i ++) {
        memory[i] = calloc((size_t)floats, sizeof(float));
        test_assert(box_audio_core_effect_memory(core, i, memory[i], floats));
    }

    audio_play(core, 1, 1, 1, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, 0);
    for (k = 0; k < 12; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 2; i ++) {
            params[0] = 0.05f + (float)((k + i) % 5) * 0.11f;
            params[1] = 0.1f * (float)(k % 9);
            params[2] = 500.0f + 2000.0f * (float)(k % 4);
            params[3] = (float)((k + i) % 2);
            params[4] = 0.25f * (float)(k % 5);
            test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_DELAY,
                1 + (i + k / 4) % 2, i, BOX_AUDIO_BUS_NONE, params));
        }

        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, k % 3 ? AUDIO_FRAMES / 40 : AUDIO_FRAMES / 4);
        audio_events(core, 0, NULL);
    }

    // The voices stop and the tails decay to silence, then a voice starts them again
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
    for (k = 0; k < 8; k ++) {
        audio_render(core, 0, AUDIO_FRAMES);
    }

    test_assert(!box_audio_core_effect_running(core, 0));
    test_assert(!box_audio_core_effect_running(core, 1));
    audio_play(core, 3, 1, 1, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
        BOX_AUDIO_BUS_NONE, NULL));
    test_assert(box_audio_write_effect(&w, 1, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0,
        BOX_AUDIO_BUS_NONE, NULL));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(!box_audio_core_effect_running(core, 0));
    test_assert(!box_audio_core_effect_running(core, 1));
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
    for (i = 0; i < 2; i ++) {
        free(memory[i]);
    }
}

/* Phasers and choruses in the insert slots of buses: sweeps, changes of the rate, the stages,
 * the voices, the feedback, the spread and the mix, a move to another bus, tails that decay to
 * silence while the LFOs run on and start again, and a removal, without a call of the
 * allocator. */
void NoAlloc_modulation(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_CHORUS, AUDIO_RATE);
    float *memory[2], params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k;
    test_assert(floats > 0);
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_PHASER, AUDIO_RATE), 0);
    audio_order(core, routes, 3);
    audio_sine(audio_sound_a, AUDIO_FRAMES / 4, 330, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES / 4, 1, AUDIO_RATE));
    for (i = 0; i < 2; i ++) {
        memory[i] = calloc((size_t)floats, sizeof(float));
        test_assert(box_audio_core_effect_memory(core, 2 + i, memory[i], floats));
    }

    audio_play(core, 1, 1, 1, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, 0);
    for (k = 0; k < 12; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 2; i ++) {
            uint32_t bus = 1 + (i + k / 4) % 2;
            params[0] = (float)(2 + 2 * ((k + i) % 6));
            params[1] = 0.5f + 1.5f * (float)(k % 5);
            params[2] = 0.1f * (float)(k % 11);
            params[3] = 200.0f + 400.0f * (float)(k % 4);
            params[4] = 0.9f - 0.3f * (float)(k % 7);
            params[5] = 0.25f * (float)(k % 5);
            params[6] = 0.2f * (float)(k % 6);
            test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_PHASER, bus, i,
                BOX_AUDIO_BUS_NONE, params));
            params[0] = (float)(1 + (k + i) % 4);
            params[1] = 0.3f + (float)(k % 7);
            params[2] = 0.5f + 2.0f * (float)(k % 4);
            params[3] = 5.0f + 6.0f * (float)(k % 6);
            params[4] = 0.25f * (float)(k % 5);
            params[5] = 0.3f * (float)(k % 4);
            test_assert(box_audio_write_effect(&w, 2 + i, BOX_AUDIO_EFFECT_CHORUS, bus, 2 + i,
                BOX_AUDIO_BUS_NONE, params));
        }

        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, k % 3 ? AUDIO_FRAMES / 40 : AUDIO_FRAMES / 4);
        audio_events(core, 0, NULL);
    }

    // The voices stop and the tails decay to silence, then a voice starts them again
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
    for (k = 0; k < 8; k ++) {
        audio_render(core, 0, AUDIO_FRAMES);
    }

    for (i = 0; i < 4; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    audio_play(core, 3, 1, 1, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < 4; i ++) {
        test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE,
            0, BOX_AUDIO_BUS_NONE, NULL));
    }

    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < 4; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
    for (i = 0; i < 2; i ++) {
        free(memory[i]);
    }
}

/* Saturators in the insert slots of buses: changes of the drive, the asymmetry, the bits, the
 * mix, the mode, the cuts, the tone and the rate while they run, moves to another bus and slot, the reduction read, tails that decay
 * to silence and start again, and a removal, without a call of the allocator. */
void NoAlloc_saturator(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k;
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_SATURATOR, AUDIO_RATE), 0);
    audio_order(core, routes, 3);
    audio_sine(audio_sound_a, AUDIO_FRAMES / 4, 330, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES / 4, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 0.8f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 12; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 3; i ++) {
            params[0] = 0.1f * (float)((k + i) % 11);
            params[1] = 0.25f * (float)((k + 2 * i) % 5);
            params[2] = 16.0f - 1.5f * (float)((k + i) % 11);
            params[3] = 1 - 0.2f * (float)(k % 6);
            params[4] = (float)((k + i) % BOX_AUDIO_SATURATOR_MODES);
            params[5] = 100.0f * (float)(k % 3);
            params[6] = 6000.0f * (float)((k + i) % 2);
            params[7] = -1 + 0.5f * (float)((k + 2 * i) % 5);
            params[8] = 3000.0f * (float)(k % 4);
            test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_SATURATOR,
                1 + (i + k / 4) % 2, (i + k) % BOX_AUDIO_EFFECT_SLOTS, BOX_AUDIO_BUS_NONE,
                params));
        }

        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, k % 3 ? AUDIO_FRAMES / 40 : AUDIO_FRAMES / 4);
        audio_events(core, 0, NULL);
        box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    }

    // A saturator that arrived in its new slot during the last block runs from the next one
    audio_render(core, 0, AUDIO_FRAMES / 40);
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_core_effect_running(core, i));
    }

    // The voices stop and the tails decay to silence, then a voice starts them again
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 1, 0));
    test_assert(box_audio_write_stop(&w, 2, 0));
    box_audio_core_apply(core, w.data, w.size);
    for (k = 0; k < 4; k ++) {
        audio_render(core, 0, AUDIO_FRAMES);
    }

    for (i = 0; i < 3; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    audio_play(core, 3, 1, 1, 0.5f, 0, 1, 0);
    audio_play(core, 4, 1, 2, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 8);
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_core_effect_running(core, i));
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE,
            0, BOX_AUDIO_BUS_NONE, NULL));
    }

    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < 3; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Transient shapers in the insert slots of buses: changes of the attack and the sustain while
 * they run, moves to another bus and slot, envelopes that fall to silence and start again, and
 * a removal, without a call of the allocator. */
void NoAlloc_transient_shaper(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    uint8_t bytes[1024];
    box_audio_writer_t w;
    uint32_t i, k;
    test_int(box_audio_effect_floats(BOX_AUDIO_EFFECT_TRANSIENT_SHAPER, AUDIO_RATE), 0);
    audio_order(core, routes, 3);
    for (i = 0; i < AUDIO_FRAMES / 4; i ++) {
        double t = (double)(i % (AUDIO_FRAMES / 16)) / AUDIO_RATE;
        audio_sound_a[i] = (float)(0.5 * exp(-t / 0.03) * sin(2 * AUDIO_PI * 180 * t));
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES / 4, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 0.8f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 2, 0.5f, 0, 1.5f, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < 12; k ++) {
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 3; i ++) {
            params[0] = -1 + 0.2f * (float)((k + i) % 11);
            params[1] = 1 - 0.5f * (float)((k + 2 * i) % 5);
            test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_TRANSIENT_SHAPER,
                1 + (i + k / 4) % 2, (i + k) % BOX_AUDIO_EFFECT_SLOTS, BOX_AUDIO_BUS_NONE,
                params));
        }

        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, k % 3 ? AUDIO_FRAMES / 40 : AUDIO_FRAMES / 4);
        audio_events(core, 0, NULL);
    }

    // A shaper that arrived in its new slot during the last block runs from the next one
    audio_render(core, 0, AUDIO_FRAMES / 40);
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_core_effect_running(core, i));
    }

    // The voices stop and the envelopes fall to silence, then a voice starts them again
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 1, 0));
    test_assert(box_audio_write_stop(&w, 2, 0));
    box_audio_core_apply(core, w.data, w.size);
    for (k = 0; k < 4; k ++) {
        audio_render(core, 0, AUDIO_FRAMES);
    }

    for (i = 0; i < 3; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    audio_play(core, 3, 1, 1, 0.5f, 0, 1, 0);
    audio_play(core, 4, 1, 2, 0.5f, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 8);
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_core_effect_running(core, i));
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_write_effect(&w, i, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE,
            0, BOX_AUDIO_BUS_NONE, NULL));
    }

    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < 3; i ++) {
        test_assert(!box_audio_core_effect_running(core, i));
    }

    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch with subs, on a mono layer, a wide unison layer and a modulator with
 * feedback, under a glide and an LFO, render without a call of the allocator. */
void NoAlloc_synth_sub(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 3;
    p.seed = 9;
    p.glide = 0.05f;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .level = 0.3f, .sub = 0.7f, .sub_octave = 1, .voices = 3, .detune = 20, .width = 1 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.2f, .sub = 0.5f, .sub_octave = 2, .pulse_width = 0.3f, .feedback = 0.4f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 440,
        .level = 1, .sub = 1, .sub_octave = 1, .modulates = 2,
        .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 1 };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 20, .amplitude = 0.2f };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 16; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.05f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < 16; i ++) {
        audio_voice_set(core, i, BOX_AUDIO_PARAM_PITCH, 1.5f);
    }

    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch through the drive of the mix, with an asymmetry: a mono saw, a wide unison
 * layer, a pluck and a modulator, into a filter, under an LFO, render without a call of the
 * allocator. */
void NoAlloc_synth_drive(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 4;
    p.seed = 5;
    p.has_drive = 1;
    p.pre_drive = 0.7f;
    p.pre_asymmetry = 0.3f;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 800;
    p.resonance = 0.5f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .level = 0.3f, .voices = 3, .detune = 20, .width = 1 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 220,
        .level = 0.3f, .voices = 1 };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330,
        .level = 0.3f };
    p.layer[3] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 660,
        .level = 1, .modulates = 3, .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 1 };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 20, .cutoff = 1 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 8; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.05f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch with hard sync, on a mono saw under a glide, a wide unison square and a
 * triangle phase modulated by a modulator, under LFOs of the ratio, render without a call of
 * the allocator. */
void NoAlloc_synth_sync(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 4;
    p.seed = 5;
    p.glide = 0.05f;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .level = 0.3f, .sync = 2.5f, .sub = 0.5f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.2f, .sync = 3.7f, .pulse_width = 0.3f, .voices = 3, .detune = 20,
        .width = 1 };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_TRIANGLE, .frequency = 165,
        .level = 0.2f, .sync = 1.5f };
    p.layer[3] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330,
        .level = 1, .modulates = 3, .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 1 };
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
        .hard_sync = 12 };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 1,
        .layer = 4, .hard_sync = 24 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 16; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.05f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < 16; i ++) {
        audio_voice_set(core, i, BOX_AUDIO_PARAM_PITCH, 1.5f);
    }

    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch through a formant filter with drive, on a mono saw and a wide unison
 * layer, the vowel moved by the filter envelope and an LFO, render without a call of the
 * allocator. */
void NoAlloc_synth_formant(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 2;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.filter = BOX_AUDIO_SYNTH_FORMANT;
    p.resonance = 0.5f;
    p.drive = 0.5f;
    p.vowel = 1;
    p.filter_vowel = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.05f, .decay = 0.1f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .level = 0.3f, .voices = 3, .detune = 20, .width = 1 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.3f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .vowel = 1 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 8; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.05f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch through a comb filter render without a call of the allocator: a mono
 * noise layer and a wide unison saw, the cutoff moved by the filter envelope and an LFO, the
 * note by a pitch envelope, an LFO and a glide, as many voices as the pool of comb lines
 * holds and more, which are ignored. */
void NoAlloc_synth_comb(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 2;
    p.seed = 9;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.filter = BOX_AUDIO_SYNTH_COMB;
    p.cutoff = 4000;
    p.feedback = 0.9f;
    p.tune = 1.5f;
    p.glide = 0.05f;
    p.filter_amount = -2;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.01f, .decay = 0.1f };
    p.pitch_envelope = (box_audio_synth_pitch_envelope_t){ .amount = 7, .decay = 0.05f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .frequency = 110,
        .level = 0.3f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 220,
        .level = 0.3f, .voices = 3, .detune = 20, .width = 1 };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .cutoff = 1, .pitch = 30 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    p.layers = 1;
    p.lfo[0].cutoff = 0;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 2, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < BOX_AUDIO_SYNTH_COMBS; i ++) {
        audio_play(core, i, 1 + (i & 1), 0, 0.02f, 0, 0.5f + 0.05f * (float)i,
            BOX_AUDIO_PLAY_LOOP);
    }

    for (i = 0; i < BOX_AUDIO_SYNTH_COMBS; i ++) {
        audio_voice_set(core, i, BOX_AUDIO_PARAM_PITCH, 1.5f);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch of wavetable layers render without a call of the allocator: a mono layer
 * whose position an LFO and its envelope move, a wide unison layer under a glide, a synced
 * layer with a sub and a wavetable modulator, and a release of the table while they play. The
 * table is built before, on the main thread. */
void NoAlloc_synth_wavetable(void) {
    // audio/noalloc
    box_audio_core_t *core;
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    int32_t floats = 0;
    float *table;
    uint32_t i;
    ecs_os_set_api_defaults();
    table = box_audio_wavetable_kind(WavetableHarmonics, &floats);
    core = noalloc_core();
    test_assert(table != NULL);
    test_assert(box_audio_core_sound(core, 2, table, floats, 1, 0));
    p.layers = 4;
    p.seed = 3;
    p.glide = 0.05f;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 110, .level = 0.3f, .table = 2, .position = 3, .position_envelope = 12 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 220, .level = 0.2f, .table = 2, .position = 20.5f, .voices = 3,
        .detune = 20, .width = 1 };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 165, .level = 0.2f, .table = 2, .position = 8, .sync = 2.5f,
        .sub = 0.5f };
    p.layer[3] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 330, .level = 1, .modulates = 3, .modulation = BOX_AUDIO_SYNTH_PHASE,
        .index = 1, .table = 2, .position = 1 };
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
        .position = 10, .pitch = 10 };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 5,
        .layer = 2, .position = 4 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 16; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.05f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES / 2);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_release(&w, 2));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 16; i ++) {
        audio_voice_set(core, i, BOX_AUDIO_PARAM_PITCH, 1.5f);
    }

    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
    ecs_os_free(table);
}

/* The expression of voices without a call of the allocator: ramps of the bend, the mod and the
 * pressure of synth voices on the mono path and on the stereo path of unison, of a pluck string
 * and of a sample voice, scheduled ramps, ramps at the frame of their play, a cancel and a
 * legato note that resets them. */
void NoAlloc_expression(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0}, q = {0};
    static uint8_t bytes[64 * 1024];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 2;
    p.seed = 5;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 1, .release = 0.05f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 400;
    p.keytrack = 1;
    p.glide = 0.05f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .level = 0.3f, .voices = 3, .detune = 20, .width = 0.5f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 220,
        .level = 0.3f, .modulates = 1, .index = 1 };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 30, .cutoff = 0.5f };
    q.layers = 1;
    q.seed = 3;
    q.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = 220,
        .level = 0.5f, .decay = 2, .brightness = 0.5f, .hardness = 0.5f, .rings_free = 1 };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 2, &q));
    box_audio_core_apply(core, w.data, w.size);
    audio_sine(audio_sound_a, 4800, 1000, 0.5f);
    test_assert(box_audio_core_sound(core, 3, audio_sound_a, 4800, 1, AUDIO_RATE));

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < 12; i ++) {
        uint64_t at = 1 + (uint64_t)i * 1000;
        uint32_t sound = 1 + i % 3;
        test_assert(box_audio_write_voice_ramp(&w, 1 + i, BOX_AUDIO_PARAM_BEND, -3, 0, at));
        test_assert(box_audio_write_play(&w, 1 + i, sound, 0, 0.1f, 0, 1,
            sound == 3 ? BOX_AUDIO_PLAY_LOOP : BOX_AUDIO_PLAY_GATE, at));
        test_assert(box_audio_write_voice_ramp(&w, 1 + i, BOX_AUDIO_PARAM_BEND, 12, 4000,
            at + 100));
        test_assert(box_audio_write_voice_ramp(&w, 1 + i, BOX_AUDIO_PARAM_MOD, 0, 2000,
            at + 200));
        test_assert(box_audio_write_voice_ramp(&w, 1 + i, BOX_AUDIO_PARAM_PRESSURE, 1, 3000,
            at + 300));
        test_assert(box_audio_write_voice_set(&w, 1 + i, BOX_AUDIO_PARAM_PITCH, 1.5f,
            at + 2500));
    }

    test_assert(box_audio_write_voice_ramp(&w, 4, BOX_AUDIO_PARAM_BEND, 48, 100, 20000));
    test_assert(box_audio_write_cancel(&w, BOX_AUDIO_CMD_VOICE_RAMP, 4, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato_note(&w, 100, 1, 1, 0, 0.1f, 0, 1.2f, 1, 0, 0,
        BOX_AUDIO_PLAY_GATE, 0, 0));
    test_assert(box_audio_write_voice_ramp(&w, 100, BOX_AUDIO_PARAM_BEND, 5, 4800, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, AUDIO_FRAMES / 2, AUDIO_FRAMES / 2);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}

/* Voices of a patch with 8 routes of the modulation matrix, every source and target among
 * them, with the mod envelope, a wide unison layer, a modulator and a filter, gated and
 * released, render without a call of the allocator. */
void NoAlloc_synth_routes(void) {
    // audio/noalloc
    box_audio_core_t *core = noalloc_core();
    box_audio_synth_patch_t p = {0};
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 3;
    p.seed = 11;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    p.mod_envelope = (box_audio_synth_envelope_t){
        .attack = 0.05f, .decay = 0.1f, .sustain = 0.2f, .release = 0.05f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 800;
    p.resonance = 0.3f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .level = 0.3f, .voices = 3, .detune = 20, .width = 1 };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.2f, .pulse_width = 0.3f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 440,
        .level = 1, .modulates = 2, .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 1 };
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5 };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 9 };
    p.routes = 8;
    p.route[0] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_MOD_ENV,
        .target = BOX_AUDIO_SYNTH_TO_CUTOFF, .amount = 3 };
    p.route[1] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO2,
        .target = BOX_AUDIO_SYNTH_TO_PAN, .amount = 1 };
    p.route[2] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_VELOCITY,
        .target = BOX_AUDIO_SYNTH_TO_PITCH, .amount = 1 };
    p.route[3] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_RANDOM,
        .target = BOX_AUDIO_SYNTH_TO_DETUNE, .amount = 20 };
    p.route[4] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_AMP,
        .target = BOX_AUDIO_SYNTH_TO_INDEX, .layer = 3, .amount = 2 };
    p.route[5] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_LFO1,
        .target = BOX_AUDIO_SYNTH_TO_PULSE_WIDTH, .layer = 2, .amount = 0.2f };
    p.route[6] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_PRESSURE,
        .target = BOX_AUDIO_SYNTH_TO_RESONANCE, .amount = 0.5f };
    p.route[7] = (box_audio_synth_route_t){ .source = BOX_AUDIO_SYNTH_FROM_FILTER,
        .target = BOX_AUDIO_SYNTH_TO_LEVEL, .layer = 1, .amount = -0.5f };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 16; i ++) {
        audio_play(core, i, 1, 0, 0.02f, 0, 0.5f + 0.05f * (float)i,
            i % 2 ? BOX_AUDIO_PLAY_GATE : BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES / 2);
    for (i = 0; i < 16; i ++) {
        audio_voice_set(core, i, BOX_AUDIO_PARAM_PRESSURE, 0.7f);
        audio_voice_set(core, i, BOX_AUDIO_PARAM_PITCH, 1.5f);
    }

    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(box_audio_core_voice_count(core) > 0);
    test_int(noalloc_allocs, 1);
    test_int(noalloc_frees, 0);
    box_audio_core_free(core);
    test_int(noalloc_frees, 1);
}
