/**
 * @file Effects.c
 * @brief Tests for the effect chain of a view: bloom, tonemap, exposure, fog, SMAA, color
 * grading, vignette, chromatic aberration and depth of field, and for the ambient occlusion
 * (Gtao) of the indirect light.
 *
 * Most cases render an unlit view (a C literal View) from 5 units in front of the origin into
 * a 64x64 capture; a unit cube at the origin covers columns 26 to 38 of the middle row.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "../texture/texture.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Create a world that renders headless, with a camera 5 units in front of the origin.
 *
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* effects_world(
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 0, 5});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});

    return world;
}

/** Create an unlit view with a black clear that runs one effect.
 *
 * @param world The world
 * @param camera The camera
 * @param effect The effect entity
 * @return The view
 */
static ecs_entity_t effects_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t effect)
{
    ecs_entity_t result = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = effect };
    ecs_set_ptr(world, result, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);

    return result;
}

/** Create a cube that emits a color and reflects nothing.
 *
 * @param world The world
 * @param x Position of its center along X
 * @param size Edge of the cube
 * @param strength Emissive strength
 * @param color Emissive color
 * @return The cube
 */
static ecs_entity_t effects_cube(
    ecs_world_t *world,
    float x,
    float size,
    float strength,
    Rgb color)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {x, 0, 0});
    ecs_set(world, result, Box, {size, size, size});
    ecs_set(world, result, Rgb, {0, 0, 0});
    ecs_set_ptr(world, result, Emissive, (&(Emissive){.strength = strength, .color = color}));

    return result;
}

/** Load the last capture of a 64x64 surface.
 *
 * @param world The world
 * @param file File name of the capture in the temporary folder
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* effects_load(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, 64);
    ecs_os_free(path);

    return result;
}

/** Encode a linear channel as an 8 bit sRGB target stores it.
 *
 * @param c The channel
 * @return The stored value
 */
static int effects_srgb8(
    float c)
{
    c = glm_clamp(c, 0.0f, 1.0f);
    c = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;

    return (int)(c * 255.0f + 0.5f);
}

/** Decode a stored 8 bit sRGB channel to linear.
 *
 * @param c The stored value
 * @return The linear channel
 */
static float effects_linear(
    unsigned char c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Get the saturation of a stored pixel in linear light: (max - min) / max.
 *
 * @param px The pixel
 * @return The saturation
 */
static float effects_saturation(
    const unsigned char *px)
{
    float r = effects_linear(px[0]), g = effects_linear(px[1]), b = effects_linear(px[2]);
    float hi = glm_max(r, glm_max(g, b)), lo = glm_min(r, glm_min(g, b));

    return hi > 0 ? (hi - lo) / hi : 0.0f;
}

void Effects_bloom_spreads_past_emissive_edge(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "bloom.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/bloom.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char before = px[(32 * 64 + 44) * 4];
    unsigned char center = px[(32 * 64 + 32) * 4];
    stbi_image_free(px);

    ecs_entity_t bloom = ecs_new(world);
    ecs_add(world, bloom, Bloom);
    View *v = ecs_get_mut(world, view, View);
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    ecs_modified(world, view, View);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char after = px[(32 * 64 + 44) * 4];
    test_int(center, 255);
    test_assert(before < 4);
    test_assert(after > before + 12);
    test_int(px[(32 * 64 + 32) * 4], 255);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_bloom_threshold_keeps_dim_edges(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_set(world, bloom, Bloom, {.intensity = 0.5f, .threshold = 1, .high_pass_frequency = 1,
        .max_mip_dimension = 512});
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 0.8f, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "dim.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dim.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 44) * 4] < 4);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    stbi_image_free(px);

    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 44) * 4] > 12);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_bloom_intensity_change_applies(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_set(world, bloom, Bloom, {.intensity = 0.1f, .threshold = 1, .high_pass_frequency = 1,
        .max_mip_dimension = 512});
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "change.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/change.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char weak = px[(32 * 64 + 44) * 4];
    stbi_image_free(px);

    ecs_set(world, bloom, Bloom, {.intensity = 0.8f, .threshold = 1, .high_pass_frequency = 1,
        .max_mip_dimension = 512});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char strong = px[(32 * 64 + 44) * 4];
    test_assert(weak > 2);
    test_assert(strong > weak + 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_tonemap_compresses_hdr(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    float strength[3] = {0.25f, 1.5f, 6.0f};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_entity_t cube = ecs_new(world);
        ecs_set(world, cube, Position, {(float)(i - 1) * 1.5f, 0, 0});
        ecs_set(world, cube, Box, {1, 1, 1});
        ecs_set(world, cube, Rgb, {0, 0, 0});
        ecs_set(world, cube, Emissive, {.strength = strength[i], .color = {255, 255, 255}});
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "tonemap.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/tonemap.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char dim = px[(32 * 64 + 14) * 4];
    test_int(px[(32 * 64 + 32) * 4], 255);
    test_int(px[(32 * 64 + 50) * 4], 255);
    stbi_image_free(px);

    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    View *v = ecs_get_mut(world, view, View);
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_modified(world, view, View);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char mid = px[(32 * 64 + 32) * 4];
    unsigned char bright = px[(32 * 64 + 50) * 4];
    test_assert(mid < 252);
    test_assert(bright < 255);
    test_assert(bright > mid);
    test_assert(abs((int)px[(32 * 64 + 14) * 4] - (int)dim) < 16);
    test_assert(px[(32 * 64 + 14) * 4] < mid);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_tonemap_keeps_hue_of_bright_colors(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 4, .color = {255, 120, 40}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "hue.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/hue.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 240);
    test_assert(c[0] > c[1] + 30);
    test_assert(c[1] > c[2]);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/* Mid gray, white and yellow cubes against black match the C twin of the shader; mid gray stays
 * a neutral gray a little darker than its input and white is not clipped. */
void Effects_tonemap_tony_mc_mapface_matches_lut(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_set(world, tonemap, Tonemap, {.operator_ = TonemapOperatorTonyMcMapface});
    effects_view(world, camera, tonemap);
    float hdr[3][3] = {{0.18f, 0.18f, 0.18f}, {1, 1, 1}, {1, 1, 0}};
    effects_cube(world, -1.5f, 1, 0.18f, (Rgb){255, 255, 255});
    effects_cube(world, 0, 1, 1, (Rgb){255, 255, 255});
    effects_cube(world, 1.5f, 1, 1, (Rgb){255, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "tony.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *error = NULL;
    uint32_t *lut = box_render_tony_load(BOX_RENDER_TONY_PATH, &error);
    test_assert(lut != NULL);
    unsigned char *px = effects_load(world, "tony.png");
    const int32_t columns[3] = {14, 32, 50};
    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        float expect[3];
        const unsigned char *p = &px[(32 * 64 + columns[i]) * 4];
        box_render_tonemap_tony(lut, hdr[i], expect);
        for (c = 0; c < 3; c ++) {
            test_assert(abs((int)p[c] - effects_srgb8(expect[c])) <= 3);
        }
    }

    const unsigned char *gray = &px[(32 * 64 + 14) * 4];
    test_assert(abs((int)gray[0] - (int)gray[1]) <= 2);
    test_assert(abs((int)gray[0] - (int)gray[2]) <= 2);
    test_assert(gray[0] < effects_srgb8(0.18f));
    test_assert(px[(32 * 64 + 32) * 4] < 250);

    stbi_image_free(px);
    ecs_os_free(lut);
    ecs_fini(world);
}

/* A bright yellow keeps less of its chroma with Tony McMapface than with PBR Neutral, and the
 * black clear stays black up to the dither. */
void Effects_tonemap_tony_desaturates_bright_colors(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    effects_view(world, camera, tonemap);
    effects_cube(world, 0, 1, 1, (Rgb){255, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "chroma.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    unsigned char *px = effects_load(world, "chroma.png");
    float neutral = effects_saturation(&px[(32 * 64 + 32) * 4]);
    stbi_image_free(px);

    ecs_set(world, tonemap, Tonemap, {.operator_ = TonemapOperatorTonyMcMapface});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = effects_load(world, "chroma.png");
    float tony = effects_saturation(&px[(32 * 64 + 32) * 4]);
    const unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(neutral > 0.95f);
    test_assert(tony < neutral - 0.05f);
    test_assert(c[0] > c[2] + 60);
    test_assert(c[1] > c[2] + 60);

    const unsigned char *k = &px[(32 * 64 + 50) * 4];
    test_assert(k[0] < 12 && k[1] < 12 && k[2] < 12);

    stbi_image_free(px);
    ecs_fini(world);
}

/* The first frame already has the exposure of the dim wall, 2.3 stops up. From frame 11 the
 * wall is 10 times brighter and the exposure comes down toward 0 stops at the speed down. */
void Effects_exposure_adapts_over_frames(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t exposure = ecs_new(world);
    ecs_add(world, exposure, AutoExposure);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_set(world, tonemap, Tonemap, {.auto_exposure = exposure});
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Box, {20, 20, 1});
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, Emissive, {.strength = 0.02f, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "adapt.png",
        .resolution_scale = 1});
    char *path = NULL;
    unsigned char seen[5] = {0};
    int32_t frame, sample = 0;
    for (frame = 1; frame <= 180; frame ++) {
        if (frame == 11) {
            ecs_set(world, wall, Emissive, {.strength = 0.2f, .color = {255, 255, 255}});
        }

        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        if (frame != 1 && frame != 10 && frame != 11 && frame != 60 && frame != 180) {
            continue;
        }

        if (!path) {
            path = flecs_asprintf("%s/adapt.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        }

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        seen[sample ++] = px[(32 * 64 + 32) * 4];
        stbi_image_free(px);
    }

    test_assert(abs((int)seen[0] - (int)seen[1]) < 3);
    test_assert(seen[0] > 40);
    test_assert(seen[2] > seen[1] + 60);
    test_assert(seen[2] > seen[3] + 10);
    test_assert(seen[3] > seen[4] + 10);

    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_exposure_deterministic_with_fixed_delta(void) {
    // lighting/effects
    unsigned char result[2] = {0};
    int32_t run;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 5});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t exposure = ecs_new(world);
        ecs_set(world, exposure, AutoExposure, {.min_brightness = 0.2f, .max_brightness = 0.2f,
            .min_log_luma = -8, .max_log_luma = 4, .speed_up = 2, .speed_down = 2,
            .high_percentile = 1});
        ecs_entity_t view = ecs_new(world);
        View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
        ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = exposure };
        ecs_set_ptr(world, view, View, &value);
        ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
        ecs_entity_t wall = ecs_new(world);
        ecs_set(world, wall, Box, {20, 20, 1});
        ecs_set(world, wall, Position, {0, 0, 0});
        ecs_set(world, wall, Rgb, {0, 0, 0});
        ecs_set(world, wall, Emissive, {.strength = 2, .color = {255, 255, 255}});
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "fixed.png",
            .resolution_scale = 1});
        int32_t frame;
        for (frame = 0; frame < 20; frame ++) {
            ecs_progress(world, 1.0f / 60);
        }

        box_render_sync(world);
        char *path = flecs_asprintf("%s/fixed.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        result[run] = px[(32 * 64 + 32) * 4];
        stbi_image_free(px);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_assert(result[0] < 255);
    test_int(result[0], result[1]);
}

/* The exposure scales a wall of 3 to the target brightness of 0.2 in the first frame, where
 * adapting from 0 stops at this speed would take seconds. */
void Effects_exposure_first_frame_at_target(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t exposure = ecs_new(world);
    ecs_set(world, exposure, AutoExposure, {.min_brightness = 0.2f, .max_brightness = 0.2f,
        .min_log_luma = -8, .max_log_luma = 4, .speed_up = 0.5f, .speed_down = 0.5f,
        .high_percentile = 1});
    effects_view(world, camera, exposure);
    ecs_entity_t wall = effects_cube(world, 0, 1, 3, (Rgb){255, 255, 255});
    ecs_set(world, wall, Box, {20, 20, 1});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "first.png",
        .resolution_scale = 1});
    unsigned char seen[2] = {0};
    int32_t frame;
    for (frame = 1; frame <= 30; frame ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        if (frame == 1 || frame == 30) {
            unsigned char *px = effects_load(world, "first.png");
            seen[frame == 1 ? 0 : 1] = px[(32 * 64 + 32) * 4];
            stbi_image_free(px);
        }
    }

    test_assert(abs((int)seen[0] - effects_srgb8(0.2f)) <= 3);
    test_assert(abs((int)seen[0] - (int)seen[1]) <= 1);

    ecs_fini(world);
}

/* A wall that fills the view and a cube in front of black that covers less than a fifth of it
 * get the same exposure: the black pixels, in bin 0, are left out of the histogram. */
void Effects_exposure_ignores_pixels_below_min_log_luma(void) {
    // lighting/effects
    unsigned char result[2] = {0};
    int32_t run;
    for (run = 0; run < 2; run ++) {
        ecs_entity_t camera;
        ecs_world_t *world = effects_world(&camera);
        ecs_entity_t exposure = ecs_new(world);
        ecs_set(world, exposure, AutoExposure, {.min_brightness = 0.1f, .max_brightness = 0.1f,
            .min_log_luma = -8, .max_log_luma = 4, .speed_up = 3, .speed_down = 1,
            .low_percentile = 0, .high_percentile = 1});
        effects_view(world, camera, exposure);

        ecs_entity_t shape = effects_cube(world, 0, 1.5f, 0.5f, (Rgb){255, 255, 255});
        if (!run) {
            ecs_set(world, shape, Box, {20, 20, 1});
        }

        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "bin0.png",
            .resolution_scale = 1});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        unsigned char *px = effects_load(world, "bin0.png");
        result[run] = px[(32 * 64 + 32) * 4];
        if (run) {
            test_int(px[(4 * 64 + 4) * 4], 0);
        }

        stbi_image_free(px);
        ecs_fini(world);
    }

    test_assert(abs((int)result[0] - effects_srgb8(0.1f)) <= 3);
    test_assert(abs((int)result[0] - (int)result[1]) <= 1);
}

void Effects_fog_darkens_with_distance(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t near_box = ecs_new(world);
    ecs_set(world, near_box, Position, {-1, 0, 0});
    ecs_set(world, near_box, Box, {1, 1, 1});
    ecs_set(world, near_box, Rgb, {0, 0, 0});
    ecs_set(world, near_box, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_entity_t far_box = ecs_new(world);
    ecs_set(world, far_box, Position, {3, 0, -20});
    ecs_set(world, far_box, Box, {4, 4, 4});
    ecs_set(world, far_box, Rgb, {0, 0, 0});
    ecs_set(world, far_box, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "fog.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fog.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 20) * 4] > 250);
    test_assert(px[(32 * 64 + 39) * 4] > 250);
    stbi_image_free(px);

    ecs_entity_t fog = ecs_new(world);
    ecs_set(world, fog, HeightFog, {.density = 0.1f, .falloff = 0, .max_opacity = 1,
        .color = {20, 20, 20}});
    View *v = ecs_get_mut(world, view, View);
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = fog };
    ecs_modified(world, view, View);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char near_px = px[(32 * 64 + 20) * 4];
    unsigned char far_px = px[(32 * 64 + 39) * 4];
    test_assert(near_px < 245);
    test_assert(far_px + 40 < near_px);
    test_assert(px[(4 * 64 + 4) * 4] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_fog_lit_by_scene_sun(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t fog = ecs_new(world);
    ecs_set(world, fog, HeightFog, {.density = 0.2f, .falloff = 0, .max_opacity = 1,
        .color = {255, 255, 255}, .sun_inscatter = 1, .ambient_inscatter = 1});
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = fog };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 0.2f * GLM_PIf});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "sun.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/sun.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char dim = px[(32 * 64 + 32) * 4];
    stbi_image_free(px);

    ecs_set(world, sun, DirectionalLight, {.intensity = 0.8f * GLM_PIf});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char bright = px[(32 * 64 + 32) * 4];
    test_assert(dim > 20);
    test_assert(bright > dim + 30);
    stbi_image_free(px);

    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] < 4);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Create a headless world with the crease of the Gtao cases: a white floor meeting a white wall
 * of a width, seen from above by a camera 4 units in front of the wall. The crease is near the
 * middle row, the floor 2.7 units from the wall at the bottom rows.
 *
 * A white ambient light of an intensity lights it, and a directional light of an intensity, or
 * when that is 0 a point light of intensity 0 out of reach, which replaces the default sun.
 *
 * @param ambient Intensity of the ambient light
 * @param sun Intensity of the directional light, 0 for none
 * @param wall_width Width of the wall
 * @param camera Receives the camera
 * @param floor Receives the floor
 * @param wall Receives the wall
 * @return The world
 */
