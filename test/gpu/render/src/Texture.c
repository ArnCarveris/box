/**
 * @file Texture.c
 * @brief Tests for loading textures from files and sampling them in PBR texture layers.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "render.h"
#include "../../../../src/jobs/jobs.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

void Texture_load_png(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/load.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 8, 4, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status != NULL);
    test_int(status->state, TextureLoading);
    test_int(status->revision, 0);
    test_assert(!ecs_has(world, tex, Texture));

    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_assert(status->error == NULL);
    const Texture *t = ecs_get(world, tex, Texture);
    test_assert(t != NULL);
    test_int(t->width, 8);
    test_int(t->height, 4);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_requested_size(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/size.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 8, 4, 4, px, 8 * 4) != 0);

    ecs_entity_t both = ecs_new(world);
    ecs_set(world, both, TexturePath, {.path = file, .width = 5, .height = 7});
    ecs_entity_t width = ecs_new(world);
    ecs_set(world, width, TexturePath, {.path = file, .width = 16});
    ecs_entity_t height = ecs_new(world);
    ecs_set(world, height, TexturePath, {.path = file, .height = 2});
    int32_t frames = 0;
    while ((ecs_get(world, both, TextureStatus)->state == TextureLoading ||
        ecs_get(world, width, TextureStatus)->state == TextureLoading ||
        ecs_get(world, height, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, both, Texture)->width, 5);
    test_int(ecs_get(world, both, Texture)->height, 7);
    test_int(ecs_get(world, width, Texture)->width, 16);
    test_int(ecs_get(world, width, Texture)->height, 8);
    test_int(ecs_get(world, height, Texture)->width, 4);
    test_int(ecs_get(world, height, Texture)->height, 2);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_load_svg(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/icon.svg", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
        "<rect width=\"40\" height=\"20\" fill=\"#ff0000\"/></svg>", f);
    fclose(f);

    ecs_entity_t native = ecs_new(world);
    ecs_set(world, native, TexturePath, {.path = file});
    ecs_entity_t scaled = ecs_new(world);
    ecs_set(world, scaled, TexturePath, {.path = file, .width = 200});
    int32_t frames = 0;
    while ((ecs_get(world, native, TextureStatus)->state == TextureLoading ||
        ecs_get(world, scaled, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, native, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, native, Texture)->width, 40);
    test_int(ecs_get(world, native, Texture)->height, 20);
    test_int(ecs_get(world, scaled, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, scaled, Texture)->width, 200);
    test_int(ecs_get(world, scaled, Texture)->height, 100);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_missing_file_fails(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_log_set_level(-4);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = "no/such/texture.png"});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureFailed);
    test_int(status->revision, 0);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "no/such/texture.png") != NULL);
    test_assert(!ecs_has(world, tex, Texture));

    ecs_fini(world);
}

void Texture_invalid_image_fails(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/broken.png", tmp);
    char *svg = flecs_asprintf("%s/broken.svg", tmp);
    FILE *f = fopen(png, "w");
    test_assert(f != NULL);
    fputs("this is not an image", f);
    fclose(f);
    f = fopen(svg, "w");
    test_assert(f != NULL);
    fputs("<svg width=", f);
    fclose(f);

    ecs_log_set_level(-4);
    ecs_entity_t raster = ecs_new(world);
    ecs_set(world, raster, TexturePath, {.path = png});
    ecs_entity_t vector = ecs_new(world);
    ecs_set(world, vector, TexturePath, {.path = svg});
    int32_t frames = 0;
    while ((ecs_get(world, raster, TextureStatus)->state == TextureLoading ||
        ecs_get(world, vector, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, raster, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "broken.png") != NULL);
    status = ecs_get(world, vector, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "broken.svg") != NULL);
    test_assert(!ecs_has(world, raster, Texture));
    test_assert(!ecs_has(world, vector, Texture));

    ecs_os_free(png);
    ecs_os_free(svg);
    ecs_fini(world);
}

void Texture_reload_on_path_change(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *first = flecs_asprintf("%s/first.png", tmp);
    char *second = flecs_asprintf("%s/second.png", tmp);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(first, 8, 4, 4, px, 8 * 4) != 0);
    test_assert(stbi_write_png(second, 2, 2, 4, px, 2 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = first});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    test_int(ecs_get(world, tex, Texture)->width, 8);

    ecs_set(world, tex, TexturePath, {.path = second});
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureLoading);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 2);
    test_int(ecs_get(world, tex, Texture)->height, 2);

    ecs_os_free(first);
    ecs_os_free(second);
    ecs_fini(world);
}

void Texture_failed_reload_keeps_image(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/keep.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 8, 4, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);

    ecs_log_set_level(-4);
    ecs_set(world, tex, TexturePath, {.path = "no/such/file.png"});
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureFailed);
    test_int(status->revision, 1);
    test_assert(strstr(status->error, "no/such/file.png") != NULL);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    test_int(ecs_get(world, tex, Texture)->height, 4);

    ecs_set(world, tex, TexturePath, {.path = file});
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_assert(status->error == NULL);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_remove_path(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/remove.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[4 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t pending = ecs_new(world);
    ecs_set(world, pending, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_assert(ecs_has(world, tex, Texture));
    ecs_remove(world, tex, TexturePath);
    test_assert(!ecs_has(world, tex, Texture));
    test_assert(!ecs_has(world, tex, TextureStatus));

    ecs_set(world, pending, TexturePath, {.path = file, .width = 2});
    ecs_remove(world, pending, TexturePath);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, pending, TextureStatus));
    test_assert(!ecs_has(world, tex, Texture));

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_texture_quality_caps_size(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/large.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t *px = ecs_os_calloc(1024 * 256 * 4);
    test_assert(stbi_write_png(file, 1024, 256, 4, px, 1024 * 4) != 0);
    ecs_os_free(px);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .texture_quality = TextureQualityLow,
        .resolution_scale = 1});
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t small = ecs_new(world);
    ecs_set(world, small, TexturePath, {.path = file, .width = 128});
    int32_t frames = 0;
    while ((ecs_get(world, tex, TextureStatus)->state == TextureLoading ||
        ecs_get(world, small, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, Texture)->width, 512);
    test_int(ecs_get(world, tex, Texture)->height, 128);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    test_int(ecs_get(world, small, Texture)->width, 128);
    test_int(ecs_get(world, small, Texture)->height, 32);

    Surface *surface = ecs_get_mut(world, s, Surface);
    surface->texture_quality = TextureQualityMedium;
    ecs_modified(world, s, Surface);
    ecs_progress(world, 0);
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, Texture)->width, 1024);
    test_int(ecs_get(world, tex, Texture)->height, 256);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    test_int(ecs_get(world, small, Texture)->width, 128);

    ecs_os_free(file);
    ecs_fini(world);
}

/** Write a grey PNG and give it a modification time, so its stamp differs for sure from the
 * stamp of any earlier content.
 *
 * @param path The file
 * @param width Width of the image
 * @param height Height of the image
 * @param mtime Seconds of the modification time
 */
