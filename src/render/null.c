/**
 * @file render/null.c
 * @brief Null backend that renders nothing and records the packets it receives.
 */

#include "render.h"

/** Remember the thread of the backend and count the call.
 *
 * @param ctx The null backend
 * @return 0
 */
static int box_render_null_init(
    void *ctx)
{
    box_render_null_t *null = ctx;
    null->inits ++;
    null->thread = ecs_os_thread_self();

    return 0;
}

/** Count a call that was not made on the thread that ran init.
 *
 * @param null The null backend
 */
static void box_render_null_check_thread(
    box_render_null_t *null)
{
    if (ecs_os_thread_self() != null->thread) {
        null->wrong_thread ++;
    }
}

/** Record a packet and keep a copy of it; the reset before the copy shrinks a large data block
 * of the copy with the same hysteresis as the packets of the ring.
 *
 * @param ctx The null backend
 * @param packet The packet
 */
static void box_render_null_render(
    void *ctx,
    const box_render_packet_t *packet)
{
    box_render_null_t *null = ctx;
    int32_t i;
    box_render_null_check_thread(null);
    if (null->packets && packet->frame != null->frame + 1) {
        null->gaps ++;
    }

    null->packets ++;
    null->frame = packet->frame;
    null->time = packet->time;
    null->delta = packet->delta;
    null->views += ecs_vec_count(&packet->views);
    null->batches += ecs_vec_count(&packet->batches);
    null->uploads += ecs_vec_count(&packet->uploads);
    null->scatters += ecs_vec_count(&packet->scatters);
    null->resources += ecs_vec_count(&packet->resources);
    null->targets += ecs_vec_count(&packet->targets);
    for (i = 0; i < ecs_vec_count(&packet->uploads); i ++) {
        null->bytes += ecs_vec_get_t(&packet->uploads, box_render_upload_t, i)->size;
    }

    box_render_packet_reset(&null->last);
    box_render_packet_copy(&null->last, packet);
}

/** Count the release of the backend.
 *
 * @param ctx The null backend
 */
static void box_render_null_fini(
    void *ctx)
{
    box_render_null_t *null = ctx;
    box_render_null_check_thread(null);
    null->finis ++;
}

box_render_null_t* box_render_null_new(void)
{
    box_render_null_t *result = ecs_os_calloc_t(box_render_null_t);
    box_render_packet_init(&result->last);

    return result;
}

void box_render_null_free(
    void *ctx)
{
    box_render_null_t *null = ctx;
    box_render_packet_fini(&null->last);
    ecs_os_free(null);
}

box_render_backend_t box_render_null_backend(
    box_render_null_t *null)
{
    box_render_backend_t result = {
        .ctx = null,
        .init = box_render_null_init,
        .render = box_render_null_render,
        .fini = box_render_null_fini
    };

    return result;
}
