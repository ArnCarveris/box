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
    uint8_t bytes[512];
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
    p.cutoff_end = 8000;
    p.cutoff_sweep = 0.15f;
    p.resonance = 0.7f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 440, .sweep = 0.1f, .level = 0.3f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.2f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.2f, .has_envelope = 1, .envelope = { .attack = 0.001f, .decay = 0.02f } };
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
