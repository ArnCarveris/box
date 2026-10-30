/**
 * @file ClearCoat.c
 * @brief Tests for how a ClearCoat shades and updates, checked on PNG captures.
 *
 * Most cases look at a rough dark quad facing the camera under a point light 3 units in front
 * of it, where a coat shows a second, sharp highlight at the center of the image.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Width of the captures of the clear coat cases */
#define COAT_W (128)

/** Height of the captures of the clear coat cases */
#define COAT_H (64)

/** Create the world of the clear coat cases: a camera 6 units in front of the origin looking
 * down -z into a 128x64 surface, where a unit at the origin is about 9 pixels and the origin is
 * pixel 64, 32.
 *
 * It has no ambient light and a directional light of intensity 0, so there is no default sun.
 *
 * @param lit Whether the view is lit
 * @return The world
 */
static ecs_world_t* coat_world(
    bool lit)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = lit});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    return world;
}

/** Render two frames of a clear coat case into a 128x64 PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* coat_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = COAT_W, .height = COAT_H,
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
    test_int(w, COAT_W);
    test_int(h, COAT_H);

    return px;
}

/** Add a rough dark gray dielectric 4x4 quad facing the camera, centered on a point of the
 * x axis.
 *
 * @param world The world
 * @param x Position of the center along x
 * @param coat The ClearCoat of the quad, NULL for none
 * @return The quad
 */
static ecs_entity_t coat_quad(
    ecs_world_t *world,
    float x,
    const ClearCoat *coat)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Quad, {4, 4});
    ecs_set(world, e, Rgb, {40, 40, 40});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    if (coat) {
        ecs_set_ptr(world, e, ClearCoat, coat);
    }

    return e;
}

/** Add a point light 3 units in front of a point of the x axis.
 *
 * @param world The world
 * @param x Position along x
 * @param intensity Intensity of the light
 */
static void coat_light(
    ecs_world_t *world,
    float x,
    float intensity)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 3});
    ecs_set(world, e, PointLight, {.intensity = intensity, .range = 20});
}

/** Render a rough quad on the origin under a point light 3 units in front of it.
 *
 * @param coat The ClearCoat of the quad, NULL for none
 * @param intensity Intensity of the light
 * @param lit Whether the view is lit
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* coat_highlight(
    const ClearCoat *coat,
    float intensity,
    bool lit,
    const char *name)
{
    ecs_world_t *world = coat_world(lit);
    coat_quad(world, 0, coat);
    coat_light(world, 0, intensity);
    unsigned char *px = coat_render(world, name);
    ecs_fini(world);

    return px;
}

/** Sum of the channels of a pixel of a clear coat capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Red plus green plus blue
 */
static int coat_sum(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = &px[(y * COAT_W + x) * 4];

    return p[0] + p[1] + p[2];
}

/** Find the brightest pixel of a range of columns of a clear coat capture.
 *
 * @param px The pixels
 * @param x0 First column
 * @param x1 Column after the last
 * @param x Receives the column of the peak, NULL to ignore
 * @param y Receives the row of the peak, NULL to ignore
 * @return The channel sum of the peak
 */
static int coat_peak(
    const unsigned char *px,
    int x0,
    int x1,
    int *x,
    int *y)
{
    int best = -1, i, j;
    for (j = 0; j < COAT_H; j ++) {
        for (i = x0; i < x1; i ++) {
            int sum = coat_sum(px, i, j);
            if (sum > best) {
                best = sum;
                if (x) {
                    *x = i;
                }

                if (y) {
                    *y = j;
                }
            }
        }
    }

    return best;
}

/** Count the pixels where a coated capture is brighter than an uncoated one by at least half
 * of its largest difference, the area of the highlight of the coat at half its height.
 *
 * @param coated The capture with a coat
 * @param plain The same capture without one
 * @param peak Receives the largest difference of the channel sums
 * @return The count
 */
