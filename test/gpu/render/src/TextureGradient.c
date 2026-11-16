/**
 * @file TextureGradient.c
 * @brief Tests for the TexGradient node of the texture graphs: the uphill vector of ramps on the
 * plane, its length against TexSlope of the same height on the plane and the sphere, tangent
 * vectors toward the north pole on the sphere without a seam, strength 0 and strength edits
 * that do not compile.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TG_FRAMES (40)

/** Pi */
#define TG_PI (3.14159265358979)

/** Node of a scalar kind: a smooth function of its vector input, see tg_wave_wgsl */
typedef struct TgWave {
    ecs_entity_t in;    /**< Vector input node */
    vec4 in_value;      /**< Value of in without a node */
} TgWave;

/** WGSL of TgWave: on the plane a periodic wave of u and v, on the sphere a smooth function
 * of the direction; 0.5 + 0.25 sin(2 pi 3 x) cos(2 pi 2 y) + 0.2 sin(3 x + 1) z + 0.2 y y */
static const char *tg_wave_wgsl =
    "fn tg_wave(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let d = i[0];\n"
    "    let tau = 6.283185307179586;\n"
    "    let h = 0.5 + 0.25 * sin(tau * 3.0 * d.x) * cos(tau * 2.0 * d.y)\n"
    "        + 0.2 * sin(3.0 * d.x + 1.0) * d.z + 0.2 * d.y * d.y;\n"
    "    return vec4f(h, 0.0, 0.0, 0.0);\n"
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

/** Run frames until a texture is ready at a revision or failed and check that it is ready.
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
        if (status && (status->state == TextureFailed ||
            (status->state == TextureReady && status->revision >= revision)))
        {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state == TextureFailed) {
        printf("graph failed: %s\n", status->error ? status->error : "(none)");
    }

    test_int(status->state, TextureReady);

    return status;
}

/** Read back the value of a node of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The node entity
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
static void tg_expect(
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
static void tg_sphere_frame(
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
    double phi = (u - 0.5) * 2.0 * TG_PI, theta = v * TG_PI;
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

/** Dot product of two 3D vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return a . b
 */
static double tg_dot3(
    const double a[3],
    const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/** Distance between the first three channels of two texels.
 *
 * @param texels The texels
 * @param width Width of the texels
 * @param x0 Column of the first texel
 * @param y0 Row of the first texel
 * @param x1 Column of the second texel
 * @param y1 Row of the second texel
 * @return The distance
 */
static double tg_distance(
    const float *texels,
    int32_t width,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    const float *a = &texels[(y0 * width + x0) * 4];
    const float *b = &texels[(y1 * width + x1) * 4];
    double d = 0;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        d += ((double)a[c] - b[c]) * ((double)a[c] - b[c]);
    }

    return sqrt(d);
}

/** Register the test kind TgWave.
 *
 * @param world The world
 * @return The component of TgWave
 */
static ecs_entity_t tg_register(
    ecs_world_t *world)
{
    ecs_entity_t wave = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TgWave" }),
        .type.size = ECS_SIZEOF(TgWave),
        .type.alignment = ECS_ALIGNOF(ecs_entity_t)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = wave,
        .output = TexScalar,
        .inputs = {{ "in", offsetof(TgWave, in), TexVector, "in_value",
            offsetof(TgWave, in_value) }},
        .function = "tg_wave",
        .source = tg_wave_wgsl
    }) > 0);

    return wave;
}

/** Create a height node of a channel of the coordinates times a factor.
 *
 * @param world The world
 * @param channel Channel of the coordinates
 * @param factor Factor of the channel
 * @return The height node
 */
static ecs_entity_t tg_ramp(
    ecs_world_t *world,
    TextureChannel channel,
    float factor)
{
    ecs_entity_t coords = ecs_new(world);
    ecs_entity_t scaled = ecs_new(world);
    ecs_entity_t height = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set(world, scaled, TexMultiply, {.a = coords,
        .b_value = {factor, factor, factor, factor}});
    ecs_set(world, height, TexSplit, {.input = scaled, .channel = channel});

    return height;
}

/** Create a TgWave height node of the coordinates.
 *
 * @param world The world
 * @param kind The component of TgWave
 * @return The height node
 */
static ecs_entity_t tg_wave(
    ecs_world_t *world,
    ecs_entity_t kind)
{
    ecs_entity_t coords = ecs_new(world);
    ecs_entity_t height = ecs_new(world);
    TgWave w = {.in = coords};
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set_id(world, height, kind, ECS_SIZEOF(TgWave), &w);

    return height;
}

