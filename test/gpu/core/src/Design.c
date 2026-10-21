#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Design_capture(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, App, {
        .headless = true, .design = true, .frames = 1,
        .unguarded = true, .unpaced = true
    });
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {
        .width = 256, .height = 256, .resolution_scale = 1, .pixel_ratio = 1,
        .write_to_file = "design.png"
    });
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {10, 20, 30});
    ecs_set(world, shape, Box, {2, 2, 2});
    ecs_set(world, shape, Rgb, {200, 200, 200});
    test_int(box_app_run(world), 0);
    box_render_sync(world);
    const char *dir = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/design.png", dir);
    int width, height, channels;
    unsigned char *overview = stbi_load(path, &width, &height, &channels, 4);
    test_assert(overview != NULL);
    test_int(width, 256);
    test_int(height, 256);
    ecs_os_free(path);
    const char *names[] = {
        "top", "bottom", "left", "right", "front", "back",
        "perspective", "perspective-back"
    };
    for (int i = 0; i < 8; i ++) {
        path = flecs_asprintf("%s/design-%s.png", dir, names[i]);
        unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
        test_assert(pixels != NULL);
        test_int(width, 128);
        test_int(height, i < 6 ? 138 : 112);
        unsigned char *center = &pixels[((height / 2) * width + 64) * 4];
        test_assert(center[0] > 100);
        test_assert(abs(center[0] - center[1]) < 3);
        test_assert(abs(center[1] - center[2]) < 3);
        int x = i < 6 ? (i % 2) * 64 + 32 : 192;
        int y = i < 6 ? (int)((i / 2 + 0.5f) * (256.0f / 3) + 8)
            : (i - 6) * 128 + 72;
        unsigned char *tile = &overview[(y * 256 + x) * 4];
        test_assert(abs(tile[0] - center[0]) < 5);
        test_assert(abs(tile[1] - center[1]) < 5);
        test_assert(abs(tile[2] - center[2]) < 5);
        stbi_image_free(pixels);
        ecs_os_free(path);
    }
    stbi_image_free(overview);
    ecs_progress(world, 0.016f);
    ecs_progress(world, 0.016f);
    box_render_sync(world);
    path = flecs_asprintf("%s/design.png", dir);
    overview = stbi_load(path, &width, &height, &channels, 4);
    test_assert(overview != NULL);
    for (int i = 0; i < 8; i ++) {
        int left = i < 6 ? (i % 2) * 64 : 128;
        int top = i < 6 ? (int)((i / 2) * (256.0f / 3)) : (i - 6) * 128;
        int ink = 0;
        for (int y = top + 2; y < top + 14; y ++) {
            for (int x = left + 4; x < left + 30; x ++) {
                ink += overview[(y * width + x) * 4] > 80;
            }
        }
        test_assert(ink > 5);
        test_int(overview[((top + 40) * width + left) * 4], 0);
    }
    stbi_image_free(overview);
    ecs_os_free(path);
    ecs_fini(world);
}
