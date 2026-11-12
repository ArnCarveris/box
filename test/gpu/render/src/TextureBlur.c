/**
 * @file TextureBlur.c
 * @brief Tests for the TexBlur node of the texture graphs: the blur of a constant, of an
 * impulse and of a periodic pattern on the plane, the sphere against a reference of its
 * equirectangular blur with its seam and poles, and radius edits that do not compile.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TB_FRAMES (40)

/** Difference a half float channel of a value up to 1 may have from its value */
#define TB_TOLERANCE_16 (2e-3f)

/** Difference a 32 bit float value may have from the reference of the blur */
#define TB_TOLERANCE_32 (5e-5f)

/** Pi */
#define TB_PI (3.14159265358979)

/** Taps on each side of a pass of TexBlur at most, as its WGSL loops */
#define TB_TAPS (64)

/** Node of a scalar kind that is 1 at one texel and 0 elsewhere */
typedef struct TbImpulse {
    int32_t x;          /**< Column of the texel */
    int32_t y;          /**< Row of the texel */
} TbImpulse;

/** Node of a scalar kind that is smooth and periodic in the domain: sin(2 pi u) +
 * 0.5 cos(2 pi v) on the plane, x + 0.5 z of the direction on the sphere */
typedef struct TbWave {
    float unused;       /**< No parameter */
} TbWave;

/** Node of a vector kind that splats a scalar input into (x, x, x, 1) */
typedef struct TbSplat {
    ecs_entity_t in;    /**< Scalar input node */
    float in_value;     /**< Value of in without a node */
} TbSplat;

/** WGSL of TbImpulse: a neighbourhood kind without inputs, so it gets the texel */
static const char *tb_impulse_wgsl =
    "fn tb_impulse(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f,\n"
    "    texel: vec2i) -> vec4f {\n"
    "    let at = vec2i(bitcast<i32>(p[0].x), bitcast<i32>(p[0].y));\n"
    "    return vec4f(select(0.0, 1.0, all(texel == at)), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TbWave */
static const char *tb_wave_wgsl =
    "fn tb_wave(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    if (box_texgraph_stage.domain == 1u) {\n"
    "        return vec4f(coords.x + 0.5 * coords.z, 0.0, 0.0, 0.0);\n"
    "    }\n"
    "    let tau = 2.0 * BOX_TEXGRAPH_PI;\n"
    "    return vec4f(sin(tau * coords.x) + 0.5 * cos(tau * coords.y), 0.0, 0.0, 0.0);\n"
    "}\n";

/** WGSL of TbSplat */
static const char *tb_splat_wgsl =
    "fn tb_splat(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(i[0].x, i[0].x, i[0].x, 1.0);\n"
    "}\n";

