/**
 * @file TextureNodesImage.c
 * @brief Tests for the TexImage and TexLut nodes of TextureGraph on the GPU backend: a known image
 * sampled on both domains and looked up as a table, graphs that sample other graphs evaluated
 * after them and again when their image changes, rebinding without a compile, and the cycle,
 * loading and failure cases.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** An 8x4 RGBA image whose texel (x, y) is (16 + 32 x, 32 + 64 y, 255 - 24 (x + y), 255) */
#define IMAGE_8X4 "test/assets/textures/graph/image_8x4.png"

/** A quad whose emission samples a graph that samples an animated graph */
#define GRAPH_IMAGE_ANIMATED "test/assets/textures/graph/image_animated.flecs"

/** Graphs that sample a TextureGen and the texture of a Surface that clears to red */
#define GRAPH_IMAGE_SOURCES "test/assets/textures/graph/image_sources.flecs"

/** Width of IMAGE_8X4 */
#define TI_W (8)

/** Height of IMAGE_8X4 */
#define TI_H (4)

/** Frames a test waits at most for an outcome */
#define TG_FRAMES (40)

/** Difference a stored RGBA8 channel may have from its value */
#define TG_TOLERANCE_8 (1.0f / 255.0f + 1e-4f)

/** Difference a half float channel of a value up to 1 may have from its value */
#define TG_TOLERANCE_16 (2e-3f)

/** Node of a neighbourhood kind whose value is its input at the texel to the right */
typedef struct TgShift {
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TgShift;

/** WGSL of TgShift, which reads its materialized input with the shared helpers */
static const char *tg_shift_wgsl =
    "fn tg_shift(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    t0: texture_2d<f32>) -> vec4f {\n"
    "    return box_texgraph_load(t0, texel + vec2i(1, 0));\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tg_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tg_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});

    return world;
}

/** Run frames and wait for each to render.
 *
 * @param world The world
 * @param count Frames to run
 */
static void tg_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run a script from the repository.
 *
 * @param world The world
 * @param file The script, from the repository root
 */
static void tg_run_file(
    ecs_world_t *world,
    const char *file)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
}

/** Run frames until a texture failed, or is ready with at least a revision.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 * @return The status
 */
static const TextureStatus* tg_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TG_FRAMES; i ++) {
        tg_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state == TextureFailed) {
            break;
        }

        if (status && status->state == TextureReady && status->revision >= revision) {
            break;
        }
    }

    test_assert(status != NULL);

    return status;
}

/** Run frames until a texture is ready with a revision, failing the test otherwise.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 */
static void tg_ready(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = tg_wait(world, tex, revision);
    if (status->state == TextureFailed) {
        printf("error: %s\n", status->error);
    }

    test_int(status->state, TextureReady);
    test_int(status->revision, revision);
}

/** Read back the image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Expected width
 * @param height Expected height
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tg_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tg_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TG_FRAMES && !result; i ++) {
        tg_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_uint(r->texture, tex);
                test_int(r->width, width);
                test_int(r->height, height);
                result = r->texels;
            } else {
                ecs_os_free(r->texels);
            }
        }
    }

    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
    test_assert(result != NULL);

    return result;
}

/** Read back the first channel of the first texel of the image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Width of the image
 * @param height Height of the image
 * @return The channel
 */
static float tg_read_value(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width,
    int32_t height)
{
    float *texels = tg_read(world, tex, width, height);
    float result = texels[0];
    ecs_os_free(texels);

    return result;
}

/** Check a texel against the expected value within a tolerance.
 *
 * @param texels The texels
 * @param width Width of the texels
 * @param x Column
 * @param y Row
 * @param expect Expected RGBA
 * @param tolerance Largest difference of a channel
 */
static void tg_expect(
    const float *texels,
    int32_t width,
    int32_t x,
    int32_t y,
    const float expect[4],
    float tolerance)
{
    const float *t = &texels[(y * width + x) * 4];
    int32_t c;
    for (c = 0; c < 4; c ++) {
        if (fabsf(t[c] - expect[c]) > tolerance) {
            printf("texel %d, %d channel %d: %f, expected %f\n", x, y, c, t[c], expect[c]);
        }

        test_assert(fabsf(t[c] - expect[c]) <= tolerance);
    }
}

