/**
 * @file StartupOverlay.c
 * @brief The startup overlay draws its panel in the middle of the surface over the game while a
 * blocking task is in progress, and the game alone once it closed. The overlay loads the copy of
 * its script in test/assets/startup.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width of the surface of the tests in points */
#define STARTUP_W (640)

/** Height of the surface of the tests in points */
#define STARTUP_H (360)

/** Frames a test waits at most for the overlay to close, while its fonts load */
#define STARTUP_WAIT (2000)

/** Create a headless world whose game view clears to blue, a surface that is written to a file
 * and an overlay that may open and loads the script of the tests.
 *
 * @param file Name of the PNG in the temporary folder
 * @return The world
 */
static ecs_world_t* startup_world(
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_startup_script(world, "test/assets/startup/startup.flecs");
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    StartupOverlay *o = ecs_singleton_get_mut(world, StartupOverlay);
    o->enabled = true;
    ecs_singleton_modified(world, StartupOverlay);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = STARTUP_W, .height = STARTUP_H,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return world;
}

/** Render the frame and load the PNG it wrote.
 *
 * @param world The world
 * @param file Name of the PNG in the temporary folder
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* startup_render(
    ecs_world_t *world,
    const char *file)
{
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, STARTUP_W);
    test_int(h, STARTUP_H);

    return px;
}

/** Check whether a pixel is the blue the game view clears to.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row from the top
 * @return Whether every channel is within 3 of blue
 */
static bool startup_blue(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    const unsigned char *p = &px[(y * STARTUP_W + x) * 4];

    return p[0] < 4 && p[1] < 4 && p[2] > 251;
}

void StartupOverlay_draws_panel(void) {
    const char *file = "startup_panel.png";
    ecs_world_t *world = startup_world(file);
    box_task_id_t block = box_task_start(world, "Blocking work", true, 2);
    box_task_advance(world, block, 1);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, StartupOverlay)->open, true);
    unsigned char *px = startup_render(world, file);
    test_bool(startup_blue(px, 4, 4), true);
    test_bool(startup_blue(px, STARTUP_W - 5, STARTUP_H - 5), true);
    test_bool(startup_blue(px, STARTUP_W / 2, STARTUP_H / 2), false);
    stbi_image_free(px);

    box_task_end(world, block);
    for (i = 0; i < STARTUP_WAIT && ecs_singleton_get(world, StartupOverlay)->open; i ++) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, StartupOverlay)->open, false);
    px = startup_render(world, file);
    test_bool(startup_blue(px, STARTUP_W / 2, STARTUP_H / 2), true);
    stbi_image_free(px);

    ecs_fini(world);
}
