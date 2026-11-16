/**
 * @file TextureFlow.c
 * @brief Tests for the TexFlow node of the texture graphs: the streak of a texel and a line
 * smeared along a constant field on the plane, no smear without reach, length or field, a
 * streak bent by a curved field, vector and tiling inputs, the sphere across its seam and its
 * poles, and reach edits that do not compile.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TF_FRAMES (40)

/** Difference a 32 bit float value may have from its expected value */
#define TF_TOLERANCE_32 (1e-5f)

/** Pi */
#define TF_PI (3.14159265358979)

/** Node of a scalar kind that is 1 at one texel and 0 elsewhere, or a cone of a radius in
 * texels around the texel */
typedef struct TfSpot {
    int32_t x;          /**< Column of the texel */
    int32_t y;          /**< Row of the texel */
    float radius;       /**< Radius of the cone in texels, 0 for the texel alone */
} TfSpot;

/** Node of a scalar kind that is 1 in one column and 0 elsewhere */
typedef struct TfLine {
    int32_t x;          /**< The column */
} TfLine;

/** Node of a scalar kind that is smooth and periodic in the domain: sin(2 pi u) +
 * 0.5 cos(2 pi v) on the plane, x + 0.5 z of the direction on the sphere */
typedef struct TfWave {
    float unused;       /**< No parameter */
} TfWave;

/** Node of a vector kind that splats a scalar input into (x, x, x, 1) */
typedef struct TfSplat {
    ecs_entity_t in;    /**< Scalar input node */
    float in_value;     /**< Value of in without a node */
} TfSplat;

/** Node of a vector kind of a field: on the plane counterclockwise around the center of the
 * image (as seen with v down), on the sphere toward east */
typedef struct TfSwirl {
    float unused;       /**< No parameter */
} TfSwirl;

/** WGSL of TfSpot: a neighbourhood kind without inputs, so it gets the texel */
static const char *tf_spot_wgsl =
    "fn tf_spot(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f,\n"
    "    texel: vec2i) -> vec4f {\n"
    "    let at = vec2i(bitcast<i32>(p[0].x), bitcast<i32>(p[0].y));\n"
    "    let radius = bitcast<f32>(p[0].z);\n"
    "    if (radius <= 0.0) {\n"
    "        return vec4f(select(0.0, 1.0, all(texel == at)), 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let d = length(vec2f(texel - at));\n"
    "    return vec4f(max(1.0 - d / radius, 0.0), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TfLine */
static const char *tf_line_wgsl =
    "fn tf_line(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f,\n"
    "    texel: vec2i) -> vec4f {\n"
    "    return vec4f(select(0.0, 1.0, texel.x == bitcast<i32>(p[0].x)), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TfWave */
static const char *tf_wave_wgsl =
    "fn tf_wave(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        return vec4f(coords.x + 0.5 * coords.z, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let tau = 2.0 * BOX_TEXGRAPH_PI;\n"
    "    return vec4f(sin(tau * coords.x) + 0.5 * cos(tau * coords.y), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TfSplat */
static const char *tf_splat_wgsl =
    "fn tf_splat(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x, i[0].x, i[0].x, 1.0);\n"
    "}\n";

