/**
 * @file AtmosphereSky.c
 * @brief Tests for the sky the forward pass draws behind the geometry and the aerial
 * perspective of the 3D meshes in a scene with an Atmosphere.
 *
 * Boxes are drawn by unlit views, so a box shows its own color unless the atmosphere between it
 * and the camera changes it. Pixels are compared as the PNG holds them, display encoded.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width of the rendered image */
#define WIDTH (64)

/** Height of the rendered image */
#define HEIGHT (48)

/** Frames a render runs */
#define FRAMES (3)

/** Clear color of the views */
#define CLEAR ((rgba){10, 200, 20, 255})

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Create a scene with the default Atmosphere and a white sun at an elevation.
 *
 * @param world The world
 * @param units_per_km World units in a km
 * @param elevation Elevation of the sun in degrees
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    float units_per_km,
    float elevation)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, Atmosphere);
    ecs_get_mut(world, scene, Atmosphere)->units_per_km = units_per_km;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(elevation), 0, 0});

    return scene;
}

/** Add a camera at a height that looks along -z, pitched up, to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param y Height of the camera
 * @param fov Vertical field of view in degrees
 * @param pitch Angle above the horizon the camera looks at in degrees
 * @return The camera
 */
static ecs_entity_t camera_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float y,
    float fov,
    float pitch)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, y, 0});
    ecs_set(world, camera, Rotation, {glm_rad(pitch), 0, 0});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f});

    return camera;
}

/** Add a view of a camera that clears to CLEAR, with the View constructor's settings.
 *
 * @param world The world
 * @param camera The camera
 * @param lit Whether the view is lit
 * @param atmosphere Whether the view renders the atmosphere
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t camera,
    bool lit,
    bool atmosphere)
{
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = CLEAR};
    v->lighting.enabled = lit;
    v->lighting.atmosphere = atmosphere;
    ecs_modified(world, view, View);

    return view;
}

/** Add an unlit box to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param position Center of the box
 * @param size Edge of the box
 * @param color Color of the box
 */
static void box_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float position[3],
    float size,
    Rgb color)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {size, size, size});
    ecs_set_ptr(world, e, Rgb, &color);
}

/** Render frames of a world into a WIDTH x HEIGHT image.
 *
 * @param world The world, deleted
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* render(
    ecs_world_t *world)
{
    int32_t frame;
    int w = 0, h = 0, n = 0;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = WIDTH, .height = HEIGHT, .write_to_file = "sky.png",
        .resolution_scale = 1});
    for (frame = 0; frame < FRAMES; frame ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/sky.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, WIDTH);
    test_int(h, HEIGHT);
    ecs_os_free(path);
    ecs_fini(world);

    return result;
}

/** Get a pixel of an image.
 *
 * @param pixels The image
 * @param x Column
 * @param y Row, 0 at the top
 * @param out Receives red, green and blue
 */
static void pixel(
    const unsigned char *pixels,
    int32_t x,
    int32_t y,
    int32_t out[3])
{
    const unsigned char *p = pixels + (y * WIDTH + x) * 4;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

/** Get the largest difference of the channels of two colors.
 *
 * @param a The first color
 * @param b The second color
 * @return The difference
 */
static int32_t difference(
    const int32_t a[3],
    const int32_t b[3])
{
    int32_t c, result = 0;
    for (c = 0; c < 3; c ++) {
        result = glm_max(result, abs(a[c] - b[c]));
    }

    return result;
}

/** Get the sum of the squared differences of the channels of two colors.
 *
 * @param a The first color
 * @param b The second color
 * @return The squared distance
 */
static int32_t distance2(
    const int32_t a[3],
    const int32_t b[3])
{
    int32_t c, result = 0;
    for (c = 0; c < 3; c ++) {
        result += (a[c] - b[c]) * (a[c] - b[c]);
    }

    return result;
}

/** Render two boxes of one color in front of the horizon at noon, one 20 m and one 30 km
 * away, seen by an unlit view.
 *
 * @param atmosphere Whether the view renders the atmosphere
 * @return The pixels, the near box at (16, 24), the far box at (32, 24) and the sky at the
 * horizon at (56, 24)
 */
static unsigned char* render_boxes(
    bool atmosphere)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 100, 90);
    ecs_entity_t camera = camera_new(world, scene, 0, 60, 0);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = CLEAR},
        .lighting = {.atmosphere = atmosphere}});
    box_new(world, scene, (float[3]){-0.75f, 0, -2}, 0.5f, (Rgb){200, 40, 40});
    box_new(world, scene, (float[3]){0, 0, -3000}, 600, (Rgb){200, 40, 40});

    return render(world);
}