/** Ticket of the next readback */
static uint32_t tb_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tb_world(void)
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
static void tb_frames(
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
static const TextureStatus* tb_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TB_FRAMES; i ++) {
        tb_frames(world, 1);
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
static float* tb_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tb_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TB_FRAMES && !result; i ++) {
        tb_frames(world, 1);
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
static void tb_expect(
    float value,
    float expect,
    float tolerance,
    int32_t x,
    int32_t y)
{
    if (fabsf(value - expect) > tolerance) {
        printf("texel %d, %d: %f, expected %f\n", x, y, value, expect);
    }

    test_assert(fabsf(value - expect) <= tolerance);
}

/** Register a node kind of a test component.
 *
 * @param world The world
 * @param name Name of the component
 * @param size Bytes of the component
 * @param desc The kind, its component is set here
 * @return The component
 */
static ecs_entity_t tb_register(
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

/** Register TbImpulse.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tb_impulse_kind(
    ecs_world_t *world)
{
    return tb_register(world, "TbImpulse", ECS_SIZEOF(TbImpulse),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {
                { "x", offsetof(TbImpulse, x), 4, BOX_TEXTURE_PARAM_I32 },
                { "y", offsetof(TbImpulse, y), 4, BOX_TEXTURE_PARAM_I32 }
            },
            .function = "tb_impulse",
            .source = tb_impulse_wgsl,
            .flags = BOX_TEXTURE_NODE_NEIGHBOURHOOD
        });
}

/** Register TbWave.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tb_wave_kind(
    ecs_world_t *world)
{
    return tb_register(world, "TbWave", ECS_SIZEOF(TbWave),
        &(box_texture_node_desc_t){
            .output = TexScalar,
            .params = {{ "unused", offsetof(TbWave, unused), 4, BOX_TEXTURE_PARAM_F32 }},
            .function = "tb_wave",
            .source = tb_wave_wgsl
        });
}

/** Register TbSplat, once.
 *
 * @param world The world
 * @return The component
 */
static ecs_entity_t tb_splat_kind(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_lookup(world, "TbSplat");
    if (result && ecs_has(world, result, TextureNodeKind)) {
        goto done;
    }

    result = tb_register(world, "TbSplat", ECS_SIZEOF(TbSplat),
        &(box_texture_node_desc_t){
            .output = TexVector,
            .inputs = {{ "in", offsetof(TbSplat, in), TexScalar, "in_value",
                offsetof(TbSplat, in_value) }},
            .function = "tb_splat",
            .source = tb_splat_wgsl
        });
done:
    return result;
}

/** Create a graph that blurs a scalar node and splats the blurred value into the image.
 *
 * @param world The world
 * @param input The scalar node to blur
 * @param radius Radius of the blur
 * @param width Width
 * @param height Height
 * @param domain Domain
 * @param blur Receives the blur node
 * @return The texture entity
 */
static ecs_entity_t tb_scalar_graph(
    ecs_world_t *world,
    ecs_entity_t input,
    float radius,
    int32_t width,
    int32_t height,
    TextureGraphDomain domain,
    ecs_entity_t *blur)
{
    ecs_entity_t splat_c = tb_splat_kind(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t splat = ecs_new(world);
    *blur = ecs_new(world);
    ecs_set(world, *blur, TexBlur, {.input = input, .radius = radius});
    TbSplat s = {.in = *blur};
    ecs_set_id(world, splat, splat_c, ECS_SIZEOF(TbSplat), &s);
    ecs_set(world, tex, TextureGraph, {.output = splat, .width = width, .height = height,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Value of an image at integer coordinates outside it as TexBlur reads it: the plane
 * repeats, the sphere wraps in longitude and continues across a pole on the opposite meridian.
 *
 * @param img One float per texel
 * @param w Width
 * @param h Height
 * @param sphere The domain is the sphere
 * @param x Column
 * @param y Row
 * @return The value
 */
static double tb_load(
    const double *img,
    int32_t w,
    int32_t h,
    bool sphere,
    int32_t x,
    int32_t y)
{
    if (sphere) {
        int32_t period = 2 * h;
        y = ((y % period) + period) % period;
        if (y >= h) {
            y = period - 1 - y;
            x += w / 2;
        }
    } else {
        y = ((y % h) + h) % h;
    }

    x = ((x % w) + w) % w;

    return img[y * w + x];
}

/** One pass of the reference of TexBlur along an axis.
 *
 * @param in One float per texel
 * @param out Receives the pass
 * @param w Width
 * @param h Height
 * @param sphere The domain is the sphere
 * @param axis 0 along x, 1 along y
 * @param radius Radius in texels of the 512 pixel wide image
 */
static void tb_reference_pass(
    const double *in,
    double *out,
    int32_t w,
    int32_t h,
    bool sphere,
    int32_t axis,
    double radius)
{
    int32_t x, y, k;
    double scaled = (radius > 0 ? radius : 0) * w / 512.0;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            double r = scaled, sum = 0, total = 0;
            if (sphere && axis == 0) {
                double s = sin((y + 0.5) / h * TB_PI);
                r = fmin(scaled / fmax(s, 1e-6), w * 0.5);
            } else if (sphere) {
                r = scaled * 2.0 * h / w;
            }

            if (r <= 0) {
                out[y * w + x] = tb_load(in, w, h, sphere, x, y);
                continue;
            }

            int32_t taps = (int32_t)ceil(r);
            taps = taps < 1 ? 1 : taps > TB_TAPS ? TB_TAPS : taps;
            double spacing = r / taps, sigma = r / 3.0;
            for (k = -taps; k <= taps; k ++) {
                double o = k * spacing, fo = floor(o), f = o - fo;
                double wk = exp(-0.5 * o * o / (sigma * sigma));
                int32_t i = (int32_t)fo;
                double a = axis == 0 ? tb_load(in, w, h, sphere, x + i, y)
                    : tb_load(in, w, h, sphere, x, y + i);
                double b = axis == 0 ? tb_load(in, w, h, sphere, x + i + 1, y)
                    : tb_load(in, w, h, sphere, x, y + i + 1);
                sum += wk * (a + (b - a) * f);
                total += wk;
            }

            out[y * w + x] = sum / total;
        }
    }
}

/** Reference of TexBlur of a scalar image.
 *
 * @param texels Four floats per texel, the scalar in the first
 * @param w Width
 * @param h Height
 * @param sphere The domain is the sphere
 * @param radius Radius in texels of the 512 pixel wide image
 * @return One double per texel, freed with ecs_os_free
 */
static double* tb_reference(
    const float *texels,
    int32_t w,
    int32_t h,
    bool sphere,
    double radius)
{
    double *in = ecs_os_malloc_n(double, w * h);
    double *mid = ecs_os_malloc_n(double, w * h);
    double *out = ecs_os_malloc_n(double, w * h);
    int32_t i;
    for (i = 0; i < w * h; i ++) {
        in[i] = texels[i * 4];
    }

    tb_reference_pass(in, mid, w, h, sphere, 0, radius);
    tb_reference_pass(mid, out, w, h, sphere, 1, radius);
    ecs_os_free(mid);
    ecs_os_free(in);

    return out;
}

/** Check the blur of a node against the reference of the blur of its input.
 *
 * @param input Four floats per texel of the input
 * @param blurred Four floats per texel of the blur
 * @param w Width
 * @param h Height
 * @param sphere The domain is the sphere
 * @param radius Radius of the blur
 */
static void tb_expect_reference(
    const float *input,
    const float *blurred,
    int32_t w,
    int32_t h,
    bool sphere,
    float radius)
{
    double *expect = tb_reference(input, w, h, sphere, radius);
    int32_t x, y;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            tb_expect(blurred[(y * w + x) * 4], (float)expect[y * w + x], TB_TOLERANCE_32,
                x, y);
        }
    }

    ecs_os_free(expect);
}

/** Check that the blur of a constant is the constant in a domain, for a constant node and for
 * the constant of the input without a node.
 *
 * @param domain The domain
 */
static void tb_constant_in(
    TextureGraphDomain domain)
{
    ecs_world_t *world = tb_world();
    const float value[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    int32_t x, y, c, w = 64, h = 32;
    ecs_entity_t constant = ecs_new(world);
    ecs_entity_t blur = ecs_new(world);
    ecs_entity_t fallback = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t tex_fallback = ecs_new(world);
    ecs_set(world, constant, TexConstant, {.value = {value[0], value[1], value[2], value[3]}});
    ecs_set(world, blur, TexBlur, {.input = constant, .radius = 40});
    ecs_set(world, fallback, TexBlur, {
        .input_value = {value[0], value[1], value[2], value[3]}, .radius = 40});
    ecs_set(world, tex, TextureGraph, {.output = blur, .width = w, .height = h,
        .domain = domain, .format = GraphHdr});
    ecs_set(world, tex_fallback, TextureGraph, {.output = fallback, .width = w, .height = h,
        .domain = domain, .format = GraphHdr});
    test_int(tb_wait(world, tex, 1)->state, TextureReady);
    test_int(tb_wait(world, tex_fallback, 1)->state, TextureReady);

    float *texels = tb_read(world, tex, 0, w, h);
    float *texels_fallback = tb_read(world, tex_fallback, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            for (c = 0; c < 4; c ++) {
                tb_expect(texels[(y * w + x) * 4 + c], value[c], TB_TOLERANCE_16, x, y);
                tb_expect(texels_fallback[(y * w + x) * 4 + c], value[c], TB_TOLERANCE_16,
                    x, y);
            }
        }
    }

    ecs_os_free(texels_fallback);
    ecs_os_free(texels);
    ecs_fini(world);
}

void TextureBlur_constant_plane(void) {
    // textures/graph
    tb_constant_in(DomainPlane);
}

void TextureBlur_constant_sphere(void) {
    // textures/graph
    tb_constant_in(DomainSphere);
}

/** Check the blur of an impulse on the plane: the discrete normalized gaussian of a standard
 * deviation of a third of the radius, scaled with the width, wrapped around the edges.
 *
 * @param w Width
 * @param h Height
 */
static void tb_impulse_in(
    int32_t w,
    int32_t h)
{
    ecs_world_t *world = tb_world();
    const float radius = 64;
    const int32_t ix = 1, iy = h - 2;
    ecs_entity_t impulse_c = tb_impulse_kind(world);
    ecs_entity_t impulse = ecs_new(world), blur;
    TbImpulse imp = {.x = ix, .y = iy};
    ecs_set_id(world, impulse, impulse_c, ECS_SIZEOF(TbImpulse), &imp);
    ecs_entity_t tex = tb_scalar_graph(world, impulse, radius, w, h, DomainPlane, &blur);
    test_int(tb_wait(world, tex, 1)->state, TextureReady);

    float *texels = tb_read(world, tex, blur, w, h);
    double r = radius * w / 512.0, sigma = r / 3.0, total = 0, sum = 0, moment = 0;
    int32_t taps = (int32_t)ceil(r), k, x, y;
    test_assert(taps <= TB_TAPS);
    for (k = -taps; k <= taps; k ++) {
        total += exp(-0.5 * k * k / (sigma * sigma));
    }

    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int32_t dx = ((x - ix) % w + w) % w, dy = ((y - iy) % h + h) % h;
            dx = dx > w / 2 ? dx - w : dx;
            dy = dy > h / 2 ? dy - h : dy;
            double gx = abs(dx) <= taps ? exp(-0.5 * dx * dx / (sigma * sigma)) / total : 0;
            double gy = abs(dy) <= taps ? exp(-0.5 * dy * dy / (sigma * sigma)) / total : 0;
            float v = texels[(y * w + x) * 4];
            tb_expect(v, (float)(gx * gy), 2e-6f, x, y);
            sum += v;
            moment += v * dx * dx;
        }
    }

    // normalized, and as wide as the truncated gaussian of the scaled radius
    test_assert(fabs(sum - 1.0) < 1e-4);
    double variance = 0;
    for (k = -taps; k <= taps; k ++) {
        variance += k * k * exp(-0.5 * k * k / (sigma * sigma)) / total;
    }

    test_assert(fabs(moment - variance) < 1e-3 * variance);
    test_assert(variance > 0.8 * sigma * sigma && variance < 1.01 * sigma * sigma);
    ecs_os_free(texels);
    ecs_fini(world);
}

