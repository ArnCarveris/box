/**
 * @file renderer/renderer.c
 * @brief Renderer lifecycle, thread and packet handoff.
 */

#include "renderer_internal.h"
#include <stdlib.h>
#ifdef BOX_PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif
#include <stdio.h>
#include <string.h>

/** Render one packet if the device is ready, then return it to the queue.
 *
 * @param r The renderer
 * @param p The packet
 */
static void box_renderer_consume(
    box_renderer_t *r,
    box_packet_t *p)
{
    if (r->ready) {
        box_frame_render(r, p);
        r->frames ++;
#ifdef BOX_PLATFORM_WEB
        if (r->frames == 1) {
            emscripten_run_script("window.dispatchEvent(new Event('box-first-frame'))");
        }
#endif
    }
    box_queue_release(&r->queue, p);
}

#ifndef BOX_PLATFORM_WEB
/** Thread entry: initialize the device, then render packets until stopped.
 *
 * @param arg The renderer
 * @return NULL
 */
static void* box_renderer_thread(
    void *arg)
{
    box_renderer_t *r = arg;
    if (box_device_init(r)) {
        r->failed = true;
        box_queue_stop(&r->queue);
    } else {
        box_frame_init(r);
        r->ready = true;
    }

    box_packet_t *p;
    while ((p = box_queue_wait(&r->queue))) {
        box_renderer_consume(r, p);
    }

    return NULL;
}
#endif

box_renderer_t* box_renderer_create(
    const box_renderer_desc_t *desc)
{
    box_renderer_t *r = calloc(1, sizeof(box_renderer_t));
    r->desc = *desc;
    r->desc.canvas_selector = NULL;
    if (desc->canvas_selector) {
        size_t len = strlen(desc->canvas_selector) + 1;
        char *copy = malloc(len);
        memcpy(copy, desc->canvas_selector, len);
        r->desc.canvas_selector = copy;
    }
    box_queue_init(&r->queue);

#ifndef BOX_PLATFORM_WEB
    if (desc->threaded) {
        if (pthread_create(&r->thread, NULL, box_renderer_thread, r)) {
            fprintf(stderr, "box: failed to start renderer thread\n");
            box_renderer_destroy(r);
            r = NULL;
        } else {
            r->thread_started = true;
        }
        goto done;
    }
#endif

    if (box_device_init(r)) {
        r->failed = true;
#ifndef BOX_PLATFORM_WEB
        box_renderer_destroy(r);
        r = NULL;
#endif
    } else {
#ifndef BOX_PLATFORM_WEB
        box_frame_init(r);
        r->ready = true;
#endif
    }

#ifndef BOX_PLATFORM_WEB
done:
#endif
    return r;
}

void box_renderer_destroy(
    box_renderer_t *r)
{
    box_queue_stop(&r->queue);
#ifndef BOX_PLATFORM_WEB
    if (r->thread_started) {
        pthread_join(r->thread, NULL);
    }
#endif
    box_device_fini(r);
    box_queue_fini(&r->queue);
    free((void*)r->desc.canvas_selector);
    free(r);
}

box_packet_t* box_renderer_acquire(
    box_renderer_t *r)
{
    box_packet_t *p = box_queue_acquire(&r->queue);
    if (p) {
        memset(p, 0, sizeof(box_packet_t));
    }
    return p;
}

void box_renderer_submit(
    box_renderer_t *r,
    box_packet_t *p)
{
    box_queue_submit(&r->queue, p);
#ifndef BOX_PLATFORM_WEB
    if (r->desc.threaded) {
        return;
    }
#endif
    box_packet_t *ready;
    while ((ready = box_queue_poll(&r->queue))) {
        box_renderer_consume(r, ready);
    }
}

bool box_renderer_ready(
    const box_renderer_t *r)
{
    return r->ready;
}

uint32_t box_renderer_frames(
    const box_renderer_t *r)
{
    return r->frames;
}