static ecs_world_t* gtao_world(
    float ambient,
    float sun,
    float wall_width,
    ecs_entity_t *camera,
    ecs_entity_t *floor,
    ecs_entity_t *wall)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = ambient});
    ecs_entity_t light = ecs_new(world);
    if (sun > 0) {
        ecs_set(world, light, Rotation, {1.2f, 0, 0});
        ecs_set(world, light, DirectionalLight, {.intensity = sun * GLM_PIf});
    } else {
        ecs_set(world, light, Position, {1000, 1000, 1000});
        ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});
    }

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 1.5f, 3});
    ecs_set(world, *camera, Rotation, {-0.35f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});
    *floor = ecs_new(world);
    ecs_set(world, *floor, Position, {0, -0.1f, 0});
    ecs_set(world, *floor, Box, {10, 0.2f, 10});
    ecs_set(world, *floor, Rgb, {255, 255, 255});
    *wall = ecs_new(world);
    ecs_set(world, *wall, Position, {0, 2, -1.1f});
    ecs_set(world, *wall, Box, {wall_width, 4, 0.2f});
    ecs_set(world, *wall, Rgb, {255, 255, 255});

    return world;
}

/** Set the camera, lighting and effects of a view with a black clear.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param lit Whether the view is lit
 * @param effects The effect entities, in order
 * @param count Number of effects
 */
static void gtao_view_set(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    bool lit,
    const ecs_entity_t *effects,
    int32_t count)
{
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = lit, .lighting.depth_prepass = true};
    int32_t i;
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, count);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = effects[i] };
    }

    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
}

/** Create a Gtao effect entity.
 *
 * @param world The world
 * @param radius World radius of the occlusion
 * @param power Final value power, the strength
 * @param half Whether it computes at half resolution
 * @return The entity
 */
static ecs_entity_t gtao_effect(
    ecs_world_t *world,
    float radius,
    float power,
    bool half)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Gtao, {.radius = radius, .falloff_range = 0.615f,
        .final_value_power = power, .quality = GtaoHigh, .denoise_passes = 1,
        .half_resolution = half});

    return result;
}

/** Render a frame into the 64x64 capture of a surface and copy it.
 *
 * @param world The world
 * @param file File name of the capture of the surface
 * @return The RGBA pixels, freed with ecs_os_free
 */
static unsigned char* gtao_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    unsigned char *px = effects_load(world, file);
    unsigned char *result = ecs_os_malloc(64 * 64 * 4);
    ecs_os_memcpy(result, px, 64 * 64 * 4);
    stbi_image_free(px);

    return result;
}

/** Add the 64x64 surface that captures the views.
 *
 * @param world The world
 * @param file File name of the capture
 */
static void gtao_surface(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
}

/** Get the largest difference of a color channel between two captures in a region.
 *
 * @param a The first capture
 * @param b The second capture
 * @param x0 First column
 * @param y0 First row
 * @param x1 Last column, included
 * @param y1 Last row, included
 * @return The difference
 */
static int gtao_diff(
    const unsigned char *a,
    const unsigned char *b,
    int x0,
    int y0,
    int x1,
    int y1)
{
    int x, y, c, result = 0;
    for (y = y0; y <= y1; y ++) {
        for (x = x0; x <= x1; x ++) {
            for (c = 0; c < 3; c ++) {
                int d = abs((int)a[(y * 64 + x) * 4 + c] - (int)b[(y * 64 + x) * 4 + c]);
                result = d > result ? d : result;
            }
        }
    }

    return result;
}

/** Get how much darker the second capture is than the first at most, in the red channel of
 * the middle column between two rows: the crease.
 *
 * @param before The capture without Gtao
 * @param after The capture with Gtao
 * @param y0 First row
 * @param y1 Last row, included
 * @return The darkening, negative when every pixel got brighter
 */
static int gtao_darkening(
    const unsigned char *before,
    const unsigned char *after,
    int y0,
    int y1)
{
    int y, result = -255;
    for (y = y0; y <= y1; y ++) {
        int d = (int)before[(y * 64 + 32) * 4] - (int)after[(y * 64 + 32) * 4];
        result = d > result ? d : result;
    }

    return result;
}

void Effects_gtao_darkens_crease(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_crease.png");
    unsigned char *before = gtao_frame(world, "gtao_crease.png");

    ecs_entity_t gtao = gtao_effect(world, 0.5f, 2.2f, true);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_crease.png");

    test_assert(before[(62 * 64 + 32) * 4] > 60);
    test_assert(before[(62 * 64 + 32) * 4] < 250);
    test_assert(gtao_darkening(before, after, 16, 48) > 10);
    test_assert(gtao_diff(before, after, 8, 40, 56, 47) <= 2);
    test_assert(gtao_diff(before, after, 24, 0, 40, 3) <= 2);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void Effects_gtao_spares_direct_light(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0, 0.5f, 10, &camera, &floor, &wall);
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_direct.png");
    unsigned char *before = gtao_frame(world, "gtao_direct.png");

    ecs_entity_t gtao = gtao_effect(world, 1, 4, true);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_direct.png");

    test_assert(before[(62 * 64 + 32) * 4] > 20);
    test_assert(gtao_diff(before, after, 0, 0, 63, 63) <= 1);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void Effects_gtao_spares_emission_and_blended(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t lamp = ecs_new(world);
    ecs_set(world, lamp, Position, {0, 0.5f, -0.5f});
    ecs_set(world, lamp, Box, {1, 1, 1});
    ecs_set(world, lamp, Rgb, {0, 0, 0});
    ecs_set(world, lamp, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, lamp, Emissive, {.strength = 1, .color = {255, 128, 64}});
    ecs_entity_t pane = ecs_new(world);
    ecs_set(world, pane, Position, {0, 0.5f, 0.4f});
    ecs_set(world, pane, Quad, {0.6f, 0.6f});
    ecs_set(world, pane, Rgb, {128, 160, 255});
    ecs_set(world, pane, Opacity, {0.5f});
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_emission.png");
    unsigned char *before = gtao_frame(world, "gtao_emission.png");

    ecs_entity_t gtao = gtao_effect(world, 1, 4, true);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_emission.png");

    test_assert(before[(32 * 64 + 32) * 4] > 60);
    test_assert(gtao_diff(before, after, 30, 30, 34, 34) <= 1);
    test_assert(gtao_diff(before, after, 32, 24, 32, 25) <= 1);
    test_assert(gtao_diff(before, after, 25, 32, 25, 32) <= 1);
    int x, y, darkest = 0;
    for (y = 0; y < 64; y ++) {
        for (x = 0; x < 64; x ++) {
            int d = (int)before[(y * 64 + x) * 4 + 2] - (int)after[(y * 64 + x) * 4 + 2];
            darkest = d > darkest ? d : darkest;
        }
    }

    test_assert(darkest > 8);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void Effects_gtao_unlit_view_unchanged(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, false, NULL, 0);
    gtao_surface(world, "gtao_unlit.png");
    unsigned char *before = gtao_frame(world, "gtao_unlit.png");

    ecs_entity_t gtao = gtao_effect(world, 1, 4, true);
    gtao_view_set(world, view, camera, false, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_unlit.png");

    test_assert(before[(62 * 64 + 32) * 4] > 250);
    test_assert(gtao_diff(before, after, 0, 0, 63, 63) == 0);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void Effects_gtao_ortho_view_unchanged(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_set(world, camera, Position, {0, 3, 6});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 4, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_ortho.png");
    unsigned char *before = gtao_frame(world, "gtao_ortho.png");

    ecs_entity_t gtao = gtao_effect(world, 1, 4, true);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_ortho.png");

    test_assert(before[(62 * 64 + 32) * 4] > 60);
    test_assert(gtao_diff(before, after, 0, 0, 63, 63) == 0);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void Effects_gtao_combines_orm_min(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    char *file = flecs_asprintf("%s/gtao_orm_half.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t texels[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        texels[i * 4 + 0] = 128;
        texels[i * 4 + 1] = 255;
        texels[i * 4 + 2] = 255;
        texels[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, texels, 4 * 4) != 0);
    ecs_entity_t orm = ecs_new(world);
    ecs_set(world, orm, TexturePath, {.path = file});
    ecs_set(world, floor, PbrTextures, {.orm = orm});
    ecs_set(world, wall, PbrTextures, {.orm = orm});
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_orm.png");
    const TextureStatus *status = NULL;
    for (i = 0; i < 200 && (!status || status->state != TextureReady); i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        status = ecs_get(world, orm, TextureStatus);
    }

    test_assert(status != NULL);
    test_int(status->state, TextureReady);
    ecs_progress(world, 1.0f / 60);
    unsigned char *before = gtao_frame(world, "gtao_orm.png");

    ecs_entity_t gtao = gtao_effect(world, 1, 4, true);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_orm.png");

    test_assert(before[(62 * 64 + 32) * 4] > 30);
    test_assert(gtao_diff(before, after, 24, 60, 40, 63) <= 1);
    test_assert(gtao_darkening(before, after, 16, 48) > 4);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_os_free(file);
    ecs_fini(world);
}

void Effects_gtao_specular_occlusion(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 0.2f});
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_specular.png");
    unsigned char *before = gtao_frame(world, "gtao_specular.png");

    ecs_entity_t gtao = gtao_effect(world, 1, 4, true);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    unsigned char *after = gtao_frame(world, "gtao_specular.png");

    test_assert(before[(62 * 64 + 32) * 4] > 30);
    test_assert(gtao_diff(before, after, 8, 40, 56, 47) <= 2);
    test_assert(gtao_darkening(before, after, 34, 48) > 4);

    ecs_os_free(before);
    ecs_os_free(after);
    ecs_fini(world);
}

void Effects_gtao_half_resolution_close_to_full(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 2, &camera, &floor, &wall);
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, NULL, 0);
    gtao_surface(world, "gtao_half.png");
    unsigned char *none = gtao_frame(world, "gtao_half.png");

    ecs_entity_t full = gtao_effect(world, 1.5f, 2.2f, false);
    gtao_view_set(world, view, camera, true, &full, 1);
    unsigned char *at_full = gtao_frame(world, "gtao_half.png");

    ecs_entity_t half = gtao_effect(world, 1.5f, 2.2f, true);
    gtao_view_set(world, view, camera, true, &half, 1);
    unsigned char *at_half = gtao_frame(world, "gtao_half.png");

    test_assert(gtao_darkening(none, at_full, 16, 48) > 10);
    test_assert(gtao_darkening(none, at_half, 16, 48) > 10);
    test_assert(gtao_diff(at_full, at_half, 32, 16, 32, 48) <= 16);

    int x, left = -1, right = -1;
    for (x = 0; x < 64; x ++) {
        if (none[(8 * 64 + x) * 4] > 8) {
            right = x;
            if (left < 0) {
                left = x;
            }
        }
    }

    test_assert(left > 4);
    test_assert(right < 59);
    test_assert(gtao_diff(none, at_half, left - 3, 2, left + 3, 12) <= 2);
    test_assert(gtao_diff(none, at_half, right - 3, 2, right + 3, 12) <= 2);
    test_assert(gtao_diff(none, at_full, left - 3, 2, left + 3, 12) <= 2);
    test_assert(gtao_diff(none, at_full, right - 3, 2, right + 3, 12) <= 2);
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);

    ecs_os_free(none);
    ecs_os_free(at_full);
    ecs_os_free(at_half);
    ecs_fini(world);
}

void Effects_gtao_same_every_frame(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_entity_t gtao = gtao_effect(world, 0.5f, 2.2f, true);
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, &gtao, 1);
    gtao_surface(world, "gtao_frames.png");
    unsigned char *first = gtao_frame(world, "gtao_frames.png");
    unsigned char *second = gtao_frame(world, "gtao_frames.png");
    unsigned char *third = gtao_frame(world, "gtao_frames.png");

    test_assert(gtao_diff(first, second, 0, 0, 63, 63) == 0);
    test_assert(gtao_diff(second, third, 0, 0, 63, 63) == 0);

    ecs_os_free(first);
    ecs_os_free(second);
    ecs_os_free(third);
    ecs_fini(world);
}

void Effects_gtao_position_in_list(void) {
    // lighting/effects
    ecs_entity_t camera, floor, wall;
    ecs_world_t *world = gtao_world(0.5f, 0, 10, &camera, &floor, &wall);
    ecs_entity_t gtao = gtao_effect(world, 0.5f, 2.2f, true);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t first[2] = {gtao, tonemap};
    ecs_entity_t last[2] = {tonemap, gtao};
    ecs_entity_t view = ecs_new(world);
    gtao_view_set(world, view, camera, true, &tonemap, 1);
    gtao_surface(world, "gtao_order.png");
    unsigned char *none = gtao_frame(world, "gtao_order.png");
    gtao_view_set(world, view, camera, true, first, 2);
    unsigned char *listed_first = gtao_frame(world, "gtao_order.png");
    gtao_view_set(world, view, camera, true, last, 2);
    unsigned char *listed_last = gtao_frame(world, "gtao_order.png");

    test_assert(gtao_darkening(none, listed_first, 16, 48) > 8);
    test_assert(gtao_diff(listed_first, listed_last, 0, 0, 63, 63) == 0);

    ecs_os_free(none);
    ecs_os_free(listed_first);
    ecs_os_free(listed_last);
    ecs_fini(world);
}

void Effects_gtao_settings_clamped(void) {
    // lighting/effects
    Gtao out;
    box_render_gtao_settings(&(Gtao){.radius = -1, .falloff_range = -0.5f,
        .final_value_power = -2, .quality = (GtaoQuality)-1, .denoise_passes = -4,
        .half_resolution = false}, &out);
    test_flt(out.radius, 0);
    test_flt(out.falloff_range, 0);
    test_flt(out.final_value_power, 0);
    test_int(out.quality, GtaoHigh);
    test_int(out.denoise_passes, 0);
    test_bool(out.half_resolution, false);

    box_render_gtao_settings(&(Gtao){.radius = 20, .falloff_range = 3,
        .final_value_power = 8, .quality = (GtaoQuality)12, .denoise_passes = 9,
        .half_resolution = true}, &out);
    test_flt(out.radius, 20);
    test_flt(out.falloff_range, 1);
    test_flt(out.final_value_power, 8);
    test_int(out.quality, GtaoHigh);
    test_int(out.denoise_passes, 3);
    test_bool(out.half_resolution, true);

    box_render_gtao_settings(&(Gtao){.radius = 0.75f, .falloff_range = 0.3f,
        .final_value_power = 1.5f, .quality = GtaoUltra, .denoise_passes = 2,
        .half_resolution = true}, &out);
    test_flt(out.radius, 0.75f);
    test_flt(out.falloff_range, 0.3f);
    test_flt(out.final_value_power, 1.5f);
    test_int(out.quality, GtaoUltra);
    test_int(out.denoise_passes, 2);
}

