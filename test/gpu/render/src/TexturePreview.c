/**
 * @file TexturePreview.c
 * @brief Tests for TextureNodePreview on the GPU backend: the status with the PNG of the value
 * of a node scaled down, the default size, the rate of the readbacks, no encode while the value
 * is the same, removal, nodes outside the graph, other readbacks and TextureGraphStats.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Coordinates scaled per channel and offset over a 256 x 128 GraphLinear image */
#define PREVIEW_GRAPH "test/assets/textures/graph/preview.flecs"

/** Frames a test waits at most for a preview */
#define PREVIEW_FRAMES (60)

/** Difference of a channel of a preview from its value, in steps of 255 */
#define PREVIEW_TOLERANCE (2)

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views, and run the preview graph in it.
 *
 * @return The world
 */
static ecs_world_t* preview_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    char *path = box_app_locate(PREVIEW_GRAPH);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);

    return world;
}

/** Run frames and wait for each to render.
 *
 * @param world The world
 * @param count Frames to run
 */
static void preview_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }
}

/** Look up a node of the preview graph.
 *
 * @param world The world
 * @param name Name of the node
 * @return The node entity
 */
static ecs_entity_t preview_node(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t graph = ecs_lookup(world, "graph_preview");
    test_assert(graph != 0);
    ecs_entity_t result = ecs_lookup_child(world, graph, name);
    test_assert(result != 0);

    return result;
}

/** Run frames until a node has a preview of at least a revision.
 *
 * @param world The world
 * @param node The node entity
 * @param revision Revision to wait for
 * @return The status
 */
static const TextureNodePreviewStatus* preview_wait(
    ecs_world_t *world,
    ecs_entity_t node,
    int32_t revision)
{
    const TextureNodePreviewStatus *status = NULL;
    int32_t i;
    for (i = 0; i < PREVIEW_FRAMES; i ++) {
        preview_frames(world, 1);
        status = ecs_get(world, node, TextureNodePreviewStatus);
        if (status && status->revision >= revision) {
            break;
        }
    }

    test_assert(status != NULL);
    test_assert(status->revision >= revision);

    return status;
}

/** Get the value of a base64 digit.
 *
 * @param c The digit
 * @return Its value, -1 for padding or another character
 */
static int32_t preview_digit(
    char c)
{
    int32_t result = -1;
    if (c >= 'A' && c <= 'Z') {
        result = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
        result = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
        result = c - '0' + 52;
    } else if (c == '+') {
        result = 62;
    } else if (c == '/') {
        result = 63;
    }

    return result;
}

/** Decode the PNG of a preview.
 *
 * @param status The status
 * @return RGBA8 pixels of width times height, freed with stbi_image_free
 */
static uint8_t* preview_decode(
    const TextureNodePreviewStatus *status)
{
    test_assert(status->png != NULL);
    int32_t len = (int32_t)strlen(status->png), i, size = 0, bits = 0, w = 0, h = 0, n = 0;
    uint32_t acc = 0;
    test_assert(len > 0 && len % 4 == 0);
    uint8_t *bytes = ecs_os_malloc(len);
    for (i = 0; i < len; i ++) {
        int32_t d = preview_digit(status->png[i]);
        if (d < 0) {
            test_assert(status->png[i] == '=');
            continue;
        }

        acc = (acc << 6) | (uint32_t)d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes[size ++] = (uint8_t)(acc >> bits);
        }
    }

    test_assert(bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G');
    uint8_t *result = stbi_load_from_memory(bytes, size, &w, &h, &n, 4);
    ecs_os_free(bytes);
    test_assert(result != NULL);
    test_int(w, status->width);
    test_int(h, status->height);

    return result;
}

/** Check a pixel of a preview against the expected color.
 *
 * @param pixels The pixels
 * @param width Width of the preview
 * @param x Column
 * @param y Row
 * @param expect Expected red, green and blue in 0..1
 */
static void preview_expect(
    const uint8_t *pixels,
    int32_t width,
    int32_t x,
    int32_t y,
    const float expect[3])
{
    const uint8_t *p = &pixels[(y * width + x) * 4];
    int32_t c;
    for (c = 0; c < 3; c ++) {
        int32_t want = (int32_t)(expect[c] * 255.0f + 0.5f);
        if (abs((int32_t)p[c] - want) > PREVIEW_TOLERANCE) {
            printf("pixel %d, %d channel %d: %d, expected %d\n", x, y, c, p[c], want);
        }

        test_assert(abs((int32_t)p[c] - want) <= PREVIEW_TOLERANCE);
    }

    test_int(p[3], 255);
}

/** Read the readbacks the GPU backend delivered.
 *
 * @param world The world
 * @return texture_graph_reads of the counters
 */
static int32_t preview_reads(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);

    return stats.texture_graph_reads;
}

void TexturePreview_size_and_pixels(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t scaled = preview_node(world, "scaled");
    ecs_set(world, scaled, TextureNodePreview, { .size = 8 });
    const TextureNodePreviewStatus *status = preview_wait(world, scaled, 1);
    test_int(status->width, 8);
    test_int(status->height, 4);
    test_int(status->revision, 1);
    uint8_t *pixels = preview_decode(status);
    int32_t x, y;
    for (y = 0; y < 4; y ++) {
        for (x = 0; x < 8; x ++) {
            float expect[3] = { ((float)x + 0.5f) / 8, 0.5f * ((float)y + 0.5f) / 4, 0 };
            preview_expect(pixels, 8, x, y, expect);
        }
    }

    stbi_image_free(pixels);
    ecs_fini(world);
}

