/**
 * @file TextureBlend.c
 * @brief Tests for drawing shapes with a TextureBlend on the GPU backend.
 *
 * A blended quad on the left is compared with a plain quad on the right whose color is the
 * weighted mix of the layers in linear light, so the two render alike when the mask weighs the
 * layers as documented. A layer without PbrTextures contributes a white albedo.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** RGB PNG of 16 by 16 pixels of (128, 200, 50) */
#define MASK_RGB_PNG "test/assets/textures/mask_rgb.png"

/** RGB PNG of 16 by 16 pixels of (128, 0, 0) */
#define MASK_RED_PNG "test/assets/textures/mask_red.png"

/** Gray PNG of 16 by 16 pixels of 0 */
#define BLACK_PNG "test/assets/textures/cookie_black.png"

/** Create a headless world with a camera, a view whose pixels 16 and 48 of row 16 see x of
 * -3.46 and 3.46 and a surface of 64 by 32 pixels.
 *
 * @param file File the surface writes
 * @return The world
 */
static ecs_world_t* world_new(
    const char *file)
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
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = (char*)file,
        .resolution_scale = 1});

    return world;
}

/** Create a quad of a material without metal.
 *
 * @param world The world
 * @param x Its x
 * @return The quad
 */
static ecs_entity_t quad_new(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {x, 0, 0});
    ecs_set(world, result, Quad, {3, 3});
    ecs_set(world, result, Rgb, {255, 255, 255});
    ecs_set(world, result, PbrMaterial, {.metallic = 0, .roughness = 1});

    return result;
}

/** Render the world and compare the centers of the left and right
 * quads.
 *
 * @param world The world
 * @param file File the surface writes, that of world_new
 * @param stats Receives the stats of the backend
 */
static void render_compare(
    ecs_world_t *world,
    const char *file,
    box_render_gpu_stats_t *stats)
{
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, stats));
    test_int(stats->errors, 0);

    char *path = flecs_asprintf("%s/%s", tmp, file);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, 64);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[0] > 32);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("%s: channel %d left %d right %d\n", file, c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
}

/** Layer 0 alone, without a mask, renders as its textures do through PbrTextures. */
void TextureBlend_layer_zero_alone(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_alone.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, PbrTextures, {.albedo = tex});
    ecs_entity_t blended = quad_new(world, -3.46f);
    ecs_set(world, blended, TextureBlend, {.layers = {layer}});
    ecs_entity_t plain = quad_new(world, 3.46f);
    ecs_set(world, plain, PbrTextures, {.albedo = tex});

    box_render_gpu_stats_t stats;
    render_compare(world, "blend_alone.png", &stats);
    test_int(stats.blend_arrays, 3);

    ecs_fini(world);
}

/** A single channel mask of red 128 weighs layer 0 (red 128) by 1 - r and layer 1 (no
 * PbrTextures, white) by r: (0.498 * 0.216 + 0.502, 0.502, 0.502) in linear light, Rgb
 * (205, 188, 188). */
void TextureBlend_single_channel_mask(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_single.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexturePath, {.path = MASK_RED_PNG, .single_channel = true});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t layer0 = ecs_new(world);
    ecs_set(world, layer0, PbrTextures, {.albedo = red});
    ecs_entity_t layer1 = ecs_new(world);
    ecs_entity_t blended = quad_new(world, -3.46f);
    ecs_set(world, blended, TextureBlend, {.mask = mask, .layers = {layer0, layer1}});
    ecs_entity_t plain = quad_new(world, 3.46f);
    ecs_set(world, plain, Rgb, {205, 188, 188});

    box_render_gpu_stats_t stats;
    render_compare(world, "blend_single.png", &stats);

    ecs_fini(world);
}

/** An RGBA mask of (128, 200, 50, 255) weighs four layers by its channels over their sum: layer
 * 3 (red 128) by 0.403 and the white layers 0..2 by the rest, Rgb (216, 203, 203). The mask is
 * read as stored though the texture is not linear. */
