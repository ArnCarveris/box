/**
 * @file TextureCoordNodes.c
 * @brief Tests for the coordinate nodes of a TextureGraph on the GPU backend: TexWarp and
 * TexTransform on the plane and the sphere, read back from their nodes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Coordinates turned a quarter, scaled twice and warped by a constant offset */
#define GRAPH_COORDS "test/assets/textures/graph/coords.flecs"

/** Frames a test waits at most for an outcome */
#define TC_FRAMES (40)

/** Difference a float channel of a node read back may have from its value: vector nodes are
 * materialized as RGBA16Float */
#define TC_TOLERANCE (2e-3f)

/** Pi */
#define TC_PI (3.14159265358979f)

/** Node of a scalar kind whose value is its value member, for an angle from a node */
typedef struct TcScalar {
    float value;        /**< The value */
} TcScalar;

/** WGSL of TcScalar */
static const char *tc_scalar_wgsl =
    "fn tc_scalar(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(bitcast<f32>(p[0].x), 0.0, 0.0, 0.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tc_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tc_world(void)
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
static void tc_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready, and check that it is.
 *
 * @param world The world
 * @param tex The texture entity
 */
static void tc_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TC_FRAMES; i ++) {
        tc_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state != TextureLoading) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("texture graph failed: %s\n", status->error);
    }

    test_int(status->state, TextureReady);
}

/** Read back the value of a node of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The node entity
 * @param width Width of the graph
 * @param height Height of the graph
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tc_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tc_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TC_FRAMES && !result; i ++) {
        tc_frames(world, 1);
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

/** Check a texel against the expected value within a tolerance.
 *
 * @param texels The texels
 * @param width Width of the texels
 * @param x Column
 * @param y Row
 * @param expect Expected RGBA
 * @param tolerance Largest difference of a channel
 */