void TexturePreview_default_size(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t offset = preview_node(world, "offset");
    ecs_add(world, offset, TextureNodePreview);
    const TextureNodePreviewStatus *status = preview_wait(world, offset, 1);
    test_int(status->width, 128);
    test_int(status->height, 64);
    uint8_t *pixels = preview_decode(status);
    float first[3] = { 1.0f / 256, 0.5f * 1.0f / 128, 0.25f };
    float last[3] = { 255.0f / 256, 0.5f * 127.0f / 128, 0.25f };
    preview_expect(pixels, 128, 0, 0, first);
    preview_expect(pixels, 128, 127, 63, last);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void TexturePreview_same_value_not_encoded(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t scaled = preview_node(world, "scaled");
    ecs_set(world, scaled, TextureNodePreview, { .size = 8, .rate = 30 });
    preview_wait(world, scaled, 1);
    int32_t reads = preview_reads(world);
    preview_frames(world, 30);
    test_assert(preview_reads(world) > reads + 2);
    test_int(ecs_get(world, scaled, TextureNodePreviewStatus)->revision, 1);

    ecs_set(world, scaled, TexMath, {
        .op = MathMultiply, .a = preview_node(world, "uv"), .b_value = { 0, 1, 0, 1 } });
    const TextureNodePreviewStatus *status = preview_wait(world, scaled, 2);
    test_int(status->revision, 2);
    uint8_t *pixels = preview_decode(status);
    float expect[3] = { 0, 0.5f / 4, 0 };
    preview_expect(pixels, 8, 3, 0, expect);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void TexturePreview_rate_limits_readbacks(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t scaled = preview_node(world, "scaled");
    ecs_set(world, scaled, TextureNodePreview, { .size = 8, .rate = 4 });
    preview_wait(world, scaled, 1);
    int32_t reads = preview_reads(world);
    preview_frames(world, 60);
    int32_t made = preview_reads(world) - reads;
    test_assert(made >= 2);
    test_assert(made <= 5);
    ecs_fini(world);
}

void TexturePreview_remove_stops_readbacks(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t scaled = preview_node(world, "scaled");
    ecs_set(world, scaled, TextureNodePreview, { .size = 8, .rate = 30 });
    preview_wait(world, scaled, 1);
    ecs_remove(world, scaled, TextureNodePreview);
    preview_frames(world, 5);
    test_assert(!ecs_has(world, scaled, TextureNodePreviewStatus));
    int32_t reads = preview_reads(world);
    preview_frames(world, 30);
    test_int(preview_reads(world), reads);
    test_assert(!ecs_has(world, scaled, TextureNodePreviewStatus));
    ecs_fini(world);
}

void TexturePreview_node_outside_graph(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t loose = preview_node(world, "loose");
    ecs_set(world, loose, TextureNodePreview, { .size = 8, .rate = 30 });
    preview_frames(world, 30);
    test_assert(!ecs_has(world, loose, TextureNodePreviewStatus));

    ecs_entity_t graph = ecs_lookup(world, "graph_preview");
    ecs_set(world, graph, TextureGraph, {
        .output = loose, .width = 256, .height = 128, .format = GraphLinear });
    const TextureNodePreviewStatus *status = preview_wait(world, loose, 1);
    uint8_t *pixels = preview_decode(status);
    float expect[3] = { 1, 0, 1 };
    preview_expect(pixels, status->width, 0, 0, expect);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void TexturePreview_other_readbacks_kept(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t graph = ecs_lookup(world, "graph_preview");
    ecs_entity_t scaled = preview_node(world, "scaled");
    ecs_set(world, scaled, TextureNodePreview, { .size = 8, .rate = 60 });
    preview_wait(world, scaled, 1);
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    test_bool(box_render_texture_graph_read(world, graph, 0, 7), true);
    bool found = false;
    int32_t i, j;
    for (i = 0; i < PREVIEW_FRAMES && !found; i ++) {
        preview_frames(world, 1);
        ecs_vec_clear(&reads);
        test_bool(box_render_texture_graph_reads(world, &reads), true);
        for (j = 0; j < ecs_vec_count(&reads); j ++) {
            box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, j);
            test_assert(r->ticket == 7);
            test_assert(r->texels != NULL);
            test_uint(r->texture, graph);
            test_int(r->width, 256);
            test_int(r->height, 128);
            found = true;
            ecs_os_free(r->texels);
        }
    }

    test_bool(found, true);
    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
    ecs_fini(world);
}

void TexturePreview_stats(void) {
    // materials/textures
    ecs_world_t *world = preview_world();
    ecs_entity_t scaled = preview_node(world, "scaled");
    ecs_set(world, scaled, TextureNodePreview, { .size = 8 });
    preview_wait(world, scaled, 1);
    preview_frames(world, 2);
    const TextureGraphStats *stats = ecs_singleton_get(world, TextureGraphStats);
    test_assert(stats != NULL);
    test_assert(stats->graphs >= 1);
    test_assert(stats->programs >= 1);
    test_assert(stats->evaluations >= 1);
    test_assert(stats->dispatches >= 1);
    test_assert(stats->texels >= 256 * 128);
    test_assert(stats->reads >= 1);
    test_int(stats->previews, 1);
    test_assert(stats->gpu_ms >= 0);

    ecs_remove(world, scaled, TextureNodePreview);
    preview_frames(world, 2);
    test_int(ecs_singleton_get(world, TextureGraphStats)->previews, 0);
    ecs_fini(world);
}
