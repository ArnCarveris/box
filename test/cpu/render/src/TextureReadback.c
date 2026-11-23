/**
 * @file TextureReadback.c
 * @brief Tests for the CPU copies of TextureReadback: the sampling math on synthetic copies
 * (texels, bilinear by uv, clamp and wrap, the formats), the defaults of the component, the copy
 * of an image made on the CPU with the null backend, its mip level, its update with the image,
 * the script functions and the copy freed with the component.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "texture.h"

/** Frames a test waits at most for a copy */
#define RB_FRAMES (30)

/** Difference a sampled value may have from its expected value */
#define RB_EPS (1e-5f)

/** Create a world that renders into the null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* rb_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void rb_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 64.0f);
    box_render_sync(world);
}

/** Set the TexturePixels of a texture to a gray ramp: texel (x, y) is x * 16 + y in its red
 * channel (or its only channel), 255 - that in green, 7 in blue and 255 in alpha.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Width of the image
 * @param height Height of the image
 * @param single One channel per texel
 * @param offset Added to every red value
 */
static void rb_pixels(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width,
    int32_t height,
    bool single,
    int32_t offset)
{
    TexturePixels *p = ecs_ensure(world, tex, TexturePixels);
    int32_t channels = single ? 1 : 4, x, y;
    ecs_os_free(p->pixels);
    p->width = width;
    p->height = height;
    p->single_channel = single;
    p->pixels = ecs_os_malloc_n(uint8_t, width * height * channels);
    for (y = 0; y < height; y ++) {
        for (x = 0; x < width; x ++) {
            uint8_t *t = &p->pixels[(y * width + x) * channels];
            t[0] = (uint8_t)(x * 16 + y + offset);
            if (!single) {
                t[1] = (uint8_t)(255 - (x * 16 + y));
                t[2] = 7;
                t[3] = 255;
            }
        }
    }

    ecs_modified(world, tex, TexturePixels);
}

/** Run frames until the copy of a texture is ready with a revision above one.
 *
 * @param world The world
 * @param tex The texture entity
 * @param above The revision the copy must be above
 * @return The status
 */
static const TextureReadbackStatus* rb_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t above)
{
    const TextureReadbackStatus *status = NULL;
    int32_t i;
    for (i = 0; i < RB_FRAMES; i ++) {
        rb_frame(world);
        status = ecs_get(world, tex, TextureReadbackStatus);
        if (status && status->ready && status->revision > above) {
            break;
        }
    }

    test_assert(status != NULL);
    test_bool(status->ready, true);
    test_assert(status->revision > above);

    return status;
}

void TextureReadback_texel_formats(void) {
    uint8_t r8[4] = { 0, 51, 102, 255 };
    uint8_t rgba8[8] = { 255, 0, 51, 102, 0, 255, 255, 0 };
    float r32[2] = { -3.5f, 1000.25f };
    float rgba32[8] = { 1, 2, 3, 4, -1, -2, 100, 0.5f };
    float out[4];
    box_texture_copy_t copy = { .data = r8, .width = 2, .height = 2, .format = ReadbackR8 };
    test_bool(box_texture_copy_texel(&copy, 1, 0, out), true);
    test_flt(out[0], 0.2f);
    test_flt(out[1], 0);
    test_flt(out[2], 0);
    test_flt(out[3], 1);
    test_bool(box_texture_copy_texel(&copy, 1, 1, out), true);
    test_flt(out[0], 1);

    copy = (box_texture_copy_t){ .data = rgba8, .width = 2, .height = 1,
        .format = ReadbackRGBA8 };
    test_bool(box_texture_copy_texel(&copy, 0, 0, out), true);
    test_flt(out[0], 1);
    test_flt(out[1], 0);
    test_flt(out[2], 0.2f);
    test_flt(out[3], 0.4f);
    test_bool(box_texture_copy_texel(&copy, 1, 0, out), true);
    test_flt(out[1], 1);
    test_flt(out[3], 0);

    copy = (box_texture_copy_t){ .data = r32, .width = 1, .height = 2,
        .format = ReadbackR32Float };
    test_bool(box_texture_copy_texel(&copy, 0, 1, out), true);
    test_flt(out[0], 1000.25f);
    test_flt(out[3], 1);

    copy = (box_texture_copy_t){ .data = rgba32, .width = 2, .height = 1,
        .format = ReadbackRGBA32Float };
    test_bool(box_texture_copy_texel(&copy, 1, 0, out), true);
    test_flt(out[0], -1);
    test_flt(out[1], -2);
    test_flt(out[2], 100);
    test_flt(out[3], 0.5f);

    test_int(box_texture_copy_texel_size(ReadbackNone), 0);
    test_int(box_texture_copy_texel_size(ReadbackR8), 1);
    test_int(box_texture_copy_texel_size(ReadbackRGBA8), 4);
    test_int(box_texture_copy_texel_size(ReadbackR32Float), 4);
    test_int(box_texture_copy_texel_size(ReadbackRGBA32Float), 16);
}

