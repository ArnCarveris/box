/**
 * @file TextureTransmittance.c
 * @brief Tests for the TexTransmittance node of the texture graphs: the attenuation of a
 * constant density with the distance to the light, the shadow of a dense disc, no attenuation
 * without absorption or density, the range, a ring of density on the sphere across its seam
 * and its poles, and edits that do not compile.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TT_FRAMES (40)

/** Difference a 32 bit float value may have from its expected value */
#define TT_TOLERANCE_32 (2e-4f)

/** Pi */
#define TT_PI (3.14159265358979)

/** Node of a scalar kind on the plane: inside within radius (in widths of the image) of a
 * center in uv, outside elsewhere */
typedef struct TtDisc {
    float center[2];    /**< Center in uv */
    float radius;       /**< Radius in widths of the image, negative for none */
    float inside;       /**< Value in the disc */
    float outside;      /**< Value outside the disc */
} TtDisc;

/** Node of a scalar kind on the sphere: value where the cosine of the angle to an axis lies in
 * lo..hi, 0 elsewhere: a ring around the axis */
typedef struct TtRing {
    float axis[3];      /**< Unit axis */
    float lo;           /**< Cosine of the outer angle */
    float hi;           /**< Cosine of the inner angle */
    float value;        /**< Value in the ring */
} TtRing;

/** Node of a vector kind that splats a scalar input into (x, x, x, 1) */
typedef struct TtSplat {
    ecs_entity_t in;    /**< Scalar input node */
    float in_value;     /**< Value of in without a node */
} TtSplat;

/** WGSL of TtDisc */
static const char *tt_disc_wgsl =
    "fn tt_disc(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let center = bitcast<vec2f>(p[0].xy);\n"
    "    let radius = bitcast<f32>(p[0].z);\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    let d = length((coords.xy - center) * size) / size.x;\n"
    "    let v = select(bitcast<f32>(p[1].x), bitcast<f32>(p[0].w), d <= radius);\n"
    "    return vec4f(v, 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TtRing */
static const char *tt_ring_wgsl =
    "fn tt_ring(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let c = dot(coords.xyz, bitcast<vec3f>(p[0].xyz));\n"
    "    let inside = c >= bitcast<f32>(p[0].w) && c <= bitcast<f32>(p[1].x);\n"
    "    return vec4f(select(0.0, bitcast<f32>(p[1].y), inside), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TtSplat */
static const char *tt_splat_wgsl =
    "fn tt_splat(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x, i[0].x, i[0].x, 1.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tt_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tt_world(void)
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
static void tt_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is not loading and has at least a revision.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 * @return The status
 */
static const TextureStatus* tt_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TT_FRAMES; i ++) {
        tt_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state == TextureFailed) {
            break;
        }

        if (status && status->state == TextureReady && status->revision >= revision) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("error: %s\n", status->error ? status->error : "(none)");
    }

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
static float* tt_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tt_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TT_FRAMES && !result; i ++) {
        tt_frames(world, 1);
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

/** Check a value against the expected one within a tolerance, printing where it differs.
 *
 * @param value The value
 * @param expect The expected value
 * @param tolerance Largest difference
 * @param x Column, for the message
 * @param y Row, for the message
 */
static void tt_expect(
    float value,
    float expect,
    float tolerance,
    int32_t x,
    int32_t y)
{
    if (!(fabsf(value - expect) <= tolerance)) {
        printf("texel %d, %d: %f, expected %f\n", x, y, value, expect);
    }

    test_assert(fabsf(value - expect) <= tolerance);
}

/** Register a node kind of a test component, once.
 *
 * @param world The world
 * @param name Name of the component
 * @param size Bytes of the component
 * @param desc The kind, its component is set here
 * @return The component
 */
static ecs_entity_t tt_register(
    ecs_world_t *world,
    const char *name,
    ecs_size_t size,
    box_texture_node_desc_t *desc)
{
    ecs_entity_t c = ecs_lookup(world, name);
    if (c && ecs_has(world, c, TextureNodeKind)) {
        goto done;
    }

    c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = name }),
        .type.size = size,
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    desc->component = c;
    test_assert(box_texture_node_register(world, desc) > 0);
done:
    return c;
}

