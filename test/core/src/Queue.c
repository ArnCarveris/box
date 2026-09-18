/**
 * @file Queue.c
 * @brief Tests for the renderer packet queue.
 */

#include <bake_test.h>
#include <pthread.h>
#include <unistd.h>
#include "../../../src/renderer/queue.h"

void Queue_init_all_free(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    test_int(q.free_count, BOX_QUEUE_SIZE);
    test_int(q.ready_count, 0);
    test_bool(q.stopped, false);
    box_queue_fini(&q);
}

void Queue_acquire_submit_wait_release(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    box_packet_t *p = box_queue_acquire(&q);
    test_assert(p != NULL);
    test_int(q.free_count, BOX_QUEUE_SIZE - 1);
    p->frame = 42;
    box_queue_submit(&q, p);
    test_int(q.ready_count, 1);
    box_packet_t *r = box_queue_wait(&q);
    test_assert(r == p);
    test_int(r->frame, 42);
    test_int(q.ready_count, 0);
    box_queue_release(&q, r);
    test_int(q.free_count, BOX_QUEUE_SIZE);
    box_queue_fini(&q);
}

void Queue_poll_returns_null_when_empty(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    test_assert(box_queue_poll(&q) == NULL);
    box_packet_t *p = box_queue_acquire(&q);
    box_queue_submit(&q, p);
    test_assert(box_queue_poll(&q) == p);
    test_assert(box_queue_poll(&q) == NULL);
    box_queue_fini(&q);
}

void Queue_fifo_order(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    box_packet_t *a = box_queue_acquire(&q);
    box_packet_t *b = box_queue_acquire(&q);
    box_packet_t *c = box_queue_acquire(&q);
    a->frame = 1; b->frame = 2; c->frame = 3;
    box_queue_submit(&q, a);
    box_queue_submit(&q, b);
    box_queue_submit(&q, c);
    test_int(box_queue_wait(&q)->frame, 1);
    test_int(box_queue_wait(&q)->frame, 2);
    test_int(box_queue_wait(&q)->frame, 3);
    box_queue_fini(&q);
}

void Queue_acquire_returns_null_after_stop(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    box_queue_stop(&q);
    test_assert(box_queue_acquire(&q) == NULL);
    test_assert(box_queue_wait(&q) == NULL);
    box_queue_fini(&q);
}

static void* release_later(
    void *arg)
{
    box_queue_t *q = arg;
    usleep(50000);
    box_packet_t *p = box_queue_wait(q);
    box_queue_release(q, p);

    return NULL;
}

void Queue_acquire_blocks_until_release(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    int i;
    for (i = 0; i < BOX_QUEUE_SIZE; i ++) {
        box_queue_submit(&q, box_queue_acquire(&q));
    }

    test_int(q.free_count, 0);
    pthread_t thread;
    pthread_create(&thread, NULL, release_later, &q);
    box_packet_t *p = box_queue_acquire(&q);
    test_assert(p != NULL);
    pthread_join(thread, NULL);
    box_queue_fini(&q);
}

static void* submit_later(
    void *arg)
{
    box_queue_t *q = arg;
    usleep(50000);
    box_packet_t *p = box_queue_acquire(q);
    p->frame = 9;
    box_queue_submit(q, p);

    return NULL;
}

void Queue_wait_blocks_until_submit(void)
{
    // engine_core/surface
    box_queue_t q;
    box_queue_init(&q);
    pthread_t thread;
    pthread_create(&thread, NULL, submit_later, &q);
    box_packet_t *p = box_queue_wait(&q);
    test_assert(p != NULL);
    test_int(p->frame, 9);
    pthread_join(thread, NULL);
    box_queue_fini(&q);
}
