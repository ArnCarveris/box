/**
 * @file TextureGraphWorld.c
 * @brief Tests for TextureGraph on DomainWorld: adjacent windows of a graph of noise, cells, a
 * warp, a blur and a slope agree on their shared edge texels, a window a million meters from the
 * origin is as smooth as one at it, the float formats store heights in meters and the value cache
 * keeps the windows of one graph apart, also for dozens of small windows.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TW_FRAMES (60)

/** Ticket of the next readback */
static uint32_t tw_ticket = 1;

/** Nodes of the terrain like test graph, which the windows share */
typedef struct tw_nodes_t {
    ecs_entity_t ridged;    /**< Ridged noise of the absolute position */
    ecs_entity_t cells;     /**< Cells of the absolute position, a Vector */
    ecs_entity_t warp;      /**< Coordinates warped by a vector of noise */
    ecs_entity_t warped;    /**< Noise at the warped coordinates */
    ecs_entity_t height;    /**< Height in meters: the noises and the cells summed and scaled */
    ecs_entity_t blur;      /**< TexBlur of the height, 6 m */
    ecs_entity_t slope;     /**< TexSlope of the height */
    ecs_entity_t output;    /**< Combine of blur, slope, the cells distance and the warped noise */
} tw_nodes_t;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tw_world(void)
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
static void tw_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready with at least a revision.
 *
 * @param world The world
 * @param tex The texture entity
 * @param revision Revision to wait for
 */
static void tw_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t revision)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TW_FRAMES; i ++) {
        tw_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && status->state == TextureFailed) {
            break;
        }

        if (status && status->state == TextureReady && status->revision >= revision) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state != TextureReady) {
        printf("error: %s\n", status->error ? status->error : "(none)");
    }

    test_int(status->state, TextureReady);
}

/** Read back the value of a node of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The node entity, 0 for the image
 * @param width Width of the graph
 * @param height Height of the graph
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* tw_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t node,
    int32_t width,
    int32_t height)
{
    uint32_t ticket = tw_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, node, ticket), true);
    for (i = 0; i < TW_FRAMES && !result; i ++) {
        tw_frames(world, 1);
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

/** Create the nodes of the terrain like test graph.
 *
 * @param world The world
 * @return The nodes
 */
static tw_nodes_t tw_terrain(
    ecs_world_t *world)
{
    tw_nodes_t n = {0};
    ecs_entity_t offset = ecs_new(world), cells_x = ecs_new(world);
    ecs_entity_t sum0 = ecs_new(world), sum1 = ecs_new(world);
    n.ridged = ecs_new(world);
    n.cells = ecs_new(world);
    n.warp = ecs_new(world);
    n.warped = ecs_new(world);
    n.height = ecs_new(world);
    n.blur = ecs_new(world);
    n.slope = ecs_new(world);
    n.output = ecs_new(world);
    ecs_set(world, n.ridged, TexNoise, {.frequency = 1.0f / 32, .octaves = 5, .lacunarity = 2,
        .gain = 0.5f, .seed = 3, .fractal = NoiseRidged, .offset = 1, .channels = 1});
    ecs_set(world, n.cells, TexCells, {.count = 64, .jitter = 1, .seed = 5, .edge = 0.25f,
        .size = 16});
    ecs_set(world, offset, TexNoise, {.frequency = 1.0f / 40, .octaves = 2, .lacunarity = 2,
        .gain = 0.5f, .seed = 9, .offset = 1, .channels = 3});
    ecs_set(world, n.warp, TexWarp, {.offset = offset, .amount_value = 20});
    ecs_set(world, n.warped, TexNoise, {.coords = n.warp, .frequency = 1.0f / 16, .octaves = 3,
        .lacunarity = 2, .gain = 0.5f, .seed = 11, .offset = 1, .channels = 1});
    ecs_set(world, cells_x, TexSplit, {.input = n.cells, .channel = ChannelR});
    ecs_set(world, sum0, TexAdd, {.a = n.ridged, .b = n.warped});
    ecs_set(world, sum1, TexAdd, {.a = sum0, .b = cells_x});
    ecs_set(world, n.height, TexMultiply, {.a = sum1, .b_value = {100, 100, 100, 100}});
    ecs_set(world, n.blur, TexBlur, {.input = n.height, .radius = 6});
    ecs_set(world, n.slope, TexSlope, {.height = n.height, .strength = 1});
    ecs_set(world, n.output, TexCombine, {.r = n.blur, .g = n.slope, .b = cells_x,
        .a = n.warped});

    return n;
}

