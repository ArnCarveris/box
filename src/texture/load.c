/**
 * @file texture/load.c
 * @brief Decodes image files into RGBA8 pixels on a worker thread.
 *
 * Raster formats decode with stb_image, SVG files render with resvg at the requested size;
 * the result is scaled to the requested size and capped to the texture quality. A linear image
 * (TexturePath.linear) has its color channels encoded to sRGB last.
 *
 * A single channel image (TexturePath.single_channel) keeps only its red channel as stored.
 */

#include "texture.h"
#include "../render/preprocess.h"
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define STBI_MALLOC(size) ecs_os_malloc((ecs_size_t)(size))
#define STBI_REALLOC(ptr, size) ecs_os_realloc(ptr, (ecs_size_t)(size))
#define STBI_FREE(ptr) ecs_os_free(ptr)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STBIR_MALLOC(size, ctx) ((void)(ctx), ecs_os_malloc((ecs_size_t)(size)))
#define STBIR_FREE(ptr, ctx) ((void)(ctx), ecs_os_free(ptr))
#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <resvg.h>

/** Resolution SVG lengths in physical units are converted with */
#define BOX_TEXTURE_SVG_DPI (96.0f)

/** Protects the creation of the shared resvg options */
static ecs_os_mutex_t box_texture_svg_lock;

/** Options every SVG is parsed with, created by the first SVG decode */
static resvg_options *box_texture_svg_options;

/** Largest file a texture is read from, in bytes */
#define BOX_TEXTURE_MAX_FILE (256 * 1024 * 1024)

/** Read a file into memory.
 *
 * @param path The file
 * @param size Receives the bytes read
 * @return The bytes, NULL when the file cannot be read
 */
static uint8_t* box_texture_read(
    const char *path,
    int32_t *size)
{
    uint8_t *result = NULL;
    long len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    if (!fseek(f, 0, SEEK_END)) {
        len = ftell(f);
    }

    if (len <= 0 || len > BOX_TEXTURE_MAX_FILE || fseek(f, 0, SEEK_SET)) {
        goto close;
    }

    result = ecs_os_malloc((ecs_size_t)len);
    if (fread(result, 1, (size_t)len, f) != (size_t)len) {
        ecs_os_free(result);
        result = NULL;
        goto close;
    }

    *size = (int32_t)len;
close:
    fclose(f);
done:
    return result;
}

/** Check whether a file is an SVG by its extension.
 *
 * @param path The file
 * @return Whether the path ends in .svg or .svgz, in any case
 */
static bool box_texture_is_svg(
    const char *path)
{
    const char *ext = strrchr(path, '.');
    char lower[6] = {0};
    int32_t i;
    for (i = 0; ext && ext[i] && i < 5; i ++) {
        lower[i] = (char)tolower((unsigned char)ext[i]);
    }

    return ext && (!ecs_os_strcmp(lower, ".svg") || !ecs_os_strcmp(lower, ".svgz"));
}

/** Compute the size of a decoded image from its source size, the request and the cap.
 *
 * @param job The job with the request and the quality
 * @param src_width Width of the source
 * @param src_height Height of the source
 * @param width Receives the width
 * @param height Receives the height
 */
static void box_texture_size(
    const box_texture_job_t *job,
    double src_width,
    double src_height,
    int32_t *width,
    int32_t *height)
{
    double w = src_width, h = src_height;
    if (job->request_width > 0 && job->request_height > 0) {
        w = job->request_width;
        h = job->request_height;
    } else if (job->request_width > 0) {
        w = job->request_width;
        h = src_height * w / src_width;
    } else if (job->request_height > 0) {
        h = job->request_height;
        w = src_width * h / src_height;
    }

    double largest = w > h ? w : h;
    if (job->quality > 0 && largest > job->quality) {
        w *= job->quality / largest;
        h *= job->quality / largest;
    }

    *width = w < 1 ? 1 : (int32_t)floor(w + 0.5);
    *height = h < 1 ? 1 : (int32_t)floor(h + 0.5);
}

/** Decode a raster image with stb_image and scale it to its final size.
 *
 * @param job The job, receives the pixels or the error
 * @param data The bytes of the file
 * @param size Bytes of the file
 */
static void box_texture_decode_raster(
    box_texture_job_t *job,
    const uint8_t *data,
    int32_t size)
{
    int w = 0, h = 0, n = 0;
    uint8_t *src = stbi_load_from_memory(data, size, &w, &h, &n, 4);
    if (!src) {
        job->error = flecs_asprintf("cannot decode image '%s': %s",
            job->path, stbi_failure_reason());
        goto done;
    }

    box_texture_size(job, w, h, &job->width, &job->height);
    if (job->width == w && job->height == h) {
        job->pixels = src;
        goto done;
    }

    job->pixels = ecs_os_malloc(job->width * job->height * 4);
    stbir_resize_uint8_linear(src, w, h, 0, job->pixels, job->width, job->height, 0, STBIR_RGBA);
    stbi_image_free(src);
done:
    return;
}

/** Get the options SVGs are parsed with, creating them on first use.
 *
 * @return The options
 */
static const resvg_options* box_texture_svg_opts(void)
{
    ecs_os_mutex_lock(box_texture_svg_lock);
    if (!box_texture_svg_options) {
        box_texture_svg_options = resvg_options_create();
        resvg_options_set_dpi(box_texture_svg_options, BOX_TEXTURE_SVG_DPI);
        resvg_options_load_system_fonts(box_texture_svg_options);
    }

    ecs_os_mutex_unlock(box_texture_svg_lock);

    return box_texture_svg_options;
}

