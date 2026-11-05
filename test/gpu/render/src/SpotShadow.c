/**
 * @file SpotShadow.c
 * @brief Tests for the shadows of spot lights through the shadow atlas, with the GPU backend.
 *
 * Most cases look straight down from 4 units above a white floor into a 64x64 image with a 90
 * degree camera: world x = 0 lands on column 32 and a world unit is 8 pixels.
 *
 * A spot light 10 units above the origin shines straight down; a 1x1 slab 6 units above the
 * floor, behind the camera, shadows the floor from x = -1.25 to 1.25.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define SPOT_SIZE (64)

/** Red channel at most of a shadowed pixel */
#define SPOT_DARK (40)

/** Red channel at least of a lit pixel */
#define SPOT_LIT (100)

/** Create a headless world on the main thread without ambient light, with a white rough floor
 * on y = 0.
 *
 * @return The world
 */
static ecs_world_t* spot_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    return world;
}

/** Create a camera looking straight down from a height and a view of it.
 *
 * @param world The world
 * @param height Height of the camera
 * @param fov Vertical field of view in degrees
 * @param shadows Whether the view renders shadows
 * @return The view
 */
static ecs_entity_t spot_view(
    ecs_world_t *world,
    float height,
    float fov,
    bool shadows)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, height, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = shadows;
    ecs_modified(world, view, View);

    return view;
}

/** Create a spot light shining straight down.
 *
 * @param world The world
 * @param position World position of the light
 * @param range Range of the light
 * @param outer Outer half angle in degrees, the inner one is 5 degrees less
 * @param intensity Intensity of the light
 * @return The light, with shadows
 */
static ecs_entity_t spot_light(
    ecs_world_t *world,
    vec3 position,
    float range,
    float outer,
    float intensity)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, e, SpotLight, {
        .intensity = intensity, .range = range, .inner_angle = outer - 5,
        .outer_angle = outer, .shadows = true, .shadow_resolution = 512
    });

    return e;
}

/** Create a thin slab.
 *
 * @param world The world
 * @param position World position of its center
 * @param size Edge of the slab along x and z
 * @return The slab
 */
static ecs_entity_t spot_slab(
    ecs_world_t *world,
    vec3 position,
    float size)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {size, 0.1f, size});

    return e;
}

/** Create the scene of most cases: the 90 degree camera 4 units above the floor, a spot light
 * 10 units above the origin and the slab 6 units above the floor.
 *
 * @param light Receives the light
 * @param slab Receives the slab
 * @return The world
 */
