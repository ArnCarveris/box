/**
 * @file TextureLayers.c
 * @brief Tests for drawing shapes with TextureLayers on the GPU backend.
 *
 * A quad with layers on the left is compared with a plain quad on the right whose material is
 * what the layers should make of the left one, so the two render alike when the layers change
 * the surface as documented. The layers cover the quads fully: no mask, facing 0, amount 1.
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

/** Create a headless world with a camera, a view whose pixels 16 and 48 of row 16 see x of
 * -3.46 and 3.46 and a surface of 64 by 32 pixels.
 *
 * @param file File the surface writes
 * @param layers Whether the scene has Scene.texture_layers
 * @return The world
 */
static ecs_world_t* world_new(
    const char *file,
    bool layers)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Scene, {.texture_blend = true, .texture_layers = layers});

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

/** Create a quad of a color and a material.
 *
 * @param world The world
 * @param x Its x
 * @param color Its color
 * @param metallic Its metallic
 * @param roughness Its roughness
 * @return The quad
 */
static ecs_entity_t quad_new(
    ecs_world_t *world,
    float x,
    Rgb color,
    float metallic,
    float roughness)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {x, 0, 0});
    ecs_set(world, result, Quad, {3, 3});
    ecs_set_ptr(world, result, Rgb, &color);
    ecs_set(world, result, PbrMaterial, {.metallic = metallic, .roughness = roughness});

    return result;
}

/** Create a layer that covers everything it is applied to.
 *
 * @param world The world
 * @param albedo How it changes the albedo
 * @param roughness How it changes the roughness
 * @return The layer
 */
static ecs_entity_t layer_new(
    ecs_world_t *world,
    TextureLayerBlend albedo,
    TextureLayerBlend roughness)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TextureLayer, {.amount = 1, .direction = {0, 1, 0}, .contrast = 8,
        .albedo = albedo, .roughness = roughness, .metallic = LayerKeep,
        .occlusion = LayerKeep});

    return result;
}

/** Render the world and load the image of its surface.
 *
 * @param world The world
 * @param file File the surface writes, that of world_new
 * @param stats Receives the stats of the backend
 * @return RGBA pixels of 64 by 32, freed with stbi_image_free
 */
static unsigned char* render(
    ecs_world_t *world,
    const char *file,
    box_render_gpu_stats_t *stats)
{
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    int32_t i;
    for (i = 0; i < 4; i ++) {
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
    test_int(h, 32);
    ecs_os_free(path);

    return img;
}

/** Render the world and compare the centers of the left and right quads.
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
    unsigned char *img = render(world, file, stats);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    int32_t c;
    test_assert(left[0] > 16 || left[1] > 16 || left[2] > 16);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("%s: channel %d left %d right %d\n", file, c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
}

/** A layer that mixes its color and roughness in renders as a quad of that color and
 * roughness; the layered pipeline and bind group are valid */
void TextureLayers_mix_constants(void) {
    // materials/texture_layers
    ecs_world_t *world = world_new("layers_mix.png", true);
    ecs_entity_t layer = layer_new(world, LayerMix, LayerMix);
    ecs_set(world, layer, Rgb, {200, 40, 30});
    ecs_set(world, layer, PbrMaterial, {.roughness = 0.6f});
    ecs_entity_t covered = quad_new(world, -3.46f, (Rgb){40, 90, 220}, 0, 1);
    ecs_set(world, covered, TextureLayers, {.layers = {layer}});
    quad_new(world, 3.46f, (Rgb){200, 40, 30}, 0, 0.6f);

    box_render_gpu_stats_t stats;
    render_compare(world, "layers_mix.png", &stats);

    ecs_fini(world);
}

/** A layer at amount 0 leaves the surface as it is */
void TextureLayers_amount_zero(void) {
    // materials/texture_layers
    ecs_world_t *world = world_new("layers_zero.png", true);
    ecs_entity_t layer = layer_new(world, LayerMix, LayerMix);
    ecs_set(world, layer, Rgb, {200, 40, 30});
    ecs_get_mut(world, layer, TextureLayer)->amount = 0;
    ecs_modified(world, layer, TextureLayer);
    ecs_entity_t covered = quad_new(world, -3.46f, (Rgb){40, 90, 220}, 0, 1);
    ecs_set(world, covered, TextureLayers, {.layers = {layer}});
    quad_new(world, 3.46f, (Rgb){40, 90, 220}, 0, 1);

    box_render_gpu_stats_t stats;
    render_compare(world, "layers_zero.png", &stats);

    ecs_fini(world);
}

/** The albedo map of a layer is decoded from sRGB as the albedo map of a material is */
void TextureLayers_albedo_map(void) {
    // materials/texture_layers
    ecs_world_t *world = world_new("layers_albedo.png", true);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t layer = layer_new(world, LayerMix, LayerKeep);
    ecs_set(world, layer, PbrTextures, {.albedo = tex});
    ecs_entity_t covered = quad_new(world, -3.46f, (Rgb){40, 90, 220}, 0, 1);
    ecs_set(world, covered, TextureLayers, {.layers = {layer}});
    ecs_entity_t plain = quad_new(world, 3.46f, (Rgb){255, 255, 255}, 0, 1);
    ecs_set(world, plain, PbrTextures, {.albedo = tex});

    box_render_gpu_stats_t stats;
    render_compare(world, "layers_albedo.png", &stats);

    ecs_fini(world);
}

/** With porosity 1 a wet layer does not change a metal, and multiplies the albedo of a rough
 * dielectric by its color */
void TextureLayers_porosity(void) {
    // materials/texture_layers
    ecs_world_t *world = world_new("layers_metal.png", true);
    ecs_entity_t wet = layer_new(world, LayerMultiply, LayerKeep);
    ecs_set(world, wet, Rgb, {128, 128, 128});
    ecs_get_mut(world, wet, TextureLayer)->porosity = 1;
    ecs_modified(world, wet, TextureLayer);
    ecs_entity_t metal = quad_new(world, -3.46f, (Rgb){220, 190, 140}, 1, 0.5f);
    ecs_set(world, metal, TextureLayers, {.layers = {wet}});
    quad_new(world, 3.46f, (Rgb){220, 190, 140}, 1, 0.5f);

    box_render_gpu_stats_t stats;
    render_compare(world, "layers_metal.png", &stats);
    ecs_fini(world);

    world = world_new("layers_rough.png", true);
    wet = layer_new(world, LayerMultiply, LayerKeep);
    ecs_set(world, wet, Rgb, {128, 128, 128});
    ecs_get_mut(world, wet, TextureLayer)->porosity = 1;
    ecs_modified(world, wet, TextureLayer);
    ecs_entity_t rough = quad_new(world, -3.46f, (Rgb){200, 200, 200}, 0, 1);
    ecs_set(world, rough, TextureLayers, {.layers = {wet}});
    quad_new(world, 3.46f, (Rgb){100, 100, 100}, 0, 1);

    render_compare(world, "layers_rough.png", &stats);
    ecs_fini(world);
}

/** A layer over a TextureBlend draws with the blend variant of the layers group, whose layer
 * maps follow the heights of the blend */
void TextureLayers_texture_blend(void) {
    // materials/texture_layers
    ecs_world_t *world = world_new("layers_blend.png", true);
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG});
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = MASK_RGB_PNG});
    ecs_entity_t base = ecs_new(world);
    ecs_set(world, base, PbrTextures, {.albedo = red});
    ecs_entity_t layer = layer_new(world, LayerMix, LayerKeep);
    ecs_set(world, layer, PbrTextures, {.albedo = tex});
    ecs_entity_t blended = quad_new(world, -3.46f, (Rgb){255, 255, 255}, 0, 1);
    ecs_set(world, blended, TextureBlend, {.layers = {base}});
    ecs_set(world, blended, TextureLayers, {.layers = {layer}});
    ecs_entity_t plain = quad_new(world, 3.46f, (Rgb){255, 255, 255}, 0, 1);
    ecs_set(world, plain, PbrTextures, {.albedo = tex});

    box_render_gpu_stats_t stats;
    render_compare(world, "layers_blend.png", &stats);

    ecs_fini(world);
}

