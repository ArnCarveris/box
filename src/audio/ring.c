/**
 * @file audio/ring.c
 * @brief A single producer single consumer ring of bytes, lock-free with C11 atomics.
 *
 * The producer owns head and the consumer owns tail. Each loads the index of the other side
 * with acquire and publishes its own with release, so bytes are copied in before the other
 * side sees them and copied out before the producer may write over them.
 */

#include "ring.h"
#include <flecs.h>
#include <string.h>

int box_audio_ring_init(
    box_audio_ring_t *ring,
    int32_t capacity)
{
    size_t size = 64;
    int result = -1;
    while (size < (size_t)(capacity > 0 ? capacity : 1)) {
        size *= 2;
    }

    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    ring->data = ecs_os_malloc((ecs_size_t)size);
    ring->capacity = ring->data ? size : 0;
    if (!ring->data) {
        goto done;
    }

    result = 0;
done:
    return result;
}

void box_audio_ring_fini(
    box_audio_ring_t *ring)
{
    ecs_os_free(ring->data);
    ring->data = NULL;
    ring->capacity = 0;
}

int32_t box_audio_ring_room(
    const box_audio_ring_t *ring)
{
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    return (int32_t)(ring->capacity - (head - tail));
}

int32_t box_audio_ring_used(
    const box_audio_ring_t *ring)
{
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);

    return (int32_t)(head - tail);
}

/** Copy bytes into the ring at an index, in two pieces when they cross the end.
 *
 * @param ring The ring
 * @param at Free running index
 * @param data The bytes
 * @param size Number of bytes
 */
static void box_audio_ring_put(
    box_audio_ring_t *ring,
    size_t at,
    const void *data,
    size_t size)
{
    size_t offset = at & (ring->capacity - 1);
    size_t first = ring->capacity - offset;
    if (first > size) {
        first = size;
    }

    memcpy(ring->data + offset, data, first);
    if (size > first) {
        memcpy(ring->data, (const uint8_t*)data + first, size - first);
    }
}

/** Copy bytes out of the ring at an index, in two pieces when they cross the end.
 *
 * @param ring The ring
 * @param at Free running index
 * @param out Receives the bytes
 * @param size Number of bytes
 */
static void box_audio_ring_get(
    const box_audio_ring_t *ring,
    size_t at,
    void *out,
    size_t size)
{
    size_t offset = at & (ring->capacity - 1);
    size_t first = ring->capacity - offset;
    if (first > size) {
        first = size;
    }

    memcpy(out, ring->data + offset, first);
    if (size > first) {
        memcpy((uint8_t*)out + first, ring->data, size - first);
    }
}

bool box_audio_ring_write(
    box_audio_ring_t *ring,
    const void *a,
    int32_t a_size,
    const void *b,
    int32_t b_size)
{
    size_t head, tail, size;
    bool result = false;
    if (a_size < 0 || b_size < 0) {
        goto done;
    }

    size = (size_t)a_size + (size_t)b_size;
    head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (ring->capacity - (head - tail) < size) {
        goto done;
    }

    if (a_size) {
        box_audio_ring_put(ring, head, a, (size_t)a_size);
    }

    if (b_size) {
        box_audio_ring_put(ring, head + (size_t)a_size, b, (size_t)b_size);
    }

    atomic_store_explicit(&ring->head, head + size, memory_order_release);
    result = true;
done:
    return result;
}

bool box_audio_ring_peek(
    const box_audio_ring_t *ring,
    void *out,
    int32_t size)
{
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    bool result = false;
    if (size < 0 || head - tail < (size_t)size) {
        goto done;
    }

    box_audio_ring_get(ring, tail, out, (size_t)size);
    result = true;
done:
    return result;
}

bool box_audio_ring_read(
    box_audio_ring_t *ring,
    void *out,
    int32_t size)
{
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    bool result = false;
    if (size < 0 || head - tail < (size_t)size) {
        goto done;
    }

    if (out) {
        box_audio_ring_get(ring, tail, out, (size_t)size);
    }

    atomic_store_explicit(&ring->tail, tail + (size_t)size, memory_order_release);
    result = true;
done:
    return result;
}
