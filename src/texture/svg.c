/**
 * @file texture/svg.c
 * @brief Rasterizes an Svg component with resvg and uploads it as a texture.
 */

#include "texture_internal.h"
#include "../shader/shader_internal.h"
#include <resvg.h>
#include <math.h>
#include <stdio.h>

/** Largest edge length an SVG is rasterized at before it is scaled to its class */
#define BOX_SVG_MAX_SIZE (BOX_TEXTURE_MAX_SIZE)

/** Dots per inch resvg converts absolute SVG units with */
#define BOX_SVG_DPI (96.0f)

/** Fallback font the engine ships, relative to the search path */
#define BOX_SVG_FONT "etc/fonts/B612-Regular.ttf"

/** Family name of the fallback font */
#define BOX_SVG_FONT_FAMILY "B612"

/** Parser and font options, shared by every rasterization and never mutated */
static resvg_options *box_svg_opts;

/** Read a file into a newly allocated buffer.
 *
 * @param path The file
 * @param size Receives the byte count
 * @return The bytes, owned by the caller, or NULL when the file cannot be read
 */
static uint8_t* box_svg_read(
    const char *path,
    long *size)
{
    uint8_t *result = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (bytes <= 0) {
        fclose(f);
        goto done;
    }

    result = ecs_os_malloc((ecs_size_t)bytes);
    size_t read = fread(result, 1, (size_t)bytes, f);
    fclose(f);
    *size = (long)read;
done:
    return result;
}

/** Load the fallback font the engine ships into the font database.
 *
 * The font makes text render the same on a machine without fonts and on the web
 * build, where the file comes out of the embedded `etc` folder.
 *
 * @param opts The options to load the font into
 */
static void box_svg_load_font(
    resvg_options *opts)
{
    char path[BOX_SHADER_PATH_MAX];
    if (!box_shader_resolve(BOX_SVG_FONT, path, sizeof(path))) {
        ecs_warn("svg: no fallback font at '%s', text needs a system font", BOX_SVG_FONT);
        goto done;
    }

    long size = 0;
    uint8_t *data = box_svg_read(path, &size);
    if (!data) {
        goto done;
    }

    resvg_options_load_font_data(opts, (const char*)data, (uintptr_t)size);
    ecs_os_free(data);
    resvg_options_set_font_family(opts, BOX_SVG_FONT_FAMILY);
    resvg_options_set_sans_serif_family(opts, BOX_SVG_FONT_FAMILY);
done:
    return;
}

/** Build the shared options once, with the system fonts and the fallback font.
 *
 * The options are read only afterwards, which is what lets a worker thread parse
 * an SVG with them while another one does the same.
 */
static void box_svg_options(void)
{
    if (box_svg_opts) {
        goto done;
    }

    box_svg_opts = resvg_options_create();
    ecs_assert(box_svg_opts != NULL, ECS_OUT_OF_MEMORY, NULL);
    resvg_options_set_dpi(box_svg_opts, BOX_SVG_DPI);
#ifndef __EMSCRIPTEN__
    resvg_options_load_system_fonts(box_svg_opts);
#endif
    box_svg_load_font(box_svg_opts);
done:
    return;
}

/** Parse the file or the inline data of an Svg component.
 *
 * @param svg The component
 * @return The parsed tree, or NULL when neither source parses
 */
static resvg_render_tree* box_svg_parse(
    const Svg *svg)
{
    resvg_render_tree *result = NULL;
    int32_t err = RESVG_OK;
    if (svg->file && svg->file[0]) {
        char path[BOX_SHADER_PATH_MAX];
        box_shader_resolve(svg->file, path, sizeof(path));
        err = resvg_parse_tree_from_file(path, box_svg_opts, &result);
        if (err != RESVG_OK) {
            ecs_err("svg: cannot parse '%s' (%d)", svg->file, err);
        }
    } else if (svg->data && svg->data[0]) {
        uintptr_t len = (uintptr_t)ecs_os_strlen(svg->data);
        err = resvg_parse_tree_from_data(svg->data, len, box_svg_opts, &result);
        if (err != RESVG_OK) {
            ecs_err("svg: cannot parse the inline data (%d)", err);
        }
    }

    if (result && err != RESVG_OK) {
        resvg_tree_destroy(result);
        result = NULL;
    }

    if (result) {
        resvg_size size = resvg_get_image_size(result);
        if (!(size.width >= 1.0f) || !(size.height >= 1.0f)) {
            ecs_err("svg: image has no size");
            resvg_tree_destroy(result);
            result = NULL;
        }
    }

    return result;
}