/** Create a texture graph on DomainWorld.
 *
 * @param world The world
 * @param output The output node
 * @param size Width and height of the image
 * @param x World x of the window
 * @param z World z of the window
 * @param meters Width and depth of the window
 * @param format Format of the image
 * @return The texture entity
 */
static ecs_entity_t tw_graph(
    ecs_world_t *world,
    ecs_entity_t output,
    int32_t size,
    double x,
    double z,
    float meters,
    TextureGraphFormat format)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {.output = output, .width = size, .height = size,
        .domain = DomainWorld, .format = format,
        .window = {.x = x, .z = z, .width = meters, .depth = meters}});

    return tex;
}

/** Compare a channel of the last column (or row) of a window with the first column (or row) of
 * the window after it.
 *
 * @param a Values of the first window
 * @param b Values of the second window
 * @param size Width and height of both
 * @param vertical Compare rows, the second window below the first
 * @param channel The channel
 * @param tolerance Largest difference
 * @param label Name of the values, printed on a failure
 */
static void tw_expect_edge(
    const float *a,
    const float *b,
    int32_t size,
    bool vertical,
    int32_t channel,
    float tolerance,
    const char *label)
{
    int32_t k;
    float worst = 0, spread = 0, first = 0;
    for (k = 0; k < size; k ++) {
        int32_t ia = vertical ? ((size - 1) * size + k) : (k * size + size - 1);
        int32_t ib = vertical ? k : k * size;
        float va = a[ia * 4 + channel], vb = b[ib * 4 + channel];
        float d = fabsf(va - vb);
        test_assert(isfinite(va) && isfinite(vb));
        worst = d > worst ? d : worst;
        first = k ? first : va;
        spread = fabsf(va - first) > spread ? fabsf(va - first) : spread;
    }

    if (worst > tolerance) {
        printf("%s: edge differs by %g (channel %d, %s)\n", label, (double)worst, channel,
            vertical ? "rows" : "columns");
    }

    test_assert(worst <= tolerance);

    /* The edge is not constant, so the test compares a pattern */
    test_assert(spread > 0);
}

/** Read a node of three windows (one, one to its right and one below it) and compare their
 * shared edges.
 *
 * @param world The world
 * @param tex The three texture entities
 * @param node The node entity, 0 for the image
 * @param size Width and height of the windows
 * @param channels Channels compared
 * @param tolerance Largest difference
 * @param label Name of the node
 */
static void tw_expect_node(
    ecs_world_t *world,
    const ecs_entity_t *tex,
    ecs_entity_t node,
    int32_t size,
    int32_t channels,
    float tolerance,
    const char *label)
{
    float *a = tw_read(world, tex[0], node, size, size);
    float *right = tw_read(world, tex[1], node, size, size);
    float *below = tw_read(world, tex[2], node, size, size);
    int32_t c;
    for (c = 0; c < channels; c ++) {
        tw_expect_edge(a, right, size, false, c, tolerance, label);
        tw_expect_edge(a, below, size, true, c, tolerance, label);
    }

    ecs_os_free(a);
    ecs_os_free(right);
    ecs_os_free(below);
}

/** Get the mean and the largest absolute difference of neighbouring texels of a channel.
 *
 * @param img Four floats per texel
 * @param size Width and height
 * @param mean Receives the mean difference
 * @param max Receives the largest difference
 */
static void tw_roughness(
    const float *img,
    int32_t size,
    double *mean,
    double *max)
{
    int32_t x, y, count = 0;
    *mean = 0;
    *max = 0;
    for (y = 0; y < size; y ++) {
        for (x = 0; x + 1 < size; x ++) {
            double d = fabs((double)img[(y * size + x + 1) * 4] - (double)img[(y * size + x) * 4]);
            double e = y + 1 < size
                ? fabs((double)img[((y + 1) * size + x) * 4] - (double)img[(y * size + x) * 4])
                : 0;
            *mean += d + e;
            count += y + 1 < size ? 2 : 1;
            *max = d > *max ? d : *max;
            *max = e > *max ? e : *max;
        }
    }

    *mean /= count;
}

