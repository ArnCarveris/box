/**
 * @file FileWatch.c
 * @brief Tests for the file watch of box.app: its settings, its status, the passes of the watch
 * thread and the textures it loads again when their file changes.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <box.h>
#include <bake_test.h>
#include <watch.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../../../src/posix.h"
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

/** Most files a test kind watches */
#define LOG_FILES (32)

/** Owners of a test kind, one per file, and how often the watch told each about a change */
typedef struct changed_log_t {
    ecs_entity_t owners[LOG_FILES]; /**< Owner of each file */
    char *paths[LOG_FILES];         /**< Path of each file */
    int32_t calls[LOG_FILES];       /**< Changes each owner was told */
    int32_t count;                  /**< Files watched */
} changed_log_t;

/** Count a change an owner of the test kind was told.
 *
 * @param world The world
 * @param ctx The log
 * @param owner The owner
 */
static void on_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    changed_log_t *log = ctx;
    int32_t i;
    (void)world;
    for (i = 0; i < LOG_FILES; i ++) {
        if (log->owners[i] == owner) {
            log->calls[i] ++;
        }
    }
}

/** Write a text file and give it a modification time.
 *
 * @param path The file
 * @param text Content of the file
 * @param mtime Seconds of the modification time
 */
static void write_text(
    const char *path,
    const char *text,
    int64_t mtime)
{
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs(text, f);
    fclose(f);
    struct timespec times[2] = {{ .tv_sec = (time_t)mtime }, { .tv_sec = (time_t)mtime }};
    test_int(utimensat(AT_FDCWD, path, times, 0), 0);
}

/** Replace a text file at once, by renaming a new file over it, so a pass on the watch thread
 * sees either the old or the new content and never a file half written.
 *
 * @param path The file
 * @param text Content of the file
 * @param mtime Seconds of the modification time
 */
static void replace_text(
    const char *path,
    const char *text,
    int64_t mtime)
{
    char *tmp = flecs_asprintf("%s.new", path);
    write_text(tmp, text, mtime);
    test_int(rename(tmp, path), 0);
    ecs_os_free(tmp);
}

/** Register a test kind and watch count text files, each by an owner of its own.
 *
 * @param world The world
 * @param log The log, receives the owners and the paths
 * @param count Number of files
 * @return Id of the kind
 */
static int32_t watch_files(
    ecs_world_t *world,
    changed_log_t *log,
    int32_t count)
{
    int32_t i, kind = box_file_watch_kind(world, "test", on_changed, log);
    test_assert(kind != 0);
    for (i = 0; i < count; i ++) {
        char name[32];
        box_file_stamp_t stamp;
        snprintf(name, sizeof(name), "watch_%d.txt", i);
        log->paths[i] = tmp_file(world, name);
        write_text(log->paths[i], name, 1000000000);
        box_file_stamp(log->paths[i], &stamp);
        log->owners[i] = ecs_new(world);
        box_file_watch_add(world, kind, log->owners[i], log->paths[i], &stamp);
    }

    log->count = count;

    return kind;
}

/** Free the paths of a log.
 *
 * @param log The log
 */
static void log_fini(
    changed_log_t *log)
{
    int32_t i;
    for (i = 0; i < log->count; i ++) {
        ecs_os_free(log->paths[i]);
    }
}

/** Sum the changes the owners of a log were told.
 *
 * @param log The log
 * @return Changes told to any owner
 */
static int32_t log_calls(
    const changed_log_t *log)
{
    int32_t i, result = 0;
    for (i = 0; i < LOG_FILES; i ++) {
        result += log->calls[i];
    }

    return result;
}

/** Run frames of a fixed delta until the file watch completes a pass.
 *
 * @param world The world
 * @param delta Seconds of each frame
 * @param max Most frames to run
 * @return Frames run, more than max when no pass completed
 */
static int32_t run_pass(
    ecs_world_t *world,
    ecs_ftime_t delta,
    int32_t max)
{
    int64_t polls = ecs_singleton_get(world, FileWatchStatus)->polls;
    int32_t frames = 0;
    while (ecs_singleton_get(world, FileWatchStatus)->polls == polls && frames <= max) {
        ecs_progress(world, delta);
        frames ++;
    }

    return frames;
}

