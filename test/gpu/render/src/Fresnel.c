/**
 * @file Fresnel.c
 * @brief Tests for how a Fresnel adds a rim of emitted color and opacity by the view angle,
 * checked on PNG captures.
 *
 * The cases look at a smooth sphere of radius 2 on the origin from a camera 6 units in front of
 * it. Its silhouette is about 29 pixels from the center of the 128x96 capture, where the normal
 * is nearly perpendicular to the view; at the center it faces the camera, where the factor is
 * the bias (0).
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width of the captures */
#define FRESNEL_W (128)

/** Height of the captures */
#define FRESNEL_H (96)

/** Column of the center of the sphere in the captures */
#define FRESNEL_CX (64)

/** Row of the center of the sphere in the captures */
#define FRESNEL_CY (48)

/** Create the world of the cases: a camera 6 units in front of the origin looking down -z into
 * a 128x96 surface cleared to black. A lit view has a point light next to the camera and a dim
 * ambient light, no sun.
 *
 * @param lit Whether the view is lit
 * @param fresnel Whether the scene enables Fresnel (Scene.fresnel); every other feature is on
 * @return The world
 */
static ecs_world_t* fresnel_world(
    bool lit,
    bool fresnel)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true,
        .shader_features.Extended.fresnel = fresnel});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = lit});
    ecs_singleton_set(world, AmbientLight, {.intensity = lit ? 0.2f : 0.0f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    if (lit) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {1, 1, 5});
        ecs_set(world, light, PointLight, {.intensity = 20, .range = 30});
    }

    return world;
}

/** Render two frames into a 128x96 PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* fresnel_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = FRESNEL_W, .height = FRESNEL_H,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_delete(world, e);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, FRESNEL_W);
    test_int(h, FRESNEL_H);

    return px;
}

/** Add a smooth sphere of radius 2 on the origin.
 *
 * @param world The world
 * @param color Its base color
 * @param fresnel Its Fresnel, NULL for none
 * @return The sphere
 */
static ecs_entity_t fresnel_sphere(
    ecs_world_t *world,
    Rgb color,
    const Fresnel *fresnel)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Sphere, {.segments = 48, .smooth = true, .radius = 2});
    ecs_set_ptr(world, e, Rgb, &color);
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.8f});
    if (fresnel) {
        ecs_set_ptr(world, e, Fresnel, fresnel);
    }

    return e;
}

/** Pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return The RGBA bytes
 */
static const unsigned char* fresnel_px(
    const unsigned char *px,
    int x,
    int y)
{
    return &px[(y * FRESNEL_W + x) * 4];
}

/** Find the column of the right silhouette of the sphere on its center row: the rightmost
 * pixel with a channel above a threshold.
 *
 * @param px The pixels
 * @return The column, -1 for none
 */
static int fresnel_edge(
    const unsigned char *px)
{
    int x;
    for (x = FRESNEL_W - 1; x > FRESNEL_CX; x --) {
        const unsigned char *p = fresnel_px(px, x, FRESNEL_CY);
        if (p[0] > 16 || p[1] > 16 || p[2] > 16) {
            return x;
        }
    }

    return -1;
}

/** Check that two pixels differ by at most a tolerance per channel.
 *
 * @param a The first pixel
 * @param b The second pixel
 * @param tolerance Largest difference
 * @return Whether they match
 */
static bool fresnel_near(
    const unsigned char *a,
    const unsigned char *b,
    int tolerance)
{
    int i;
    for (i = 0; i < 3; i ++) {
        if (abs((int)a[i] - (int)b[i]) > tolerance) {
            return false;
        }
    }

    return true;
}

/** Render a sphere with and without a Fresnel in worlds of the same setup.
 *
 * @param lit Whether the view is lit
 * @param enabled Whether the scene enables Fresnel
 * @param color Base color of the sphere
 * @param opacity Opacity of the sphere, 1 for none
 * @param fresnel The Fresnel
 * @param name Prefix of the capture names
 * @param plain Receives the capture without the Fresnel
 * @param rim Receives the capture with it
 */
static void fresnel_pair(
    bool lit,
    bool enabled,
    Rgb color,
    float opacity,
    const Fresnel *fresnel,
    const char *name,
    unsigned char **plain,
    unsigned char **rim)
{
    int i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = fresnel_world(lit, enabled);
        ecs_entity_t e = fresnel_sphere(world, color, i ? fresnel : NULL);
        if (opacity < 1) {
            ecs_set(world, e, Opacity, {opacity});
        }

        char *file = flecs_asprintf("%s_%s.png", name, i ? "rim" : "plain");
        unsigned char *px = fresnel_render(world, file);
        ecs_os_free(file);
        if (i) {
            *rim = px;
        } else {
            *plain = px;
        }

        ecs_fini(world);
    }
}

