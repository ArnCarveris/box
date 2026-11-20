/**
 * @file GpuStats.c
 * @brief Tests the counters behind the GPU stats panel: the webgpu commands the renderer
 * records, the triangles its indirect draws drew after culling and GpuStats while it is open.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void GpuStats_counts_commands(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    for (int32_t i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 2 - 3, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_add(world, e, DynamicTransform);
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    const GpuStats *s = ecs_singleton_get(world, GpuStats);
    test_bool(s->gpu, true);
    test_int(s->frames, 1);
    test_assert(s->packets >= 1);
    test_int(s->scene.dynamic, 4);
    test_assert(s->scene.instances >= 4);
    test_assert(s->scene.views >= 1);
    test_assert(s->scene.visible >= 4);
    test_assert(s->scene.triangles >= 48);
    test_assert(s->commands.render_passes >= 1);
    test_assert(s->commands.draws >= 1);
    test_assert(s->commands.pipelines >= 1);
    test_assert(s->commands.bind_groups > s->commands.pipelines);
    test_assert(s->commands.buffer_binds >= 1);
    test_assert(s->commands.submits >= 1);
    test_assert(s->commands.command_buffers >= s->commands.submits);
    test_assert(s->commands.writes >= 1);
    test_assert(s->commands.write_bytes > 0);
    test_assert(s->commands.total > s->commands.draws);
    test_assert(s->commands.pipelines_cached > 0);
    test_assert(s->commands.pipelines_created > 0);
    test_assert(s->memory.buffers > 0);
    test_assert(s->memory.meshes > 0);
    test_assert(s->memory.targets > 0);
    test_assert(s->memory.instance_bytes > 0);

    ecs_fini(world);
}

void GpuStats_counts_triangles(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    for (int32_t i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 2 - 3, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_stats_detail(world, true), true);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t stats;
    box_render_cull_stats_t cull;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_bool(box_render_gpu_cull_stats(world, view, &cull), true);
    test_int(cull.visible, 4);
    test_assert(cull.indices > 0);
    test_assert(stats.triangle_reads > 0);
    test_int(stats.view_triangles % stats.triangle_reads, 0);
    test_int(stats.view_triangles / stats.triangle_reads, cull.indices / 3);
    test_int(stats.view_triangles / stats.triangle_reads, 4 * 12);
    test_int(stats.shadow_triangles, 0);

    ecs_fini(world);
}

void GpuStats_counts_culled_triangles(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    float pos[6][2] = {{-2, -2}, {2, 2}, {-20, 0}, {20, 0}, {0, -20}, {0, 20}};
    for (int32_t i = 0; i < 6; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {pos[i][0], pos[i][1], 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_stats_detail(world, true), true);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t stats;
    box_render_cull_stats_t cull;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_bool(box_render_gpu_cull_stats(world, view, &cull), true);
    test_int(cull.tested, 6);
    test_int(cull.visible, 2);
    test_assert(stats.triangle_reads > 0);
    test_int(stats.view_triangles / stats.triangle_reads, 2 * 12);

    ecs_fini(world);
}

void GpuStats_detail_off_when_closed(void) {
    // fps/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    box_render_gpu_stats_t stats;
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.triangle_reads, 0);

    ecs_singleton_set(world, GpuPanel, { .open = true, .interval = 0 });
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);
    test_assert(stats.triangle_reads > 0);

    ecs_singleton_set(world, GpuPanel, { .open = false });
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);
    int32_t reads = stats.triangle_reads;
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.triangle_reads, reads);

    ecs_fini(world);
}