void AtmosphereSky_zenith_bluer_and_darker_than_horizon(void) {
    // atmosphere/sky
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 1000, 90);
    ecs_get_mut(world, scene, Atmosphere)->sun_disk_intensity = 0;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t camera = camera_new(world, scene, 0, 90, 45);
    view_new(world, camera, true, true);
    unsigned char *pixels = render(world);
    int32_t zenith[3], horizon[3], clear[3] = {CLEAR.r, CLEAR.g, CLEAR.b};
    pixel(pixels, WIDTH / 2, 0, zenith);
    pixel(pixels, WIDTH / 2, HEIGHT - 1, horizon);
    test_assert(difference(zenith, clear) > 8);
    test_assert(difference(horizon, clear) > 8);
    test_assert(zenith[0] + zenith[1] + zenith[2] < horizon[0] + horizon[1] + horizon[2]);
    test_assert(zenith[2] * (horizon[0] + horizon[1] + horizon[2]) >
        horizon[2] * (zenith[0] + zenith[1] + zenith[2]));
    test_assert(zenith[2] > zenith[0]);
    stbi_image_free(pixels);
}

void AtmosphereSky_view_without_atmosphere_shows_clear_color(void) {
    // atmosphere/sky
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 1000, 90);
    ecs_entity_t camera = camera_new(world, scene, 0, 90, 45);
    view_new(world, camera, true, false);
    unsigned char *pixels = render(world);
    int32_t i;
    for (i = 0; i < WIDTH * HEIGHT; i ++) {
        test_int(pixels[i * 4], CLEAR.r);
        test_int(pixels[i * 4 + 1], CLEAR.g);
        test_int(pixels[i * 4 + 2], CLEAR.b);
    }

    stbi_image_free(pixels);
}

void AtmosphereSky_far_box_approaches_horizon_color(void) {
    // atmosphere/sky
    unsigned char *sky = render_boxes(true);
    unsigned char *plain = render_boxes(false);
    int32_t near_sky[3], far_sky[3], horizon[3], near_plain[3], far_plain[3];
    pixel(sky, 16, 24, near_sky);
    pixel(sky, 32, 24, far_sky);
    pixel(sky, 56, 24, horizon);
    pixel(plain, 16, 24, near_plain);
    pixel(plain, 32, 24, far_plain);
    test_assert(difference(near_plain, (int32_t[3]){200, 40, 40}) <= 2);
    test_assert(difference(far_plain, (int32_t[3]){200, 40, 40}) <= 2);
    test_assert(difference(near_sky, near_plain) <= 6);
    test_assert(difference(far_sky, far_plain) > 20);
    test_assert(distance2(far_sky, horizon) * 4 < distance2(far_plain, horizon));
    test_assert(distance2(far_sky, horizon) < distance2(near_sky, horizon));
    stbi_image_free(sky);
    stbi_image_free(plain);
}

void AtmosphereSky_sky_does_not_draw_over_geometry(void) {
    // atmosphere/sky
    unsigned char *images[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, 1000, 90);
        ecs_entity_t camera = camera_new(world, scene, 0, 60, 20);
        view_new(world, camera, false, i == 0);
        box_new(world, scene, (float[3]){0, 0.73f, -2}, 0.5f, (Rgb){40, 60, 220});
        images[i] = render(world);
    }

    int32_t box_sky[3], box_plain[3], corner_sky[3], corner_plain[3];
    pixel(images[0], WIDTH / 2, HEIGHT / 2, box_sky);
    pixel(images[1], WIDTH / 2, HEIGHT / 2, box_plain);
    pixel(images[0], 2, 2, corner_sky);
    pixel(images[1], 2, 2, corner_plain);
    test_assert(difference(box_plain, (int32_t[3]){40, 60, 220}) <= 2);
    test_assert(difference(box_sky, box_plain) <= 2);
    test_assert(difference(corner_plain, (int32_t[3]){CLEAR.r, CLEAR.g, CLEAR.b}) == 0);
    test_assert(difference(corner_sky, corner_plain) > 8);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** Render a lit view that looks at the white sun of a scene, the sun at the center of the
 * image.
 *
 * @param elevation Elevation of the sun and pitch of the camera in degrees
 * @param fov Vertical field of view in degrees
 * @param radius Angular radius of the sun disk in degrees
 * @param intensity Atmosphere.sun_disk_intensity
 * @return The pixels, the sun at (WIDTH / 2, HEIGHT / 2)
 */