void TextureBlur_impulse_plane(void) {
    // textures/graph
    tb_impulse_in(64, 64);
}

void TextureBlur_impulse_scales_with_size(void) {
    // textures/graph
    tb_impulse_in(128, 64);
}

/** A vector input blurs each channel: an impulse splatted into a vector blurs into the same
 * gaussian in x, y and z, its alpha stays 1, and the value is a Vector. */
void TextureBlur_vector_input(void) {
    // textures/graph
    ecs_world_t *world = tb_world();
    int32_t w = 64, h = 32, x, y;
    ecs_entity_t impulse_c = tb_impulse_kind(world);
    ecs_entity_t impulse = ecs_new(world), scalar_blur;
    TbImpulse imp = {.x = 10, .y = 5};
    ecs_set_id(world, impulse, impulse_c, ECS_SIZEOF(TbImpulse), &imp);
    ecs_entity_t tex_scalar = tb_scalar_graph(world, impulse, 48, w, h, DomainPlane,
        &scalar_blur);
    ecs_entity_t splat = tb_splat_kind(world);
    ecs_entity_t vector = ecs_new(world);
    ecs_entity_t blur = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    TbSplat s = {.in = impulse};
    ecs_set_id(world, vector, splat, ECS_SIZEOF(TbSplat), &s);
    ecs_set(world, blur, TexBlur, {.input = vector, .radius = 48});
    ecs_set(world, tex, TextureGraph, {.output = blur, .width = w, .height = h,
        .format = GraphHdr});
    test_int(tb_wait(world, tex_scalar, 1)->state, TextureReady);
    test_int(tb_wait(world, tex, 1)->state, TextureReady);

    float *scalar = tb_read(world, tex_scalar, scalar_blur, w, h);
    float *texels = tb_read(world, tex, 0, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *t = &texels[(y * w + x) * 4];
            float v = scalar[(y * w + x) * 4];
            tb_expect(t[0], v, TB_TOLERANCE_16 * 0.1f, x, y);
            tb_expect(t[1], v, TB_TOLERANCE_16 * 0.1f, x, y);
            tb_expect(t[2], v, TB_TOLERANCE_16 * 0.1f, x, y);
            tb_expect(t[3], 1.0f, TB_TOLERANCE_16, x, y);
        }
    }

    ecs_os_free(texels);
    ecs_os_free(scalar);
    ecs_fini(world);
}