/** Create a TtDisc node.
 *
 * @param world The world
 * @param u Column of the center in uv
 * @param v Row of the center in uv
 * @param radius Radius in widths of the image, negative for none
 * @param inside Value in the disc
 * @param outside Value outside the disc
 * @return The node
 */
static ecs_entity_t tt_disc(
    ecs_world_t *world,
    float u,
    float v,
    float radius,
    float inside,
    float outside)
{
    ecs_entity_t c = tt_register(world, "TtDisc", ECS_SIZEOF(TtDisc),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {
                { "center", offsetof(TtDisc, center), 8, BOX_TEXTURE_PARAM_F32 },
                { "radius", offsetof(TtDisc, radius), 4, BOX_TEXTURE_PARAM_F32 },
                { "inside", offsetof(TtDisc, inside), 4, BOX_TEXTURE_PARAM_F32 },
                { "outside", offsetof(TtDisc, outside), 4, BOX_TEXTURE_PARAM_F32 }
            },
            .function = "tt_disc",
            .source = tt_disc_wgsl
        });
    ecs_entity_t node = ecs_new(world);
    TtDisc d = {.center = {u, v}, .radius = radius, .inside = inside, .outside = outside};
    ecs_set_id(world, node, c, ECS_SIZEOF(TtDisc), &d);

    return node;
}

/** Create a TtRing node.
 *
 * @param world The world
 * @param axis Unit axis
 * @param inner Inner angle of the ring in degrees
 * @param outer Outer angle of the ring in degrees
 * @param value Value in the ring
 * @return The node
 */
static ecs_entity_t tt_ring(
    ecs_world_t *world,
    const float *axis,
    double inner,
    double outer,
    float value)
{
    ecs_entity_t c = tt_register(world, "TtRing", ECS_SIZEOF(TtRing),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {
                { "axis", offsetof(TtRing, axis), 12, BOX_TEXTURE_PARAM_F32 },
                { "lo", offsetof(TtRing, lo), 4, BOX_TEXTURE_PARAM_F32 },
                { "hi", offsetof(TtRing, hi), 4, BOX_TEXTURE_PARAM_F32 },
                { "value", offsetof(TtRing, value), 4, BOX_TEXTURE_PARAM_F32 }
            },
            .function = "tt_ring",
            .source = tt_ring_wgsl
        });
    ecs_entity_t node = ecs_new(world);
    TtRing r = {
        .axis = {axis[0], axis[1], axis[2]},
        .lo = (float)cos(outer * TT_PI / 180),
        .hi = (float)cos(inner * TT_PI / 180),
        .value = value
    };
    ecs_set_id(world, node, c, ECS_SIZEOF(TtRing), &r);

    return node;
}

/** Create a TtSplat node.
 *
 * @param world The world
 * @param in The scalar input node
 * @return The node
 */
static ecs_entity_t tt_splat(
    ecs_world_t *world,
    ecs_entity_t in)
{
    ecs_entity_t c = tt_register(world, "TtSplat", ECS_SIZEOF(TtSplat),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .inputs = {{ "in", offsetof(TtSplat, in), TexScalar, "in_value",
                offsetof(TtSplat, in_value) }},
            .function = "tt_splat",
            .source = tt_splat_wgsl
        });
    ecs_entity_t node = ecs_new(world);
    TtSplat s = {.in = in};
    ecs_set_id(world, node, c, ECS_SIZEOF(TtSplat), &s);

    return node;
}

/** Create a graph of a TexTransmittance node splatted into the image, read back at 32 bit
 * from the node.
 *
 * @param world The world
 * @param t The node component
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @param node Receives the TexTransmittance node
 * @return The texture entity
 */