static unsigned char* render_sun(
    float elevation,
    float fov,
    float radius,
    float intensity)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 1000, elevation);
    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->sun_radius = radius;
    a->sun_disk_intensity = intensity;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t camera = camera_new(world, scene, 0, fov, elevation);
    view_new(world, camera, true, true);

    return render(world);
}

/** Get the sum of the channels of a color.
 *
 * @param c The color
 * @return The sum
 */
static int32_t brightness(
    const int32_t c[3])
{
    return c[0] + c[1] + c[2];
}

void AtmosphereSky_sun_brighter_than_sky_beside_disk(void) {
    // atmosphere/sun_disk
    unsigned char *pixels = render_sun(20, 4, 0.266f, 1);
    int32_t sun[3], beside[3];
    pixel(pixels, WIDTH / 2, HEIGHT / 2, sun);
    pixel(pixels, WIDTH / 2 + 12, HEIGHT / 2, beside);
    test_assert(brightness(sun) > brightness(beside) + 60);
    test_assert(sun[0] > beside[0] && sun[1] > beside[1] && sun[2] >= beside[2]);
    stbi_image_free(pixels);
}

void AtmosphereSky_sun_disk_intensity_zero_removes_disk(void) {
    // atmosphere/sun_disk
    unsigned char *with = render_sun(20, 4, 0.266f, 1);
    unsigned char *without = render_sun(20, 4, 0.266f, 0);
    int32_t sun_with[3], sun_without[3], beside_with[3], beside_without[3];
    pixel(with, WIDTH / 2, HEIGHT / 2, sun_with);
    pixel(without, WIDTH / 2, HEIGHT / 2, sun_without);
    pixel(with, WIDTH / 2 + 12, HEIGHT / 2, beside_with);
    pixel(without, WIDTH / 2 + 12, HEIGHT / 2, beside_without);
    test_assert(difference(sun_with, sun_without) > 20);
    test_assert(difference(sun_without, beside_without) <= 6);
    test_assert(difference(beside_with, beside_without) <= 1);
    stbi_image_free(with);
    stbi_image_free(without);
}

void AtmosphereSky_sun_below_horizon_draws_no_disk(void) {
    // atmosphere/sun_disk
    unsigned char *with = render_sun(-5, 10, 2, 1);
    unsigned char *without = render_sun(-5, 10, 2, 0);
    int32_t i, c;
    for (i = 0; i < WIDTH * HEIGHT; i ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(abs(with[i * 4 + c] - without[i * 4 + c]) <= 1);
        }
    }

    stbi_image_free(with);
    stbi_image_free(without);
}

void AtmosphereSky_disk_center_brighter_than_limb(void) {
    // atmosphere/sun_disk
    unsigned char *pixels = render_sun(20, 10, 2, 0.3f);
    int32_t center[3], limb[3], beside[3];
    pixel(pixels, WIDTH / 2, HEIGHT / 2, center);
    pixel(pixels, WIDTH / 2 + 8, HEIGHT / 2, limb);
    pixel(pixels, WIDTH / 2 + 14, HEIGHT / 2, beside);
    test_assert(center[0] < 255 && center[1] < 255 && center[2] < 255);
    test_assert(brightness(limb) > brightness(beside) + 20);
    test_assert(brightness(center) > brightness(limb) + 15);
    stbi_image_free(pixels);
}

/** Color of the fogs, which a fog colored by the atmosphere ignores */
#define FOG_COLOR ((rgb){255, 0, 255})

