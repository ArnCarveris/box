/**
 * @file TextureGraphEval.c
 * @brief Tests for the evaluation of TextureGraph on the GPU backend: the images of constant,
 * coordinate and math graphs in both domains read back, nodes read back, compiles shared by
 * structure and kept by parameter edits, failures, status and a material that samples a graph,
 * the TexBlocks node against the GenBlocks layers of TextureGen and the TexRamp and TexCurve
 * nodes against their CPU twins.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "texture.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** A quad whose emission samples a constant red graph */
#define GRAPH_MATERIAL "test/assets/textures/graph/material.flecs"

/** A graph of coordinates scaled, offset and mixed with a constant */
#define GRAPH_MATH "test/assets/textures/graph/math.flecs"

/** Frames a test waits at most for an outcome */
#define TG_FRAMES (40)

/** Difference a stored RGBA8 or R8 channel may have from its value */
#define TG_TOLERANCE_8 (1.0f / 255.0f + 1e-4f)

/** Difference a half float channel of a value up to 1 may have from its value */
#define TG_TOLERANCE_16 (2e-3f)

/** Pi */
#define TG_PI (3.14159265358979f)

/** Node of a scalar kind: its value times the x of its vector input */
typedef struct TgScalar {
    float value;        /**< Factor */
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TgScalar;

/** Node of a vector kind that splats a scalar input into (x, x, x, 1) */
typedef struct TgSplat {
    ecs_entity_t in;    /**< Scalar input node */
    float in_value;     /**< Value of in without a node */
} TgSplat;

/** Node of a neighbourhood kind whose value is its input at the texel to the right */
typedef struct TgShift {
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TgShift;

/** Node of a kind whose WGSL does not compile */
typedef struct TgBroken {
    float value;        /**< Unused parameter */
} TgBroken;

/** WGSL of TgScalar */
static const char *tg_scalar_wgsl =
    "fn tg_scalar(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(bitcast<f32>(p[0].x) * i[0].x, 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TgSplat */
static const char *tg_splat_wgsl =
    "fn tg_splat(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x, i[0].x, i[0].x, 1.0);\n"
    "}\n";

/** WGSL of TgShift, which reads its materialized input with the shared helpers */
static const char *tg_shift_wgsl =
    "fn tg_shift(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f, texel: vec2i,\n"
    "    t0: texture_2d<f32>) -> vec4f {\n"
    "    return box_texgraph_load(t0, texel + vec2i(1, 0));\n"
    "}\n";

/** WGSL of TgBroken: an identifier that is not defined */
static const char *tg_broken_wgsl =
    "fn tg_broken(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return undefined_value;\n"
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

/** Run frames until a texture is not loading and has at least a revision.
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

/** Read back the image of a texture graph or the value of one of its nodes.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The node entity, 0 for the image
 * @param width Expected width
 * @param height Expected height
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tg_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tg_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TG_FRAMES && !result; i ++) {
        tg_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_uint(r->texture, tex);
                test_uint(r->node, node);
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

/** Get the coordinates of the center of a texel of the plane domain.
 *
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param out Receives (u, v, 0, 1)
 */
static void tg_plane(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    float out[4])
{
    out[0] = ((float)x + 0.5f) / (float)width;
    out[1] = ((float)y + 0.5f) / (float)height;
    out[2] = 0;
    out[3] = 1;
}

/** Get the coordinates of the center of a texel of the sphere domain: the direction of the
 * equirectangular texel, -z at the center column, y up at the top row.
 *
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param out Receives (x, y, z, 1)
 */
static void tg_sphere(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    float out[4])
{
    float u = ((float)x + 0.5f) / (float)width;
    float v = ((float)y + 0.5f) / (float)height;
    float phi = (u - 0.5f) * 2.0f * TG_PI, theta = v * TG_PI;
    out[0] = sinf(theta) * sinf(phi);
    out[1] = cosf(theta);
    out[2] = -sinf(theta) * cosf(phi);
    out[3] = 1;
}

/** Display encode a linear channel as the GPU does for GraphColor.
 *
 * @param c The channel, 0..1
 * @return The encoded channel
 */
static float tg_srgb(
    float c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/** Create a graph of one constant node.
 *
 * @param world The world
 * @param value The constant
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @param format Format
 * @return The texture entity
 */
static ecs_entity_t tg_constant(
    ecs_world_t *world,
    const float value[4],
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    TextureGraphFormat format)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t constant = ecs_new(world);
    ecs_set(world, constant, TexConstant, {.value = {value[0], value[1], value[2], value[3]}});
    ecs_set(world, tex, TextureGraph, {.output = constant, .width = width, .height = height,
        .domain = domain, .format = format});

    return tex;
}

/** Create a graph of the coordinates times a constant.
 *
 * @param world The world
 * @param constant Receives the constant node
 * @param math Receives the multiply node
 * @param value The constant
 * @param format Format
 * @return The texture entity
 */
static ecs_entity_t tg_scaled(
    ecs_world_t *world,
    ecs_entity_t *constant,
    ecs_entity_t *math,
    const float value[4],
    TextureGraphFormat format)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    *constant = ecs_new(world);
    *math = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, *constant, TexConstant, {.value = {value[0], value[1], value[2], value[3]}});
    ecs_set(world, *math, TexMultiply, {.a = uv, .b = *constant});
    ecs_set(world, tex, TextureGraph, {.output = *math, .width = 8, .height = 8,
        .format = format});

    return tex;
}

/** Register a node kind of a test component.
 *
 * @param world The world
 * @param name Name of the component
 * @param size Bytes of the component
 * @param desc The kind, its component is set here
 * @return The component
 */
static ecs_entity_t tg_register(
    ecs_world_t *world,
    const char *name,
    ecs_size_t size,
    box_texture_node_desc_t *desc)
{
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = name }),
        .type.size = size,
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    desc->component = c;
    test_assert(box_texture_node_register(world, desc) > 0);

    return c;
}

