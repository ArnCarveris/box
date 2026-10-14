/**
 * @file render/capture.c
 * @brief Captures of a target: a copy into a readback buffer that is written as a PNG file.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

#include <stdio.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

/** Outcome of mapping a readback buffer */
typedef struct box_gpu_map_t {
    bool done;                  /**< The map callback ran */
    WGPUMapAsyncStatus status;  /**< Status the callback reported */
} box_gpu_map_t;

/** Record the outcome of a map request.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_map_t to fill
 * @param userdata2 Unused
 */
static void box_gpu_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_map_t *map = userdata1;
    (void)message;
    (void)userdata2;
    map->status = status;
    map->done = true;
}

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
    if (!(t->usage & WGPUTextureUsage_CopySrc)) {
        ecs_err("the image of this target cannot be copied, capture skipped");
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    if (t->readback && t->readback_size != size) {
        box_gpu_capture_fini(t);
    }

    if (!t->readback) {
        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = size
        };
        t->readback = wgpuDeviceCreateBuffer(gpu->device, &desc);
        t->readback_size = size;
        t->pixels = ecs_os_malloc((ecs_size_t)size);
    }

    WGPUTexelCopyTextureInfo src = { .texture = texture, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferInfo dst = {
        .layout = { .bytesPerRow = stride, .rowsPerImage = height },
        .buffer = t->readback
    };
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    t->capture_width = width;
    t->capture_height = height;
    t->capture_stride = stride;
    t->capture_pending = true;
done:
    return;
}

/** Wait until the readback buffer of a target is mapped and copy it.
 *
 * @param gpu The backend
 * @param t The target
 * @return 0 when the pixels were copied, nonzero when the map failed
 */
static int box_gpu_capture_read(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    box_gpu_map_t map = {0};
    int result = -1;
    wgpuBufferMapAsync(t->readback, WGPUMapMode_Read, 0, (size_t)t->readback_size,
        (WGPUBufferMapCallbackInfo){
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_gpu_on_map,
            .userdata1 = &map
        });
    while (!map.done) {
        wgpuDevicePoll(gpu->device, true, NULL);
    }

    if (map.status != WGPUMapAsyncStatus_Success) {
        ecs_err("cannot map the capture buffer");
        goto done;
    }

    const void *data = wgpuBufferGetConstMappedRange(t->readback, 0, (size_t)t->readback_size);
    ecs_os_memcpy(t->pixels, data, (ecs_size_t)t->readback_size);
    wgpuBufferUnmap(t->readback);
    result = 0;
done:
    return result;
}

void box_gpu_capture_write(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const char *path)
{
    char *part = NULL;
    if (!t->capture_pending) {
        goto done;
    }

    t->capture_pending = false;
    if (box_gpu_capture_read(gpu, t)) {
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    if (box_gpu_is_bgra(t->format)) {
        box_gpu_swizzle(t->pixels, t->capture_width, t->capture_height, t->capture_stride);
    }

    part = flecs_asprintf("%s.part", path);
    if (!stbi_write_png(part, (int)t->capture_width, (int)t->capture_height, 4, t->pixels,
        (int)t->capture_stride) || rename(part, path))
    {
        ecs_err("cannot write capture '%s'", path);
        remove(part);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    box_gpu_count(gpu, BOX_GPU_STAT_CAPTURES);
done:
    ecs_os_free(part);
}

void box_gpu_capture_fini(
    box_gpu_target_t *t)
{
    if (t->readback) {
        wgpuBufferRelease(t->readback);
    }

    ecs_os_free(t->pixels);
    t->readback = NULL;
    t->readback_size = 0;
    t->pixels = NULL;
}

#endif