static int coat_half_width(
    const unsigned char *coated,
    const unsigned char *plain,
    int *peak)
{
    int best = 0, count = 0, i, j;
    for (j = 0; j < COAT_H; j ++) {
        for (i = 0; i < COAT_W; i ++) {
            int d = coat_sum(coated, i, j) - coat_sum(plain, i, j);
            best = d > best ? d : best;
        }
    }

    for (j = 0; j < COAT_H; j ++) {
        for (i = 0; i < COAT_W; i ++) {
            count += 2 * (coat_sum(coated, i, j) - coat_sum(plain, i, j)) >= best;
        }
    }

    *peak = best;

    return count;
}

/** Check that two clear coat captures are identical and free them.
 *
 * @param a The first capture
 * @param b The second capture
 */
static void coat_expect_same(
    unsigned char *a,
    unsigned char *b)
{
    test_int(memcmp(a, b, COAT_W * COAT_H * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);
}

/** Check whether a pixel is within a tolerance of a color.
 *
 * @param p The pixel
 * @param r Red
 * @param g Green
 * @param b Blue
 * @param tolerance Largest channel difference
 * @return Whether every channel is within the tolerance
 */
static bool coat_near(
    const unsigned char *p,
    int r,
    int g,
    int b,
    int tolerance)
{
    return abs((int)p[0] - r) <= tolerance && abs((int)p[1] - g) <= tolerance &&
        abs((int)p[2] - b) <= tolerance;
}

void ClearCoat_clear_coat_highlight(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 1, .roughness = 0.1f};
    unsigned char *plain = coat_highlight(NULL, 100, true, "coat_plain.png");
    unsigned char *coated = coat_highlight(&coat, 100, true, "coat_coated.png");
    int x = 0, y = 0, i, j;
    int plain_peak = coat_peak(plain, 0, COAT_W, NULL, NULL);
    int coated_peak = coat_peak(coated, 0, COAT_W, &x, &y);
    test_assert(coated_peak > 700);
    test_assert(coated_peak > plain_peak + 200);
    test_assert(abs(x - 64) <= 2);
    test_assert(abs(y - 32) <= 2);

    test_assert(coat_sum(coated, x + 6, y) < coated_peak - 200);

    int far[4][2] = {{48, 16}, {80, 16}, {48, 48}, {80, 48}};
    for (i = 0; i < 4; i ++) {
        const unsigned char *a = &plain[(far[i][1] * COAT_W + far[i][0]) * 4];
        const unsigned char *b = &coated[(far[i][1] * COAT_W + far[i][0]) * 4];
        test_assert(a[0] > 20);
        for (j = 0; j < 3; j ++) {
            test_assert(abs((int)a[j] - (int)b[j]) <= 8);
        }
    }

    stbi_image_free(plain);
    stbi_image_free(coated);
}

void ClearCoat_clear_coat_strength_zero_unchanged(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 0, .roughness = 0.3f};
    coat_expect_same(coat_highlight(NULL, 100, true, "coat_zero_plain.png"),
        coat_highlight(&coat, 100, true, "coat_zero.png"));
}

void ClearCoat_clear_coat_roughness_widens(void) {
    // materials/pbr
    ClearCoat smooth = {.strength = 1, .roughness = 0.1f};
    ClearCoat rough = {.strength = 1, .roughness = 0.5f};
    unsigned char *plain = coat_highlight(NULL, 5, true, "coat_widens_plain.png");
    unsigned char *a = coat_highlight(&smooth, 5, true, "coat_smooth.png");
    unsigned char *b = coat_highlight(&rough, 5, true, "coat_rough.png");
    int smooth_peak = 0, rough_peak = 0;
    int smooth_width = coat_half_width(a, plain, &smooth_peak);
    int rough_width = coat_half_width(b, plain, &rough_peak);
    test_assert(smooth_peak > 300);
    test_assert(rough_peak > 30);
    test_assert(rough_peak < smooth_peak / 2);
    test_assert(rough_width > smooth_width * 2);

    stbi_image_free(plain);
    stbi_image_free(a);
    stbi_image_free(b);
}