/** WGSL of TfSwirl */
static const char *tf_swirl_wgsl =
    "fn tf_swirl(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        return vec4f(-coords.z, 0.0, coords.x, 0.0);\n"
    "    }\n"
    "    return vec4f(-(coords.y - 0.5), coords.x - 0.5, 0.0, 0.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tf_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tf_world(void)
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
static void tf_frames(
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
static const TextureStatus* tf_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TF_FRAMES; i ++) {
        tf_frames(world, 1);
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
static float* tf_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tf_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TF_FRAMES && !result; i ++) {
        tf_frames(world, 1);
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
static void tf_expect(
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
static ecs_entity_t tf_register(
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

/** Create a TfSpot node.
 *
 * @param world The world
 * @param x Column
 * @param y Row
 * @param radius Radius of the cone, 0 for one texel
 * @return The node
 */
static ecs_entity_t tf_spot(
    ecs_world_t *world,
    int32_t x,
    int32_t y,
    float radius)
{
    ecs_entity_t c = tf_register(world, "TfSpot", ECS_SIZEOF(TfSpot),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {
                { "x", offsetof(TfSpot, x), 4, BOX_TEXTURE_PARAM_I32 },
                { "y", offsetof(TfSpot, y), 4, BOX_TEXTURE_PARAM_I32 },
                { "radius", offsetof(TfSpot, radius), 4, BOX_TEXTURE_PARAM_F32 }
            },
            .function = "tf_spot",
            .source = tf_spot_wgsl,
            .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
        });
    ecs_entity_t node = ecs_new(world);
    TfSpot s = {.x = x, .y = y, .radius = radius};
    ecs_set_id(world, node, c, ECS_SIZEOF(TfSpot), &s);

    return node;
}

/** Create a TfLine node.
 *
 * @param world The world
 * @param x The column
 * @return The node
 */
static ecs_entity_t tf_line(
    ecs_world_t *world,
    int32_t x)
{
    ecs_entity_t c = tf_register(world, "TfLine", ECS_SIZEOF(TfLine),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {{ "x", offsetof(TfLine, x), 4, BOX_TEXTURE_PARAM_I32 }},
            .function = "tf_line",
            .source = tf_line_wgsl,
            .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
        });
    ecs_entity_t node = ecs_new(world);
    TfLine l = {.x = x};
    ecs_set_id(world, node, c, ECS_SIZEOF(TfLine), &l);

    return node;
}

/** Create a TfWave node.
 *
 * @param world The world
 * @return The node
 */
static ecs_entity_t tf_wave(
    ecs_world_t *world)
{
    ecs_entity_t c = tf_register(world, "TfWave", ECS_SIZEOF(TfWave),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {{ "unused", offsetof(TfWave, unused), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "tf_wave",
            .source = tf_wave_wgsl
        });
    ecs_entity_t node = ecs_new(world);
    TfWave v = {0};
    ecs_set_id(world, node, c, ECS_SIZEOF(TfWave), &v);

    return node;
}

/** Create a TfSplat node.
 *
 * @param world The world
 * @param in The scalar input node
 * @return The node
 */
static ecs_entity_t tf_splat(
    ecs_world_t *world,
    ecs_entity_t in)
{
    ecs_entity_t c = tf_register(world, "TfSplat", ECS_SIZEOF(TfSplat),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .inputs = {{ "in", offsetof(TfSplat, in), TexScalar, "in_value",
                offsetof(TfSplat, in_value) }},
            .function = "tf_splat",
            .source = tf_splat_wgsl
        });
    ecs_entity_t node = ecs_new(world);
    TfSplat s = {.in = in};
    ecs_set_id(world, node, c, ECS_SIZEOF(TfSplat), &s);

    return node;
}

/** Create a TfSwirl node.
 *
 * @param world The world
 * @return The node
 */
static ecs_entity_t tf_swirl(
    ecs_world_t *world)
{
    ecs_entity_t c = tf_register(world, "TfSwirl", ECS_SIZEOF(TfSwirl),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .params = {{ "unused", offsetof(TfSwirl, unused), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "tf_swirl",
            .source = tf_swirl_wgsl
        });
    ecs_entity_t node = ecs_new(world);
    TfSwirl s = {0};
    ecs_set_id(world, node, c, ECS_SIZEOF(TfSwirl), &s);

    return node;
}

/** Create a graph that smears a scalar node along a field and splats the value into the image.
 *
 * @param world The world
 * @param input The scalar node to smear
 * @param field The vector node of the field, 0 for field_value
 * @param field_value Value of the field without a node, NULL for zero
 * @param reach Reach of the smear
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @param flow Receives the flow node
 * @return The texture entity
 */
static ecs_entity_t tf_graph(
    ecs_world_t *world,
    ecs_entity_t input,
    ecs_entity_t field,
    const float *field_value,
    float reach,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    ecs_entity_t *flow)
{
    ecs_entity_t tex = ecs_new(world);
    TexFlow f = {.input = input, .field = field, .length_value = 1, .reach = reach};
    *flow = ecs_new(world);
    if (field_value) {
        memcpy(f.field_value, field_value, sizeof(f.field_value));
    }

    ecs_set_ptr(world, *flow, TexFlow, &f);
    ecs_entity_t splat = tf_splat(world, *flow);
    ecs_set(world, tex, TextureGraph, {.output = splat, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Weight of the tap k of a streak of taps on each side, normalized by the weights of the
 * streak: a texel one step from the bright one gets weight 1 and the others none.
 *
 * @param taps Taps on each side
 * @param k The tap
 * @return The weight, 0 beyond the streak
 */
static double tf_weight(
    int32_t taps,
    int32_t k)
{
    double sigma = taps / 3.0, total = 0;
    int32_t j;
    if (abs(k) > taps) {
        return 0;
    }

    for (j = -taps; j <= taps; j ++) {
        total += exp(-0.5 * j * j / (sigma * sigma));
    }

    return exp(-0.5 * k * k / (sigma * sigma)) / total;
}

/** Check the streak of a bright texel smeared along +u on the plane: in its row the weights of
 * the gaussian over taps texels on each side, symmetric, 0 elsewhere.
 *
 * @param texels Four floats per texel, the value in the first
 * @param w Width
 * @param h Height
 * @param ix Column of the bright texel
 * @param iy Row of the bright texel
 * @param taps Texels of the streak on each side
 */
static void tf_expect_streak(
    const float *texels,
    int32_t w,
    int32_t h,
    int32_t ix,
    int32_t iy,
    int32_t taps)
{
    int32_t x, y;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int32_t dx = ((x - ix) % w + w) % w;
            dx = dx > w / 2 ? dx - w : dx;
            double expect = y == iy ? tf_weight(taps, dx) : 0;
            tf_expect(texels[(y * w + x) * 4], (float)expect, TF_TOLERANCE_32, x, y);
        }
    }

    for (x = 1; x <= taps; x ++) {
        float right = texels[(iy * w + (ix + x) % w) * 4];
        float left = texels[(iy * w + (ix - x + w) % w) * 4];
        tf_expect(left, right, TF_TOLERANCE_32, ix - x, iy);
        test_assert(right > 0);
    }
}

/** A bright texel smeared along a constant +u field becomes a horizontal streak of reach
 * texels of a 512 wide image on each side, symmetric, with the weights of the gaussian. A
 * bright column smeared along +u becomes a band of the same profile in every row. */
void TextureFlow_streak_plane(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float east[4] = {1, 0, 0, 0};
    int32_t w = 64, h = 32, x, y;
    ecs_entity_t spot = tf_spot(world, 20, 10, 0), flow;
    ecs_entity_t tex = tf_graph(world, spot, 0, east, 64, w, h, DomainPlane, &flow);
    test_int(tf_wait(world, tex, 1)->state, TextureReady);
    float *texels = tf_read(world, tex, flow, w, h);
    tf_expect_streak(texels, w, h, 20, 10, 8);
    ecs_os_free(texels);

    ecs_entity_t line = tf_line(world, 30), line_flow;
    ecs_entity_t tex_line = tf_graph(world, line, 0, east, 64, w, h, DomainPlane,
        &line_flow);
    test_int(tf_wait(world, tex_line, 1)->state, TextureReady);
    texels = tf_read(world, tex_line, line_flow, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int32_t dx = x - 30;
            tf_expect(texels[(y * w + x) * 4], (float)tf_weight(8, dx), TF_TOLERANCE_32, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** A bright column smeared along a constant +v field, along itself, is unchanged; so is a
 * column smeared by a field read from a node. */
void TextureFlow_line_along_field(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float south[4] = {0, 1, 0, 0};
    int32_t w = 64, h = 32, x, y;
    ecs_entity_t line = tf_line(world, 10), flow;
    ecs_entity_t tex = tf_graph(world, line, 0, south, 128, w, h, DomainPlane, &flow);
    test_int(tf_wait(world, tex, 1)->state, TextureReady);
    float *texels = tf_read(world, tex, flow, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tf_expect(texels[(y * w + x) * 4], x == 10 ? 1.0f : 0.0f, TF_TOLERANCE_32, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Without a reach, with a length of 0 or with a zero field every texel keeps its input. */
void TextureFlow_unchanged_without_smear(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float east[4] = {1, 0, 0, 0};
    const float zero[4] = {0, 0, 0, 0};
    int32_t w = 64, h = 32, x, y, c;
    ecs_entity_t wave = tf_wave(world);
    ecs_entity_t flows[3], texs[3];
    texs[0] = tf_graph(world, wave, 0, east, 0, w, h, DomainPlane, &flows[0]);
    texs[1] = tf_graph(world, wave, 0, east, 64, w, h, DomainPlane, &flows[1]);
    texs[2] = tf_graph(world, wave, 0, zero, 64, w, h, DomainPlane, &flows[2]);
    TexFlow *f = ecs_get_mut(world, flows[1], TexFlow);
    f->length_value = 0;
    ecs_modified(world, flows[1], TexFlow);
    for (c = 0; c < 3; c ++) {
        test_int(tf_wait(world, texs[c], 1)->state, TextureReady);
    }

    for (c = 0; c < 3; c ++) {
        float *input = tf_read(world, texs[c], wave, w, h);
        float *texels = tf_read(world, texs[c], flows[c], w, h);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                tf_expect(texels[(y * w + x) * 4], input[(y * w + x) * 4], TF_TOLERANCE_32,
                    x, y);
            }
        }

        ecs_os_free(texels);
        ecs_os_free(input);
    }

    ecs_fini(world);
}

/** A field that circles the center of the image bends the streak: a spot on the circle reaches
 * a texel further along the circle, off the straight line through the spot along the field
 * there, which a straight field does not reach. */
void TextureFlow_curved_field(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float south[4] = {0, 1, 0, 0};
    int32_t w = 64, h = 64, x, y;
    ecs_entity_t spot = tf_spot(world, 48, 32, 3);
    ecs_entity_t swirl = tf_swirl(world), flow, straight_flow;
    ecs_entity_t tex = tf_graph(world, spot, swirl, NULL, 112, w, h, DomainPlane, &flow);
    ecs_entity_t tex_straight = tf_graph(world, spot, 0, south, 112, w, h, DomainPlane,
        &straight_flow);
    test_int(tf_wait(world, tex, 1)->state, TextureReady);
    test_int(tf_wait(world, tex_straight, 1)->state, TextureReady);

    float *texels = tf_read(world, tex, flow, w, h);
    float *straight = tf_read(world, tex_straight, straight_flow, w, h);
    float bent = texels[(41 * w + 44) * 4];
    float line = straight[(41 * w + 44) * 4];
    printf("bent %f, straight %f\n", bent, line);
    test_assert(bent > 5e-3f);
    test_assert(line < 1e-4f);
    test_assert(bent > line);

    // the streamlines of the swirl stay finite around its center, where the field vanishes
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            test_assert(isfinite(texels[(y * w + x) * 4]));
        }
    }

    ecs_os_free(straight);
    ecs_os_free(texels);
    ecs_fini(world);
}

/** A vector input smears each channel: a bright texel splatted into a vector smears into the
 * streak of the scalar in x, y and z, its alpha stays 1, and the value is a Vector. The kind
 * takes any input and a vector field. */
void TextureFlow_vector_input(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float east[4] = {1, 0, 0, 0};
    int32_t w = 64, h = 32, x, y;
    ecs_entity_t spot = tf_spot(world, 10, 5, 0), scalar_flow;
    ecs_entity_t tex_scalar = tf_graph(world, spot, 0, east, 64, w, h, DomainPlane,
        &scalar_flow);
    ecs_entity_t vector = tf_splat(world, spot);
    ecs_entity_t flow = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, flow, TexFlow, {.input = vector, .field_value = {1, 0, 0, 0},
        .length_value = 1, .reach = 64});
    ecs_set(world, tex, TextureGraph, {.output = flow, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tf_wait(world, tex_scalar, 1)->state, TextureReady);
    test_int(tf_wait(world, tex, 1)->state, TextureReady);

    float *scalar = tf_read(world, tex_scalar, scalar_flow, w, h);
    float *texels = tf_read(world, tex, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *t = &texels[(y * w + x) * 4];
            float v = scalar[(y * w + x) * 4];
            tf_expect(t[0], v, 2e-4f, x, y);
            tf_expect(t[1], v, 2e-4f, x, y);
            tf_expect(t[2], v, 2e-4f, x, y);
            tf_expect(t[3], 1.0f, 2e-3f, x, y);
        }
    }

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexFlow), TextureNodeKind);
    test_assert(kind != NULL);
    test_bool(kind->neighbourhood, true);
    test_int(kind->output, TexAny);
    test_int(kind->inputs[0].type, TexAny);
    test_int(kind->inputs[1].type, TexVector);
    test_int(kind->inputs[2].type, TexScalar);
    ecs_entity_t fresh = ecs_new(world);
    ecs_add(world, fresh, TexFlow);
    test_flt(ecs_get(world, fresh, TexFlow)->length_value, 1);

    // the value of a scalar input is a scalar, which an input that expects a vector refuses
    ecs_entity_t split = ecs_new(world);
    ecs_entity_t tex_fail = ecs_new(world);
    ecs_set(world, split, TexSplit, {.input = scalar_flow});
    ecs_set(world, tex_fail, TextureGraph, {.output = split, .width = 8, .height = 8,
        .format = GraphHdr});
    ecs_log_set_level(-4);
    const TextureStatus *status = tf_wait(world, tex_fail, 1);
    ecs_log_set_level(-1);
    test_int(status->state, TextureFailed);

    ecs_os_free(texels);
    ecs_os_free(scalar);
    ecs_fini(world);
}

/** The smear of a pattern that tiles still tiles: sin(2 pi u) + 0.5 cos(2 pi v) smeared along
 * +u is the same waves with the sine damped by the response of the gaussian, at the edges as
 * well. */
void TextureFlow_tiling_plane(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float east[4] = {1, 0, 0, 0};
    int32_t w = 64, h = 32, x, y, k;
    ecs_entity_t wave = tf_wave(world), flow;
    ecs_entity_t tex = tf_graph(world, wave, 0, east, 64, w, h, DomainPlane, &flow);
    test_int(tf_wait(world, tex, 1)->state, TextureReady);

    float *texels = tf_read(world, tex, flow, w, h);
    double ax = 0;
    for (k = -8; k <= 8; k ++) {
        ax += tf_weight(8, k) * cos(2 * TF_PI * k / w);
    }

    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            double u = (x + 0.5) / w, v = (y + 0.5) / h;
            double expect = ax * sin(2 * TF_PI * u) + 0.5 * cos(2 * TF_PI * v);
            tf_expect(texels[(y * w + x) * 4], (float)expect, 5e-4f, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** On the sphere a field toward east smears a meridian along the latitudes: the streak spreads
 * to both sides of the seam alike and continues across it. */
void TextureFlow_sphere_seam(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    int32_t w = 64, h = 32, x, y, k;
    ecs_entity_t line = tf_line(world, 0);
    ecs_entity_t east = tf_swirl(world), flow;
    ecs_entity_t tex = tf_graph(world, line, east, NULL, 64, w, h, DomainSphere, &flow);
    test_int(tf_wait(world, tex, 1)->state, TextureReady);

    float *texels = tf_read(world, tex, flow, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            test_assert(isfinite(texels[(y * w + x) * 4]));
        }

        for (k = 1; k < 8; k ++) {
            float left = texels[(y * w + w - k) * 4], right = texels[(y * w + k) * 4];
            tf_expect(left, right, 2e-3f, w - k, y);
        }
    }

    // by the equator one texel is one step of the streak, as on the plane
    for (y = h / 2 - 1; y <= h / 2; y ++) {
        float center = texels[(y * w) * 4];
        float left = texels[(y * w + w - 1) * 4];
        float right = texels[(y * w + 1) * 4];
        test_assert(center < 0.5f);
        test_assert(left > 0.05f);
        tf_expect(left, (float)tf_weight(8, 1), 0.02f, w - 1, y);
        tf_expect(right, (float)tf_weight(8, 1), 0.02f, 1, y);
        test_assert(texels[(y * w + w / 2) * 4] < 1e-4f);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** On the sphere a streak crosses the poles and the field vanishes on them: a smooth function
 * smeared along fields across the pole, toward the pole and toward east has no NaN and stays
 * within the range of the input. */
void TextureFlow_sphere_poles(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float across[4] = {1, 0, 0, 0};
    const float up[4] = {0, 1, 0, 0};
    int32_t w = 64, h = 32, x, c;
    ecs_entity_t wave = tf_wave(world), flows[3], texs[3];
    texs[0] = tf_graph(world, wave, 0, across, 256, w, h, DomainSphere, &flows[0]);
    texs[1] = tf_graph(world, wave, 0, up, 256, w, h, DomainSphere, &flows[1]);
    texs[2] = tf_graph(world, wave, tf_swirl(world), NULL, 256, w, h, DomainSphere,
        &flows[2]);
    for (c = 0; c < 3; c ++) {
        test_int(tf_wait(world, texs[c], 1)->state, TextureReady);
    }

    for (c = 0; c < 3; c ++) {
        float *input = tf_read(world, texs[c], wave, w, h);
        float *texels = tf_read(world, texs[c], flows[c], w, h);
        float lo = input[0], hi = input[0];
        for (x = 0; x < w * h; x ++) {
            lo = input[x * 4] < lo ? input[x * 4] : lo;
            hi = input[x * 4] > hi ? input[x * 4] : hi;
        }

        for (x = 0; x < w * h; x ++) {
            float v = texels[x * 4];
            test_assert(isfinite(v));
            test_assert(v >= lo - 1e-3f && v <= hi + 1e-3f);
        }

        ecs_os_free(texels);
        ecs_os_free(input);
    }

    ecs_fini(world);
}

/** The reach and the length without a node are parameters: editing them writes the parameter
 * buffer, no stage compiles and the program is kept, and the streak follows them. */
void TextureFlow_reach_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tf_world();
    const float east[4] = {1, 0, 0, 0};
    int32_t w = 64, h = 32;
    box_render_gpu_stats_t before, after;
    ecs_entity_t spot = tf_spot(world, 20, 10, 0), flow;
    tf_frames(world, 1);
    ecs_entity_t tex = tf_graph(world, spot, 0, east, 16, w, h, DomainPlane, &flow);
    const TextureStatus *status = tf_wait(world, tex, 1);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);

    float *texels = tf_read(world, tex, flow, w, h);
    tf_expect_streak(texels, w, h, 20, 10, 2);
    ecs_os_free(texels);
    test_bool(box_render_gpu_stats(world, &before), true);

    TexFlow *f = ecs_get_mut(world, flow, TexFlow);
    f->reach = 128;
    f->length_value = 0.5f;
    ecs_modified(world, flow, TexFlow);
    status = tf_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);

    texels = tf_read(world, tex, flow, w, h);
    tf_expect_streak(texels, w, h, 20, 10, 8);
    ecs_os_free(texels);
    ecs_fini(world);
}