void Effects_chain_order_matters(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_set(world, bloom, Bloom, {.intensity = 0.5f, .threshold = 1, .high_pass_frequency = 1,
        .max_mip_dimension = 512});
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 2);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, view, View, &value);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "order.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/order.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char bloom_first = px[(32 * 64 + 44) * 4];
    stbi_image_free(px);

    ecs_vec_get_t(&value.effects, ViewEffect, 0)->effect = tonemap;
    ecs_vec_get_t(&value.effects, ViewEffect, 1)->effect = bloom;
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char tonemap_first = px[(32 * 64 + 44) * 4];
    test_assert(bloom_first > 12);
    test_assert(tonemap_first < 4);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_chain_reconfigured_on_change(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 160, 80}});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -1, 0});
    ecs_set(world, floor, Box, {8, 0.2f, 8});
    ecs_set(world, floor, Rgb, {120, 140, 160});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "reconf.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/reconf.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *plain = ecs_os_malloc(64 * 64 * 4);
    ecs_os_memcpy(plain, px, 64 * 64 * 4);
    stbi_image_free(px);

    ecs_entity_t bloom = ecs_new(world);
    ecs_add(world, bloom, Bloom);
    ecs_entity_t fog = ecs_new(world);
    ecs_add(world, fog, HeightFog);
    View *v = ecs_get_mut(world, view, View);
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = fog };
    ecs_modified(world, view, View);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(memcmp(px, plain, 64 * 64 * 4) != 0);
    stbi_image_free(px);

    v = ecs_get_mut(world, view, View);
    ecs_vec_get_t(&v->effects, ViewEffect, 0)->enabled = false;
    ecs_vec_get_t(&v->effects, ViewEffect, 1)->enabled = false;
    ecs_modified(world, view, View);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(memcmp(px, plain, 64 * 64 * 4) == 0);
    stbi_image_free(px);

    v = ecs_get_mut(world, view, View);
    ecs_vec_get_t(&v->effects, ViewEffect, 0)->enabled = true;
    ecs_vec_get_t(&v->effects, ViewEffect, 1)->enabled = true;
    ecs_modified(world, view, View);
    ecs_delete(world, bloom);
    ecs_remove(world, fog, HeightFog);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(memcmp(px, plain, 64 * 64 * 4) == 0);
    stbi_image_free(px);

    ecs_add(world, fog, HeightFog);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(memcmp(px, plain, 64 * 64 * 4) != 0);
    stbi_image_free(px);

    v = ecs_get_mut(world, view, View);
    ecs_vec_clear(&v->effects);
    ecs_modified(world, view, View);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(memcmp(px, plain, 64 * 64 * 4) == 0);

    stbi_image_free(px);
    ecs_os_free(plain);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_view_without_clear_composites_over(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, View, {.camera = camera, .clear = {.enabled = true,
        .color = {200, 30, 30, 255}}});
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t overlay_camera = ecs_new(world);
    ecs_add_pair(world, overlay_camera, EcsChildOf, scene);
    ecs_set(world, overlay_camera, Position, {0, 0, 5});
    ecs_set(world, overlay_camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_add(world, bloom, Bloom);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t overlay = ecs_new(world);
    View value = {.camera = overlay_camera};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 2);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, overlay, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t cube = ecs_new(world);
    ecs_add_pair(world, cube, EcsChildOf, scene);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 0.5f, .color = {40, 220, 40}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "over.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/over.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *corner = &px[(4 * 64 + 4) * 4];
    unsigned char *center = &px[(32 * 64 + 32) * 4];
    test_int(corner[0], 200);
    test_int(corner[1], 30);
    test_int(corner[2], 30);
    test_assert(center[1] > 120);
    test_assert(center[0] < 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_zero_literal_applies_as_set(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_set(world, bloom, Bloom, {.threshold = 1});
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "zero.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/zero.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 44) * 4] < 4);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Effects_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Bloom);
    ecs_add(world, e, Gtao);
    ecs_add(world, e, HeightFog);
    ecs_add(world, e, AutoExposure);
    ecs_add(world, e, Tonemap);
    const Bloom *b = ecs_get(world, e, Bloom);
    test_flt(b->intensity, 0.3f);
    test_flt(b->low_frequency_boost, 0.7f);
    test_flt(b->low_frequency_boost_curvature, 0.95f);
    test_flt(b->high_pass_frequency, 1);
    test_flt(b->threshold, 1);
    test_flt(b->threshold_softness, 0);
    test_flt(b->firefly_clamp, 12);
    test_flt(b->temporal, 0);
    test_int(b->max_mip_dimension, 512);
    const Gtao *ao = ecs_get(world, e, Gtao);
    test_flt(ao->radius, 0.5f);
    test_flt(ao->falloff_range, 0.615f);
    test_flt(ao->final_value_power, 2.2f);
    test_int(ao->quality, GtaoHigh);
    test_int(ao->denoise_passes, 1);
    test_bool(ao->half_resolution, true);
    Gtao resolved;
    box_render_gtao_settings(ao, &resolved);
    test_flt(resolved.radius, 0.5f);
    test_flt(resolved.falloff_range, 0.615f);
    test_flt(resolved.final_value_power, 2.2f);
    test_int(resolved.quality, GtaoHigh);
    test_int(resolved.denoise_passes, 1);
    test_bool(resolved.half_resolution, true);
    const HeightFog *f = ecs_get(world, e, HeightFog);
    test_flt(f->density, 0.1f);
    test_flt(f->falloff, 0.3f);
    test_flt(f->base_height, 0);
    test_flt(f->max_opacity, 1);
    test_int(f->color.r, 191);
    test_int(f->color.g, 158);
    test_int(f->color.b, 140);
    test_flt(f->sun_inscatter, 1);
    test_flt(f->ambient_inscatter, 1);
    const AutoExposure *a = ecs_get(world, e, AutoExposure);
    test_flt(a->target_brightness, 0);
    test_flt(a->min_brightness, 0.1f);
    test_flt(a->max_brightness, 0.3f);
    test_flt(a->min_log_luma, -8);
    test_flt(a->max_log_luma, 4);
    test_flt(a->speed_up, 3);
    test_flt(a->speed_down, 1);
    test_flt(a->low_percentile, 0.5f);
    test_flt(a->high_percentile, 0.95f);
    test_flt(a->exposure_compensation, 0);
    test_flt(a->min_exposure, 0);
    test_flt(a->max_exposure, 0);
    test_uint(ecs_get(world, e, Tonemap)->auto_exposure, 0);
    test_int(ecs_get(world, e, Tonemap)->operator_, TonemapOperatorPbrNeutral);

    ecs_fini(world);
}

void Effects_script_entry_enabled_by_default(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "effects", "using box.view\nusing box.effects\n"
        "glow { Bloom: {} }\nmain { View: {effects: [{effect: glow}]} }\n", NULL) == 0);
    ecs_entity_t view = ecs_lookup(world, "main");
    ecs_entity_t glow = ecs_lookup(world, "glow");
    test_assert(view != 0);
    const View *v = ecs_get(world, view, View);
    test_int(ecs_vec_count(&v->effects), 1);
    test_bool(ecs_vec_first_t(&v->effects, ViewEffect)->enabled, true);
    test_uint(ecs_vec_first_t(&v->effects, ViewEffect)->effect, glow);
    test_bool(v->lighting.enabled, true);

    ecs_fini(world);
}

void Effects_bloom_temporal_keeps_last_frame(void) {
    // lighting/effects
    unsigned char glow[2] = {0};
    float temporal[2] = {0, 0.9f};
    int32_t run;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 5});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t bloom = ecs_new(world);
        ecs_set(world, bloom, Bloom, {.intensity = 0.5f, .threshold = 1,
            .temporal = temporal[run], .high_pass_frequency = 1, .max_mip_dimension = 512});
        ecs_entity_t view = ecs_new(world);
        View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
        ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = bloom };
        ecs_set_ptr(world, view, View, &value);
        ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
        ecs_entity_t cube = ecs_new(world);
        ecs_set(world, cube, Position, {0, 0, 0});
        ecs_set(world, cube, Box, {1, 1, 1});
        ecs_set(world, cube, Rgb, {0, 0, 0});
        ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "temporal.png",
            .resolution_scale = 1});
        int32_t frame;
        for (frame = 0; frame < 30; frame ++) {
            ecs_progress(world, 1.0f / 60);
        }

        ecs_set(world, cube, Position, {0, 100, 0});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        char *path = flecs_asprintf("%s/temporal.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        glow[run] = px[(32 * 64 + 40) * 4];
        stbi_image_free(px);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_assert(glow[0] < 4);
    test_assert(glow[1] > glow[0] + 8);
}

void Effects_follow_surface_resize(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_set(world, bloom, Bloom, {.intensity = 0.5f, .threshold = 1, .high_pass_frequency = 1,
        .max_mip_dimension = 512});
    ecs_entity_t gtao = ecs_new(world);
    ecs_add(world, gtao, Gtao);
    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 2);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = gtao };
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "resize.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, s, Surface, {.width = 128, .height = 64, .resolution_scale = 2,
        .write_to_file = "resize.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resize.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 128);
    test_int(h, 64);
    test_assert(px[(32 * 128 + 64) * 4] > 250);
    test_assert(px[(32 * 128 + 80) * 4] > 8);
    test_assert(px[(2 * 128 + 2) * 4] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Set the camera, clear and effects of a view, which renders unlit.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param clear Whether the view clears to black
 * @param effects The effect entities, in order
 * @param count Number of effects
 */
static void smaa_view_set(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    bool clear,
    const ecs_entity_t *effects,
    int32_t count)
{
    View value = {.camera = camera, .clear = {.enabled = clear, .color = {0, 0, 0, 255}}};
    int32_t i;
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, count);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = effects[i] };
    }

    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
}

/** Create a white quad at the origin turned 30 degrees about the view axis.
 *
 * @param world The world
 * @param size Edge of the quad
 * @return The quad
 */
static ecs_entity_t smaa_quad(
    ecs_world_t *world,
    float size)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Quad, {size, size});
    ecs_set(world, result, Rotation, {0, 0, glm_rad(30)});
    ecs_set(world, result, Rgb, {0, 0, 0});
    ecs_set(world, result, Emissive, {.strength = 8, .color = {255, 255, 255}});

    return result;
}

/** Render a frame and keep a copy of the 64x64 capture it wrote.
 *
 * @param world The world
 * @param file File name of the capture in the temporary folder
 * @return The RGBA pixels, freed with ecs_os_free
 */
static unsigned char* smaa_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    unsigned char *px = effects_load(world, file);
    unsigned char *result = ecs_os_malloc(64 * 64 * 4);
    ecs_os_memcpy(result, px, 64 * 64 * 4);
    stbi_image_free(px);

    return result;
}

/** Count the pixels whose channels all lie strictly between 8 and 247.
 *
 * @param px The RGBA pixels of a 64x64 capture
 * @return The count
 */
static int32_t smaa_partial(
    const unsigned char *px)
{
    int32_t i, c, result = 0;
    for (i = 0; i < 64 * 64; i ++) {
        bool partial = true;
        for (c = 0; c < 3; c ++) {
            partial = partial && px[i * 4 + c] > 8 && px[i * 4 + c] < 247;
        }

        result += partial;
    }

    return result;
}

/** Check whether the pixels within 3 pixels of a pixel all equal it.
 *
 * @param px The RGBA pixels of a 64x64 capture
 * @param x Column of the pixel
 * @param y Row of the pixel
 * @return Whether the area around the pixel is flat
 */
static bool smaa_flat(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    int32_t dx, dy, c;
    bool result = true;
    for (dy = -3; dy <= 3 && result; dy ++) {
        for (dx = -3; dx <= 3 && result; dx ++) {
            int32_t nx = glm_clamp(x + dx, 0, 63), ny = glm_clamp(y + dy, 0, 63);
            for (c = 0; c < 4; c ++) {
                result = result && px[(ny * 64 + nx) * 4 + c] == px[(y * 64 + x) * 4 + c];
            }
        }
    }

    return result;
}

void Effects_smaa_softens_diagonal_edge(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    ecs_entity_t chain[2] = {tonemap, smaa};
    ecs_entity_t view = ecs_new(world);
    smaa_view_set(world, view, camera, true, chain, 1);
    smaa_quad(world, 3);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "smaa_edge.png",
        .resolution_scale = 1});
    unsigned char *plain = smaa_frame(world, "smaa_edge.png");

    smaa_view_set(world, view, camera, true, chain, 2);
    unsigned char *smooth = smaa_frame(world, "smaa_edge.png");
    test_assert(plain[(32 * 64 + 32) * 4] > 250);
    test_assert(smooth[(32 * 64 + 32) * 4] > 250);
    test_assert(plain[(2 * 64 + 2) * 4] < 4);
    test_assert(smooth[(2 * 64 + 2) * 4] < 4);
    test_assert(smaa_partial(plain) <= 4);
    test_assert(smaa_partial(smooth) >= 40);

    ecs_os_free(smooth);
    ecs_os_free(plain);
    ecs_fini(world);
}

void Effects_smaa_keeps_flat_areas(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    ecs_entity_t chain[2] = {tonemap, smaa};
    ecs_entity_t view = ecs_new(world);
    smaa_view_set(world, view, camera, true, chain, 1);
    smaa_quad(world, 3);
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -2.2f, -1});
    ecs_set(world, floor, Quad, {8, 1.5f});
    ecs_set(world, floor, Rgb, {120, 60, 30});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "smaa_flat.png",
        .resolution_scale = 1});
    unsigned char *plain = smaa_frame(world, "smaa_flat.png");

    smaa_view_set(world, view, camera, true, chain, 2);
    unsigned char *smooth = smaa_frame(world, "smaa_flat.png");
    int32_t x, y, c, flat = 0, lit = 0, changed = 0;
    for (y = 0; y < 64; y ++) {
        for (x = 0; x < 64; x ++) {
            if (!smaa_flat(plain, x, y)) {
                continue;
            }

            flat ++;
            lit += plain[(y * 64 + x) * 4] > 100;
            for (c = 0; c < 4; c ++) {
                int32_t i = (y * 64 + x) * 4 + c;
                changed += abs((int)plain[i] - (int)smooth[i]) > 1;
            }
        }
    }

    test_assert(flat > 1000);
    test_assert(lit > 200);
    test_int(changed, 0);

    ecs_os_free(smooth);
    ecs_os_free(plain);
    ecs_fini(world);
}

void Effects_smaa_leaves_later_view(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    ecs_entity_t chain[2] = {tonemap, smaa};
    ecs_entity_t view = ecs_new(world);
    smaa_view_set(world, view, camera, true, chain, 1);
    smaa_quad(world, 2);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t ui_camera = ecs_new(world);
    ecs_add_pair(world, ui_camera, EcsChildOf, scene);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t ui_view = ecs_new(world);
    ecs_set(world, ui_view, View, {.camera = ui_camera});
    ecs_entity_t rect = ecs_new(world);
    ecs_add_pair(world, rect, EcsChildOf, scene);
    ecs_set(world, rect, Position, {0, 26, 0});
    ecs_set(world, rect, Rotation, {0, 0, glm_rad(8)});
    ecs_set(world, rect, Rect, {40, 6});
    ecs_set(world, rect, Rgb, {40, 200, 255});
    ecs_entity_t label = ecs_new(world);
    ecs_add_pair(world, label, EcsChildOf, scene);
    ecs_set(world, label, Position, {0, -25, 0});
    ecs_set(world, label, Text, {.value = "AVW", .size = 12,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, label, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "smaa_ui.png",
        .resolution_scale = 1});
    unsigned char *plain = smaa_frame(world, "smaa_ui.png");

    smaa_view_set(world, view, camera, true, chain, 2);
    unsigned char *smooth = smaa_frame(world, "smaa_ui.png");
    int32_t x, y, i, ui = 0, wrong = 0;
    for (y = 0; y < 64; y ++) {
        if (y > 12 && y < 50) {
            continue;
        }

        for (x = 0; x < 64; x ++) {
            i = (y * 64 + x) * 4;
            ui += plain[i] > 8 || plain[i + 1] > 8 || plain[i + 2] > 8;
            wrong += memcmp(&plain[i], &smooth[i], 4) != 0;
        }
    }

    test_assert(ui > 40);
    test_int(wrong, 0);
    test_assert(memcmp(plain, smooth, 64 * 64 * 4) != 0);

    ecs_os_free(smooth);
    ecs_os_free(plain);
    ecs_fini(world);
}

void Effects_smaa_last_composites_over(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, View, {.camera = camera, .clear = {.enabled = true,
        .color = {200, 30, 30, 255}}});
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t overlay_camera = ecs_new(world);
    ecs_add_pair(world, overlay_camera, EcsChildOf, scene);
    ecs_set(world, overlay_camera, Position, {0, 0, 5});
    ecs_set(world, overlay_camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    ecs_entity_t chain[2] = {tonemap, smaa};
    ecs_entity_t overlay = ecs_new(world);
    smaa_view_set(world, overlay, overlay_camera, false, chain, 2);
    ecs_entity_t quad = smaa_quad(world, 3);
    ecs_add_pair(world, quad, EcsChildOf, scene);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "smaa_over.png",
        .resolution_scale = 1});
    unsigned char *px = smaa_frame(world, "smaa_over.png");

    unsigned char *corner = &px[(4 * 64 + 4) * 4];
    unsigned char *center = &px[(32 * 64 + 32) * 4];
    test_int(corner[0], 200);
    test_int(corner[1], 30);
    test_int(corner[2], 30);
    test_int(corner[3], 255);
    test_assert(center[0] > 250);
    test_assert(center[1] > 250);
    test_assert(center[2] > 250);
    int32_t i, mixed = 0;
    for (i = 0; i < 64 * 64; i ++) {
        mixed += px[i * 4 + 1] > 40 && px[i * 4 + 1] < 240 && px[i * 4] > px[i * 4 + 1];
    }

    test_assert(mixed >= 20);

    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_smaa_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Smaa);
    const Smaa *a = ecs_get(world, e, Smaa);
    test_flt(a->threshold, 0.1f);
    test_int(a->max_search_steps, 16);
    test_int(a->max_search_steps_diag, 8);
    test_int(a->corner_rounding, 25);

    Smaa resolved;
    box_render_smaa_settings(a, &resolved);
    test_flt(resolved.threshold, 0.1f);
    test_int(resolved.max_search_steps, 16);
    test_int(resolved.max_search_steps_diag, 8);
    test_int(resolved.corner_rounding, 25);

    test_assert(ecs_script_run(world, "smaa", "using box.effects\n"
        "aa { Smaa: {} }\n", NULL) == 0);
    const Smaa *scripted = ecs_get(world, ecs_lookup(world, "aa"), Smaa);
    test_assert(scripted != NULL);
    test_flt(scripted->threshold, 0.1f);
    test_int(scripted->max_search_steps, 16);

    ecs_fini(world);
}

