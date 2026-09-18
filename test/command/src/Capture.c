/**
 * @file Capture.c
 * @brief Tests for the capture command and the PNG it writes to the session resources.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"
#include "../../../src/surface/surface_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Capture_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCommand);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Capture_teardown(void)
{
    ecs_fini(w);
}

void Capture_prints_path_of_the_active_camera(void)
{
    // command/capture
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    bool error = true;
    char *out = box_command_run(w, session, "capture", &error);
    test_bool(error, false);
    test_str(out, "capture_0_0.png\n");
    ecs_os_free(out);

    const Capture *capture = ecs_get(w, cam, Capture);
    test_assert(capture != NULL);
    char *expect = flecs_asprintf("%s/resources/capture_0_0.png",
        ecs_get(w, session, Terminal)->session_dir);
    test_str(capture->path, expect);
    ecs_os_free(expect);
}

void Capture_path_uses_the_frame_number(void)
{
    // command/capture
    App *app = ecs_singleton_ensure(w, App);
    app->frame = 7;
    ecs_singleton_modified(w, App);

    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    char *out = box_command_run(w, session, "capture", NULL);
    test_str(out, "capture_7_0.png\n");
    ecs_os_free(out);
}

void Capture_named_camera(void)
{
    // command/capture
    ecs_entity_t a = ecs_entity(w, { .name = "cam0" });
    ecs_set(w, a, Position, {0, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(w, { .name = "cam1" });
    ecs_set(w, b, Position, {0, 0, 5});
    ecs_set(w, b, Camera, {.fov = 60});
    char *out = box_command_run(w, session, "capture cam1", NULL);
    test_str(out, "capture_0_0.png\n");
    ecs_os_free(out);
    test_assert(!ecs_has(w, a, Capture));
    test_assert(ecs_has(w, b, Capture));
}

void Capture_multiple_cameras(void)
{
    // command/capture
    ecs_entity_t a = ecs_entity(w, { .name = "cam0" });
    ecs_set(w, a, Position, {0, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(w, { .name = "cam1" });
    ecs_set(w, b, Position, {0, 0, 5});
    ecs_set(w, b, Camera, {.fov = 60});
    char *out = box_command_run(w, session, "capture cam0,cam1", NULL);
    test_str(out, "capture_0_0.png\ncapture_0_1.png\n");
    ecs_os_free(out);

    const Capture *first = ecs_get(w, a, Capture);
    const Capture *second = ecs_get(w, b, Capture);
    test_assert(first != NULL);
    test_assert(second != NULL);
    test_assert(strstr(first->path, "/resources/capture_0_0.png") != NULL);
    test_assert(strstr(second->path, "/resources/capture_0_1.png") != NULL);
}

void Capture_camera_relative_to_cwd(void)
{
    // command/capture
    ecs_entity_t cam = ecs_entity(w, { .name = "scene.cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "capture cam", NULL);
    test_str(out, "capture_0_0.png\n");
    ecs_os_free(out);
    test_assert(ecs_has(w, cam, Capture));
}

void Capture_unknown_camera(void)
{
    // command/capture
    bool error = false;
    char *out = box_command_run(w, session, "capture nope", &error);
    test_bool(error, true);
    test_str(out, "unknown camera 'nope'");
    ecs_os_free(out);
}

void Capture_entity_without_camera(void)
{
    // command/capture
    ecs_entity(w, { .name = "thing" });
    bool error = false;
    char *out = box_command_run(w, session, "capture thing", &error);
    test_bool(error, true);
    test_str(out, "'thing' is not a camera");
    ecs_os_free(out);
}

void Capture_without_active_camera(void)
{
    // command/capture
    bool error = false;
    char *out = box_command_run(w, session, "capture", &error);
    test_bool(error, true);
    test_str(out, "no active camera");
    ecs_os_free(out);
}

void Capture_second_request_replaces_the_first(void)
{
    // command/capture
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    char *out = box_command_run(w, session, "capture", NULL);
    ecs_os_free(out);

    App *app = ecs_singleton_ensure(w, App);
    app->frame = 3;
    ecs_singleton_modified(w, App);

    out = box_command_run(w, session, "capture", NULL);
    test_str(out, "capture_3_0.png\n");
    ecs_os_free(out);

    const Capture *capture = ecs_get(w, cam, Capture);
    test_assert(capture != NULL);
    test_assert(strstr(capture->path, "capture_3_0.png") != NULL);
}

void Capture_headless_run_writes_the_png(void)
{
    // command/capture
    App *app = ecs_singleton_ensure(w, App);
    app->headless = true;
    ecs_singleton_modified(w, App);

    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 64;
    s->height = 32;
    ecs_singleton_modified(w, Surface);

    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});

    const SurfaceImpl *impl = ecs_get(w, ecs_id(Surface), SurfaceImpl);
    if (impl && impl->renderer) {
        char *out = box_command_run(w, session, "capture", NULL);
        char *file = ecs_os_strdup(out);
        char *end = strchr(file, '\n');
        end[0] = '\0';
        char *path = flecs_asprintf("%s/resources/%s",
            ecs_get(w, session, Terminal)->session_dir, file);
        bool written = false;
        int32_t i;
        for (i = 0; i < 200 && !written; i ++) {
            ecs_progress(w, 0);
            FILE *f = fopen(path, "rb");
            if (f) {
                unsigned char magic[4] = {0};
                size_t got = fread(magic, 1, 4, f);
                fclose(f);
                written = got == 4 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
            }
        }

        test_bool(written, true);
        test_assert(!ecs_has(w, cam, Capture));
        ecs_os_free(path);
        ecs_os_free(file);
        ecs_os_free(out);
    }
}
