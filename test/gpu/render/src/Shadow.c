/**
 * @file Shadow.c
 * @brief Tests for cascaded shadow maps: cascade splits, cascade fitting and casters.
 *
 * The capture cases look straight down from 10 units above a white floor lit by a sun that
 * shines straight down, into a 64x64 image: world x = -3 lands on pixel column 15, x = 3 on
 * column 48. A small slab 5 units above x = -3 shadows the floor below it.
 *
 * The alpha tested caster is a 3x3 quad 5 units above x = -3 that faces the floor, so the camera
 * culls it: world x = -3.75 below its left half lands on column 11, x = -2.25 below its right
 * half on column 20.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Create a headless world without ambient light with a white rough floor on y = 0 and a sun.
 *
 * @param pitch Rotation.x of the sun, its elevation: pi / 2 shines straight down
 * @return The world
 */
static ecs_world_t* shadow_world(
    float pitch)
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
        .shader_features.Extended.diffuse_transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set_name(world, floor, "floor");
    ecs_set(world, floor, Position, {0, -0.1f, -20});
    ecs_set(world, floor, Box, {40, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {pitch, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    return world;
}

/** Create a view with the View defaults and shadows enabled, or with shadow settings.
 *
 * @param world The world
 * @param camera The camera
 * @param shadows The shadow settings, NULL for the defaults
 * @return The view
 */
static ecs_entity_t shadow_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    const ShadowSettings *shadows)
{
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    if (shadows) {
        v->shadows = *shadows;
    }

    ecs_modified(world, view, View);

    return view;
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world, without a Surface
 * @param file Image file in the temporary folder
 * @param size Width and height of the image
 * @return RGBA pixels, freed with stbi_image_free
 */
static unsigned char* shadow_capture(
    ecs_world_t *world,
    const char *file,
    int32_t size)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = size, .height = size,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, size);
    test_int(h, size);
    ecs_os_free(path);

    return px;
}

/** Edge in pixels of the images of the filter cases */
#define SHADOW_EDGE_SIZE (128)

/** Render the edge of a slab 6 units above a floor lit straight down, seen from 3 units above
 * the floor, so the slab (x < 0) is behind the camera and its shadow edge runs down the middle
 * column. The single cascade has 64 texels, about 4 pixels each.
 *
 * @param filter_size The filter size of the view
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a SHADOW_EDGE_SIZE square image, freed with stbi_image_free
 */
static unsigned char* shadow_edge_render(
    int32_t filter_size,
    const char *file)
{
    ecs_world_t *world = shadow_world((float)GLM_PI_2);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 3, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    shadow_view(world, camera, &(ShadowSettings){.enabled = true, .cascades = 1,
        .resolution = 64, .max_distance = 4, .depth_bias = 2, .normal_offset = 2,
        .filter_size = filter_size});
    ecs_entity_t slab = ecs_new(world);
    ecs_set(world, slab, Position, {-10, 6, 0});
    ecs_set(world, slab, Box, {20, 0.2f, 20});
    unsigned char *px = shadow_capture(world, file, SHADOW_EDGE_SIZE);
    ecs_fini(world);

    return px;
}

/** Check that the middle row of an edge image rises monotonically from the shadow to the lit
 * floor and get the width of its ramp, the pixels between 10% and 90% of the rise.
 *
 * @param px Pixels of a shadow_edge_render image
 * @return The width of the ramp in pixels
 */
static int32_t shadow_ramp_width(
    const unsigned char *px)
{
    const unsigned char *row = &px[(SHADOW_EDGE_SIZE / 2) * SHADOW_EDGE_SIZE * 4];
    int32_t lo = row[4 * 4], hi = row[(SHADOW_EDGE_SIZE - 5) * 4];
    int32_t x, width = 0;
    test_assert(lo < 30);
    test_assert(hi > 150);
    for (x = 1; x < SHADOW_EDGE_SIZE; x ++) {
        test_assert((int32_t)row[x * 4] >= (int32_t)row[(x - 1) * 4] - 2);
    }

    for (x = 0; x < SHADOW_EDGE_SIZE; x ++) {
        int32_t v = row[x * 4];
        if (v * 10 > lo * 9 + hi && v * 10 < lo + hi * 9) {
            width ++;
        }
    }

    return width;
}