/** Turn premultiplied RGBA8 pixels into straight alpha.
 *
 * @param pixels The pixels
 * @param count Number of pixels
 */
static void box_texture_unpremultiply(
    uint8_t *pixels,
    int32_t count)
{
    int32_t i, c;
    for (i = 0; i < count; i ++) {
        uint8_t *p = &pixels[i * 4];
        int32_t a = p[3];
        if (!a || a == 255) {
            continue;
        }

        for (c = 0; c < 3; c ++) {
            int32_t v = (p[c] * 255 + a / 2) / a;
            p[c] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
}

/** Render an SVG with resvg at its final size.
 *
 * @param job The job, receives the pixels or the error
 * @param data The bytes of the file
 * @param size Bytes of the file
 */
static void box_texture_decode_svg(
    box_texture_job_t *job,
    const uint8_t *data,
    int32_t size)
{
    resvg_render_tree *tree = NULL;
    int32_t err = resvg_parse_tree_from_data(
        (const char*)data, (uintptr_t)size, box_texture_svg_opts(), &tree);
    if (err != RESVG_OK) {
        job->error = flecs_asprintf("cannot parse svg '%s' (resvg error %d)", job->path, err);
        goto done;
    }

    resvg_size src = resvg_get_image_size(tree);
    if (src.width <= 0 || src.height <= 0) {
        job->error = flecs_asprintf("svg '%s' has no size", job->path);
        goto free;
    }

    box_texture_size(job, src.width, src.height, &job->width, &job->height);
    resvg_transform transform = resvg_transform_identity();
    transform.a = (float)job->width / src.width;
    transform.d = (float)job->height / src.height;
    job->pixels = ecs_os_calloc(job->width * job->height * 4);
    resvg_render(tree, transform, (uint32_t)job->width, (uint32_t)job->height,
        (char*)job->pixels);
    box_texture_unpremultiply(job->pixels, job->width * job->height);
free:
    resvg_tree_destroy(tree);
done:
    return;
}

void box_texture_encode_linear(
    uint8_t *pixels,
    int32_t count)
{
    uint8_t table[256];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        double v = (double)i / 255.0;
        v = v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
        table[i] = (uint8_t)floor(v * 255.0 + 0.5);
    }

    for (i = 0; i < count; i ++) {
        for (c = 0; c < 3; c ++) {
            pixels[i * 4 + c] = table[pixels[i * 4 + c]];
        }
    }
}

void box_texture_red_channel(
    uint8_t *pixels,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        pixels[i] = pixels[i * 4];
    }
}

void box_texture_decode_init(void)
{
    if (!box_texture_svg_lock) {
        box_texture_svg_lock = ecs_os_mutex_new();
    }
}

void box_texture_decode(
    box_texture_job_t *job)
{
    int32_t size = 0;
    char *file = box_shader_locate(job->path);
    if (file) {
        box_file_stamp(file, &job->stamp);
    }

    uint8_t *data = file ? box_texture_read(file, &size) : NULL;
    job->file = file;
    if (!data) {
        job->error = flecs_asprintf("cannot read image '%s'", job->path ? job->path : "");
        goto done;
    }

    if (box_texture_is_svg(job->path)) {
        box_texture_decode_svg(job, data, size);
    } else {
        box_texture_decode_raster(job, data, size);
    }

    ecs_os_free(data);
    if (job->single_channel && job->pixels) {
        box_texture_red_channel(job->pixels, job->width * job->height);
    } else if (job->linear && job->pixels) {
        box_texture_encode_linear(job->pixels, job->width * job->height);
    }

done:
    return;
}

float* box_texture_decode_float(
    const char *path,
    int32_t max_width,
    int32_t *width,
    int32_t *height,
    char **error,
    char **file,
    box_file_stamp_t *stamp)
{
    int w = 0, h = 0, n = 0;
    int32_t size = 0;
    float *result = NULL;
    char *found = path ? box_shader_locate(path) : NULL;
    if (found && stamp) {
        box_file_stamp(found, stamp);
    }

    uint8_t *data = found ? box_texture_read(found, &size) : NULL;
    if (file) {
        *file = found;
    } else {
        ecs_os_free(found);
    }

    if (!data) {
        *error = flecs_asprintf("cannot read image '%s'", path ? path : "");
        goto done;
    }

    result = stbi_loadf_from_memory(data, size, &w, &h, &n, 4);
    ecs_os_free(data);
    if (!result) {
        *error = flecs_asprintf("cannot decode image '%s': %s", path, stbi_failure_reason());
        goto done;
    }

    *width = w;
    *height = h;
    if (max_width > 0 && w > max_width) {
        int32_t nh = (int32_t)floor((double)h * max_width / w + 0.5);
        nh = nh < 1 ? 1 : nh;
        float *scaled = ecs_os_malloc((ecs_size_t)((size_t)max_width * nh * 4 * sizeof(float)));
        stbir_resize_float_linear(result, w, h, 0, scaled, max_width, nh, 0, STBIR_4CHANNEL);
        stbi_image_free(result);
        result = scaled;
        *width = max_width;
        *height = nh;
    }

done:
    return result;
}
