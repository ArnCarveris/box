/**
 * @file texture/load.c
 * @brief Reads image files with stb_image and scales them into a size class mip chain.
 */

#include "texture_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_THREAD_LOCALS
#include <stb_image.h>

/** Edge length of the size class a request and the surface cap land in.
 *
 * @param request Requested edge length in pixels, 0 keeps the source size
 * @param width Width of the image file
 * @param height Height of the image file
 * @param cap Largest edge length the surface allows
 * @return The edge length of the size class
 */
static uint32_t box_texture_target_size(
    int32_t request,
    int32_t width,
    int32_t height,
    uint32_t cap)
{
    int32_t source = width > height ? width : height;
    uint32_t want = (uint32_t)(request > 0 ? request : source);
    if (want > cap) {
        want = cap;
    }

    uint32_t result = BOX_TEXTURE_MIN_SIZE;
    uint32_t cls;
    for (cls = 1; cls < BOX_TEXTURE_CLASSES; cls ++) {
        uint32_t size = box_texture_class_size(cls);
        if (size <= want && size <= cap) {
            result = size;
        }
    }

    return result;
}

/** Average one destination texel over the source rectangle it covers.
 *
 * @param src RGBA8 source pixels
 * @param width Source width
 * @param x0 First source column
 * @param x1 Column after the last
 * @param y0 First source row
 * @param y1 Row after the last
 * @param out Four bytes receiving the average
 */
static void box_texture_average(
    const uint8_t *src,
    uint32_t width,
    uint32_t x0,
    uint32_t x1,
    uint32_t y0,
    uint32_t y1,
    uint8_t *out)
{
    uint32_t sum[4] = {0, 0, 0, 0};
    uint32_t count = 0;
    uint32_t y;
    for (y = y0; y < y1; y ++) {
        uint32_t x;
        for (x = x0; x < x1; x ++) {
            const uint8_t *p = src + ((size_t)y * width + x) * 4;
            sum[0] += p[0];
            sum[1] += p[1];
            sum[2] += p[2];
            sum[3] += p[3];
            count ++;
        }
    }

    ecs_assert(count != 0, ECS_INTERNAL_ERROR, NULL);
    int32_t c;
    for (c = 0; c < 4; c ++) {
        out[c] = (uint8_t)(sum[c] / count);
    }
}

/** Scale an RGBA8 image to a square size with a box filter.
 *
 * @param src RGBA8 source pixels
 * @param width Source width
 * @param height Source height
 * @param size Destination edge length
 * @param dst RGBA8 destination of size times size texels
 */
static void box_texture_scale(
    const uint8_t *src,
    uint32_t width,
    uint32_t height,
    uint32_t size,
    uint8_t *dst)
{
    uint32_t y;
    for (y = 0; y < size; y ++) {
        uint32_t y0 = y * height / size;
        uint32_t y1 = (y + 1) * height / size;
        if (y1 <= y0) {
            y1 = y0 + 1;
        }

        uint32_t x;
        for (x = 0; x < size; x ++) {
            uint32_t x0 = x * width / size;
            uint32_t x1 = (x + 1) * width / size;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }

            box_texture_average(src, width, x0, x1, y0, y1, dst + ((size_t)y * size + x) * 4);
        }
    }
}

/** Build the mip chain of a square texture by averaging two by two blocks.
 *
 * @param pixels The chain buffer with level 0 filled
 * @param size Edge length of level 0
 */
static void box_texture_chain(
    uint8_t *pixels,
    uint32_t size)
{
    uint8_t *src = pixels;
    uint32_t level = size;
    while (level > 1) {
        uint8_t *dst = src + (size_t)level * level * 4;
        uint32_t half = level >> 1;
        uint32_t y;
        for (y = 0; y < half; y ++) {
            uint32_t x;
            for (x = 0; x < half; x ++) {
                box_texture_average(src, level, x * 2, x * 2 + 2, y * 2, y * 2 + 2,
                    dst + ((size_t)y * half + x) * 4);
            }
        }

        src = dst;
        level = half;
    }
}

void box_texture_image(
    const uint8_t *src,
    int32_t width,
    int32_t height,
    int32_t request,
    uint32_t cap,
    box_image_t *out)
{
    uint32_t size = box_texture_target_size(request, width, height, cap);
    out->pixels = malloc(box_texture_bytes(size));
    out->size = size;
    out->mips = box_texture_mips(size);
    out->source_width = width;
    out->source_height = height;
    box_texture_scale(src, (uint32_t)width, (uint32_t)height, size, out->pixels);
    box_texture_chain(out->pixels, size);
}

int box_texture_load(
    const char *path,
    int32_t request,
    uint32_t cap,
    box_image_t *out)
{
    int result = -1;
    int width = 0, height = 0, channels = 0;
    stbi_uc *source = stbi_load(path, &width, &height, &channels, 4);
    if (!source) {
        ecs_err("texture: cannot read '%s': %s", path, stbi_failure_reason());
        goto done;
    }

    box_texture_image(source, width, height, request, cap, out);
    stbi_image_free(source);
    result = 0;
done:
    return result;
}