void Effects_smaa_settings_clamped(void) {
    // lighting/effects
    Smaa out;
    box_render_smaa_settings(&(Smaa){.threshold = 0, .max_search_steps = 0,
        .max_search_steps_diag = -3, .corner_rounding = -5}, &out);
    test_flt(out.threshold, 0.01f);
    test_int(out.max_search_steps, 1);
    test_int(out.max_search_steps_diag, 0);
    test_int(out.corner_rounding, 0);

    box_render_smaa_settings(&(Smaa){.threshold = 2, .max_search_steps = 500,
        .max_search_steps_diag = 50, .corner_rounding = 200}, &out);
    test_flt(out.threshold, 0.5f);
    test_int(out.max_search_steps, 112);
    test_int(out.max_search_steps_diag, 20);
    test_int(out.corner_rounding, 100);

    box_render_smaa_settings(&(Smaa){.threshold = 0.2f, .max_search_steps = 32,
        .max_search_steps_diag = 0, .corner_rounding = 50}, &out);
    test_flt(out.threshold, 0.2f);
    test_int(out.max_search_steps, 32);
    test_int(out.max_search_steps_diag, 0);
    test_int(out.corner_rounding, 50);
}

void Effects_smaa_follows_surface_resize(void) {
    // lighting/effects
    ecs_entity_t camera;
    ecs_world_t *world = effects_world(&camera);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    ecs_entity_t chain[2] = {tonemap, smaa};
    ecs_entity_t view = ecs_new(world);
    smaa_view_set(world, view, camera, true, chain, 2);
    smaa_quad(world, 3);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = "smaa_resize.png", .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, s, Surface, {.width = 128, .height = 64, .resolution_scale = 2,
        .write_to_file = "smaa_resize.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 48, .resolution_scale = 0.5f,
        .write_to_file = "smaa_resize.png"});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/smaa_resize.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 96);
    test_int(h, 48);
    test_assert(px[(24 * 96 + 48) * 4] > 250);
    test_assert(px[(2 * 96 + 2) * 4] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Columns of the middle row of the color grading scene: a muted orange, a grey and a saturated
 * red cube, and the black background */
#define GRADING_MUTED (15)
#define GRADING_GREY (32)
#define GRADING_RED (49)
#define GRADING_BACKGROUND (2)

/** Create the color grading scene: an unlit view without effects and three emissive cubes of
 * strength 1, a muted orange, a grey and a saturated red, into a 64x64 capture.
 *
 * @param camera Receives the camera
 * @param view Receives the view
 * @param file File name of the capture in the temporary folder
 * @return The world
 */
static ecs_world_t* grading_world(
    ecs_entity_t *camera,
    ecs_entity_t *view,
    const char *file)
{
    ecs_world_t *world = effects_world(camera);
    *view = ecs_new(world);
    smaa_view_set(world, *view, *camera, true, NULL, 0);
    effects_cube(world, -1.5f, 1, 1, (Rgb){200, 150, 130});
    effects_cube(world, 0, 1, 1, (Rgb){128, 128, 128});
    effects_cube(world, 1.5f, 1, 1, (Rgb){230, 40, 40});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file),
        .resolution_scale = 1});

    return world;
}

/** Create an effect entity with color grading settings, starting from the defaults.
 *
 * @param world The world
 * @param value The settings
 * @return The entity
 */
static ecs_entity_t grading_effect(
    ecs_world_t *world,
    const ColorGrading *value)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set_ptr(world, result, ColorGrading, value);

    return result;
}

/** Get the default color grading settings.
 *
 * @return The settings a constructed ColorGrading has
 */
static ColorGrading grading_defaults(void) {
    return (ColorGrading){
        .contrast = 1, .saturation = 1, .gamma = {1, 1, 1}, .gain = {1, 1, 1},
        .lut_strength = 1
    };
}

/** Set the effects of the view and render frames, enough for a LUT texture to load.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param effects The effect entities, in order
 * @param count Number of effects
 * @param file File name of the capture
 * @return The RGBA pixels, freed with ecs_os_free
 */
static unsigned char* grading_frame(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    const ecs_entity_t *effects,
    int32_t count,
    const char *file)
{
    smaa_view_set(world, view, camera, true, effects, count);
    ecs_progress(world, 1.0f / 60);

    return smaa_frame(world, file);
}

/** Get a pixel of the middle row of a 64x64 capture.
 *
 * @param px The pixels
 * @param x The column
 * @return The pixel
 */
static const unsigned char* grading_px(
    const unsigned char *px,
    int32_t x)
{
    return &px[(32 * 64 + x) * 4];
}

/** Get the largest difference of a channel between two 64x64 captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The largest difference
 */
static int grading_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i;
    int result = 0;
    for (i = 0; i < 64 * 64 * 4; i ++) {
        result = glm_imax(result, abs((int)a[i] - (int)b[i]));
    }

    return result;
}

void Effects_color_grading_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ColorGrading);
    const ColorGrading *g = ecs_get(world, e, ColorGrading);
    test_flt(g->temperature, 0);
    test_flt(g->tint, 0);
    test_flt(g->exposure, 0);
    test_flt(g->contrast, 1);
    test_flt(g->saturation, 1);
    test_flt(g->vibrance, 0);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        test_flt(g->lift[i], 0);
        test_flt(g->gamma[i], 1);
        test_flt(g->gain[i], 1);
    }

    test_uint(g->lut, 0);
    test_flt(g->lut_strength, 1);

    ColorGrading resolved;
    box_render_grading_settings(g, &resolved);
    test_flt(resolved.contrast, 1);
    test_flt(resolved.saturation, 1);
    test_flt(resolved.gamma[1], 1);
    test_flt(resolved.lut_strength, 1);

    float m[3][3];
    box_render_grading_balance(&resolved, m);
    int32_t j;
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 3; j ++) {
            test_flt(m[i][j], i == j ? 1.0f : 0.0f);
        }
    }

    test_assert(ecs_script_run(world, "grading", "using box.effects\n"
        "look { ColorGrading: {} }\n", NULL) == 0);
    const ColorGrading *scripted = ecs_get(world, ecs_lookup(world, "look"), ColorGrading);
    test_assert(scripted != NULL);
    test_flt(scripted->contrast, 1);
    test_flt(scripted->saturation, 1);
    test_flt(scripted->gamma[2], 1);
    test_flt(scripted->gain[0], 1);
    test_flt(scripted->lut_strength, 1);

    ecs_fini(world);
}

void Effects_color_grading_settings_clamped(void) {
    // lighting/effects
    ColorGrading out;
    box_render_grading_settings(&(ColorGrading){.contrast = -1, .saturation = -2,
        .vibrance = -3, .gamma = {0, -1, 0.5f}, .lut_strength = 2}, &out);
    test_flt(out.contrast, 0);
    test_flt(out.saturation, 0);
    test_flt(out.vibrance, -1);
    test_flt(out.gamma[0], 0.01f);
    test_flt(out.gamma[1], 0.01f);
    test_flt(out.gamma[2], 0.5f);
    test_flt(out.lut_strength, 1);

    box_render_grading_settings(&(ColorGrading){.contrast = 1.5f, .saturation = 3,
        .vibrance = 2, .temperature = 4, .gamma = {2, 2, 2}, .lut_strength = -1}, &out);
    test_flt(out.contrast, 1.5f);
    test_flt(out.saturation, 3);
    test_flt(out.vibrance, 2);
    test_flt(out.temperature, 4);
    test_flt(out.lut_strength, 0);
}

void Effects_color_grading_defaults_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_default.png");
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_default.png");
    ecs_entity_t grading = ecs_new(world);
    ecs_add(world, grading, ColorGrading);
    unsigned char *graded = grading_frame(world, view, camera, &grading, 1,
        "grading_default.png");
    test_assert(grading_px(base, GRADING_GREY)[0] > 100);
    test_assert(grading_diff(base, graded) <= 1);

    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t chain[2] = {tonemap, grading};
    unsigned char *tonemapped = grading_frame(world, view, camera, chain, 1,
        "grading_default.png");
    unsigned char *both = grading_frame(world, view, camera, chain, 2, "grading_default.png");
    test_assert(grading_diff(tonemapped, both) <= 1);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(graded);
    ecs_os_free(tonemapped);
    ecs_os_free(both);
    ecs_fini(world);
}

void Effects_color_grading_zero_literal_applies_as_set(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_zero.png");
    ecs_entity_t grading = ecs_new(world);
    ecs_set(world, grading, ColorGrading, {0});
    unsigned char *px = grading_frame(world, view, camera, &grading, 1, "grading_zero.png");
    int32_t x, c;
    for (x = 0; x < 64; x ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(grading_px(px, x)[c] < 2);
        }
    }

    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_color_grading_temperature_warms(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_temperature.png");
    ColorGrading value = grading_defaults();
    value.temperature = 0.5f;
    ecs_entity_t warm = grading_effect(world, &value);
    value.temperature = -0.5f;
    ecs_entity_t cool = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_temperature.png");
    unsigned char *w = grading_frame(world, view, camera, &warm, 1, "grading_temperature.png");
    unsigned char *k = grading_frame(world, view, camera, &cool, 1, "grading_temperature.png");
    const unsigned char *grey = grading_px(base, GRADING_GREY);
    const unsigned char *gw = grading_px(w, GRADING_GREY);
    const unsigned char *gk = grading_px(k, GRADING_GREY);
    test_assert(abs((int)grey[0] - (int)grey[2]) <= 1);
    test_assert(gw[0] > gw[2] + 4);
    test_assert(gw[0] > grey[0]);
    test_assert(gw[2] < grey[2]);
    test_assert(gk[2] > gk[0] + 4);
    test_assert(gk[2] > grey[2]);
    test_assert(gk[0] < grey[0]);
    test_assert(grading_px(w, GRADING_BACKGROUND)[0] < 2);

    ecs_os_free(base);
    ecs_os_free(w);
    ecs_os_free(k);
    ecs_fini(world);
}

void Effects_color_grading_tint_magenta(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_tint.png");
    ColorGrading value = grading_defaults();
    value.tint = 0.5f;
    ecs_entity_t magenta = grading_effect(world, &value);
    value.tint = -0.5f;
    ecs_entity_t green = grading_effect(world, &value);
    unsigned char *m = grading_frame(world, view, camera, &magenta, 1, "grading_tint.png");
    unsigned char *g = grading_frame(world, view, camera, &green, 1, "grading_tint.png");
    const unsigned char *gm = grading_px(m, GRADING_GREY);
    const unsigned char *gg = grading_px(g, GRADING_GREY);
    test_assert(gm[1] + 4 < gm[0]);
    test_assert(gm[1] + 4 < gm[2]);
    test_assert(gg[1] > gg[0] + 4);
    test_assert(gg[1] > gg[2] + 4);

    ecs_os_free(m);
    ecs_os_free(g);
    ecs_fini(world);
}

void Effects_color_grading_exposure_scales(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_exposure.png");
    ColorGrading value = grading_defaults();
    value.exposure = 1;
    ecs_entity_t brighter = grading_effect(world, &value);
    value.exposure = -1;
    ecs_entity_t darker = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_exposure.png");
    unsigned char *b = grading_frame(world, view, camera, &brighter, 1, "grading_exposure.png");
    unsigned char *d = grading_frame(world, view, camera, &darker, 1, "grading_exposure.png");
    float grey = effects_linear(grading_px(base, GRADING_GREY)[1]);
    test_assert(abs((int)grading_px(b, GRADING_GREY)[1] - effects_srgb8(grey * 2)) <= 2);
    test_assert(abs((int)grading_px(d, GRADING_GREY)[1] - effects_srgb8(grey * 0.5f)) <= 2);
    test_assert(grading_px(b, GRADING_BACKGROUND)[1] < 2);

    ecs_os_free(base);
    ecs_os_free(b);
    ecs_os_free(d);
    ecs_fini(world);
}