/** Get the stored value of a texel of IMAGE_8X4.
 *
 * @param x Column
 * @param y Row
 * @param out Receives RGBA in 0..1
 */
static void ti_pixel(
    int32_t x,
    int32_t y,
    float out[4])
{
    out[0] = (float)(16 + 32 * x) / 255.0f;
    out[1] = (float)(32 + 64 * y) / 255.0f;
    out[2] = (float)(255 - 24 * (x + y)) / 255.0f;
    out[3] = 1.0f;
}

/** Get the statistics of the GPU backend.
 *
 * @param world The world
 * @return The statistics
 */
static box_render_gpu_stats_t tg_stats(
    ecs_world_t *world)
{
    box_render_gpu_stats_t result;
    test_bool(box_render_gpu_stats(world, &result), true);

    return result;
}

/** Create a texture of IMAGE_8X4.
 *
 * @param world The world
 * @return The texture entity
 */
static ecs_entity_t ti_image(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_entity(world, { .name = "image_8x4" });
    ecs_set(world, result, TexturePath, {.path = IMAGE_8X4});

    return result;
}

/** Create a graph of one TexImage node.
 *
 * @param world The world
 * @param texture The texture it samples
 * @param coords Its coords input node, 0 for the coordinates of the texel
 * @param address Its addressing
 * @param linear Whether it reads the image as stored
 * @param width Width of the graph
 * @param height Height of the graph
 * @param domain Domain of the graph
 * @param format Format of the graph
 * @param node Receives the TexImage node, may be NULL
 * @return The texture entity of the graph
 */
static ecs_entity_t ti_graph(
    ecs_world_t *world,
    ecs_entity_t texture,
    ecs_entity_t coords,
    TextureAddress address,
    bool linear,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    TextureGraphFormat format,
    ecs_entity_t *node)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, TexImage, {.texture = texture, .coords = coords,
        .address = address, .linear = linear});
    ecs_set(world, tex, TextureGraph, {.output = image, .width = width, .height = height,
        .domain = domain, .format = format});
    if (node) {
        *node = image;
    }

    return tex;
}

/** Create a graph of one constant node.
 *
 * @param world The world
 * @param value The constant
 * @param constant Receives the constant node, may be NULL
 * @return The texture entity, 8x8 GraphHdr
 */
static ecs_entity_t ti_constant(
    ecs_world_t *world,
    const float value[4],
    ecs_entity_t *constant)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, TexConstant, {.value = {value[0], value[1], value[2], value[3]}});
    ecs_set(world, tex, TextureGraph, {.output = c, .width = 8, .height = 8,
        .format = GraphHdr});
    if (constant) {
        *constant = c;
    }

    return tex;
}

/** Register the neighbourhood kind TgShift.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t ti_register_shift(
    ecs_world_t *world)
{
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TgShift" }),
        .type.size = ECS_SIZEOF(TgShift),
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .output = TexVector,
        .inputs = {{ "in", offsetof(TgShift, in), TexVector, "in_value",
            offsetof(TgShift, in_value) }},
        .function = "tg_shift",
        .source = tg_shift_wgsl,
        .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
    }) > 0);

    return c;
}

/** A plane graph of the size of the image reads it back texel by texel: as stored with linear,
 * and decoded and encoded again through GraphColor without. */
