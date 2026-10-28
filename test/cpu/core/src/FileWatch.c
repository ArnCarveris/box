/**
 * @file FileWatch.c
 * @brief Tests for the file watch of box.app: its settings, its status and the textures it
 * loads again when their file changes.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Create a headless world, which has a temporary folder and waits for its texture loads.
 *
 * @param interval FileWatch.interval
 * @return The world
 */
static ecs_world_t* world_new(
    float interval)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, FileWatch, {.interval = interval});

    return world;
}

/** Get a file in the temporary folder of a world.
 *
 * @param world The world
 * @param name Name of the file
 * @return The path, freed by the caller
 */
static char* tmp_file(
    ecs_world_t *world,
    const char *name)
{
    return flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
}

/** Write a grey PNG and give it a modification time, so its stamp differs for sure from the
 * stamp of any earlier content.
 *
 * @param path The file
 * @param width Width of the image
 * @param height Height of the image
 * @param mtime Seconds of the modification time
 */
static void write_png(
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
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    return ecs_get(world, tex, TextureStatus);
}

void FileWatch_defaults(void) {
    // engine/file_watch
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const FileWatch *watch = ecs_singleton_get(world, FileWatch);
    test_assert(watch != NULL);
    test_flt(watch->interval, BOX_FILE_WATCH_INTERVAL);
    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    test_assert(status != NULL);
    test_int(status->files, 0);
    test_int(status->polls, 0);
    test_int(status->changes, 0);
    test_assert(status->last == NULL);

    ecs_fini(world);
}

void FileWatch_polls_every_frame(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0);

    int32_t i;
    for (i = 1; i <= 5; i ++) {
        ecs_progress(world, 0);
        test_int(ecs_singleton_get(world, FileWatchStatus)->polls, i);
    }

    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 0);

    ecs_fini(world);
}

void FileWatch_disabled(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(-1);
    char *file = tmp_file(world, "disabled.png");
    write_png(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(wait_texture(world, tex)->revision, 1);

    write_png(file, 2, 2, 1000000100);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    const FileWatchStatus *watch = ecs_singleton_get(world, FileWatchStatus);
    test_int(watch->polls, 0);
    test_int(watch->changes, 0);
    test_assert(watch->last == NULL);

    ecs_os_free(file);
    ecs_fini(world);
}

void FileWatch_shared_file_counts_once(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0);
    char *file = tmp_file(world, "shared.png");
    write_png(file, 8, 4, 1000000000);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, TexturePath, {.path = file});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, TexturePath, {.path = file, .width = 16});
    wait_texture(world, a);
    wait_texture(world, b);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FileWatchStatus)->files, 1);
    test_int(ecs_get(world, a, TextureStatus)->revision, 1);
    test_int(ecs_get(world, b, TextureStatus)->revision, 1);

    write_png(file, 2, 2, 1000000100);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);
    wait_texture(world, a);
    wait_texture(world, b);

    test_int(ecs_get(world, a, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, a, TextureStatus)->revision, 2);
    test_int(ecs_get(world, a, Texture)->width, 2);
    test_int(ecs_get(world, b, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, b, TextureStatus)->revision, 2);
    test_int(ecs_get(world, b, Texture)->width, 16);
    test_int(ecs_get(world, b, Texture)->height, 16);
    ecs_progress(world, 0);
    const FileWatchStatus *watch = ecs_singleton_get(world, FileWatchStatus);
    test_int(watch->files, 1);
    test_int(watch->changes, 1);

    ecs_os_free(file);
    ecs_fini(world);
}

void FileWatch_new_path_stops_watching(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0);
    char *first = tmp_file(world, "first.png");
    char *second = tmp_file(world, "second.png");
    write_png(first, 8, 4, 1000000000);
    write_png(second, 4, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = first});
    test_int(wait_texture(world, tex)->revision, 1);
    ecs_set(world, tex, TexturePath, {.path = second});
    test_int(wait_texture(world, tex)->revision, 2);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FileWatchStatus)->files, 1);

    write_png(first, 2, 2, 1000000100);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 4);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 0);

    write_png(second, 6, 2, 1000000100);
    ecs_progress(world, 0);
    const TextureStatus *status = wait_texture(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 3);
    test_int(ecs_get(world, tex, Texture)->width, 6);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);
    test_str(ecs_singleton_get(world, FileWatchStatus)->last, second);

    ecs_os_free(first);
    ecs_os_free(second);
    ecs_fini(world);
}

void FileWatch_remove_stops_watching(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0);
    char *file = tmp_file(world, "removed.png");
    write_png(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(wait_texture(world, tex)->revision, 1);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FileWatchStatus)->files, 1);

    ecs_remove(world, tex, TexturePath);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FileWatchStatus)->files, 0);

    write_png(file, 2, 2, 1000000100);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const FileWatchStatus *watch = ecs_singleton_get(world, FileWatchStatus);
    test_int(watch->files, 0);
    test_int(watch->changes, 0);
    test_assert(!ecs_has(world, tex, TextureStatus));
    test_assert(!ecs_has(world, tex, Texture));

    ecs_os_free(file);
    ecs_fini(world);
}

void FileWatch_change_status(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0);
    char *file = tmp_file(world, "status.png");
    write_png(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(wait_texture(world, tex)->revision, 1);
    ecs_progress(world, 0);
    const FileWatchStatus *watch = ecs_singleton_get(world, FileWatchStatus);
    int64_t polls = watch->polls;
    test_assert(polls > 0);
    test_int(watch->files, 1);
    test_int(watch->changes, 0);
    test_assert(watch->last == NULL);

    write_png(file, 2, 2, 1000000100);
    ecs_progress(world, 0);
    watch = ecs_singleton_get(world, FileWatchStatus);
    test_int(watch->polls, polls + 1);
    test_int(watch->changes, 1);
    test_str(watch->last, file);
    test_assert(ecs_get(world, tex, TextureStatus)->revision == 2 ||
        ecs_get(world, tex, TextureStatus)->state == TextureLoading);

    const TextureStatus *status = wait_texture(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 2);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);

    ecs_os_free(file);
    ecs_fini(world);
}
