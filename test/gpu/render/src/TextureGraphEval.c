/**
 * @file TextureGraphEval.c
 * @brief Tests for the evaluation of TextureGraph on the GPU backend: the images of constant,
 * coordinate and math graphs in both domains read back, nodes read back, compiles shared by
 * structure and kept by parameter edits, failures, status and a material that samples a graph.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

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
    ecs_set(world, uv, TexCoords, {0});
    ecs_set(world, *constant, TexConstant, {.value = {value[0], value[1], value[2], value[3]}});
    ecs_set(world, *math, TexMath, {.op = MathMultiply, .a = uv, .b = *constant});
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
    ecs_set(world, uv, TexCoords, {0});
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
    ecs_set(world, uv, TexCoords, {0});
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

/** The input of a neighbourhood node is materialized in its own stage, which the node reads at
 * another texel: two dispatches, and the coordinates of the texel to the right, wrapped. */
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
    ecs_set(world, uv, TexCoords, {0});
    ecs_set_id(world, shift, shift_c, ECS_SIZEOF(TgShift), &s);
    ecs_set(world, tex, TextureGraph, {.output = shift, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tg_wait(world, tex, 1)->state, TextureReady);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, 1);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 2);
    test_int(after.texture_graph_texels - before.texture_graph_texels, 2 * w * h);
    test_assert(after.texture_graph_pool_bytes >= (int64_t)w * h * 8);
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

    TexMath *m = ecs_get_mut(world, math, TexMath);
    m->b = 0;
    m->b_value[0] = 0.5f;
    m->b_value[1] = 0.5f;
    m->b_value[2] = 0.5f;
    m->b_value[3] = 0.5f;
    ecs_modified(world, math, TexMath);
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
    ecs_set(world, uv, TexCoords, {0});
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