static ecs_entity_t tt_graph(
    ecs_world_t *world,
    const TexTransmittance *t,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    ecs_entity_t *node)
{
    ecs_entity_t tex = ecs_new(world);
    *node = ecs_new(world);
    ecs_set_ptr(world, *node, TexTransmittance, t);
    ecs_entity_t splat = tt_splat(world, *node);
    ecs_set(world, tex, TextureGraph, {.output = splat, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Transmittance of a constant density on the plane: exp(-absorption * density * L), L the
 * distance from the center of a texel to the light in texels of a 512 wide image, capped by
 * range.
 *
 * @param x Column
 * @param y Row
 * @param w Width
 * @param h Height
 * @param t The node, density_value the density
 * @return The value
 */
static double tt_constant(
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const TexTransmittance *t)
{
    double dx = t->light[0] * w - (x + 0.5), dy = t->light[1] * h - (y + 0.5);
    double l = sqrt(dx * dx + dy * dy) * 512.0 / w;
    if (t->range > 0 && l > t->range) {
        l = t->range;
    }

    return exp(-t->absorption * t->density_value * l);
}

/** Check every texel of a graph of a constant density on the plane against tt_constant.
 *
 * @param texels Four floats per texel, the value in the first
 * @param w Width
 * @param h Height
 * @param t The node
 * @param tolerance Largest difference
 */
static void tt_expect_constant(
    const float *texels,
    int32_t w,
    int32_t h,
    const TexTransmittance *t,
    float tolerance)
{
    int32_t x, y;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tt_expect(texels[(y * w + x) * 4], (float)tt_constant(x, y, w, h, t), tolerance,
                x, y);
        }
    }
}

/** Direction of the center of a texel of the sphere, as box_texgraph_coords.
 *
 * @param x Column
 * @param y Row
 * @param w Width
 * @param h Height
 * @param d Receives the direction
 */
static void tt_direction(
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    double *d)
{
    double phi = ((x + 0.5) / w - 0.5) * 2 * TT_PI, theta = (y + 0.5) / h * TT_PI;
    d[0] = sin(theta) * sin(phi);
    d[1] = cos(theta);
    d[2] = -sin(theta) * cos(phi);
}

/** Angle in degrees between the center of a texel of the sphere and a unit axis.
 *
 * @param x Column
 * @param y Row
 * @param w Width
 * @param h Height
 * @param axis The axis
 * @return The angle
 */
static double tt_angle(
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const float *axis)
{
    double d[3];
    tt_direction(x, y, w, h, d);
    double c = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
    c = c > 1 ? 1 : c < -1 ? -1 : c;
    return acos(c) * 180 / TT_PI;
}

/** A constant density d gives exp(-absorption * d * L) at a distance L from the light, from
 * density_value and from a node of the same constant, with the light inside and outside the
 * image. The kind is a neighbourhood kind of a Scalar density and has 64 steps when added. */
void TextureTransmittance_constant_density(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    int32_t w = 128, h = 64;
    ecs_entity_t node, node_density, node_outside;
    TexTransmittance t = {.density_value = 2, .light = {0.25f, 0.5f}, .absorption = 0.002f,
        .steps = 64};
    ecs_entity_t tex = tt_graph(world, &t, w, h, DomainPlane, &node);
    TexTransmittance tn = t;
    tn.density = tt_disc(world, 0.5f, 0.5f, -1, 0, 2);
    ecs_entity_t tex_density = tt_graph(world, &tn, w, h, DomainPlane, &node_density);
    TexTransmittance to = t;
    to.light[0] = 1.5f;
    to.light[1] = -0.25f;
    ecs_entity_t tex_outside = tt_graph(world, &to, w, h, DomainPlane, &node_outside);
    test_int(tt_wait(world, tex, 1)->state, TextureReady);
    test_int(tt_wait(world, tex_density, 1)->state, TextureReady);
    test_int(tt_wait(world, tex_outside, 1)->state, TextureReady);

    float *texels = tt_read(world, tex, node, w, h);
    tt_expect_constant(texels, w, h, &t, TT_TOLERANCE_32);
    ecs_os_free(texels);

    texels = tt_read(world, tex_density, node_density, w, h);
    tt_expect_constant(texels, w, h, &t, 1e-3f);
    ecs_os_free(texels);

    texels = tt_read(world, tex_outside, node_outside, w, h);
    tt_expect_constant(texels, w, h, &to, TT_TOLERANCE_32);
    ecs_os_free(texels);

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexTransmittance), TextureNodeKind);
    test_assert(kind != NULL);
    test_bool(kind->neighbourhood, true);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 1);
    test_int(kind->inputs[0].type, TexScalar);
    ecs_entity_t fresh = ecs_new(world);
    ecs_add(world, fresh, TexTransmittance);
    test_int(ecs_get(world, fresh, TexTransmittance)->steps, 64);

    ecs_fini(world);
}

