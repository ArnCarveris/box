/**
 * @file Effects.c
 * @brief Tests for the effect chain of a view: bloom, tonemap, exposure, fog and SMAA, and for
 * the ambient occlusion (Gtao) of the indirect light.
 *
 * Most cases render an unlit view (a C literal View) from 5 units in front of the origin into
 * a 64x64 capture; a unit cube at the origin covers columns 26 to 38 of the middle row.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

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
        .lighting.enabled = lit};
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
