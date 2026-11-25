/**
 * @file FrameCost.c
 * @brief Tests what a frame of the GPU backend records when nothing changed: the compute passes
 * of a frame share a pass, the target is cleared by its first pass, the atmosphere passes of a
 * view and the uploads of the cull pass run again only when what they read changed.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void FrameCost_compute_passes_share_a_pass(void) {
    // render/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -10});
    ecs_set(world, wall, Box, {30, 30, 1});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t hidden = ecs_new(world);
    ecs_set(world, hidden, Position, {0, 0, -20});
    ecs_set(world, hidden, Box, {2, 2, 2});
    ecs_set(world, hidden, Rgb, {255, 0, 0});
    ecs_entity_t away = ecs_new(world);
    ecs_set(world, away, Position, {100, 0, -10});
    ecs_set(world, away, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before, after, detail;
    test_bool(box_render_gpu_stats(world, &before), true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    int32_t frames = after.packets - before.packets;
    test_int(frames, 6);
    test_int(after.api_compute_passes - before.api_compute_passes, 2 * frames);
    test_assert(after.cull_dispatches - before.cull_dispatches >= 4 * frames);
    test_assert(after.api_dispatches - before.api_dispatches >
        after.api_compute_passes - before.api_compute_passes);
    test_int(after.cull_passes - before.cull_passes, 0);

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    int32_t waited = 0;
    while ((!status || status->frame < start) && waited < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
        waited ++;
    }

    test_assert(status != NULL);
    test_int(status->instances, 3);
    test_int(status->visible, 1);
    test_int(status->frustum_culled, 1);
    test_int(status->occlusion_culled, 1);

    test_bool(box_render_gpu_stats_detail(world, true), true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &before), true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &detail), true);
    frames = detail.packets - before.packets;
    test_int(frames, 6);
    test_assert(detail.api_compute_passes - before.api_compute_passes >= 2 * frames);
    if (detail.cull_passes > before.cull_passes) {
        test_int(detail.api_compute_passes - before.api_compute_passes, 3 * frames);
    }

    ecs_fini(world);
}

void FrameCost_target_cleared_by_first_pass(void) {
    // render/gpu
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
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_set(world, box, Rgb, {255, 255, 255});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before, with_view, without_view;
    test_bool(box_render_gpu_stats(world, &before), true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &with_view), true);
    test_int(with_view.packets - before.packets, 4);
    int32_t drawn = with_view.api_render_passes - before.api_render_passes;
    test_assert(with_view.forward_passes - before.forward_passes == 4);

    ecs_delete(world, view);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &before), true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &without_view), true);
    test_int(without_view.packets - before.packets, 4);
    test_int(without_view.forward_passes - before.forward_passes, 0);
    int32_t cleared = without_view.api_render_passes - before.api_render_passes;
    test_assert(cleared >= 4);
    test_int(drawn, cleared);

    ecs_fini(world);
}

void FrameCost_atmosphere_passes_only_when_changed(void) {
    // render/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {.aerial_perspective = true});

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.aerial_perspective = true});
    ecs_add(world, scene, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(40), 0, 0});
    ecs_get_mut(world, scene, Atmosphere)->sun = sun;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 0});
    ecs_set(world, camera, Rotation, {glm_rad(10), 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {10, 200, 20, 255}};
    v->lighting.enabled = false;
    v->lighting.atmosphere = true;
    ecs_modified(world, view, View);
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, box, Position, {0, 0, -5});
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_set(world, box, Rgb, {200, 40, 40});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1});
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t s0, s1, s2, s3;
    test_bool(box_render_gpu_stats(world, &s0), true);
    test_assert(s0.atmosphere_views >= 1);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &s1), true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_set(world, camera, Rotation, {glm_rad(10), glm_rad((float)(i + 1) * 3), 0});
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &s2), true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &s3), true);
    test_int(s1.packets - s0.packets, 4);
    test_int(s2.packets - s1.packets, 4);
    test_int(s3.packets - s2.packets, 4);
    int32_t still = s1.api_dispatches - s0.api_dispatches;
    int32_t moving = s2.api_dispatches - s1.api_dispatches;
    int32_t again = s3.api_dispatches - s2.api_dispatches;
    test_int(moving - still, 2 * 4);
    test_int(again, still);
    test_assert(s2.api_writes - s1.api_writes > s1.api_writes - s0.api_writes);

    ecs_fini(world);
}

void FrameCost_cull_uploads_only_when_changed(void) {
    // render/gpu
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t near_box = ecs_new(world);
    ecs_set(world, near_box, Position, {0, 0, -10});
    ecs_set(world, near_box, Box, {2, 2, 2});
    ecs_set(world, near_box, Rgb, {0, 255, 0});
    ecs_entity_t side = ecs_new(world);
    ecs_set(world, side, Position, {-40, 0, -10});
    ecs_set(world, side, Box, {2, 2, 2});
    ecs_set(world, side, Rgb, {255, 0, 0});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t s0, s1, s2;
    test_bool(box_render_gpu_stats(world, &s0), true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &s1), true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_set(world, camera, Rotation, {0, glm_rad((float)(i + 1) * 0.5f), 0});
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &s2), true);
    test_int(s1.packets - s0.packets, 4);
    test_int(s2.packets - s1.packets, 4);
    int32_t still = s1.api_writes - s0.api_writes;
    int32_t moving = s2.api_writes - s1.api_writes;
    test_assert(moving >= still + 4);

    ecs_set(world, camera, Rotation, {0, glm_rad(76), 0});
    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    int32_t waited = 0;
    while ((!status || status->frame < start) && waited < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
        waited ++;
    }

    test_assert(status != NULL);
    test_int(status->instances, 2);
    test_int(status->visible, 1);
    test_int(status->frustum_culled, 1);

    ecs_set(world, camera, Rotation, {0, 0, 0});
    start = ecs_get_world_info(world)->frame_count_total + 3;
    status = NULL;
    waited = 0;
    while ((!status || status->frame < start) && waited < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
        waited ++;
    }

    test_assert(status != NULL);
    test_int(status->visible, 1);
    test_int(status->frustum_culled, 1);

    ecs_fini(world);
}
