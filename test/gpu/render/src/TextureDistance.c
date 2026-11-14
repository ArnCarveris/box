/**
 * @file TextureDistance.c
 * @brief Tests for the TexDistance node of the texture graphs: the signed distance to the 0.5
 * contour of a wave and of a hard edged band on the plane and of caps on the sphere against their exact distances, the
 * range that limits it in units that do not change with the width, a zero range, an input
 * without a node and range edits that do not compile.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TD_FRAMES (40)

/** Difference in texels of the image a distance may have from the exact one on the plane */
#define TD_TOLERANCE_PLANE (0.05)

/** Difference in texels of the image a distance may have from the exact one on the sphere */
#define TD_TOLERANCE_SPHERE (0.75)

/** Pi */
#define TD_PI (3.14159265358979)

/** Node of a scalar kind that crosses 0.5 twice along u on the plane: 0.5 + 0.5 sin(2 pi u),
 * at least 0.5 for u in 0..0.5 */
typedef struct TdWave {
    float unused;       /**< No parameter */
} TdWave;

/** Node of a scalar kind with hard edges on the plane: 1 for u in 0.25..0.75, else 0 */
typedef struct TdBand {
    float unused;       /**< No parameter */
} TdBand;

/** Node of a scalar kind of a cap on the sphere: 0.5 + dot(direction, axis) - cosine, at
 * least 0.5 within the angle acos(cosine) of the axis */
typedef struct TdCap {
    vec4 cap;           /**< The unit axis in xyz, the cosine of the angle in w */
} TdCap;

/** WGSL of TdWave */
static const char *td_wave_wgsl =
    "fn td_wave(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(0.5 + 0.5 * sin(2.0 * BOX_TEXGRAPH_PI * coords.x), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TdBand */
static const char *td_band_wgsl =
    "fn td_band(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(select(0.0, 1.0, coords.x >= 0.25 && coords.x < 0.75), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TdCap */
