/**
 * @file renderer/capture.c
 * @brief Reads a render target back to the CPU and writes it as PNG.
 *
 * A capture copies the target into a slot buffer and maps it asynchronously. Native encodes
 * the mapped pixels on a worker thread, the web target writes them from the map callback.
 */

#include "renderer_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef BOX_PLATFORM_WEB
#include <sys/stat.h>
#endif

/** Convert a BGRA row to RGBA in place.
 *
 * @param row Row start
 * @param width Pixels in the row
 */
static void box_bgra_to_rgba(
    uint8_t *row,
    uint32_t width)
{
    uint32_t i;
    for (i = 0; i < width; i ++) {
        uint8_t b = row[i * 4];
        row[i * 4] = row[i * 4 + 2];
        row[i * 4 + 2] = b;
    }
}

/** Convert every row of an image from BGRA to RGBA in place.
 *
 * @param pixels First row
 * @param width Pixels in a row
 * @param height Rows in the image
 * @param stride Bytes per row
 */
static void box_capture_convert(
    uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride)
{
    uint32_t y;
    for (y = 0; y < height; y ++) {
        box_bgra_to_rgba(pixels + (size_t)y * stride, width);
    }
}

/** Test whether the color format stores blue before red.
 *
 * @param r The renderer
 * @return true when rows need a channel swap
 */
static bool box_capture_is_bgra(
    const box_renderer_t *r)
{
    return r->format == WGPUTextureFormat_BGRA8Unorm ||
        r->format == WGPUTextureFormat_BGRA8UnormSrgb;
}

/** Bytes a row of the readback buffer takes, padded to the copy alignment.
 *
 * @param width Pixels in a row
 * @return Bytes per row
 */
static uint32_t box_capture_stride(
    uint32_t width)
{
    return (width * 4 + 255) & ~255u;
}

#ifdef BOX_PLATFORM_WEB
/** Create every parent directory of a file path.
 *
 * @param path File path
 */
static void box_capture_mkdir(
    const char *path)
{
    char dir[BOX_CAPTURE_PATH_MAX];
    size_t len = strlen(path);
    assert(len < sizeof(dir));
    memcpy(dir, path, len + 1);

    size_t i;
    for (i = 1; i < len; i ++) {
        if (dir[i] != '/') {
            continue;
        }

        dir[i] = '\0';
        mkdir(dir, 0777);
        dir[i] = '/';
    }
}
#endif

/** Convert the rows of a capture and write them to its file.
 *
 * The image is written next to the file and renamed over it, so a reader never
 * sees a partially written PNG.
 *
 * @param path Output file
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride Bytes per row
 * @param bgra Rows need a BGRA to RGBA swap
 * @param pixels The rows, converted in place
 * @return 0 on success, nonzero on failure
 */
static int box_capture_save(
    const char *path,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    bool bgra,
    uint8_t *pixels)
{
    if (bgra) {
        box_capture_convert(pixels, width, height, stride);
    }

#ifdef BOX_PLATFORM_WEB
    box_capture_mkdir(path);
#endif
    char part[BOX_CAPTURE_PATH_MAX + 8];
    snprintf(part, sizeof(part), "%s.part", path);
    int result = box_image_write_png(part, width, height, stride, pixels);
    if (!result && rename(part, path)) {
        fprintf(stderr, "box: cannot write '%s'\n", path);
        remove(part);
        result = -1;
    }

    return result;
}

/** Submit a copy of the texture into the buffer of a slot.
 *
 * The buffer of an earlier capture is kept when it has the size this one needs.
 *
 * @param r The renderer
 * @param c The slot, with its size fields set
 * @param texture Texture with CopySrc usage
 */