void FileWatch_pass_every_interval(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    watch_files(world, &log, 30);

    ecs_progress(world, 1.0f / 60.0f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 0);
    ecs_progress(world, 1.0f / 60.0f);
    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, 1);
    test_int(status->files, 30);

    int32_t i;
    for (i = 2; i <= 4; i ++) {
        int32_t frames = run_pass(world, 1.0f / 60.0f, 30);
        test_assert(frames >= 15 && frames <= 16);
        test_int(ecs_singleton_get(world, FileWatchStatus)->polls, i);
    }

    status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->changes, 0);
    test_assert(status->last == NULL);
    test_int(log_calls(&log), 0);

    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_change_found_within_interval(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    watch_files(world, &log, 30);
    test_assert(run_pass(world, 0.025f, 20) <= 2);

    const int32_t changed[] = {0, 17, 29, 5};
    int32_t i;
    for (i = 0; i < 4; i ++) {
        int32_t index = changed[i], frames = 0;
        write_text(log.paths[index], "changed content", 1000000100 + i);
        while (!log.calls[index] && frames < 30) {
            ecs_progress(world, 0.025f);
            frames ++;
        }

        test_assert(frames <= 11);
        test_int(log.calls[index], 1);
        test_int(log_calls(&log), i + 1);
        const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
        test_int(status->changes, i + 1);
        test_str(status->last, log.paths[index]);
    }

    run_pass(world, 0.025f, 20);
    run_pass(world, 0.025f, 20);
    test_int(log_calls(&log), 4);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 4);

    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_interval_zero_checks_every_file(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0);
    changed_log_t log = {0};
    watch_files(world, &log, 5);

    int32_t i;
    for (i = 1; i <= 3; i ++) {
        ecs_progress(world, 0.001f);
        test_int(ecs_singleton_get(world, FileWatchStatus)->polls, i);
    }

    write_text(log.paths[3], "changed content", 1000000100);
    ecs_progress(world, 0.001f);
    test_int(log.calls[3], 1);
    test_int(log_calls(&log), 1);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);

    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_long_frame_one_pass(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    watch_files(world, &log, 8);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 2.0f);
        test_int(ecs_singleton_get(world, FileWatchStatus)->polls, i);
    }

    replace_text(log.paths[6], "changed content", 1000000100);
    ecs_progress(world, 2.0f);
    ecs_progress(world, 2.0f);
    test_int(log.calls[6], 1);
    test_int(log_calls(&log), 1);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 5);

    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_interval_change_ends_pass(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    watch_files(world, &log, 4);
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 0);

    ecs_singleton_set(world, FileWatch, {.interval = 0});
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 2);
    write_text(log.paths[1], "changed content", 1000000100);
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 3);
    test_int(log.calls[1], 1);

    ecs_singleton_set(world, FileWatch, {.interval = 0.25f});
    ecs_progress(world, 0.01f);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    write_text(log.paths[2], "changed content", 1000000100);
    int32_t i;
    for (i = 0; i < 30; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 3);
    test_int(log.calls[2], 0);

    ecs_singleton_set(world, FileWatch, {.interval = 0.25f});
    test_assert(run_pass(world, 0.01f, 30) <= 27);
    test_int(log.calls[2], 1);
    test_int(log_calls(&log), 2);

    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_stop_watching_during_pass(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    int32_t kind = watch_files(world, &log, 10);

    ecs_progress(world, 0.025f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 0);
    replace_text(log.paths[0], "changed content", 1000000100);
    box_file_watch_clear(world, kind, log.owners[0]);
    box_file_watch_clear(world, kind, log.owners[4]);
    char *added = tmp_file(world, "added.txt");
    box_file_stamp_t stamp;
    write_text(added, "added", 1000000000);
    box_file_stamp(added, &stamp);
    log.owners[10] = ecs_new(world);
    box_file_watch_add(world, kind, log.owners[10], added, &stamp);

    ecs_progress(world, 0.025f);
    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, 1);
    test_int(status->files, 9);
    test_int(status->changes, 0);

    write_text(added, "added again", 1000000100);
    test_assert(run_pass(world, 0.025f, 20) <= 11);
    status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, 2);
    test_int(status->changes, 1);
    test_str(status->last, added);
    test_int(log.calls[10], 1);
    test_int(log_calls(&log), 1);

    ecs_os_free(added);
    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_stale_registration_starts_pass(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    int32_t kind = watch_files(world, &log, 1);
    test_assert(run_pass(world, 0.01f, 5) <= 2);
    ecs_progress(world, 0.01f);
    int64_t polls = ecs_singleton_get(world, FileWatchStatus)->polls;

    box_file_stamp_t stale;
    box_file_stamp(log.paths[0], &stale);
    stale.sec -= 10;
    log.owners[1] = ecs_new(world);
    box_file_watch_add(world, kind, log.owners[1], log.paths[0], &stale);
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, polls);
    ecs_progress(world, 0.01f);
    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, polls + 1);
    test_int(status->changes, 0);
    test_int(status->files, 1);
    test_int(log.calls[0], 0);
    test_int(log.calls[1], 1);

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_int(log.calls[1], 1);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, polls + 1);

    char *other = tmp_file(world, "other.txt");
    box_file_stamp_t stamp;
    write_text(other, "other", 1000000000);
    box_file_stamp(other, &stamp);
    log.owners[2] = ecs_new(world);
    log.owners[3] = ecs_new(world);
    box_file_watch_add(world, kind, log.owners[2], other, &stamp);
    box_file_watch_add(world, kind, log.owners[3], other, &stale);
    box_file_watch_clear(world, kind, log.owners[2]);
    box_file_watch_clear(world, kind, log.owners[3]);
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, polls + 1);
    test_int(ecs_singleton_get(world, FileWatchStatus)->files, 1);
    test_int(log.calls[2], 0);
    test_int(log.calls[3], 0);

    ecs_os_free(other);
    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_restored_while_queued_reloads_once(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    changed_log_t log = {0};
    int32_t kind = watch_files(world, &log, 1);
    test_assert(run_pass(world, 0.01f, 5) <= 2);

    box_file_stamp_t stale;
    box_file_stamp(log.paths[0], &stale);
    stale.sec -= 10;
    log.owners[1] = ecs_new(world);
    box_file_watch_add(world, kind, log.owners[1], log.paths[0], &stale);
    test_int(unlink(log.paths[0]), 0);
    write_text(log.paths[0], "restored content", 1000000100);
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);

    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->changes, 1);
    test_str(status->last, log.paths[0]);
    test_int(log.calls[0], 1);
    test_int(log.calls[1], 1);

    run_pass(world, 0.05f, 10);
    run_pass(world, 0.05f, 10);
    test_int(log.calls[0], 1);
    test_int(log.calls[1], 1);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);

    log_fini(&log);
    ecs_fini(world);
}

