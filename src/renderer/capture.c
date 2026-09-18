/**
 * @file renderer/capture.c
 * @brief Reads a render target back to the CPU and writes it as PNG.
 *
 * Native maps the readback buffer with a blocking poll loop, the web target
 * queues the capture and writes it from the map callback of the browser.
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

/** Create a mappable buffer and submit a copy of the texture into it.
 *
 * @param r The renderer
 * @param texture Texture with CopySrc usage
 * @param stride Bytes per row in the buffer
 * @param size Bytes the buffer holds
 * @return The buffer the copy writes to
 */
static WGPUBuffer box_capture_copy(
    box_renderer_t *r,
    WGPUTexture texture,
    uint32_t stride,
    uint64_t size)
{
    WGPUBufferDescriptor buffer_desc = {
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
        .size = size
    };
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(r->device, &buffer_desc);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    WGPUTexelCopyTextureInfo src = { .texture = texture, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferInfo dst = {
        .layout = { .bytesPerRow = stride, .rowsPerImage = r->height },
        .buffer = buffer
    };
    WGPUExtent3D extent = { r->width, r->height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    return buffer;
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

/** Copy the mapped rows out of a finished capture and write them as PNG.
 *
 * @param c The capture
 */
static void box_capture_write(
    box_capture_t *c)
{
    size_t size = (size_t)c->stride * c->height;
    const void *mapped = wgpuBufferGetConstMappedRange(c->buffer, 0, size);
    assert(mapped != NULL);
    uint8_t *pixels = malloc(size);
    memcpy(pixels, mapped, size);
    wgpuBufferUnmap(c->buffer);

    if (c->bgra) {
        box_capture_convert(pixels, c->width, c->height, c->stride);
    }

    box_capture_mkdir(c->path);
    box_image_write_png(c->path, c->width, c->height, c->stride, pixels);
    free(pixels);
}

/** Write the image of a mapped capture and release its slot.
 *
 * @param status Map outcome
 * @param message Error text
 * @param userdata1 The capture
 * @param userdata2 Unused
 */
static void box_on_capture_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message; (void)userdata2;
    box_capture_t *c = userdata1;
    if (status == WGPUMapAsyncStatus_Success) {
        box_capture_write(c);
    } else {
        fprintf(stderr, "box: failed to map capture buffer for '%s'\n", c->path);
    }

    wgpuBufferRelease(c->buffer);
    c->buffer = NULL;
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

/** Queue a capture: submit the copy and map its buffer asynchronously.
 *
 * @param r The renderer
 * @param texture Texture with CopySrc usage
 * @param path Output file
 * @return 0 when the capture was queued, nonzero when no slot was free
 */
static int box_capture_queue(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path)
{
    int result = -1;
    box_capture_t *c = box_capture_slot(r);
    if (!c) {
        goto done;
    }

    c->busy = true;
    c->width = r->width;
    c->height = r->height;
    c->stride = box_capture_stride(r->width);
    c->bgra = box_capture_is_bgra(r);
    snprintf(c->path, sizeof(c->path), "%s", path);

    uint64_t size = (uint64_t)c->stride * c->height;
    c->buffer = box_capture_copy(r, texture, c->stride, size);

    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_on_capture_mapped,
        .userdata1 = c
    };
    wgpuBufferMapAsync(c->buffer, WGPUMapMode_Read, 0, (size_t)size, info);
    result = 0;
done:
    return result;
}
#else
/** Mark a buffer map request as finished.
 *
 * @param status Map outcome
 * @param message Error text
 * @param userdata1 Pointer to a bool set to true
 * @param userdata2 Pointer to a bool set to true on failure
 */
static void box_on_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message;
    *(bool*)userdata1 = true;
    if (status != WGPUMapAsyncStatus_Success) {
        *(bool*)userdata2 = true;
    }
}

/** Submit the copy, wait for the map and write the PNG.
 *
 * @param r The renderer
 * @param texture Texture with CopySrc usage
 * @param path Output file
 * @return 0 on success, nonzero on failure
 */
static int box_capture_blocking(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path)
{
    int result = -1;
    uint32_t width = r->width;
    uint32_t height = r->height;
    uint32_t stride = box_capture_stride(width);
    uint64_t size = (uint64_t)stride * height;
    WGPUBuffer buffer = box_capture_copy(r, texture, stride, size);

    bool done = false, failed = false;
    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = box_on_mapped,
        .userdata1 = &done,
        .userdata2 = &failed
    };
    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, (size_t)size, info);
    while (!done) {
        wgpuDevicePoll(r->device, true, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }

    if (failed) {
        fprintf(stderr, "box: failed to map capture buffer\n");
        goto done;
    }

    const void *mapped = wgpuBufferGetConstMappedRange(buffer, 0, (size_t)size);
    uint8_t *pixels = malloc((size_t)size);
    memcpy(pixels, mapped, (size_t)size);
    wgpuBufferUnmap(buffer);

    if (box_capture_is_bgra(r)) {
        box_capture_convert(pixels, width, height, stride);
    }

    result = box_image_write_png(path, width, height, stride, pixels);
    free(pixels);
done:
    wgpuBufferRelease(buffer);

    return result;
}
#endif

int box_capture_texture(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path)
{
#ifdef BOX_PLATFORM_WEB
    return box_capture_queue(r, texture, path);
#else
    return box_capture_blocking(r, texture, path);
#endif
}