static void tc_expect(
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

/** Get the coordinates of the center of a texel of a domain: (u, v, 0, 1) on the plane, the
 * direction of the equirectangular texel on the sphere, -z at the center column, y up at the
 * top row.
 *
 * @param domain The domain
 * @param x Column
 * @param y Row
 * @param width Width of the image
 * @param height Height of the image
 * @param out Receives the coordinates
 */
static void tc_coords(
    TextureGraphDomain domain,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    float out[4])
{
    float u = ((float)x + 0.5f) / (float)width;
    float v = ((float)y + 0.5f) / (float)height;
    if (domain == DomainSphere) {
        float phi = (u - 0.5f) * 2.0f * TC_PI, theta = v * TC_PI;
        out[0] = sinf(theta) * sinf(phi);
        out[1] = cosf(theta);
        out[2] = -sinf(theta) * cosf(phi);
    } else {
        out[0] = u;
        out[1] = v;
        out[2] = 0;
    }

    out[3] = 1;
}

/** Rotate a point right handed about a unit axis (Rodrigues).
 *
 * @param p The point
 * @param axis The unit axis
 * @param degrees The angle
 * @param out Receives the rotated point
 */
static void tc_rotate(
    const float p[3],
    const float axis[3],
    float degrees,
    float out[3])
{
    float a = degrees * TC_PI / 180.0f, c = cosf(a), s = sinf(a);
    float d = axis[0] * p[0] + axis[1] * p[1] + axis[2] * p[2];
    float cr[3] = {
        axis[1] * p[2] - axis[2] * p[1],
        axis[2] * p[0] - axis[0] * p[2],
        axis[0] * p[1] - axis[1] * p[0]
    };
    int32_t k;
    for (k = 0; k < 3; k ++) {
        out[k] = p[k] * c + cr[k] * s + axis[k] * d * (1.0f - c);
    }
}

/** Create a texture graph whose output is a node, stored as HDR.
 *
 * @param world The world
 * @param output The output node
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @return The texture entity
 */
static ecs_entity_t tc_graph(
    ecs_world_t *world,
    ecs_entity_t output,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {.output = output, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Register the scalar kind TcScalar.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tc_register_scalar(
    ecs_world_t *world)
{
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TcScalar" }),
        .type.size = ECS_SIZEOF(TcScalar),
        .type.alignment = ECS_ALIGNOF(TcScalar)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .output = TexScalar,
        .params = {{ "value", offsetof(TcScalar, value), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "tc_scalar",
        .source = tc_scalar_wgsl
    }) > 0);

    return c;
}

/** Check that identity nodes leave the coordinates unchanged in a domain: a TexTransform with
 * its defaults, a TexWarp without amount, and a TexWarp of offset 0.5, each with and without a
 * coords node.
 *
 * @param domain The domain
 */
static void tc_identity_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tc_world();
    int32_t x, y, n, w = 16, h = 8;
    float expect[4];
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t transform = ecs_new(world);
    ecs_entity_t still = ecs_new(world);
    ecs_entity_t centred = ecs_new(world);
    ecs_entity_t chained = ecs_new(world);
    ecs_entity_t out = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, transform, TexTransform, {.scale = {1, 1, 1}});
    ecs_set(world, still, TexWarp, {.offset_value = {0.9f, 0.1f, 0.3f, 0}});
    ecs_set(world, centred, TexWarp, {.coords = uv, .offset_value = {0.5f, 0.5f, 0.5f, 0.5f},
        .amount_value = 3});
    ecs_set(world, chained, TexTransform, {.coords = centred, .scale = {1, 1, 1}});
    ecs_set(world, out, TexAdd, {.a = chained, .b = still});
    ecs_entity_t tex = tc_graph(world, out, w, h, domain);
    ecs_entity_t extra = tc_graph(world, transform, w, h, domain);
    tc_wait(world, tex);
    tc_wait(world, extra);

    ecs_entity_t nodes[4] = { still, centred, chained, transform };
    ecs_entity_t texs[4] = { tex, tex, tex, extra };
    for (n = 0; n < 4; n ++) {
        float *texels = tc_read(world, texs[n], nodes[n], w, h);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                tc_coords(domain, x, y, w, h, expect);
                tc_expect(texels, w, x, y, expect, TC_TOLERANCE);
            }
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

void TextureCoordNodes_identity_plane(void) {
    // textures/graph
    tc_identity_in(DomainPlane);
}

void TextureCoordNodes_identity_sphere(void) {
    // textures/graph
    tc_identity_in(DomainSphere);
}

/** A TexWarp by a constant offset on the plane shifts the coordinates by (offset - 0.5) *
 * amount, with the offset and the amount from nodes or from their values. */
void TextureCoordNodes_warp_constant_plane(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    ecs_entity_t scalar_c = tc_register_scalar(world);
    int32_t x, y, w = 16, h = 8;
    float expect[4];
    ecs_entity_t offset = ecs_new(world);
    ecs_entity_t amount = ecs_new(world);
    ecs_entity_t by_value = ecs_new(world);
    ecs_entity_t by_node = ecs_new(world);
    ecs_entity_t out = ecs_new(world);
    TcScalar a = {.value = 0.25f};
    ecs_set(world, offset, TexConstant, {.value = {0.9f, 0.3f, 0.5f, 0.5f}});
    ecs_set_id(world, amount, scalar_c, ECS_SIZEOF(TcScalar), &a);
    ecs_set(world, by_value, TexWarp, {.offset_value = {0.7f, 0.4f, 0.5f, 0},
        .amount_value = 0.5f});
    ecs_set(world, by_node, TexWarp, {.offset = offset, .amount = amount});
    ecs_set(world, out, TexAdd, {.a = by_value, .b = by_node});
    ecs_entity_t tex = tc_graph(world, out, w, h, DomainPlane);
    tc_wait(world, tex);

    float *values = tc_read(world, tex, by_value, w, h);
    float *nodes = tc_read(world, tex, by_node, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tc_coords(DomainPlane, x, y, w, h, expect);
            expect[0] += 0.1f;
            expect[1] -= 0.05f;
            tc_expect(values, w, x, y, expect, TC_TOLERANCE);
            tc_expect(nodes, w, x, y, expect, TC_TOLERANCE);
        }
    }

    ecs_os_free(nodes);
    ecs_os_free(values);
    ecs_fini(world);
}