/** The blur of a pattern that tiles still tiles: sin(2 pi u) + 0.5 cos(2 pi v) blurs into
 * the same waves damped by the response of the wrapped gaussian, at the edges as well. */
void TextureBlur_tiling_plane(void) {
    // textures/graph
    ecs_world_t *world = tb_world();
    const float radius = 64;
    int32_t w = 64, h = 32, x, y, k;
    ecs_entity_t wave_c = tb_wave_kind(world);
    ecs_entity_t wave = ecs_new(world), blur;
    TbWave wv = {0};
    ecs_set_id(world, wave, wave_c, ECS_SIZEOF(TbWave), &wv);
    ecs_entity_t tex = tb_scalar_graph(world, wave, radius, w, h, DomainPlane, &blur);
    test_int(tb_wait(world, tex, 1)->state, TextureReady);

    float *input = tb_read(world, tex, wave, w, h);
    float *texels = tb_read(world, tex, blur, w, h);
    double r = radius * w / 512.0, sigma = r / 3.0, total = 0, ax = 0, ay = 0;
    int32_t taps = (int32_t)ceil(r);
    for (k = -taps; k <= taps; k ++) {
        double g = exp(-0.5 * k * k / (sigma * sigma));
        total += g;
        ax += g * cos(2 * TB_PI * k / w);
        ay += g * cos(2 * TB_PI * k / h);
    }

    ax /= total;
    ay /= total;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            double u = (x + 0.5) / w, v = (y + 0.5) / h;
            double expect = ax * sin(2 * TB_PI * u) + 0.5 * ay * cos(2 * TB_PI * v);
            tb_expect(texels[(y * w + x) * 4], (float)expect, 2e-4f, x, y);
        }
    }

    // across the edges the blur continues as inside: the step from the last column to the
    // first is the step of the waves, as is the step from the last row to the first
    for (y = 0; y < h; y ++) {
        double step = ax * (sin(2 * TB_PI * 0.5 / w) - sin(2 * TB_PI * (w - 0.5) / w));
        float d = texels[(y * w) * 4] - texels[(y * w + w - 1) * 4];
        tb_expect(d, (float)step, 4e-4f, 0, y);
    }

    tb_expect_reference(input, texels, w, h, false, radius);
    ecs_os_free(texels);
    ecs_os_free(input);
    ecs_fini(world);
}

