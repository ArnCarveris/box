/**
 * @file TextureGraphNormal.c
 * @brief Tests for the TexNormal and TexSlope nodes of TextureGraph on the GPU backend: the
 * normals and slopes of heights of known slope on the plane and the sphere, the normals of a
 * graph against GenNormal of the same height, and continuity across the seam and the poles of
 * the sphere.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "texture.h"

/** Frames a test waits at most for an outcome */
#define TN_FRAMES (40)

/** Difference a stored RGBA8 channel may have from its value */
#define TN_TOLERANCE_8 (1.0f / 255.0f + 1e-4f)

/** Pi */
#define TN_PI (3.14159265358979)

/** Largest factor of the sphere normals near the poles, BOX_TEXGRAPH_POLE_SCALE */
#define TN_POLE_SCALE (8.0)

/** Node of a scalar kind: the dot product of its vector input with weights */
typedef struct TnDot {
    vec4 weights;       /**< Weights of the channels */
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TnDot;

/** Node of a scalar kind: a smooth function of its vector input, see tn_wave_wgsl */
typedef struct TnWave {
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TnWave;

/** WGSL of TnDot */
static const char *tn_dot_wgsl =
    "fn tn_dot(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(dot(bitcast<vec4f>(p[0]), i[0]), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TnWave: on the plane a periodic wave of u and v, on the sphere a smooth function
 * of the direction; 0.5 + 0.25 sin(2 pi 3 x) cos(2 pi 2 y) + 0.2 sin(3 x + 1) z + 0.2 y y */
static const char *tn_wave_wgsl =
    "fn tn_wave(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let d = i[0];\n"
    "    let tau = 6.283185307179586;\n"
    "    let h = 0.5 + 0.25 * sin(tau * 3.0 * d.x) * cos(tau * 2.0 * d.y)\n"
    "        + 0.2 * sin(3.0 * d.x + 1.0) * d.z + 0.2 * d.y * d.y;\n"
    "    return vec4f(h, 0.0, 0.0, 0.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tn_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tn_world(void)
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
static void tn_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready or failed and check that it is ready.
 *
 * @param world The world
 * @param tex The texture entity
 */
static void tn_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TN_FRAMES; i ++) {
        tn_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && (status->state == TextureFailed ||
            (status->state == TextureReady && status->revision >= 1)))
        {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("graph failed: %s\n", status->error);
    }

    test_int(status->state, TextureReady);
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
static float* tn_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tn_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TN_FRAMES && !result; i ++) {
        tn_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
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

/** Check a channel against the expected value within a tolerance.
 *
 * @param texels The texels
 * @param width Width of the texels
 * @param x Column
 * @param y Row
 * @param c Channel
 * @param expect Expected value
 * @param tolerance Largest difference
 */
static void tn_expect(
    const float *texels,
    int32_t width,
    int32_t x,
    int32_t y,
    int32_t c,
    double expect,
    double tolerance)
{
    float v = texels[(y * width + x) * 4 + c];
    if (fabs((double)v - expect) > tolerance) {
        printf("texel %d, %d channel %d: %f, expected %f\n", x, y, c, v, expect);
    }

    test_assert(fabs((double)v - expect) <= tolerance);
}

/** Check an encoded normal against the differences it comes from.
 *
 * @param texels The texels
 * @param width Width of the texels
 * @param x Column
 * @param y Row
 * @param dx Difference along u
 * @param dy Difference down the image
 * @param tolerance Largest difference of a channel
 */
static void tn_expect_normal(
    const float *texels,
    int32_t width,
    int32_t x,
    int32_t y,
    double dx,
    double dy,
    double tolerance)
{
    double len = sqrt(dx * dx + dy * dy + 1.0);
    tn_expect(texels, width, x, y, 0, 0.5 - 0.5 * dx / len, tolerance);
    tn_expect(texels, width, x, y, 1, 0.5 + 0.5 * dy / len, tolerance);
    tn_expect(texels, width, x, y, 2, 0.5 + 0.5 / len, tolerance);
    tn_expect(texels, width, x, y, 3, 1.0, tolerance);
}

/** Get the frame of the center of a texel of the sphere domain: its direction, the unit tangent
 * toward growing u (east) and the unit tangent toward shrinking v (north).
 *
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param p Receives the direction
 * @param east Receives the east tangent
 * @param north Receives the north tangent
 */
static void tn_sphere_frame(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    double p[3],
    double east[3],
    double north[3])
{
    double u = ((double)x + 0.5) / width;
    double v = ((double)y + 0.5) / height;
    double phi = (u - 0.5) * 2.0 * TN_PI, theta = v * TN_PI;
    p[0] = sin(theta) * sin(phi);
    p[1] = cos(theta);
    p[2] = -sin(theta) * cos(phi);
    east[0] = cos(phi);
    east[1] = 0;
    east[2] = sin(phi);
    north[0] = -cos(theta) * sin(phi);
    north[1] = sin(theta);
    north[2] = cos(theta) * cos(phi);
}

/** Get the lengths of the u and v tangents of the frame a Sphere mesh takes from its UVs at a
 * row (cotangent_frame in mesh_surface.wgsl): the gradients of u and v, the longer of unit
 * length, where the gradient of u is 1 / (2 sin(theta)) times that of v.
 *
 * @param y Row
 * @param height Height of the image
 * @param tu Receives the length of the u tangent
 * @param tv Receives the length of the v tangent
 */
static void tn_sphere_tangents(
    int32_t y,
    int32_t height,
    double *tu,
    double *tv)
{
    double r = 2.0 * sin(((double)y + 0.5) / height * TN_PI);
    *tu = r > 1 ? 1 / r : 1;
    *tv = r > 1 ? 1 : r;
}

/** Dot product of two 3D vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return a . b
 */
static double tn_dot3(
    const double a[3],
    const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/** Register the test kinds TnDot and TnWave.
 *
 * @param world The world
 * @param dot Receives the component of TnDot
 * @param wave Receives the component of TnWave
 */
static void tn_register(
    ecs_world_t *world,
    ecs_entity_t *dot,
    ecs_entity_t *wave)
{
    *dot = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TnDot" }),
        .type.size = ECS_SIZEOF(TnDot),
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = *dot,
        .output = TexScalar,
        .inputs = {{ "in", offsetof(TnDot, in), TexVector, "in_value",
            offsetof(TnDot, in_value) }},
        .params = {{ "weights", offsetof(TnDot, weights), 16, BOX_TEXTURE_PARAM_F32 }},
        .function = "tn_dot",
        .source = tn_dot_wgsl
    }) > 0);

    *wave = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TnWave" }),
        .type.size = ECS_SIZEOF(TnWave),
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = *wave,
        .output = TexScalar,
        .inputs = {{ "in", offsetof(TnWave, in), TexVector, "in_value",
            offsetof(TnWave, in_value) }},
        .function = "tn_wave",
        .source = tn_wave_wgsl
    }) > 0);
}