/** A TexWarp on the sphere keeps unit length: by a constant offset it is the normalized shifted
 * direction, and by an offset that varies with the coordinates it stays on the sphere. */
void TextureCoordNodes_warp_sphere_unit(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, k, w = 32, h = 16;
    float c[4], expect[4];
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t constant = ecs_new(world);
    ecs_entity_t varying = ecs_new(world);
    ecs_entity_t out = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, constant, TexWarp, {.offset_value = {0.9f, 0.2f, 0.6f, 0},
        .amount_value = 0.8f});
    ecs_set(world, varying, TexWarp, {.coords = constant, .offset = uv, .amount_value = 0.7f});
    ecs_set(world, out, TexAdd, {.a = constant, .b = varying});
    ecs_entity_t tex = tc_graph(world, out, w, h, DomainSphere);
    tc_wait(world, tex);

    float *fixed = tc_read(world, tex, constant, w, h);
    float *moving = tc_read(world, tex, varying, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *m = &moving[(y * w + x) * 4];
            float len = 0;
            tc_coords(DomainSphere, x, y, w, h, c);
            expect[0] = c[0] + 0.4f * 0.8f;
            expect[1] = c[1] - 0.3f * 0.8f;
            expect[2] = c[2] + 0.1f * 0.8f;
            for (k = 0; k < 3; k ++) {
                len += expect[k] * expect[k];
            }

            len = sqrtf(len);
            for (k = 0; k < 3; k ++) {
                expect[k] /= len;
            }

            expect[3] = 1;
            tc_expect(fixed, w, x, y, expect, TC_TOLERANCE);

            len = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            test_assert(fabsf(len - 1.0f) <= TC_TOLERANCE);
            test_assert(fabsf(m[3] - 1.0f) <= TC_TOLERANCE);
        }
    }

    ecs_os_free(moving);
    ecs_os_free(fixed);
    ecs_fini(world);
}

/** A TexTransform on the plane rotates about the image centre from +u towards +v: by its angle
 * value of 30 degrees, and by 90 degrees from a Scalar node, which is exact and with a scale and
 * a translate maps the texel lattice onto itself. */
void TextureCoordNodes_rotate_plane(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    ecs_entity_t scalar_c = tc_register_scalar(world);
    int32_t x, y, w = 16, h = 16;
    float c[4], expect[4];
    float co = cosf(30.0f * TC_PI / 180.0f), si = sinf(30.0f * TC_PI / 180.0f);
    ecs_entity_t angle = ecs_new(world);
    ecs_entity_t by_value = ecs_new(world);
    ecs_entity_t by_node = ecs_new(world);
    ecs_entity_t out = ecs_new(world);
    TcScalar a = {.value = 90};
    ecs_set_id(world, angle, scalar_c, ECS_SIZEOF(TcScalar), &a);
    ecs_set(world, by_value, TexTransform, {.angle_value = 30, .scale = {1, 1, 1}});
    ecs_set(world, by_node, TexTransform, {.angle = angle, .angle_value = 45,
        .scale = {2, 2, 1}, .translate = {0.25f, -1, 0.5f}});
    ecs_set(world, out, TexAdd, {.a = by_value, .b = by_node});
    ecs_entity_t tex = tc_graph(world, out, w, h, DomainPlane);
    tc_wait(world, tex);

    float *values = tc_read(world, tex, by_value, w, h);
    float *nodes = tc_read(world, tex, by_node, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tc_coords(DomainPlane, x, y, w, h, c);
            float dx = c[0] - 0.5f, dy = c[1] - 0.5f;
            expect[0] = co * dx - si * dy + 0.5f;
            expect[1] = si * dx + co * dy + 0.5f;
            expect[2] = 0;
            expect[3] = 1;
            tc_expect(values, w, x, y, expect, TC_TOLERANCE);

            /* A quarter turn takes (dx, dy) to (-dy, dx) */
            expect[0] = -2 * dy + 0.5f + 0.25f;
            expect[1] = 2 * dx + 0.5f - 1;
            expect[2] = 0.5f;
            tc_expect(nodes, w, x, y, expect, TC_TOLERANCE);
        }
    }

    /* The known point right of the centre moves below it, down the image */
    float *right = &values[(7 * w + 15) * 4];
    tc_coords(DomainPlane, 15, 7, w, h, c);
    test_assert(right[1] > c[1]);
    right = &nodes[(7 * w + 15) * 4];
    test_assert(fabsf(right[0] - (0.5f - 2 * (c[1] - 0.5f) + 0.25f)) <= TC_TOLERANCE);
    test_assert(fabsf(right[1] - (0.5f + 2 * (c[0] - 0.5f) - 1)) <= TC_TOLERANCE);

    ecs_os_free(nodes);
    ecs_os_free(values);
    ecs_fini(world);
}

