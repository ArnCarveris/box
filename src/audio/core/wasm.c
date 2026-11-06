/**
 * @file audio/core/wasm.c
 * @brief Exports of the standalone wasm of the core, which the web runs in an AudioWorklet.
 *
 * Built only by tools/audio_core.py with BOX_AUDIO_CORE_WASM; the main module compiles it to
 * nothing. The worklet copies command batches and sounds into the heap of this module, which
 * grows its memory, so the worklet recreates its views of the memory after an allocation.
 */

#ifdef BOX_AUDIO_CORE_WASM

#include "core.h"

/** Bytes of the header of a heap block, which keeps the memory after it 16 byte aligned */
#define BOX_AUDIO_WASM_HEADER 16

/** Smallest rest of a free block that is split off as its own block */
#define BOX_AUDIO_WASM_SPLIT 64

/** Bytes of a page of wasm memory */
#define BOX_AUDIO_WASM_PAGE 65536

/** Events the worklet reads per call; it calls again until the core has none left */
#define BOX_AUDIO_WASM_EVENTS 64

/** Start of the heap, placed by the linker after the data and the stack */
extern unsigned char __heap_base;

/** A block of the heap, free or allocated */
typedef struct box_audio_wasm_block_t {
    size_t size;                          /**< Bytes of the block, header included */
    struct box_audio_wasm_block_t *next;  /**< Next free block by address, when free */
} box_audio_wasm_block_t;

/** Free blocks by address */
static box_audio_wasm_block_t *box_audio_wasm_free_list;

/** End of the used heap; memory after it is not in a block yet */
static size_t box_audio_wasm_top;

/** The core of the worklet */
static box_audio_core_t *box_audio_core_wasm;

/** Left channel of the last render */
static float box_audio_core_wasm_l[BOX_AUDIO_CORE_QUANTUM];

/** Right channel of the last render */
static float box_audio_core_wasm_r[BOX_AUDIO_CORE_QUANTUM];

/** Events of the last read */
static box_audio_event_t box_audio_core_wasm_ev[BOX_AUDIO_WASM_EVENTS];

_Static_assert(sizeof(box_audio_meter_t) == 32, "the worklet reads meters of 32 bytes");

/** Levels of the buses of the last read of the meters */
static box_audio_meter_t box_audio_core_wasm_mt[BOX_AUDIO_BUS_MAX];

/** Values of the effects of the last read of the meters */
static box_audio_effect_meter_t box_audio_core_wasm_fx[BOX_AUDIO_EFFECT_MAX];

/** Status of the last stream asked for */
static box_audio_stream_status_t box_audio_core_wasm_st;

/** Fill bytes, for the code the compiler generates.
 *
 * @param dst The bytes
 * @param c The value
 * @param n Number of bytes
 * @return dst
 */
void* memset(
    void *dst,
    int c,
    size_t n)
{
    unsigned char *d = dst;
    size_t i;
    for (i = 0; i < n; i ++) {
        d[i] = (unsigned char)c;
    }

    return dst;
}

/** Copy bytes, for the code the compiler generates.
 *
 * @param dst Destination
 * @param src Source
 * @param n Number of bytes
 * @return dst
 */
void* memcpy(
    void *dst,
    const void *src,
    size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t i;
    for (i = 0; i < n; i ++) {
        d[i] = s[i];
    }

    return dst;
}

/** Allocate from the heap, first fit, growing the memory when no free block fits.
 *
 * @param size Bytes
 * @return 16 byte aligned memory, NULL when the memory cannot grow
 */