/** Render an unlit view at 1 m above the ground of a scene with 1 world unit per meter that
 * looks along -z at the horizon, toward a wall 2 km away in a dense ground fog.
 *
 * @param elevation Elevation of the sun in degrees
 * @param yaw Yaw of the sun in degrees, 0 in front of the camera and 180 behind it
 * @param fog Whether the view has the fog, else the wall is left out too
 * @param atmosphere HeightFog.atmosphere of the fog
 * @return The pixels, the wall at (WIDTH / 2, HEIGHT / 2), or the sky just above the horizon
 * at (WIDTH / 2, HEIGHT / 2 - 1) without fog
 */
static unsigned char* render_fog(
    float elevation,
    float yaw,
    bool fog,
    bool atmosphere)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 1000, elevation);
    ecs_get_mut(world, scene, Atmosphere)->sun_disk_intensity = 0;
    ecs_modified(world, scene, Atmosphere);
    ecs_iter_t it = ecs_children(world, scene);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_has(world, it.entities[i], DirectionalLight)) {
                ecs_set(world, it.entities[i], Rotation, {glm_rad(elevation), glm_rad(yaw), 0});
            }
        }
    }

    ecs_entity_t camera = camera_new(world, scene, 1, 60, 0);
    ecs_entity_t view = view_new(world, camera, false, true);
    if (fog) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, HeightFog, {.density = 0.005f, .falloff = 0.001f, .max_opacity = 1,
            .color = FOG_COLOR, .sun_inscatter = 1, .ambient_inscatter = 1,
            .atmosphere = atmosphere});
        View *v = ecs_get_mut(world, view, View);
        *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = e };
        ecs_modified(world, view, View);
        box_new(world, scene, (float[3]){0, 0, -2000}, 1600, (Rgb){200, 40, 40});
    }

    return render(world);
}

void AtmosphereSky_fog_at_noon_tints_toward_horizon(void) {
    // atmosphere/fog
    unsigned char *sky = render_fog(90, 0, false, false);
    unsigned char *colored = render_fog(90, 0, true, true);
    unsigned char *plain = render_fog(90, 0, true, false);
    int32_t horizon[3], wall_colored[3], wall_plain[3];
    int32_t fog[3] = {FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b};
    pixel(sky, WIDTH / 2, HEIGHT / 2 - 1, horizon);
    pixel(colored, WIDTH / 2, HEIGHT / 2, wall_colored);
    pixel(plain, WIDTH / 2, HEIGHT / 2, wall_plain);
    test_assert(difference(wall_plain, fog) <= 3);
    test_assert(difference(wall_colored, (int32_t[3]){200, 40, 40}) > 40);
    test_assert(distance2(wall_colored, horizon) * 8 < distance2(wall_colored, fog));
    test_assert(difference(wall_colored, horizon) <= 40);
    test_assert(wall_colored[2] > wall_colored[0]);
    stbi_image_free(sky);
    stbi_image_free(colored);
    stbi_image_free(plain);
}

void AtmosphereSky_fog_at_sunset_warmer_toward_sun(void) {
    // atmosphere/fog
    unsigned char *toward = render_fog(3, 0, true, true);
    unsigned char *away = render_fog(3, 180, true, true);
    int32_t sun_side[3], other_side[3];
    pixel(toward, WIDTH / 2, HEIGHT / 2, sun_side);
    pixel(away, WIDTH / 2, HEIGHT / 2, other_side);
    test_assert(brightness(sun_side) > brightness(other_side) + 20);
    test_assert(sun_side[0] - sun_side[2] > other_side[0] - other_side[2] + 20);
    test_assert(sun_side[0] > sun_side[2]);
    stbi_image_free(toward);
    stbi_image_free(away);
}

void AtmosphereSky_fog_without_atmosphere_uses_color(void) {
    // atmosphere/fog
    unsigned char *plain = render_fog(3, 0, true, false);
    unsigned char *colored = render_fog(3, 0, true, true);
    int32_t wall_plain[3], wall_colored[3];
    int32_t fog[3] = {FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b};
    pixel(plain, WIDTH / 2, HEIGHT / 2, wall_plain);
    pixel(colored, WIDTH / 2, HEIGHT / 2, wall_colored);
    test_assert(difference(wall_plain, fog) <= 3);
    test_assert(difference(wall_colored, fog) > 60);
    stbi_image_free(plain);
    stbi_image_free(colored);
}