void Effects_color_grading_contrast_hdr(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_contrast.png");
    ColorGrading value = grading_defaults();
    value.contrast = 1.5f;
    ecs_entity_t grading = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_contrast.png");
    unsigned char *px = grading_frame(world, view, camera, &grading, 1, "grading_contrast.png");
    // Around a middle grey of 0.18: the red channel of the muted cube (0.58) brightens, its
    // blue channel (0.22) barely moves and the green and blue of the red cube (0.02) darken
    const unsigned char *muted = grading_px(px, GRADING_MUTED);
    const unsigned char *red = grading_px(px, GRADING_RED);
    test_assert(muted[0] > grading_px(base, GRADING_MUTED)[0]);
    test_assert(red[1] < grading_px(base, GRADING_RED)[1]);
    test_assert(red[2] < grading_px(base, GRADING_RED)[2]);
    float in = effects_linear(grading_px(base, GRADING_GREY)[1]);
    float expected = 0.18f * powf(in / 0.18f, 1.5f);
    test_assert(abs((int)grading_px(px, GRADING_GREY)[1] - effects_srgb8(expected)) <= 2);
    test_assert(grading_px(px, GRADING_BACKGROUND)[0] < 2);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_color_grading_contrast_display(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_contrast_ldr.png");
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ColorGrading value = grading_defaults();
    value.contrast = 1.5f;
    ecs_entity_t grading = grading_effect(world, &value);
    ecs_entity_t chain[2] = {tonemap, grading};
    unsigned char *base = grading_frame(world, view, camera, chain, 1,
        "grading_contrast_ldr.png");
    unsigned char *px = grading_frame(world, view, camera, chain, 2, "grading_contrast_ldr.png");
    // After the Tonemap contrast pivots around 0.5: linear 0.22 of the grey cube darkens
    float in = effects_linear(grading_px(base, GRADING_GREY)[1]);
    float expected = glm_max((in - 0.5f) * 1.5f + 0.5f, 0.0f);
    test_assert(in < 0.5f);
    test_assert(abs((int)grading_px(px, GRADING_GREY)[1] - effects_srgb8(expected)) <= 2);
    test_assert(grading_px(px, GRADING_GREY)[1] < grading_px(base, GRADING_GREY)[1]);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_color_grading_saturation(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_saturation.png");
    ColorGrading value = grading_defaults();
    value.saturation = 0;
    ecs_entity_t grey = grading_effect(world, &value);
    value.saturation = 1.5f;
    ecs_entity_t vivid = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_saturation.png");
    unsigned char *g = grading_frame(world, view, camera, &grey, 1, "grading_saturation.png");
    unsigned char *v = grading_frame(world, view, camera, &vivid, 1, "grading_saturation.png");
    const unsigned char *muted = grading_px(g, GRADING_MUTED);
    test_assert(abs((int)muted[0] - (int)muted[1]) <= 1);
    test_assert(abs((int)muted[0] - (int)muted[2]) <= 1);
    const unsigned char *in = grading_px(base, GRADING_MUTED);
    float luma = effects_linear(in[0]) * 0.2126f + effects_linear(in[1]) * 0.7152f +
        effects_linear(in[2]) * 0.0722f;
    test_assert(abs((int)muted[1] - effects_srgb8(luma)) <= 2);
    test_assert(effects_saturation(grading_px(v, GRADING_MUTED)) >
        effects_saturation(in) + 0.05f);
    // Grey and black stay as they are
    int32_t c;
    for (c = 0; c < 3; c ++) {
        test_assert(abs((int)grading_px(v, GRADING_GREY)[c] -
            (int)grading_px(base, GRADING_GREY)[c]) <= 1);
        test_assert(grading_px(v, GRADING_BACKGROUND)[c] < 2);
    }

    ecs_os_free(base);
    ecs_os_free(g);
    ecs_os_free(v);
    ecs_fini(world);
}

void Effects_color_grading_vibrance_favors_muted(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_vibrance.png");
    ColorGrading value = grading_defaults();
    value.vibrance = 1;
    ecs_entity_t grading = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_vibrance.png");
    unsigned char *px = grading_frame(world, view, camera, &grading, 1, "grading_vibrance.png");
    float muted = effects_saturation(grading_px(px, GRADING_MUTED)) -
        effects_saturation(grading_px(base, GRADING_MUTED));
    float red = effects_saturation(grading_px(px, GRADING_RED)) -
        effects_saturation(grading_px(base, GRADING_RED));
    test_assert(muted > 0.05f);
    test_assert(muted > red);
    int32_t c;
    for (c = 0; c < 3; c ++) {
        test_assert(abs((int)grading_px(px, GRADING_GREY)[c] -
            (int)grading_px(base, GRADING_GREY)[c]) <= 1);
    }

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_color_grading_lift_gamma_gain(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_lgg.png");
    ColorGrading value = grading_defaults();
    glm_vec3_copy((vec3){1, 0.5f, 0.5f}, value.gain);
    ecs_entity_t gain = grading_effect(world, &value);
    value = grading_defaults();
    glm_vec3_copy((vec3){0, 0, 0.1f}, value.lift);
    ecs_entity_t lift = grading_effect(world, &value);
    value = grading_defaults();
    glm_vec3_copy((vec3){2, 2, 2}, value.gamma);
    ecs_entity_t gamma = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_lgg.png");
    unsigned char *a = grading_frame(world, view, camera, &gain, 1, "grading_lgg.png");
    unsigned char *b = grading_frame(world, view, camera, &lift, 1, "grading_lgg.png");
    unsigned char *c = grading_frame(world, view, camera, &gamma, 1, "grading_lgg.png");
    float grey = effects_linear(grading_px(base, GRADING_GREY)[0]);

    // Gain halves green and blue, keeps red
    const unsigned char *ga = grading_px(a, GRADING_GREY);
    test_assert(abs((int)ga[0] - (int)grading_px(base, GRADING_GREY)[0]) <= 1);
    test_assert(abs((int)ga[1] - effects_srgb8(grey * 0.5f)) <= 2);

    // Lift adds to every pixel, the black background turns blue
    const unsigned char *bg = grading_px(b, GRADING_BACKGROUND);
    test_assert(bg[0] < 2);
    test_assert(abs((int)bg[2] - effects_srgb8(0.1f)) <= 2);

    // Gamma 2 is a square root
    test_assert(abs((int)grading_px(c, GRADING_GREY)[1] - effects_srgb8(sqrtf(grey))) <= 2);
    test_assert(grading_px(c, GRADING_BACKGROUND)[1] < 2);

    ecs_os_free(base);
    ecs_os_free(a);
    ecs_os_free(b);
    ecs_os_free(c);
    ecs_fini(world);
}

void Effects_color_grading_disabled_entry_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_disabled.png");
    ColorGrading value = grading_defaults();
    value.saturation = 0;
    value.exposure = 2;
    ecs_entity_t grading = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_disabled.png");
    View v = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &v.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &v.effects, ViewEffect) = (ViewEffect){
        .enabled = false, .effect = grading };
    ecs_set_ptr(world, view, View, &v);
    ecs_vec_fini_t(NULL, &v.effects, ViewEffect);
    unsigned char *px = smaa_frame(world, "grading_disabled.png");
    test_assert(grading_diff(base, px) <= 1);

    ecs_add_id(world, grading, EcsDisabled);
    unsigned char *off = grading_frame(world, view, camera, &grading, 1,
        "grading_disabled.png");
    test_assert(grading_diff(base, off) <= 1);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_os_free(off);
    ecs_fini(world);
}

void Effects_color_grading_same_entity_after_tonemap(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_order.png");
    ColorGrading value = grading_defaults();
    value.contrast = 2;
    value.saturation = 0.5f;
    ecs_entity_t both = grading_effect(world, &value);
    ecs_add(world, both, Tonemap);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t grading = grading_effect(world, &value);
    ecs_entity_t chain[2] = {tonemap, grading};
    ecs_entity_t reversed[2] = {grading, tonemap};
    unsigned char *a = grading_frame(world, view, camera, &both, 1, "grading_order.png");
    unsigned char *b = grading_frame(world, view, camera, chain, 2, "grading_order.png");
    unsigned char *c = grading_frame(world, view, camera, reversed, 2, "grading_order.png");
    test_assert(grading_diff(a, b) <= 1);
    test_assert(grading_diff(a, c) > 8);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_os_free(c);
    ecs_fini(world);
}

void Effects_color_grading_follows_surface_resize(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_resize.png");
    ColorGrading value = grading_defaults();
    value.saturation = 0;
    ecs_entity_t grading = grading_effect(world, &value);
    smaa_view_set(world, view, camera, true, &grading, 1);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_iter_t it = ecs_each(world, Surface);
    ecs_entity_t surface = 0;
    while (ecs_each_next(&it)) {
        surface = it.entities[0];
    }

    test_assert(surface != 0);
    ecs_set(world, surface, Surface, {.width = 128, .height = 64, .resolution_scale = 2,
        .write_to_file = "grading_resize.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, surface, Surface, {.width = 96, .height = 48, .resolution_scale = 0.5f,
        .write_to_file = "grading_resize.png"});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/grading_resize.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 96);
    test_int(h, 48);
    // The red cube right of the middle is grey
    const unsigned char *red = &px[(24 * 96 + 60) * 4];
    test_assert(red[0] > 20);
    test_assert(abs((int)red[0] - (int)red[1]) <= 1);
    test_assert(abs((int)red[0] - (int)red[2]) <= 1);
    test_assert(px[(2 * 96 + 2) * 4] < 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Create a texture entity that loads a strip LUT of test/assets/luts.
 *
 * @param world The world
 * @param file File name in test/assets/luts
 * @return The texture entity
 */
static ecs_entity_t grading_lut(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("test/assets/luts/%s", file);
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexturePath, {.path = path});
    ecs_os_free(path);

    return result;
}

/** Test that the image of a LUT texture entity is loaded with a shape.
 *
 * @param world The world
 * @param lut The texture entity
 * @param width The expected width
 * @param height The expected height
 */
static void grading_lut_loaded(
    ecs_world_t *world,
    ecs_entity_t lut,
    int32_t width,
    int32_t height)
{
    const box_textures_t *t = box_textures_get(world);
    test_assert(t != NULL);
    test_assert(box_textures_handle(t, lut) != 0);
    const box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, lut);
    test_assert(tex != NULL);
    test_int(tex->width, width);
    test_int(tex->height, height);
}

static int32_t grading_errors = 0;

static void grading_count_errors(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -3 && strstr(msg, "ColorGrading LUT")) {
        grading_errors ++;
    }
}

void Effects_color_grading_lut_identity_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_identity.png");
    ColorGrading value = grading_defaults();
    value.lut = grading_lut(world, "identity_16.png");
    ecs_entity_t small = grading_effect(world, &value);
    value.lut = grading_lut(world, "identity_32.png");
    ecs_entity_t large = grading_effect(world, &value);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t chain[2] = {tonemap, small};
    unsigned char *base = grading_frame(world, view, camera, chain, 1, "grading_identity.png");
    unsigned char *a = grading_frame(world, view, camera, chain, 2, "grading_identity.png");
    chain[1] = large;
    unsigned char *b = grading_frame(world, view, camera, chain, 2, "grading_identity.png");
    grading_lut_loaded(world, ecs_get(world, small, ColorGrading)->lut, 256, 16);
    grading_lut_loaded(world, value.lut, 1024, 32);
    test_assert(grading_diff(base, a) <= 1);
    test_assert(grading_diff(base, b) <= 1);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

/** Load a strip LUT of test/assets/luts as stored.
 *
 * @param file File name in test/assets/luts
 * @param size Receives the slices of the LUT, its height
 * @return The RGBA rows, freed with stbi_image_free
 */
static unsigned char* grading_lut_pixels(
    const char *file,
    int32_t *size)
{
    char *rel = flecs_asprintf("test/assets/luts/%s", file);
    char *path = box_app_locate(rel);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, h * h);
    *size = h;
    ecs_os_free(path);
    ecs_os_free(rel);

    return result;
}

void Effects_color_grading_lut_sepia_matches(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_sepia.png");
    ColorGrading value = grading_defaults();
    value.lut = grading_lut(world, "sepia_16.png");
    ecs_entity_t sepia = grading_effect(world, &value);
    value.lut_strength = 0.5f;
    ecs_entity_t half = grading_effect(world, &value);
    value.lut_strength = 0;
    ecs_entity_t none = grading_effect(world, &value);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t chain[2] = {tonemap, sepia};
    unsigned char *base = grading_frame(world, view, camera, chain, 1, "grading_sepia.png");
    unsigned char *full = grading_frame(world, view, camera, chain, 2, "grading_sepia.png");
    chain[1] = half;
    unsigned char *blend = grading_frame(world, view, camera, chain, 2, "grading_sepia.png");
    chain[1] = none;
    unsigned char *off = grading_frame(world, view, camera, chain, 2, "grading_sepia.png");
    int32_t size = 0;
    unsigned char *lut = grading_lut_pixels("sepia_16.png", &size);
    test_int(size, 16);

    const int32_t columns[3] = {GRADING_MUTED, GRADING_GREY, GRADING_RED};
    int32_t i, c;
    ColorGrading resolved;
    for (i = 0; i < 3; i ++) {
        const unsigned char *in = grading_px(base, columns[i]);
        float lin[3] = {effects_linear(in[0]), effects_linear(in[1]), effects_linear(in[2])};
        float expected[3], expected_half[3];
        box_render_grading_settings(&(ColorGrading){.contrast = 1, .saturation = 1,
            .gamma = {1, 1, 1}, .gain = {1, 1, 1}, .lut_strength = 1}, &resolved);
        box_render_grading_apply(&resolved, true, lut, size, lin, expected);
        resolved.lut_strength = 0.5f;
        box_render_grading_apply(&resolved, true, lut, size, lin, expected_half);
        const unsigned char *out = grading_px(full, columns[i]);
        const unsigned char *mid = grading_px(blend, columns[i]);
        for (c = 0; c < 3; c ++) {
            test_assert(abs((int)out[c] - effects_srgb8(expected[c])) <= 2);
            test_assert(abs((int)mid[c] - effects_srgb8(expected_half[c])) <= 2);
        }

        // Sepia: red above green above blue
        test_assert(out[0] > out[1]);
        test_assert(out[1] > out[2]);
    }

    test_assert(grading_diff(base, off) <= 1);

    stbi_image_free(lut);
    ecs_os_free(base);
    ecs_os_free(full);
    ecs_os_free(blend);
    ecs_os_free(off);
    ecs_fini(world);
}

void Effects_color_grading_lut_wrong_shape_ignored(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = grading_world(&camera, &view, "grading_shape.png");
    ColorGrading value = grading_defaults();
    value.lut = grading_lut(world, "not_a_strip.png");
    ecs_entity_t grading = grading_effect(world, &value);
    unsigned char *base = grading_frame(world, view, camera, NULL, 0, "grading_shape.png");
    ecs_os_api_log_t log = ecs_os_api.log_;
    grading_errors = 0;
    ecs_os_api.log_ = grading_count_errors;
    unsigned char *px = grading_frame(world, view, camera, &grading, 1, "grading_shape.png");
    unsigned char *again = smaa_frame(world, "grading_shape.png");
    ecs_os_api.log_ = log;
    grading_lut_loaded(world, value.lut, 32, 16);
    test_int(grading_errors, 1);
    test_assert(grading_diff(base, px) <= 1);
    test_assert(grading_diff(base, again) <= 1);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_os_free(again);
    ecs_fini(world);
}

/** Create the vignette scene: a view that clears to white without geometry, a flat white input,
 * into a capture of the given size.
 *
 * @param camera Receives the camera
 * @param view Receives the view
 * @param width Width of the surface
 * @param height Height of the surface
 * @param file File name of the capture in the temporary folder
 * @return The world
 */
static ecs_world_t* vignette_world(
    ecs_entity_t *camera,
    ecs_entity_t *view,
    int32_t width,
    int32_t height,
    const char *file)
{
    ecs_world_t *world = effects_world(camera);
    *view = ecs_new(world);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = width, .height = height,
        .write_to_file = ECS_CONST_CAST(char*, file),
        .resolution_scale = 1});

    return world;
}

/** Get the default vignette settings.
 *
 * @return The settings a constructed Vignette has
 */
static Vignette vignette_defaults(void) {
    return (Vignette){
        .intensity = 0.3f, .radius = 0.75f, .smoothness = 0.45f, .roundness = 1,
        .center = {0.5f, 0.5f}
    };
}

/** Create an effect entity with vignette settings.
 *
 * @param world The world
 * @param value The settings
 * @return The entity
 */
static ecs_entity_t vignette_effect(
    ecs_world_t *world,
    const Vignette *value)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set_ptr(world, result, Vignette, value);

    return result;
}

/** Set the effects of the white view, render frames and keep a copy of the capture.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param effects The effect entities, in order
 * @param count Number of effects
 * @param width Width of the capture
 * @param height Height of the capture
 * @param file File name of the capture
 * @return The RGBA pixels, freed with ecs_os_free
 */
static unsigned char* vignette_frame(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    const ecs_entity_t *effects,
    int32_t count,
    int32_t width,
    int32_t height,
    const char *file)
{
    View value = {.camera = camera,
        .clear = {.enabled = true, .color = {255, 255, 255, 255}}};
    int32_t i;
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, count);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = effects[i] };
    }

    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, width);
    test_int(h, height);
    ecs_os_free(path);
    unsigned char *result = ecs_os_malloc(width * height * 4);
    ecs_os_memcpy(result, px, width * height * 4);
    stbi_image_free(px);

    return result;
}

/** Get a pixel of a capture.
 *
 * @param px The pixels
 * @param width Width of the capture
 * @param x The column
 * @param y The row
 * @return The pixel
 */
static const unsigned char* vignette_px(
    const unsigned char *px,
    int32_t width,
    int32_t x,
    int32_t y)
{
    return &px[(y * width + x) * 4];
}

/** Get the largest difference of a channel between two captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @param size Pixels of a capture
 * @return The largest difference
 */
static int vignette_diff(
    const unsigned char *a,
    const unsigned char *b,
    int32_t size)
{
    int32_t i;
    int result = 0;
    for (i = 0; i < size * 4; i ++) {
        result = glm_imax(result, abs((int)a[i] - (int)b[i]));
    }

    return result;
}

void Effects_vignette_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Vignette);
    const Vignette *v = ecs_get(world, e, Vignette);
    test_flt(v->intensity, 0.3f);
    test_flt(v->radius, 0.75f);
    test_flt(v->smoothness, 0.45f);
    test_flt(v->roundness, 1);
    test_flt(v->center[0], 0.5f);
    test_flt(v->center[1], 0.5f);
    test_int(v->color.r, 0);
    test_int(v->color.g, 0);
    test_int(v->color.b, 0);

    Vignette resolved;
    box_render_vignette_settings(v, &resolved);
    test_flt(resolved.intensity, 0.3f);
    test_flt(resolved.radius, 0.75f);
    test_flt(resolved.smoothness, 0.45f);
    test_flt(resolved.roundness, 1);
    test_flt(resolved.center[0], 0.5f);

    test_assert(ecs_script_run(world, "vignette", "using box.effects\n"
        "lens { Vignette: {} }\n", NULL) == 0);
    const Vignette *scripted = ecs_get(world, ecs_lookup(world, "lens"), Vignette);
    test_assert(scripted != NULL);
    test_flt(scripted->intensity, 0.3f);
    test_flt(scripted->radius, 0.75f);
    test_flt(scripted->smoothness, 0.45f);
    test_flt(scripted->roundness, 1);
    test_flt(scripted->center[1], 0.5f);

    ecs_fini(world);
}