static ecs_world_t* spot_scene(
    ecs_entity_t *light,
    ecs_entity_t *slab)
{
    ecs_world_t *world = spot_world();
    spot_view(world, 4, 90, true);
    *light = spot_light(world, (vec3){0, 10, 0}, 20, 45, 60 * GLM_PIf);
    *slab = spot_slab(world, (vec3){0, 6, 0}, 1);

    return world;
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a SPOT_SIZE square image, freed with stbi_image_free
 */
static unsigned char* spot_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = SPOT_SIZE, .height = SPOT_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, SPOT_SIZE);
    test_int(h, SPOT_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get the red channel of a pixel.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row
 * @return The channel
 */
static int32_t spot_red(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return px[(y * SPOT_SIZE + x) * 4];
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void spot_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

void SpotShadow_caster_shadows_floor(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    unsigned char *px = spot_capture(world, "spot_caster.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    test_assert(spot_red(px, 30, 34) < SPOT_DARK);
    test_assert(spot_red(px, 56, 32) > SPOT_LIT);
    test_assert(spot_red(px, 8, 32) > SPOT_LIT);
    test_assert(spot_red(px, 32, 8) > SPOT_LIT);
    stbi_image_free(px);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_tiles, 1);
    test_int(status->shadow_dropped, 0);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_assert(stats.shadow_tiles >= 1);
    test_assert(stats.shadow_draws >= 1);
    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_shadows_false_unshadowed(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    SpotLight *s = ecs_get_mut(world, light, SpotLight);
    s->shadows = false;
    ecs_modified(world, light, SpotLight);
    unsigned char *px = spot_capture(world, "spot_false.png");
    test_assert(spot_red(px, 32, 32) > SPOT_LIT);
    test_assert(spot_red(px, 56, 32) > SPOT_LIT);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shadow_tiles, 0);
    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_view_shadows_disabled_unshadowed(void) {
    // lighting/shadows
    ecs_world_t *world = spot_world();
    spot_view(world, 4, 90, false);
    spot_light(world, (vec3){0, 10, 0}, 20, 45, 60 * GLM_PIf);
    spot_slab(world, (vec3){0, 6, 0}, 1);
    unsigned char *px = spot_capture(world, "spot_view_off.png");
    test_assert(spot_red(px, 32, 32) > SPOT_LIT);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);
    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_no_shadow_caster_casts_none(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    ecs_add(world, slab, NoShadow);
    unsigned char *px = spot_capture(world, "spot_noshadow.png");
    test_assert(spot_red(px, 32, 32) > SPOT_LIT);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_baked_light_has_no_tile(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    ecs_singleton_set(world, LightBake, {.probe_spacing = 4, .bounces = 0, .capture_size = 8,
        .budget_ms = 2, .bounds_padding = 1, .compute = true});
    SpotLight *s = ecs_get_mut(world, light, SpotLight);
    s->bake = LightBakeModeBaked;
    ecs_modified(world, light, SpotLight);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        stbi_image_free(spot_capture(world, "spot_baked.png"));
        test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);
        test_int(ecs_singleton_get(world, RenderStatus)->shadow_dropped, 0);
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shadow_tiles, 0);
    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_moved_light_moves_shadow(void) {
    // lighting/shadows
    ecs_world_t *world = spot_world();
    spot_view(world, 4, 90, true);
    spot_slab(world, (vec3){0, 6, 0}, 1);
    ecs_entity_t pivot = ecs_new(world);
    ecs_set(world, pivot, Position, {0, 0, 0});
    ecs_set(world, pivot, Rotation, {0, 0, 0});
    ecs_entity_t light = spot_light(world, (vec3){0, 10, 0}, 20, 45, 60 * GLM_PIf);
    ecs_add_pair(world, light, EcsChildOf, pivot);

    unsigned char *px = spot_capture(world, "spot_before.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    test_assert(spot_red(px, 52, 32) > SPOT_LIT);
    stbi_image_free(px);

    ecs_set(world, pivot, Rotation, {0, 0, 0.2f});
    px = spot_capture(world, "spot_after.png");
    test_assert(spot_red(px, 32, 32) > SPOT_LIT);
    test_assert(spot_red(px, 52, 32) < SPOT_DARK);
    stbi_image_free(px);

    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_atlas_full_drops_farthest(void) {
    // lighting/shadows
    ecs_world_t *world = spot_world();
    ecs_singleton_set(world, ShadowAtlas, {.resolution = 512, .min_tile = 512});
    spot_view(world, 30, 30, true);
    ecs_entity_t near = spot_light(world, (vec3){-4, 10, 0}, 14, 25, 200 * GLM_PIf);
    ecs_entity_t far = spot_light(world, (vec3){4, 10, 6}, 13, 25, 200 * GLM_PIf);
    spot_slab(world, (vec3){-2.5f, 5, 0}, 0.6f);
    spot_slab(world, (vec3){2.5f, 5, 6}, 0.6f);
    (void)near;
    (void)far;

    unsigned char *px = spot_capture(world, "spot_full.png");
    test_assert(spot_red(px, 28, 32) < SPOT_DARK);
    test_assert(spot_red(px, 36, 56) > SPOT_LIT);
    stbi_image_free(px);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_tiles, 1);
    test_int(status->shadow_dropped, 1);
    spot_no_errors(world);
    ecs_fini(world);
}

/* The tile tests the floor and both slabs and keeps the floor and the slab inside its frustum;
 * the tile renders every frame without the shadow cache */
void SpotShadow_light_view_culls_casters(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    test_bool(box_render_shadow_cache(world, false), true);
    spot_slab(world, (vec3){30, 6, 0}, 1);
    box_render_gpu_stats_t before, after;
    unsigned char *px = spot_capture(world, "spot_cull.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    stbi_image_free(px);

    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, 1);
    test_assert(after.shadow_draws > before.shadow_draws);

    box_render_cull_stats_t tile;
    test_bool(box_render_gpu_cull_tile(world, 0, &tile), true);
    test_int(tile.tested, 3);
    test_int(tile.visible, 2);
    test_bool(box_render_gpu_cull_tile(world, 1, &tile), false);
    spot_no_errors(world);
    ecs_fini(world);
}

/* Without the shadow cache, so the tile renders in every frame */
void SpotShadow_two_views_render_atlas_once(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    test_bool(box_render_shadow_cache(world, false), true);
    spot_view(world, 5, 90, true);
    unsigned char *px = spot_capture(world, "spot_two.png");
    stbi_image_free(px);

    box_render_gpu_stats_t before, after;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.passes - before.passes, 2);
    test_int(after.shadow_tiles - before.shadow_tiles, 1);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    spot_no_errors(world);
    ecs_fini(world);
}

/** Run frames until the light bake and the reflection probes of the default scene converged.
 *
 * @param world The world
 * @return Whether they converged within 120 frames
 */
static bool spot_converge(
    ecs_world_t *world)
{
    ecs_entity_t bake = ecs_id(LightBake);
    int32_t frame;
    bool result = false;
    for (frame = 0; !result && frame < 120; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        result = ecs_get(world, bake, LightBakeStatus)->converged &&
            ecs_get(world, ecs_id(ReflectionBake), ReflectionBakeStatus)->converged;
    }

    return result;
}

void SpotShadow_tile_change_rebakes_nothing(void) {
    // lighting/shadows
    ecs_world_t *world = spot_world();
    ecs_entity_t view = spot_view(world, 4, 90, true);
    spot_light(world, (vec3){0, 10, 0}, 20, 45, 60 * GLM_PIf);
    spot_slab(world, (vec3){0, 6, 0}, 1);
    ecs_singleton_set(world, LightBake, {.probe_spacing = 4, .bounces = 1, .capture_size = 8,
        .budget_ms = 2, .bounds_padding = 1, .compute = true});
    ecs_singleton_set(world, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = true, .scene_probe_height = 2});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    test_bool(spot_converge(world), true);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);

    int32_t round, frame;
    for (round = 0; round < 2; round ++) {
        View *v = ecs_get_mut(world, view, View);
        v->shadows.enabled = round == 1;
        ecs_modified(world, view, View);
        for (frame = 0; frame < 5; frame ++) {
            ecs_progress(world, 0);
            box_render_sync(world);
            test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, round);
            test_bool(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->converged, true);
            test_bool(ecs_get(world, ecs_id(ReflectionBake), ReflectionBakeStatus)->converged,
                true);
        }
    }

    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_atlas_resolution_change(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    unsigned char *px = spot_capture(world, "spot_res_a.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    stbi_image_free(px);

    ecs_singleton_set(world, ShadowAtlas, {.resolution = 1024, .min_tile = 64});
    px = spot_capture(world, "spot_res_b.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    test_assert(spot_red(px, 56, 32) > SPOT_LIT);
    stbi_image_free(px);

    ecs_singleton_set(world, ShadowAtlas, {.resolution = 256, .min_tile = 64});
    px = spot_capture(world, "spot_res_c.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    test_assert(spot_red(px, 56, 32) > SPOT_LIT);
    stbi_image_free(px);

    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    spot_no_errors(world);
    ecs_fini(world);
}

void SpotShadow_device_loss_recreates_atlas(void) {
    // lighting/shadows
    ecs_entity_t light, slab;
    ecs_world_t *world = spot_scene(&light, &slab);
    unsigned char *px = spot_capture(world, "spot_loss_a.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    stbi_image_free(px);

    box_render_gpu_lose_device(world);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    px = spot_capture(world, "spot_loss_b.png");
    test_assert(spot_red(px, 32, 32) < SPOT_DARK);
    test_assert(spot_red(px, 56, 32) > SPOT_LIT);
    stbi_image_free(px);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 1);
    ecs_fini(world);
}