/** Render a saturated red quad seen at a grazing angle of 75 degrees from its normal, lit by a
 * directional light from behind it and to the side, away from the mirror direction of the view,
 * so it shows no highlight of the coat.
 *
 * @param coat The ClearCoat of the quad, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* coat_grazing(
    const ClearCoat *coat,
    const char *name)
{
    ecs_world_t *world = coat_world(true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, -4});
    ecs_set(world, e, Rotation, {-75.0f * GLM_PIf / 180.0f, 0, 0});
    ecs_set(world, e, Quad, {4, 8});
    ecs_set(world, e, Rgb, {255, 20, 20});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    if (coat) {
        ecs_set_ptr(world, e, ClearCoat, coat);
    }

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5824f, -0.4997f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    unsigned char *px = coat_render(world, name);
    ecs_fini(world);

    return px;
}

void ClearCoat_clear_coat_attenuates_base(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 1, .roughness = 0};
    unsigned char *plain = coat_grazing(NULL, "coat_grazing_plain.png");
    unsigned char *coated = coat_grazing(&coat, "coat_grazing.png");
    int x, y, plain_red = 0, coated_red = 0;
    for (y = 31; y <= 33; y ++) {
        for (x = 60; x <= 68; x ++) {
            const unsigned char *a = &plain[(y * COAT_W + x) * 4];
            const unsigned char *b = &coated[(y * COAT_W + x) * 4];
            test_assert(a[0] > a[1] + 40);
            test_assert(b[0] <= a[0]);
            plain_red += a[0];
            coated_red += b[0];
        }
    }

    test_assert(plain_red > 27 * 60);
    test_assert(coated_red + 27 * 3 < plain_red);

    stbi_image_free(plain);
    stbi_image_free(coated);
}

/** Render a rough black dielectric sphere at the origin in a view that samples reflection
 * probes, with a probe that captures an enclosure of red emissive walls whose wall toward +z,
 * the one the center of the sphere reflects back toward the camera, is green.
 *
 * @param coat The ClearCoat of the sphere, NULL for none
 * @return The center pixel, red, green and blue
 */