static void box_capture_copy(
    box_renderer_t *r,
    box_capture_t *c,
    WGPUTexture texture)
{
    uint64_t size = (uint64_t)c->stride * c->height;
    if (c->buffer && c->size != size) {
        wgpuBufferRelease(c->buffer);
        c->buffer = NULL;
    }

    if (!c->buffer) {
        WGPUBufferDescriptor buffer_desc = {
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = size
        };
        c->buffer = wgpuDeviceCreateBuffer(r->device, &buffer_desc);
        c->size = size;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    WGPUTexelCopyTextureInfo src = { .texture = texture, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferInfo dst = {
        .layout = { .bytesPerRow = c->stride, .rowsPerImage = c->height },
        .buffer = c->buffer
    };
    WGPUExtent3D extent = { c->width, c->height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

#ifndef BOX_PLATFORM_WEB
/** Take the next job of the worker, waiting for one until the worker stops.
 *
 * @param w The worker
 * @return The job, NULL when the worker stops
 */
static box_capture_job_t* box_capture_worker_next(
    box_capture_worker_t *w)
{
    pthread_mutex_lock(&w->lock);
    while (!w->head && !w->stop) {
        pthread_cond_wait(&w->cond, &w->lock);
    }

    box_capture_job_t *result = w->head;
    if (result) {
        w->head = result->next;
        if (!w->head) {
            w->tail = NULL;
        }
    }

    pthread_mutex_unlock(&w->lock);

    return result;
}

/** Thread entry: encode and write queued captures until stopped.
 *
 * @param arg The worker
 * @return NULL
 */
static void* box_capture_worker_main(
    void *arg)
{
    box_capture_worker_t *w = arg;
    box_capture_job_t *job;
    while ((job = box_capture_worker_next(w))) {
        box_capture_save(job->path, job->width, job->height, job->stride, job->bgra,
            job->pixels);
        free(job->pixels);
        free(job);

        pthread_mutex_lock(&w->lock);
        w->pending --;
        pthread_cond_broadcast(&w->cond);
        pthread_mutex_unlock(&w->lock);
    }

    return NULL;
}

/** Start the worker of the renderer when it is not running yet.
 *
 * @param r The renderer
 * @return 0 when the worker runs, nonzero when it could not be started
 */
static int box_capture_worker_start(
    box_renderer_t *r)
{
    int result = 0;
    box_capture_worker_t *w = &r->capture_worker;
    if (w->started) {
        goto done;
    }

    pthread_mutex_init(&w->lock, NULL);
    pthread_cond_init(&w->cond, NULL);
    if (pthread_create(&w->thread, NULL, box_capture_worker_main, w)) {
        fprintf(stderr, "box: failed to start capture worker\n");
        pthread_cond_destroy(&w->cond);
        pthread_mutex_destroy(&w->lock);
        result = -1;
        goto done;
    }

    w->started = true;
done:
    return result;
}

/** Wait until the worker holds no more than a number of jobs.
 *
 * @param r The renderer
 * @param max Jobs the worker may still hold
 */
static void box_capture_worker_wait(
    box_renderer_t *r,
    int32_t max)
{
    box_capture_worker_t *w = &r->capture_worker;
    if (!w->started) {
        goto done;
    }

    pthread_mutex_lock(&w->lock);
    while (w->pending > max) {
        pthread_cond_wait(&w->cond, &w->lock);
    }

    pthread_mutex_unlock(&w->lock);
done:
    return;
}

/** Count the frames the worker holds jobs and wait for it once it held them too long.
 *
 * This bounds the frames between a capture and its file, whatever the worker costs.
 *
 * @param r The renderer
 */
static void box_capture_worker_age(
    box_renderer_t *r)
{
    box_capture_worker_t *w = &r->capture_worker;
    if (!w->started) {
        goto done;
    }

    pthread_mutex_lock(&w->lock);
    bool pending = w->pending > 0;
    pthread_mutex_unlock(&w->lock);
    w->wait = pending ? w->wait + 1 : 0;
    if (w->wait >= BOX_CAPTURE_ENCODE_DELAY) {
        box_capture_worker_wait(r, 0);
        w->wait = 0;
    }

done:
    return;
}

/** Stop the worker once it wrote every queued capture.
 *
 * @param r The renderer
 */
static void box_capture_worker_stop(
    box_renderer_t *r)
{
    box_capture_worker_t *w = &r->capture_worker;
    if (!w->started) {
        goto done;
    }

    pthread_mutex_lock(&w->lock);
    w->stop = true;
    pthread_cond_broadcast(&w->cond);
    pthread_mutex_unlock(&w->lock);
    pthread_join(w->thread, NULL);
    pthread_cond_destroy(&w->cond);
    pthread_mutex_destroy(&w->lock);
    memset(w, 0, sizeof(box_capture_worker_t));
done:
    return;
}

/** Copy the mapped rows of a capture into a job and queue it on the worker.
 *
 * When the worker cannot be started the capture is written on the calling thread.
 *
 * @param r The renderer
 * @param c The slot, with its buffer mapped
 */
static void box_capture_hand_off(
    box_renderer_t *r,
    box_capture_t *c)
{
    size_t size = (size_t)c->size;
    const void *mapped = wgpuBufferGetConstMappedRange(c->buffer, 0, size);
    assert(mapped != NULL);
    box_capture_job_t *job = calloc(1, sizeof(box_capture_job_t));
    job->pixels = malloc(size);
    memcpy(job->pixels, mapped, size);
    wgpuBufferUnmap(c->buffer);
    job->width = c->width;
    job->height = c->height;
    job->stride = c->stride;
    job->bgra = c->bgra;
    memcpy(job->path, c->path, sizeof(job->path));

    if (box_capture_worker_start(r)) {
        box_capture_save(job->path, job->width, job->height, job->stride, job->bgra,
            job->pixels);
        free(job->pixels);
        free(job);
        goto done;
    }

    box_capture_worker_t *w = &r->capture_worker;
    pthread_mutex_lock(&w->lock);
    if (w->tail) {
        w->tail->next = job;
    } else {
        w->head = job;
    }

    w->tail = job;
    w->pending ++;
    pthread_cond_broadcast(&w->cond);
    pthread_mutex_unlock(&w->lock);
done:
    return;
}
#else
/** Copy the mapped rows of a capture out of its buffer and write them.
 *
 * @param r The renderer
 * @param c The slot, with its buffer mapped
 */
static void box_capture_hand_off(
    box_renderer_t *r,
    box_capture_t *c)
{
    (void)r;
    size_t size = (size_t)c->size;
    const void *mapped = wgpuBufferGetConstMappedRange(c->buffer, 0, size);
    assert(mapped != NULL);
    uint8_t *pixels = malloc(size);
    memcpy(pixels, mapped, size);
    wgpuBufferUnmap(c->buffer);
    box_capture_save(c->path, c->width, c->height, c->stride, c->bgra, pixels);
    free(pixels);
}
#endif

/** Hand the pixels of a mapped capture on and release its slot.
 *
 * @param status Map outcome
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 The slot
 */
static void box_on_capture_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message;
    box_renderer_t *r = userdata1;
    box_capture_t *c = userdata2;
    if (status == WGPUMapAsyncStatus_Success) {
        box_capture_hand_off(r, c);
    } else {
        fprintf(stderr, "box: failed to map capture buffer for '%s'\n", c->path);
    }

    c->busy = false;
}

/** Take a free capture slot.
 *
 * @param r The renderer
 * @return The slot, NULL when every slot is in flight
 */
static box_capture_t* box_capture_slot(
    box_renderer_t *r)
{
    box_capture_t *result = NULL;
    int32_t i;
    for (i = 0; i < BOX_CAPTURE_PENDING_MAX; i ++) {
        if (!r->captures[i].busy) {
            result = &r->captures[i];
            break;
        }
    }

    return result;
}

/** Test whether a capture still waits for its map.
 *
 * @param r The renderer
 * @return true when a slot is in flight
 */
static bool box_capture_busy(
    const box_renderer_t *r)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < BOX_CAPTURE_PENDING_MAX; i ++) {
        result |= r->captures[i].busy;
    }

    return result;
}

#ifndef BOX_PLATFORM_WEB
/** Take a free capture slot, waiting for the GPU to finish one when all are in flight.
 *
 * @param r The renderer
 * @return The slot
 */
static box_capture_t* box_capture_slot_wait(
    box_renderer_t *r)
{
    box_capture_worker_wait(r, BOX_CAPTURE_JOBS_MAX - 1);
    box_capture_t *result;
    while (!(result = box_capture_slot(r))) {
        wgpuDevicePoll(r->device, true, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }

    return result;
}
#endif

int box_capture_texture(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path)
{
    int result = -1;
#ifdef BOX_PLATFORM_WEB
    box_capture_t *c = box_capture_slot(r);
    WGPUCallbackMode mode = WGPUCallbackMode_AllowSpontaneous;
#else
    box_capture_t *c = box_capture_slot_wait(r);
    WGPUCallbackMode mode = WGPUCallbackMode_AllowProcessEvents;
#endif
    if (!c) {
        goto done;
    }

    c->busy = true;
    c->wait = 0;
    c->width = wgpuTextureGetWidth(texture);
    c->height = wgpuTextureGetHeight(texture);
    c->stride = box_capture_stride(c->width);
    c->bgra = box_capture_is_bgra(r);
    snprintf(c->path, sizeof(c->path), "%s", path);
    box_capture_copy(r, c, texture);

    WGPUBufferMapCallbackInfo info = {
        .mode = mode,
        .callback = box_on_capture_mapped,
        .userdata1 = r,
        .userdata2 = c
    };
    wgpuBufferMapAsync(c->buffer, WGPUMapMode_Read, 0, (size_t)c->size, info);
    result = 0;
done:
    return result;
}

void box_capture_poll(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    bool late = false;
    int32_t i;
    for (i = 0; i < BOX_CAPTURE_PENDING_MAX; i ++) {
        box_capture_t *c = &r->captures[i];
        if (c->busy) {
            c->wait ++;
            late |= c->wait >= BOX_CAPTURE_MAP_DELAY;
        }
    }

    if (box_capture_busy(r)) {
        wgpuDevicePoll(r->device, late, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }

    box_capture_worker_age(r);
#else
    (void)r;
#endif
}

void box_renderer_captures_wait(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    while (box_capture_busy(r)) {
        wgpuDevicePoll(r->device, true, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }

    box_capture_worker_wait(r, 0);
#else
    (void)r;
#endif
}

void box_capture_fini(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    if (r->device) {
        box_renderer_captures_wait(r);
    }

    box_capture_worker_stop(r);
#endif
    int32_t i;
    for (i = 0; i < BOX_CAPTURE_PENDING_MAX; i ++) {
        box_capture_t *c = &r->captures[i];
        if (c->buffer && !c->busy) {
            wgpuBufferRelease(c->buffer);
        }

        c->buffer = NULL;
    }
}