/** On the sphere the blur of an impulse at the seam matches the reference of the
 * equirectangular blur and spreads to both sides of the seam alike. */
void TextureBlur_sphere_seam(void) {
    // textures/graph
    ecs_world_t *world = tb_world();
    const float radius = 64;
    int32_t w = 64, h = 32, x, y, k;
    ecs_entity_t impulse_c = tb_impulse_kind(world);
    ecs_entity_t wave_c = tb_wave_kind(world);
    ecs_entity_t impulse = ecs_new(world), wave = ecs_new(world), blur, wave_blur;
    TbImpulse imp = {.x = 0, .y = h / 2};
    TbWave wv = {0};
    ecs_set_id(world, impulse, impulse_c, ECS_SIZEOF(TbImpulse), &imp);
    ecs_set_id(world, wave, wave_c, ECS_SIZEOF(TbWave), &wv);
    ecs_entity_t tex = tb_scalar_graph(world, impulse, radius, w, h, DomainSphere, &blur);
    ecs_entity_t tex_wave = tb_scalar_graph(world, wave, radius, w, h, DomainSphere,
        &wave_blur);
    test_int(tb_wait(world, tex, 1)->state, TextureReady);
    test_int(tb_wait(world, tex_wave, 1)->state, TextureReady);

    float *input = tb_read(world, tex, impulse, w, h);
    float *texels = tb_read(world, tex, blur, w, h);
    tb_expect_reference(input, texels, w, h, true, radius);
    for (y = 0; y < h; y ++) {
        for (k = 1; k < 8; k ++) {
            float left = texels[(y * w + w - k) * 4], right = texels[(y * w + k) * 4];
            tb_expect(left, right, 1e-6f, w - k, y);
        }
    }

    test_assert(texels[(h / 2 * w + w - 1) * 4] > 0.5f * texels[(h / 2 * w) * 4]);

    // a smooth function of the direction stays smooth across the seam: the step from the
    // last column to the first is no larger than the steps within the row
    float *wave_in = tb_read(world, tex_wave, wave, w, h);
    float *smooth = tb_read(world, tex_wave, wave_blur, w, h);
    tb_expect_reference(wave_in, smooth, w, h, true, radius);
    for (y = 0; y < h; y ++) {
        float largest = 0;
        for (x = 1; x < w; x ++) {
            float d = fabsf(smooth[(y * w + x) * 4] - smooth[(y * w + x - 1) * 4]);
            largest = d > largest ? d : largest;
        }

        float seam = fabsf(smooth[(y * w) * 4] - smooth[(y * w + w - 1) * 4]);
        test_assert(seam <= 1.5f * largest + 1e-4f);
    }

    ecs_os_free(smooth);
    ecs_os_free(wave_in);
    ecs_os_free(texels);
    ecs_os_free(input);
    ecs_fini(world);
}

