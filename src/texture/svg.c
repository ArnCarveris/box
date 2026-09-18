/**
 * @file texture/svg.c
 * @brief Rasterizes an Svg component with nanosvg and uploads it as a texture.
 */

#include "texture_internal.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include <nanosvg.h>

#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

/** Largest edge length an SVG is rasterized at before it is scaled to its class */
#define BOX_SVG_MAX_SIZE (BOX_TEXTURE_MAX_SIZE)

/** Dots per inch nanosvg converts absolute SVG units with */
#define BOX_SVG_DPI (96.0f)

/** Parse the file or the inline data of an Svg component.
 *
 * The inline data is parsed from a copy because nanosvg writes into the string
 * it is given, and the copy is released again because the parsed image holds no
 * pointers into it.
 *
 * @param svg The component
 * @return The parsed image, or NULL when neither source parses
 */
static NSVGimage* box_svg_parse(
    const Svg *svg)
{
    NSVGimage *result = NULL;
    if (svg->file && svg->file[0]) {
        result = nsvgParseFromFile(svg->file, "px", BOX_SVG_DPI);
        if (!result) {
            ecs_err("svg: cannot parse '%s'", svg->file);
        }
    } else if (svg->data && svg->data[0]) {
        char *copy = ecs_os_strdup(svg->data);
        result = nsvgParse(copy, "px", BOX_SVG_DPI);
        ecs_os_free(copy);
        if (!result) {
            ecs_err("svg: cannot parse the inline data");
        }
    }

    if (result && (result->width < 1.0f || result->height < 1.0f)) {
        ecs_err("svg: image has no size");
        nsvgDelete(result);
        result = NULL;
    }

    return result;
}

/** Clamp a rasterization edge length to the range nanosvg is called with.
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
 * @param image The parsed SVG
 * @param svg The component
 * @param size Receives the width and the height in pixels
 * @param scale Receives the uniform scale nanosvg rasterizes with
 */
static void box_svg_target(
    const NSVGimage *image,
    const Svg *svg,
    int32_t *size,
    float *scale)
{
    float sw = image->width;
    float sh = image->height;
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

/** Rasterize an SVG and hand the pixels to the texture upload path.
 *
 * nanosvg returns straight alpha with its own edge defringing, which is what
 * the texture path expects, so the pixels are uploaded as they come out.
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
    NSVGimage *image = box_svg_parse(svg);
    if (!image) {
        goto done;
    }

    int32_t size[2];
    float scale;
    box_svg_target(image, svg, size, &scale);

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    ecs_assert(rast != NULL, ECS_OUT_OF_MEMORY, NULL);
    uint8_t *pixels = ecs_os_calloc((ecs_size_t)(size[0] * size[1] * 4));
    float tx = ((float)size[0] - image->width * scale) * 0.5f;
    float ty = ((float)size[1] - image->height * scale) * 0.5f;
    nsvgRasterize(rast, image, tx, ty, scale, pixels, size[0], size[1], size[0] * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

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