/** Writing the amount of a layer changes the next frame without a new batch or pipeline */
void TextureLayers_amount_animates(void) {
    // materials/texture_layers
    ecs_world_t *world = world_new("layers_animate.png", true);
    ecs_entity_t layer = layer_new(world, LayerMix, LayerKeep);
    ecs_set(world, layer, Rgb, {200, 40, 30});
    ecs_entity_t covered = quad_new(world, -3.46f, (Rgb){40, 90, 220}, 0, 1);
    ecs_set(world, covered, TextureLayers, {.layers = {layer}});
    quad_new(world, 3.46f, (Rgb){200, 40, 30}, 0, 1);

    box_render_gpu_stats_t stats;
    render_compare(world, "layers_animate.png", &stats);
    int32_t pipelines = stats.pipelines;

    ecs_get_mut(world, layer, TextureLayer)->amount = 0;
    ecs_modified(world, layer, TextureLayer);
    unsigned char *img = render(world, "layers_animate.png", &stats);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    test_assert(left[2] > left[0]);
    test_int(stats.pipelines, pipelines);
    stbi_image_free(img);

    ecs_fini(world);
}

/** The layers module of the mesh shader exists only in a scene with Scene.texture_layers, and a
 * shape without layers renders the same with the flag on as off */
void TextureLayers_flag_off(void) {
    // materials/texture_layers
    box_render_gpu_stats_t off, on;
    unsigned char *a, *b;
    ecs_world_t *world = world_new("layers_off.png", false);
    ecs_entity_t layer = layer_new(world, LayerMix, LayerMix);
    ecs_set(world, layer, Rgb, {200, 40, 30});
    ecs_entity_t covered = quad_new(world, -3.46f, (Rgb){40, 90, 220}, 0, 1);
    ecs_set(world, covered, TextureLayers, {.layers = {layer}});
    quad_new(world, 3.46f, (Rgb){40, 90, 220}, 0, 1);
    render_compare(world, "layers_off.png", &off);
    a = render(world, "layers_off.png", &off);
    ecs_fini(world);

    world = world_new("layers_on.png", true);
    layer = layer_new(world, LayerMix, LayerMix);
    ecs_set(world, layer, Rgb, {200, 40, 30});
    covered = quad_new(world, 3.46f, (Rgb){40, 90, 220}, 0, 1);
    ecs_set(world, covered, TextureLayers, {.layers = {layer}});
    quad_new(world, -3.46f, (Rgb){40, 90, 220}, 0, 1);
    b = render(world, "layers_on.png", &on);
    ecs_fini(world);

    test_int(on.shaders, off.shaders + 1);
    int32_t x, y, c;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            for (c = 0; c < 4; c ++) {
                test_int(a[(y * 64 + x) * 4 + c], b[(y * 64 + x) * 4 + c]);
            }
        }
    }

    unsigned char *right = &b[(16 * 64 + 48) * 4];
    test_assert(right[0] > right[2]);
    stbi_image_free(a);
    stbi_image_free(b);
}