/** On the sphere the vertical taps cross a pole onto the opposite meridian and the horizontal
 * reach widens towards the poles: an impulse next to the north pole matches the reference and
 * reaches the texels across the pole, and a smooth function stays smooth across the pole. */
void TextureBlur_sphere_poles(void) {
    // textures/graph
    ecs_world_t *world = tb_world();
    const float radius = 64;
    int32_t w = 64, h = 32, x;
    ecs_entity_t impulse_c = tb_impulse_kind(world);
    ecs_entity_t wave_c = tb_wave_kind(world);
    ecs_entity_t impulse = ecs_new(world), wave = ecs_new(world), blur, wave_blur;
    TbImpulse imp = {.x = 8, .y = 0};
    TbWave wv = {0};
    ecs_set_id(world, impulse, impulse_c, ECS_SIZEOF(TbImpulse), &imp);
    ecs_set_id(world, wave, wave_c, ECS_SIZEOF(TbWave), &wv);
    ecs_entity_t tex = tb_scalar_graph(world, impulse, radius, w, h, DomainSphere, &blur);
    ecs_entity_t tex_wave = tb_scalar_graph(world, wave, radius, w, h, DomainSphere,
        &wave_blur);
    test_int(tb_wait(world, tex, 1)->state, TextureReady);
    test_int(tb_wait(world, tex_wave, 1)->state, TextureReady);

    float *input = tb_read(world, tex, impulse, w, h);
    float *texels = tb_read(world, tex, blur, w, h);
    tb_expect_reference(input, texels, w, h, true, radius);

    // the texel across the pole gets more than a texel as far away on the same meridian
    float across = texels[(0 * w + 8 + w / 2) * 4];
    float along = texels[(2 * w + 8 + w / 2) * 4];
    test_assert(across > 0);
    test_assert(across > along);

    // the reach along x is wider by the pole than at the equator, as the texels shrink
    ecs_entity_t equator = ecs_new(world), equator_blur;
    TbImpulse eq = {.x = 8, .y = h / 2};
    ecs_set_id(world, equator, impulse_c, ECS_SIZEOF(TbImpulse), &eq);
    ecs_entity_t tex_eq = tb_scalar_graph(world, equator, radius, w, h, DomainSphere,
        &equator_blur);
    test_int(tb_wait(world, tex_eq, 1)->state, TextureReady);
    float *eq_texels = tb_read(world, tex_eq, equator_blur, w, h);
    float pole_ratio = texels[(0 * w + 8 + 6) * 4] / texels[(0 * w + 8) * 4];
    float eq_ratio = eq_texels[(h / 2 * w + 8 + 6) * 4] / eq_texels[(h / 2 * w + 8) * 4];
    test_assert(pole_ratio > eq_ratio);

    // a smooth function of the direction stays smooth across the pole: the step from a texel
    // of the first row to the one across the pole is no larger than the steps down a meridian
    float *wave_in = tb_read(world, tex_wave, wave, w, h);
    float *smooth = tb_read(world, tex_wave, wave_blur, w, h);
    tb_expect_reference(wave_in, smooth, w, h, true, radius);
    for (x = 0; x < w / 2; x ++) {
        float pole = fabsf(smooth[x * 4] - smooth[(x + w / 2) * 4]);
        float down = fabsf(smooth[x * 4] - smooth[(w + x) * 4]) +
            fabsf(smooth[(x + w / 2) * 4] - smooth[(w + x + w / 2) * 4]);
        test_assert(pole <= 1.5f * down + 1e-4f);
    }

    ecs_os_free(smooth);
    ecs_os_free(wave_in);
    ecs_os_free(eq_texels);
    ecs_os_free(texels);
    ecs_os_free(input);
    ecs_fini(world);
}