void* box_audio_core_wasm_alloc(
    int32_t size)
{
    box_audio_wasm_block_t **link = &box_audio_wasm_free_list, *b = NULL;
    size_t need = (((size_t)(size > 0 ? size : 1) + 15) & ~(size_t)15) + BOX_AUDIO_WASM_HEADER;
    void *result = NULL;
    if (!box_audio_wasm_top) {
        box_audio_wasm_top = ((size_t)&__heap_base + 15) & ~(size_t)15;
    }

    while (*link && (*link)->size < need) {
        link = &(*link)->next;
    }

    if (*link) {
        b = *link;
        if (b->size - need >= BOX_AUDIO_WASM_SPLIT) {
            box_audio_wasm_block_t *rest = (box_audio_wasm_block_t*)((char*)b + need);
            rest->size = b->size - need;
            rest->next = b->next;
            b->size = need;
            *link = rest;
        } else {
            *link = b->next;
        }
    } else {
        size_t end = (size_t)__builtin_wasm_memory_size(0) * BOX_AUDIO_WASM_PAGE;
        if (box_audio_wasm_top + need > end) {
            size_t pages = (box_audio_wasm_top + need - end + BOX_AUDIO_WASM_PAGE - 1) /
                BOX_AUDIO_WASM_PAGE;
            if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) {
                goto done;
            }
        }

        b = (box_audio_wasm_block_t*)box_audio_wasm_top;
        b->size = need;
        box_audio_wasm_top += need;
    }

    b->next = NULL;
    result = (char*)b + BOX_AUDIO_WASM_HEADER;

done:
    return result;
}

/** Free memory of box_audio_core_wasm_alloc, merging it with free neighbours.
 *
 * @param ptr The memory, may be NULL
 */
void box_audio_core_wasm_free(
    void *ptr)
{
    box_audio_wasm_block_t *b, *prev = NULL, *next = box_audio_wasm_free_list;
    if (!ptr) {
        goto done;
    }

    b = (box_audio_wasm_block_t*)((char*)ptr - BOX_AUDIO_WASM_HEADER);
    while (next && next < b) {
        prev = next;
        next = next->next;
    }

    b->next = next;
    if (next && (char*)b + b->size == (char*)next) {
        b->size += next->size;
        b->next = next->next;
    }

    if (prev) {
        prev->next = b;
        if ((char*)prev + prev->size == (char*)b) {
            prev->size += b->size;
            prev->next = b->next;
        }
    } else {
        box_audio_wasm_free_list = b;
    }

done:
    return;
}

/** Allocator of the core.
 *
 * @param size Bytes
 * @param ctx Unused
 * @return The memory
 */
static void* box_audio_core_wasm_core_alloc(
    size_t size,
    void *ctx)
{
    (void)ctx;

    return box_audio_core_wasm_alloc((int32_t)size);
}

/** Free of the allocator of the core.
 *
 * @param ptr The memory
 * @param ctx Unused
 */
static void box_audio_core_wasm_core_free(
    void *ptr,
    void *ctx)
{
    (void)ctx;
    box_audio_core_wasm_free(ptr);
}

/** Start the core at the sample rate of the AudioContext.
 *
 * @param sample_rate Frames per second
 * @return 1 when the core started, 0 when its memory could not be allocated
 */
int32_t box_audio_core_wasm_init(
    float sample_rate)
{
    box_audio_core_desc_t desc = {
        .sample_rate = sample_rate,
        .alloc = box_audio_core_wasm_core_alloc,
        .free = box_audio_core_wasm_core_free,
        .ctx = 0
    };
    if (!box_audio_core_wasm) {
        box_audio_core_wasm = box_audio_core_new(&desc);
    }

    return box_audio_core_wasm ? 1 : 0;
}

/** Give the core a sound the worklet copied into the heap.
 *
 * @param id Id of the sound
 * @param data Frames in the heap
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound
 * @return 1 when the core took it
 */
int32_t box_audio_core_wasm_sound(
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    return box_audio_core_sound(box_audio_core_wasm, id, data, frames, channels,
        sample_rate) ? 1 : 0;
}

/** Give a stream a chunk the worklet copied into the heap.
 *
 * @param stream Index of the stream
 * @param data Frames in the heap
 * @param frames Number of frames
 * @param last 1 when the stream ends with this chunk
 * @return 1 when the core took it
 */
int32_t box_audio_core_wasm_chunk(
    uint32_t stream,
    const float *data,
    int32_t frames,
    int32_t last)
{
    return box_audio_core_stream_chunk(box_audio_core_wasm, stream, data, frames,
        last != 0) ? 1 : 0;
}

/** Get the status of a stream, 7 words: voice, open, ended, consumed, at, underruns and
 * starved.
 *
 * @param stream Index of the stream
 * @return Its address in the memory of the core
 */
box_audio_stream_status_t* box_audio_core_wasm_stream_status(
    uint32_t stream)
{
    box_audio_core_stream_status(box_audio_core_wasm, stream, &box_audio_core_wasm_st);

    return &box_audio_core_wasm_st;
}

