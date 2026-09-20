/**
 * @file Surface.c
 * @brief Tests for headless surfaces and PNG capture.
 */

#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <stdio.h>

static ecs_world_t* surface_world(
    int32_t frames)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    app->frames = frames;

    return world;
}

void Surface_headless_surface_reports_size(void)
{
    // engine_core/surface
    ecs_world_t *world = surface_world(2);
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 320;
    s->height = 200;
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    const Surface *after = ecs_singleton_get(world, Surface);
    test_int(after->actual_width, 320);
    test_int(after->actual_height, 200);
    test_int(after->point_width, 320);
    test_int(after->point_height, 200);
    ecs_fini(world);
}

void Surface_headless_device_scale_doubles_the_framebuffer(void)
{
    // ore_engine/console_text_blurry
    ecs_world_t *world = surface_world(2);
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 320;
    s->height = 200;
    s->device_scale = 2;
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    const Surface *after = ecs_singleton_get(world, Surface);
    test_int(after->actual_width, 640);
    test_int(after->actual_height, 400);
    test_int(after->scaled_width, 640);
    test_int(after->scaled_height, 400);
    test_int(after->point_width, 320);
    test_int(after->point_height, 200);
    ecs_fini(world);
}

void Surface_headless_surface_writes_png(void)
{
    // engine_core/surface
    const char *path = "/tmp/box_test_surface.png";
    remove(path);
    ecs_world_t *world = surface_world(3);
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 64;
    s->height = 32;
    s->write_to_file = ecs_os_strdup(path);
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    ecs_fini(world);
    struct stat st;
    test_int(stat(path, &st), 0);
    test_assert(st.st_size > 0);
    FILE *f = fopen(path, "rb");
    unsigned char magic[4] = {0};
    test_int((int)fread(magic, 1, 4, f), 4);
    fclose(f);
    test_int(magic[1], 'P');
    test_int(magic[2], 'N');
    test_int(magic[3], 'G');
    remove(path);
}

void Surface_write_nth_adds_frame_suffix(void)
{
    // engine_core/surface
    remove("/tmp/box_test_nth_00000.png");
    remove("/tmp/box_test_nth_00002.png");
    remove("/tmp/box_test_nth_00001.png");
    ecs_world_t *world = surface_world(4);
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 16;
    s->height = 16;
    s->write_to_file = ecs_os_strdup("/tmp/box_test_nth.png");
    s->write_nth = 2;
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    ecs_fini(world);
    struct stat st;
    test_int(stat("/tmp/box_test_nth_00000.png", &st), 0);
    test_int(stat("/tmp/box_test_nth_00002.png", &st), 0);
    test_assert(stat("/tmp/box_test_nth_00001.png", &st) != 0);
    remove("/tmp/box_test_nth_00000.png");
    remove("/tmp/box_test_nth_00002.png");
}
