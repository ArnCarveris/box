/**
 * @file TextureReadback.c
 * @brief Tests for TextureReadback on the GPU backend: graphs of a known pattern of the
 * coordinates are read back to the CPU as R32Float, RGBA16Float and RGBA8 copies and sampled at
 * texel centers and between them, a change of the graph updates the copy and its revision, a
 * mip level halves the copy, a copy that is not kept is gone until the next one arrives, and
 * sampling before the first copy returns false.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "texture.h"

/** Frames a test waits at most for a copy */
#define TR_FRAMES (60)

/** Width of the test graphs */
#define TR_WIDTH (16)

/** Height of the test graphs */
#define TR_HEIGHT (8)

/** Difference a float channel may have from its value */
#define TR_TOLERANCE_32 (1e-4f)

/** Difference a half float channel of a value up to 1 may have from its value */
#define TR_TOLERANCE_16 (2e-3f)

/** Difference a stored RGBA8 channel may have from its value */
#define TR_TOLERANCE_8 (1.0f / 255.0f + 1e-4f)

/** Create a headless world that renders with the GPU backend on the main thread, with a small
 * surface without views.
 *
 * @return The world
 */
static ecs_world_t* tr_world(void)
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

/** Run a frame and wait for it to render.
 *
 * @param world The world
 */
static void tr_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
}

/** Run frames until the copy of a texture is ready with a revision above one.
 *
 * @param world The world
 * @param tex The texture entity
 * @param above The revision the copy must be above
 * @return The revision of the copy
 */
static int32_t tr_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t above)
{
    const TextureReadbackStatus *status = NULL;
    const TextureStatus *ts = NULL;
    int32_t i;
    for (i = 0; i < TR_FRAMES; i ++) {
        tr_frame(world);
        status = ecs_get(world, tex, TextureReadbackStatus);
        ts = ecs_get(world, tex, TextureStatus);
        if (ts && ts->state == TextureFailed) {
            printf("error: %s\n", ts->error ? ts->error : "(none)");
            break;
        }

        if (status && status->ready && status->revision > above) {
            break;
        }
    }

    test_assert(status != NULL);
    test_bool(status->ready, true);
    test_assert(status->revision > above);

    return status->revision;
}

/** Create a graph of the coordinates of the image plane: (u, v, 0, 1) with u and v of the
 * texel center, or for GraphFloat u + 10 v.
 *
 * @param world The world
 * @param format Format of the image
 * @param coords Receives the TexCoords node
 * @return The texture entity
 */
static ecs_entity_t tr_graph(
    ecs_world_t *world,
    TextureGraphFormat format,
    ecs_entity_t *coords)
{
    ecs_entity_t tex = ecs_new(world), uv = ecs_new(world), output = uv;
    ecs_set(world, uv, TexCoords, {.scale = 1});
    if (format == GraphFloat) {
        ecs_entity_t u = ecs_new(world), v = ecs_new(world), v10 = ecs_new(world);
        output = ecs_new(world);
        ecs_set(world, u, TexSplit, {.input = uv, .channel = ChannelR});
        ecs_set(world, v, TexSplit, {.input = uv, .channel = ChannelG});
        ecs_set(world, v10, TexMultiply, {.a = v, .b_value = {10, 10, 10, 10}});
        ecs_set(world, output, TexAdd, {.a = u, .b = v10});
    }

    ecs_set(world, tex, TextureGraph, {.output = output, .width = TR_WIDTH,
        .height = TR_HEIGHT, .format = format});
    ecs_add(world, tex, TextureReadback);
    if (coords) {
        *coords = uv;
    }

    return tex;
}

/** Get the value the pattern of tr_graph has at uv.
 *
 * @param format Format of the image
 * @param scale Scale of the TexCoords
 * @param u Horizontal coordinate
 * @param v Vertical coordinate
 * @param out Receives the value
 */
static void tr_expect(
    TextureGraphFormat format,
    float scale,
    float u,
    float v,
    float out[4])
{
    if (format == GraphFloat) {
        out[0] = scale * u + 10 * scale * v;
        out[1] = 0;
    } else {
        out[0] = scale * u;
        out[1] = scale * v;
    }

    out[2] = 0;
    out[3] = 1;
}