/** Create a height node of the coordinates: TnDot of weights or TnWave.
 *
 * @param world The world
 * @param kind The component of TnDot or TnWave
 * @param weights Weights of TnDot, NULL for TnWave
 * @return The height node
 */
static ecs_entity_t tn_height(
    ecs_world_t *world,
    ecs_entity_t kind,
    const float *weights)
{
    ecs_entity_t coords = ecs_new(world);
    ecs_entity_t height = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    if (weights) {
        TnDot d = {.weights = {weights[0], weights[1], weights[2], weights[3]}, .in = coords};
        ecs_set_id(world, height, kind, ECS_SIZEOF(TnDot), &d);
    } else {
        TnWave w = {.in = coords};
        ecs_set_id(world, height, kind, ECS_SIZEOF(TnWave), &w);
    }

    return height;
}

/** Create a graph of a TexNormal or a TexSlope of a height.
 *
 * @param world The world
 * @param height The height node
 * @param slope Make a TexSlope instead of a TexNormal
 * @param strength Strength of the node
 * @param width Width
 * @param h Height
 * @param domain Domain
 * @param node Receives the TexNormal or TexSlope node
 * @return The texture entity
 */
static ecs_entity_t tn_graph(
    ecs_world_t *world,
    ecs_entity_t height,
    bool slope,
    float strength,
    int32_t width,
    int32_t h,
    TextureGraphDomain domain,
    ecs_entity_t *node)
{
    ecs_entity_t tex = ecs_new(world);
    *node = ecs_new(world);
    if (slope) {
        ecs_set(world, *node, TexSlope, {.height = height, .strength = strength});
    } else {
        ecs_set(world, *node, TexNormal, {.height = height, .strength = strength});
    }

    ecs_set(world, tex, TextureGraph, {.output = *node, .width = width, .height = h,
        .domain = domain, .format = slope ? GraphHdr : GraphLinear});

    return tex;
}

/** Get the world space normal of an encoded tangent space normal of a texel of the sphere, as
 * a Sphere mesh decodes it in the frame of its UVs.
 *
 * @param texels The texels
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param out Receives the unit normal
 */