/** Edge in pixels of the image of the cascade blend case */
#define SHADOW_BLEND_SIZE (256)

/** Render a long floor lit straight down at a shallow angle with a row of thin bars 6 units
 * above it, out of the image, whose shadows run away from the camera across the seam of
 * cascades 0 and 1, and measure for every row how sharp the shadows are.
 *
 * The measure is the darkness below the brightest pixel of the row, squared and summed over
 * the row, over its sum. Filtering keeps the sum and spreads it, so the measure drops where
 * the texels grow, and it does not depend on where the pixels sample the stripes.
 *
 * @param blend The blend band of the view
 * @param file Image file in the temporary folder
 * @param out Receives the measure of every row
 */
static void shadow_blend_render(
    float blend,
    const char *file,
    float *out)
{
    ecs_world_t *world = shadow_world((float)GLM_PI_2);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 1.5f, 2});
    ecs_set(world, camera, Rotation, {-0.3f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    shadow_view(world, camera, &(ShadowSettings){.enabled = true, .cascades = 4,
        .resolution = 128, .max_distance = 30, .split_lambda = 0.7f, .depth_bias = 2,
        .normal_offset = 2, .filter_size = 3, .fade = 0.2f, .blend = blend});
    int32_t i, x, y;
    for (i = 0; i < 9; i ++) {
        ecs_entity_t bar = ecs_new(world);
        ecs_set(world, bar, Position, {-1.4f + 0.35f * (float)i, 6, -4});
        ecs_set(world, bar, Box, {0.06f, 0.05f, 16});
    }

    unsigned char *px = shadow_capture(world, file, SHADOW_BLEND_SIZE);
    for (y = 0; y < SHADOW_BLEND_SIZE; y ++) {
        const unsigned char *row = &px[y * SHADOW_BLEND_SIZE * 4];
        float lit = 0, sum = 0, square = 0;
        for (x = 40; x < SHADOW_BLEND_SIZE - 40; x ++) {
            lit = glm_max(lit, (float)row[x * 4]);
        }

        for (x = 40; x < SHADOW_BLEND_SIZE - 40; x ++) {
            float d = lit - (float)row[x * 4];
            sum += d;
            square += d * d;
        }

        out[y] = square / glm_max(sum, 1.0f);
    }

    stbi_image_free(px);
    ecs_fini(world);
}

/** Get the largest change of the sharpness measure between neighbouring rows of the floor in
 * front of z = -5, where the stripes are wide enough to measure.
 *
 * @param m The measure of every row
 * @return The largest change
 */
static float shadow_blend_jump(
    const float *m)
{
    float jump = 0;
    int32_t y;
    for (y = 110; y < SHADOW_BLEND_SIZE - 2; y ++) {
        jump = glm_max(jump, fabsf(m[y + 1] - m[y]));
    }

    return jump;
}

/** Write a strong normal map: bumps tilted up to about 50 degrees in both axes.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t shadow_normal_map(
    ecs_world_t *world)
{
    char *file = flecs_asprintf("%s/bumps.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[32 * 32 * 4];
    int32_t x, y;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            vec3 n = {
                0.8f * sinf((float)x * GLM_PIf / 8.0f),
                0.8f * cosf((float)y * GLM_PIf / 8.0f),
                1.0f
            };
            glm_vec3_normalize(n);
            uint8_t *p = &px[(y * 32 + x) * 4];
            p[0] = (uint8_t)roundf((n[0] * 0.5f + 0.5f) * 255.0f);
            p[1] = (uint8_t)roundf((n[1] * 0.5f + 0.5f) * 255.0f);
            p[2] = (uint8_t)roundf((n[2] * 0.5f + 0.5f) * 255.0f);
            p[3] = 255;
        }
    }

    test_assert(stbi_write_png(file, 32, 32, 4, px, 32 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_os_free(file);

    return tex;
}

void Shadow_splits_uniform(void) {
    // lighting/shadows
    float splits[4];
    box_render_shadow_splits(1, 101, 0, 4, splits);
    test_flt(splits[0], 26);
    test_flt(splits[1], 51);
    test_flt(splits[2], 76);
    test_flt(splits[3], 101);
}

void Shadow_splits_logarithmic(void) {
    // lighting/shadows
    float splits[3];
    box_render_shadow_splits(1, 1000, 1, 3, splits);
    test_assert(fabsf(splits[0] - 10) < 1e-3f);
    test_assert(fabsf(splits[1] - 100) < 1e-2f);
    test_flt(splits[2], 1000);
}

void Shadow_splits_mix_by_lambda(void) {
    // lighting/shadows
    float splits[2];
    box_render_shadow_splits(1, 100, 0.5f, 2, splits);
    test_assert(fabsf(splits[0] - (0.5f * 10 + 0.5f * 50.5f)) < 1e-3f);
    test_flt(splits[1], 100);

    box_render_shadow_splits(1, 100, 0.7f, 2, splits);
    test_assert(fabsf(splits[0] - (0.7f * 10 + 0.3f * 50.5f)) < 1e-3f);
}

void Shadow_splits_end_at_max_distance(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.5f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 3, .max_distance = 50, .split_lambda = 0.5f,
            .resolution = 2048, .filter_size = 3}});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    float expect[3];
    box_render_shadow_splits(0.5f, 50, 0.5f, 3, expect);
    test_bool(v->shadows.enabled, true);
    test_int(v->shadows.cascades, 3);
    test_flt(v->shadows.near_, 0.5f);
    test_flt(v->shadows.splits[0], expect[0]);
    test_flt(v->shadows.splits[1], expect[1]);
    test_flt(v->shadows.splits[2], 50);
    test_assert(v->shadows.splits[0] > 0.5f);
    test_assert(v->shadows.splits[1] > v->shadows.splits[0]);
    test_assert(v->shadows.splits[2] > v->shadows.splits[1]);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shadow_splits_end_at_camera_far(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .far_ = 30, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 2, .max_distance = 200, .resolution = 2048,
            .filter_size = 3}});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_bool(v->shadows.enabled, true);
    test_int(v->shadows.cascades, 2);
    test_flt(v->shadows.splits[1], 30);

    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_progress(world, 0);
    box_render_sync(world);
    v = ecs_vec_first(&null->last.views);
    test_bool(v->shadows.enabled, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shadow_cascade_contains_slice(void) {
    // lighting/shadows
    Camera c = {.fov = 60, .near_ = 0.1f};
    mat4 camera, proj;
    float dir[3] = {0.3f, 0.8f, 0.5f};
    glm_vec3_normalize(dir);
    glm_mat4_identity(camera);
    glm_translate(camera, (vec3){5, 2, 3});
    glm_rotate_y(camera, 0.7f, camera);
    glm_rotate_x(camera, -0.3f, camera);
    box_camera_projection(&c, 160, 90, proj);
    box_render_cascade_t cascade;
    box_render_shadow_cascade(camera, proj, 5, 20, dir, 1024, 50, &cascade);
    test_assert(cascade.texel > 0);

    int32_t i;
    for (i = 0; i < 8; i ++) {
        float depth = i < 4 ? 5.0f : 20.0f;
        vec4 local = {
            (i & 1 ? 1.0f : -1.0f) * depth / proj[0][0],
            (i & 2 ? 1.0f : -1.0f) * depth / proj[1][1],
            -depth, 1
        };
        vec4 world, clip;
        glm_mat4_mulv(camera, local, world);
        glm_mat4_mulv(cascade.matrix, world, clip);
        test_assert(clip[0] > -1 && clip[0] < 1);
        test_assert(clip[1] > -1 && clip[1] < 1);
        test_assert(clip[2] > 0 && clip[2] < 1);
    }
}

void Shadow_cascade_snapped_to_texels(void) {
    // lighting/shadows
    Camera c = {.fov = 60, .near_ = 0.1f};
    mat4 a, b, proj;
    float dir[3] = {-0.4f, 0.7f, 0.2f};
    int32_t res = 2048;
    glm_vec3_normalize(dir);
    box_camera_projection(&c, 160, 90, proj);
    glm_mat4_identity(a);
    glm_translate(a, (vec3){1, 2, 3});
    glm_mat4_identity(b);
    glm_translate(b, (vec3){1.37f, 2.11f, 2.47f});
    glm_rotate_y(b, 0.25f, b);
    box_render_cascade_t ca, cb;
    box_render_shadow_cascade(a, proj, 1, 10, dir, res, 50, &ca);
    box_render_shadow_cascade(b, proj, 1, 10, dir, res, 50, &cb);
    test_flt(ca.texel, cb.texel);

    vec4 p = {2, 0, -3, 1}, pa, pb;
    glm_mat4_mulv(ca.matrix, p, pa);
    glm_mat4_mulv(cb.matrix, p, pb);
    test_assert(pa[0] != pb[0] || pa[1] != pb[1]);
    float dx = (pa[0] - pb[0]) * 0.5f * (float)res;
    float dy = (pa[1] - pb[1]) * 0.5f * (float)res;
    test_assert(fabsf(dx - roundf(dx)) < 0.02f);
    test_assert(fabsf(dy - roundf(dy)) < 0.02f);
}

void Shadow_cascade_reversed_z(void) {
    // lighting/shadows
    Camera c = {.fov = 60, .near_ = 0.1f};
    mat4 camera, proj;
    float dir[3] = {0, 1, 0};
    glm_mat4_identity(camera);
    box_camera_projection(&c, 100, 100, proj);
    box_render_cascade_t cascade;
    box_render_shadow_cascade(camera, proj, 1, 10, dir, 512, 50, &cascade);

    vec4 low = {0, 0, -5, 1}, high = {0, 2, -5, 1}, cl, ch;
    glm_mat4_mulv(cascade.matrix, low, cl);
    glm_mat4_mulv(cascade.matrix, high, ch);
    test_assert(ch[2] > cl[2]);
    test_assert(cl[2] > 0);
    test_assert(ch[2] < 1);
}

void Shadow_box_casts_shadow(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "cast.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/cast.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] < 30);
    test_assert(under[1] < 30);
    test_assert(under[2] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_disabled_casts_no_shadow(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = false, .depth_bias = 2}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "disabled.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/disabled.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_no_shadow_casts_none(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_add(world, caster, NoShadow);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "ghost.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/ghost.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_no_shadow_parent_casts_none(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t group = ecs_new(world);
    ecs_add(world, group, NoShadow);
    ecs_add_pair(world, caster, EcsChildOf, group);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "group.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/group.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_no_shadow_removed_casts_again(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_add(world, caster, NoShadow);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "removed.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/removed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);
    stbi_image_free(px);

    ecs_remove(world, caster, NoShadow);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    under = &px[(32 * 64 + 15) * 4];
    open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] < 30);
    test_assert(under[1] < 30);
    test_assert(under[2] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_beyond_max_distance_unshadowed(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .max_distance = 5, .cascades = 4, .resolution = 2048, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "beyond.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/beyond.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_fade_softens_last_cascade(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 1, .max_distance = 20, .fade = 1,
            .depth_bias = 2, .normal_offset = 1.5f, .resolution = 2048, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "fade.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] > 60);
    test_assert(under[0] < (int)open[0] - 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_default_settings(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.view\n"
        "view { View: {} }\n"
        "shadowed { View: {shadows: {enabled: true}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->shadows.enabled, false);
    test_int(view->shadows.cascades, 4);
    test_int(view->shadows.resolution, 2048);
    test_flt(view->shadows.max_distance, 200);
    test_flt(view->shadows.split_lambda, 0.7f);
    test_flt(view->shadows.depth_bias, 2);
    test_flt(view->shadows.normal_offset, 2);
    test_int(view->shadows.filter_size, 3);
    test_flt(view->shadows.fade, 0.2f);
    test_flt(view->shadows.blend, 0.1f);
    const View *shadowed = ecs_get(world, ecs_lookup(world, "shadowed"), View);
    test_assert(shadowed != NULL);
    test_bool(shadowed->shadows.enabled, true);
    test_int(shadowed->shadows.cascades, 4);
    test_flt(shadowed->shadows.split_lambda, 0.7f);

    ecs_fini(world);
}

/** Render the floor of the capture cases under an alpha tested quad facing it.
 *
 * @param file Name of the PNG the surface writes
 * @param holes Whether the albedo texture of the quad has no alpha in its left half, else the
 * quad has no texture
 * @return The pixels, 64x64 RGBA, freed with stbi_image_free
 */
static unsigned char* cutout_shadow_render(
    const char *file,
    bool holes)
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
        .shader_features.Extended.diffuse_transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/shadow_halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = (i % 8) < 4 ? 0 : 255;
    }

    test_assert(stbi_write_png(png, 8, 8, 4, px, 8 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = png});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, caster, Quad, {3, 3});
    ecs_set(world, caster, AlphaCutoff, {0.5f});
    if (holes) {
        ecs_set(world, caster, PbrTextures, {.albedo = tex});
    }

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    int32_t frames = 0;
    while (frames < 100 && ecs_get(world, tex, TextureStatus)->state == TextureLoading) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", tmp, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, 64);
    test_int(h, 64);

    ecs_os_free(path);
    ecs_fini(world);
    ecs_os_free(png);

    return result;
}

