/**
 * @file renderer/capture.c
 * @brief Reads a render target back to the CPU and writes it as PNG.
 */

#include "renderer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int box_capture_texture(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path)
{
#ifdef BOX_PLATFORM_WEB
    (void)r; (void)texture; (void)path;
    return -1;
#else
    int result = -1;
    uint32_t width = r->width;
    uint32_t height = r->height;
    uint32_t stride = (width * 4 + 255) & ~255u;
    uint64_t size = (uint64_t)stride * height;

    WGPUBufferDescriptor buffer_desc = {
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
        .size = size
    };
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(r->device, &buffer_desc);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    WGPUTexelCopyTextureInfo src = { .texture = texture, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferInfo dst = {
        .layout = { .bytesPerRow = stride, .rowsPerImage = height },
        .buffer = buffer
    };
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

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

    if (r->format == WGPUTextureFormat_BGRA8Unorm || r->format == WGPUTextureFormat_BGRA8UnormSrgb) {
        uint32_t y;
        for (y = 0; y < height; y ++) {
            box_bgra_to_rgba(pixels + (size_t)y * stride, width);
        }
    }

    result = box_image_write_png(path, width, height, stride, pixels);
    free(pixels);
done:
    wgpuBufferRelease(buffer);
    return result;
#endif
}