void Effects_vignette_settings_clamped(void) {
    // lighting/effects
    Vignette out;
    box_render_vignette_settings(&(Vignette){.intensity = 2, .radius = -1, .smoothness = -1,
        .roundness = 3, .center = {-0.5f, 2}, .color = {10, 20, 30}}, &out);
    test_flt(out.intensity, 1);
    test_flt(out.radius, 0);
    test_flt(out.smoothness, 0);
    test_flt(out.roundness, 1);
    test_flt(out.center[0], -0.5f);
    test_flt(out.center[1], 2);
    test_int(out.color.g, 20);

    box_render_vignette_settings(&(Vignette){.intensity = -1, .radius = 1.5f,
        .smoothness = 0.2f, .roundness = -1}, &out);
    test_flt(out.intensity, 0);
    test_flt(out.radius, 1.5f);
    test_flt(out.smoothness, 0.2f);
    test_flt(out.roundness, 0);
}

void Effects_vignette_center_unchanged(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_center.png");
    Vignette value = vignette_defaults();
    value.intensity = 1;
    ecs_entity_t vignette = vignette_effect(world, &value);
    unsigned char *px = vignette_frame(world, view, camera, &vignette, 1, 64, 64,
        "vignette_center.png");
    const unsigned char *center = vignette_px(px, 64, 32, 32);
    test_assert(center[0] >= 254);
    test_assert(center[1] >= 254);
    test_assert(center[2] >= 254);
    test_assert(vignette_px(px, 64, 0, 0)[0] < 250);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_vignette_corners_darken_by_intensity(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_corners.png");
    Vignette value = vignette_defaults();
    value.radius = 0.4f;
    value.smoothness = 0.3f;
    value.intensity = 0.3f;
    ecs_entity_t light = vignette_effect(world, &value);
    value.intensity = 0.8f;
    ecs_entity_t strong = vignette_effect(world, &value);
    unsigned char *l = vignette_frame(world, view, camera, &light, 1, 64, 64,
        "vignette_corners.png");
    unsigned char *s = vignette_frame(world, view, camera, &strong, 1, 64, 64,
        "vignette_corners.png");
    const int32_t corners[4][2] = {{0, 0}, {63, 0}, {0, 63}, {63, 63}};
    int32_t i, c;
    for (i = 0; i < 4; i ++) {
        const unsigned char *pl = vignette_px(l, 64, corners[i][0], corners[i][1]);
        const unsigned char *ps = vignette_px(s, 64, corners[i][0], corners[i][1]);
        for (c = 0; c < 3; c ++) {
            test_assert(abs((int)pl[c] - effects_srgb8(0.7f)) <= 2);
            test_assert(abs((int)ps[c] - effects_srgb8(0.2f)) <= 2);
        }

        test_int(pl[3], 255);
    }

    test_assert(vignette_px(l, 64, 32, 32)[0] >= 254);
    test_assert(vignette_px(s, 64, 32, 32)[0] >= 254);

    ecs_os_free(l);
    ecs_os_free(s);
    ecs_fini(world);
}

void Effects_vignette_zero_intensity_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_zero.png");
    Vignette value = vignette_defaults();
    value.intensity = 0;
    value.radius = 0;
    value.color = (rgb){255, 0, 0};
    ecs_entity_t vignette = vignette_effect(world, &value);
    unsigned char *base = vignette_frame(world, view, camera, NULL, 0, 64, 64,
        "vignette_zero.png");
    unsigned char *px = vignette_frame(world, view, camera, &vignette, 1, 64, 64,
        "vignette_zero.png");
    test_assert(vignette_px(base, 64, 0, 0)[0] >= 254);
    test_assert(vignette_diff(base, px, 64 * 64) <= 1);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_vignette_zero_literal_applies_as_set(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_literal.png");
    ecs_entity_t zero = ecs_new(world);
    ecs_set(world, zero, Vignette, {0});
    ecs_entity_t hard = ecs_new(world);
    ecs_set(world, hard, Vignette, {.intensity = 1});
    unsigned char *base = vignette_frame(world, view, camera, NULL, 0, 64, 64,
        "vignette_literal.png");
    unsigned char *z = vignette_frame(world, view, camera, &zero, 1, 64, 64,
        "vignette_literal.png");
    // Intensity 0 leaves the image as is
    test_assert(vignette_diff(base, z, 64 * 64) <= 1);

    // Radius and smoothness 0 are a hard edge at the center: every pixel is black
    unsigned char *h = vignette_frame(world, view, camera, &hard, 1, 64, 64,
        "vignette_literal.png");
    int32_t i, c;
    for (i = 0; i < 64 * 64; i ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(h[i * 4 + c] < 2);
        }
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(z);
    ecs_os_free(h);
    ecs_fini(world);
}

void Effects_vignette_round_on_wide_image(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 128, 72, "vignette_round.png");
    Vignette value = vignette_defaults();
    value.intensity = 1;
    value.radius = 0.1f;
    value.smoothness = 0.8f;
    ecs_entity_t round = vignette_effect(world, &value);
    value.roundness = 0;
    ecs_entity_t rect = vignette_effect(world, &value);
    unsigned char *r = vignette_frame(world, view, camera, &round, 1, 128, 72,
        "vignette_round.png");
    unsigned char *q = vignette_frame(world, view, camera, &rect, 1, 128, 72,
        "vignette_round.png");
    int32_t k;
    for (k = 14; k <= 34; k += 10) {
        // The same distance in pixels right and down of the center darkens the same
        int h = vignette_px(r, 128, 64 + k, 36)[0];
        int v = vignette_px(r, 128, 64, 36 + k)[0];
        test_assert(abs(h - v) <= 2);
        test_assert(v < 254);

        // Following the rectangle, the horizontal falloff is stretched by 16:9
        int qh = vignette_px(q, 128, 64 + k, 36)[0];
        int qv = vignette_px(q, 128, 64, 36 + k)[0];
        test_assert(qh > qv + 4);
    }

    // Following the rectangle, the edge midpoints darken the same
    int left = vignette_px(q, 128, 0, 36)[0];
    int top = vignette_px(q, 128, 64, 0)[0];
    test_assert(abs(left - top) <= 6);

    ecs_os_free(r);
    ecs_os_free(q);
    ecs_fini(world);
}

void Effects_vignette_center_moves_bright_spot(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_moved.png");
    Vignette value = vignette_defaults();
    value.intensity = 1;
    value.radius = 0.1f;
    value.smoothness = 0.5f;
    value.center[0] = 0.25f;
    value.center[1] = 0.75f;
    ecs_entity_t vignette = vignette_effect(world, &value);
    unsigned char *px = vignette_frame(world, view, camera, &vignette, 1, 64, 64,
        "vignette_moved.png");
    const unsigned char *spot = vignette_px(px, 64, 16, 48);
    test_assert(spot[0] >= 254);
    test_assert(vignette_px(px, 64, 32, 32)[0] < spot[0] - 4);
    test_assert(vignette_px(px, 64, 48, 16)[0] < vignette_px(px, 64, 32, 32)[0]);
    // Left of the moved center is as bright as the same distance right of it
    int left = vignette_px(px, 64, 16 - 10, 48)[0];
    int right = vignette_px(px, 64, 16 + 9, 48)[0];
    test_assert(abs(left - right) <= 2);

    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_vignette_color_tints_edges(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_color.png");
    Vignette value = vignette_defaults();
    value.intensity = 1;
    value.radius = 0.3f;
    value.smoothness = 0.3f;
    value.color = (rgb){255, 64, 0};
    ecs_entity_t vignette = vignette_effect(world, &value);
    unsigned char *px = vignette_frame(world, view, camera, &vignette, 1, 64, 64,
        "vignette_color.png");
    const unsigned char *corner = vignette_px(px, 64, 0, 0);
    test_assert(corner[0] >= 253);
    test_assert(abs((int)corner[1] - 64) <= 2);
    test_assert(corner[2] < 2);
    test_int(corner[3], 255);
    const unsigned char *center = vignette_px(px, 64, 32, 32);
    test_assert(center[0] >= 254);
    test_assert(center[1] >= 254);
    test_assert(center[2] >= 254);

    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_vignette_disabled_entry_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_disabled.png");
    Vignette value = vignette_defaults();
    value.intensity = 1;
    value.radius = 0.2f;
    ecs_entity_t vignette = vignette_effect(world, &value);
    unsigned char *base = vignette_frame(world, view, camera, NULL, 0, 64, 64,
        "vignette_disabled.png");
    View v = {.camera = camera, .clear = {.enabled = true, .color = {255, 255, 255, 255}}};
    ecs_vec_init_t(NULL, &v.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &v.effects, ViewEffect) = (ViewEffect){
        .enabled = false, .effect = vignette };
    ecs_set_ptr(world, view, View, &v);
    ecs_vec_fini_t(NULL, &v.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    unsigned char *px = effects_load(world, "vignette_disabled.png");
    test_assert(vignette_diff(base, px, 64 * 64) <= 1);
    stbi_image_free(px);

    ecs_add_id(world, vignette, EcsDisabled);
    unsigned char *off = vignette_frame(world, view, camera, &vignette, 1, 64, 64,
        "vignette_disabled.png");
    test_assert(vignette_diff(base, off, 64 * 64) <= 1);

    ecs_os_free(base);
    ecs_os_free(off);
    ecs_fini(world);
}

void Effects_vignette_same_entity_after_tonemap(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_order.png");
    Vignette value = vignette_defaults();
    value.intensity = 0.6f;
    value.radius = 0.3f;
    ecs_entity_t both = vignette_effect(world, &value);
    ecs_add(world, both, Tonemap);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t vignette = vignette_effect(world, &value);
    ecs_entity_t chain[2] = {tonemap, vignette};
    unsigned char *a = vignette_frame(world, view, camera, &both, 1, 64, 64,
        "vignette_order.png");
    unsigned char *b = vignette_frame(world, view, camera, chain, 2, 64, 64,
        "vignette_order.png");
    test_assert(vignette_diff(a, b, 64 * 64) <= 1);
    test_assert(vignette_px(a, 64, 0, 0)[0] < vignette_px(a, 64, 32, 32)[0] - 8);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

void Effects_vignette_follows_surface_resize(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "vignette_resize.png");
    Vignette value = vignette_defaults();
    value.intensity = 1;
    value.radius = 0.4f;
    value.smoothness = 0.3f;
    ecs_entity_t vignette = vignette_effect(world, &value);
    unsigned char *first = vignette_frame(world, view, camera, &vignette, 1, 64, 64,
        "vignette_resize.png");
    ecs_os_free(first);
    ecs_iter_t it = ecs_each(world, Surface);
    ecs_entity_t surface = 0;
    while (ecs_each_next(&it)) {
        surface = it.entities[0];
    }

    test_assert(surface != 0);
    ecs_set(world, surface, Surface, {.width = 128, .height = 64, .resolution_scale = 2,
        .write_to_file = "vignette_resize.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, surface, Surface, {.width = 96, .height = 48, .resolution_scale = 0.5f,
        .write_to_file = "vignette_resize.png"});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/vignette_resize.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 96);
    test_int(h, 48);
    test_assert(px[(24 * 96 + 48) * 4] >= 250);
    test_assert(px[0] < 8);
    test_assert(px[(47 * 96 + 95) * 4] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Get the default chromatic aberration settings.
 *
 * @return The settings a constructed ChromaticAberration has
 */
static ChromaticAberration ca_defaults(void) {
    return (ChromaticAberration){
        .intensity = 0.005f, .samples = 8, .center = {0.5f, 0.5f}, .falloff = 2
    };
}

/** Create an effect entity with chromatic aberration settings.
 *
 * @param world The world
 * @param value The settings
 * @return The entity
 */
static ecs_entity_t ca_effect(
    ecs_world_t *world,
    const ChromaticAberration *value)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set_ptr(world, result, ChromaticAberration, value);

    return result;
}

/** Create a white vertical line through the image that reflects nothing.
 *
 * @param world The world
 * @param x Position of its center along X; 1 unit is about 11 pixels of a 64x64 capture
 * @return The line
 */
static ecs_entity_t ca_line(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {x, 0, 0});
    ecs_set(world, result, Box, {0.2f, 10, 0.2f});
    ecs_set(world, result, Rgb, {0, 0, 0});
    ecs_set(world, result, Emissive, {.strength = 1, .color = {255, 255, 255}});

    return result;
}

/** Set the effects of the view with a clear color, render frames and keep a copy of the capture.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param clear The clear color
 * @param effects The effect entities, in order
 * @param count Number of effects
 * @param width Width of the capture
 * @param height Height of the capture
 * @param file File name of the capture
 * @return The RGBA pixels, freed with ecs_os_free
 */
static unsigned char* ca_frame(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    rgba clear,
    const ecs_entity_t *effects,
    int32_t count,
    int32_t width,
    int32_t height,
    const char *file)
{
    View value = {.camera = camera, .clear = {.enabled = true, .color = clear}};
    int32_t i;
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, count);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = effects[i] };
    }

    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, width);
    test_int(h, height);
    ecs_os_free(path);
    unsigned char *result = ecs_os_malloc(width * height * 4);
    ecs_os_memcpy(result, px, width * height * 4);
    stbi_image_free(px);

    return result;
}

/** Find the brightest column of a row of a 64 wide capture within a range.
 *
 * @param px The pixels
 * @param y The row
 * @param x0 First column
 * @param x1 Last column
 * @return The column
 */
static int32_t ca_peak(
    const unsigned char *px,
    int32_t y,
    int32_t x0,
    int32_t x1)
{
    int32_t x, result = x0;
    for (x = x0; x <= x1; x ++) {
        if (vignette_px(px, 64, x, y)[1] > vignette_px(px, 64, result, y)[1]) {
            result = x;
        }
    }

    return result;
}

/** Count the pixels of a row of a 64 wide capture whose red and blue differ by more than a
 * threshold, the width of the color fringes.
 *
 * @param px The pixels
 * @param y The row
 * @param x0 First column
 * @param x1 Last column
 * @param threshold Smallest difference that counts
 * @return The number of pixels
 */
static int32_t ca_fringe(
    const unsigned char *px,
    int32_t y,
    int32_t x0,
    int32_t x1,
    int threshold)
{
    int32_t x, result = 0;
    for (x = x0; x <= x1; x ++) {
        const unsigned char *p = vignette_px(px, 64, x, y);
        result += abs((int)p[0] - (int)p[2]) > threshold;
    }

    return result;
}

void Effects_chromatic_aberration_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ChromaticAberration);
    const ChromaticAberration *v = ecs_get(world, e, ChromaticAberration);
    test_flt(v->intensity, 0.005f);
    test_int(v->samples, 8);
    test_flt(v->center[0], 0.5f);
    test_flt(v->center[1], 0.5f);
    test_flt(v->falloff, 2);

    ChromaticAberration resolved;
    box_render_chromatic_aberration_settings(v, &resolved);
    test_flt(resolved.intensity, 0.005f);
    test_int(resolved.samples, 8);
    test_flt(resolved.center[0], 0.5f);
    test_flt(resolved.falloff, 2);

    test_assert(ecs_script_run(world, "chromatic_aberration", "using box.effects\n"
        "lens { ChromaticAberration: {} }\n", NULL) == 0);
    const ChromaticAberration *scripted = ecs_get(world, ecs_lookup(world, "lens"),
        ChromaticAberration);
    test_assert(scripted != NULL);
    test_flt(scripted->intensity, 0.005f);
    test_int(scripted->samples, 8);
    test_flt(scripted->center[1], 0.5f);
    test_flt(scripted->falloff, 2);

    ecs_fini(world);
}

void Effects_chromatic_aberration_settings_clamped(void) {
    // lighting/effects
    ChromaticAberration out;
    box_render_chromatic_aberration_settings(&(ChromaticAberration){.intensity = -1,
        .samples = 1, .center = {-0.5f, 2}, .falloff = -1}, &out);
    test_flt(out.intensity, 0);
    test_int(out.samples, 3);
    test_flt(out.center[0], -0.5f);
    test_flt(out.center[1], 2);
    test_flt(out.falloff, 0);

    box_render_chromatic_aberration_settings(&(ChromaticAberration){.intensity = 0.5f,
        .samples = 40, .falloff = 3}, &out);
    test_flt(out.intensity, 0.5f);
    test_int(out.samples, 16);
    test_flt(out.falloff, 3);
}