void TextureGraphWorld_adjacent_windows_match(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    tw_nodes_t n = tw_terrain(world);
    const double x = 5000, z = -3000;
    ecs_entity_t tex[3] = {
        tw_graph(world, n.output, 129, x, z, 128, GraphFloat4),
        tw_graph(world, n.output, 129, x + 128, z, 128, GraphFloat4),
        tw_graph(world, n.output, 129, x, z + 128, 128, GraphFloat4)
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        tw_wait(world, tex[i], 1);
    }

    tw_expect_node(world, tex, n.ridged, 129, 1, 1e-4f, "ridged noise");
    tw_expect_node(world, tex, n.cells, 129, 1, 1e-3f, "cells distance");
    tw_expect_node(world, tex, n.warped, 129, 1, 1e-3f, "noise at warped coordinates");
    tw_expect_node(world, tex, n.height, 129, 1, 2e-2f, "height");
    tw_expect_node(world, tex, n.blur, 129, 1, 2e-2f, "blur");
    tw_expect_node(world, tex, n.slope, 129, 1, 2e-3f, "slope");
    tw_expect_node(world, tex, 0, 129, 4, 2e-2f, "image");

    ecs_fini(world);
}

void TextureGraphWorld_coords_meters(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t coords = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_entity_t tex = tw_graph(world, coords, 17, 1e6, 2e6, 32, GraphFloat4);
    tw_wait(world, tex, 1);
    float *img = tw_read(world, tex, 0, 17, 17);
    int32_t x, y;
    for (y = 0; y < 17; y ++) {
        for (x = 0; x < 17; x ++) {
            const float *t = &img[(y * 17 + x) * 4];
            test_flt(t[0], (float)(x * 2));
            test_flt(t[1], (float)(y * 2));
            test_flt(t[2], 0);
            test_flt(t[3], 1);
        }
    }

    ecs_os_free(img);
    ecs_fini(world);
}

void TextureGraphWorld_far_window_smooth(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 1.0f / 8, .octaves = 4, .lacunarity = 2,
        .gain = 0.5f, .seed = 21, .offset = 1, .channels = 1});
    ecs_entity_t near = tw_graph(world, noise, 129, 0, 0, 128, GraphFloat);
    ecs_entity_t far = tw_graph(world, noise, 129, 1e6, 1e6, 128, GraphFloat);
    ecs_entity_t further = tw_graph(world, noise, 129, -3.5e7, 1e6 + 0.5, 128, GraphFloat);
    double mean_near, max_near, mean_far, max_far, mean_further, max_further, apart = 0;
    int32_t i;
    tw_wait(world, near, 1);
    tw_wait(world, far, 1);
    tw_wait(world, further, 1);
    float *a = tw_read(world, near, 0, 129, 129);
    float *b = tw_read(world, far, 0, 129, 129);
    float *c = tw_read(world, further, 0, 129, 129);
    tw_roughness(a, 129, &mean_near, &max_near);
    tw_roughness(b, 129, &mean_far, &max_far);
    tw_roughness(c, 129, &mean_further, &max_further);
    printf("roughness near %g (max %g), 1e6 m %g (max %g), 3.5e7 m %g (max %g)\n",
        mean_near, max_near, mean_far, max_far, mean_further, max_further);

    /* Neighbouring texels differ as much far away as at the origin, no steps or flat spots */
    test_assert(mean_near > 1e-3);
    test_assert(mean_far > 0.5 * mean_near && mean_far < 2 * mean_near);
    test_assert(max_far < 2 * max_near + 1e-3);
    test_assert(mean_further > 0.5 * mean_near && mean_further < 2 * mean_near);
    test_assert(max_further < 2 * max_near + 1e-3);

    /* The noise does not repeat: the far window is another pattern */
    for (i = 0; i < 129 * 129; i ++) {
        apart += fabs((double)a[i * 4] - (double)b[i * 4]);
    }

    test_assert(apart / (129 * 129) > 0.02);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_os_free(c);
    ecs_fini(world);
}

void TextureGraphWorld_far_windows_match(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    tw_nodes_t n = tw_terrain(world);
    const double x = 1e6, z = -1e6;
    ecs_entity_t tex[3] = {
        tw_graph(world, n.output, 65, x, z, 64, GraphFloat4),
        tw_graph(world, n.output, 65, x + 64, z, 64, GraphFloat4),
        tw_graph(world, n.output, 65, x, z + 64, 64, GraphFloat4)
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        tw_wait(world, tex[i], 1);
    }

    tw_expect_node(world, tex, n.ridged, 65, 1, 1e-4f, "ridged noise");
    tw_expect_node(world, tex, n.cells, 65, 1, 1e-3f, "cells distance");
    tw_expect_node(world, tex, 0, 65, 4, 2e-2f, "image");

    ecs_fini(world);
}

