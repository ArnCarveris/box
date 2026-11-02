/**
 * @file audio/ring.h
 * @brief A single producer single consumer ring of bytes, lock-free with C11 atomics.
 *
 * The native transport hands commands to the audio thread and events back over two of these.
 * One thread writes and one thread reads; neither ever blocks, allocates or locks. A write is
 * all or nothing, so a record written with one call is read whole or not at all.
 */

#ifndef BOX_AUDIO_RING_H
#define BOX_AUDIO_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Bytes between the two indices, so the producer and the consumer do not share a cache line */
#define BOX_AUDIO_RING_PAD 64

/** A ring of bytes. The indices run free and wrap at the capacity, a power of two */
typedef struct box_audio_ring_t {
    atomic_size_t head;   /**< Bytes written so far, stored by the producer */
    char pad_head[BOX_AUDIO_RING_PAD - sizeof(atomic_size_t)];  /**< Keeps tail apart */
    atomic_size_t tail;   /**< Bytes read so far, stored by the consumer */
    char pad_tail[BOX_AUDIO_RING_PAD - sizeof(atomic_size_t)];  /**< Keeps data apart */
    uint8_t *data;        /**< The bytes */
    size_t capacity;      /**< Bytes the ring holds, a power of two */
} box_audio_ring_t;

/** Allocate the bytes of a ring, on the main thread before either side uses it.
 *
 * @param ring The ring
 * @param capacity Bytes the ring holds at least, rounded up to a power of two
 * @return 0 on success, nonzero when the allocation failed
 */
int box_audio_ring_init(
    box_audio_ring_t *ring,
    int32_t capacity);

/** Free the bytes of a ring once neither side uses it.
 *
 * @param ring The ring
 */
void box_audio_ring_fini(
    box_audio_ring_t *ring);

/** Get the bytes the producer may write. Producer only.
 *
 * @param ring The ring
 * @return Free bytes
 */
int32_t box_audio_ring_room(
    const box_audio_ring_t *ring);

/** Get the bytes the consumer may read. Consumer only.
 *
 * @param ring The ring
 * @return Bytes written and not read yet
 */
int32_t box_audio_ring_used(
    const box_audio_ring_t *ring);

/** Write a record of up to two parts, all or nothing, without blocking. Producer only.
 *
 * @param ring The ring
 * @param a First part
 * @param a_size Bytes of the first part
 * @param b Second part, may be NULL when b_size is 0
 * @param b_size Bytes of the second part
 * @return Whether the record fit; a full ring refuses and keeps what it holds
 */
bool box_audio_ring_write(
    box_audio_ring_t *ring,
    const void *a,
    int32_t a_size,
    const void *b,
    int32_t b_size);

/** Copy bytes from the front of the ring without taking them. Consumer only.
 *
 * @param ring The ring
 * @param out Receives the bytes
 * @param size Bytes to copy
 * @return Whether the ring held size bytes
 */
bool box_audio_ring_peek(
    const box_audio_ring_t *ring,
    void *out,
    int32_t size);

/** Take bytes from the front of the ring, all or nothing, without blocking. Consumer only.
 *
 * @param ring The ring
 * @param out Receives the bytes, may be NULL to drop them
 * @param size Bytes to take
 * @return Whether the ring held size bytes
 */
bool box_audio_ring_read(
    box_audio_ring_t *ring,
    void *out,
    int32_t size);

#endif