/** A TexTransform on the sphere rotates right handed about its axis: about +y by 90 degrees the
 * direction +x goes to -z, and about a tilted axis every direction stays unit length; a scale
 * makes the length the scale. */
void TextureCoordNodes_rotate_sphere(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, k, w = 6, h = 3;
    float c[4], expect[4];
    const float up[3] = {0, 1, 0};
    const float tilt_axis[3] = {0.6f, 0.8f, 0};
    ecs_entity_t turn = ecs_new(world);
    ecs_entity_t tilted = ecs_new(world);
    ecs_entity_t scaled = ecs_new(world);
    ecs_entity_t sum = ecs_new(world);
    ecs_entity_t out = ecs_new(world);
    ecs_set(world, turn, TexTransform, {.angle_value = 90, .scale = {1, 1, 1}});
    ecs_set(world, tilted, TexTransform, {.angle_value = 37, .axis = {3, 4, 0},
        .scale = {1, 1, 1}});
    ecs_set(world, scaled, TexTransform, {.coords = tilted, .scale = {2, 2, 2}});
    ecs_set(world, sum, TexAdd, {.a = turn, .b = tilted});
    ecs_set(world, out, TexAdd, {.a = sum, .b = scaled});
    ecs_entity_t tex = tc_graph(world, out, w, h, DomainSphere);
    tc_wait(world, tex);

    float *turned = tc_read(world, tex, turn, w, h);
    float *tilt = tc_read(world, tex, tilted, w, h);
    float *big = tc_read(world, tex, scaled, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tc_coords(DomainSphere, x, y, w, h, c);
            tc_rotate(c, up, 90, expect);
            expect[3] = 1;
            tc_expect(turned, w, x, y, expect, TC_TOLERANCE);

            tc_rotate(c, tilt_axis, 37, expect);
            tc_expect(tilt, w, x, y, expect, TC_TOLERANCE);
            for (k = 0; k < 3; k ++) {
                expect[k] *= 2;
            }

            tc_expect(big, w, x, y, expect, 2 * TC_TOLERANCE);
            const float *b = &big[(y * w + x) * 4];
            test_assert(fabsf(sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]) - 2) <=
                2 * TC_TOLERANCE);
        }
    }

    /* Texel (4, 1) is the direction +x: u 0.75 on the equator */
    tc_coords(DomainSphere, 4, 1, w, h, c);
    test_assert(fabsf(c[0] - 1) <= 1e-5f);
    const float minus_z[4] = {0, 0, -1, 1};
    tc_expect(turned, w, 4, 1, minus_z, TC_TOLERANCE);

    ecs_os_free(big);
    ecs_os_free(tilt);
    ecs_os_free(turned);
    ecs_fini(world);
}

/** The coordinates of a rotation about a tilted axis warped by an offset of the coordinates on
 * the sphere are continuous across the longitude seam: the last column is as close to the first
 * as neighbouring columns are to each other. */