static void tn_world_normal(
    const float *texels,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    double out[3])
{
    double p[3], east[3], north[3], m[3], len, tu, tv;
    const float *t = &texels[(y * width + x) * 4];
    int32_t c;
    tn_sphere_frame(x, y, width, height, p, east, north);
    tn_sphere_tangents(y, height, &tu, &tv);
    for (c = 0; c < 3; c ++) {
        m[c] = 2.0 * t[c] - 1.0;
    }

    for (c = 0; c < 3; c ++) {
        out[c] = east[c] * m[0] * tu + north[c] * m[1] * tv + p[c] * m[2];
    }

    len = sqrt(tn_dot3(out, out));
    for (c = 0; c < 3; c ++) {
        out[c] /= len;
    }
}

/** Distance between the world space normals of two texels of the sphere.
 *
 * @param texels The texels
 * @param width Width of the image
 * @param height Height of the image
 * @param x0 Column of the first texel
 * @param y0 Row of the first texel
 * @param x1 Column of the second texel
 * @param y1 Row of the second texel
 * @return The distance
 */
static double tn_world_distance(
    const float *texels,
    int32_t width,
    int32_t height,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    double a[3], b[3], d[3];
    int32_t c;
    tn_world_normal(texels, x0, y0, width, height, a);
    tn_world_normal(texels, x1, y1, width, height, b);
    for (c = 0; c < 3; c ++) {
        d[c] = a[c] - b[c];
    }

    return sqrt(tn_dot3(d, d));
}

/** Differences of the height k . p on the sphere at a texel, as the node takes them: between
 * the points one texel of the equator east and west, and south and north, on great circles.
 *
 * @param k The gradient of the height in space
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param strength Strength
 * @param dx Receives the difference east - west
 * @param dy Receives the difference south - north
 */
static void tn_sphere_differences(
    const double k[3],
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    double strength,
    double *dx,
    double *dy)
{
    double p[3], east[3], north[3];
    double scale = strength * width / 512.0;
    double step = 2.0 * sin(2.0 * TN_PI / width);
    tn_sphere_frame(x, y, width, height, p, east, north);
    *dx = tn_dot3(k, east) * step * scale;
    *dy = -tn_dot3(k, north) * step * scale;
}

/** Differences of the height k . p as TexNormal stores them on the sphere: divided by the
 * lengths of the tangents of the UV frame of a Sphere mesh (tn_sphere_tangents), so the mesh
 * decodes them to the differences, by at most TN_POLE_SCALE near the poles.
 *
 * @param k The gradient of the height in space
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param strength Strength
 * @param dx Receives the stored difference along u
 * @param dy Receives the stored difference down the image
 */
static void tn_sphere_normal_differences(
    const double k[3],
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    double strength,
    double *dx,
    double *dy)
{
    double r = 2.0 * sin(((double)y + 0.5) / height * TN_PI);
    double f = r > 1 ? 1 : (1 / r > TN_POLE_SCALE ? TN_POLE_SCALE : 1 / r);
    tn_sphere_differences(k, x, y, width, height, strength, dx, dy);
    *dx *= r * f;
    *dy *= f;
}

/** Normals of a height plane: interior texels of a plane of slope (a, b) in u and v at two
 * sizes, where the strength refers to a 512 pixel wide image so the normals are the same. */