void Shadow_cutout_shadow_has_holes(void) {
    // lighting/shadows
    unsigned char *px = cutout_shadow_render("cutout_shadow.png", true);
    unsigned char *hole = &px[(32 * 64 + 11) * 4];
    unsigned char *solid = &px[(32 * 64 + 20) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)hole[0] - (int)open[0]) < 30);
    test_assert(solid[0] < 30);
    test_assert(solid[1] < 30);
    test_assert(solid[2] < 30);

    stbi_image_free(px);
}

void Shadow_cutout_without_texture_casts_full_shadow(void) {
    // lighting/shadows
    unsigned char *px = cutout_shadow_render("cutout_full_shadow.png", false);
    unsigned char *left = &px[(32 * 64 + 11) * 4];
    unsigned char *right = &px[(32 * 64 + 20) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(left[0] < 30);
    test_assert(right[0] < 30);

    stbi_image_free(px);
}

void Shadow_tent_filter_ramps_smoothly(void) {
    // lighting/shadows
    unsigned char *px1 = shadow_edge_render(1, "tent1.png");
    unsigned char *px3 = shadow_edge_render(3, "tent3.png");
    unsigned char *px7 = shadow_edge_render(7, "tent7.png");
    int32_t w1 = shadow_ramp_width(px1);
    int32_t w3 = shadow_ramp_width(px3);
    int32_t w7 = shadow_ramp_width(px7);
    test_assert(w1 >= 1);
    test_assert(w3 > w1);
    test_assert(w7 > w3);

    stbi_image_free(px1);
    stbi_image_free(px3);
    stbi_image_free(px7);
}

void Shadow_even_filter_size_rounds_up(void) {
    // lighting/shadows
    ShadowSettings in = {.filter_size = 4}, out;
    box_render_shadow_settings(&in, &out);
    test_int(out.filter_size, 5);
    in.filter_size = 2;
    box_render_shadow_settings(&in, &out);
    test_int(out.filter_size, 3);
    in.filter_size = 8;
    box_render_shadow_settings(&in, &out);
    test_int(out.filter_size, 7);

    unsigned char *even = shadow_edge_render(4, "even4.png");
    unsigned char *odd = shadow_edge_render(5, "odd5.png");
    int32_t i;
    for (i = 0; i < SHADOW_EDGE_SIZE * SHADOW_EDGE_SIZE * 4; i ++) {
        test_assert(abs((int)even[i] - (int)odd[i]) <= 1);
    }

    stbi_image_free(even);
    stbi_image_free(odd);
}

void Shadow_cascade_blend_hides_seam(void) {
    // lighting/shadows
    float hard[SHADOW_BLEND_SIZE], soft[SHADOW_BLEND_SIZE];
    shadow_blend_render(0, "seam_hard.png", hard);
    shadow_blend_render(0.1f, "seam_soft.png", soft);
    float jump_hard = shadow_blend_jump(hard);
    float jump_soft = shadow_blend_jump(soft);
    test_assert(jump_hard > 8);
    test_assert(jump_soft < 0.6f * jump_hard);
}

void Shadow_grazing_light_no_acne(void) {
    // lighting/shadows
    ecs_world_t *world = shadow_world(glm_rad(10));
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, -20});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = shadow_view(world, camera, NULL);
    unsigned char *flat = shadow_capture(world, "grazing.png", 64);
    int32_t i, count = 64 * 64;
    float mean = 0;
    for (i = 0; i < count; i ++) {
        mean += (float)flat[i * 4];
    }

    mean /= (float)count;
    test_assert(mean > 40);
    for (i = 0; i < count; i ++) {
        test_assert((float)flat[i * 4] >= 0.9f * mean);
    }

    stbi_image_free(flat);

    ecs_entity_t floor = ecs_lookup(world, "floor");
    ecs_set(world, floor, PbrTextures, {.normal = shadow_normal_map(world)});
    ecs_set(world, floor, TextureTransform, {.scale_x = 8, .scale_y = 8});
    unsigned char *bumpy = shadow_capture(world, "grazing_bumpy.png", 64);
    View *v = ecs_get_mut(world, view, View);
    v->shadows.enabled = false;
    ecs_modified(world, view, View);
    unsigned char *unshadowed = shadow_capture(world, "grazing_unshadowed.png", 64);
    for (i = 0; i < count; i ++) {
        int32_t ref = unshadowed[i * 4], value = bumpy[i * 4];
        if (ref > 24) {
            test_assert((float)value >= 0.9f * (float)ref);
        } else {
            test_assert(abs(value - ref) <= 3);
        }
    }

    stbi_image_free(bumpy);
    stbi_image_free(unshadowed);
    ecs_fini(world);
}