void TextureGraphWorld_float_heights(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t coords = ecs_new(world), x = ecs_new(world), height = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set(world, x, TexSplit, {.input = coords, .channel = ChannelR});
    ecs_set(world, height, TexAdd, {.a = x, .b_value = {-0.37f, 0, 0, 0}});

    /* 201 texels over 2000 m: heights of -0.37 to 1999.63 m in steps of 10 m */
    ecs_entity_t tex = tw_graph(world, height, 201, 0, 0, 2000, GraphFloat);
    tw_wait(world, tex, 1);
    float *img = tw_read(world, tex, 0, 201, 201);
    int32_t i, j;
    for (j = 0; j < 201; j += 25) {
        for (i = 0; i < 201; i ++) {
            float v = img[(j * 201 + i) * 4];
            if (fabsf(v - ((float)i * 10.0f - 0.37f)) > 0.01f) {
                printf("texel %d, %d: %g, expected %g\n", i, j, (double)v,
                    (double)((float)i * 10.0f - 0.37f));
            }

            test_assert(fabsf(v - ((float)i * 10.0f - 0.37f)) <= 0.01f);
        }
    }

    ecs_os_free(img);
    ecs_fini(world);
}

void TextureGraphWorld_float4_values(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t coords = ecs_new(world), x = ecs_new(world), z = ecs_new(world);
    ecs_entity_t neg = ecs_new(world), out = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set(world, x, TexSplit, {.input = coords, .channel = ChannelR});
    ecs_set(world, z, TexSplit, {.input = coords, .channel = ChannelG});
    ecs_set(world, neg, TexMultiply, {.a = x, .b_value = {-3.5f, 0, 0, 0}});
    ecs_set(world, out, TexCombine, {.r = x, .g = z, .b = neg, .a_value = 1234.56f});
    ecs_entity_t tex = tw_graph(world, out, 33, -50, 70, 1600, GraphFloat4);
    tw_wait(world, tex, 1);
    float *img = tw_read(world, tex, 0, 33, 33);
    int32_t i, j;
    for (j = 0; j < 33; j ++) {
        for (i = 0; i < 33; i ++) {
            const float *t = &img[(j * 33 + i) * 4];
            test_assert(fabsf(t[0] - (float)i * 50.0f) <= 0.01f);
            test_assert(fabsf(t[1] - (float)j * 50.0f) <= 0.01f);
            test_assert(fabsf(t[2] + (float)i * 175.0f) <= 0.01f);
            test_assert(fabsf(t[3] - 1234.56f) <= 0.01f);
        }
    }

    ecs_os_free(img);
    ecs_fini(world);
}

void TextureGraphWorld_blur_meters(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world), blur = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 1.0f / 8, .octaves = 2, .lacunarity = 2,
        .gain = 0.5f, .seed = 4, .offset = 1, .channels = 1});
    ecs_set(world, blur, TexBlur, {.input = noise, .radius = 6});

    /* The same 64 m at one and at half a meter per texel */
    ecs_entity_t coarse = tw_graph(world, blur, 65, 300, 400, 64, GraphFloat);
    ecs_entity_t fine = tw_graph(world, blur, 129, 300, 400, 64, GraphFloat);
    tw_wait(world, coarse, 1);
    tw_wait(world, fine, 1);
    float *a = tw_read(world, coarse, 0, 65, 65);
    float *b = tw_read(world, fine, 0, 129, 129);
    double worst = 0;
    int32_t i, j;
    for (j = 0; j < 65; j ++) {
        for (i = 0; i < 65; i ++) {
            double d = fabs((double)a[(j * 65 + i) * 4] - (double)b[(j * 2 * 129 + i * 2) * 4]);
            worst = d > worst ? d : worst;
        }
    }

    printf("blur at two resolutions differs by %g\n", worst);
    test_assert(worst < 0.02);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

