/**
 * @file Ring.c
 * @brief The lock-free ring of the native transport: order, refusal, wrap around and a stress
 * of a producer and a consumer thread.
 */

#include "util.h"
#include <flecs.h>
#include <ring.h>
#include <pthread.h>
#include <sched.h>

/* The cases without a world call no flecs function that would set up the OS API they allocate
 * and log with. */
void Ring_setup(void) {
    ecs_os_set_api_defaults();
}

void Ring_order(void) {
    // audio/native
    box_audio_ring_t ring;
    int32_t i, value;
    test_int(box_audio_ring_init(&ring, 1024), 0);
    for (i = 0; i < 100; i ++) {
        test_bool(box_audio_ring_write(&ring, &i, 4, NULL, 0), true);
    }

    test_int(box_audio_ring_used(&ring), 400);
    for (i = 0; i < 100; i ++) {
        test_bool(box_audio_ring_read(&ring, &value, 4), true);
        test_int(value, i);
    }

    test_int(box_audio_ring_used(&ring), 0);
    test_bool(box_audio_ring_read(&ring, &value, 4), false);
    box_audio_ring_fini(&ring);
}

void Ring_full_refuses(void) {
    // audio/native
    box_audio_ring_t ring;
    uint8_t record[16], out[16];
    int32_t written = 0, i;
    test_int(box_audio_ring_init(&ring, 64), 0);
    test_int(box_audio_ring_room(&ring), 64);
    for (i = 0; i < 16; i ++) {
        memset(record, i, sizeof(record));
        if (!box_audio_ring_write(&ring, record, 16, NULL, 0)) {
            break;
        }

        written ++;
    }

    test_int(written, 4);
    test_int(box_audio_ring_room(&ring), 0);
    test_bool(box_audio_ring_write(&ring, record, 1, NULL, 0), false);
    test_int(box_audio_ring_used(&ring), 64);

    test_bool(box_audio_ring_read(&ring, out, 16), true);
    test_int(out[0], 0);
    test_bool(box_audio_ring_write(&ring, record, 17, NULL, 0), false);
    test_int(box_audio_ring_used(&ring), 48);
    test_bool(box_audio_ring_write(&ring, record, 16, NULL, 0), true);
    for (i = 1; i < 5; i ++) {
        test_bool(box_audio_ring_read(&ring, out, 16), true);
        test_int(out[15], i);
    }

    test_bool(box_audio_ring_write(&ring, record, 8, NULL, 0), true);
    test_bool(box_audio_ring_read(&ring, out, 9), false);
    test_int(box_audio_ring_used(&ring), 8);
    box_audio_ring_fini(&ring);
}

void Ring_wrap_around(void) {
    // audio/native
    box_audio_ring_t ring;
    uint8_t record[24], out[24];
    int32_t i, j;
    test_int(box_audio_ring_init(&ring, 64), 0);
    for (i = 0; i < 1000; i ++) {
        for (j = 0; j < 24; j ++) {
            record[j] = (uint8_t)(i * 7 + j);
        }

        test_bool(box_audio_ring_write(&ring, record, 24, NULL, 0), true);
        memset(out, 0, sizeof(out));
        test_bool(box_audio_ring_peek(&ring, out, 24), true);
        test_assert(memcmp(record, out, 24) == 0);
        memset(out, 0, sizeof(out));
        test_bool(box_audio_ring_read(&ring, out, 24), true);
        test_assert(memcmp(record, out, 24) == 0);
    }

    test_int(box_audio_ring_used(&ring), 0);
    test_int(box_audio_ring_room(&ring), 64);
    box_audio_ring_fini(&ring);
}

void Ring_two_parts(void) {
    // audio/native
    box_audio_ring_t ring;
    uint32_t header = 0xA1B2C3D4u, payload[3] = { 1, 2, 3 }, out[4];
    test_int(box_audio_ring_init(&ring, 64), 0);
    test_bool(box_audio_ring_write(&ring, &header, 4, payload, 12), true);
    test_int(box_audio_ring_used(&ring), 16);
    test_bool(box_audio_ring_read(&ring, out, 16), true);
    test_uint(out[0], 0xA1B2C3D4u);
    test_uint(out[1], 1);
    test_uint(out[3], 3);

    test_bool(box_audio_ring_write(&ring, payload, 12, payload, 12), true);
    test_bool(box_audio_ring_write(&ring, payload, 12, payload, 12), true);
    test_bool(box_audio_ring_write(&ring, &header, 4, out, 16), false);
    test_int(box_audio_ring_used(&ring), 48);
    test_bool(box_audio_ring_write(&ring, &header, 4, payload, 12), true);
    test_int(box_audio_ring_room(&ring), 0);
    box_audio_ring_fini(&ring);
}

/** Records the stress test sends */
#define RING_STRESS_RECORDS 1000000

/** State shared by the two threads of the stress test */
typedef struct ring_stress_t {
    box_audio_ring_t ring;   /**< The ring */
    uint64_t sent;       /**< Checksum of the producer */
    uint64_t received;   /**< Checksum of the consumer */
    int32_t count;       /**< Records the consumer took */
    int32_t errors;      /**< Records out of order or damaged */
    int32_t refused;     /**< Writes a full ring refused */
} ring_stress_t;

/** Next value of a xorshift generator.
 *
 * @param state The state
 * @return The value
 */
static uint32_t ring_random(
    uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return x;
}

static void* ring_producer(
    void *arg)
{
    ring_stress_t *s = arg;
    uint32_t random = 12345, payload[16];
    int32_t i, j;
    for (i = 0; i < RING_STRESS_RECORDS; i ++) {
        uint32_t words = 1 + ring_random(&random) % 15;
        uint32_t header = words;
        payload[0] = (uint32_t)i;
        for (j = 1; j < (int32_t)words; j ++) {
            payload[j] = ring_random(&random);
        }

        for (j = 0; j < (int32_t)words; j ++) {
            s->sent = s->sent * 31 + payload[j];
        }

        while (!box_audio_ring_write(&s->ring, &header, 4, payload, (int32_t)words * 4)) {
            s->refused ++;
            sched_yield();
        }
    }

    return NULL;
}

static void* ring_consumer(
    void *arg)
{
    ring_stress_t *s = arg;
    uint32_t header, payload[16];
    int32_t j;
    while (s->count < RING_STRESS_RECORDS) {
        if (!box_audio_ring_peek(&s->ring, &header, 4)) {
            sched_yield();
            continue;
        }

        if (header < 1 || header > 15 ||
            !box_audio_ring_read(&s->ring, NULL, 4) ||
            !box_audio_ring_read(&s->ring, payload, (int32_t)header * 4))
        {
            s->errors ++;
            break;
        }

        if (payload[0] != (uint32_t)s->count) {
            s->errors ++;
        }

        for (j = 0; j < (int32_t)header; j ++) {
            s->received = s->received * 31 + payload[j];
        }

        s->count ++;
    }

    return NULL;
}

void Ring_stress(void) {
    // audio/native
    static ring_stress_t s;
    pthread_t producer, consumer;
    memset(&s, 0, sizeof(s));
    test_int(box_audio_ring_init(&s.ring, 256), 0);
    test_int(pthread_create(&consumer, NULL, ring_consumer, &s), 0);
    test_int(pthread_create(&producer, NULL, ring_producer, &s), 0);
    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    test_int(s.errors, 0);
    test_int(s.count, RING_STRESS_RECORDS);
    test_assert(s.sent == s.received);
    test_int(box_audio_ring_used(&s.ring), 0);
    box_audio_ring_fini(&s.ring);
}
