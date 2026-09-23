/**
 * @file Textures.c
 * @brief Benchmarks for the extract cost of materials that bind PBR textures.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Materials in the extracted scene */
#define BENCH_MATERIAL_COUNT (1000)

/** Texture entities the materials pick their slots from */
#define BENCH_TEXTURE_COUNT (20)

/** Edge length of the generated texture fixtures */
#define BENCH_TEXTURE_SIZE (256)

int box_image_write_png(
    const char *path,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    const void *pixels);

void Textures_extract_1000_materials(
    bench_t *b)
{
    // textures/textures
    const char *path = "/tmp/box_bench_texture.png";
    uint8_t *pixels = malloc(BENCH_TEXTURE_SIZE * BENCH_TEXTURE_SIZE * 4);
    memset(pixels, 160, BENCH_TEXTURE_SIZE * BENCH_TEXTURE_SIZE * 4);
    box_image_write_png(path, BENCH_TEXTURE_SIZE, BENCH_TEXTURE_SIZE,
        BENCH_TEXTURE_SIZE * 4, pixels);
    free(pixels);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t textures[BENCH_TEXTURE_COUNT];
    int32_t i;
    for (i = 0; i < BENCH_TEXTURE_COUNT; i ++) {
        textures[i] = ecs_new(world);
        ecs_set(world, textures[i], Texture, { .path = ecs_os_strdup(path) });
    }

    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_MATERIAL_COUNT);
    for (i = 0; i < BENCH_MATERIAL_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_set(world, scene[i], PbrTextures, {
            .albedo = textures[i % BENCH_TEXTURE_COUNT],
            .emissive = textures[(i + 3) % BENCH_TEXTURE_COUNT],
            .roughness = textures[(i + 7) % BENCH_TEXTURE_COUNT],
            .normal = textures[(i + 11) % BENCH_TEXTURE_COUNT]
        });
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, 0);
    free(mesh->vertices);
    free(mesh->indices);
    for (i = 0; i < (int32_t)packet.texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
    }

    bench_set_items(b, BENCH_MATERIAL_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        for (i = 0; i < BENCH_MATERIAL_COUNT; i ++) {
            box_render_mark_dirty(world, scene[i]);
        }

        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}