/** Clamp a rasterization edge length to the range resvg is called with.
 *
 * @param size The edge length in pixels
 * @return The edge length, at least one and at most BOX_SVG_MAX_SIZE
 */
static int32_t box_svg_clamp(
    int32_t size)
{
    int32_t result = size < 1 ? 1 : size;

    return result > BOX_SVG_MAX_SIZE ? BOX_SVG_MAX_SIZE : result;
}

/** Pixel size and uniform scale an SVG is rasterized at.
 *
 * A zero width or height follows the other one at the aspect ratio of the SVG,
 * both zero keep the size of the SVG. A requested size of another aspect ratio
 * fits the SVG in it, so the art is never distorted.
 *
 * @param source The size of the parsed SVG
 * @param svg The component
 * @param size Receives the width and the height in pixels
 * @param scale Receives the uniform scale resvg rasterizes with
 */
static void box_svg_target(
    resvg_size source,
    const Svg *svg,
    int32_t *size,
    float *scale)
{
    float sw = source.width;
    float sh = source.height;
    int32_t want_w = box_svg_clamp(svg->width > 0 ? svg->width : 1) * (svg->width > 0);
    int32_t want_h = box_svg_clamp(svg->height > 0 ? svg->height : 1) * (svg->height > 0);
    float by_w = want_w ? (float)want_w / sw : 1.0f;
    float by_h = want_h ? (float)want_h / sh : 1.0f;

    size[0] = want_w ? want_w : box_svg_clamp((int32_t)(sw * by_h + 0.5f));
    size[1] = want_h ? want_h : box_svg_clamp((int32_t)(sh * by_w + 0.5f));
    float fit_w = (float)size[0] / sw;
    float fit_h = (float)size[1] / sh;
    *scale = fit_w < fit_h ? fit_w : fit_h;
}

/** Turn the premultiplied pixels resvg writes into straight alpha.
 *
 * @param pixels RGBA8 texels
 * @param count Number of texels
 */
static void box_svg_unpremultiply(
    uint8_t *pixels,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        uint8_t *p = &pixels[(size_t)i * 4];
        uint32_t a = p[3];
        if (!a) {
            continue;
        }

        int32_t c;
        for (c = 0; c < 3; c ++) {
            uint32_t v = ((uint32_t)p[c] * 255u + a / 2u) / a;
            p[c] = (uint8_t)(v > 255u ? 255u : v);
        }
    }
}

/** Give a fully transparent texel the average color of its opaque neighbours.
 *
 * nanosvg defringed its output, and the mip chain and the box filter average
 * straight RGBA, so a transparent texel that keeps its black would darken the
 * edge of the artwork at every smaller size.
 *
 * @param pixels RGBA8 texels
 * @param width Width of the image
 * @param height Height of the image
 */
static void box_svg_bleed(
    uint8_t *pixels,
    int32_t width,
    int32_t height)
{
    ecs_size_t bytes = (ecs_size_t)(width * height * 4);
    uint8_t *src = ecs_os_malloc(bytes);
    ecs_os_memcpy(src, pixels, bytes);

    int32_t y;
    for (y = 0; y < height; y ++) {
        int32_t x;
        for (x = 0; x < width; x ++) {
            uint8_t *dst = &pixels[((size_t)y * width + x) * 4];
            if (dst[3]) {
                continue;
            }

            uint32_t sum[3] = {0, 0, 0};
            uint32_t n = 0;
            int32_t sy;
            for (sy = y ? y - 1 : 0; sy <= y + 1 && sy < height; sy ++) {
                int32_t sx;
                for (sx = x ? x - 1 : 0; sx <= x + 1 && sx < width; sx ++) {
                    const uint8_t *s = &src[((size_t)sy * width + sx) * 4];
                    if (!s[3]) {
                        continue;
                    }

                    sum[0] += s[0];
                    sum[1] += s[1];
                    sum[2] += s[2];
                    n ++;
                }
            }

            if (n) {
                dst[0] = (uint8_t)((sum[0] + n / 2) / n);
                dst[1] = (uint8_t)((sum[1] + n / 2) / n);
                dst[2] = (uint8_t)((sum[2] + n / 2) / n);
            }
        }
    }

    ecs_os_free(src);
}