/** Check the copy of a tr_graph at every texel center and halfway between neighbouring texel
 * centers, both by texel and by uv.
 *
 * @param world The world
 * @param tex The texture entity
 * @param format Format of the image
 * @param scale Scale of the TexCoords
 * @param tolerance Difference a channel may have from its value
 */
static void tr_check(
    ecs_world_t *world,
    ecs_entity_t tex,
    TextureGraphFormat format,
    float scale,
    float tolerance)
{
    float out[4], expect[4];
    int32_t x, y, c;
    for (y = 0; y < TR_HEIGHT; y ++) {
        for (x = 0; x < TR_WIDTH; x ++) {
            float u = ((float)x + 0.5f) / TR_WIDTH, v = ((float)y + 0.5f) / TR_HEIGHT;
            tr_expect(format, scale, u, v, expect);
            test_bool(box_texture_readback_texel(world, tex, x, y, out), true);
            for (c = 0; c < 4; c ++) {
                test_assert(fabsf(out[c] - expect[c]) <= tolerance);
            }

            test_bool(box_texture_readback_sample(world, tex, u, v, AddressClamp, out), true);
            for (c = 0; c < 4; c ++) {
                test_assert(fabsf(out[c] - expect[c]) <= tolerance);
            }
        }
    }

    for (y = 0; y < TR_HEIGHT - 1; y ++) {
        for (x = 0; x < TR_WIDTH - 1; x ++) {
            float u = ((float)x + 1.0f) / TR_WIDTH, v = ((float)y + 1.0f) / TR_HEIGHT;
            tr_expect(format, scale, u, v, expect);
            test_bool(box_texture_readback_sample(world, tex, u, v, AddressClamp, out), true);
            for (c = 0; c < 4; c ++) {
                test_assert(fabsf(out[c] - expect[c]) <= tolerance);
            }

            /* A quarter of the way to the next texel center along u */
            u = ((float)x + 0.75f) / TR_WIDTH;
            v = ((float)y + 0.5f) / TR_HEIGHT;
            tr_expect(format, scale, u, v, expect);
            test_bool(box_texture_readback_sample(world, tex, u, v, AddressClamp, out), true);
            for (c = 0; c < 4; c ++) {
                test_assert(fabsf(out[c] - expect[c]) <= tolerance);
            }
        }
    }
}

/** Read back a tr_graph in a format and check its copy.
 *
 * @param format Format of the image
 * @param layout Expected layout of the copy
 * @param tolerance Difference a channel may have from its value
 */
static void tr_pattern(
    TextureGraphFormat format,
    TextureReadbackFormat layout,
    float tolerance)
{
    ecs_world_t *world = tr_world();
    ecs_entity_t tex = tr_graph(world, format, NULL);
    box_texture_copy_t copy;
    tr_wait(world, tex, 0);
    const TextureReadbackStatus *status = ecs_get(world, tex, TextureReadbackStatus);
    test_int(status->width, TR_WIDTH);
    test_int(status->height, TR_HEIGHT);
    test_int(status->format, layout);
    test_bool(box_texture_readback(world, tex, &copy), true);
    test_assert(copy.data != NULL);
    test_int(copy.width, TR_WIDTH);
    test_int(copy.height, TR_HEIGHT);
    test_int(copy.format, layout);
    test_int(copy.revision, status->revision);
    tr_check(world, tex, format, 1, tolerance);
    ecs_fini(world);
}

void TextureReadback_r32float(void) {
    tr_pattern(GraphFloat, ReadbackR32Float, TR_TOLERANCE_32);
}

void TextureReadback_rgba16float(void) {
    tr_pattern(GraphHdr, ReadbackRGBA32Float, TR_TOLERANCE_16);
}

void TextureReadback_rgba8(void) {
    tr_pattern(GraphLinear, ReadbackRGBA8, TR_TOLERANCE_8);
}