void TextureNodesImage_sample_plane(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t image = ti_image(world);
    ecs_entity_t raw = ti_graph(world, image, 0, AddressWrap, true, TI_W, TI_H, DomainPlane,
        GraphLinear, NULL);
    ecs_entity_t color = ti_graph(world, image, 0, AddressWrap, false, TI_W, TI_H,
        DomainPlane, GraphColor, NULL);
    int32_t x, y;
    float expect[4];
    tg_ready(world, raw, 1);
    tg_ready(world, color, 1);
    test_int(ecs_get(world, raw, Texture)->width, TI_W);
    test_int(ecs_get(world, raw, Texture)->height, TI_H);

    float *a = tg_read(world, raw, TI_W, TI_H);
    float *b = tg_read(world, color, TI_W, TI_H);
    for (y = 0; y < TI_H; y ++) {
        for (x = 0; x < TI_W; x ++) {
            ti_pixel(x, y, expect);
            tg_expect(a, TI_W, x, y, expect, TG_TOLERANCE_8);
            tg_expect(b, TI_W, x, y, expect, TG_TOLERANCE_8 * 1.5f);
        }
    }

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

/** Coordinates of a node: a texel to the right repeats the first column past the last one
 * with AddressWrap and keeps the last column with AddressClamp. */
void TextureNodesImage_wrap_and_clamp(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t image = ti_image(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shifted = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, shifted, TexAdd, {.a = uv,
        .b_value = {1.0f / TI_W, 0, 0, 0}});
    ecs_entity_t wrap = ti_graph(world, image, shifted, AddressWrap, true, TI_W, TI_H,
        DomainPlane, GraphLinear, NULL);
    ecs_entity_t clamp = ti_graph(world, image, shifted, AddressClamp, true, TI_W, TI_H,
        DomainPlane, GraphLinear, NULL);
    int32_t x, y;
    float expect[4];
    tg_ready(world, wrap, 1);
    tg_ready(world, clamp, 1);

    float *a = tg_read(world, wrap, TI_W, TI_H);
    float *b = tg_read(world, clamp, TI_W, TI_H);
    for (y = 0; y < TI_H; y ++) {
        for (x = 0; x < TI_W; x ++) {
            ti_pixel((x + 1) % TI_W, y, expect);
            tg_expect(a, TI_W, x, y, expect, TG_TOLERANCE_8);
            ti_pixel(x + 1 < TI_W ? x + 1 : TI_W - 1, y, expect);
            tg_expect(b, TI_W, x, y, expect, TG_TOLERANCE_8);
        }
    }

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

/** A sphere graph samples at the equirectangular uv of the direction of each texel, so a graph
 * of the size of the image reads it back texel by texel. */
void TextureNodesImage_sample_sphere(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t image = ti_image(world);
    ecs_entity_t tex = ti_graph(world, image, 0, AddressWrap, true, TI_W, TI_H, DomainSphere,
        GraphLinear, NULL);
    int32_t x, y;
    float expect[4];
    tg_ready(world, tex, 1);

    float *texels = tg_read(world, tex, TI_W, TI_H);
    for (y = 0; y < TI_H; y ++) {
        for (x = 0; x < TI_W; x ++) {
            ti_pixel(x, y, expect);
            tg_expect(texels, TI_W, x, y, expect, TG_TOLERANCE_8 * 2);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Read the first texel of a graph of one TexLut of the image at constant u and v.
 *
 * @param world The world
 * @param image The image
 * @param u The column
 * @param v The row
 * @param out Receives the texel
 */
static void ti_lut(
    ecs_world_t *world,
    ecs_entity_t image,
    float u,
    float v,
    float out[4])
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t lut = ecs_new(world);
    ecs_set(world, lut, TexLut, {.texture = image, .u_value = u, .v_value = v,
        .linear = true});
    ecs_set(world, tex, TextureGraph, {.output = lut, .width = 4, .height = 4,
        .format = GraphHdr});
    tg_ready(world, tex, 1);
    float *texels = tg_read(world, tex, 4, 4);
    ecs_os_memcpy(out, texels, 4 * ECS_SIZEOF(float));
    ecs_os_free(texels);
}

/** TexLut maps u and v 0..1 onto the centers of the first to the last texel, bilinear between
 * them, and clamps outside. */
void TextureNodesImage_lut(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t image = ti_image(world);
    float value[4], expect[4], left[4], right[4];
    int32_t c;

    ti_lut(world, image, 3.0f / (TI_W - 1), 2.0f / (TI_H - 1), value);
    ti_pixel(3, 2, expect);
    tg_expect(value, 1, 0, 0, expect, TG_TOLERANCE_8 + TG_TOLERANCE_16);

    ti_lut(world, image, -1.0f, 2.0f, value);
    ti_pixel(0, TI_H - 1, expect);
    tg_expect(value, 1, 0, 0, expect, TG_TOLERANCE_8 + TG_TOLERANCE_16);

    ti_lut(world, image, 3.5f / (TI_W - 1), 0.0f, value);
    ti_pixel(3, 0, left);
    ti_pixel(4, 0, right);
    for (c = 0; c < 4; c ++) {
        expect[c] = 0.5f * (left[c] + right[c]);
    }

    tg_expect(value, 1, 0, 0, expect, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_fini(world);
}

/** A graph samples the single channel image of a TextureGen as (r, 0, 0, 1) and the texture
 * of a Surface, which it follows while a draw samples it. */
void TextureNodesImage_gen_and_surface_sources(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    tg_run_file(world, GRAPH_IMAGE_SOURCES);
    ecs_entity_t from_noise = ecs_lookup(world, "graph_image_sources.from_noise");
    ecs_entity_t from_surface = ecs_lookup(world, "graph_image_sources.from_surface");
    ecs_entity_t view = ecs_lookup(world, "graph_image_sources.target_views.view");
    const float red[4] = {1, 0, 0, 1}, green[4] = {0, 1, 0, 1};
    float low = 1, high = 0;
    int32_t i;
    test_assert(from_noise != 0);
    test_assert(from_surface != 0);
    test_assert(view != 0);
    tg_ready(world, from_noise, 1);
    tg_ready(world, from_surface, 1);
    tg_frames(world, 3);

    float *texels = tg_read(world, from_noise, 16, 16);
    for (i = 0; i < 16 * 16; i ++) {
        const float *t = &texels[i * 4];
        test_assert(t[0] >= 0 && t[0] <= 1);
        test_assert(fabsf(t[1]) <= TG_TOLERANCE_16);
        test_assert(fabsf(t[2]) <= TG_TOLERANCE_16);
        test_assert(fabsf(t[3] - 1) <= TG_TOLERANCE_16);
        low = t[0] < low ? t[0] : low;
        high = t[0] > high ? t[0] : high;
    }

    test_assert(high - low > 0.05f);
    ecs_os_free(texels);

    texels = tg_read(world, from_surface, 8, 8);
    tg_expect(texels, 8, 3, 3, red, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_os_free(texels);

    View *v = ecs_get_mut(world, view, View);
    v->clear.color = (rgba){0, 255, 0, 255};
    ecs_modified(world, view, View);
    tg_frames(world, 5);
    texels = tg_read(world, from_surface, 8, 8);
    tg_expect(texels, 8, 3, 3, green, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A graph that adds four sampled images in pointwise nodes, more than one stage binds, splits
 * them over stages and adds them all. */
void TextureNodesImage_four_images_one_graph(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float values[4][4] = {
        {0.125f, 0, 0, 0}, {0, 0.25f, 0, 0}, {0, 0, 0.5f, 0}, {0, 0, 0, 1}
    };
    const float expect[4] = {0.125f, 0.25f, 0.5f, 1};
    ecs_entity_t sum = 0, tex = ecs_new(world);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t image = ecs_new(world);
        ecs_entity_t next = ecs_new(world);
        ecs_set(world, image, TexImage, {.texture = ti_constant(world, values[i], NULL),
            .linear = true});
        ecs_set(world, next, TexAdd, {.a = sum, .b = image});
        sum = next;
    }

    ecs_set(world, tex, TextureGraph, {.output = sum, .width = 8, .height = 8,
        .format = GraphHdr});
    tg_ready(world, tex, 1);

    float *texels = tg_read(world, tex, 8, 8);
    tg_expect(texels, 8, 5, 2, expect, TG_TOLERANCE_16 * 4);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A graph that samples another graph follows an edit of it in the frame the source evaluates:
 * one evaluation of each and no more, the stage of the sampling node runs again in its cached
 * value, the stage that does not depend on it stays cached. */
void TextureNodesImage_chain_updates(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t shift_c = ti_register_shift(world);
    const float first[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const float second[4] = {0.5f, 0.125f, 0.25f, 0.5f};
    ecs_entity_t constant;
    ecs_entity_t source = ti_constant(world, first, &constant);
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t image = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shift_image = ecs_new(world);
    ecs_entity_t shift_uv = ecs_new(world);
    ecs_entity_t sum = ecs_new(world);
    TgShift s = {0};
    int32_t x = 2, y = 5, c;
    float expect[4];
    ecs_set(world, image, TexImage, {.texture = source});
    ecs_set(world, uv, TexCoords, {.scale = 1});
    s.in = image;
    ecs_set_id(world, shift_image, shift_c, ECS_SIZEOF(TgShift), &s);
    s.in = uv;
    ecs_set_id(world, shift_uv, shift_c, ECS_SIZEOF(TgShift), &s);
    ecs_set(world, sum, TexAdd, {.a = shift_image, .b = shift_uv});
    ecs_set(world, tex, TextureGraph, {.output = sum, .width = 8, .height = 8,
        .format = GraphHdr});
    tg_ready(world, source, 1);
    tg_ready(world, tex, 1);
    tg_frames(world, 3);

    float *texels = tg_read(world, tex, 8, 8);
    for (c = 0; c < 4; c ++) {
        float coords[4] = {((float)(x + 1) + 0.5f) / 8, ((float)y + 0.5f) / 8, 0, 1};
        expect[c] = first[c] + coords[c];
    }

    tg_expect(texels, 8, x, y, expect, TG_TOLERANCE_16 * 4);
    ecs_os_free(texels);

    box_render_gpu_stats_t before = tg_stats(world);
    ecs_set(world, constant, TexConstant, {.value = {second[0], second[1], second[2],
        second[3]}});
    tg_ready(world, source, 2);
    tg_frames(world, 3);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, 2);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 3);
    test_int(after.texture_graph_misses - before.texture_graph_misses, 1);
    test_int(after.texture_graph_cached, before.texture_graph_cached);
    test_int(after.compiles, before.compiles);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);

    texels = tg_read(world, tex, 8, 8);
    for (c = 0; c < 4; c ++) {
        float coords[4] = {((float)(x + 1) + 0.5f) / 8, ((float)y + 0.5f) / 8, 0, 1};
        expect[c] = second[c] + coords[c];
    }

    tg_expect(texels, 8, x, y, expect, TG_TOLERANCE_16 * 4);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A graph that samples a GraphSingle graph, whose image is written after its stages, follows
 * an edit of it in the frame the source evaluates: one evaluation of each and no more. */
void TextureNodesImage_single_source_same_packet(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t source = ecs_new(world);
    ecs_entity_t constant = ecs_new(world);
    const float first[4] = {0.25f, 0, 0, 1}, second[4] = {0.75f, 0, 0, 1};
    ecs_set(world, constant, TexConstant, {.value = {0.25f, 0.25f, 0.25f, 1}});
    ecs_set(world, source, TextureGraph, {.output = constant, .width = 8, .height = 8,
        .format = GraphSingle});
    ecs_entity_t tex = ti_graph(world, source, 0, AddressWrap, true, 8, 8, DomainPlane,
        GraphHdr, NULL);
    tg_ready(world, source, 1);
    tg_ready(world, tex, 1);
    tg_frames(world, 3);

    float *texels = tg_read(world, tex, 8, 8);
    tg_expect(texels, 8, 4, 4, first, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_os_free(texels);

    box_render_gpu_stats_t before = tg_stats(world);
    ecs_set(world, constant, TexConstant, {.value = {0.75f, 0.75f, 0.75f, 1}});
    tg_ready(world, source, 2);
    tg_frames(world, 3);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, 2);

    texels = tg_read(world, tex, 8, 8);
    tg_expect(texels, 8, 4, 4, second, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A graph that samples a new revision of a TexturePath evaluates again: a single channel load
 * of the image reads as (r, 0, 0, 1), then the full color image after the edit. */
void TextureNodesImage_texture_revision_updates(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t image = ecs_entity(world, { .name = "image_8x4" });
    ecs_set(world, image, TexturePath, {.path = IMAGE_8X4, .single_channel = true});
    ecs_entity_t tex = ti_graph(world, image, 0, AddressWrap, true, TI_W, TI_H, DomainPlane,
        GraphHdr, NULL);
    float expect[4];
    tg_ready(world, tex, 1);
    test_int(ecs_get(world, image, TextureStatus)->revision, 1);

    float *texels = tg_read(world, tex, TI_W, TI_H);
    ti_pixel(5, 2, expect);
    expect[1] = 0;
    expect[2] = 0;
    expect[3] = 1;
    tg_expect(texels, TI_W, 5, 2, expect, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_os_free(texels);

    ecs_set(world, image, TexturePath, {.path = IMAGE_8X4});
    tg_ready(world, image, 2);
    tg_frames(world, 3);
    texels = tg_read(world, tex, TI_W, TI_H);
    ti_pixel(5, 2, expect);
    tg_expect(texels, TI_W, 5, 2, expect, TG_TOLERANCE_8 + TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A drawn graph that samples an animated graph follows its updates, which run while the drawn
 * graph is drawn; once no draw samples it, neither updates. */
void TextureNodesImage_animated_source(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    tg_run_file(world, GRAPH_IMAGE_ANIMATED);
    ecs_entity_t source = ecs_lookup(world, "graph_image_animated.source");
    ecs_entity_t image = ecs_lookup(world, "graph_image_animated.image");
    ecs_entity_t quad = ecs_lookup(world, "graph_image_animated.quad");
    test_assert(source != 0);
    test_assert(image != 0);
    test_assert(quad != 0);
    tg_ready(world, source, 1);
    tg_ready(world, image, 1);
    tg_frames(world, 3);

    float a = tg_read_value(world, image, 8, 8);
    tg_frames(world, 10);
    float b = tg_read_value(world, image, 8, 8);
    if (b <= a) {
        printf("a %f, b %f\n", a, b);
    }

    test_assert(b > a);
    test_assert(b > 0.5f);

    ecs_set(world, quad, PbrTextures, {0});
    tg_frames(world, 3);
    box_render_gpu_stats_t before = tg_stats(world);
    float c = tg_read_value(world, image, 8, 8);
    float s = tg_read_value(world, source, 8, 8);
    tg_frames(world, 10);
    float d = tg_read_value(world, image, 8, 8);
    float t = tg_read_value(world, source, 8, 8);
    box_render_gpu_stats_t after = tg_stats(world);
    test_assert(c == d);
    test_assert(s == t);
    test_int(after.texture_graph_evaluations, before.texture_graph_evaluations);
    test_int(ecs_get(world, image, TextureStatus)->revision, 1);
    ecs_fini(world);
}

/** Rebinding a TexImage to another texture only changes the bindings: nothing compiles and the
 * image samples the new texture; adding a TexImage to a graph changes its structure. */
void TextureNodesImage_rebind_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float red[4] = {1, 0, 0, 1}, blue[4] = {0, 0, 1, 1};
    ecs_entity_t a = ti_constant(world, red, NULL);
    ecs_entity_t b = ti_constant(world, blue, NULL);
    ecs_entity_t node;
    ecs_entity_t tex = ti_graph(world, a, 0, AddressWrap, true, 8, 8, DomainPlane, GraphHdr,
        &node);
    tg_ready(world, a, 1);
    tg_ready(world, b, 1);
    tg_ready(world, tex, 1);
    tg_frames(world, 2);

    float *texels = tg_read(world, tex, 8, 8);
    tg_expect(texels, 8, 3, 3, red, TG_TOLERANCE_16);
    ecs_os_free(texels);

    box_render_gpu_stats_t before = tg_stats(world);
    ecs_set(world, node, TexImage, {.texture = b, .linear = true});
    tg_ready(world, tex, 2);
    box_render_gpu_stats_t after = tg_stats(world);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);

    texels = tg_read(world, tex, 8, 8);
    tg_expect(texels, 8, 3, 3, blue, TG_TOLERANCE_16);
    ecs_os_free(texels);

    ecs_entity_t constant = ecs_new(world);
    ecs_entity_t sum = ecs_new(world);
    ecs_set(world, constant, TexConstant, {.value = {0.25f, 0.25f, 0.25f, 0}});
    ecs_set(world, sum, TexAdd, {.a = constant, .b = node});
    before = tg_stats(world);
    ecs_set(world, tex, TextureGraph, {.output = sum, .width = 8, .height = 8,
        .format = GraphHdr});
    tg_ready(world, tex, 3);
    after = tg_stats(world);
    test_assert(after.compiles > before.compiles);

    texels = tg_read(world, tex, 8, 8);
    const float expect[4] = {0.25f, 0.25f, 1.25f, 1};
    tg_expect(texels, 8, 3, 3, expect, TG_TOLERANCE_16 * 2);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** Graphs that sample each other fail with an error that names the cycle, also when the cycle
 * closes after the first one was ready, which then fails too; a graph that samples itself
 * fails. Breaking the cycle makes both ready again. */
void TextureNodesImage_cycle_fails(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.5f, 0.5f, 0.5f, 1};
    ecs_entity_t constant;
    ecs_entity_t b = ti_constant(world, value, &constant);
    ecs_entity_t a = ti_graph(world, b, 0, AddressWrap, true, 8, 8, DomainPlane, GraphHdr,
        NULL);
    tg_ready(world, b, 1);
    tg_ready(world, a, 1);

    ecs_entity_t back = ecs_entity(world, { .name = "samples_back" });
    ecs_set(world, back, TexImage, {.texture = a});
    ecs_log_set_level(-4);
    ecs_set(world, b, TextureGraph, {.output = back, .width = 8, .height = 8,
        .format = GraphHdr});
    const TextureStatus *status = tg_wait(world, b, 2);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    if (!strstr(status->error, "cycle")) {
        printf("error: %s\n", status->error);
    }

    test_assert(strstr(status->error, "cycle") != NULL);
    tg_frames(world, 3);
    status = ecs_get(world, a, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "cycle") != NULL);

    ecs_entity_t self = ecs_new(world);
    ecs_entity_t own = ecs_new(world);
    ecs_set(world, own, TexImage, {.texture = self});
    ecs_set(world, self, TextureGraph, {.output = own, .width = 8, .height = 8});
    status = tg_wait(world, self, 1);
    test_int(status->state, TextureFailed);
    test_assert(strstr(status->error, "cycle") != NULL);
    ecs_log_set_level(-1);

    ecs_set(world, b, TextureGraph, {.output = constant, .width = 8, .height = 8,
        .format = GraphHdr});
    status = tg_wait(world, b, 1);
    test_int(status->state, TextureReady);
    tg_frames(world, 3);
    status = tg_wait(world, a, 1);
    test_int(status->state, TextureReady);
    ecs_fini(world);
}

/** A graph that samples a graph without an image yet stays loading and is not sent, then is
 * ready with the image of the source once it has one. */
void TextureNodesImage_loading_holds(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.25f, 0.5f, 0.75f, 1};
    int32_t i;
    tg_frames(world, 1);
    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_entity_t source = ti_constant(world, value, NULL);
    ecs_entity_t tex = ti_graph(world, source, 0, AddressWrap, true, 8, 8, DomainPlane,
        GraphHdr, NULL);
    for (i = 0; i < 4; i ++) {
        tg_frames(world, 1);
        const TextureStatus *status = ecs_get(world, tex, TextureStatus);
        test_assert(status != NULL);
        test_int(status->state, TextureLoading);
        test_int(status->revision, 0);
        test_int(ecs_get(world, source, TextureStatus)->state, TextureLoading);
        test_assert(ecs_get(world, tex, Texture) == NULL);
        test_bool(box_render_texture_graph_read(world, tex, 0, tg_ticket ++), false);
    }

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_AT_ONCE), true);
    tg_ready(world, source, 1);
    tg_ready(world, tex, 1);

    float *texels = tg_read(world, tex, 8, 8);
    tg_expect(texels, 8, 4, 4, value, TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A graph fails with an error that names the texture when it samples one that failed, an
 * entity that is not a texture or no texture at all. */
void TextureNodesImage_failure_names_texture(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_log_set_level(-4);
    ecs_entity_t missing = ecs_entity(world, { .name = "missing_image" });
    ecs_set(world, missing, TexturePath, {.path = "test/assets/textures/graph/missing.png"});
    ecs_entity_t tex = ti_graph(world, missing, 0, AddressWrap, true, 8, 8, DomainPlane,
        GraphHdr, NULL);
    const TextureStatus *status = tg_wait(world, tex, 1);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    if (!strstr(status->error, "missing_image")) {
        printf("error: %s\n", status->error);
    }

    test_assert(strstr(status->error, "missing_image") != NULL);

    ecs_entity_t plain = ecs_entity(world, { .name = "not_a_texture" });
    ecs_entity_t other = ti_graph(world, plain, 0, AddressWrap, true, 8, 8, DomainPlane,
        GraphHdr, NULL);
    status = tg_wait(world, other, 1);
    test_int(status->state, TextureFailed);
    test_assert(strstr(status->error, "not_a_texture") != NULL);

    ecs_entity_t none = ti_graph(world, 0, 0, AddressWrap, true, 8, 8, DomainPlane, GraphHdr,
        NULL);
    status = tg_wait(world, none, 1);
    test_int(status->state, TextureFailed);
    test_assert(strstr(status->error, "no texture") != NULL);
    ecs_log_set_level(-1);
    ecs_fini(world);
}