void TextureCoordNodes_sphere_seam(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, k, w = 64, h = 32;
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t turn = ecs_new(world);
    ecs_entity_t warp = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 0.5f});
    ecs_set(world, turn, TexTransform, {.angle_value = 40, .axis = {1, 1, 0.5f},
        .scale = {1, 1, 1}});
    ecs_set(world, warp, TexWarp, {.coords = turn, .offset = uv, .amount_value = 0.6f});
    ecs_entity_t tex = tc_graph(world, warp, w, h, DomainSphere);
    tc_wait(world, tex);

    float *texels = tc_read(world, tex, warp, w, h);
    for (y = 1; y < h - 1; y ++) {
        float seam = 0, step = 0;
        for (k = 0; k < 3; k ++) {
            float first = texels[(y * w) * 4 + k];
            float second = texels[(y * w + 1) * 4 + k];
            float last = texels[(y * w + w - 1) * 4 + k];
            float before = texels[(y * w + w - 2) * 4 + k];
            seam += (first - last) * (first - last);
            step = fmaxf(step, fabsf(first - second));
            step = fmaxf(step, fabsf(last - before));
        }

        seam = sqrtf(seam);
        if (seam > 2 * sqrtf(3.0f) * step + TC_TOLERANCE) {
            printf("row %d: seam %f, step %f\n", y, seam, step);
        }

        test_assert(seam <= 2 * sqrtf(3.0f) * step + TC_TOLERANCE);
        for (x = 0; x < w; x ++) {
            const float *t = &texels[(y * w + x) * 4];
            test_assert(fabsf(sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]) - 1) <=
                TC_TOLERANCE);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The graph of GRAPH_COORDS: a quarter turn of the coordinates scaled twice, warped by 0.1
 * along u. */
void TextureCoordNodes_script(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 64, h = 64;
    float c[4], expect[4];
    char *path = box_app_locate(GRAPH_COORDS);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t tex = ecs_lookup(world, "graph_coords");
    ecs_entity_t warped = ecs_lookup(world, "graph_coords.warped");
    test_assert(tex != 0 && warped != 0);
    tc_wait(world, tex);

    float *texels = tc_read(world, tex, warped, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tc_coords(DomainPlane, x, y, w, h, c);
            expect[0] = -2 * (c[1] - 0.5f) + 0.5f + 0.1f;
            expect[1] = 2 * (c[0] - 0.5f) + 0.5f;
            expect[2] = 0;
            expect[3] = 1;
            tc_expect(texels, w, x, y, expect, TC_TOLERANCE);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** A scale of 0 collapses the coordinates: TexCoords to the origin, TexTransform on the plane to
 * the centre plus its translation, with the scale of only x at 0 collapsing only x. */
void TextureCoordNodes_zero_scale(void) {
    // textures/graph
    ecs_world_t *world = tc_world();
    int32_t x, y, w = 16, h = 8;
    float expect[4];
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t collapsed = ecs_new(world);
    ecs_entity_t flat_x = ecs_new(world);
    ecs_entity_t out = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 0});
    ecs_set(world, collapsed, TexTransform, {.translate = {0.1f, 0.2f, 0},
        .scale = {0, 0, 0}});
    ecs_set(world, flat_x, TexTransform, {.scale = {0, 1, 1}});
    ecs_set(world, out, TexAdd, {.a = collapsed, .b = flat_x});
    ecs_entity_t tex = tc_graph(world, out, w, h, DomainPlane);
    ecs_entity_t coords = tc_graph(world, uv, w, h, DomainPlane);
    tc_wait(world, tex);
    tc_wait(world, coords);

    float *zero = tc_read(world, coords, uv, w, h);
    float *all = tc_read(world, tex, collapsed, w, h);
    float *only_x = tc_read(world, tex, flat_x, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float origin[4] = {0, 0, 0, 1};
            const float centre[4] = {0.6f, 0.7f, 0, 1};
            tc_expect(zero, w, x, y, origin, TC_TOLERANCE);
            tc_expect(all, w, x, y, centre, TC_TOLERANCE);
            tc_coords(DomainPlane, x, y, w, h, expect);
            expect[0] = 0.5f;
            tc_expect(only_x, w, x, y, expect, TC_TOLERANCE);
        }
    }

    ecs_os_free(only_x);
    ecs_os_free(all);
    ecs_os_free(zero);
    ecs_fini(world);
}