static void coat_probe(
    const ClearCoat *coat,
    int out[3])
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 32, .budget_ms = 1,
        .scene_probe = false});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    int32_t i, shell;
    for (shell = 0; shell < 2; shell ++) {
        float distance = shell ? 1.4f : 1.1f, edge = shell ? 3.2f : 2.6f;
        for (i = 0; i < 6; i ++) {
            int32_t axis = i / 2;
            float position[3] = {6, 0, 0}, size[3] = {edge, edge, edge};
            position[axis] += i % 2 ? -distance : distance;
            size[axis] = 0.2f;
            ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, wall, Position, {position[0], position[1], position[2]});
            ecs_set(world, wall, Box, {size[0], size[1], size[2]});
            ecs_set(world, wall, Rgb, {0, 0, 0});
            if (!shell) {
                bool front = axis == 2 && !(i % 2);
                ecs_set(world, wall, Emissive, {.strength = 2,
                    .color = front ? (Rgb){0, 255, 0} : (Rgb){255, 0, 0}});
            }
        }
    }

    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 0, 0});
    ecs_set(world, probe, ReflectionProbe, {.extent = {3, 3, 3}, .offset = {6, 0, 0}});

    ecs_entity_t ball = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, ball, Position, {0, 0, 0});
    ecs_set(world, ball, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    ecs_set(world, ball, Rgb, {0, 0, 0});
    ecs_set(world, ball, PbrMaterial, {.metallic = 0, .roughness = 1});
    if (coat) {
        ecs_set_ptr(world, ball, ClearCoat, coat);
    }

    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 4});
    ecs_set(world, camera, Camera, {.fov = 30, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .lighting = {.enabled = true, .reflections = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "coat_probe.png",
        .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/coat_probe.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 48);
    const unsigned char *p = &px[(24 * 64 + 32) * 4];
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void ClearCoat_clear_coat_indirect(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 1, .roughness = 0};
    int plain[3], coated[3];
    coat_probe(NULL, plain);
    coat_probe(&coat, coated);

    test_assert(coated[1] > plain[1] + 15);
    test_assert(coated[1] > coated[0]);
    test_assert(plain[1] < plain[0] + 10);
}

/** Write a tangent space normal map of vertical stripes that tilt the normal 37 degrees left
 * and right in turn and load it.
 *
 * @param world The world
 * @param path Receives the path of the PNG, freed by the caller after the world
 * @return The texture entity
 */
static ecs_entity_t coat_normal_map(
    ecs_world_t *world,
    char **path)
{
    uint8_t px[8 * 8 * 4];
    int32_t i;
    *path = flecs_asprintf("%s/coat_normal.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) % 2 == 0;
        px[i * 4 + 0] = left ? 51 : 204;
        px[i * 4 + 1] = 128;
        px[i * 4 + 2] = 230;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(*path, 8, 8, 4, px, 8 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = *path});

    return tex;
}

/** Render a flat coated quad under a point light, with or without a strong normal map.
 *
 * @param mapped Whether the quad has the normal map
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* coat_mapped(
    bool mapped,
    const char *name)
{
    ClearCoat coat = {.strength = 1, .roughness = 0.4f};
    char *path = NULL;
    ecs_world_t *world = coat_world(true);
    ecs_entity_t quad = coat_quad(world, 0, &coat);
    coat_light(world, 0, 100);
    ecs_entity_t tex = 0;
    if (mapped) {
        tex = coat_normal_map(world, &path);
        ecs_set(world, quad, PbrTextures, {.normal = tex});
    }

    int32_t frames = 0;
    while (tex && frames < 100 &&
        ecs_get(world, tex, TextureStatus)->state == TextureLoading)
    {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_assert(!tex || ecs_get(world, tex, TextureStatus)->state == TextureReady);
    unsigned char *px = coat_render(world, name);
    ecs_fini(world);
    ecs_os_free(path);

    return px;
}

void ClearCoat_clear_coat_ignores_normal_map(void) {
    // materials/pbr
    unsigned char *flat = coat_mapped(false, "coat_flat.png");
    unsigned char *bumpy = coat_mapped(true, "coat_bumpy.png");
    int fx = 0, fy = 0, bx = 0, by = 0, i, differ = 0, inside = 0;
    coat_peak(flat, 0, COAT_W, &fx, &fy);
    coat_peak(bumpy, 0, COAT_W, &bx, &by);
    test_assert(abs(fx - bx) <= 1);
    test_assert(abs(fy - by) <= 1);

    for (i = 0; i < COAT_W * COAT_H; i ++) {
        bool a = flat[i * 4] + flat[i * 4 + 1] + flat[i * 4 + 2] > 600;
        bool b = bumpy[i * 4] + bumpy[i * 4 + 1] + bumpy[i * 4 + 2] > 600;
        differ += a != b;
        inside += a;
    }

    test_assert(inside > 4);
    test_assert(differ <= inside / 4 + 2);

    test_assert(memcmp(flat, bumpy, COAT_W * COAT_H * 4) != 0);

    stbi_image_free(flat);
    stbi_image_free(bumpy);
}

/** Render an emissive black quad in a lit view without any light.
 *
 * @param coat The ClearCoat of the quad, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* coat_emissive(
    const ClearCoat *coat,
    const char *name)
{
    ecs_world_t *world = coat_world(true);
    ecs_entity_t e = coat_quad(world, 0, coat);
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {200, 100, 50}});
    unsigned char *px = coat_render(world, name);
    ecs_fini(world);

    return px;
}

void ClearCoat_clear_coat_emission_not_attenuated(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 1, .roughness = 0};
    unsigned char *plain = coat_emissive(NULL, "coat_emissive_plain.png");
    unsigned char *coated = coat_emissive(&coat, "coat_emissive.png");
    const unsigned char *p = &coated[(32 * COAT_W + 64) * 4];
    test_assert(coat_near(p, 200, 100, 50, 2));
    p = &coated[(20 * COAT_W + 52) * 4];
    test_assert(coat_near(p, 200, 100, 50, 2));
    coat_expect_same(plain, coated);
}

void ClearCoat_clear_coat_ignored_by_unlit(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 1, .roughness = 0};
    coat_expect_same(coat_highlight(NULL, 100, false, "coat_unlit_plain.png"),
        coat_highlight(&coat, 100, false, "coat_unlit.png"));
}

/** Render a 2D rectangle in a lit view under a point light.
 *
 * @param coat The ClearCoat of the rectangle, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* coat_rect(
    const ClearCoat *coat,
    const char *name)
{
    ecs_world_t *world = coat_world(true);
    coat_light(world, 0, 100);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {4, 2});
    ecs_set(world, e, Rgb, {200, 120, 40});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
    if (coat) {
        ecs_set_ptr(world, e, ClearCoat, coat);
    }

    unsigned char *px = coat_render(world, name);
    ecs_fini(world);

    return px;
}

void ClearCoat_clear_coat_ignored_by_2d(void) {
    // materials/pbr
    ClearCoat coat = {.strength = 1, .roughness = 0};
    unsigned char *plain = coat_rect(NULL, "coat_rect_plain.png");
    test_assert(coat_sum(plain, 64, 32) > 30);
    coat_expect_same(plain, coat_rect(&coat, "coat_rect.png"));
}

void ClearCoat_clear_coat_prefab(void) {
    // materials/pbr
    ecs_world_t *world = coat_world(true);
    coat_light(world, -2.5f, 100);
    coat_light(world, 2.5f, 100);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Quad, {4, 4});
    ecs_set(world, prefab, Rgb, {40, 40, 40});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, prefab, ClearCoat, {.strength = 1, .roughness = 0.1f});
    ecs_entity_t shared = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, shared, Position, {-2.5f, 0, 0});
    ecs_entity_t own = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, own, Position, {2.5f, 0, 0});
    ecs_set(world, own, ClearCoat, {.strength = 1, .roughness = 0.1f});
    test_assert(ecs_owns(world, own, ClearCoat));
    test_assert(!ecs_owns(world, shared, ClearCoat));

    unsigned char *px = coat_render(world, "coat_prefab.png");
    int left = coat_peak(px, 0, 64, NULL, NULL);
    int right = coat_peak(px, 64, COAT_W, NULL, NULL);
    test_assert(left > 700);
    test_assert(right > 700);
    stbi_image_free(px);

    ecs_set(world, prefab, ClearCoat, {.strength = 0, .roughness = 0.1f});
    px = coat_render(world, "coat_prefab_off.png");
    left = coat_peak(px, 0, 64, NULL, NULL);
    right = coat_peak(px, 64, COAT_W, NULL, NULL);
    test_assert(left < 400);
    test_assert(right > 700);
    stbi_image_free(px);

    ecs_fini(world);
}

void ClearCoat_clear_coat_change_and_remove(void) {
    // materials/pbr
    unsigned char *before = coat_highlight(NULL, 100, true, "coat_before.png");
    int base = coat_peak(before, 0, COAT_W, NULL, NULL);

    ecs_world_t *world = coat_world(true);
    ecs_entity_t quad = coat_quad(world, 0, &(ClearCoat){.strength = 1, .roughness = 0.1f});
    coat_light(world, 0, 100);
    unsigned char *px = coat_render(world, "coat_change.png");
    test_assert(coat_peak(px, 0, COAT_W, NULL, NULL) > base + 200);
    stbi_image_free(px);

    ecs_set(world, quad, ClearCoat, {.strength = 1, .roughness = 0.6f});
    px = coat_render(world, "coat_change_rough.png");
    int rough = coat_peak(px, 0, COAT_W, NULL, NULL);
    test_assert(rough < 700);
    test_assert(rough > base);
    stbi_image_free(px);

    ecs_remove(world, quad, ClearCoat);
    px = coat_render(world, "coat_change_removed.png");
    test_int(memcmp(px, before, COAT_W * COAT_H * 4), 0);
    stbi_image_free(px);
    ecs_fini(world);

    world = coat_world(true);
    quad = coat_quad(world, 0, &(ClearCoat){.strength = 0, .roughness = 0.1f});
    ecs_add(world, quad, DynamicMaterial);
    coat_light(world, 0, 100);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = COAT_W, .height = COAT_H,
        .write_to_file = "coat_dynamic.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/coat_dynamic.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(coat_peak(px, 0, COAT_W, NULL, NULL) < base + 30);
    stbi_image_free(px);

    ClearCoat *coat = ecs_get_mut(world, quad, ClearCoat);
    coat->strength = 1;
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(coat_peak(px, 0, COAT_W, NULL, NULL) > base + 200);
    stbi_image_free(px);

    ecs_os_free(path);
    ecs_fini(world);
    stbi_image_free(before);
}