/** Render a parsed SVG into a straight alpha RGBA8 buffer.
 *
 * @param tree The parsed SVG, destroyed by the call
 * @param svg The component
 * @param size Receives the width and the height in pixels
 * @return The texels, owned by the caller
 */
static uint8_t* box_svg_render(
    resvg_render_tree *tree,
    const Svg *svg,
    int32_t *size)
{
    resvg_size source = resvg_get_image_size(tree);
    float scale;
    box_svg_target(source, svg, size, &scale);

    uint8_t *pixels = ecs_os_calloc((ecs_size_t)(size[0] * size[1] * 4));
    resvg_transform xform = resvg_transform_identity();
    xform.a = scale;
    xform.d = scale;
    xform.e = ((float)size[0] - source.width * scale) * 0.5f;
    xform.f = ((float)size[1] - source.height * scale) * 0.5f;
    resvg_render(tree, xform, (uint32_t)size[0], (uint32_t)size[1], (char*)pixels);
    resvg_tree_destroy(tree);

    box_svg_unpremultiply(pixels, size[0] * size[1]);
    box_svg_bleed(pixels, size[0], size[1]);

    return pixels;
}

/** Rasterize an SVG and hand the pixels to the texture upload path.
 *
 * @param world The world
 * @param e The texture entity
 * @param svg The component
 */
static void box_svg_rasterize(
    ecs_world_t *world,
    ecs_entity_t e,
    const Svg *svg)
{
    resvg_render_tree *tree = box_svg_parse(svg);
    if (!tree) {
        goto done;
    }

    int32_t size[2];
    uint8_t *pixels = box_svg_render(tree, svg, size);

    uint32_t cap;
    bool keep;
    box_texture_settings(world, &cap, &keep);

    box_image_t img;
    box_texture_image(pixels, size[0], size[1],
        size[0] > size[1] ? size[0] : size[1], cap, &img);
    ecs_os_free(pixels);
    box_texture_publish(world, e, &img, keep);
done:
    return;
}

/** Svg was set: rasterize it and upload the pixels.
 *
 * @param it Iterator over the changed SVG textures
 */
static void box_svg_on_set(
    ecs_iter_t *it)
{
    Svg *svg = ecs_field(it, Svg, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        bool has_file = svg[i].file && svg[i].file[0];
        bool has_data = svg[i].data && svg[i].data[0];
        if (!has_file && !has_data) {
            continue;
        }

        if (!box_texture_claim(it->world, it->entities[i], ecs_id(Svg))) {
            continue;
        }

        box_svg_rasterize(it->world, it->entities[i], &svg[i]);
    }
}

/** Svg was removed: unload the texture it produced.
 *
 * @param it Iterator over the entities that lost their SVG
 */
static void box_svg_on_remove(
    ecs_iter_t *it)
{
    if (ecs_is_fini(it->world) || ecs_should_quit(it->world)) {
        goto done;
    }

    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_texture_release(it->world, it->stage, it->entities[i], ecs_id(Svg), deleting);
    }
done:
    return;
}

void box_svg_init(
    ecs_world_t *world)
{
    box_svg_options();

    ecs_add_pair(world, ecs_id(Svg), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Svg), EcsWith, ecs_id(TextureImpl));
    ecs_add_pair(world, ecs_id(Svg), EcsWith, ecs_id(RenderTexture));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Svg) }},
        .events = { EcsOnSet },
        .callback = box_svg_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Svg) }},
        .events = { EcsOnRemove },
        .callback = box_svg_on_remove
    });
}