void Effects_chromatic_aberration_flat_unchanged(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_flat.png");
    ChromaticAberration value = ca_defaults();
    value.intensity = 0.1f;
    value.samples = 16;
    value.falloff = 1;
    ecs_entity_t ca = ca_effect(world, &value);
    const rgba clears[2] = {{255, 255, 255, 255}, {200, 100, 50, 255}};
    int32_t i;
    for (i = 0; i < 2; i ++) {
        unsigned char *base = ca_frame(world, view, camera, clears[i], NULL, 0, 64, 64,
            "ca_flat.png");
        unsigned char *px = ca_frame(world, view, camera, clears[i], &ca, 1, 64, 64,
            "ca_flat.png");
        // The weights of each channel sum to 1: a white image stays white
        test_assert(vignette_diff(base, px, 64 * 64) <= 1);
        ecs_os_free(base);
        ecs_os_free(px);
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_fini(world);
}

void Effects_chromatic_aberration_edge_line_fringes(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_line.png");
    ca_line(world, 0);
    ca_line(world, 2.2f);
    // The default falloff of 2: about 1.9 pixels at the edge line, a hundredth at the center
    ChromaticAberration value = ca_defaults();
    value.intensity = 0.1f;
    ecs_entity_t ca = ca_effect(world, &value);
    const rgba black = {0, 0, 0, 255};
    unsigned char *base = ca_frame(world, view, camera, black, NULL, 0, 64, 64,
        "ca_line.png");
    unsigned char *px = ca_frame(world, view, camera, black, &ca, 1, 64, 64, "ca_line.png");
    int32_t edge = ca_peak(base, 32, 44, 62);
    test_assert(vignette_px(base, 64, edge, 32)[1] > 200);
    test_assert(edge > 50);

    // Red moves away from the center and blue toward it: red right of the line, blue left
    int32_t x;
    bool red = false, blue = false;
    for (x = edge + 1; x <= glm_imin(edge + 5, 63); x ++) {
        const unsigned char *p = vignette_px(px, 64, x, 32);
        red |= (int)p[0] > (int)p[2] + 24;
    }

    for (x = edge - 5; x < edge; x ++) {
        const unsigned char *p = vignette_px(px, 64, x, 32);
        blue |= (int)p[2] > (int)p[0] + 24;
    }

    test_assert(red);
    test_assert(blue);

    // The line at the center keeps its colors, compared in linear light since a hundredth of
    // a pixel of white on black encodes far above 1/255
    int32_t y;
    for (y = 30; y <= 34; y ++) {
        for (x = 24; x <= 40; x ++) {
            const unsigned char *p = vignette_px(px, 64, x, y);
            test_assert(fabsf(effects_linear(p[0]) - effects_linear(p[2])) < 0.05f);
            test_assert(fabsf(effects_linear(p[0]) - effects_linear(p[1])) < 0.05f);
        }
    }

    // The center tap keeps alpha
    test_int(vignette_px(px, 64, edge, 32)[3], 255);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_chromatic_aberration_zero_intensity_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_zero.png");
    ca_line(world, 0);
    ca_line(world, 2.2f);
    ChromaticAberration value = ca_defaults();
    value.intensity = 0;
    value.falloff = 0;
    ecs_entity_t ca = ca_effect(world, &value);
    const rgba black = {0, 0, 0, 255};
    unsigned char *base = ca_frame(world, view, camera, black, NULL, 0, 64, 64,
        "ca_zero.png");
    unsigned char *px = ca_frame(world, view, camera, black, &ca, 1, 64, 64, "ca_zero.png");
    test_assert(ca_peak(base, 32, 44, 62) > 50);
    test_assert(vignette_diff(base, px, 64 * 64) <= 1);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_chromatic_aberration_zero_literal_applies_as_set(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_literal.png");
    ca_line(world, 0);
    ecs_entity_t zero = ecs_new(world);
    ecs_set(world, zero, ChromaticAberration, {0});
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, ChromaticAberration, {.intensity = 0.08f});
    const rgba black = {0, 0, 0, 255};
    unsigned char *base = ca_frame(world, view, camera, black, NULL, 0, 64, 64,
        "ca_literal.png");
    unsigned char *z = ca_frame(world, view, camera, black, &zero, 1, 64, 64,
        "ca_literal.png");
    // Intensity 0 leaves the image as is
    test_assert(vignette_diff(base, z, 64 * 64) <= 1);

    // Falloff 0 is the full offset everywhere but the center: the center line fringes too,
    // with 0 samples clamped to 3
    unsigned char *f = ca_frame(world, view, camera, black, &flat, 1, 64, 64,
        "ca_literal.png");
    test_assert(ca_fringe(f, 32, 20, 44, 24) >= 2);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(z);
    ecs_os_free(f);
    ecs_fini(world);
}

void Effects_chromatic_aberration_fringe_grows_with_intensity(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_grow.png");
    ca_line(world, 2.2f);
    ChromaticAberration value = ca_defaults();
    value.falloff = 1;
    value.samples = 16;
    value.intensity = 0.04f;
    ecs_entity_t weak = ca_effect(world, &value);
    value.intensity = 0.12f;
    ecs_entity_t strong = ca_effect(world, &value);
    const rgba black = {0, 0, 0, 255};
    unsigned char *w = ca_frame(world, view, camera, black, &weak, 1, 64, 64, "ca_grow.png");
    unsigned char *s = ca_frame(world, view, camera, black, &strong, 1, 64, 64,
        "ca_grow.png");
    int32_t fw = ca_fringe(w, 32, 40, 63, 12);
    int32_t fs = ca_fringe(s, 32, 40, 63, 12);
    test_assert(fw >= 1);
    test_assert(fs > fw);

    ecs_os_free(w);
    ecs_os_free(s);
    ecs_fini(world);
}

void Effects_chromatic_aberration_disabled_entry_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_disabled.png");
    ca_line(world, 2.2f);
    ChromaticAberration value = ca_defaults();
    value.intensity = 0.1f;
    value.falloff = 1;
    ecs_entity_t ca = ca_effect(world, &value);
    const rgba black = {0, 0, 0, 255};
    unsigned char *base = ca_frame(world, view, camera, black, NULL, 0, 64, 64,
        "ca_disabled.png");
    View v = {.camera = camera, .clear = {.enabled = true, .color = black}};
    ecs_vec_init_t(NULL, &v.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &v.effects, ViewEffect) = (ViewEffect){
        .enabled = false, .effect = ca };
    ecs_set_ptr(world, view, View, &v);
    ecs_vec_fini_t(NULL, &v.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    unsigned char *px = effects_load(world, "ca_disabled.png");
    test_assert(vignette_diff(base, px, 64 * 64) <= 1);
    stbi_image_free(px);

    ecs_add_id(world, ca, EcsDisabled);
    unsigned char *off = ca_frame(world, view, camera, black, &ca, 1, 64, 64,
        "ca_disabled.png");
    test_assert(vignette_diff(base, off, 64 * 64) <= 1);

    ecs_os_free(base);
    ecs_os_free(off);
    ecs_fini(world);
}

void Effects_chromatic_aberration_same_entity_order(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_order.png");
    ca_line(world, 2.2f);
    ChromaticAberration value = ca_defaults();
    value.intensity = 0.1f;
    value.falloff = 1;
    Vignette vig = vignette_defaults();
    vig.intensity = 0.6f;
    vig.radius = 0.3f;
    ecs_entity_t all = ca_effect(world, &value);
    ecs_add(world, all, Tonemap);
    ecs_set_ptr(world, all, Vignette, &vig);
    ecs_entity_t chain[3] = {ecs_new(world), ca_effect(world, &value),
        vignette_effect(world, &vig)};
    ecs_add(world, chain[0], Tonemap);
    const rgba black = {0, 0, 0, 255};
    unsigned char *a = ca_frame(world, view, camera, black, &all, 1, 64, 64, "ca_order.png");
    unsigned char *b = ca_frame(world, view, camera, black, chain, 3, 64, 64, "ca_order.png");
    // One entity runs Tonemap, ChromaticAberration and Vignette in that order
    test_assert(vignette_diff(a, b, 64 * 64) <= 1);
    test_assert(ca_fringe(a, 32, 40, 63, 8) >= 1);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

void Effects_chromatic_aberration_follows_surface_resize(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = vignette_world(&camera, &view, 64, 64, "ca_resize.png");
    ca_line(world, 0);
    ChromaticAberration value = ca_defaults();
    value.intensity = 0.05f;
    ecs_entity_t ca = ca_effect(world, &value);
    unsigned char *first = ca_frame(world, view, camera, (rgba){0, 0, 0, 255}, &ca, 1, 64, 64,
        "ca_resize.png");
    ecs_os_free(first);
    ecs_iter_t it = ecs_each(world, Surface);
    ecs_entity_t surface = 0;
    while (ecs_each_next(&it)) {
        surface = it.entities[0];
    }

    test_assert(surface != 0);
    ecs_set(world, surface, Surface, {.width = 128, .height = 64, .resolution_scale = 2,
        .write_to_file = "ca_resize.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, surface, Surface, {.width = 96, .height = 48, .resolution_scale = 0.5f,
        .write_to_file = "ca_resize.png"});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/ca_resize.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 96);
    test_int(h, 48);
    // The line at the center stays white, the black edges stay black
    test_assert(px[(24 * 96 + 48) * 4 + 1] >= 200);
    test_assert(px[0] < 8);
    test_assert(px[(47 * 96 + 95) * 4] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Width and height of the depth of field captures */
#define DOF_W (384)
#define DOF_H (128)

/** Column of the center of the quads at 5, 10 and 40 units */
static const int32_t dof_centers[3] = {64, 192, 320};

/** Create a world that renders headless into a surface, with a camera at the origin that looks
 * down -Z and three white quads facing it at view depth 5, 10 and 40, each 64x64 pixels of a
 * 384x128 capture, centered on row 64 and columns 64, 192 and 320.
 *
 * @param camera Receives the camera
 * @param view Receives the view
 * @param file File name of the capture in the temporary folder
 * @return The world
 */
static ecs_world_t* dof_world(
    ecs_entity_t *camera,
    ecs_entity_t *view,
    const char *file)
{
    const float depths[3] = {5, 10, 40};
    const float ndc[3] = {-2.0f / 3.0f, 0, 2.0f / 3.0f};
    float t = tanf(glm_rad(30));
    int32_t i;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    *view = ecs_new(world);
    for (i = 0; i < 3; i ++) {
        float z = depths[i], size = t * z;
        ecs_entity_t q = ecs_new(world);
        ecs_set(world, q, Position, {ndc[i] * t * z * 3.0f, 0, -z});
        ecs_set(world, q, Quad, {size, size});
        ecs_set(world, q, Rgb, {255, 255, 255});
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = DOF_W, .height = DOF_H,
        .write_to_file = ECS_CONST_CAST(char*, file),
        .resolution_scale = 1});

    return world;
}

/** Get the default depth of field settings with the largest radius, 6.4 pixels of the capture.
 *
 * @return The settings
 */
static DepthOfField dof_wide(void) {
    return (DepthOfField){
        .focus_distance = 10, .focus_range = 2, .near_transition = 4, .far_transition = 20,
        .max_radius = 0.05f, .near_blur = true
    };
}

/** Create an effect entity with depth of field settings.
 *
 * @param world The world
 * @param value The settings
 * @return The entity
 */
static ecs_entity_t dof_effect(
    ecs_world_t *world,
    const DepthOfField *value)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set_ptr(world, result, DepthOfField, value);

    return result;
}

/** Set the effects of the view with a black clear, render frames and keep a copy of the capture.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param effects The effect entities, in order
 * @param count Number of effects
 * @param enabled Whether the entries are enabled
 * @param file File name of the capture
 * @return The RGBA pixels of a DOF_W x DOF_H capture, freed with ecs_os_free
 */
static unsigned char* dof_frame(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    const ecs_entity_t *effects,
    int32_t count,
    bool enabled,
    const char *file)
{
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    int32_t i;
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, count);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = enabled, .effect = effects[i] };
    }

    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, DOF_W);
    test_int(h, DOF_H);
    ecs_os_free(path);
    unsigned char *result = ecs_os_malloc(DOF_W * DOF_H * 4);
    ecs_os_memcpy(result, px, DOF_W * DOF_H * 4);
    stbi_image_free(px);

    return result;
}

/** Get the edge contrast of a quad: the largest step of the red channel between neighbors
 * along the middle row, within 12 pixels of its left edge.
 *
 * @param px The pixels of a DOF_W x DOF_H capture
 * @param quad Index of the quad, 0 near, 1 in focus, 2 far
 * @return The largest step
 */
static int dof_edge(
    const unsigned char *px,
    int32_t quad)
{
    int32_t edge = dof_centers[quad] - 32, x, y = DOF_H / 2;
    int result = 0;
    for (x = edge - 12; x < edge + 12; x ++) {
        result = glm_imax(result, abs((int)px[(y * DOF_W + x + 1) * 4] -
            (int)px[(y * DOF_W + x) * 4]));
    }

    return result;
}

/** Count the pixels of the middle row within 12 pixels of the left edge of a quad whose red
 * channel lies strictly between 16 and 239.
 *
 * @param px The pixels of a DOF_W x DOF_H capture
 * @param quad Index of the quad, 0 near, 1 in focus, 2 far
 * @return The count
 */
static int32_t dof_partial(
    const unsigned char *px,
    int32_t quad)
{
    int32_t edge = dof_centers[quad] - 32, x, y = DOF_H / 2, result = 0;
    for (x = edge - 12; x < edge + 12; x ++) {
        unsigned char r = px[(y * DOF_W + x) * 4];
        result += r > 16 && r < 239;
    }

    return result;
}

/** Get the largest difference of a channel between two depth of field captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The largest difference
 */
static int dof_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i;
    int result = 0;
    for (i = 0; i < DOF_W * DOF_H * 4; i ++) {
        result = glm_imax(result, abs((int)a[i] - (int)b[i]));
    }

    return result;
}

void Effects_depth_of_field_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DepthOfField);
    const DepthOfField *d = ecs_get(world, e, DepthOfField);
    test_flt(d->focus_distance, 10);
    test_flt(d->focus_range, 2);
    test_flt(d->near_transition, 4);
    test_flt(d->far_transition, 20);
    test_flt(d->max_radius, 0.01f);
    test_bool(d->near_blur, true);

    DepthOfField resolved;
    box_render_depth_of_field_settings(d, &resolved);
    test_flt(resolved.focus_distance, 10);
    test_flt(resolved.focus_range, 2);
    test_flt(resolved.near_transition, 4);
    test_flt(resolved.far_transition, 20);
    test_flt(resolved.max_radius, 0.01f);
    test_bool(resolved.near_blur, true);

    test_assert(ecs_script_run(world, "dof", "using box.effects\n"
        "lens { DepthOfField: {} }\n", NULL) == 0);
    const DepthOfField *scripted = ecs_get(world, ecs_lookup(world, "lens"), DepthOfField);
    test_assert(scripted != NULL);
    test_flt(scripted->focus_distance, 10);
    test_flt(scripted->far_transition, 20);
    test_flt(scripted->max_radius, 0.01f);
    test_bool(scripted->near_blur, true);

    ecs_fini(world);
}

void Effects_depth_of_field_settings_clamped(void) {
    // lighting/effects
    DepthOfField out;
    box_render_depth_of_field_settings(&(DepthOfField){.focus_distance = -1, .focus_range = -1,
        .near_transition = -1, .far_transition = -1, .max_radius = 1}, &out);
    test_flt(out.focus_distance, 0);
    test_flt(out.focus_range, 0);
    test_flt(out.near_transition, 0);
    test_flt(out.far_transition, 0);
    test_flt(out.max_radius, 0.05f);
    test_bool(out.near_blur, false);
}