void FileWatch_restored_between_passes_reloads_once(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(0.25f);
    char *file = tmp_file(world, "restored.png");
    write_png(file, 8, 4, 1000000000);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    test_int(wait_texture(world, tex)->revision, 1);
    test_assert(run_pass(world, 0.05f, 10) <= 6);
    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    int64_t polls = status->polls;
    test_int(status->files, 1);
    test_int(status->changes, 0);

    test_int(unlink(file), 0);
    ecs_progress(world, 0.05f);
    ecs_progress(world, 0.05f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, polls);
    write_png(file, 2, 2, 1000000100);
    test_assert(run_pass(world, 0.05f, 10) <= 4);
    status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, polls + 1);
    test_int(status->changes, 1);
    test_str(status->last, file);

    const TextureStatus *tex_status = wait_texture(world, tex);
    test_int(tex_status->state, TextureReady);
    test_int(tex_status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 2);

    run_pass(world, 0.05f, 10);
    run_pass(world, 0.05f, 10);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    test_int(ecs_singleton_get(world, FileWatchStatus)->changes, 1);

    ecs_os_free(file);
    ecs_fini(world);
}

/** Calls of thread_new_fails */
static int32_t thread_new_calls;

/** Fail to create a thread, like an OS that is out of threads.
 *
 * The test replaces the API in place, ecs_os_set_api does nothing once the OS API is
 * initialized. It first runs a frame with the watch off, which starts the render thread.
 *
 * So only the watch thread fails to start. A poll that does not wait then sees the pass stuck
 * on a thread that never ran, a headless poll would wait for it forever.
 *
 * @param callback The thread function
 * @param param Passed to callback
 * @return 0, no thread
 */
static ecs_os_thread_t thread_new_fails(
    ecs_os_thread_callback_t callback,
    void *param)
{
    (void)callback;
    (void)param;
    thread_new_calls ++;

    return 0;
}

void FileWatch_thread_fails_inline_passes(void) {
    // engine/file_watch
    ecs_world_t *world = world_new(-1);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);
    changed_log_t log = {0};
    watch_files(world, &log, 4);

    ecs_progress(world, 0.05f);
    test_int(ecs_singleton_get(world, FileWatchStatus)->polls, 0);
    ecs_singleton_set(world, FileWatch, {.interval = 0.25f});

    ecs_os_api_t saved = ecs_os_get_api();
    ecs_os_api_t api = saved;
    thread_new_calls = 0;
    api.thread_new_ = thread_new_fails;
    ecs_os_api = api;
    test_assert(ecs_os_get_api().thread_new_ == thread_new_fails);
    test_assert(ecs_os_has_threading());

    int32_t i;
    for (i = 1; i <= 3; i ++) {
        test_assert(run_pass(world, 0.05f, 10) <= 6);
        test_int(ecs_singleton_get(world, FileWatchStatus)->polls, i);
    }

    write_text(log.paths[2], "changed content", 1000000100);
    test_assert(run_pass(world, 0.05f, 10) <= 6);
    const FileWatchStatus *status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, 4);
    test_int(status->changes, 1);
    test_str(status->last, log.paths[2]);
    test_int(log.calls[2], 1);
    test_int(log_calls(&log), 1);

    app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    write_text(log.paths[0], "changed content", 1000000100);
    test_assert(run_pass(world, 0.05f, 10) <= 6);
    status = ecs_singleton_get(world, FileWatchStatus);
    test_int(status->polls, 5);
    test_int(status->changes, 2);
    test_int(log.calls[0], 1);
    test_int(log_calls(&log), 2);
    test_int(thread_new_calls, 1);

    ecs_os_api = saved;
    log_fini(&log);
    ecs_fini(world);
}