/** Create a graph of a TexGradient or a TexSlope of a height.
 *
 * @param world The world
 * @param height The height node
 * @param slope Make a TexSlope instead of a TexGradient
 * @param strength Strength of the node
 * @param width Width
 * @param h Height
 * @param domain Domain
 * @param node Receives the TexGradient or TexSlope node
 * @return The texture entity
 */
static ecs_entity_t tg_graph(
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
        ecs_set(world, *node, TexGradient, {.height = height, .strength = strength});
    }

    ecs_set(world, tex, TextureGraph, {.output = *node, .width = width, .height = h,
        .domain = domain, .format = GraphHdr});

    return tex;
}

/** Ramps on the plane: a height a u gives the constant vector (a 2 / w scale, 0) on the
 * interior, a height b v the vector (0, b 2 / h scale) that points down the image, scale the
 * strength times w / 512 as TexSlope. */
void TextureGradient_plane_ramp(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t node;
    int32_t x, y, w = 64, h = 32;
    double scale = 3.0 * w / 512.0;

    ecs_entity_t height = tg_ramp(world, ChannelR, 40);
    ecs_entity_t tex = tg_graph(world, height, false, 3, w, h, DomainPlane, &node);
    tg_wait(world, tex, 1);
    float *texels = tg_read(world, tex, node, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 1; x < w - 1; x ++) {
            tg_expect(texels, w, x, y, 0, 40 * 2.0 / w * scale, 3e-3);
            tg_expect(texels, w, x, y, 1, 0, 3e-3);
            tg_expect(texels, w, x, y, 2, 0, 1e-6);
            tg_expect(texels, w, x, y, 3, 0, 1e-6);
        }
    }

    ecs_os_free(texels);

    height = tg_ramp(world, ChannelG, 48);
    tex = tg_graph(world, height, false, 3, w, h, DomainPlane, &node);
    tg_wait(world, tex, 1);
    texels = tg_read(world, tex, node, w, h);
    for (y = 1; y < h - 1; y ++) {
        for (x = 0; x < w; x ++) {
            tg_expect(texels, w, x, y, 0, 0, 3e-3);
            tg_expect(texels, w, x, y, 1, 48 * 2.0 / h * scale, 3e-3);
            tg_expect(texels, w, x, y, 2, 0, 1e-6);
            tg_expect(texels, w, x, y, 3, 0, 1e-6);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The length of the gradient of a curved height is TexSlope of the same height and strength,
 * texel by texel, on the plane and on the sphere. */
void TextureGradient_length_matches_slope(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t kind = tg_register(world);
    ecs_entity_t node, slope_node;
    const TextureGraphDomain domains[2] = {DomainPlane, DomainSphere};
    int32_t d, x, y, w = 128, h = 64;
    for (d = 0; d < 2; d ++) {
        ecs_entity_t height = tg_wave(world, kind);
        ecs_entity_t tex = tg_graph(world, height, false, 6, w, h, domains[d], &node);
        ecs_entity_t slope_tex = tg_graph(
            world, height, true, 6, w, h, domains[d], &slope_node);
        tg_wait(world, tex, 1);
        tg_wait(world, slope_tex, 1);
        float *vectors = tg_read(world, tex, node, w, h);
        float *slopes = tg_read(world, slope_tex, slope_node, w, h);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                const float *v = &vectors[(y * w + x) * 4];
                double len = sqrt((double)v[0] * v[0] + (double)v[1] * v[1] +
                    (double)v[2] * v[2]);
                tg_expect(slopes, w, x, y, 0, len, 1e-3 + 1e-3 * len);
                tg_expect(vectors, w, x, y, 3, 0, 1e-6);
                if (domains[d] == DomainPlane) {
                    tg_expect(vectors, w, x, y, 2, 0, 1e-6);
                }
            }
        }

        ecs_os_free(slopes);
        ecs_os_free(vectors);
    }

    ecs_fini(world);
}

/** The height y of the coordinates on the sphere: every vector is tangent to the sphere, is the
 * projection of +y on the tangent plane times the step of the differences, so it points toward
 * the north pole, and its length matches the differences of TexSlope. */