void Effects_depth_of_field_blurs_near_and_far(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_blur.png");
    DepthOfField value = dof_wide();
    ecs_entity_t dof = dof_effect(world, &value);
    unsigned char *base = dof_frame(world, view, camera, NULL, 0, true, "dof_blur.png");
    unsigned char *px = dof_frame(world, view, camera, &dof, 1, true, "dof_blur.png");
    int32_t i;
    for (i = 0; i < 3; i ++) {
        // Every quad has a hard edge without the effect, white inside
        test_assert(dof_edge(base, i) > 200);
        test_assert(base[(DOF_H / 2 * DOF_W + dof_centers[i]) * 4] > 250);
    }

    // The quad in focus keeps its edge, the near and far ones are blurred
    test_assert(dof_edge(px, 1) >= dof_edge(base, 1) - 16);
    test_assert(dof_partial(px, 1) <= dof_partial(base, 1) + 1);
    test_assert(dof_edge(px, 0) < dof_edge(base, 0) / 2);
    test_assert(dof_edge(px, 2) < dof_edge(base, 2) / 2);
    test_assert(dof_partial(px, 0) >= dof_partial(base, 0) + 4);
    test_assert(dof_partial(px, 2) >= dof_partial(base, 2) + 4);

    // The middle of every quad and the black far from them stay as they are
    for (i = 0; i < 3; i ++) {
        test_assert(px[(DOF_H / 2 * DOF_W + dof_centers[i]) * 4] > 245);
    }

    test_assert(px[(4 * DOF_W + 128) * 4] < 4);
    test_assert(px[(4 * DOF_W + 256) * 4] < 4);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_depth_of_field_near_blur_off(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_near_off.png");
    DepthOfField value = dof_wide();
    value.near_blur = false;
    ecs_entity_t dof = dof_effect(world, &value);
    unsigned char *base = dof_frame(world, view, camera, NULL, 0, true, "dof_near_off.png");
    unsigned char *px = dof_frame(world, view, camera, &dof, 1, true, "dof_near_off.png");
    test_assert(dof_edge(px, 0) >= dof_edge(base, 0) - 16);
    test_assert(dof_partial(px, 0) <= dof_partial(base, 0) + 1);
    test_assert(dof_edge(px, 1) >= dof_edge(base, 1) - 16);
    test_assert(dof_edge(px, 2) < dof_edge(base, 2) / 2);

    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_depth_of_field_zero_radius_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_zero.png");
    DepthOfField value = dof_wide();
    value.max_radius = 0;
    ecs_entity_t dof = dof_effect(world, &value);
    unsigned char *base = dof_frame(world, view, camera, NULL, 0, true, "dof_zero.png");
    unsigned char *px = dof_frame(world, view, camera, &dof, 1, true, "dof_zero.png");
    test_assert(dof_diff(base, px) <= 1);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_depth_of_field_zero_literal_applies_as_set(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_literal.png");
    ecs_entity_t zero = ecs_new(world);
    ecs_set(world, zero, DepthOfField, {0});
    ecs_entity_t wide = ecs_new(world);
    ecs_set(world, wide, DepthOfField, {.max_radius = 0.05f});
    unsigned char *base = dof_frame(world, view, camera, NULL, 0, true, "dof_literal.png");
    unsigned char *z = dof_frame(world, view, camera, &zero, 1, true, "dof_literal.png");
    // A largest radius of 0 leaves the image as is
    test_assert(dof_diff(base, z) <= 1);

    // Focus 0 with a hard step: everything is far at full blur
    unsigned char *w = dof_frame(world, view, camera, &wide, 1, true, "dof_literal.png");
    int32_t i;
    for (i = 0; i < 3; i ++) {
        test_assert(dof_edge(w, i) < dof_edge(base, i) / 2);
    }

    ecs_os_free(base);
    ecs_os_free(z);
    ecs_os_free(w);
    ecs_fini(world);
}

void Effects_depth_of_field_disabled_entry_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_disabled.png");
    DepthOfField value = dof_wide();
    ecs_entity_t dof = dof_effect(world, &value);
    unsigned char *base = dof_frame(world, view, camera, NULL, 0, true, "dof_disabled.png");
    unsigned char *off = dof_frame(world, view, camera, &dof, 1, false, "dof_disabled.png");
    test_assert(dof_diff(base, off) <= 1);
    ecs_os_free(off);

    ecs_add_id(world, dof, EcsDisabled);
    off = dof_frame(world, view, camera, &dof, 1, true, "dof_disabled.png");
    test_assert(dof_diff(base, off) <= 1);

    ecs_os_free(base);
    ecs_os_free(off);
    ecs_fini(world);
}

void Effects_depth_of_field_same_entity_before_tonemap(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_order.png");
    DepthOfField value = dof_wide();
    ecs_entity_t both = dof_effect(world, &value);
    ecs_add(world, both, Tonemap);
    ecs_entity_t dof = dof_effect(world, &value);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t chain[2] = {dof, tonemap};
    unsigned char *a = dof_frame(world, view, camera, &both, 1, true, "dof_order.png");
    unsigned char *b = dof_frame(world, view, camera, chain, 2, true, "dof_order.png");
    test_assert(dof_diff(a, b) <= 1);
    test_assert(dof_edge(a, 2) < dof_edge(a, 1) / 2);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

void Effects_depth_of_field_follows_surface_resize(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_resize.png");
    DepthOfField value = dof_wide();
    ecs_entity_t dof = dof_effect(world, &value);
    unsigned char *first = dof_frame(world, view, camera, &dof, 1, true, "dof_resize.png");
    test_assert(dof_edge(first, 2) < 128);
    ecs_os_free(first);
    ecs_iter_t it = ecs_each(world, Surface);
    ecs_entity_t surface = 0;
    while (ecs_each_next(&it)) {
        surface = it.entities[0];
    }

    test_assert(surface != 0);
    ecs_set(world, surface, Surface, {.width = 2 * DOF_W, .height = 2 * DOF_H,
        .resolution_scale = 0.5f, .write_to_file = "dof_resize.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, surface, Surface, {.width = 3 * DOF_W / 2, .height = 3 * DOF_H / 2,
        .resolution_scale = 1, .write_to_file = "dof_resize.png"});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/dof_resize.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    int32_t x, y = 3 * DOF_H / 4, sharp = 0, soft = 0;
    test_assert(px != NULL);
    test_int(w, 3 * DOF_W / 2);
    test_int(h, 3 * DOF_H / 2);
    // The quad in focus keeps its hard left edge at column 240, the far one at 432 is soft
    for (x = 222; x < 258; x ++) {
        sharp = glm_imax(sharp, abs((int)px[(y * w + x + 1) * 4] - (int)px[(y * w + x) * 4]));
    }

    for (x = 414; x < 450; x ++) {
        soft = glm_imax(soft, abs((int)px[(y * w + x + 1) * 4] - (int)px[(y * w + x) * 4]));
    }

    test_assert(sharp > 200);
    test_assert(soft < 128);
    test_assert(px[(y * w + 288) * 4] > 245);
    test_assert(px[(4 * w + 192) * 4] < 4);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Width and height of the tilt shift captures */
#define TILT_W (256)
#define TILT_H (256)

/** Create a world that renders headless into a surface, with a camera at the origin that looks
 * down -Z and vertical stripes at view depth z facing it: 16 white quads 8 pixels wide every 16
 * pixels, starting at column 0, over the whole height of a 256x256 capture.
 *
 * @param camera Receives the camera
 * @param view Receives the view
 * @param z View depth of the stripes
 * @param file File name of the capture in the temporary folder
 * @return The world
 */
static ecs_world_t* tilt_world(
    ecs_entity_t *camera,
    ecs_entity_t *view,
    float z,
    const char *file)
{
    float t = tanf(glm_rad(30)), px = 2.0f * t * z / TILT_H;
    int32_t i;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    *view = ecs_new(world);
    for (i = 0; i < 16; i ++) {
        ecs_entity_t q = ecs_new(world);
        ecs_set(world, q, Position, {((float)(16 * i + 4) - TILT_W / 2) * px, 0, -z});
        ecs_set(world, q, Quad, {8 * px, 1.2f * TILT_H * px});
        ecs_set(world, q, Rgb, {255, 255, 255});
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = TILT_W, .height = TILT_H,
        .write_to_file = ECS_CONST_CAST(char*, file),
        .resolution_scale = 1});

    return world;
}

/** Set the effects of the view with a black clear, render frames and keep a copy of the capture.
 *
 * @param world The world
 * @param view The view
 * @param camera The camera
 * @param effect The effect entity, 0 for none
 * @param file File name of the capture
 * @return The RGBA pixels of a TILT_W x TILT_H capture, freed with ecs_os_free
 */
static unsigned char* tilt_frame(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t camera,
    ecs_entity_t effect,
    const char *file)
{
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    if (effect) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = effect };
    }

    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, TILT_W);
    test_int(h, TILT_H);
    ecs_os_free(path);
    unsigned char *result = ecs_os_malloc(TILT_W * TILT_H * 4);
    ecs_os_memcpy(result, px, TILT_W * TILT_H * 4);
    stbi_image_free(px);

    return result;
}

/** Get the contrast of a row of a tilt shift capture: the largest minus the smallest red channel
 * of the columns 32 to 223.
 *
 * @param px The pixels of a TILT_W x TILT_H capture
 * @param y The row
 * @return The contrast
 */
static int tilt_contrast(
    const unsigned char *px,
    int32_t y)
{
    int32_t x;
    int lo = 255, hi = 0;
    for (x = 32; x < 224; x ++) {
        int r = px[(y * TILT_W + x) * 4];
        lo = glm_imin(lo, r);
        hi = glm_imax(hi, r);
    }

    return hi - lo;
}

/** Create a tilt shift effect entity with the defaults and the largest radius, 12.8 pixels of
 * the capture.
 *
 * @param world The world
 * @return The entity
 */
static ecs_entity_t tilt_effect(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_new(world);
    DepthOfField *d = ecs_ensure(world, result, DepthOfField);
    d->mode = DofTiltShift;
    d->max_radius = 0.05f;
    ecs_modified(world, result, DepthOfField);

    return result;
}

void Effects_depth_of_field_tilt_default_settings(void) {
    // lighting/effects
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DepthOfField);
    const DepthOfField *d = ecs_get(world, e, DepthOfField);
    test_int(d->mode, DofCamera);
    test_flt(d->tilt_center, 0.55f);
    test_flt(d->tilt_band, 0.08f);
    test_flt(d->tilt_transition, 0.3f);
    test_flt(d->tilt_angle, 0);

    test_assert(ecs_script_run(world, "tilt", "using box.effects\n"
        "lens { DepthOfField: {mode: DofTiltShift} }\n", NULL) == 0);
    const DepthOfField *scripted = ecs_get(world, ecs_lookup(world, "lens"), DepthOfField);
    test_assert(scripted != NULL);
    test_int(scripted->mode, DofTiltShift);
    test_flt(scripted->tilt_center, 0.55f);
    test_flt(scripted->tilt_band, 0.08f);
    test_flt(scripted->tilt_transition, 0.3f);
    test_flt(scripted->tilt_angle, 0);
    test_flt(scripted->max_radius, 0.01f);

    DepthOfField resolved;
    box_render_depth_of_field_settings(scripted, &resolved);
    test_int(resolved.mode, DofTiltShift);
    test_bool(resolved.near_blur, false);

    ecs_fini(world);
}

void Effects_depth_of_field_tilt_shift_stripes(void) {
    // lighting/effects
    const float depths[2] = {5, 40};
    unsigned char *px[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_entity_t camera, view;
        ecs_world_t *world = tilt_world(&camera, &view, depths[i], "tilt_stripes.png");
        ecs_entity_t tilt = tilt_effect(world);
        unsigned char *base = tilt_frame(world, view, camera, 0, "tilt_stripes.png");
        px[i] = tilt_frame(world, view, camera, tilt, "tilt_stripes.png");
        // The stripes are sharp everywhere without the effect
        test_assert(tilt_contrast(base, 8) > 240);
        test_assert(tilt_contrast(base, 141) > 240);
        test_assert(tilt_contrast(base, 247) > 240);
        // Sharp at tilt_center, row 141, blurred at the top and bottom
        test_assert(tilt_contrast(px[i], 141) > 240);
        test_assert(tilt_contrast(px[i], 8) < 64);
        test_assert(tilt_contrast(px[i], 247) < 64);
        // The blur grows away from the band on both sides, measured on the ramp next to the band
        // edges at rows 120 and 161, before a disc blur of the stripes first cancels them out
        test_assert(tilt_contrast(px[i], 88) > tilt_contrast(px[i], 8));
        test_assert(tilt_contrast(px[i], 200) > tilt_contrast(px[i], 247));

        box_render_gpu_stats_t stats;
        test_assert(box_render_gpu_stats(world, &stats));
        test_int(stats.errors, 0);
        ecs_os_free(base);
        ecs_fini(world);
    }

    // The same image at a different depth blurs the same
    int32_t diff = 0;
    for (i = 0; i < TILT_W * TILT_H * 4; i ++) {
        diff = glm_imax(diff, abs((int)px[0][i] - (int)px[1][i]));
    }

    test_assert(diff <= 8);
    ecs_os_free(px[0]);
    ecs_os_free(px[1]);
}

void Effects_depth_of_field_tilt_shift_center(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = tilt_world(&camera, &view, 10, "tilt_center.png");
    ecs_entity_t tilt = tilt_effect(world);
    DepthOfField *d = ecs_ensure(world, tilt, DepthOfField);
    d->tilt_center = 0.25f;
    ecs_modified(world, tilt, DepthOfField);
    unsigned char *px = tilt_frame(world, view, camera, tilt, "tilt_center.png");
    // The band moves up to row 64, the bottom is blurred
    test_assert(tilt_contrast(px, 64) > 240);
    test_assert(tilt_contrast(px, 247) < 64);
    test_assert(tilt_contrast(px, 141) < tilt_contrast(px, 64) / 2);

    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_depth_of_field_tilt_shift_zero_radius_no_op(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = tilt_world(&camera, &view, 10, "tilt_zero.png");
    ecs_entity_t tilt = tilt_effect(world);
    DepthOfField *d = ecs_ensure(world, tilt, DepthOfField);
    d->max_radius = 0;
    ecs_modified(world, tilt, DepthOfField);
    unsigned char *base = tilt_frame(world, view, camera, 0, "tilt_zero.png");
    unsigned char *px = tilt_frame(world, view, camera, tilt, "tilt_zero.png");
    int32_t i;
    int diff = 0;
    for (i = 0; i < TILT_W * TILT_H * 4; i ++) {
        diff = glm_imax(diff, abs((int)base[i] - (int)px[i]));
    }

    test_assert(diff <= 1);
    ecs_os_free(base);
    ecs_os_free(px);
    ecs_fini(world);
}

void Effects_depth_of_field_camera_ignores_tilt(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = dof_world(&camera, &view, "dof_camera_tilt.png");
    DepthOfField value = dof_wide();
    ecs_entity_t plain = dof_effect(world, &value);
    value.tilt_center = 0.1f;
    value.tilt_band = 0;
    value.tilt_transition = 0.01f;
    value.tilt_angle = 1;
    ecs_entity_t tilted = dof_effect(world, &value);
    unsigned char *a = dof_frame(world, view, camera, &plain, 1, true, "dof_camera_tilt.png");
    unsigned char *b = dof_frame(world, view, camera, &tilted, 1, true, "dof_camera_tilt.png");
    test_assert(dof_diff(a, b) <= 1);
    test_assert(dof_edge(a, 1) > 200);
    test_assert(dof_edge(a, 2) < dof_edge(a, 1) / 2);

    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

void Effects_depth_of_field_tilt_shift_follows_surface_resize(void) {
    // lighting/effects
    ecs_entity_t camera, view;
    ecs_world_t *world = tilt_world(&camera, &view, 10, "tilt_resize.png");
    ecs_entity_t tilt = tilt_effect(world);
    unsigned char *first = tilt_frame(world, view, camera, tilt, "tilt_resize.png");
    test_assert(tilt_contrast(first, 8) < 64);
    ecs_os_free(first);
    ecs_iter_t it = ecs_each(world, Surface);
    ecs_entity_t surface = 0;
    while (ecs_each_next(&it)) {
        surface = it.entities[0];
    }

    test_assert(surface != 0);
    ecs_set(world, surface, Surface, {.width = 2 * TILT_W, .height = TILT_H,
        .resolution_scale = 1, .write_to_file = "tilt_resize.png"});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/tilt_resize.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 2 * TILT_W);
    test_int(h, TILT_H);
    // The band keeps its height on the wider image: sharp at row 141, blurred at the top and
    // bottom, measured over the stripes in the middle of the image
    int32_t x, rows[3] = {141, 8, 247}, r;
    int contrast[3];
    for (r = 0; r < 3; r ++) {
        int lo = 255, hi = 0;
        for (x = TILT_W / 2 + 32; x < TILT_W / 2 + 224; x ++) {
            int v = px[(rows[r] * w + x) * 4];
            lo = glm_imin(lo, v);
            hi = glm_imax(hi, v);
        }

        contrast[r] = hi - lo;
    }

    test_assert(contrast[0] > 200);
    test_assert(contrast[1] < 64);
    test_assert(contrast[2] < 64);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