static void texture_write(
    const char *path,
    int32_t width,
    int32_t height,
    int64_t mtime)
{
    uint8_t *px = ecs_os_malloc(width * height * 4);
    memset(px, 200, (size_t)(width * height * 4));
    test_assert(stbi_write_png(path, width, height, 4, px, width * 4) != 0);
    ecs_os_free(px);
    struct timespec times[2] = {{ .tv_sec = (time_t)mtime }, { .tv_sec = (time_t)mtime }};
    test_int(utimensat(AT_FDCWD, path, times, 0), 0);
}

/** Run frames until a texture is no longer loading, checking that the previous image stays
 * until the new one is ready.
 *
 * @param world The world
 * @param tex The texture entity
 * @param width Width of the previous image, 0 when it is not checked
 * @return The status
 */
static const TextureStatus* texture_wait(
    ecs_world_t *world,
    ecs_entity_t tex,
    int32_t width)
{
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        if (width) {
            test_int(ecs_get(world, tex, Texture)->width, width);
        }

        ecs_progress(world, 0);
        frames ++;
    }

    return ecs_get(world, tex, TextureStatus);
}

void Texture_file_change_reloads(void) {
    // engine/file_watch
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, FileWatch, {.interval = 0});
    char *file = flecs_asprintf("%s/change.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    texture_write(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(texture_wait(world, tex, 0)->revision, 1);
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);

    texture_write(file, 2, 6, 1000000100);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status->state == TextureLoading || status->revision == 2);
    if (status->state == TextureLoading) {
        test_int(ecs_get(world, tex, Texture)->width, 8);
        test_int(ecs_get(world, tex, Texture)->height, 4);
    }

    status = texture_wait(world, tex, 8);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_assert(status->error == NULL);
    test_int(ecs_get(world, tex, Texture)->width, 2);
    test_int(ecs_get(world, tex, Texture)->height, 6);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_file_touch_reloads(void) {
    // engine/file_watch
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, FileWatch, {.interval = 0});
    char *file = flecs_asprintf("%s/touch.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    texture_write(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(texture_wait(world, tex, 0)->revision, 1);
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);

    struct timespec times[2] = {{ .tv_sec = 1000000100 }, { .tv_sec = 1000000100 }};
    test_int(utimensat(AT_FDCWD, file, times, 0), 0);
    ecs_progress(world, 0);
    const TextureStatus *status = texture_wait(world, tex, 8);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    test_int(ecs_get(world, tex, Texture)->height, 4);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_file_removed_keeps_image(void) {
    // engine/file_watch
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, FileWatch, {.interval = 0});
    char *file = flecs_asprintf("%s/removed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    texture_write(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(texture_wait(world, tex, 0)->revision, 1);
    ecs_progress(world, 0);

    test_int(remove(file), 0);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    const TextureStatus *status = texture_wait(world, tex, 8);
    test_int(status->state, TextureFailed);
    test_int(status->revision, 1);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "removed.png") != NULL);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    test_int(ecs_get(world, tex, Texture)->height, 4);
    test_int(ecs_singleton_get(world, FileWatchStatus)->files, 1);

    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureFailed);

    texture_write(file, 4, 4, 1000000100);
    ecs_progress(world, 0);
    status = texture_wait(world, tex, 8);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_assert(status->error == NULL);
    test_int(ecs_get(world, tex, Texture)->width, 4);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 2);

    ecs_os_free(file);
    ecs_fini(world);
}

/* Without workers and without the headless wait a decode runs at the end of the frame that
 * requested it and completes in the next frame, before its poll, so a file written between the
 * two frames changes while the reload of the previous change is in flight. */