void TextureGraphNormal_normal_plane_slope(void) {
    // textures/graph
    ecs_world_t *world = tn_world();
    ecs_entity_t dot, wave, node;
    const float weights[4] = {64, -16, 0, 0.25f};
    const int32_t widths[2] = {32, 64};
    int32_t s, x, y;
    tn_register(world, &dot, &wave);
    for (s = 0; s < 2; s ++) {
        int32_t w = widths[s], h = w / 2;
        ecs_entity_t height = tn_height(world, dot, weights);
        ecs_entity_t tex = tn_graph(world, height, false, 2, w, h, DomainPlane, &node);
        tn_wait(world, tex);
        float *texels = tn_read(world, tex, 0, w, h);
        double dx = weights[0] * 2.0 / w * 2.0 * w / 512.0;
        double dy = weights[1] * 2.0 / h * 2.0 * w / 512.0;
        test_assert(fabs(dx - 0.5) < 1e-6);
        test_assert(fabs(dy + 0.25) < 1e-6);
        for (y = 1; y < h - 1; y ++) {
            for (x = 1; x < w - 1; x ++) {
                tn_expect_normal(texels, w, x, y, dx, dy, TN_TOLERANCE_8);
            }
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** Normals of the height k . p on the sphere: every texel, the poles included, is the gradient
 * of k in the frame of the UV tangents, scaled for a Sphere mesh. Decoded as the mesh does, it
 * is the normal on the sphere, but for the rows next to the poles where it fades. */
void TextureGraphNormal_normal_sphere_slope(void) {
    // textures/graph
    ecs_world_t *world = tn_world();
    ecs_entity_t dot, wave, node;
    const float weights[4] = {12, 8, -16, 0.5f};
    const double k[3] = {12, 8, -16};
    int32_t x, y, w = 128, h = 64;
    double dx, dy;
    tn_register(world, &dot, &wave);
    ecs_entity_t height = tn_height(world, dot, weights);
    ecs_entity_t tex = tn_graph(world, height, false, 1, w, h, DomainSphere, &node);
    tn_wait(world, tex);
    float *texels = tn_read(world, tex, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tn_sphere_normal_differences(k, x, y, w, h, 1, &dx, &dy);
            tn_expect_normal(texels, w, x, y, dx, dy, 0.012);
        }
    }

    for (y = 2; y < h - 2; y ++) {
        for (x = 0; x < w; x ++) {
            double p[3], east[3], north[3], n[3], expect[3], len;
            int32_t c;
            tn_sphere_frame(x, y, w, h, p, east, north);
            tn_sphere_differences(k, x, y, w, h, 1, &dx, &dy);
            for (c = 0; c < 3; c ++) {
                expect[c] = -dx * east[c] + dy * north[c] + p[c];
            }

            len = sqrt(tn_dot3(expect, expect));
            tn_world_normal(texels, x, y, w, h, n);
            for (c = 0; c < 3; c ++) {
                if (fabs(n[c] - expect[c] / len) > 0.02) {
                    printf("texel %d, %d: world normal %f, expected %f\n", x, y, n[c],
                        expect[c] / len);
                }

                test_assert(fabs(n[c] - expect[c] / len) <= 0.02);
            }
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Slopes of the same heights: the length of the differences of TexNormal, on the interior of
 * the plane with a TexSplit height and on the whole sphere. */
void TextureGraphNormal_slope_plane_sphere(void) {
    // textures/graph
    ecs_world_t *world = tn_world();
    ecs_entity_t dot, wave, node;
    const float weights[4] = {12, 8, -16, 0.5f};
    const double k[3] = {12, 8, -16};
    int32_t x, y, w = 64, h = 32;
    double dx, dy;
    tn_register(world, &dot, &wave);

    ecs_entity_t coords = ecs_new(world);
    ecs_entity_t scaled = ecs_new(world);
    ecs_entity_t channel = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set(world, scaled, TexMultiply, {.a = coords,
        .b_value = {0, 48, 0, 0}});
    ecs_set(world, channel, TexSplit, {.input = scaled, .channel = ChannelG});
    ecs_entity_t tex = tn_graph(world, channel, true, 3, w, h, DomainPlane, &node);
    tn_wait(world, tex);
    float *texels = tn_read(world, tex, 0, w, h);
    dy = 48 * 2.0 / h * 3.0 * w / 512.0;
    for (y = 1; y < h - 1; y ++) {
        for (x = 0; x < w; x ++) {
            tn_expect(texels, w, x, y, 0, dy, 3e-3);
        }
    }

    ecs_os_free(texels);

    ecs_entity_t height = tn_height(world, dot, weights);
    tex = tn_graph(world, height, true, 1, 128, 64, DomainSphere, &node);
    tn_wait(world, tex);
    texels = tn_read(world, tex, 0, 128, 64);
    for (y = 0; y < 64; y ++) {
        for (x = 0; x < 128; x ++) {
            tn_sphere_differences(k, x, y, 128, 64, 1, &dx, &dy);
            tn_expect(texels, 128, x, y, 0, sqrt(dx * dx + dy * dy), 0.02);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** A graph and GenNormal of the same height give the same normals: the height of the graph
 * read back from its intermediate through box_texture_gen_normal, at a size that is not 512
 * nor square, wrapping across the edges. */
void TextureGraphNormal_normal_matches_gen(void) {
    // textures/graph
    ecs_world_t *world = tn_world();
    ecs_entity_t dot, wave, node;
    int32_t i, s, x, y, c, w = 64, h = 32;
    float strengths[] = {24, -3};
    tn_register(world, &dot, &wave);
    ecs_entity_t height = tn_height(world, wave, NULL);
    for (s = 0; s < 2; s ++) {
        ecs_entity_t tex = tn_graph(
            world, height, false, strengths[s], w, h, DomainPlane, &node);
        tn_wait(world, tex);
        float *texels = tn_read(world, tex, 0, w, h);
        float *heights = tn_read(world, tex, height, w, h);
        float *field = ecs_os_malloc_n(float, w * h);
        uint8_t *rgba = ecs_os_malloc_n(uint8_t, w * h * 4);
        for (i = 0; i < w * h; i ++) {
            field[i] = heights[i * 4];
        }

        box_texture_gen_normal(field, w, h, strengths[s], rgba);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                for (c = 0; c < 4; c ++) {
                    tn_expect(texels, w, x, y, c, rgba[(y * w + x) * 4 + c] / 255.0,
                        1.0 / 255.0 + 1e-3);
                }
            }
        }

        ecs_os_free(rgba);
        ecs_os_free(field);
        ecs_os_free(heights);
        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** The world space normals of a curved height on the sphere change across the seam and across
 * the poles no more than between other neighbours: no seam, flipped axis or pole artefact. */
void TextureGraphNormal_normal_sphere_seam_poles(void) {
    // textures/graph
    ecs_world_t *world = tn_world();
    ecs_entity_t dot, wave, node;
    int32_t x, y, w = 128, h = 64;
    tn_register(world, &dot, &wave);
    ecs_entity_t height = tn_height(world, wave, NULL);
    ecs_entity_t tex = tn_graph(world, height, false, 8, w, h, DomainSphere, &node);
    tn_wait(world, tex);
    float *texels = tn_read(world, tex, 0, w, h);
    double flat = 0;
    for (y = 0; y < h; y ++) {
        double inner = 0, seam;
        for (x = 0; x < w - 1; x ++) {
            double d = tn_world_distance(texels, w, h, x, y, x + 1, y);
            inner = d > inner ? d : inner;
        }

        seam = tn_world_distance(texels, w, h, w - 1, y, 0, y);
        if (seam > inner * 1.5 + 0.02) {
            printf("row %d: seam %f, neighbours up to %f\n", y, seam, inner);
        }

        test_assert(seam <= inner * 1.5 + 0.02);
    }

    for (x = 0; x < w; x ++) {
        double d = tn_world_distance(texels, w, h, x, 0, x, 1);
        flat = d > flat ? d : flat;
        d = tn_world_distance(texels, w, h, x, h - 1, x, h - 2);
        flat = d > flat ? d : flat;
    }

    for (x = 0; x < w / 2; x ++) {
        double top = tn_world_distance(texels, w, h, x, 0, x + w / 2, 0);
        double bottom = tn_world_distance(texels, w, h, x, h - 1, x + w / 2, h - 1);
        if (top > flat * 1.5 + 0.02 || bottom > flat * 1.5 + 0.02) {
            printf("column %d: across the poles %f and %f, neighbours up to %f\n", x, top,
                bottom, flat);
        }

        test_assert(top <= flat * 1.5 + 0.02);
        test_assert(bottom <= flat * 1.5 + 0.02);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Strength 0 means no relief: a TexNormal of a sloped height is the flat normal (0.5, 0.5, 1,
 * 1) and its TexSlope is 0, on the plane and on the sphere. */
void TextureGraphNormal_zero_strength_flat(void) {
    // textures/graph
    ecs_world_t *world = tn_world();
    ecs_entity_t dot, wave, node;
    const float weights[4] = {12, 8, -16, 0.5f};
    int32_t d, x, y, w = 32, h = 16;
    const TextureGraphDomain domains[2] = {DomainPlane, DomainSphere};
    tn_register(world, &dot, &wave);
    for (d = 0; d < 2; d ++) {
        ecs_entity_t height = tn_height(world, dot, weights);
        ecs_entity_t tex = tn_graph(world, height, false, 0, w, h, domains[d], &node);
        ecs_entity_t slope_tex = tn_graph(world, height, true, 0, w, h, domains[d], &node);
        tn_wait(world, tex);
        tn_wait(world, slope_tex);
        float *normals = tn_read(world, tex, 0, w, h);
        float *slopes = tn_read(world, slope_tex, 0, w, h);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                tn_expect_normal(normals, w, x, y, 0, 0, TN_TOLERANCE_8);
                tn_expect(slopes, w, x, y, 0, 0, 1e-4);
            }
        }

        ecs_os_free(slopes);
        ecs_os_free(normals);
    }

    ecs_fini(world);
}
