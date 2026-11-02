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
