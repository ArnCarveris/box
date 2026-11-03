/**
 * @file TextureMapping.c
 * @brief Benchmarks for the fragment cost of the TextureMapping modes on the GPU.
 *
 * A headless world renders a sphere that fills a 3840x2160 offscreen surface, lit by a sun,
 * with albedo, ORM, normal and emissive layers, on the main thread and waits for the GPU after
 * every frame, so a case measures a whole frame.
 *
 * Eight concentric alpha blended shells are drawn around it, so every pixel shades nine
 * fragments; the cost of a mode is the difference with the case without TextureMapping.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Side of the noise texture of the layers in pixels */
#define BENCH_MAPPING_TEXTURE (512)

/** Shapes drawn over each pixel: the opaque sphere and the blended shells in front of it */
#define BENCH_MAPPING_LAYERS (9)

/** Mapping a case runs */
typedef enum bench_mapping_t {
    BENCH_MAPPING_NONE,     /**< No TextureMapping */
    BENCH_MAPPING_UV,       /**< MappingUv */
    BENCH_MAPPING_OBJECT,   /**< MappingObject */
    BENCH_MAPPING_WORLD,    /**< MappingWorld with a blend of 0 */
    BENCH_MAPPING_TRIPLANAR /**< MappingWorld with a blend of 0.5 */
} bench_mapping_t;

/** Set the material of a shape of a case.
 *
 * @param world The world
 * @param e The shape
 * @param tex The texture of every layer
 * @param kind The mapping of the case
 */
static void bench_mapping_material(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t tex,
    bench_mapping_t kind)
{
    ecs_set(world, e, Rgb, {220, 200, 180});
    ecs_set(world, e, PbrTextures, {.albedo = tex, .orm = tex, .normal = tex, .emissive = tex});
    ecs_set(world, e, Emissive, {.strength = 0.1f, .color = {255, 255, 255}});
    if (kind == BENCH_MAPPING_UV) {
        ecs_set(world, e, TextureMapping, {.mode = MappingUv, .size = 1});
    } else if (kind == BENCH_MAPPING_OBJECT) {
        ecs_set(world, e, TextureMapping, {.mode = MappingObject, .size = 1});
    } else if (kind == BENCH_MAPPING_WORLD) {
        ecs_set(world, e, TextureMapping, {.mode = MappingWorld, .size = 1});
    } else if (kind == BENCH_MAPPING_TRIPLANAR) {
        ecs_set(world, e, TextureMapping, {.mode = MappingWorld, .size = 1, .blend = 0.5f});
    }
}

/** Render frames of a screen filling sphere and blended shells with a mapping.
 *
 * @param b The benchmark
 * @param kind The mapping of the shapes
 */
static void bench_mapping(
    bench_t *b,
    bench_mapping_t kind)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    char *file = flecs_asprintf("%s/bench.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t *px = ecs_os_malloc(BENCH_MAPPING_TEXTURE * BENCH_MAPPING_TEXTURE * 4);
    int32_t i;
    uint32_t seed = 1;
    for (i = 0; i < BENCH_MAPPING_TEXTURE * BENCH_MAPPING_TEXTURE * 4; i ++) {
        seed = seed * 1664525u + 1013904223u;
        px[i] = (uint8_t)(seed >> 24);
    }

    stbi_write_png(file, BENCH_MAPPING_TEXTURE, BENCH_MAPPING_TEXTURE, 4, px,
        BENCH_MAPPING_TEXTURE * 4);
    ecs_os_free(px);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 4.2f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {110, 150, 210, 255}}, .lighting.enabled = true});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});

    for (i = 0; i < BENCH_MAPPING_LAYERS; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Sphere, {.radius = 3.0f + (float)i * 0.1f, .smooth = true,
            .segments = 64});
        bench_mapping_material(world, e, tex, kind);
        if (i) {
            ecs_set(world, e, Opacity, {0.5f});
            ecs_set(world, e, BlendMode, {AlphaBlend});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 3840, .height = 2160, .resolution_scale = 1});
    for (i = 0; i < 100; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        const TextureStatus *status = ecs_get(world, tex, TextureStatus);
        if (i >= 3 && status && status->state != TextureLoading) {
            break;
        }
    }

    uint32_t word = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
    ecs_os_free(file);
}

void TextureMapping_none(
    bench_t *b)
{
    // materials/texture_mapping
    bench_mapping(b, BENCH_MAPPING_NONE);
}

void TextureMapping_uv(
    bench_t *b)
{
    // materials/texture_mapping
    bench_mapping(b, BENCH_MAPPING_UV);
}

void TextureMapping_object(
    bench_t *b)
{
    // materials/texture_mapping
    bench_mapping(b, BENCH_MAPPING_OBJECT);
}

void TextureMapping_world(
    bench_t *b)
{
    // materials/texture_mapping_world
    bench_mapping(b, BENCH_MAPPING_WORLD);
}

void TextureMapping_triplanar(
    bench_t *b)
{
    // materials/texture_mapping_world
    bench_mapping(b, BENCH_MAPPING_TRIPLANAR);
}