/** Give an effect slot the memory the worklet allocated in the heap.
 *
 * @param slot Slot of the effect
 * @param data The memory
 * @param floats Floats of the memory
 * @return 1 when the core took it
 */
int32_t box_audio_core_wasm_effect(
    uint32_t slot,
    float *data,
    int32_t floats)
{
    return box_audio_core_effect_memory(box_audio_core_wasm, slot, data, floats) ? 1 : 0;
}

/** Apply a command batch the worklet copied into the heap.
 *
 * @param commands The bytes
 * @param size Number of bytes
 */
void box_audio_core_wasm_apply(
    const void *commands,
    int32_t size)
{
    box_audio_core_apply(box_audio_core_wasm, commands, size);
}

/** Render a quantum into the channel buffers.
 *
 * @param frames Frames to render, at most BOX_AUDIO_CORE_QUANTUM
 */
void box_audio_core_wasm_render(
    int32_t frames)
{
    if (frames > BOX_AUDIO_CORE_QUANTUM) {
        frames = BOX_AUDIO_CORE_QUANTUM;
    }

    box_audio_core_render(box_audio_core_wasm, box_audio_core_wasm_l, box_audio_core_wasm_r,
        frames);
}

/** Read events of the core into the event buffer.
 *
 * @return Number of events read
 */
int32_t box_audio_core_wasm_read_events(void)
{
    return box_audio_core_read_events(box_audio_core_wasm, box_audio_core_wasm_ev,
        BOX_AUDIO_WASM_EVENTS);
}

/** Get the event buffer, 3 words per event: type, id and value.
 *
 * @return Its address in the memory of the core
 */
box_audio_event_t* box_audio_core_wasm_events(void)
{
    return box_audio_core_wasm_ev;
}

/** Get the number of events the core lost since the last LOST event was read.
 *
 * @return Number of events
 */
int32_t box_audio_core_wasm_events_lost(void)
{
    return box_audio_core_wasm ? box_audio_core_events_lost(box_audio_core_wasm) : 0;
}

/** Get the number of voices that play.
 *
 * @return Number of voices
 */
int32_t box_audio_core_wasm_voices(void)
{
    return box_audio_core_wasm ? box_audio_core_voice_count(box_audio_core_wasm) : 0;
}

/** Get the clock of the core, the frames it rendered, as a double, which holds every frame
 * count exactly for millennia and needs no BigInt in the worklet.
 *
 * @return Frames of the core clock
 */
double box_audio_core_wasm_clock(void)
{
    return box_audio_core_wasm ? (double)box_audio_core_clock(box_audio_core_wasm) : 0;
}

/** Take the meters of the buses and the effects since the last read into their buffers,
 * BOX_AUDIO_BUS_MAX box_audio_meter_t of 32 bytes each: the peaks as 2 floats, the sums of
 * squares as 2 doubles, the frames as a word and the K-weighted sum as a float.
 *
 * @return Address of the meters of the buses in the memory of the core
 */
box_audio_meter_t* box_audio_core_wasm_meters(void)
{
    box_audio_core_meters(box_audio_core_wasm, box_audio_core_wasm_mt, BOX_AUDIO_BUS_MAX);
    box_audio_core_effect_meters(box_audio_core_wasm, box_audio_core_wasm_fx,
        BOX_AUDIO_EFFECT_MAX);

    return box_audio_core_wasm_mt;
}

/** Get the values of the effects of the last read of the meters, a float of reduction per
 * slot.
 *
 * @return Their address in the memory of the core
 */
box_audio_effect_meter_t* box_audio_core_wasm_effect_meters(void)
{
    return box_audio_core_wasm_fx;
}

/** Get the left channel buffer.
 *
 * @return Its address in the memory of the core
 */
float* box_audio_core_wasm_left(void)
{
    return box_audio_core_wasm_l;
}

/** Get the right channel buffer.
 *
 * @return Its address in the memory of the core
 */
float* box_audio_core_wasm_right(void)
{
    return box_audio_core_wasm_r;
}

#else

/** Keeps the translation unit of the main module from being empty */
typedef int box_audio_core_wasm_unused_t;

#endif