void TextureReadback_texel_clamps(void) {
    float r32[6] = { 0, 1, 2, 10, 11, 12 };
    float out[4];
    box_texture_copy_t copy = { .data = r32, .width = 3, .height = 2,
        .format = ReadbackR32Float };
    test_bool(box_texture_copy_texel(&copy, -5, 0, out), true);
    test_flt(out[0], 0);
    test_bool(box_texture_copy_texel(&copy, 7, 9, out), true);
    test_flt(out[0], 12);
    test_bool(box_texture_copy_texel(&copy, 1, -1, out), true);
    test_flt(out[0], 1);
}

void TextureReadback_sample_bilinear(void) {
    /* A 4 x 2 copy whose texel (x, y) is x + 10 y: bilinear between centers is linear */
    float r32[8] = { 0, 1, 2, 3, 10, 11, 12, 13 };
    float out[4];
    int32_t x, y;
    box_texture_copy_t copy = { .data = r32, .width = 4, .height = 2,
        .format = ReadbackR32Float };
    for (y = 0; y < 2; y ++) {
        for (x = 0; x < 4; x ++) {
            float u = ((float)x + 0.5f) / 4, v = ((float)y + 0.5f) / 2;
            test_bool(box_texture_copy_sample(&copy, u, v, AddressClamp, out), true);
            test_assert(fabsf(out[0] - (float)(x + 10 * y)) < RB_EPS);
            test_flt(out[3], 1);
        }
    }

    /* Halfway between texels (1, 0) and (2, 0), and a quarter of the way down to row 1 */
    test_bool(box_texture_copy_sample(&copy, 0.5f, 0.25f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 1.5f) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 0.5f, 0.375f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 4.0f) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 0.5f, 0.5f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 6.5f) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 0.3125f, 0.25f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 0.75f) < RB_EPS);
}

void TextureReadback_sample_clamp(void) {
    float r32[8] = { 0, 1, 2, 3, 10, 11, 12, 13 };
    float out[4];
    box_texture_copy_t copy = { .data = r32, .width = 4, .height = 2,
        .format = ReadbackR32Float };
    test_bool(box_texture_copy_sample(&copy, 0, 0, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 0) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 1, 1, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 13) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, -3, 0.25f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 0) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 5, 0.75f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 13) < RB_EPS);
}

void TextureReadback_sample_wrap(void) {
    float r32[8] = { 0, 1, 2, 3, 10, 11, 12, 13 };
    float out[4];
    box_texture_copy_t copy = { .data = r32, .width = 4, .height = 2,
        .format = ReadbackR32Float };
    /* The left edge is halfway between the last and the first column */
    test_bool(box_texture_copy_sample(&copy, 0, 0.25f, AddressWrap, out), true);
    test_assert(fabsf(out[0] - 1.5f) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 1, 0.25f, AddressWrap, out), true);
    test_assert(fabsf(out[0] - 1.5f) < RB_EPS);
    /* One whole repeat to the left or right samples the same */
    test_bool(box_texture_copy_sample(&copy, -0.625f, 0.25f, AddressWrap, out), true);
    test_assert(fabsf(out[0] - 1) < RB_EPS);
    test_bool(box_texture_copy_sample(&copy, 2.375f, 0.75f, AddressWrap, out), true);
    test_assert(fabsf(out[0] - 11) < RB_EPS);
    /* The top edge is halfway between the rows */
    test_bool(box_texture_copy_sample(&copy, 0.125f, 0, AddressWrap, out), true);
    test_assert(fabsf(out[0] - 5) < RB_EPS);
}