/** A dense disc between the light and a region casts a shadow behind it: low values on the
 * far side along the line from the light, 1 on the near side and beside it. With the light on
 * the other side the shadow points the other way. */
void TextureTransmittance_disc_shadow(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    int32_t w = 128, h = 128, c;
    ecs_entity_t disc = tt_disc(world, 0.5f, 0.5f, 0.08f, 1, 0);
    ecs_entity_t nodes[2], texs[2];
    TexTransmittance t = {.density = disc, .light = {0.1f, 0.5f}, .absorption = 0.05f,
        .steps = 64};
    texs[0] = tt_graph(world, &t, w, h, DomainPlane, &nodes[0]);
    t.light[0] = 0.9f;
    texs[1] = tt_graph(world, &t, w, h, DomainPlane, &nodes[1]);
    for (c = 0; c < 2; c ++) {
        test_int(tt_wait(world, texs[c], 1)->state, TextureReady);
    }

    for (c = 0; c < 2; c ++) {
        float *texels = tt_read(world, texs[c], nodes[c], w, h);
        int32_t near_x = c ? 96 : 32, far_x = c ? 32 : 96, end_x = c ? 8 : 120;
        int32_t x, y;
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                float v = texels[(y * w + x) * 4];
                test_assert(isfinite(v));
                test_assert(v >= 0 && v <= 1 + 1e-5f);
            }
        }

        // near side and beside the disc, nothing between the texel and the light
        tt_expect(texels[(64 * w + near_x) * 4], 1, TT_TOLERANCE_32, near_x, 64);
        tt_expect(texels[(102 * w + 64) * 4], 1, TT_TOLERANCE_32, 64, 102);
        tt_expect(texels[(26 * w + 64) * 4], 1, TT_TOLERANCE_32, 64, 26);
        tt_expect(texels[(96 * w + far_x) * 4], 1, TT_TOLERANCE_32, far_x, 96);
        tt_expect(texels[(32 * w + far_x) * 4], 1, TT_TOLERANCE_32, far_x, 32);

        // the shadow behind the disc, along the line from the light through its center
        for (x = far_x; x != end_x; x += c ? -4 : 4) {
            float v = texels[(64 * w + x) * 4];
            if (!(v < 0.1f)) {
                printf("texel %d, 64: %f in the shadow\n", x, v);
            }
            test_assert(v < 0.1f);
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** Without absorption or without density every texel is 1, behind a dense disc as well. */
void TextureTransmittance_unattenuated(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    int32_t w = 64, h = 64, x, c;
    ecs_entity_t disc = tt_disc(world, 0.5f, 0.5f, 0.1f, 4, 0);
    ecs_entity_t zero = tt_disc(world, 0.5f, 0.5f, 0.1f, 0, 0);
    ecs_entity_t nodes[3], texs[3];
    TexTransmittance t0 = {.density = disc, .light = {0.1f, 0.5f}, .absorption = 0,
        .steps = 64};
    TexTransmittance t1 = {.density_value = 0, .light = {0.1f, 0.5f}, .absorption = 1,
        .steps = 64};
    TexTransmittance t2 = {.density = zero, .light = {0.1f, 0.5f}, .absorption = 1,
        .steps = 64};
    texs[0] = tt_graph(world, &t0, w, h, DomainPlane, &nodes[0]);
    texs[1] = tt_graph(world, &t1, w, h, DomainPlane, &nodes[1]);
    texs[2] = tt_graph(world, &t2, w, h, DomainPlane, &nodes[2]);
    for (c = 0; c < 3; c ++) {
        test_int(tt_wait(world, texs[c], 1)->state, TextureReady);
    }

    for (c = 0; c < 3; c ++) {
        float *texels = tt_read(world, texs[c], nodes[c], w, h);
        for (x = 0; x < w * h; x ++) {
            tt_expect(texels[x * 4], 1, TT_TOLERANCE_32, x % w, x / w);
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** The range caps the path: a constant density gives exp(-absorption * d * min(L, range)), and
 * a texel behind a dense disc further than the range from it stays lit. */
void TextureTransmittance_range_limits(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    int32_t w = 128, h = 64;
    ecs_entity_t node, node_disc, node_full;
    TexTransmittance t = {.density_value = 1, .light = {0.0f, 0.5f}, .absorption = 0.01f,
        .range = 64, .steps = 64};
    ecs_entity_t tex = tt_graph(world, &t, w, h, DomainPlane, &node);
    ecs_entity_t disc = tt_disc(world, 0.5f, 0.5f, 0.08f, 1, 0);
    TexTransmittance td = {.density = disc, .light = {0.1f, 0.5f}, .absorption = 0.05f,
        .range = 51, .steps = 64};
    ecs_entity_t tex_disc = tt_graph(world, &td, w, w, DomainPlane, &node_disc);
    td.range = 0;
    ecs_entity_t tex_full = tt_graph(world, &td, w, w, DomainPlane, &node_full);
    test_int(tt_wait(world, tex, 1)->state, TextureReady);
    test_int(tt_wait(world, tex_disc, 1)->state, TextureReady);
    test_int(tt_wait(world, tex_full, 1)->state, TextureReady);

    float *texels = tt_read(world, tex, node, w, h);
    tt_expect_constant(texels, w, h, &t, TT_TOLERANCE_32);
    tt_expect(texels[(32 * w + 100) * 4], (float)exp(-0.01 * 64), TT_TOLERANCE_32, 100, 32);
    ecs_os_free(texels);

    // 12.75 texels of the 128 wide image: the path from u 0.75 ends before the disc
    texels = tt_read(world, tex_disc, node_disc, w, w);
    tt_expect(texels[(64 * w + 96) * 4], 1, TT_TOLERANCE_32, 96, 64);
    ecs_os_free(texels);
    texels = tt_read(world, tex_full, node_full, w, w);
    test_assert(texels[(64 * w + 96) * 4] < 0.1f);
    ecs_os_free(texels);

    ecs_fini(world);
}

/** On the sphere a ring of density 20 to 40 degrees around the light, across its meridian,
 * shadows the texels beyond it by exp(-absorption * thickness), the thickness the 20 degrees
 * of the ring in units of 2 pi / 512, and leaves the texels inside it at 1. */
void TextureTransmittance_sphere_band(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    const float light[3] = {0, 0, -1};
    int32_t w = 256, h = 128, x, y;
    ecs_entity_t ring = tt_ring(world, light, 20, 40, 1), node;
    TexTransmittance t = {.density = ring, .light = {0, 0, -1}, .absorption = 0.05f,
        .steps = 256};
    ecs_entity_t tex = tt_graph(world, &t, w, h, DomainSphere, &node);
    test_int(tt_wait(world, tex, 1)->state, TextureReady);

    float *texels = tt_read(world, tex, node, w, h);
    double beyond = exp(-0.05 * 20.0 / 360.0 * 512.0);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            float v = texels[(y * w + x) * 4];
            double a = tt_angle(x, y, w, h, light);
            test_assert(isfinite(v));
            if (a < 17) {
                tt_expect(v, 1, TT_TOLERANCE_32, x, y);
            } else if (a > 45 && a < 170) {
                tt_expect(v, (float)beyond, 0.04f, x, y);
            }
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** With the light on the longitude seam the arcs cross it: the value is the same at columns
 * mirrored about the seam and continuous from the last column to the first. */
void TextureTransmittance_sphere_seam(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    const float light[3] = {0, 0, 1};
    int32_t w = 128, h = 64, x, y;
    ecs_entity_t ring = tt_ring(world, light, 15, 35, 1), node;
    TexTransmittance t = {.density = ring, .light = {0, 0, 1}, .absorption = 0.05f,
        .steps = 256};
    ecs_entity_t tex = tt_graph(world, &t, w, h, DomainSphere, &node);
    test_int(tt_wait(world, tex, 1)->state, TextureReady);

    float *texels = tt_read(world, tex, node, w, h);
    double beyond = exp(-0.05 * 20.0 / 360.0 * 512.0);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w / 2; x ++) {
            float left = texels[(y * w + w - 1 - x) * 4], right = texels[(y * w + x) * 4];
            test_assert(isfinite(left));
            test_assert(isfinite(right));
            tt_expect(left, right, 2e-3f, w - 1 - x, y);
        }
    }

    // on the equator by the seam inside the ring, beyond it on the far side
    tt_expect(texels[(h / 2 * w) * 4], 1, TT_TOLERANCE_32, 0, h / 2);
    tt_expect(texels[(h / 2 * w + w - 1) * 4], 1, TT_TOLERANCE_32, w - 1, h / 2);
    tt_expect(texels[(h / 2 * w + w / 4) * 4], (float)beyond, 0.05f, w / 4, h / 2);
    tt_expect(texels[(h / 2 * w + 3 * w / 4) * 4], (float)beyond, 0.05f, 3 * w / 4, h / 2);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** Lights at a pole, at the center of a texel and on the equator give no NaN anywhere, the
 * arcs crossing the poles and reaching the opposite direction; the texel at the light is 1. */
void TextureTransmittance_sphere_poles(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    const float pole[3] = {0, 1, 0};
    int32_t w = 64, h = 32, x, c;
    double at[3];
    tt_direction(w / 4, h / 4, w, h, at);
    ecs_entity_t ring = tt_ring(world, pole, 20, 40, 1);
    ecs_entity_t full = tt_ring(world, pole, 0, 180, 1);
    ecs_entity_t nodes[3], texs[3];
    TexTransmittance t0 = {.density = ring, .light = {0, 1, 0}, .absorption = 0.05f,
        .steps = 64};
    TexTransmittance t1 = {.density = full, .absorption = 0.05f, .steps = 64,
        .light = {(float)at[0], (float)at[1], (float)at[2]}};
    TexTransmittance t2 = {.density = full, .light = {1, 0, 0}, .absorption = 0.05f,
        .steps = 16};
    texs[0] = tt_graph(world, &t0, w, h, DomainSphere, &nodes[0]);
    texs[1] = tt_graph(world, &t1, w, h, DomainSphere, &nodes[1]);
    texs[2] = tt_graph(world, &t2, w, h, DomainSphere, &nodes[2]);
    for (c = 0; c < 3; c ++) {
        test_int(tt_wait(world, texs[c], 1)->state, TextureReady);
    }

    for (c = 0; c < 3; c ++) {
        float *texels = tt_read(world, texs[c], nodes[c], w, h);
        for (x = 0; x < w * h; x ++) {
            float v = texels[x * 4];
            if (!isfinite(v) || v < 0 || v > 1 + 1e-5f) {
                printf("graph %d texel %d, %d: %f\n", c, x % w, x / w, v);
            }
            test_assert(isfinite(v));
            test_assert(v >= 0 && v <= 1 + 1e-5f);
        }

        if (c == 0) {
            // the rows by the pole lie inside the ring, the rows by the opposite pole beyond
            for (x = 0; x < w; x ++) {
                tt_expect(texels[x * 4], 1, TT_TOLERANCE_32, x, 0);
                test_assert(texels[((h - 1) * w + x) * 4] < 0.5f);
            }
        } else if (c == 1) {
            tt_expect(texels[(h / 4 * w + w / 4) * 4], 1, 1e-3f, w / 4, h / 4);
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** The light, the absorption, the range and the steps are parameters: editing them writes the
 * parameter buffer, no stage compiles and the program is kept, and the value follows them. */
void TextureTransmittance_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tt_world();
    int32_t w = 128, h = 64;
    box_render_gpu_stats_t before, after;
    ecs_entity_t density = tt_disc(world, 0.5f, 0.5f, -1, 0, 2), node;
    tt_frames(world, 1);
    TexTransmittance t = {.density = density, .density_value = 2, .light = {0.25f, 0.5f},
        .absorption = 0.002f, .steps = 64};
    ecs_entity_t tex = tt_graph(world, &t, w, h, DomainPlane, &node);
    const TextureStatus *status = tt_wait(world, tex, 1);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);

    float *texels = tt_read(world, tex, node, w, h);
    tt_expect_constant(texels, w, h, &t, 1e-3f);
    ecs_os_free(texels);
    test_bool(box_render_gpu_stats(world, &before), true);

    TexTransmittance *m = ecs_get_mut(world, node, TexTransmittance);
    m->light[0] = 0.75f;
    m->light[1] = 0.25f;
    m->absorption = 0.004f;
    m->range = 200;
    m->steps = 32;
    t = *m;
    ecs_modified(world, node, TexTransmittance);
    status = tt_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);

    texels = tt_read(world, tex, node, w, h);
    tt_expect_constant(texels, w, h, &t, 1e-3f);
    ecs_os_free(texels);
    ecs_fini(world);
}
