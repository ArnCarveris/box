/**
 * @file ShadowStartup.c
 * @brief Tests that a spot light tile rendered in a startup frame that could not draw its
 * casters renders again once it can, with the GPU backend.
 *
 * A spot light 6 units up shines straight down with a 35 degree cone onto a white floor; a
 * 2 by 2 slab 3 units up shadows the floor within 2 units of the axis. The camera looks down
 * from 30 units up with a 30 degree field of view, so a floor point x units off the axis is
 * about 4 x pixels off the center of the image, and the slab covers the 4 pixels around it.
 *
 * Natively the backend creates pipelines on worker threads, render pipelines first, so in the
 * first frames of a window the shadow pipelines can exist while the cull pipelines, which pick
 * the casters each tile draws, do not. The cases hold back the cull pipelines the same way, by
 * giving the cull shader code that fails to compile until the frames they test.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the image */
#define STARTUP_SIZE (64)

/** Pixels from the center to a floor point in the shadow of the slab, 1.5 units off the axis */
#define STARTUP_SHADOWED (6)

/** Pixels from the center to a lit floor point, 3 units off the axis */
#define STARTUP_LIT (12)

/** Cull shader that fails to compile */
static const char *startup_broken_cull =
    "fn f() -> f32 { return undefined_name; }\n";

/** Create the scene on the main thread, without ambient light.
 *
 * @return The world
 */
static ecs_world_t* startup_scene(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    ecs_entity_t slab = ecs_new(world);
    ecs_set(world, slab, Position, {0, 3, 0});
    ecs_set(world, slab, Box, {2, 0.1f, 2});
    ecs_set(world, slab, Rgb, {255, 255, 255});

    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 6, 0});
    ecs_set(world, light, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, light, SpotLight, {
        .intensity = 60 * GLM_PIf, .range = 10, .inner_angle = 30, .outer_angle = 35,
        .shadows = true, .shadow_resolution = 512
    });

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 30, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 30, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    ecs_entity_t surface = ecs_entity(world, { .name = "capture" });
    ecs_set(world, surface, Surface, {.width = STARTUP_SIZE, .height = STARTUP_SIZE,
        .resolution_scale = 1});

    return world;
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a STARTUP_SIZE square image, freed with stbi_image_free
 */
static unsigned char* startup_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    ecs_set(world, e, Surface, {.width = STARTUP_SIZE, .height = STARTUP_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, STARTUP_SIZE);
    test_int(h, STARTUP_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get the mean red of the four pixels at a distance from the center along the axes.
 *
 * @param px RGBA pixels of a STARTUP_SIZE square image
 * @param d Distance in pixels
 * @return The mean red
 */
static int32_t startup_ring(
    const unsigned char *px,
    int32_t d)
{
    int32_t c = STARTUP_SIZE / 2;
    int32_t sum = px[(c * STARTUP_SIZE + c + d) * 4] + px[(c * STARTUP_SIZE + c - d) * 4] +
        px[((c + d) * STARTUP_SIZE + c) * 4] + px[((c - d) * STARTUP_SIZE + c) * 4];

    return sum / 4;
}

/** Run a frame.
 *
 * @param world The world
 */
static void startup_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** The slab shadows the floor when the cull pipelines exist from the first frame: the
 * reference of the other case. */
void ShadowStartup_cull_ready_shadows_floor(void) {
    ecs_world_t *world = startup_scene();
    startup_frame(world);
    startup_frame(world);

    unsigned char *px = startup_capture(world, "startup_ready.png");
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    test_assert(startup_ring(px, STARTUP_SHADOWED) < 40);
    test_assert(startup_ring(px, STARTUP_LIT) > 80);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The tile renders in the first frames while the cull pipelines do not exist yet, so no
 * caster draws into it. Once they exist nothing changed on the CPU side, yet the tile must
 * render again with its casters instead of being kept empty, and the slab shadows the floor. */
void ShadowStartup_cull_late_renders_tile(void) {
    ecs_world_t *world = startup_scene();
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *broken = flecs_asprintf("%s/startup_cull.wgsl", tmp);
    FILE *f = fopen(broken, "w");
    test_assert(f != NULL);
    fputs(startup_broken_cull, f);
    fclose(f);

    ecs_entity_t cull = ecs_lookup(world, "box.shader.builtin.cull");
    test_assert(cull != 0);
    char *orig = ecs_os_strdup(ecs_get(world, cull, Shader)->path);
    ecs_set(world, cull, Shader, {.path = broken});

    box_render_gpu_stats_t before, after;
    ecs_log_set_level(-4);
    startup_frame(world);
    startup_frame(world);
    ecs_log_set_level(-1);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    test_assert(box_render_gpu_stats(world, &before));
    test_assert(before.shadow_tiles >= 1);

    ecs_set(world, cull, Shader, {.path = orig});
    startup_frame(world);
    startup_frame(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_assert(after.shadow_tiles > before.shadow_tiles);

    unsigned char *px = startup_capture(world, "startup_late.png");
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    test_assert(startup_ring(px, STARTUP_LIT) > 80);
    test_assert(startup_ring(px, STARTUP_SHADOWED) < 40);
    stbi_image_free(px);

    ecs_os_free(orig);
    ecs_os_free(broken);
    ecs_fini(world);
}