void TextureBlend_rgba_mask(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_rgba.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t white = ecs_new(world);
    ecs_entity_t layer3 = ecs_new(world);
    ecs_set(world, layer3, PbrTextures, {.albedo = red});
    ecs_entity_t blended = quad_new(world, -3.46f);
    ecs_set(world, blended, TextureBlend, {.mask = mask,
        .layers = {white, white, white, layer3}});
    ecs_entity_t plain = quad_new(world, 3.46f);
    ecs_set(world, plain, Rgb, {216, 203, 203});

    box_render_gpu_stats_t stats;
    render_compare(world, "blend_rgba.png", &stats);

    ecs_fini(world);
}

/** Shapes whose blends have the same layers share one array per map, of the size of their
 * largest source with a full mip chain, and the arrays are released with the last shape. */
void TextureBlend_arrays_shared_released(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_shared.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t layer0 = ecs_new(world);
    ecs_set(world, layer0, PbrTextures, {.albedo = red});
    ecs_entity_t layer1 = ecs_new(world);
    ecs_entity_t a = quad_new(world, -3.46f);
    ecs_set(world, a, TextureBlend, {.layers = {layer0, layer1}});
    ecs_entity_t b = quad_new(world, 3.46f);
    ecs_set(world, b, TextureBlend, {.mask = red, .layers = {layer0, layer1}});
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    test_int(stats.blend_arrays, 3);
    test_int(stats.blend_builds, 3);

    // Albedo slices of 16x16 with 5 levels, ORM and normal slices of 1x1 without sources, 2
    // slices of 4 bytes per texel
    test_int(stats.blend_bytes, ((16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1) + 1 + 1) * 4 * 2);

    ecs_delete(world, a);
    ecs_delete(world, b);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    test_int(stats.blend_arrays, 0);
    test_int(stats.blend_bytes, 0);
    test_int(stats.blend_builds, 3);

    ecs_fini(world);
}

/** Changing the PbrTextures of a layer to another image builds the arrays of the new sources
 * and the shape shows it. */
void TextureBlend_layer_change_rebuilds(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_change.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t rgb = ecs_new(world);
    ecs_set(world, rgb, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, PbrTextures, {.albedo = red});
    ecs_entity_t blended = quad_new(world, -3.46f);
    ecs_set(world, blended, TextureBlend, {.layers = {layer}});
    ecs_entity_t plain = quad_new(world, 3.46f);
    ecs_set(world, plain, PbrTextures, {.albedo = rgb});
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    ecs_set(world, layer, PbrTextures, {.albedo = rgb});
    box_render_gpu_stats_t stats;
    render_compare(world, "blend_change.png", &stats);
    test_int(stats.blend_arrays, 3);
    test_int(stats.blend_builds, 4);

    ecs_fini(world);
}

/** With a contrast the heights shape the weights of a single channel mask of red 128: layer 0
 * (red 128) weighs 0.498 at a height of 0.502, layer 1 (white) 0.502 at a height of 0.
 *
 * Layer 0 is higher by 0.498, so at a contrast of 32 it shows alone, as the plain quad with its
 * albedo does. The heights make a fourth array. */
void TextureBlend_height_contrast(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_height.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexturePath, {.path = MASK_RED_PNG, .single_channel = true});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t black = ecs_new(world);
    ecs_set(world, black, TexturePath, {.path = BLACK_PNG});
    ecs_entity_t layer0 = ecs_new(world);
    ecs_set(world, layer0, PbrTextures, {.albedo = red});
    ecs_entity_t layer1 = ecs_new(world);
    ecs_entity_t blended = quad_new(world, -3.46f);
    ecs_set(world, blended, TextureBlend, {.mask = mask, .layers = {layer0, layer1},
        .heights = {red, black}, .contrast = 32});
    ecs_entity_t plain = quad_new(world, 3.46f);
    ecs_set(world, plain, PbrTextures, {.albedo = red});

    box_render_gpu_stats_t stats;
    render_compare(world, "blend_height.png", &stats);
    test_int(stats.blend_arrays, 4);

    ecs_fini(world);
}

