/**
 * @file renderer/queue.h
 * @brief Fixed size packet queue with a free list and a ready list.
 *
 * Producers acquire free packets and submit them, the consumer waits for ready ones.
 */

#ifndef BOX_RENDERER_QUEUE_H
#define BOX_RENDERER_QUEUE_H

#include "packet.h"

#ifndef BOX_PLATFORM_WEB
#include <pthread.h>
#endif

/** Number of packets in a queue */
#define BOX_QUEUE_SIZE (3)

/** Packet ring shared by the main thread and the renderer */
typedef struct box_queue_t {
    box_packet_t packets[BOX_QUEUE_SIZE];      /**< Packet storage */
    box_packet_t *free_list[BOX_QUEUE_SIZE];   /**< Packets available to producers */
    box_packet_t *ready_list[BOX_QUEUE_SIZE];  /**< Packets submitted, oldest first */
    int32_t free_count;                        /**< Entries in free_list */
    int32_t ready_count;                       /**< Entries in ready_list */
    bool stopped;                              /**< Set to wake waiters and refuse work */
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_t lock;                      /**< Protects the lists */
    pthread_cond_t free_cond;                  /**< Signalled when a packet is released */
    pthread_cond_t ready_cond;                 /**< Signalled when a packet is submitted */
#endif
} box_queue_t;

/** Initialize a queue with all packets free.
 *
 * @param q The queue
 */
void box_queue_init(
    box_queue_t *q);

/** Release queue resources.
 *
 * @param q The queue
 */
void box_queue_fini(
    box_queue_t *q);

/** Take a free packet, blocking until one is available.
 *
 * @param q The queue
 * @return A packet, or NULL when the queue is stopped
 */
box_packet_t* box_queue_acquire(
    box_queue_t *q);

/** Submit an acquired packet to the consumer.
 *
 * @param q The queue
 * @param p The packet
 */
void box_queue_submit(
    box_queue_t *q,
    box_packet_t *p);

/** Take the oldest ready packet, blocking until one is available.
 *
 * @param q The queue
 * @return A packet, or NULL when the queue is stopped and drained
 */
box_packet_t* box_queue_wait(
    box_queue_t *q);

/** Take the oldest ready packet without blocking.
 *
 * @param q The queue
 * @return A packet, or NULL when none is ready
 */
box_packet_t* box_queue_poll(
    box_queue_t *q);

/** Return a consumed packet to the free list.
 *
 * @param q The queue
 * @param p The packet
 */
void box_queue_release(
    box_queue_t *q,
    box_packet_t *p);

/** Stop the queue and wake all waiters.
 *
 * @param q The queue
 */
void box_queue_stop(
    box_queue_t *q);

#endif
