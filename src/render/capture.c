/**
 * @file render/capture.c
 * @brief Captures of a target: a copy into a readback buffer that the map callback writes as PNG.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

#include <stdio.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

/** A capture on its way from the GPU to its PNG file */
struct box_gpu_capture_t {
    box_gpu_t *gpu;                 /**< The backend */
    uint32_t target;                /**< Handle of the target the buffer returns to */
    WGPUBuffer buffer;              /**< Buffer the image is copied into */
    uint64_t size;                  /**< Bytes of the buffer */
    uint32_t width;                 /**< Width of the copied image */
    uint32_t height;                /**< Height of the copied image */
    uint32_t stride;                /**< Bytes per row of the copied image */
    bool bgra;                      /**< The pixels are in BGRA order */
    char *path;                     /**< File the PNG is written to */
};

/** Swap the red and blue channel of every pixel of an image.
 *
 * @param pixels First row
 * @param width Pixels in a row
 * @param height Rows
 * @param stride Bytes per row
 */
static void box_gpu_swizzle(
    uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride)
{
    uint32_t x, y;
    for (y = 0; y < height; y ++) {
        uint8_t *row = &pixels[(size_t)y * stride];
        for (x = 0; x < width; x ++) {
            uint8_t b = row[x * 4];
            row[x * 4] = row[x * 4 + 2];
            row[x * 4 + 2] = b;
        }
    }
}

/** Check whether a color format stores blue before red.
 *
 * @param format The format
 * @return Whether the channels of a pixel are in BGRA order
 */
static bool box_gpu_is_bgra(
    WGPUTextureFormat format)
{
    return format == WGPUTextureFormat_BGRA8Unorm ||
        format == WGPUTextureFormat_BGRA8UnormSrgb;
}

/** Encode the mapped image of a capture as a PNG file.
 *
 * @param capture The capture, whose buffer is mapped
 * @return 0 when the file was written
 */
static int box_gpu_capture_png(
    box_gpu_capture_t *capture)
{
    const void *data = wgpuBufferGetConstMappedRange(capture->buffer, 0, (size_t)capture->size);
    uint8_t *pixels = ecs_os_malloc((ecs_size_t)capture->size);
    char *part = flecs_asprintf("%s.part", capture->path);
    int result = -1;
    ecs_os_memcpy(pixels, data, (ecs_size_t)capture->size);
    wgpuBufferUnmap(capture->buffer);
    if (capture->bgra) {
        box_gpu_swizzle(pixels, capture->width, capture->height, capture->stride);
    }

    if (!stbi_write_png(part, (int)capture->width, (int)capture->height, 4, pixels,
        (int)capture->stride) || rename(part, capture->path))
    {
        ecs_err("cannot write capture '%s'", capture->path);
        remove(part);
        goto done;
    }

    result = 0;
done:
    ecs_os_free(part);
    ecs_os_free(pixels);

    return result;
}

/** Free a capture, returning its buffer to its target when the target has no idle buffer.
 *
 * @param capture The capture
 */
static void box_gpu_capture_free(
    box_gpu_capture_t *capture)
{
    box_gpu_target_t *t = ecs_map_get_deref(
        &capture->gpu->targets, box_gpu_target_t, capture->target);
    if (t && !t->readback && !t->capture) {
        t->readback = capture->buffer;
        t->readback_size = capture->size;
    } else {
        wgpuBufferRelease(capture->buffer);
    }

    ecs_os_free(capture->path);
    ecs_os_free(capture);
}

/** Write the PNG of a capture once its buffer is mapped and free the capture.
 *
 * @param status Outcome of the map
 * @param message Error text
 * @param userdata1 The box_gpu_capture_t
 * @param userdata2 Unused
 */
static void box_gpu_capture_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_capture_t *capture = userdata1;
    box_gpu_t *gpu = capture->gpu;
    int32_t i, count = ecs_vec_count(&gpu->captures);
    (void)message;
    (void)userdata2;
    for (i = 0; i < count; i ++) {
        if (ecs_vec_get_t(&gpu->captures, box_gpu_capture_t*, i)[0] == capture) {
            ecs_vec_remove_t(&gpu->captures, box_gpu_capture_t*, i);
            break;
        }
    }

    if (status != WGPUMapAsyncStatus_Success) {
        ecs_err("cannot map the capture buffer of '%s'", capture->path);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
    } else if (box_gpu_capture_png(capture)) {
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
    } else {
        box_gpu_count(gpu, BOX_GPU_STAT_CAPTURES);
    }

    box_gpu_capture_free(capture);
}

void box_gpu_capture_encode(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTexture texture)
{
    uint32_t width = wgpuTextureGetWidth(texture);
    uint32_t height = wgpuTextureGetHeight(texture);
    uint32_t stride = (width * 4 + BOX_GPU_ROW_ALIGN - 1) & ~(uint32_t)(BOX_GPU_ROW_ALIGN - 1);
    uint64_t size = (uint64_t)stride * height;
    box_gpu_capture_t *capture;
    if (!(t->usage & WGPUTextureUsage_CopySrc)) {
        ecs_err("the image of this target cannot be copied, capture skipped");
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    if (t->readback && t->readback_size != size) {
        box_gpu_capture_fini(t);
    }

    capture = ecs_os_calloc_t(box_gpu_capture_t);
    capture->gpu = gpu;
    capture->target = t->handle;
    capture->buffer = t->readback;
    capture->size = size;
    capture->width = width;
    capture->height = height;
    capture->stride = stride;
    capture->bgra = box_gpu_is_bgra(t->format);
    t->readback = NULL;
    t->readback_size = 0;
    if (!capture->buffer) {
        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = size
        };
        capture->buffer = wgpuDeviceCreateBuffer(gpu->device, &desc);
    }

    WGPUTexelCopyTextureInfo src = { .texture = texture, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferInfo dst = {
        .layout = { .bytesPerRow = stride, .rowsPerImage = height },
        .buffer = capture->buffer
    };
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    t->capture = capture;
done:
    return;
}

void box_gpu_capture_write(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const char *path)
{
    box_gpu_capture_t *capture = t->capture;
    if (!capture) {
        goto done;
    }

    t->capture = NULL;
    capture->path = ecs_os_strdup(path);
    ecs_vec_append_t(NULL, &gpu->captures, box_gpu_capture_t*)[0] = capture;
    wgpuBufferMapAsync(capture->buffer, WGPUMapMode_Read, 0, (size_t)capture->size,
        (WGPUBufferMapCallbackInfo){
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_gpu_capture_on_map,
            .userdata1 = capture
        });
done:
    return;
}

void box_gpu_capture_fini(
    box_gpu_target_t *t)
{
    if (t->readback) {
        wgpuBufferRelease(t->readback);
    }

    if (t->capture) {
        wgpuBufferRelease(t->capture->buffer);
        ecs_os_free(t->capture);
    }

    t->readback = NULL;
    t->readback_size = 0;
    t->capture = NULL;
}

void box_gpu_capture_flush(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
#ifndef __EMSCRIPTEN__
    int32_t count = ecs_vec_count(&gpu->captures);
    while (count && gpu->device) {
        wgpuDevicePoll(gpu->device, true, NULL);
        if (ecs_vec_count(&gpu->captures) == count) {
            ecs_err("%d captures did not complete", count);
            break;
        }

        count = ecs_vec_count(&gpu->captures);
    }
#endif

    if (!ecs_vec_count(&gpu->captures)) {
        ecs_vec_fini_t(NULL, &gpu->captures, box_gpu_capture_t*);
    }
}

#endif