/** The radius is a parameter: editing it writes the parameter buffer, no stage compiles and
 * the program is kept, and the image follows the new radius. The blur takes two dispatches
 * after the stage of its input. */
void TextureBlur_radius_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tb_world();
    int32_t w = 64, h = 32;
    box_render_gpu_stats_t before, after;
    ecs_entity_t impulse_c = tb_impulse_kind(world);
    ecs_entity_t impulse = ecs_new(world), blur;
    TbImpulse imp = {.x = 20, .y = 10};
    ecs_set_id(world, impulse, impulse_c, ECS_SIZEOF(TbImpulse), &imp);
    tb_frames(world, 1);
    test_bool(box_render_gpu_stats(world, &before), true);
    ecs_entity_t tex = tb_scalar_graph(world, impulse, 16, w, h, DomainPlane, &blur);
    const TextureStatus *status = tb_wait(world, tex, 1);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.texture_graph_evaluations - before.texture_graph_evaluations, 1);
    test_int(after.texture_graph_dispatches - before.texture_graph_dispatches, 3);
    test_int(after.texture_graph_programs, 1);

    float *input = tb_read(world, tex, impulse, w, h);
    float *texels = tb_read(world, tex, blur, w, h);
    tb_expect_reference(input, texels, w, h, false, 16);
    ecs_os_free(texels);
    test_bool(box_render_gpu_stats(world, &before), true);

    ecs_set(world, blur, TexBlur, {.input = impulse, .radius = 100});
    status = tb_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);

    texels = tb_read(world, tex, blur, w, h);
    tb_expect_reference(input, texels, w, h, false, 100);
    ecs_os_free(texels);
    ecs_os_free(input);
    ecs_fini(world);
}

/** The value of a blur has the type of its input: a scalar blur into an input that expects a
 * vector fails the graph. TextureNodeKind describes the two passes. */
void TextureBlur_follows_input_type(void) {
    // textures/graph
    ecs_world_t *world = tb_world();
    ecs_entity_t wave_c = tb_wave_kind(world);
    ecs_entity_t wave = ecs_new(world);
    ecs_entity_t blur = ecs_new(world);
    ecs_entity_t math = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    TbWave wv = {0};
    ecs_set_id(world, wave, wave_c, ECS_SIZEOF(TbWave), &wv);
    ecs_set(world, blur, TexBlur, {.input = wave, .radius = 8});
    ecs_set(world, math, TexMath, {.op = MathAdd, .a = blur});
    ecs_set(world, tex, TextureGraph, {.output = math, .width = 8, .height = 8,
        .format = GraphHdr});
    ecs_log_set_level(-4);
    const TextureStatus *status = tb_wait(world, tex, 1);
    ecs_log_set_level(-1);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "Scalar") != NULL);

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexBlur), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->passes, 2);
    test_bool(kind->follows_input, true);
    test_bool(kind->neighbourhood, true);
    test_int(kind->output, TexVector);
    ecs_fini(world);
}
