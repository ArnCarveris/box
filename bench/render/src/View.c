/**
 * @file View.c
 * @brief Benchmark for extracting a scene through two views into one frame packet.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/render/render_internal.h"

void View_extract_two_views(bench_t *b)
{
    // views/view
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});

    int32_t i;
    for (i = 0; i < 1000; i ++) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {(float)(i % 50) * 2, 1, (float)(i / 50) * 2});
        ecs_set(world, light, PointLight, {.intensity = 1, .range = 4});
    }

    ecs_entity_t main_camera = ecs_new(world);
    ecs_set(world, main_camera, Position, {50, 20, -30});
    ecs_set(world, main_camera, Camera, {.fov = 60});
    ecs_entity_t side_camera = ecs_new(world);
    ecs_set(world, side_camera, Position, {-30, 10, 20});
    ecs_set(world, side_camera, Camera, {.fov = 45});

    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, RenderTarget, {512, 512});

    ecs_entity_t main_view = ecs_new(world);
    ecs_add(world, main_view, View);
    View *main_settings = ecs_get_mut(world, main_view, View);
    main_settings->camera = main_camera;
    ecs_modified(world, main_view, View);

    ecs_entity_t side_view = ecs_new(world);
    ecs_add(world, side_view, View);
    View *side_settings = ecs_get_mut(world, side_view, View);
    side_settings->camera = side_camera;
    side_settings->target = target;
    side_settings->priority = -1;
    ecs_modified(world, side_view, View);

    bench_set_items(b, 2);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.views.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
