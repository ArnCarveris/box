/**
 * @file renderer/queue.c
 * @brief Packet queue implementation with pthread synchronization on native.
 */

#include "queue.h"
#include <assert.h>
#include <string.h>

#ifndef BOX_PLATFORM_WEB
#define box_queue_lock(q) pthread_mutex_lock(&(q)->lock)
#define box_queue_unlock(q) pthread_mutex_unlock(&(q)->lock)
#else
#define box_queue_lock(q) ((void)(q))
#define box_queue_unlock(q) ((void)(q))
#endif

void box_queue_init(
    box_queue_t *q)
{
    memset(q, 0, sizeof(box_queue_t));
    int32_t i;
    for (i = 0; i < BOX_QUEUE_SIZE; i ++) {
        q->free_list[i] = &q->packets[i];
    }
    q->free_count = BOX_QUEUE_SIZE;
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->free_cond, NULL);
    pthread_cond_init(&q->ready_cond, NULL);
#endif
}

void box_queue_fini(
    box_queue_t *q)
{
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->free_cond);
    pthread_cond_destroy(&q->ready_cond);
#else
    (void)q;
#endif
}

box_packet_t* box_queue_acquire(
    box_queue_t *q)
{
    box_packet_t *result = NULL;
    box_queue_lock(q);
#ifndef BOX_PLATFORM_WEB
    while (!q->free_count && !q->stopped) {
        pthread_cond_wait(&q->free_cond, &q->lock);
    }
#endif
    if (q->free_count && !q->stopped) {
        result = q->free_list[-- q->free_count];
    }
    box_queue_unlock(q);
    return result;
}

void box_queue_submit(
    box_queue_t *q,
    box_packet_t *p)
{
    box_queue_lock(q);
    assert(q->ready_count < BOX_QUEUE_SIZE);
    q->ready_list[q->ready_count ++] = p;
#ifndef BOX_PLATFORM_WEB
    pthread_cond_signal(&q->ready_cond);
#endif
    box_queue_unlock(q);
}

/** Remove and return the oldest ready packet, caller holds the lock.
 *
 * @param q The queue
 * @return The packet, or NULL when none is ready
 */
static box_packet_t* box_queue_pop_ready(
    box_queue_t *q)
{
    box_packet_t *result = NULL;
    if (q->ready_count) {
        result = q->ready_list[0];
        q->ready_count --;
        memmove(&q->ready_list[0], &q->ready_list[1],
            (size_t)q->ready_count * sizeof(box_packet_t*));
    }
    return result;
}

box_packet_t* box_queue_wait(
    box_queue_t *q)
{
    box_queue_lock(q);
#ifndef BOX_PLATFORM_WEB
    while (!q->ready_count && !q->stopped) {
        pthread_cond_wait(&q->ready_cond, &q->lock);
    }
#endif
    box_packet_t *result = box_queue_pop_ready(q);
    box_queue_unlock(q);
    return result;
}

box_packet_t* box_queue_poll(
    box_queue_t *q)
{
    box_queue_lock(q);
    box_packet_t *result = box_queue_pop_ready(q);
    box_queue_unlock(q);
    return result;
}

void box_queue_release(
    box_queue_t *q,
    box_packet_t *p)
{
    box_queue_lock(q);
    assert(q->free_count < BOX_QUEUE_SIZE);
    q->free_list[q->free_count ++] = p;
#ifndef BOX_PLATFORM_WEB
    pthread_cond_signal(&q->free_cond);
#endif
    box_queue_unlock(q);
}

void box_queue_stop(
    box_queue_t *q)
{
    box_queue_lock(q);
    q->stopped = true;
#ifndef BOX_PLATFORM_WEB
    pthread_cond_broadcast(&q->free_cond);
    pthread_cond_broadcast(&q->ready_cond);
#endif
    box_queue_unlock(q);
}