void Texture_file_change_while_loading(void) {
    // engine/file_watch
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, FileWatch, {.interval = 0});
    char *file = flecs_asprintf("%s/inflight.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    test_int(box_jobs_set_workers(world, 0), 0);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);
    texture_write(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(texture_wait(world, tex, 0)->revision, 1);
    ecs_progress(world, 0);

    texture_write(file, 2, 2, 1000000100);
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureLoading);
    texture_write(file, 6, 2, 1000000200);
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureLoading);
    test_int(status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 2);

    status = texture_wait(world, tex, 2);
    test_int(status->state, TextureReady);
    test_int(status->revision, 3);
    test_int(ecs_get(world, tex, Texture)->width, 6);
    test_int(ecs_get(world, tex, Texture)->height, 2);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 2);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 3);
    test_int(ecs_get(world, tex, Texture)->width, 6);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_emissive_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 3.2f, 0});
    ecs_set(world, plain, Quad, {12, 0.4f});
    ecs_set(world, plain, Rgb, {0, 0, 0});
    ecs_set(world, plain, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, plain, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "emissive.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/emissive.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 12) * 4];
    unsigned char *right = &img[(16 * 64 + 52) * 4];
    unsigned char *top = &img[(1 * 64 + 32) * 4];
    test_assert(left[0] > 240);
    test_assert(left[1] < 20);
    test_assert(right[0] < 20);
    test_assert(right[1] > 240);
    test_assert(top[2] > 240);
    test_assert(top[0] < 20);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_albedo_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/blue.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 20;
        px[i * 4 + 1] = 40;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t textured = ecs_new(world);
    ecs_set(world, textured, Position, {-3, 0, 0});
    ecs_set(world, textured, Quad, {5, 5});
    ecs_set(world, textured, Rgb, {255, 255, 255});
    ecs_set(world, textured, PbrMaterial, {.roughness = 1});
    ecs_set(world, textured, PbrTextures, {.albedo = tex});
    ecs_entity_t tinted = ecs_new(world);
    ecs_set(world, tinted, Position, {3, 0, 0});
    ecs_set(world, tinted, Quad, {5, 5});
    ecs_set(world, tinted, Rgb, {255, 0, 0});
    ecs_set(world, tinted, PbrMaterial, {.roughness = 1});
    ecs_set(world, tinted, PbrTextures, {.albedo = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "albedo.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/albedo.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 18) * 4];
    unsigned char *right = &img[(16 * 64 + 46) * 4];
    test_assert(left[2] > 80);
    test_assert(left[2] > left[0] + 50);
    test_assert(left[2] > left[1] + 40);
    test_assert(right[0] < 40);
    test_assert(right[1] < 40);
    test_assert(right[2] < 40);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_wrap_repeat(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_set(world, quad, TextureTransform, {.scale_x = 2, .scale_y = 1});
    ecs_set(world, quad, TextureWrap, {WrapRepeat});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "repeat.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/repeat.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 10) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 24) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 1] < 20);
    test_assert(img[(16 * 64 + 52) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_wrap_clamp(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_set(world, quad, TextureTransform, {.scale_x = 2, .scale_y = 1});
    ecs_set(world, quad, TextureWrap, {WrapClamp});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "clamp.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/clamp.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 10) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 24) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 0] < 20);
    test_assert(img[(16 * 64 + 52) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_wrap_mirror(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_set(world, quad, TextureTransform, {.scale_x = 2, .scale_y = 1});
    ecs_set(world, quad, TextureWrap, {WrapMirror});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "mirror.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mirror.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 10) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 24) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 52) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 52) * 4 + 1] < 20);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_transform_change(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t still = ecs_new(world);
    ecs_set(world, still, Position, {0, 1.6f, 0});
    ecs_set(world, still, Quad, {12, 3});
    ecs_set(world, still, Rgb, {0, 0, 0});
    ecs_set(world, still, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, still, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, still, PbrTextures, {.emissive = tex});
    ecs_entity_t moving = ecs_new(world);
    ecs_add(world, moving, DynamicMaterial);
    ecs_set(world, moving, Position, {0, -1.6f, 0});
    ecs_set(world, moving, Quad, {12, 3});
    ecs_set(world, moving, Rgb, {0, 0, 0});
    ecs_set(world, moving, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, moving, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, moving, PbrTextures, {.emissive = tex});
    ecs_set(world, moving, TextureTransform, {.scale_x = 1, .scale_y = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "offset.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/offset.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(9 * 64 + 16) * 4 + 0] > 240);
    test_assert(img[(9 * 64 + 48) * 4 + 1] > 240);
    test_assert(img[(23 * 64 + 16) * 4 + 0] > 240);
    test_assert(img[(23 * 64 + 48) * 4 + 1] > 240);
    stbi_image_free(img);

    ecs_set(world, still, TextureTransform, {.scale_x = 1, .scale_y = 1, .offset_x = 0.5f});
    TextureTransform *uv = ecs_get_mut(world, moving, TextureTransform);
    uv->offset_x = 0.5f;
    ecs_progress(world, 0);
    box_render_sync(world);

    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(9 * 64 + 16) * 4 + 1] > 240);
    test_assert(img[(9 * 64 + 48) * 4 + 0] > 240);
    test_assert(img[(23 * 64 + 16) * 4 + 1] > 240);
    test_assert(img[(23 * 64 + 48) * 4 + 0] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_anisotropy(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/stripes.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i, x, y;
    for (i = 0; i < 64; i ++) {
        uint8_t v = (i % 8) < 4 ? 255 : 0;
        px[i * 4 + 0] = v;
        px[i * 4 + 1] = v;
        px[i * 4 + 2] = v;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0.5f, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, 0, -100});
    ecs_set(world, floor, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, floor, Quad, {20, 200});
    ecs_set(world, floor, Rgb, {0, 0, 0});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, floor, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, floor, PbrTextures, {.emissive = tex});
    ecs_set(world, floor, TextureTransform, {.scale_x = 10, .scale_y = 100});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "aniso.png",
        .anisotropy = AnisotropyOff, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/aniso.png", tmp);
    int w = 0, h = 0, n = 0;
    int32_t contrast[2] = {0, 0};
    for (i = 0; i < 2; i ++) {
        if (i) {
            Surface *s = ecs_get_mut(world, e, Surface);
            s->anisotropy = AnisotropyUltra;
            ecs_modified(world, e, Surface);
            ecs_progress(world, 0);
            box_render_sync(world);
        }

        unsigned char *img = stbi_load(path, &w, &h, &n, 4);
        test_assert(img != NULL);
        for (y = 17; y < 19; y ++) {
            int32_t lo = 255, hi = 0;
            for (x = 16; x < 48; x ++) {
                int32_t v = img[(y * 64 + x) * 4 + 1];
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }

            contrast[i] = hi - lo > contrast[i] ? hi - lo : contrast[i];
        }

        stbi_image_free(img);
    }

    test_assert(contrast[1] > contrast[0] + 40);

    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_normal_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *toward = flecs_asprintf("%s/toward.png", tmp);
    char *away = flecs_asprintf("%s/away.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 204;
        px[i * 4 + 1] = 128;
        px[i * 4 + 2] = 230;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(toward, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 51;
    }

    test_assert(stbi_write_png(away, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t lit = ecs_new(world);
    ecs_set(world, lit, TexturePath, {.path = toward});
    ecs_entity_t dark = ecs_new(world);
    ecs_set(world, dark, TexturePath, {.path = away});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-3.5f, 0, 0});
    ecs_set(world, a, Quad, {6, 6});
    ecs_set(world, a, Rgb, {200, 200, 200});
    ecs_set(world, a, PbrMaterial, {.roughness = 1});
    ecs_set(world, a, PbrTextures, {.normal = lit});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {3.5f, 0, 0});
    ecs_set(world, b, Quad, {6, 6});
    ecs_set(world, b, Rgb, {200, 200, 200});
    ecs_set(world, b, PbrMaterial, {.roughness = 1});
    ecs_set(world, b, PbrTextures, {.normal = dark});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "normal.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/normal.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[1] > right[1] + 40);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(toward);
    ecs_os_free(away);
    ecs_fini(world);
}

void Texture_gpu_reload_changes_image(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *red = flecs_asprintf("%s/red.png", tmp);
    char *green = flecs_asprintf("%s/green.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(red, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
    }

    test_assert(stbi_write_png(green, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = red});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "reload.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/reload.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 1] < 20);
    stbi_image_free(img);

    ecs_set(world, tex, TexturePath, {.path = green});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] < 20);
    test_assert(img[(16 * 64 + 32) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(red);
    ecs_os_free(green);
    ecs_fini(world);
}

void Texture_gpu_deleted_texture_unbinds(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/red.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "unbind.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/unbind.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 1] < 20);
    stbi_image_free(img);

    ecs_delete(world, tex);
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 2] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_inherited_from_prefab(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *red = flecs_asprintf("%s/red.png", tmp);
    char *green = flecs_asprintf("%s/green.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(red, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
    }

    test_assert(stbi_write_png(green, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t red_tex = ecs_new(world);
    ecs_set(world, red_tex, TexturePath, {.path = red});
    ecs_entity_t green_tex = ecs_new(world);
    ecs_set(world, green_tex, TexturePath, {.path = green});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, PbrTextures, {.emissive = red_tex});
    ecs_entity_t quad = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    test_assert(!ecs_owns(world, quad, PbrTextures));
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "prefab.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/prefab.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 1] < 20);
    stbi_image_free(img);

    ecs_set(world, prefab, PbrTextures, {.emissive = green_tex});
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] < 20);
    test_assert(img[(16 * 64 + 32) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(red);
    ecs_os_free(green);
    ecs_fini(world);
}

void Texture_gpu_svg_colors(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.svg", tmp);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<rect width=\"4\" height=\"8\" fill=\"#0000ff\"/>"
        "<rect x=\"4\" width=\"4\" height=\"8\" fill=\"#ffff00\"/></svg>", f);
    fclose(f);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file, .width = 64, .height = 64});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "svg.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, tex, Texture)->width, 64);
    char *path = flecs_asprintf("%s/svg.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 12) * 4];
    unsigned char *right = &img[(16 * 64 + 52) * 4];
    test_assert(left[0] < 20);
    test_assert(left[1] < 20);
    test_assert(left[2] > 240);
    test_assert(right[0] > 240);
    test_assert(right[1] > 240);
    test_assert(right[2] < 20);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_albedo_mip_linear_average(void) {
    // lighting_fixes/albedo_mips
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *checker_file = flecs_asprintf("%s/checker.png", tmp);
    char *grey_file = flecs_asprintf("%s/grey.png", tmp);
    uint8_t checker[16 * 16 * 4], grey[16 * 16 * 4];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        uint8_t v = ((i % 16) + (i / 16)) % 2 ? 255 : 0;
        for (c = 0; c < 3; c ++) {
            checker[i * 4 + c] = v;
            grey[i * 4 + c] = 188;
        }

        checker[i * 4 + 3] = 255;
        grey[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(checker_file, 16, 16, 4, checker, 16 * 4) != 0);
    test_assert(stbi_write_png(grey_file, 16, 16, 4, grey, 16 * 4) != 0);

    ecs_entity_t checker_tex = ecs_new(world);
    ecs_set(world, checker_tex, TexturePath, {.path = checker_file});
    ecs_entity_t grey_tex = ecs_new(world);
    ecs_set(world, grey_tex, TexturePath, {.path = grey_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {-3, 0, 0});
    ecs_set(world, far, Quad, {5, 5});
    ecs_set(world, far, Rgb, {255, 255, 255});
    ecs_set(world, far, PbrMaterial, {.roughness = 1});
    ecs_set(world, far, PbrTextures, {.albedo = checker_tex});
    ecs_set(world, far, TextureTransform, {.scale_x = 64, .scale_y = 64});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Position, {3, 0, 0});
    ecs_set(world, flat, Quad, {5, 5});
    ecs_set(world, flat, Rgb, {255, 255, 255});
    ecs_set(world, flat, PbrMaterial, {.roughness = 1});
    ecs_set(world, flat, PbrTextures, {.albedo = grey_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "mips.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mips.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *averaged = &img[(16 * 64 + 18) * 4];
    unsigned char *reference = &img[(16 * 64 + 46) * 4];
    test_assert(reference[1] > 60);
    test_assert(reference[1] < 250);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(averaged[c] - reference[c]) < 12);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(grey_file);
    ecs_os_free(checker_file);
    ecs_fini(world);
}

void Texture_gpu_orm_mip_stored_average(void) {
    // lighting_fixes/albedo_mips
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *checker_file = flecs_asprintf("%s/rough_checker.png", tmp);
    char *half_file = flecs_asprintf("%s/rough_half.png", tmp);
    uint8_t checker[16 * 16 * 4], half[16 * 16 * 4];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        uint8_t v = ((i % 16) + (i / 16)) % 2 ? 255 : 0;
        checker[i * 4 + 0] = 255;
        checker[i * 4 + 1] = v;
        checker[i * 4 + 2] = 255;
        checker[i * 4 + 3] = 255;
        half[i * 4 + 0] = 255;
        half[i * 4 + 1] = 128;
        half[i * 4 + 2] = 255;
        half[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(checker_file, 16, 16, 4, checker, 16 * 4) != 0);
    test_assert(stbi_write_png(half_file, 16, 16, 4, half, 16 * 4) != 0);

    ecs_entity_t checker_tex = ecs_new(world);
    ecs_set(world, checker_tex, TexturePath, {.path = checker_file});
    ecs_entity_t half_tex = ecs_new(world);
    ecs_set(world, half_tex, TexturePath, {.path = half_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 3});
    ecs_set(world, light, PointLight, {.intensity = 2 * GLM_PIf, .range = 20});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {-1.5f, 0, 0});
    ecs_set(world, far, Quad, {2.5f, 2.5f});
    ecs_set(world, far, Rgb, {255, 255, 255});
    ecs_set(world, far, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, far, PbrTextures, {.orm = checker_tex});
    ecs_set(world, far, TextureTransform, {.scale_x = 64, .scale_y = 64});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Position, {1.5f, 0, 0});
    ecs_set(world, flat, Quad, {2.5f, 2.5f});
    ecs_set(world, flat, Rgb, {255, 255, 255});
    ecs_set(world, flat, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, flat, PbrTextures, {.orm = half_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "rough.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/rough.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *averaged = &img[(16 * 64 + 29) * 4];
    unsigned char *reference = &img[(16 * 64 + 34) * 4];
    test_assert(reference[1] > 60);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(averaged[c] - reference[c]) < 12);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(half_file);
    ecs_os_free(checker_file);
    ecs_fini(world);
}

void Texture_gpu_mip_data_then_color(void) {
    // lighting_fixes/albedo_mips
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *checker_file = flecs_asprintf("%s/checker.png", tmp);
    char *grey_file = flecs_asprintf("%s/grey.png", tmp);
    uint8_t checker[16 * 16 * 4], grey[16 * 16 * 4];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        uint8_t v = ((i % 16) + (i / 16)) % 2 ? 255 : 0;
        for (c = 0; c < 3; c ++) {
            checker[i * 4 + c] = v;
            grey[i * 4 + c] = 188;
        }

        checker[i * 4 + 3] = 255;
        grey[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(checker_file, 16, 16, 4, checker, 16 * 4) != 0);
    test_assert(stbi_write_png(grey_file, 16, 16, 4, grey, 16 * 4) != 0);

    ecs_entity_t checker_tex = ecs_new(world);
    ecs_set(world, checker_tex, TexturePath, {.path = checker_file});
    ecs_entity_t grey_tex = ecs_new(world);
    ecs_set(world, grey_tex, TexturePath, {.path = grey_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {-3, 0, 0});
    ecs_set(world, far, Quad, {5, 5});
    ecs_set(world, far, Rgb, {255, 255, 255});
    ecs_set(world, far, PbrMaterial, {.roughness = 1});
    ecs_set(world, far, PbrTextures, {.orm = checker_tex});
    ecs_set(world, far, TextureTransform, {.scale_x = 64, .scale_y = 64});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Position, {3, 0, 0});
    ecs_set(world, flat, Quad, {5, 5});
    ecs_set(world, flat, Rgb, {255, 255, 255});
    ecs_set(world, flat, PbrMaterial, {.roughness = 1});
    ecs_set(world, flat, PbrTextures, {.albedo = grey_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "data_color.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_set(world, far, PbrTextures, {.albedo = checker_tex});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/data_color.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *averaged = &img[(16 * 64 + 18) * 4];
    unsigned char *reference = &img[(16 * 64 + 46) * 4];
    test_assert(reference[1] > 60);
    test_assert(reference[1] < 250);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(averaged[c] - reference[c]) < 12);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(grey_file);
    ecs_os_free(checker_file);
    ecs_fini(world);
}

void Texture_gpu_mip_shared_color_and_data(void) {
    // lighting_fixes/albedo_mips
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *checker_file = flecs_asprintf("%s/checker.png", tmp);
    char *grey_file = flecs_asprintf("%s/grey.png", tmp);
    uint8_t checker[16 * 16 * 4], grey[16 * 16 * 4];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        uint8_t v = ((i % 16) + (i / 16)) % 2 ? 255 : 0;
        for (c = 0; c < 3; c ++) {
            checker[i * 4 + c] = v;
            grey[i * 4 + c] = 188;
        }

        checker[i * 4 + 3] = 255;
        grey[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(checker_file, 16, 16, 4, checker, 16 * 4) != 0);
    test_assert(stbi_write_png(grey_file, 16, 16, 4, grey, 16 * 4) != 0);

    ecs_entity_t checker_tex = ecs_new(world);
    ecs_set(world, checker_tex, TexturePath, {.path = checker_file});
    ecs_entity_t grey_tex = ecs_new(world);
    ecs_set(world, grey_tex, TexturePath, {.path = grey_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {-3, 0, 0});
    ecs_set(world, far, Quad, {5, 5});
    ecs_set(world, far, Rgb, {255, 255, 255});
    ecs_set(world, far, PbrMaterial, {.roughness = 1});
    ecs_set(world, far, PbrTextures, {.albedo = checker_tex, .orm = checker_tex});
    ecs_set(world, far, TextureTransform, {.scale_x = 64, .scale_y = 64});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Position, {3, 0, 0});
    ecs_set(world, flat, Quad, {5, 5});
    ecs_set(world, flat, Rgb, {255, 255, 255});
    ecs_set(world, flat, PbrMaterial, {.roughness = 1});
    ecs_set(world, flat, PbrTextures, {.albedo = grey_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "shared.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/shared.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *averaged = &img[(16 * 64 + 18) * 4];
    unsigned char *reference = &img[(16 * 64 + 46) * 4];
    test_assert(reference[1] > 60);
    test_assert(reference[1] < 250);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(averaged[c] - reference[c]) < 12);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(grey_file);
    ecs_os_free(checker_file);
    ecs_fini(world);
}

void Texture_gpu_mip_reload_keeps_color(void) {
    // lighting_fixes/albedo_mips
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *checker_file = flecs_asprintf("%s/checker.png", tmp);
    char *grey_file = flecs_asprintf("%s/grey.png", tmp);
    uint8_t checker[16 * 16 * 4], grey[16 * 16 * 4];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        uint8_t v = ((i % 16) + (i / 16)) % 2 ? 255 : 0;
        for (c = 0; c < 3; c ++) {
            checker[i * 4 + c] = v;
            grey[i * 4 + c] = 188;
        }

        checker[i * 4 + 3] = 255;
        grey[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(checker_file, 16, 16, 4, checker, 16 * 4) != 0);
    test_assert(stbi_write_png(grey_file, 16, 16, 4, grey, 16 * 4) != 0);

    ecs_entity_t checker_tex = ecs_new(world);
    ecs_set(world, checker_tex, TexturePath, {.path = checker_file});
    ecs_entity_t grey_tex = ecs_new(world);
    ecs_set(world, grey_tex, TexturePath, {.path = grey_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {-3, 0, 0});
    ecs_set(world, far, Quad, {5, 5});
    ecs_set(world, far, Rgb, {255, 255, 255});
    ecs_set(world, far, PbrMaterial, {.roughness = 1});
    ecs_set(world, far, PbrTextures, {.albedo = checker_tex});
    ecs_set(world, far, TextureTransform, {.scale_x = 64, .scale_y = 64});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Position, {3, 0, 0});
    ecs_set(world, flat, Quad, {5, 5});
    ecs_set(world, flat, Rgb, {255, 255, 255});
    ecs_set(world, flat, PbrMaterial, {.roughness = 1});
    ecs_set(world, flat, PbrTextures, {.albedo = grey_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "reloaded.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    char *inverted_file = flecs_asprintf("%s/inverted.png", tmp);
    for (i = 0; i < 256; i ++) {
        for (c = 0; c < 3; c ++) {
            checker[i * 4 + c] = (uint8_t)(255 - checker[i * 4 + c]);
        }
    }

    test_assert(stbi_write_png(inverted_file, 16, 16, 4, checker, 16 * 4) != 0);
    ecs_set(world, checker_tex, TexturePath, {.path = inverted_file});
    int32_t frames = 0;
    while (ecs_get(world, checker_tex, TextureStatus)->state == TextureLoading &&
        frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, checker_tex, TextureStatus)->revision, 2);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_os_free(inverted_file);

    char *path = flecs_asprintf("%s/reloaded.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *averaged = &img[(16 * 64 + 18) * 4];
    unsigned char *reference = &img[(16 * 64 + 46) * 4];
    test_assert(reference[1] > 60);
    test_assert(reference[1] < 250);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(averaged[c] - reference[c]) < 12);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(grey_file);
    ecs_os_free(checker_file);
    ecs_fini(world);
}

void Texture_gpu_emissive_mip_linear_average(void) {
    // lighting_fixes/albedo_mips
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *checker_file = flecs_asprintf("%s/checker.png", tmp);
    char *grey_file = flecs_asprintf("%s/grey.png", tmp);
    uint8_t checker[16 * 16 * 4], grey[16 * 16 * 4];
    int32_t i, c;
    for (i = 0; i < 256; i ++) {
        uint8_t v = ((i % 16) + (i / 16)) % 2 ? 255 : 0;
        for (c = 0; c < 3; c ++) {
            checker[i * 4 + c] = v;
            grey[i * 4 + c] = 188;
        }

        checker[i * 4 + 3] = 255;
        grey[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(checker_file, 16, 16, 4, checker, 16 * 4) != 0);
    test_assert(stbi_write_png(grey_file, 16, 16, 4, grey, 16 * 4) != 0);

    ecs_entity_t checker_tex = ecs_new(world);
    ecs_set(world, checker_tex, TexturePath, {.path = checker_file});
    ecs_entity_t grey_tex = ecs_new(world);
    ecs_set(world, grey_tex, TexturePath, {.path = grey_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {-3, 0, 0});
    ecs_set(world, far, Quad, {5, 5});
    ecs_set(world, far, Rgb, {0, 0, 0});
    ecs_set(world, far, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, far, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, far, PbrTextures, {.emissive = checker_tex});
    ecs_set(world, far, TextureTransform, {.scale_x = 64, .scale_y = 64});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Position, {3, 0, 0});
    ecs_set(world, flat, Quad, {5, 5});
    ecs_set(world, flat, Rgb, {0, 0, 0});
    ecs_set(world, flat, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, flat, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, flat, PbrTextures, {.emissive = grey_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "emissive_mips.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/emissive_mips.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *averaged = &img[(16 * 64 + 18) * 4];
    unsigned char *reference = &img[(16 * 64 + 46) * 4];
    test_assert(reference[1] > 60);
    test_assert(reference[1] < 250);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(averaged[c] - reference[c]) < 12);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(grey_file);
    ecs_os_free(checker_file);
    ecs_fini(world);
}

void Texture_gpu_orm_roughness_green(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/orm_smooth.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 25;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t left_light = ecs_new(world);
    ecs_set(world, left_light, Position, {-1.5f, 0, 2});
    ecs_set(world, left_light, PointLight, {.intensity = 4 * GLM_PIf, .range = 10});
    ecs_entity_t right_light = ecs_new(world);
    ecs_set(world, right_light, Position, {1.5f, 0, 2});
    ecs_set(world, right_light, PointLight, {.intensity = 4 * GLM_PIf, .range = 10});
    ecs_entity_t rough = ecs_new(world);
    ecs_set(world, rough, Position, {-1.5f, 0, 0});
    ecs_set(world, rough, Quad, {2.5f, 2.5f});
    ecs_set(world, rough, Rgb, {255, 255, 255});
    ecs_set(world, rough, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_entity_t smooth = ecs_new(world);
    ecs_set(world, smooth, Position, {1.5f, 0, 0});
    ecs_set(world, smooth, Quad, {2.5f, 2.5f});
    ecs_set(world, smooth, Rgb, {255, 255, 255});
    ecs_set(world, smooth, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, smooth, PbrTextures, {.orm = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 128, .height = 64, .write_to_file = "orm_green.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/orm_green.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    int32_t x, y, rough_peak = 0, smooth_peak = 0, rough_bright = 0, smooth_bright = 0;
    for (y = 24; y < 40; y ++) {
        for (x = 42; x < 58; x ++) {
            rough_peak = glm_max(rough_peak, img[(y * 128 + x) * 4 + 1]);
            smooth_peak = glm_max(smooth_peak, img[(y * 128 + x + 28) * 4 + 1]);
        }
    }

    for (y = 24; y < 40; y ++) {
        for (x = 42; x < 58; x ++) {
            rough_bright += img[(y * 128 + x) * 4 + 1] * 2 > rough_peak;
            smooth_bright += img[(y * 128 + x + 28) * 4 + 1] * 2 > smooth_peak;
        }
    }

    test_assert(rough_peak > 20);
    test_assert(smooth_peak > rough_peak + 40);
    test_assert(smooth_bright * 4 < rough_bright);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_orm_metallic_blue(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *dielectric_file = flecs_asprintf("%s/orm_dielectric.png", tmp);
    char *metal_file = flecs_asprintf("%s/orm_metal.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i, c;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(dielectric_file, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 2] = 255;
    }

    test_assert(stbi_write_png(metal_file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t dielectric_tex = ecs_new(world);
    ecs_set(world, dielectric_tex, TexturePath, {.path = dielectric_file});
    ecs_entity_t metal_tex = ecs_new(world);
    ecs_set(world, metal_tex, TexturePath, {.path = metal_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {
        .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {-4, 0, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {60, 200, 60});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_entity_t dielectric = ecs_new(world);
    ecs_set(world, dielectric, Position, {0, 0, 0});
    ecs_set(world, dielectric, Quad, {3, 3});
    ecs_set(world, dielectric, Rgb, {60, 200, 60});
    ecs_set(world, dielectric, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, dielectric, PbrTextures, {.orm = dielectric_tex});
    ecs_entity_t metal = ecs_new(world);
    ecs_set(world, metal, Position, {4, 0, 0});
    ecs_set(world, metal, Quad, {3, 3});
    ecs_set(world, metal, Rgb, {60, 200, 60});
    ecs_set(world, metal, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, metal, PbrTextures, {.orm = metal_tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 128, .height = 64, .write_to_file = "orm_blue.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/orm_blue.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *reference = &img[(32 * 128 + 27) * 4];
    unsigned char *diffuse = &img[(32 * 128 + 64) * 4];
    unsigned char *metallic = &img[(32 * 128 + 101) * 4];
    test_assert(reference[1] > 150);
    test_assert(reference[1] > reference[0] + 60);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(diffuse[c] - reference[c]) < 4);
    }

    test_assert(diffuse[1] > metallic[1] + 30);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(metal_file);
    ecs_os_free(dielectric_file);
    ecs_fini(world);
}

void Texture_gpu_orm_occlusion_indirect_only(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/orm_occluded.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i, c;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {
        .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    ecs_entity_t occluded = ecs_new(world);
    ecs_set(world, occluded, Position, {-2, 0, 0});
    ecs_set(world, occluded, Quad, {3, 3});
    ecs_set(world, occluded, Rgb, {200, 200, 200});
    ecs_set(world, occluded, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, occluded, PbrTextures, {.orm = tex});
    ecs_entity_t open = ecs_new(world);
    ecs_set(world, open, Position, {2, 0, 0});
    ecs_set(world, open, Quad, {3, 3});
    ecs_set(world, open, Rgb, {200, 200, 200});
    ecs_set(world, open, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "orm_red.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/orm_red.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 22) * 4];
    unsigned char *right = &img[(16 * 64 + 41) * 4];
    test_assert(right[1] > 100);
    for (c = 0; c < 3; c ++) {
        test_assert(left[c] < 4);
    }

    stbi_image_free(img);

    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    left = &img[(16 * 64 + 22) * 4];
    right = &img[(16 * 64 + 41) * 4];
    test_assert(right[1] > 100);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(left[c] - right[c]) < 4);
    }

    stbi_image_free(img);

    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    ecs_set(world, occluded, Emissive, {.strength = 1, .color = {255, 120, 40}});
    ecs_set(world, open, Emissive, {.strength = 1, .color = {255, 120, 40}});
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    left = &img[(16 * 64 + 22) * 4];
    right = &img[(16 * 64 + 41) * 4];
    test_assert(right[0] > 200);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(left[c] - right[c]) < 4);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_orm_missing_layer_unchanged(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *white_file = flecs_asprintf("%s/orm_white.png", tmp);
    char *occluded_file = flecs_asprintf("%s/orm_black_red.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    memset(px, 255, sizeof(px));
    test_assert(stbi_write_png(white_file, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
    }

    test_assert(stbi_write_png(occluded_file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t white_tex = ecs_new(world);
    ecs_set(world, white_tex, TexturePath, {.path = white_file});
    ecs_entity_t occluded_tex = ecs_new(world);
    ecs_set(world, occluded_tex, TexturePath, {.path = occluded_file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {-1, 0.5f, 2});
    ecs_set(world, light, PointLight, {.intensity = 3 * GLM_PIf, .range = 10});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Position, {-1.5f, 0, 0});
    ecs_set(world, quad, Quad, {2.5f, 2.5f});
    ecs_set(world, quad, Rgb, {200, 140, 90});
    ecs_set(world, quad, PbrMaterial, {.metallic = 0.5f, .roughness = 0.4f});
    ecs_set(world, quad, Emissive, {.strength = 0.2f, .color = {40, 80, 255}});
    ecs_entity_t probe = ecs_new(world);
    ecs_set(world, probe, Position, {1.5f, 0, 0});
    ecs_set(world, probe, Quad, {2.5f, 2.5f});
    ecs_set(world, probe, Rgb, {200, 200, 200});
    ecs_set(world, probe, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "orm_missing.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/orm_missing.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *before = ecs_os_malloc(64 * 32 * 4);
    ecs_os_memcpy(before, img, 64 * 32 * 4);
    stbi_image_free(img);

    ecs_set(world, quad, PbrTextures, {.orm = white_tex});
    ecs_set(world, probe, PbrTextures, {.orm = occluded_tex});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *probe_before = &before[(16 * 64 + 39) * 4];
    unsigned char *probe_after = &img[(16 * 64 + 39) * 4];
    test_assert(probe_before[1] > probe_after[1] + 10);
    int32_t x, y, c;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            for (c = 0; c < 3; c ++) {
                test_int(img[(y * 64 + x) * 4 + c], before[(y * 64 + x) * 4 + c]);
            }
        }
    }

    stbi_image_free(img);
    ecs_os_free(before);
    ecs_os_free(path);
    ecs_os_free(occluded_file);
    ecs_os_free(white_file);
    ecs_fini(world);
}

void Texture_gpu_orm_back_face_plain(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/orm_back.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i, c;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 25;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {
        .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Position, {-4, 0, 0});
    ecs_set(world, front, Quad, {3, 3});
    ecs_set(world, front, Rgb, {200, 200, 200});
    ecs_set(world, front, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, front, PbrTextures, {.orm = tex});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {0, 0, 0});
    ecs_set(world, back, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, back, Quad, {3, 3});
    ecs_set(world, back, Rgb, {200, 200, 200});
    ecs_set(world, back, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, back, PbrTextures, {.orm = tex});
    ecs_set(world, back, BackFaceMode, {BackFacePlain});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {4, 0, 0});
    ecs_set(world, plain, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {200, 200, 200});
    ecs_set(world, plain, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, plain, BackFaceMode, {BackFacePlain});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 128, .height = 64, .write_to_file = "orm_back.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/orm_back.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *textured = &img[(32 * 128 + 27) * 4];
    unsigned char *backside = &img[(32 * 128 + 64) * 4];
    unsigned char *reference = &img[(32 * 128 + 101) * 4];
    test_assert(reference[1] > 60);
    test_assert(textured[1] < 4);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(backside[c] - reference[c]) < 4);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}