void Shadow_contact_shadow_close(void) {
    // lighting/shadows
    ecs_world_t *world = shadow_world(glm_rad(45));
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 4, .near_ = 0.1f,
        .far_ = 20});
    shadow_view(world, camera, NULL);
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {0, 0.5f, 0});
    ecs_set(world, box, Box, {2, 1, 2});
    ecs_set(world, box, Rgb, {255, 0, 0});
    ecs_set(world, box, PbrMaterial, {.roughness = 1});
    unsigned char *px = shadow_capture(world, "contact.png", 64);
    int32_t y, first = -1, last = -1;
    for (y = 0; y < 64; y ++) {
        const unsigned char *p = &px[(y * 64 + 32) * 4];
        if (p[0] > 2 * (int32_t)p[1] + 20) {
            first = first < 0 ? y : first;
            last = y;
        }
    }

    test_assert(first > 8);
    test_assert(last < 55);
    const unsigned char *before = &px[((first - 8) * 64 + 32) * 4];
    const unsigned char *after = &px[((last + 8) * 64 + 32) * 4];
    int32_t lit = before[0] > after[0] ? before[0] : after[0];
    int32_t dir = after[0] < before[0] ? 1 : -1;
    int32_t edge = dir > 0 ? last : first;
    test_assert(lit > 60);
    test_assert(abs((int)before[0] - (int)after[0]) > lit / 2);
    test_assert(px[((edge + 2 * dir) * 64 + 32) * 4] < lit / 2);
    test_assert(px[((edge + 4 * dir) * 64 + 32) * 4] < lit / 3);

    stbi_image_free(px);
    ecs_fini(world);
}

void Shadow_min_caster_texels_culls_small_caster(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .culling.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3,
            .min_caster_texels = 1000}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64,
        .write_to_file = "min_texels_small.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/min_texels_small.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_min_caster_texels_keeps_large_caster(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .culling.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3,
            .min_caster_texels = 4}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {-3, 5, 0});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64,
        .write_to_file = "min_texels_large.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/min_texels_large.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] < 30);
    test_assert(under[1] < 30);
    test_assert(under[2] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