/** Render the world and read the pixels 16 and 48 of row 16, the centers of the left and right
 * quads.
 *
 * @param world The world
 * @param file File the surface writes, that of world_new
 * @param stats Receives the stats of the backend
 * @param left Receives the RGB of the left quad
 * @param right Receives the RGB of the right quad
 */
static void render_read(
    ecs_world_t *world,
    const char *file,
    box_render_gpu_stats_t *stats,
    int32_t left[3],
    int32_t right[3])
{
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, stats));
    test_int(stats->errors, 0);

    char *path = flecs_asprintf("%s/%s", tmp, file);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, 64);
    for (c = 0; c < 3; c ++) {
        left[c] = img[(16 * 64 + 16) * 4 + c];
        right[c] = img[(16 * 64 + 48) * 4 + c];
    }

    stbi_image_free(img);
    ecs_os_free(path);
}

/** A contrast without heights keeps every layer flat at 0.5 and binds the one-slice placeholder
 * instead of a fourth array, but still sharpens the mask: the RGBA mask of (128, 200, 50, 255)
 * weighs layer 0 (red 128) by 0.39 and layer 1 (white) by 0.61, and at a contrast of 32 layer 0
 * falls under the drop share, so the left quad shows white where the linear blend on the right
 * mixes in the red. */
void TextureBlend_flat_heights_contrast(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_flat.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t layer0 = ecs_new(world);
    ecs_set(world, layer0, PbrTextures, {.albedo = red});
    ecs_entity_t layer1 = ecs_new(world);
    ecs_entity_t sharp = quad_new(world, -3.46f);
    ecs_set(world, sharp, TextureBlend, {.mask = mask, .layers = {layer0, layer1},
        .contrast = 32});
    ecs_entity_t linear = quad_new(world, 3.46f);
    ecs_set(world, linear, TextureBlend, {.mask = mask, .layers = {layer0, layer1}});

    box_render_gpu_stats_t stats;
    int32_t left[3], right[3];
    render_read(world, "blend_flat.png", &stats, left, right);
    test_int(stats.blend_arrays, 3);

    // Red 128 is about the same in both, green and blue are lower where the red mixes in
    printf("blend_flat.png: left (%d, %d, %d) right (%d, %d, %d)\n",
        left[0], left[1], left[2], right[0], right[1], right[2]);
    test_assert(left[0] > 32);
    test_assert(left[1] > right[1] + 16);
    test_assert(left[2] > right[2] + 16);

    ecs_fini(world);
}

/** A blend with a contrast switches from the placeholder to a heights array when a layer gains
 * a height image, and back when it loses it again. */
void TextureBlend_flat_heights_switch(void) {
    // materials/texture_blend
    ecs_world_t *world = world_new("blend_flat_switch.png");
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t layer0 = ecs_new(world);
    ecs_set(world, layer0, PbrTextures, {.albedo = red});
    ecs_entity_t layer1 = ecs_new(world);
    ecs_entity_t blended = quad_new(world, -3.46f);
    ecs_set(world, blended, TextureBlend, {.mask = mask, .layers = {layer0, layer1},
        .contrast = 32});
    quad_new(world, 3.46f);

    box_render_gpu_stats_t stats;
    int32_t left[3], right[3];
    render_read(world, "blend_flat_switch.png", &stats, left, right);
    test_int(stats.blend_arrays, 3);

    ecs_set(world, blended, TextureBlend, {.mask = mask, .layers = {layer0, layer1},
        .heights = {red}, .contrast = 32});
    render_read(world, "blend_flat_switch.png", &stats, left, right);
    test_int(stats.blend_arrays, 4);

    ecs_set(world, blended, TextureBlend, {.mask = mask, .layers = {layer0, layer1},
        .contrast = 32});
    render_read(world, "blend_flat_switch.png", &stats, left, right);
    test_int(stats.blend_arrays, 3);

    ecs_fini(world);
}
