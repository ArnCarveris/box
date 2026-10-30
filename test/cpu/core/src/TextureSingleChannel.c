/**
 * @file TextureSingleChannel.c
 * @brief Tests for TexturePath.single_channel: an image loads as one R8 channel, the red
 * channel of the file as stored.
 */

#include <box.h>
#include <bake_test.h>
#include "texture.h"

/** Grayscale PNG of 4 by 2 pixels: 0, 64, 128, 255 over 32, 96, 160, 224 */
#define GRAY_PNG "test/assets/textures/gray.png"

/** RGB PNG of 4 by 2 pixels with distinct red, green and blue channels */
#define RGB_PNG "test/assets/textures/rgb.png"

/** Create a headless world, which waits for its texture loads.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Run frames until a texture is no longer loading.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The status
 */
static const TextureStatus* wait_texture(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    int32_t frames = 0;
    ecs_progress(world, 0);
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    return ecs_get(world, tex, TextureStatus);
}

/** Get the registry entry of a texture entity.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The entry
 */
static const box_texture_t* entry_of(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    box_textures_t *t = box_textures_get(world);
    test_assert(t != NULL);
    box_texture_t *entry = ecs_map_get_deref(&t->textures, box_texture_t, tex);
    test_assert(entry != NULL);

    return entry;
}

/** Check that a texture holds the red channel of the RGB PNG.
 *
 * @param world The world
 * @param tex The texture entity
 */
static void test_rgb_red(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    static const uint8_t red[8] = { 200, 100, 0, 255, 50, 10, 220, 128 };
    const box_texture_t *entry = entry_of(world, tex);
    int32_t i;
    test_bool(entry->single_channel, true);
    test_assert(entry->pixels != NULL);
    for (i = 0; i < 8; i ++) {
        test_int(entry->pixels[i], red[i]);
    }
}

void TextureSingleChannel_red_channel(void) {
    // materials/textures
    uint8_t px[3 * 4] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
    box_texture_red_channel(px, 3);
    test_int(px[0], 10);
    test_int(px[1], 50);
    test_int(px[2], 90);
}

void TextureSingleChannel_default_off(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = GRAY_PNG });
    test_bool(ecs_get(world, tex, TexturePath)->single_channel, false);

    const char *code =
        "using box.texture\n"
        "plain { TexturePath: {path: \"" GRAY_PNG "\"} }\n"
        "mask { TexturePath: {path: \"" GRAY_PNG "\", single_channel: true} }\n";
    test_assert(ecs_script_run(world, "single", code, NULL) == 0);
    test_bool(ecs_get(world, ecs_lookup(world, "plain"), TexturePath)->single_channel, false);
    test_bool(ecs_get(world, ecs_lookup(world, "mask"), TexturePath)->single_channel, true);

    test_int(wait_texture(world, tex)->state, TextureReady);
    test_bool(entry_of(world, tex)->single_channel, false);

    ecs_fini(world);
}

void TextureSingleChannel_gray_png(void) {
    // materials/textures
    static const uint8_t gray[8] = { 0, 64, 128, 255, 32, 96, 160, 224 };
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = GRAY_PNG, .single_channel = true });
    const TextureStatus *status = wait_texture(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_assert(status->error == NULL);
    const Texture *size = ecs_get(world, tex, Texture);
    test_assert(size != NULL);
    test_int(size->width, 4);
    test_int(size->height, 2);

    const box_texture_t *entry = entry_of(world, tex);
    int32_t i;
    test_bool(entry->single_channel, true);
    test_int(entry->width, 4);
    test_int(entry->height, 2);
    test_assert(entry->pixels != NULL);
    for (i = 0; i < 8; i ++) {
        test_int(entry->pixels[i], gray[i]);
    }

    ecs_fini(world);
}

void TextureSingleChannel_rgb_png(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = RGB_PNG, .single_channel = true });
    const TextureStatus *status = wait_texture(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    const Texture *size = ecs_get(world, tex, Texture);
    test_assert(size != NULL);
    test_int(size->width, 4);
    test_int(size->height, 2);
    test_rgb_red(world, tex);

    ecs_fini(world);
}

void TextureSingleChannel_linear_ignored(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {
        .path = RGB_PNG, .single_channel = true, .linear = true });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_rgb_red(world, tex);

    ecs_fini(world);
}

void TextureSingleChannel_change_reloads(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = RGB_PNG });
    test_int(wait_texture(world, tex)->state, TextureReady);
    int32_t revision = ecs_get(world, tex, TextureStatus)->revision;
    const box_texture_t *entry = entry_of(world, tex);
    test_bool(entry->single_channel, false);
    test_int(entry->pixels[0], 200);
    test_int(entry->pixels[1], 10);

    ecs_set(world, tex, TexturePath, { .path = RGB_PNG, .single_channel = true });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, revision + 1);
    test_rgb_red(world, tex);

    ecs_set(world, tex, TexturePath, { .path = RGB_PNG });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, revision + 2);
    entry = entry_of(world, tex);
    test_bool(entry->single_channel, false);
    test_int(entry->pixels[1], 10);

    ecs_fini(world);
}

void TextureSingleChannel_resized(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {
        .path = GRAY_PNG, .width = 8, .single_channel = true });
    test_int(wait_texture(world, tex)->state, TextureReady);
    const Texture *size = ecs_get(world, tex, Texture);
    test_int(size->width, 8);
    test_int(size->height, 4);
    const box_texture_t *entry = entry_of(world, tex);
    test_bool(entry->single_channel, true);
    test_int(entry->width, 8);
    test_int(entry->height, 4);
    test_int(entry->pixels[0], 0);

    ecs_fini(world);
}

/** Real allocation functions, restored after cpu_copy_size */
static ecs_os_api_malloc_t real_malloc;
static ecs_os_api_realloc_t real_realloc;
static ecs_os_api_calloc_t real_calloc;

/** Last buffer allocated or reallocated and its size in bytes */
static void *last_ptr;
static ecs_size_t last_size;

static void* record_malloc(
    ecs_size_t size)
{
    last_ptr = real_malloc(size);
    last_size = size;

    return last_ptr;
}

static void* record_realloc(
    void *ptr,
    ecs_size_t size)
{
    last_ptr = real_realloc(ptr, size);
    last_size = size;

    return last_ptr;
}

static void* record_calloc(
    ecs_size_t size)
{
    last_ptr = real_calloc(size);
    last_size = size;

    return last_ptr;
}

void TextureSingleChannel_cpu_copy_size(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    static const int32_t widths[2] = { 0, 8 };
    static const int32_t sizes[2] = { 4 * 2, 8 * 4 };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_texture_job_t job = {
            .path = RGB_PNG, .request_width = widths[i], .single_channel = true };

        real_malloc = ecs_os_api.malloc_;
        real_realloc = ecs_os_api.realloc_;
        real_calloc = ecs_os_api.calloc_;
        ecs_os_api.malloc_ = record_malloc;
        ecs_os_api.realloc_ = record_realloc;
        ecs_os_api.calloc_ = record_calloc;
        last_ptr = NULL;
        last_size = 0;
        box_texture_decode(&job);
        ecs_os_api.malloc_ = real_malloc;
        ecs_os_api.realloc_ = real_realloc;
        ecs_os_api.calloc_ = real_calloc;

        test_assert(job.error == NULL);
        test_assert(job.pixels != NULL);
        test_int(job.width * job.height, sizes[i]);
        test_assert(last_ptr == job.pixels);
        test_int(last_size, sizes[i]);

        ecs_os_free(job.pixels);
        ecs_os_free(job.file);
    }

    ecs_fini(world);
}