void TextureReadback_r32float_raw(void) {
    ecs_world_t *world = tr_world();
    ecs_entity_t tex = tr_graph(world, GraphFloat, NULL);
    box_texture_copy_t copy;
    tr_wait(world, tex, 0);
    test_bool(box_texture_readback(world, tex, &copy), true);
    test_int(box_texture_copy_texel_size(copy.format), 4);
    const float *f = copy.data;
    /* Texel (3, 5) */
    test_assert(fabsf(f[5 * TR_WIDTH + 3] - (3.5f / TR_WIDTH + 10 * 5.5f / TR_HEIGHT)) <
        TR_TOLERANCE_32);
    ecs_fini(world);
}

void TextureReadback_before_first_copy(void) {
    ecs_world_t *world = tr_world();
    ecs_entity_t tex = tr_graph(world, GraphFloat, NULL);
    box_texture_copy_t copy;
    float out[4] = { 1, 1, 1, 1 };
    test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.5f, AddressClamp, out), false);
    test_flt(out[0], 0);
    test_bool(box_texture_readback_texel(world, tex, 1, 1, out), false);
    test_bool(box_texture_readback(world, tex, &copy), false);
    tr_frame(world);
    const TextureReadbackStatus *status = ecs_get(world, tex, TextureReadbackStatus);
    if (status && !status->ready) {
        test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.5f, AddressClamp, out),
            false);
        test_int(status->revision, 0);
    }

    tr_wait(world, tex, 0);
    test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.5f, AddressClamp, out), true);
    ecs_fini(world);
}

void TextureReadback_graph_change(void) {
    ecs_world_t *world = tr_world();
    ecs_entity_t uv;
    ecs_entity_t tex = tr_graph(world, GraphFloat, &uv);
    int32_t revision = tr_wait(world, tex, 0), next;
    tr_check(world, tex, GraphFloat, 1, TR_TOLERANCE_32);
    ecs_set(world, uv, TexCoords, {.scale = 2});
    next = tr_wait(world, tex, revision);
    test_assert(next > revision);
    tr_check(world, tex, GraphFloat, 2, 2 * TR_TOLERANCE_32);
    test_int(ecs_get(world, tex, TextureReadbackStatus)->revision, next);
    ecs_fini(world);
}

void TextureReadback_keep_off(void) {
    ecs_world_t *world = tr_world();
    ecs_entity_t uv;
    ecs_entity_t tex = tr_graph(world, GraphFloat, &uv);
    int32_t revision, i;
    bool gap = false;
    ecs_set(world, tex, TextureReadback, {.keep = false});
    revision = tr_wait(world, tex, 0);
    ecs_set(world, uv, TexCoords, {.scale = 2});
    for (i = 0; i < TR_FRAMES; i ++) {
        const TextureReadbackStatus *status;
        float out[4];
        tr_frame(world);
        status = ecs_get(world, tex, TextureReadbackStatus);
        if (!status->ready) {
            gap = true;
            test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.5f, AddressClamp, out),
                false);
        } else if (status->revision > revision) {
            break;
        } else {
            /* Until the new revision, a ready copy is the old one */
            test_int(status->revision, revision);
        }
    }

    test_bool(gap, true);
    tr_check(world, tex, GraphFloat, 2, 2 * TR_TOLERANCE_32);
    ecs_fini(world);
}

void TextureReadback_level(void) {
    ecs_world_t *world = tr_world();
    ecs_entity_t tex = tr_graph(world, GraphFloat, NULL);
    float out[4];
    ecs_set(world, tex, TextureReadback, {.level = 1, .keep = true});
    tr_wait(world, tex, 0);
    const TextureReadbackStatus *status = ecs_get(world, tex, TextureReadbackStatus);
    test_int(status->width, TR_WIDTH / 2);
    test_int(status->height, TR_HEIGHT / 2);
    /* The average of a linear pattern over 2 x 2 texels is its value at their shared corner,
     * the center of the texel of the level */
    test_bool(box_texture_readback_sample(world, tex, 0.375f, 0.625f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - (0.375f + 10 * 0.625f)) < TR_TOLERANCE_32 * 10);
    ecs_fini(world);
}