void TextureGraphWorld_cache_keeps_windows_apart(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world), blur = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 1.0f / 8, .octaves = 3, .lacunarity = 2,
        .gain = 0.5f, .seed = 8, .offset = 1, .channels = 1});
    ecs_set(world, blur, TexBlur, {.input = noise, .radius = 3});
    ecs_entity_t a = tw_graph(world, blur, 33, 0, 0, 32, GraphFloat);
    ecs_entity_t b = tw_graph(world, blur, 33, 32, 0, 32, GraphFloat);
    ecs_entity_t c = tw_graph(world, blur, 33, 0, 0, 64, GraphFloat);
    double ab = 0, ac = 0, same = 0;
    int32_t i;
    tw_wait(world, a, 1);
    tw_wait(world, b, 1);
    tw_wait(world, c, 1);

    /* The noise, a cached value as the input of the blur, differs per window */
    float *na = tw_read(world, a, noise, 33, 33);
    float *nb = tw_read(world, b, noise, 33, 33);
    float *nc = tw_read(world, c, noise, 33, 33);
    for (i = 0; i < 33 * 33; i ++) {
        ab += fabs((double)na[i * 4] - (double)nb[i * 4]);
        ac += fabs((double)na[i * 4] - (double)nc[i * 4]);
    }

    test_assert(ab / (33 * 33) > 0.01);
    test_assert(ac / (33 * 33) > 0.01);

    /* The left edge of b is the right edge of a */
    for (i = 0; i < 33; i ++) {
        test_assert(fabsf(na[(i * 33 + 32) * 4] - nb[(i * 33) * 4]) < 1e-4f);
    }

    /* Moving b onto the window of a gives the image of a */
    float *ia = tw_read(world, a, 0, 33, 33);
    int32_t revision = ecs_get(world, b, TextureStatus)->revision;
    ecs_set(world, b, TextureGraph, {.output = blur, .width = 33, .height = 33,
        .domain = DomainWorld, .format = GraphFloat,
        .window = {.x = 0, .z = 0, .width = 32, .depth = 32}});
    tw_wait(world, b, revision + 1);
    float *ib = tw_read(world, b, 0, 33, 33);
    for (i = 0; i < 33 * 33; i ++) {
        same += fabs((double)ia[i * 4] - (double)ib[i * 4]);
    }

    test_assert(same == 0);

    ecs_os_free(na);
    ecs_os_free(nb);
    ecs_os_free(nc);
    ecs_os_free(ia);
    ecs_os_free(ib);
    ecs_fini(world);
}

void TextureGraphWorld_many_windows(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    tw_nodes_t n = tw_terrain(world);
    ecs_entity_t tex[64];
    float *edge[2];
    int32_t i, frames = 0, ready = 0;
    ecs_time_t t = {0};
    ecs_time_measure(&t);
    for (i = 0; i < 64; i ++) {
        tex[i] = tw_graph(world, n.output, 129, -512 + (i % 8) * 128.0, 256 + (i / 8) * 128.0,
            128, GraphFloat4);
    }

    while (ready < 64 && frames < TW_FRAMES * 4) {
        tw_frames(world, 1);
        frames ++;
        ready = 0;
        for (i = 0; i < 64; i ++) {
            const TextureStatus *status = ecs_get(world, tex[i], TextureStatus);
            if (status && status->state == TextureFailed) {
                printf("error: %s\n", status->error ? status->error : "(none)");
                test_assert(false);
            }

            ready += status && status->state == TextureReady && status->revision >= 1;
        }
    }

    double seconds = ecs_time_measure(&t);
    const TextureGraphStats *stats = ecs_singleton_get(world, TextureGraphStats);
    test_int(ready, 64);
    test_assert(stats != NULL);
    printf("64 windows of 129x129 ready in %d frames, %.1f ms: %d programs, %d dispatches, "
        "%lld texels, %.3f GPU ms\n", frames, seconds * 1000, stats->programs, stats->dispatches,
        (long long)stats->texels, stats->gpu_ms);

    /* The windows share the programs of one structure */
    test_assert(stats->programs <= 2);

    /* A few times as long as one window evaluated alone at most */
    test_assert(frames < TW_FRAMES * 2);

    /* Tiles 9 and 10 of the grid share an edge */
    edge[0] = tw_read(world, tex[9], 0, 129, 129);
    edge[1] = tw_read(world, tex[10], 0, 129, 129);
    tw_expect_edge(edge[0], edge[1], 129, false, 0, 2e-2f, "image of tiles 9 and 10");
    ecs_os_free(edge[0]);
    ecs_os_free(edge[1]);

    ecs_fini(world);
}

/** A material whose layers name float graphs, which no layer can filter on the web, renders
 * with the defaults of the layers instead of failing the device. */
void TextureGraphWorld_float_material_defaults(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 4});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});

    ecs_entity_t noise = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 0.05f, .octaves = 2, .lacunarity = 2,
        .gain = 0.5f, .channels = 1});
    ecs_entity_t height = tw_graph(world, noise, 33, 0, 0, 64, GraphFloat);
    ecs_entity_t rgba = tw_graph(world, noise, 33, 0, 0, 64, GraphFloat4);
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Position, {0, 0, 0});
    ecs_set(world, quad, Quad, {2, 2});
    ecs_set(world, quad, Rgb, {255, 255, 255});
    ecs_set(world, quad, PbrMaterial, {.roughness = 0.8f});
    ecs_set(world, quad, PbrTextures, {.albedo = height, .emissive = rgba});

    tw_wait(world, height, 1);
    tw_wait(world, rgba, 1);
    tw_frames(world, 3);

    ecs_fini(world);
}