void TextureGradient_sphere_north(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t node;
    int32_t x, y, c, w = 128, h = 64;
    double strength = 40;
    double step = 2.0 * sin(2.0 * TG_PI / w) * strength * w / 512.0;
    ecs_entity_t height = tg_ramp(world, ChannelG, 1);
    ecs_entity_t tex = tg_graph(world, height, false, (float)strength, w, h, DomainSphere,
        &node);
    tg_wait(world, tex, 1);
    float *texels = tg_read(world, tex, node, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            double p[3], east[3], north[3], v[3], len;
            const float *t = &texels[(y * w + x) * 4];
            tg_sphere_frame(x, y, w, h, p, east, north);
            for (c = 0; c < 3; c ++) {
                v[c] = t[c];
            }

            if (fabs(tg_dot3(v, p)) > 1e-3) {
                printf("texel %d, %d: not tangent, dot %f\n", x, y, tg_dot3(v, p));
            }

            test_assert(fabs(tg_dot3(v, p)) <= 1e-3);
            test_assert(v[1] >= -1e-4);
            tg_expect(texels, w, x, y, 3, 0, 1e-6);
            for (c = 0; c < 3; c ++) {
                tg_expect(texels, w, x, y, c, north[c] * north[1] * step, 0.04 * step + 1e-3);
            }

            if (y >= 2 && y < h - 2) {
                len = sqrt(tg_dot3(v, v));
                test_assert(len > 0);
                if (tg_dot3(v, north) / len < 0.99) {
                    printf("texel %d, %d: not toward the north pole, %f\n", x, y,
                        tg_dot3(v, north) / len);
                }

                test_assert(tg_dot3(v, north) / len >= 0.99);
            }
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** The gradient of a curved height on the sphere changes across the seam of the longitude no
 * more than between other neighbours of the row. */
void TextureGradient_sphere_seam(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t kind = tg_register(world);
    ecs_entity_t node;
    int32_t x, y, w = 128, h = 64;
    ecs_entity_t height = tg_wave(world, kind);
    ecs_entity_t tex = tg_graph(world, height, false, 8, w, h, DomainSphere, &node);
    tg_wait(world, tex, 1);
    float *texels = tg_read(world, tex, node, w, h);
    for (y = 0; y < h; y ++) {
        double inner = 0, seam;
        for (x = 0; x < w - 1; x ++) {
            double d = tg_distance(texels, w, x, y, x + 1, y);
            inner = d > inner ? d : inner;
        }

        seam = tg_distance(texels, w, w - 1, y, 0, y);
        if (seam > inner * 1.5 + 1e-3) {
            printf("row %d: seam %f, neighbours up to %f\n", y, seam, inner);
        }

        test_assert(seam <= inner * 1.5 + 1e-3);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Strength 0 gives the zero vector on the plane and on the sphere. */
void TextureGradient_zero_strength(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t kind = tg_register(world);
    ecs_entity_t node;
    const TextureGraphDomain domains[2] = {DomainPlane, DomainSphere};
    int32_t d, c, x, y, w = 32, h = 16;
    for (d = 0; d < 2; d ++) {
        ecs_entity_t height = tg_wave(world, kind);
        ecs_entity_t tex = tg_graph(world, height, false, 0, w, h, domains[d], &node);
        tg_wait(world, tex, 1);
        float *texels = tg_read(world, tex, node, w, h);
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                for (c = 0; c < 4; c ++) {
                    tg_expect(texels, w, x, y, c, 0, 1e-6);
                }
            }
        }

        ecs_os_free(texels);
    }

    ecs_fini(world);
}

/** The strength is a parameter: editing it writes the parameter buffer, no stage compiles and
 * the program is kept, and the vectors follow the new strength. */
void TextureGradient_strength_edit_no_recompile(void) {
    // textures/graph
    ecs_world_t *world = tg_world();
    ecs_entity_t node;
    int32_t x, y, w = 64, h = 32;
    box_render_gpu_stats_t before, after;
    tg_frames(world, 1);
    ecs_entity_t height = tg_ramp(world, ChannelR, 40);
    ecs_entity_t tex = tg_graph(world, height, false, 1, w, h, DomainPlane, &node);
    const TextureStatus *status = tg_wait(world, tex, 1);
    test_int(status->revision, 1);
    float *texels = tg_read(world, tex, node, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 1; x < w - 1; x ++) {
            tg_expect(texels, w, x, y, 0, 40 * 2.0 / 512.0, 3e-3);
        }
    }

    ecs_os_free(texels);
    test_bool(box_render_gpu_stats(world, &before), true);

    ecs_set(world, node, TexGradient, {.height = height, .strength = 4});
    status = tg_wait(world, tex, 2);
    test_int(status->revision, 2);
    test_bool(box_render_gpu_stats(world, &after), true);
    test_int(after.compiles, before.compiles);
    test_int(after.texture_graph_programs, before.texture_graph_programs);

    texels = tg_read(world, tex, node, w, h);
    for (y = 0; y < h; y ++) {
        for (x = 1; x < w - 1; x ++) {
            tg_expect(texels, w, x, y, 0, 4 * 40 * 2.0 / 512.0, 3e-3);
            tg_expect(texels, w, x, y, 1, 0, 3e-3);
        }
    }

    ecs_os_free(texels);
    ecs_fini(world);
}
