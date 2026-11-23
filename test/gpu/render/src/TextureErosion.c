/**
 * @file TextureErosion.c
 * @brief Tests for the TexErosion and TexCenter nodes of the texture graphs: the defaults, a
 * flat slope that stays flat, gullies that run down the slope, adjacent windows of DomainWorld
 * that agree on their shared edge and the vector from a point of the world.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Frames a test waits at most for an outcome */
#define TE_FRAMES (60)

/** Ticket of the next readback */
static uint32_t te_ticket = 1;

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* te_world(void)
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
static void te_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Run frames until a texture is ready and check that it is.
 *
 * @param world The world
 * @param tex The texture entity
 */
static void te_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TE_FRAMES; i ++) {
        te_frames(world, 1);
        status = ecs_get(world, tex, TextureStatus);
        if (status && (status->state == TextureFailed || status->state == TextureReady)) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->state != TextureReady) {
        printf("error: %s\n", status->error ? status->error : "(none)");
    }

    test_int(status->state, TextureReady);
}

/** Read back the image of a texture graph.
 *
 * @param world The world
 * @param tex The texture entity
 * @param size Width and height of the graph
 * @return Four floats per texel, freed with ecs_os_free
 */
static float* te_read(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t size)
{
    uint32_t ticket = te_ticket ++;
    float *result = NULL;
    int32_t i, j;
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, tex, 0, ticket), true);
    for (i = 0; i < TE_FRAMES && !result; i ++) {
        te_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            if (r->ticket == ticket && !result) {
                test_assert(r->texels != NULL);
                test_int(r->width, size);
                test_int(r->height, size);
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

/** Create a texture graph on DomainWorld with a float image.
 *
 * @param world The world
 * @param output The output node
 * @param size Width and height of the image
 * @param x World x of the window
 * @param z World z of the window
 * @param meters Width and depth of the window
 * @return The texture entity
 */
static ecs_entity_t te_graph(
    ecs_world_t *world,
    ecs_entity_t output,
    int32_t size,
    double x,
    double z,
    float meters)
{
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {.output = output, .width = size, .height = size,
        .domain = DomainWorld, .format = GraphFloat4,
        .window = {.x = x, .z = z, .width = meters, .depth = meters}});

    return tex;
}

/** Create a TexErosion node of a constant slope.
 *
 * @param world The world
 * @param sx Slope along x
 * @param sz Slope along z
 * @param octaves Octaves of the node
 * @return The node entity
 */
static ecs_entity_t te_erosion(
    ecs_world_t *world,
    float sx,
    float sz,
    int32_t octaves)
{
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexErosion, {.slope_value = {sx, sz, 0, 0}, .frequency = 1.0f / 16,
        .octaves = octaves, .lacunarity = 2, .gain = 0.5f, .seed = 7, .scale = 1, .branch = 1});

    return node;
}

void TextureErosion_defaults(void) {
    // materials/textures
    ecs_world_t *world = te_world();
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, TexErosion);
    const TexErosion *erosion = ecs_get(world, e, TexErosion);
    test_flt(erosion->frequency, 4);
    test_int(erosion->octaves, 5);
    test_flt(erosion->lacunarity, 2);
    test_flt(erosion->gain, 0.5f);
    test_flt(erosion->scale, 1);
    test_flt(erosion->branch, 1);
    test_int(erosion->seed, 0);
    test_assert(erosion->slope == 0);
    test_assert(erosion->coords == 0);
    test_assert(ecs_lookup(world, "box.texture.nodes.Erosion") == ecs_id(TexErosion));
    test_assert(ecs_lookup(world, "box.texture.nodes.Center") == ecs_id(TexCenter));
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexErosion), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 2);
    test_bool(kind->neighbourhood, false);

    ecs_fini(world);
}

void TextureErosion_flat_is_zero(void) {
    // materials/textures
    ecs_world_t *world = te_world();
    ecs_entity_t node = te_erosion(world, 0, 0, 5);
    ecs_entity_t tex = te_graph(world, node, 33, 100, 200, 64);
    te_wait(world, tex);
    float *img = te_read(world, tex, 33);
    int32_t i;
    for (i = 0; i < 33 * 33; i ++) {
        test_flt(img[i * 4], 0);
    }

    ecs_os_free(img);
    ecs_fini(world);
}

void TextureErosion_gullies_run_down_slope(void) {
    // materials/textures
    ecs_world_t *world = te_world();
    ecs_entity_t node = te_erosion(world, 2, 0, 1);
    ecs_entity_t tex = te_graph(world, node, 65, 0, 0, 64);
    te_wait(world, tex);
    float *img = te_read(world, tex, 65);
    double along = 0, across = 0, lowest = 1, highest = -1;
    int32_t x, y;
    for (y = 0; y + 1 < 65; y ++) {
        for (x = 0; x + 1 < 65; x ++) {
            double v = img[(y * 65 + x) * 4];
            test_assert(isfinite(v));
            test_assert(v >= -1.001 && v <= 1.001);
            along += fabs((double)img[(y * 65 + x + 1) * 4] - v);
            across += fabs((double)img[((y + 1) * 65 + x) * 4] - v);
            lowest = v < lowest ? v : lowest;
            highest = v > highest ? v : highest;
        }
    }

    test_assert(across > 3 * along);
    test_assert(lowest < -0.5);
    test_assert(highest > 0.5);

    ecs_os_free(img);
    ecs_fini(world);
}

void TextureErosion_adjacent_windows_match(void) {
    // materials/textures
    ecs_world_t *world = te_world();
    ecs_entity_t node = te_erosion(world, 0.8f, -1.3f, 5);
    const double x = 1000000, z = -250000;
    ecs_entity_t a = te_graph(world, node, 65, x, z, 64);
    ecs_entity_t right = te_graph(world, node, 65, x + 64, z, 64);
    ecs_entity_t below = te_graph(world, node, 65, x, z + 64, 64);
    te_wait(world, a);
    te_wait(world, right);
    te_wait(world, below);
    float *ia = te_read(world, a, 65);
    float *ir = te_read(world, right, 65);
    float *ib = te_read(world, below, 65);
    double spread = 0;
    int32_t k;
    for (k = 0; k < 65; k ++) {
        test_assert(fabsf(ia[(k * 65 + 64) * 4] - ir[(k * 65) * 4]) < 2e-3f);
        test_assert(fabsf(ia[(64 * 65 + k) * 4] - ib[k * 4]) < 2e-3f);
        spread += fabs((double)ia[(k * 65 + 64) * 4] - (double)ia[64 * 4]);
    }

    test_assert(spread > 0.5);

    ecs_os_free(ia);
    ecs_os_free(ir);
    ecs_os_free(ib);
    ecs_fini(world);
}

void TextureErosion_center_from_point(void) {
    // materials/textures
    ecs_world_t *world = te_world();
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexCenter, {.x = 1000000.5, .z = -2000000.25});
    ecs_entity_t tex = te_graph(world, node, 17, 1000000, -2000000, 32);
    te_wait(world, tex);
    float *img = te_read(world, tex, 17);
    int32_t x, y;
    for (y = 0; y < 17; y ++) {
        for (x = 0; x < 17; x ++) {
            const float *t = &img[(y * 17 + x) * 4];
            test_flt(t[0], (float)(x * 2) - 0.5f);
            test_flt(t[1], (float)(y * 2) + 0.25f);
            test_flt(t[2], 0);
            test_flt(t[3], 0);
        }
    }

    ecs_os_free(img);
    ecs_fini(world);
}