static const char *td_cap_wgsl =
    "fn td_cap(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let cap = bitcast<vec4f>(p[0]);\n"
    "    return vec4f(0.5 + dot(coords.xyz, cap.xyz) - cap.w, 0.0, 0.0, 0.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t td_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* td_world(void)
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
static void td_frames(
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
static const TextureStatus* td_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TD_FRAMES; i ++) {
        td_frames(world, 1);
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
static float* td_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = td_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TD_FRAMES && !result; i ++) {
        td_frames(world, 1);
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
static void td_expect(
    double value,
    double expect,
    double tolerance,
    int32_t x,
    int32_t y)
{
    if (fabs(value - expect) > tolerance) {
        printf("texel %d, %d: %f, expected %f\n", x, y, value, expect);
    }

    test_assert(fabs(value - expect) <= tolerance);
}

/** Register a node kind of a test component, once.
 *
 * @param world The world
 * @param name Name of the component
 * @param size Bytes of the component
 * @param desc The kind, its component is set here
 * @return The component
 */
static ecs_entity_t td_register(
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

/** Create a TdWave node.
 *
 * @param world The world
 * @return The node
 */
static ecs_entity_t td_wave(
    ecs_world_t *world)
{
    ecs_entity_t c = td_register(world, "TdWave", ECS_SIZEOF(TdWave),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {{ "unused", offsetof(TdWave, unused), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "td_wave",
            .source = td_wave_wgsl
        });
    ecs_entity_t e = ecs_new(world);
    TdWave wave = {0};
    ecs_set_id(world, e, c, ECS_SIZEOF(TdWave), &wave);

    return e;
}

/** Create a TdBand node.
 *
 * @param world The world
 * @return The node
 */
static ecs_entity_t td_band(
    ecs_world_t *world)
{
    ecs_entity_t c = td_register(world, "TdBand", ECS_SIZEOF(TdBand),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {{ "unused", offsetof(TdBand, unused), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "td_band",
            .source = td_band_wgsl
        });
    ecs_entity_t e = ecs_new(world);
    TdBand band = {0};
    ecs_set_id(world, e, c, ECS_SIZEOF(TdBand), &band);

    return e;
}

/** Create a TdCap node.
 *
 * @param world The world
 * @param axis Axis of the cap, normalized here
 * @param angle Angle of the cap from its axis in radians
 * @return The node
 */
static ecs_entity_t td_cap(
    ecs_world_t *world,
    const double axis[3],
    double angle)
{
    ecs_entity_t c = td_register(world, "TdCap", ECS_SIZEOF(TdCap),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {{ "cap", offsetof(TdCap, cap), 16, BOX_TEXTURE_PARAM_F32 }},
            .function = "td_cap",
            .source = td_cap_wgsl
        });
    double l = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    ecs_entity_t e = ecs_new(world);
    TdCap cap = {.cap = {(float)(axis[0] / l), (float)(axis[1] / l), (float)(axis[2] / l),
        (float)cos(angle)}};
    ecs_set_id(world, e, c, ECS_SIZEOF(TdCap), &cap);

    return e;
}

/** Create a graph whose image is the distance to the contour of an input node.
 *
 * @param world The world
 * @param input The scalar input node, 0 for none
 * @param input_value Value of the input without a node
 * @param range Range of the distance
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @param dist Receives the distance node
 * @return The texture entity
 */
static ecs_entity_t td_graph(
    ecs_world_t *world,
    ecs_entity_t input,
    float input_value,
    float range,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    ecs_entity_t *dist)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t splat = ecs_new(world);
    *dist = ecs_new(world);
    ecs_set(world, *dist, TexDistance, {.input = input, .input_value = input_value,
        .range = range});
    ecs_set(world, splat, TexSplat, {.in = *dist});
    ecs_set(world, tex, TextureGraph, {.output = splat, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Exact distance of TdWave at a column: to u 0, 0.5 or 1, positive for u in 0..0.5, in
 * texels of a 512 pixel wide image and limited to the range.
 *
 * @param x Column
 * @param w Width
 * @param range Range
 * @return The distance
 */
static double td_wave_expect(
    int32_t x,
    int32_t w,
    double range)
{
    double u = (x + 0.5) / w;
    double d = fmin(fabs(u - 0.5), fmin(u, 1 - u)) * 512;
    double side = u <= 0.5 ? 1 : -1;

    return side * fmin(d, range);
}

/** Exact distance of TdBand at a column: to u 0.25 or 0.75, positive for u in 0.25..0.75, in
 * texels of a 512 pixel wide image and limited to the range.
 *
 * @param x Column
 * @param w Width
 * @param range Range
 * @return The distance
 */
static double td_band_expect(
    int32_t x,
    int32_t w,
    double range)
{
    double u = (x + 0.5) / w;
    double d = fmin(fabs(u - 0.25), fabs(u - 0.75)) * 512;
    double side = u >= 0.25 && u < 0.75 ? 1 : -1;

    return side * fmin(d, range);
}

/** Exact distance of TdCap at a texel of the sphere, in texels of a 512 pixel wide image of
 * the equator and limited to the range.
 *
 * @param x Column
 * @param y Row
 * @param w Width
 * @param h Height
 * @param axis Axis of the cap
 * @param angle Angle of the cap
 * @param range Range
 * @return The distance
 */
static double td_cap_expect(
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const double axis[3],
    double angle,
    double range)
{
    double phi = ((x + 0.5) / w - 0.5) * 2 * TD_PI, theta = (y + 0.5) / h * TD_PI;
    double d[3] = {sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi)};
    double l = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    double c = (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / l;
    double a = acos(fmax(-1, fmin(1, c)));
    double side = c >= cos(angle) ? 1 : -1;

    return side * fmin(fabs(a - angle) * 256 / TD_PI, range);
}

/** Check the distance to the contour of TdWave on the plane.
 *
 * @param w Width
 * @param h Height
 * @param range Range
 */
static void td_wave_in(
    int32_t w,
    int32_t h,
    float range)
{
    ecs_world_t *world = td_world();
    ecs_entity_t dist;
    ecs_entity_t tex = td_graph(world, td_wave(world), 0, range, w, h, DomainPlane, &dist);
    int32_t x, y;
    test_int(td_wait(world, tex, 1)->state, TextureReady);

    float *texels = td_read(world, tex, dist, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            td_expect(texels[(y * w + x) * 4], td_wave_expect(x, w, range),
                TD_TOLERANCE_PLANE * 512 / w, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Check the distance to the contour of TdCap on the sphere.
 *
 * @param axis Axis of the cap
 * @param angle Angle of the cap in radians
 * @param range Range
 */
static void td_cap_in(
    const double axis[3],
    double angle,
    float range)
{
    ecs_world_t *world = td_world();
    int32_t w = 128, h = 64, x, y;
    ecs_entity_t dist;
    ecs_entity_t tex = td_graph(world, td_cap(world, axis, angle), 0, range, w, h,
        DomainSphere, &dist);
    test_int(td_wait(world, tex, 1)->state, TextureReady);

    float *texels = td_read(world, tex, dist, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            td_expect(texels[(y * w + x) * 4], td_cap_expect(x, y, w, h, axis, angle, range),
                TD_TOLERANCE_SPHERE * 512 / w, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The contour of an input of 0 and 1 is between the texels, where the input interpolated
 * between them is 0.5, so a band whose edges are between texels has its exact distance */
void TextureDistance_hard_edges(void) {
    // textures/graph
    ecs_world_t *world = td_world();
    int32_t w = 64, h = 16, x, y;
    ecs_entity_t dist;
    ecs_entity_t tex = td_graph(world, td_band(world), 0, 200, w, h, DomainPlane, &dist);
    test_int(td_wait(world, tex, 1)->state, TextureReady);

    float *texels = td_read(world, tex, dist, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            td_expect(texels[(y * w + x) * 4], td_band_expect(x, w, 200),
                TD_TOLERANCE_PLANE * 512 / w, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** On the plane the distance is the distance to the nearest of the two contours of the wave,
 * wrapped around the edges, positive on the side at least 0.5. */
void TextureDistance_wave_plane(void) {
    // textures/graph
    td_wave_in(64, 32, 200);
}

/** The distance stops at the range, at either sign. */
void TextureDistance_range_limits(void) {
    // textures/graph
    td_wave_in(64, 32, 20);
}

/** The range and the distance are in texels of a 512 pixel wide image: the same graph four
 * times wider has the same distances at the same coordinates. */
void TextureDistance_scales_with_size(void) {
    // textures/graph
    td_wave_in(64, 16, 60);
    td_wave_in(256, 16, 60);
}

/** A range of 0 makes the value 0 everywhere. */
void TextureDistance_zero_range(void) {
    // textures/graph
    ecs_world_t *world = td_world();
    int32_t w = 32, h = 16, i;
    ecs_entity_t dist;
    ecs_entity_t tex = td_graph(world, td_wave(world), 0, 0, w, h, DomainPlane, &dist);
    test_int(td_wait(world, tex, 1)->state, TextureReady);

    float *texels = td_read(world, tex, dist, w, h);
    for (i = 0; i < w * h; i ++) {
        test_assert(texels[i * 4] == 0);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** An input without a node has no contour: the value is the range, with the sign of the side
 * of input_value. */
void TextureDistance_without_input(void) {
    // textures/graph
    ecs_world_t *world = td_world();
    int32_t w = 32, h = 16, i;
    ecs_entity_t inside, outside;
    ecs_entity_t tex_in = td_graph(world, 0, 0.75f, 7, w, h, DomainSphere, &inside);
    ecs_entity_t tex_out = td_graph(world, 0, 0.25f, 7, w, h, DomainPlane, &outside);
    test_int(td_wait(world, tex_in, 1)->state, TextureReady);
    test_int(td_wait(world, tex_out, 1)->state, TextureReady);

    float *in = td_read(world, tex_in, inside, w, h);
    float *out = td_read(world, tex_out, outside, w, h);
    for (i = 0; i < w * h; i ++) {
        td_expect(in[i * 4], 7, 1e-6, i % w, i / w);
        td_expect(out[i * 4], -7, 1e-6, i % w, i / w);
    }

    ecs_os_free(out);
    ecs_os_free(in);
    ecs_fini(world);
}

/** On the sphere the distance is along great circles: a cap around the north pole, whose
 * contour is a circle of latitude. */
void TextureDistance_sphere_pole_cap(void) {
    // textures/graph
    const double axis[3] = {0, 1, 0};
    td_cap_in(axis, TD_PI / 3, 40);
}

/** A cap centered on the seam of the longitude: the distance continues across the seam. */
void TextureDistance_sphere_seam(void) {
    // textures/graph
    const double axis[3] = {0, 0.3, 1};
    td_cap_in(axis, 50 * TD_PI / 180, 40);
}

/** A cap tilted from the pole that holds it: the flood crosses the pole. */
void TextureDistance_sphere_across_pole(void) {
    // textures/graph
    const double axis[3] = {sin(20 * TD_PI / 180), cos(20 * TD_PI / 180), 0};
    td_cap_in(axis, 35 * TD_PI / 180, 40);
}

/** The range is a parameter: editing it writes the parameter buffer, no stage compiles and the
 * program is kept, and the image follows the new range. */
void TextureDistance_range_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = td_world();
    int32_t w = 64, h = 16, x, y;
    box_render_gpu_stats_t before, after;
    ecs_entity_t wave = td_wave(world), dist;
    td_frames(world, 1);
    ecs_entity_t tex = td_graph(world, wave, 0, 10, w, h, DomainPlane, &dist);
    const TextureStatus *status = td_wait(world, tex, 1);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);

    float *texels = td_read(world, tex, dist, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            td_expect(texels[(y * w + x) * 4], td_wave_expect(x, w, 10),
                TD_TOLERANCE_PLANE * 512 / w, x, y);
        }
    }

    ecs_os_free(texels);
    test_bool(box_render_gpu_stats(world, &before), true);

    ecs_set(world, dist, TexDistance, {.input = wave, .range = 100});
    status = td_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);

    texels = td_read(world, tex, dist, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            td_expect(texels[(y * w + x) * 4], td_wave_expect(x, w, 100),
                TD_TOLERANCE_PLANE * 512 / w, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The input is a Scalar: a Vector input fails the graph. TextureNodeKind describes the passes
 * of the flood. */
void TextureDistance_scalar_input(void) {
    // textures/graph
    ecs_world_t *world = td_world();
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t dist = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, dist, TexDistance, {.input = uv, .range = 8});
    ecs_set(world, tex, TextureGraph, {.output = dist, .width = 8, .height = 8,
        .format = GraphHdr});
    ecs_log_set_level(-4);
    const TextureStatus *status = td_wait(world, tex, 1);
    ecs_log_set_level(-1);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "Scalar") != NULL);

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexDistance), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->passes, BOX_TEX_DISTANCE_PASSES);
    test_bool(kind->neighbourhood, true);
    test_int(kind->output, TexScalar);
    test_int(kind->inputs[0].type, TexScalar);
    ecs_fini(world);
}