/** Check the image of a graph of one constant in a domain and a format.
 *
 * @param domain The domain
 */
static void tg_constant_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tg_world();
    const float value[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    ecs_entity_t tex = tg_constant(world, value, 8, 4, domain, GraphLinear);
    const TextureStatus *status = tg_wait(world, tex, 1);
    int32_t x, y;
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    test_int(ecs_get(world, tex, Texture)->height, 4);

    float *texels = tg_read(world, tex, 0, 8, 4);
    for (y = 0; y < 4; y ++) {
        for (x = 0; x < 8; x ++) {
            tg_expect(texels, 8, x, y, value, TG_TOLERANCE_8);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

void TextureGraphEval_constant_plane(void) {
    // textures/graph
    tg_constant_in(DomainPlane);
}

void TextureGraphEval_constant_sphere(void) {
    // textures/graph
    tg_constant_in(DomainSphere);
}

/** Check the image of a graph of the coordinates in a domain, stored as HDR.
 *
 * @param domain The domain
 */
static void tg_coords_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tg_world();
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    int32_t x, y, w = 32, h = 16;
    float expect[4];
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, tex, TextureGraph, {.output = uv, .width = w, .height = h,
        .domain = domain, .format = GraphHdr});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *texels = tg_read(world, tex, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            if (domain == DomainSphere) {
                tg_sphere(x, y, w, h, expect);
            } else {
                tg_plane(x, y, w, h, expect);
            }

            tg_expect(texels, w, x, y, expect, TG_TOLERANCE_16);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

void TextureGraphEval_coords_plane(void) {
    // textures/graph
    tg_coords_in(DomainPlane);
}

void TextureGraphEval_coords_sphere(void) {
    // textures/graph
    tg_coords_in(DomainSphere);
}

/** Check the image of the math graph of GRAPH_MATH in a domain.
 *
 * @param domain The domain
 */
static void tg_math_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tg_world();
    int32_t x, y, c, w = 16, h = 8;
    const float scale[4] = {0.5f, 0.25f, 0.5f, 1}, offset[4] = {0.2f, 0.1f, 0.05f, 0};
    float coords[4], expect[4];
    tg_run_file(world, GRAPH_MATH);
    ecs_entity_t tex = ecs_lookup(world, "graph_math");
    test_assert(tex != 0);
    TextureGraph *graph = ecs_get_mut(world, tex, TextureGraph);
    graph->domain = domain;
    ecs_modified(world, tex, TextureGraph);
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *texels = tg_read(world, tex, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            if (domain == DomainSphere) {
                tg_sphere(x, y, w, h, coords);
            } else {
                tg_plane(x, y, w, h, coords);
            }

            for (c = 0; c < 4; c ++) {
                float v = coords[c] * scale[c] + offset[c];
                expect[c] = v * 0.75f + 0.25f;
            }

            tg_expect(texels, w, x, y, expect, TG_TOLERANCE_16);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

void TextureGraphEval_math_plane(void) {
    // textures/graph
    tg_math_in(DomainPlane);
}

void TextureGraphEval_math_sphere(void) {
    // textures/graph
    tg_math_in(DomainSphere);
}

/** GraphColor stores the display encoding of the linear output with alpha as computed, so a
 * color layer that decodes it samples the output back.
 */
void TextureGraphEval_color_encoded(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.5f, 0.2f, 1.0f, 0.5f};
    float expect[4] = {tg_srgb(0.5f), tg_srgb(0.2f), 1.0f, 0.5f};
    ecs_entity_t tex = tg_constant(world, value, 4, 4, DomainPlane, GraphColor);
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *texels = tg_read(world, tex, 0, 4, 4);
    tg_expect(texels, 4, 0, 0, expect, TG_TOLERANCE_8);
    tg_expect(texels, 4, 3, 3, expect, TG_TOLERANCE_8);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** GraphSingle stores the first channel in an R8 image, which reads back as (r, 0, 0, 1). */
void TextureGraphEval_single_channel(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.6f, 0.9f, 0.1f, 0.5f};
    const float expect[4] = {0.6f, 0, 0, 1};
    ecs_entity_t tex = tg_constant(world, value, 8, 8, DomainPlane, GraphSingle);
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *texels = tg_read(world, tex, 0, 8, 8);
    tg_expect(texels, 8, 0, 0, expect, TG_TOLERANCE_8);
    tg_expect(texels, 8, 7, 7, expect, TG_TOLERANCE_8);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A readback of a node materializes it: the value of the multiply node of the math graph
 * before its offset, at half precision. */
void TextureGraphEval_node_readback(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    int32_t x, y, c, w = 16, h = 8;
    const float scale[4] = {0.5f, 0.25f, 0.5f, 1};
    float expect[4];
    tg_run_file(world, GRAPH_MATH);
    ecs_entity_t tex = ecs_lookup(world, "graph_math");
    ecs_entity_t scaled = ecs_lookup(world, "graph_math.scaled");
    test_assert(tex != 0);
    test_assert(scaled != 0);
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *texels = tg_read(world, tex, scaled, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tg_plane(x, y, w, h, expect);
            for (c = 0; c < 4; c ++) {
                expect[c] *= scale[c];
            }

            tg_expect(texels, w, x, y, expect, TG_TOLERANCE_16);
        }
    }

    ecs_os_free(texels);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    ecs_fini(world);
}

/** A scalar node materializes in an R32Float intermediate and reads back as (x, 0, 0, 1); a
 * vector kind splats it into the image. */
void TextureGraphEval_scalar_node_readback(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    int32_t x, y, w = 8, h = 4;
    float coords[4];
    ecs_entity_t scalar_c = tg_register(world, "TgScalar", ECS_SIZEOF(TgScalar),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .inputs = {{ "in", offsetof(TgScalar, in), TexVector, "in_value",
                offsetof(TgScalar, in_value) }},
            .params = {{ "value", offsetof(TgScalar, value), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "tg_scalar",
            .source = tg_scalar_wgsl
        });
    ecs_entity_t splat_c = tg_register(world, "TgSplat", ECS_SIZEOF(TgSplat),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .inputs = {{ "in", offsetof(TgSplat, in), TexScalar, "in_value",
                offsetof(TgSplat, in_value) }},
            .function = "tg_splat",
            .source = tg_splat_wgsl
        });
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t scalar = ecs_new(world);
    ecs_entity_t splat = ecs_new(world);
    TgScalar s = {.value = 2, .in = uv};
    TgSplat p = {.in = scalar};
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set_id(world, scalar, scalar_c, ECS_SIZEOF(TgScalar), &s);
    ecs_set_id(world, splat, splat_c, ECS_SIZEOF(TgSplat), &p);
    ecs_set(world, tex, TextureGraph, {.output = splat, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *image = tg_read(world, tex, 0, w, h);
    float *values = tg_read(world, tex, scalar, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tg_plane(x, y, w, h, coords);
            float v = 2 * coords[0];
            const float pixel[4] = {v, v, v, 1};
            const float value[4] = {v, 0, 0, 1};
            tg_expect(image, w, x, y, pixel, TG_TOLERANCE_16 * 2);
            tg_expect(values, w, x, y, value, 1e-5f);
        }
    }

    ecs_os_free(values);
    ecs_os_free(image);
    ecs_fini(world);
}

/** The input of a neighbourhood node is materialized in its own stage, a cached value, which the
 * node reads at another texel: two dispatches, and the coordinates of the texel to the right,
 * wrapped. */
void TextureGraphEval_neighbourhood_materialized(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    int32_t x, y, w = 8, h = 4;
    float expect[4];
    box_render_gpu_stats_t before, after;
    ecs_entity_t shift_c = tg_register(world, "TgShift", ECS_SIZEOF(TgShift),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .inputs = {{ "in", offsetof(TgShift, in), TexVector, "in_value",
                offsetof(TgShift, in_value) }},
            .function = "tg_shift",
            .source = tg_shift_wgsl,
            .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
        });
    tg_frames(world, 1);
    test_bool(box_render_gpu_stats(world, &before), true);
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t shift = ecs_new(world);
    TgShift s = {.in = uv};
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set_id(world, shift, shift_c, ECS_SIZEOF(TgShift), &s);
    ecs_set(world, tex, TextureGraph, {.output = shift, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, 1);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 2);
    test_int(after.texture_graph_texels - before.texture_graph_texels, 2 * w * h);
    test_assert(after.texture_graph_cached_bytes >= (int64_t)w * h * 8);
    test_int(after.texture_graphs, 1);
    test_int(after.texture_graph_programs, 1);

    float *texels = tg_read(world, tex, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tg_plane((x + 1) % w, y, w, h, expect);
            tg_expect(texels, w, x, y, expect, TG_TOLERANCE_16);
        }
    }

    ecs_os_free(texels);
    box_render_bake_timing_t timing;
    test_bool(box_render_gpu_bake_timing(world, &timing), true);
    tg_frames(world, 4);
    test_bool(box_render_gpu_stats(world, &after), true);
    if (timing.measured) {
        test_assert(after.texture_graph_timed >= 1);
        test_assert(after.texture_graph_ns > 0);
    }

    ecs_fini(world);
}

/** An edit of a parameter writes the parameter buffer: no module compiles, the image changes
 * and the revision increments once. */
void TextureGraphEval_param_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const float edited[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    box_render_gpu_stats_t before, after;
    ecs_entity_t constant, math;
    ecs_entity_t tex = tg_scaled(world, &constant, &math, value, GraphHdr);
    const TextureStatus *status = tg_wait(world, tex, 1);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_bool(box_render_gpu_stats(world, &before), true);

    ecs_set(world, constant, TexConstant, {.value = {1.0f, 0.5f, 0.0f, 1.0f}});
    status = tg_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, 1);

    float *texels = tg_read(world, tex, 0, 8, 8);
    float expect[4];
    int32_t c;
    tg_plane(5, 2, 8, 8, expect);
    for (c = 0; c < 4; c ++) {
        expect[c] *= edited[c];
    }

    tg_expect(texels, 8, 5, 2, expect, TG_TOLERANCE_16);
    ecs_os_free(texels);
    tg_frames(world, 3);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    ecs_fini(world);
}

/** An edit of an edge changes the structure: the stages compile again and the image follows
 * the new structure. */
void TextureGraphEval_structure_edit_recompiles(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    box_render_gpu_stats_t before, after;
    ecs_entity_t constant, math;
    ecs_entity_t tex = tg_scaled(world, &constant, &math, value, GraphHdr);
    test_int(tg_wait(world, tex, 1)->state, TextureReady);
    test_bool(box_render_gpu_stats(world, &before), true);

    TexMultiply *m = ecs_get_mut(world, math, TexMultiply);
    m->b = 0;
    m->b_value[0] = 0.5f;
    m->b_value[1] = 0.5f;
    m->b_value[2] = 0.5f;
    m->b_value[3] = 0.5f;
    ecs_modified(world, math, TexMultiply);
    const TextureStatus *status = tg_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_assert(after.compiles > before.compiles);
    test_int(after.texture_graph_programs, 1);

    float *texels = tg_read(world, tex, 0, 8, 8);
    float expect[4];
    int32_t c;
    tg_plane(3, 6, 8, 8, expect);
    for (c = 0; c < 4; c ++) {
        expect[c] *= 0.5f;
    }

    tg_expect(texels, 8, 3, 6, expect, TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** Two graphs of one structure, such as two planets of one template, share their modules: the
 * second compiles nothing, and each has its own parameters. */
void TextureGraphEval_shared_structure(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float a_value[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const float b_value[4] = {1.0f, 0.0f, 0.5f, 1.0f};
    box_render_gpu_stats_t first, second;
    ecs_entity_t a_constant, a_math, b_constant, b_math;
    ecs_entity_t a = tg_scaled(world, &a_constant, &a_math, a_value, GraphHdr);
    test_int(tg_wait(world, a, 1)->state, TextureReady);
    test_bool(box_render_gpu_stats(world, &first), true);
    test_int(first.texture_graph_programs, 1);

    ecs_entity_t b = tg_scaled(world, &b_constant, &b_math, b_value, GraphHdr);
    test_int(tg_wait(world, b, 1)->state, TextureReady);
    test_bool(box_render_gpu_stats(world, &second), true);
    test_int(second.compiles, first.compiles);
    test_int(second.texture_graph_programs, 1);
    test_int(second.texture_graphs, 2);

    float *ta = tg_read(world, a, 0, 8, 8);
    float *tb = tg_read(world, b, 0, 8, 8);
    float coords[4], ea[4], eb[4];
    int32_t c;
    tg_plane(6, 1, 8, 8, coords);
    for (c = 0; c < 4; c ++) {
        ea[c] = coords[c] * a_value[c];
        eb[c] = coords[c] * b_value[c];
    }

    tg_expect(ta, 8, 6, 1, ea, TG_TOLERANCE_16);
    tg_expect(tb, 8, 6, 1, eb, TG_TOLERANCE_16);
    ecs_os_free(ta);
    ecs_os_free(tb);
    ecs_fini(world);
}

/** A node whose WGSL does not compile fails the texture with the error and the node, and the
 * texture keeps the image it had. */
void TextureGraphEval_compile_failure_reports_node(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    int32_t w = 8, h = 4;
    float expect[4];
    ecs_entity_t broken_c = tg_register(world, "TgBroken", ECS_SIZEOF(TgBroken),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .params = {{ "value", offsetof(TgBroken, value), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "tg_broken",
            .source = tg_broken_wgsl
        });
    ecs_entity_t tex = ecs_entity(world, { .name = "failing" });
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t broken = ecs_entity(world, { .name = "broken_node" });
    TgBroken bad = {.value = 1};
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set_id(world, broken, broken_c, ECS_SIZEOF(TgBroken), &bad);
    ecs_set(world, tex, TextureGraph, {.output = uv, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    ecs_set(world, tex, TextureGraph, {.output = broken, .width = w, .height = h,
        .format = GraphHdr});
    ecs_log_set_level(-4);
    const TextureStatus *status = tg_wait(world, tex, 2);
    ecs_log_set_level(-1);
    test_int(status->state, TextureFailed);
    test_int(status->revision, 1);
    test_assert(status->error != NULL);
    if (!strstr(status->error, "broken_node") || !strstr(status->error, "failing")) {
        printf("error: %s\n", status->error);
    }

    test_assert(strstr(status->error, "failing") != NULL);
    test_assert(strstr(status->error, "broken_node") != NULL);

    float *texels = tg_read(world, tex, 0, w, h);
    tg_plane(2, 1, w, h, expect);
    tg_expect(texels, w, 2, 1, expect, TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** The texture loads until its stages exist and its first evaluation ran, then is ready with
 * revision 1 and its size; a parameter edit loads again and makes revision 2. */
void TextureGraphEval_status_transitions(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const float value[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    int32_t i;
    tg_frames(world, 1);
    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_entity_t tex = tg_constant(world, value, 16, 8, DomainPlane, GraphLinear);
    for (i = 0; i < 4; i ++) {
        tg_frames(world, 1);
        const TextureStatus *status = ecs_get(world, tex, TextureStatus);
        test_assert(status != NULL);
        test_int(status->state, TextureLoading);
        test_int(status->revision, 0);
        test_assert(ecs_get(world, tex, Texture) == NULL);
    }

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_AT_ONCE), true);
    const TextureStatus *status = tg_wait(world, tex, 1);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_int(ecs_get(world, tex, Texture)->width, 16);
    test_int(ecs_get(world, tex, Texture)->height, 8);

    tg_frames(world, 3);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);

    ecs_entity_t constant = ecs_get(world, tex, TextureGraph)->output;
    ecs_set(world, constant, TexConstant, {.value = {0.5f, 0.5f, 0.5f, 1.0f}});
    ecs_progress(world, 1.0f / 60);
    status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureLoading);
    test_int(status->revision, 1);
    box_render_sync(world);
    status = tg_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    tg_frames(world, 3);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    ecs_fini(world);
}

/** A material samples the image of a graph like any texture: the emission of a quad that a
 * constant red graph multiplies renders red, not the white of the default emissive layer. */
void TextureGraphEval_material_samples_graph(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    tg_run_file(world, GRAPH_MATERIAL);
    ecs_entity_t image = ecs_lookup(world, "graph_material.image");
    test_assert(image != 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 32, .write_to_file = "graph.png",
        .resolution_scale = 1});
    test_int(tg_wait(world, image, 1)->state, TextureReady);
    tg_frames(world, 3);
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);

    char *path = flecs_asprintf("%s/graph.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, 32);
    test_int(h, 32);
    unsigned char *center = &img[(16 * 32 + 16) * 4];
    if (center[0] < 100 || center[1] > 30 || center[2] > 30) {
        printf("center: %d %d %d\n", center[0], center[1], center[2]);
    }

    test_assert(center[0] >= 100);
    test_assert(center[1] <= 30);
    test_assert(center[2] <= 30);
    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Texels whose block or mask may differ from GenBlocks, of every 1000: those whose center
 * falls within float rounding of a cut or a course */
#define TG_BLOCKS_OUTLIERS (5)

/** Create a graph of one TexBlocks node, stored as HDR.
 *
 * @param world The world
 * @param blocks The node
 * @param coords The node of its coordinates, 0 for the domain
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @return The texture entity
 */
static ecs_entity_t tg_blocks(
    ecs_world_t *world,
    const TexBlocks *blocks,
    ecs_entity_t coords,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain)
{
    ecs_entity_t tex = ecs_entity(world, { .name = "blocks_graph" });
    ecs_entity_t node = ecs_entity(world, { .name = "blocks_node" });
    TexBlocks value = *blocks;
    value.coords = coords;
    ecs_set_ptr(world, node, TexBlocks, &value);
    ecs_set(world, tex, TextureGraph, {.output = node, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Check the image of a TexBlocks node against the face mask, ids and tones of the GenBlocks
 * layer of the same parameters that TextureGen generates on the CPU. A softness below 0 is the
 * softness 0 of the layer, 1.3 times the gap.
 *
 * @param blocks The node; its seed is the seed of the generator, layer_seed that of the layer
 * @param width Width
 * @param height Height
 */
static void tg_blocks_match(
    const TexBlocks *blocks,
    int32_t width,
    int32_t height)
{
    ecs_world_t *world = tg_world();
    ecs_entity_t tex = tg_blocks(world, blocks, 0, width, height, DomainPlane);
    int32_t count = width * height, i, outliers = 0;
    int32_t tones = blocks->tones;
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    TextureGenLayer layer = {
        .kind = GenBlocks, .seed = blocks->layer_seed, .tones = blocks->tones,
        .tone_order = blocks->tone_order, .courses = blocks->courses,
        .per_course = blocks->per_course, .gap = blocks->gap, .jitter = blocks->jitter,
        .stagger = blocks->stagger, .softness = blocks->softness < 0 ? 0 : blocks->softness
    };
    float *value = ecs_os_malloc_n(float, count);
    float *id = ecs_os_malloc_n(float, count);
    int32_t *tone = ecs_os_malloc_n(int32_t, count);
    box_texture_gen_pattern(&layer, blocks->seed, width, height, value, id, tone);

    float *texels = tg_read(world, tex, 0, width, height);
    for (i = 0; i < count; i ++) {
        const float *t = &texels[i * 4];
        float level = tones > 1 ? (float)tone[i] / (float)(tones - 1) : 0.0f;
        bool ok = fabsf(t[0] - value[i]) <= 5e-3f && fabsf(t[1] - id[i]) <= TG_TOLERANCE_16 &&
            fabsf(t[2] - level) <= TG_TOLERANCE_16 && t[3] >= 0.0f && t[3] <= 1.0f;
        if (!ok) {
            if (outliers < 8) {
                printf("texel %d, %d: (%f, %f, %f, %f), GenBlocks (%f, %f, %f)\n",
                    i % width, i / width, t[0], t[1], t[2], t[3], value[i], id[i], level);
            }

            outliers ++;
        }
    }

    test_assert(outliers * 1000 <= count * TG_BLOCKS_OUTLIERS);

    ecs_os_free(texels);
    ecs_os_free(value);
    ecs_os_free(id);
    ecs_os_free(tone);
    ecs_fini(world);
}

void TextureGraphEval_blocks_match_gen(void) {
    // textures/graph
    tg_blocks_match(&(TexBlocks){
        .courses = 8, .per_course = 4, .gap = 0.006f, .jitter = 0.22f, .stagger = 0.5f,
        .softness = -1, .seed = 11
    }, 128, 128);
}

void TextureGraphEval_blocks_match_gen_aspect(void) {
    // textures/graph
    tg_blocks_match(&(TexBlocks){
        .courses = 6, .per_course = 5, .gap = 0.02f, .softness = 0.03f, .jitter = 0.4f,
        .stagger = 0.3f, .seed = 3, .layer_seed = 52
    }, 128, 64);
}

void TextureGraphEval_blocks_match_gen_tones(void) {
    // textures/graph
    TextureGenToneOrder orders[] = {
        GenToneRandom, GenToneChecker, GenToneCourses, GenToneColumns
    };
    int32_t o;
    for (o = 0; o < 4; o ++) {
        tg_blocks_match(&(TexBlocks){
            .courses = 4, .per_course = 4, .gap = 0.04f, .jitter = 0.2f, .stagger = 0.5f,
            .softness = -1, .tones = 3, .tone_order = orders[o], .seed = 5, .layer_seed = o
        }, 64, 64);
    }
}

void TextureGraphEval_blocks_match_gen_hard(void) {
    // textures/graph
    tg_blocks_match(&(TexBlocks){
        .courses = 3, .per_course = 2, .jitter = 0.6f, .stagger = 0.25f, .softness = -1,
        .tones = 2, .seed = 9
    }, 64, 64);
}

void TextureGraphEval_blocks_match_gen_wrap(void) {
    // textures/graph
    /* Courses whose cuts are all below 0.5, so the last block wraps around to u = 0 */
    int32_t seeds[] = {4, 6, 7};
    int32_t s;
    for (s = 0; s < 3; s ++) {
        tg_blocks_match(&(TexBlocks){
            .courses = 8, .per_course = 2, .gap = 0.01f, .jitter = 0.5f, .softness = -1,
            .seed = seeds[s]
        }, 128, 128);
    }

    for (s = 0; s < 4; s ++) {
        tg_blocks_match(&(TexBlocks){
            .courses = 1, .per_course = 1, .gap = 0.01f, .jitter = 0.5f, .softness = -1,
            .seed = s
        }, 64, 64);
    }
}

void TextureGraphEval_blocks_tiling(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const TexBlocks blocks = {
        .courses = 4, .per_course = 3, .gap = 0.02f, .jitter = 0.3f, .stagger = 0.5f,
        .softness = -1, .tones = 2, .seed = 7
    };
    int32_t w = 64, h = 64, x, y, outliers = 0;
    ecs_entity_t plain = tg_blocks(world, &blocks, 0, w, h, DomainPlane);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t twice = ecs_new(world);
    ecs_entity_t shift = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, twice, TexMultiply, {.a = uv,
        .b_value = {2.0f, 2.0f, 1.0f, 1.0f}});
    ecs_set(world, shift, TexAdd, {.a = uv,
        .b_value = {1.0f, -2.0f, 0.0f, 0.0f}});
    ecs_entity_t repeated = ecs_new(world);
    ecs_entity_t repeated_node = ecs_new(world);
    TexBlocks value = blocks;
    value.coords = twice;
    ecs_set_ptr(world, repeated_node, TexBlocks, &value);
    ecs_set(world, repeated, TextureGraph, {.output = repeated_node, .width = w, .height = h,
        .format = GraphHdr});
    ecs_entity_t shifted = ecs_new(world);
    ecs_entity_t shifted_node = ecs_new(world);
    value.coords = shift;
    ecs_set_ptr(world, shifted_node, TexBlocks, &value);
    ecs_set(world, shifted, TextureGraph, {.output = shifted_node, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tg_wait(world, plain, 1)->state, TextureReady);
    test_int(tg_wait(world, repeated, 1)->state, TextureReady);
    test_int(tg_wait(world, shifted, 1)->state, TextureReady);

    float *a = tg_read(world, plain, 0, w, h);
    float *b = tg_read(world, repeated, 0, w, h);
    float *c = tg_read(world, shifted, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *pa = &a[(y * w + x) * 4];
            const float *pc = &c[(y * w + x) * 4];
            const float *pb = &b[(y * w + x) * 4];
            const float *pq = &b[(((y + h / 2) % h) * w + (x + w / 2) % w) * 4];
            int32_t k;
            bool ok = true;
            for (k = 0; k < 4; k ++) {
                ok = ok && fabsf(pa[k] - pc[k]) <= 5e-3f && fabsf(pb[k] - pq[k]) <= 5e-3f;
            }

            if (!ok) {
                outliers ++;
            }
        }
    }

    test_assert(outliers * 1000 <= w * h * TG_BLOCKS_OUTLIERS);

    /* Across the wrap of the plane the edge columns are in one block or in a joint */
    for (y = 0; y < h; y ++) {
        const float *left = &a[(y * w) * 4];
        const float *right = &a[(y * w + w - 1) * 4];
        test_assert(left[1] == right[1] || left[0] < 1.0f || right[0] < 1.0f);
    }

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_os_free(c);
    ecs_fini(world);
}

void TextureGraphEval_blocks_sphere_error(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const TexBlocks blocks = {.courses = 4, .per_course = 3, .gap = 0.02f, .softness = -1};
    ecs_log_set_level(-4);
    ecs_entity_t tex = tg_blocks(world, &blocks, 0, 32, 16, DomainSphere);
    const TextureStatus *status = tg_wait(world, tex, 1);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    if (!strstr(status->error, "blocks_node") || !strstr(status->error, "DomainPlane")) {
        printf("error: %s\n", status->error);
    }

    test_assert(strstr(status->error, "blocks_node") != NULL);
    test_assert(strstr(status->error, "DomainPlane") != NULL);
    test_assert(strstr(status->error, "blocks_graph") != NULL);

    ecs_fini(world);
}

/** Keys of the ramps of the tests: the outer keys at texel centers of a 32 wide plane, so the
 * texels 6, 15 and 25 are at a key */
#define TG_RAMP_KEYS \
    {6.5f / 32.0f, {255, 0, 0}}, {15.5f / 32.0f, {0, 128, 255}}, {25.5f / 32.0f, {20, 200, 40}}

/** Difference a ramp color stored as RGBA8 may have from the color of box_gen_ramp, which
 * rounds once where the GPU decodes to linear and encodes again */
#define TG_TOLERANCE_RAMP (2.0f / 255.0f + 1e-4f)

/** Difference a curve value of an R32Float node may have from box_curve_value */
#define TG_TOLERANCE_CURVE (1e-4f)

/** Register TgScalar, its value times the x of its vector input.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tg_register_scalar(
    ecs_world_t *world)
{
    return tg_register(world, "TgScalar", ECS_SIZEOF(TgScalar), &(box_texture_node_desc_t){
        .output = TexScalar,
        .inputs = {{ "in", offsetof(TgScalar, in), TexVector, "in_value",
            offsetof(TgScalar, in_value) }},
        .params = {{ "value", offsetof(TgScalar, value), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "tg_scalar",
        .source = tg_scalar_wgsl
    });
}

/** Register TgSplat, which splats a scalar input into (x, x, x, 1).
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tg_register_splat(
    ecs_world_t *world)
{
    return tg_register(world, "TgSplat", ECS_SIZEOF(TgSplat), &(box_texture_node_desc_t){
        .output = TexVector,
        .inputs = {{ "in", offsetof(TgSplat, in), TexScalar, "in_value",
            offsetof(TgSplat, in_value) }},
        .function = "tg_splat",
        .source = tg_splat_wgsl
    });
}

/** Create the scalar node of the tests of TexRamp and TexCurve: x of the coordinates times a
 * factor.
 *
 * @param world The world
 * @param factor The factor
 * @return The node
 */
static ecs_entity_t tg_coords_x(
    ecs_world_t *world,
    float factor)
{
    ecs_entity_t scalar_c = tg_register_scalar(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t scalar = ecs_new(world);
    TgScalar s = {.value = factor, .in = uv};
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set_id(world, scalar, scalar_c, ECS_SIZEOF(TgScalar), &s);

    return scalar;
}

/** Get the input of the tests of TexRamp and TexCurve at a texel: x of its coordinates times
 * a factor.
 *
 * @param domain The domain
 * @param x Column
 * @param y Row
 * @param width Width
 * @param height Height
 * @param factor The factor
 * @return The input
 */
static float tg_input(
    TextureGraphDomain domain,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    float factor)
{
    float coords[4];
    if (domain == DomainSphere) {
        tg_sphere(x, y, width, height, coords);
    } else {
        tg_plane(x, y, width, height, coords);
    }

    return coords[0] * factor;
}

/** Check the image of a ramp graph against box_gen_ramp, through box_texture_gen_albedo.
 *
 * @param world The world
 * @param tex The texture entity, a GraphColor ramp of the x of the coordinates times factor
 * @param domain The domain
 * @param width Width
 * @param height Height
 * @param factor The factor
 * @param keys The keys of the ramp
 * @param key_count Number of keys
 */
static void tg_ramp_expect(
    ecs_world_t *world,
    ecs_entity_t tex,
    TextureGraphDomain domain,
    int32_t width,
    int32_t height,
    float factor,
    const TextureGenKey *keys,
    int32_t key_count)
{
    float *texels = tg_read(world, tex, 0, width, height);
    int32_t x, y, c;
    for (y = 0; y < height; y ++) {
        for (x = 0; x < width; x ++) {
            float h = tg_input(domain, x, y, width, height, factor);
            uint8_t rgba[4];
            float expect[4];
            box_texture_gen_albedo(&h, 1, keys, key_count, NULL, NULL, 0, rgba);
            for (c = 0; c < 4; c ++) {
                expect[c] = (float)rgba[c] / 255.0f;
            }

            tg_expect(texels, width, x, y, expect, TG_TOLERANCE_RAMP);
        }
    }

    ecs_os_free(texels);
}

/** Check a ramp of the x of the coordinates in a domain: the first color below the first key,
 * the last above the last, the key colors at the keys and mixed between.
 *
 * @param domain The domain
 */
static void tg_ramp_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tg_world();
    const TextureGenKey keys[] = { TG_RAMP_KEYS };
    int32_t w = 32, h = domain == DomainSphere ? 16 : 1;
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t ramp = ecs_new(world);
    ecs_entity_t scalar = tg_coords_x(world, 1);
    ecs_set(world, ramp, TexRamp, {.input = scalar, .keys = BOX_VEC_VIEW(TextureGenKey,
        TG_RAMP_KEYS)});
    ecs_set(world, tex, TextureGraph, {.output = ramp, .width = w, .height = h,
        .domain = domain, .format = GraphColor});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);
    tg_ramp_expect(world, tex, domain, w, h, 1, keys, 3);
    ecs_fini(world);
}

void TextureGraphEval_ramp_plane(void) {
    // textures/graph
    tg_ramp_in(DomainPlane);
}

void TextureGraphEval_ramp_sphere(void) {
    // textures/graph
    tg_ramp_in(DomainSphere);
}

/** The value of a ramp node is the linear color of the mixed display encoded keys, with alpha
 * 1; without keys the input is a display encoded gray, without an input node its value. */
void TextureGraphEval_ramp_linear_value(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    const TextureGenKey keys[] = { TG_RAMP_KEYS };
    int32_t x, w = 32, c;
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t ramp = ecs_new(world);
    ecs_entity_t scalar = tg_coords_x(world, 1);
    ecs_set(world, ramp, TexRamp, {.input = scalar, .keys = BOX_VEC_VIEW(TextureGenKey,
        TG_RAMP_KEYS)});
    ecs_set(world, tex, TextureGraph, {.output = ramp, .width = w, .height = 1,
        .format = GraphHdr});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);

    float *texels = tg_read(world, tex, 0, w, 1);
    for (x = 0; x < w; x ++) {
        float in = tg_input(DomainPlane, x, 0, w, 1, 1), expect[4];
        uint8_t rgba[4];
        box_texture_gen_albedo(&in, 1, keys, 3, NULL, NULL, 0, rgba);
        for (c = 0; c < 3; c ++) {
            float d = (float)rgba[c] / 255.0f;
            expect[c] = d <= 0.04045f ? d / 12.92f : powf((d + 0.055f) / 1.055f, 2.4f);
        }

        expect[3] = 1;
        tg_expect(texels, w, x, 0, expect, TG_TOLERANCE_RAMP);
    }

    ecs_os_free(texels);

    ecs_set(world, ramp, TexRamp, {.input_value = 0.5f});
    test_int(tg_wait(world, tex, 2)->state, TextureReady);
    texels = tg_read(world, tex, 0, w, 1);
    float gray = powf((0.5f + 0.055f) / 1.055f, 2.4f);
    const float expect[4] = {gray, gray, gray, 1};
    tg_expect(texels, w, 7, 0, expect, TG_TOLERANCE_16);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** Check the value of a curve node against box_curve_value at the input of each texel.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The TexCurve node
 * @param curve The curve
 * @param width Width
 * @param factor Factor of the x of the coordinates of the input
 */
static void tg_curve_expect(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    const Curve *curve,
    int32_t width,
    float factor)
{
    float *values = tg_read(world, tex, node, width, 1);
    int32_t x;
    for (x = 0; x < width; x ++) {
        float in = tg_input(DomainPlane, x, 0, width, 1, factor);
        float expect = box_curve_value(curve, in, in);
        float value = values[x * 4];
        if (fabsf(value - expect) > TG_TOLERANCE_CURVE) {
            printf("texel %d at %f: %f, expected %f\n", x, in, value, expect);
        }

        test_assert(fabsf(value - expect) <= TG_TOLERANCE_CURVE);
    }

    ecs_os_free(values);
}

/** A curve node maps its input through each kind of curve as box_curve_value does, before,
 * between and after its keys; switching the kind only changes the parameters. */
void TextureGraphEval_curve_values(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t splat_c = tg_register_splat(world);
    const CurveKind kinds[] = {CurveLinear, CurveCatmullRom, CurveBezier};
    box_render_gpu_stats_t before, after;
    int32_t w = 64, i;
    float factor = 1.4f;
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t node = ecs_new(world);
    ecs_entity_t splat = ecs_new(world);
    ecs_entity_t scalar = tg_coords_x(world, factor);
    TgSplat p = {.in = node};
    ecs_set_id(world, splat, splat_c, ECS_SIZEOF(TgSplat), &p);
    for (i = 0; i < 3; i ++) {
        Curve curve = {.kind = kinds[i], .keys = BOX_CURVE_KEYS({0.1f, 0.2f, 0, 1.5f},
            {0.35f, 0.9f, -0.5f, 0.25f}, {0.7f, 0.1f, 2, -1}, {1.2f, 0.6f, 0.5f, 0})};
        ecs_set(world, node, TexCurve, {.input = scalar, .curve = curve});
        if (!i) {
            ecs_set(world, tex, TextureGraph, {.output = splat, .width = w, .height = 1,
                .format = GraphHdr});
        }

        test_int(tg_wait(world, tex, i + 1)->state, TextureReady);
        if (!i) {
            test_bool(box_render_gpu_stats(world, &before), true);
        }

        tg_curve_expect(world, tex, node, &curve, w, factor);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.texture_graph_programs, 1);
    ecs_fini(world);
}

/** Adding, editing and removing the keys of a ramp and of a curve changes the parameters of
 * the graph and never its structure: nothing compiles again, and the images follow the keys. */
void TextureGraphEval_keys_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t splat_c = tg_register_splat(world);
    box_render_gpu_stats_t before, after;
    int32_t w = 32;
    ecs_entity_t ramp_tex = ecs_new(world);
    ecs_entity_t curve_tex = ecs_new(world);
    ecs_entity_t ramp = ecs_new(world);
    ecs_entity_t curve = ecs_new(world);
    ecs_entity_t splat = ecs_new(world);
    ecs_entity_t scalar = tg_coords_x(world, 1);
    TgSplat p = {.in = curve};
    ecs_set_id(world, splat, splat_c, ECS_SIZEOF(TgSplat), &p);
    ecs_set(world, ramp, TexRamp, {.input = scalar, .keys = BOX_VEC_VIEW(TextureGenKey,
        {0.25f, {0, 0, 0}})});
    ecs_set(world, curve, TexCurve, {.input = scalar, .curve = {
        .keys = BOX_CURVE_KEYS({0, 0}, {1, 1})}});
    ecs_set(world, ramp_tex, TextureGraph, {.output = ramp, .width = w, .height = 1});
    ecs_set(world, curve_tex, TextureGraph, {.output = splat, .width = w, .height = 1,
        .format = GraphHdr});
    test_int(tg_wait(world, ramp_tex, 1)->state, TextureReady);
    test_int(tg_wait(world, curve_tex, 1)->state, TextureReady);

    /* Reading the curve node materializes it once, which compiles the stages of the new cut */
    tg_curve_expect(world, curve_tex, curve, &ecs_get(world, curve, TexCurve)->curve, w, 1);
    test_bool(box_render_gpu_stats(world, &before), true);

    const TextureGenKey keys[] = { TG_RAMP_KEYS };
    ecs_set(world, ramp, TexRamp, {.input = scalar, .keys = BOX_VEC_VIEW(TextureGenKey,
        TG_RAMP_KEYS)});
    test_int(tg_wait(world, ramp_tex, 2)->state, TextureReady);
    tg_ramp_expect(world, ramp_tex, DomainPlane, w, 1, 1, keys, 3);

    const TextureGenKey edited[] = {{0.1f, {10, 20, 30}}, {0.9f, {250, 240, 230}}};
    ecs_set(world, ramp, TexRamp, {.input = scalar, .keys = BOX_VEC_VIEW(TextureGenKey,
        {0.1f, {10, 20, 30}}, {0.9f, {250, 240, 230}})});
    test_int(tg_wait(world, ramp_tex, 3)->state, TextureReady);
    tg_ramp_expect(world, ramp_tex, DomainPlane, w, 1, 1, edited, 2);

    ecs_set(world, curve, TexCurve, {.input = scalar, .curve = {.kind = CurveCatmullRom,
        .keys = BOX_CURVE_KEYS({0.1f, 0.5f}, {0.4f, 0.2f}, {0.6f, 0.8f}, {0.9f, 0.3f})}});
    test_int(tg_wait(world, curve_tex, 2)->state, TextureReady);
    tg_curve_expect(world, curve_tex, curve, &ecs_get(world, curve, TexCurve)->curve, w, 1);

    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);
    ecs_fini(world);
}

/** A ramp with more keys than BOX_TEXTURE_RAMP_KEYS fails its graph with an error. */
void TextureGraphEval_ramp_too_many_keys(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t ramp = ecs_new(world);
    TexRamp *value = ecs_ensure(world, ramp, TexRamp);
    int32_t i;
    value->input_value = 0.5f;
    ecs_vec_init_if_t(&value->keys, TextureGenKey);
    for (i = 0; i <= BOX_TEXTURE_RAMP_KEYS; i ++) {
        *ecs_vec_append_t(NULL, &value->keys, TextureGenKey) = (TextureGenKey){
            (float)i / BOX_TEXTURE_RAMP_KEYS, {(uint8_t)(i * 7), 0, 0}};
    }

    ecs_modified(world, ramp, TexRamp);
    ecs_set(world, tex, TextureGraph, {.output = ramp, .width = 8, .height = 1});
    const TextureStatus *status = tg_wait(world, tex, 1);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "more than 32") != NULL);
    ecs_fini(world);
}