void Fresnel_red_rim_at_silhouette(void) {
    // materials/fresnel
    Fresnel red = { .color = {255, 0, 0}, .strength = 4, .power = 2, .bias = 0 };
    unsigned char *plain, *rim;
    fresnel_pair(true, true, (Rgb){128, 128, 128}, 1, &red, "fresnel_red", &plain, &rim);

    int edge = fresnel_edge(rim);
    test_assert(edge > FRESNEL_CX + 20);
    test_assert(edge < FRESNEL_CX + 34);

    /* Just inside the silhouette the red channel is far brighter, green and blue are not */
    const unsigned char *a = fresnel_px(plain, edge - 1, FRESNEL_CY);
    const unsigned char *b = fresnel_px(rim, edge - 1, FRESNEL_CY);
    test_assert((int)b[0] - (int)a[0] > 60);
    test_assert(abs((int)b[1] - (int)a[1]) <= 4);
    test_assert(abs((int)b[2] - (int)a[2]) <= 4);
    test_assert(b[0] > b[1] + 60);

    /* The left silhouette rims as well */
    const unsigned char *l = fresnel_px(rim, 2 * FRESNEL_CX - edge, FRESNEL_CY);
    test_assert(l[0] > l[1] + 40);

    /* At the center the surface faces the camera: unchanged */
    test_assert(fresnel_near(fresnel_px(plain, FRESNEL_CX, FRESNEL_CY),
        fresnel_px(rim, FRESNEL_CX, FRESNEL_CY), 1));
    test_assert(fresnel_near(fresnel_px(plain, FRESNEL_CX - 4, FRESNEL_CY - 4),
        fresnel_px(rim, FRESNEL_CX - 4, FRESNEL_CY - 4), 2));

    /* Outside the sphere the background stays black */
    test_assert(fresnel_near(fresnel_px(rim, FRESNEL_W - 2, FRESNEL_CY),
        fresnel_px(plain, FRESNEL_W - 2, FRESNEL_CY), 0));

    stbi_image_free(plain);
    stbi_image_free(rim);
}

void Fresnel_bias_lights_center(void) {
    // materials/fresnel
    Fresnel biased = { .color = {0, 255, 0}, .strength = 2, .power = 5, .bias = 0.5f };
    unsigned char *plain, *rim;
    fresnel_pair(true, true, (Rgb){128, 128, 128}, 1, &biased, "fresnel_bias", &plain, &rim);

    /* With a bias the center glows too, in the green of the rim */
    const unsigned char *a = fresnel_px(plain, FRESNEL_CX, FRESNEL_CY);
    const unsigned char *b = fresnel_px(rim, FRESNEL_CX, FRESNEL_CY);
    test_assert((int)b[1] - (int)a[1] > 30);
    test_assert(abs((int)b[0] - (int)a[0]) <= 4);

    stbi_image_free(plain);
    stbi_image_free(rim);
}

void Fresnel_opacity_raises_rim_alpha(void) {
    // materials/fresnel
    Fresnel field = { .color = {255, 255, 255}, .strength = 0, .power = 2, .bias = 0,
        .opacity = 1 };
    unsigned char *plain, *rim;
    fresnel_pair(false, true, (Rgb){255, 255, 255}, 0.25f, &field, "fresnel_alpha", &plain,
        &rim);

    /* A white sphere on black: the brightness is its alpha, raised at the rim */
    int edge = fresnel_edge(rim);
    test_assert(edge > FRESNEL_CX + 20);
    test_assert(edge < FRESNEL_CX + 34);
    const unsigned char *a = fresnel_px(plain, edge - 1, FRESNEL_CY);
    const unsigned char *b = fresnel_px(rim, edge - 1, FRESNEL_CY);
    test_assert((int)b[0] - (int)a[0] > 40);
    test_assert((int)b[1] - (int)a[1] > 40);
    test_assert((int)b[2] - (int)a[2] > 40);

    /* At the center the alpha is the Opacity alone */
    test_assert(fresnel_near(fresnel_px(plain, FRESNEL_CX, FRESNEL_CY),
        fresnel_px(rim, FRESNEL_CX, FRESNEL_CY), 1));
    test_assert(fresnel_px(rim, FRESNEL_CX, FRESNEL_CY)[0] < 200);

    stbi_image_free(plain);
    stbi_image_free(rim);
}

void Fresnel_opacity_ignored_when_opaque(void) {
    // materials/fresnel
    Fresnel field = { .color = {255, 255, 255}, .strength = 0, .power = 2, .bias = 0,
        .opacity = 1 };
    unsigned char *plain, *rim;
    fresnel_pair(true, true, (Rgb){128, 128, 128}, 1, &field, "fresnel_opaque", &plain, &rim);

    int x, y;
    for (y = 0; y < FRESNEL_H; y ++) {
        for (x = 0; x < FRESNEL_W; x ++) {
            test_assert(fresnel_near(fresnel_px(plain, x, y), fresnel_px(rim, x, y), 1));
        }
    }

    stbi_image_free(plain);
    stbi_image_free(rim);
}

void Fresnel_scene_off_unchanged(void) {
    // materials/fresnel
    Fresnel red = { .color = {255, 0, 0}, .strength = 4, .power = 2, .bias = 0.25f,
        .opacity = 1 };
    unsigned char *plain, *rim;
    fresnel_pair(true, false, (Rgb){128, 128, 128}, 1, &red, "fresnel_off", &plain, &rim);
    test_int(memcmp(plain, rim, FRESNEL_W * FRESNEL_H * 4), 0);
    stbi_image_free(plain);
    stbi_image_free(rim);

    fresnel_pair(false, false, (Rgb){255, 255, 255}, 0.25f, &red, "fresnel_off_blend", &plain,
        &rim);
    test_int(memcmp(plain, rim, FRESNEL_W * FRESNEL_H * 4), 0);
    stbi_image_free(plain);
    stbi_image_free(rim);
}

void Fresnel_ignored_by_2d(void) {
    // materials/fresnel
    unsigned char *px[2];
    int i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = fresnel_world(false, true);
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, 0});
        ecs_set(world, e, Rect, {4, 4});
        ecs_set(world, e, Rgb, {200, 200, 200});
        if (i) {
            ecs_set(world, e, Fresnel, {.color = {255, 0, 0}, .strength = 4, .power = 1,
                .bias = 1, .opacity = 1});
        }

        px[i] = fresnel_render(world, i ? "fresnel_2d_rim.png" : "fresnel_2d_plain.png");
        ecs_fini(world);
    }

    test_int(memcmp(px[0], px[1], FRESNEL_W * FRESNEL_H * 4), 0);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}