void TextureReadback_sample_rgba8(void) {
    uint8_t rgba8[8] = { 0, 255, 0, 255, 255, 0, 51, 255 };
    float out[4];
    box_texture_copy_t copy = { .data = rgba8, .width = 2, .height = 1,
        .format = ReadbackRGBA8 };
    test_bool(box_texture_copy_sample(&copy, 0.5f, 0.5f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - 0.5f) < RB_EPS);
    test_assert(fabsf(out[1] - 0.5f) < RB_EPS);
    test_assert(fabsf(out[2] - 0.1f) < RB_EPS);
    test_assert(fabsf(out[3] - 1) < RB_EPS);
}

void TextureReadback_sample_empty(void) {
    float out[4] = { 5, 5, 5, 5 };
    box_texture_copy_t copy = { 0 };
    test_bool(box_texture_copy_sample(&copy, 0.5f, 0.5f, AddressClamp, out), false);
    test_flt(out[0], 0);
    test_flt(out[3], 0);
    test_bool(box_texture_copy_texel(&copy, 0, 0, out), false);
    test_bool(box_texture_copy_sample(NULL, 0.5f, 0.5f, AddressClamp, out), false);
}

void TextureReadback_defaults(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    ecs_add(world, tex, TextureReadback);
    const TextureReadback *r = ecs_get(world, tex, TextureReadback);
    test_assert(r != NULL);
    test_int(r->level, 0);
    test_bool(r->keep, true);
    test_assert(ecs_lookup(world, "box.texture.Readback") == ecs_id(TextureReadback));
    test_assert(ecs_lookup(world, "box.texture.ReadbackStatus") ==
        ecs_id(TextureReadbackStatus));
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_before_copy(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    box_texture_copy_t copy;
    float out[4];
    ecs_add(world, tex, TextureReadback);
    test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.5f, AddressClamp, out), false);
    test_bool(box_texture_readback_texel(world, tex, 0, 0, out), false);
    test_bool(box_texture_readback(world, tex, &copy), false);
    test_assert(copy.data == NULL);
    rb_frame(world);
    const TextureReadbackStatus *status = ecs_get(world, tex, TextureReadbackStatus);
    test_assert(status != NULL);
    test_bool(status->ready, false);
    test_int(status->revision, 0);
    test_int(status->format, ReadbackNone);
    test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.5f, AddressClamp, out), false);
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_pixels_copy(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    box_texture_copy_t copy;
    float out[4];
    ecs_add(world, tex, TextureReadback);
    rb_pixels(world, tex, 8, 4, false, 0);
    const TextureReadbackStatus *status = rb_wait(world, tex, 0);
    test_int(status->width, 8);
    test_int(status->height, 4);
    test_int(status->format, ReadbackRGBA8);
    test_bool(box_texture_readback(world, tex, &copy), true);
    test_int(copy.width, 8);
    test_int(copy.height, 4);
    test_int(copy.revision, status->revision);
    test_int(((const uint8_t*)copy.data)[(2 * 8 + 3) * 4], 3 * 16 + 2);
    test_bool(box_texture_readback_texel(world, tex, 3, 2, out), true);
    test_assert(fabsf(out[0] - (float)(3 * 16 + 2) / 255.0f) < RB_EPS);
    test_assert(fabsf(out[1] - (float)(255 - (3 * 16 + 2)) / 255.0f) < RB_EPS);
    test_assert(fabsf(out[2] - 7.0f / 255.0f) < RB_EPS);
    test_assert(fabsf(out[3] - 1) < RB_EPS);
    /* Between texels (3, 2) and (4, 2) */
    test_bool(box_texture_readback_sample(world, tex, 0.5f, 0.625f, AddressClamp, out), true);
    test_assert(fabsf(out[0] - (float)(3 * 16 + 2 + 8) / 255.0f) < RB_EPS);
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_single_channel(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    float out[4];
    ecs_add(world, tex, TextureReadback);
    rb_pixels(world, tex, 4, 4, true, 0);
    const TextureReadbackStatus *status = rb_wait(world, tex, 0);
    test_int(status->format, ReadbackR8);
    test_bool(box_texture_readback_texel(world, tex, 2, 1, out), true);
    test_assert(fabsf(out[0] - 33.0f / 255.0f) < RB_EPS);
    test_flt(out[1], 0);
    test_flt(out[3], 1);
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_level(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    float out[4];
    ecs_set(world, tex, TextureReadback, {.level = 1, .keep = true});
    rb_pixels(world, tex, 8, 4, true, 0);
    const TextureReadbackStatus *status = rb_wait(world, tex, 0);
    test_int(status->width, 4);
    test_int(status->height, 2);
    /* Texel (1, 1) averages (2..3, 2..3): x * 16 + y averages to 42.5, stored in 8 bits */
    test_bool(box_texture_readback_texel(world, tex, 1, 1, out), true);
    test_assert(fabsf(out[0] * 255.0f - 42.5f) < 0.5f + 1e-3f);
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_follows_revision(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    float out[4];
    int32_t revision;
    ecs_add(world, tex, TextureReadback);
    rb_pixels(world, tex, 4, 4, true, 0);
    revision = rb_wait(world, tex, 0)->revision;
    test_bool(box_texture_readback_texel(world, tex, 0, 0, out), true);
    test_assert(fabsf(out[0]) < RB_EPS);
    rb_pixels(world, tex, 4, 4, true, 100);
    test_assert(rb_wait(world, tex, revision)->revision > revision);
    test_bool(box_texture_readback_texel(world, tex, 0, 0, out), true);
    test_assert(fabsf(out[0] - 100.0f / 255.0f) < RB_EPS);
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_remove_frees(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_new(world);
    float out[4];
    ecs_add(world, tex, TextureReadback);
    rb_pixels(world, tex, 4, 4, true, 0);
    rb_wait(world, tex, 0);
    ecs_remove(world, tex, TextureReadback);
    test_bool(box_texture_readback_texel(world, tex, 0, 0, out), false);
    test_bool(ecs_has(world, tex, TextureReadbackStatus), false);
    test_assert(box_texture_readback_get(box_textures_get(world), tex) == NULL);
    rb_frame(world);
    test_bool(ecs_has(world, tex, TextureReadbackStatus), false);

    ecs_add(world, tex, TextureReadback);
    rb_wait(world, tex, 0);
    ecs_delete(world, tex);
    test_assert(box_texture_readback_get(box_textures_get(world), tex) == NULL);
    rb_frame(world);
    ecs_fini(world);
    box_render_null_free(null);
}

void TextureReadback_script_sample(void) {
    box_render_null_t *null;
    ecs_world_t *world = rb_world(&null);
    ecs_entity_t tex = ecs_entity(world, { .name = "rb_tex" });
    ecs_add(world, tex, TextureReadback);
    rb_pixels(world, tex, 8, 4, false, 0);
    rb_wait(world, tex, 0);
    test_assert(ecs_script_run(world, "rb_script",
        "using box.transform\n"
        "rb_out {\n"
        "  Position: {\n"
        "    x: box.texture.sample(rb_tex, 0.4375, 0.625)\n"
        "    y: box.texture.sample_channel(rb_tex, 0.4375, 0.625, 1)\n"
        "    z: box.texture.sample_wrap(rb_tex, 0, 0.125)\n"
        "  }\n"
        "}\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "rb_out");
    test_assert(e != 0);
    const Position *p = ecs_get(world, e, Position);
    test_assert(p != NULL);
    /* The center of texel (3, 2), and the left edge halfway between columns 7 and 0 */
    test_assert(fabsf(p->x - (float)(3 * 16 + 2) / 255.0f) < 1e-4f);
    test_assert(fabsf(p->y - (float)(255 - (3 * 16 + 2)) / 255.0f) < 1e-4f);
    test_assert(fabsf(p->z - (float)(7 * 16 / 2) / 255.0f) < 1e-4f);
    ecs_fini(world);
    box_render_null_free(null);
}
