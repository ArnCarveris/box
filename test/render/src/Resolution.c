/**
 * @file Resolution.c
 * @brief Tests for Surface.resolution_scale: the sizes it reports and the size a view renders at.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Resolution_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Resolution_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Resolution_a_surface_view_takes_the_scaled_size(void)
{
    // ore_engine/resolution_scale
    packet.scaled_width = 480;
    packet.scaled_height = 270;
    box_render_extract(w, &packet);
    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->width, 480);
    test_int(v->height, 270);
    test_int(packet.width, 960);
    test_int(packet.height, 540);
}

void Resolution_an_unscaled_surface_view_keeps_the_window_size(void)
{
    // ore_engine/resolution_scale
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->width, 960);
    test_int(v->height, 540);
}

void Resolution_a_render_target_keeps_its_own_size(void)
{
    // ore_engine/resolution_scale
    packet.scaled_width = 480;
    packet.scaled_height = 270;
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {512, 256});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -4});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->target = target;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->width, 512);
    test_int(v->height, 256);
}

void Resolution_the_surface_reports_the_size_it_renders_at(void)
{
    // ore_engine/resolution_scale
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    app->frames = 2;
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 320;
    s->height = 200;
    s->resolution_scale = 2;
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    const Surface *after = ecs_singleton_get(world, Surface);
    test_int(after->actual_width, 320);
    test_int(after->actual_height, 200);
    test_int(after->scaled_width, 160);
    test_int(after->scaled_height, 100);
    ecs_fini(world);
}

void Resolution_a_scale_of_one_reports_the_window_size(void)
{
    // ore_engine/resolution_scale
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    app->frames = 2;
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 320;
    s->height = 200;
    s->resolution_scale = 1;
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    const Surface *after = ecs_singleton_get(world, Surface);
    test_int(after->scaled_width, 320);
    test_int(after->scaled_height, 200);
    ecs_fini(world);
}

void Resolution_a_scale_below_one_is_the_full_resolution(void)
{
    // ore_engine/resolution_scale
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    app->frames = 2;
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 320;
    s->height = 200;
    s->resolution_scale = 0.5f;
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    const Surface *after = ecs_singleton_get(world, Surface);
    test_int(after->scaled_width, 320);
    test_int(after->scaled_height, 200);
    ecs_fini(world);
}

void Resolution_the_capture_keeps_the_window_size(void)
{
    // ore_engine/resolution_scale
    const char *path = "/tmp/box_test_resolution.png";
    remove(path);
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    app->frames = 3;
    Surface *s = ecs_singleton_ensure(world, Surface);
    s->width = 128;
    s->height = 64;
    s->resolution_scale = 2;
    s->write_to_file = ecs_os_strdup(path);
    ecs_singleton_modified(world, Surface);
    test_int(box_run(world), 0);
    ecs_fini(world);

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    unsigned char header[24] = {0};
    test_int((int)fread(header, 1, 24, f), 24);
    fclose(f);
    remove(path);
    uint32_t width = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16) |
        ((uint32_t)header[18] << 8) | (uint32_t)header[19];
    uint32_t height = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16) |
        ((uint32_t)header[22] << 8) | (uint32_t)header[23];
    test_int(width, 128);
    test_int(height, 64);
}
