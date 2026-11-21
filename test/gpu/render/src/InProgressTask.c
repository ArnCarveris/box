/**
 * @file InProgressTask.c
 * @brief The tasks the renderer reports in InProgressTask: shader compiles whose outcome did not
 * arrive and pipelines being created block until every module and pipeline is ready, while
 * frames keep rendering, and a LightBake reports its probes as a task that does not block.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

/** Blit shader that fills the image with magenta */
static const char *task_magenta =
    "struct V { @builtin(position) pos: vec4f, @location(0) uv: vec2f, };\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> V {\n"
    "    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return V(vec4f(p * 2.0 - 1.0, 0.0, 1.0), vec2f(p.x, 1.0 - p.y));\n"
    "}\n"
    "@group(0) @binding(0) var scene: texture_2d<f32>;\n"
    "@group(0) @binding(1) var scene_sampler: sampler;\n"
    "@fragment fn fs(v: V) -> @location(0) vec4f {\n"
    "    return textureSample(scene, scene_sampler, v.uv) * 0.0 + vec4f(1.0, 0.0, 1.0, 1.0);\n"
    "}\n";

/** Mesh shader that compiles but whose pipelines do not fit the layout of the forward pipeline,
 * which has no binding 99 */
static const char *task_unbound =
    "override blend_mode: u32 = 0u;\n"
    "@group(0) @binding(99) var<uniform> extra: vec4f;\n"
    "@vertex fn vs(@location(0) p: vec3f) -> @builtin(position) vec4f {\n"
    "    return vec4f(p, 1.0);\n"
    "}\n"
    "@fragment fn fs() -> @location(0) vec4f {\n"
    "    return extra + vec4f(f32(blend_mode));\n"
    "}\n";

/** Create a world with box that renders a cube into a surface.
 *
 * A world that is not headless creates its pipelines on the workers instead of waiting for
 * them; its surface renders into a texture, as one without a window. It keeps the temporary
 * folder it got as a headless world.
 *
 * @param headless Whether the frames are headless
 * @return The world
 */
static ecs_world_t* task_world_new(
    bool headless)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
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
    ecs_set(world, cube, Rgb, {255, 0, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1,
        .texture = headless ? 0 : ecs_new(world)});
    if (!headless) {
        app = ecs_singleton_get_mut(world, App);
        app->headless = false;
        ecs_singleton_modified(world, App);
    }

    return world;
}

/** Create a headless world with box that renders a cube into a surface.
 *
 * @return The world
 */
static ecs_world_t* task_world(void) {
    return task_world_new(true);
}

/** Run a frame and wait for the renderer.
 *
 * @param world The world
 * @return The tasks the frame wrote
 */
static const InProgressTask* task_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    return ecs_singleton_get(world, InProgressTask);
}

void InProgressTask_first_frame_compiles(void) {
    ecs_world_t *world = task_world();
    const InProgressTask *t = task_frame(world);
    const InProgressTaskItem *item = box_task_find(t, BOX_TASK_SHADERS);
    test_assert(item != NULL);
    test_bool(item->blocking, true);
    test_assert(item->total > 0);
    test_assert(item->done <= item->total);
    test_assert(t->blocking >= 1);

    int32_t i;
    for (i = 0; i < 20 && box_task_find(t, BOX_TASK_SHADERS); i ++) {
        t = task_frame(world);
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);

    ecs_fini(world);
}

void InProgressTask_held_compile_blocks(void) {
    ecs_world_t *world = task_world();
    const InProgressTask *t = NULL;
    int32_t i;
    for (i = 0; i < 20; i ++) {
        t = task_frame(world);
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *magenta = flecs_asprintf("%s/task_magenta.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    test_assert(f != NULL);
    fputs(task_magenta, f);
    fclose(f);

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    test_assert(blit != 0);
    ecs_set(world, blit, Shader, {.path = magenta});
    for (i = 0; i < 5; i ++) {
        t = task_frame(world);
        const InProgressTaskItem *item = box_task_find(t, BOX_TASK_SHADERS);
        test_assert(item != NULL);
        test_bool(item->blocking, true);
        test_int(item->done, 0);
        test_int(item->total, 1);
        test_assert(t->blocking >= 1);
    }

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_AT_ONCE), true);
    for (i = 0; i < 10 && box_task_find(t, BOX_TASK_SHADERS); i ++) {
        t = task_frame(world);
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, true);

    ecs_os_free(magenta);
    ecs_fini(world);
}

void InProgressTask_held_pipeline_blocks(void) {
    ecs_world_t *world = task_world();
    const InProgressTask *t = NULL;
    box_render_gpu_stats_t before, stats;
    int32_t i, frames, building = 0;
    ecs_singleton_set(world, Scene, {.shader_features.Extended.blend_modes = true});
    for (i = 0; i < 20; i ++) {
        t = task_frame(world);
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    test_bool(box_render_gpu_stats(world, &before), true);
    test_int(before.pipelines_building, 0);

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {1, 0, 0});
    ecs_set(world, cube, Rgb, {0, 255, 0});
    BlendMode blend = Additive;
    ecs_set_ptr(world, cube, BlendMode, &blend);
    frames = before.frames;
    for (i = 0; i < 5; i ++) {
        t = task_frame(world);
        test_bool(box_render_gpu_stats(world, &stats), true);
        test_assert(stats.frames > frames);
        frames = stats.frames;
        test_assert(stats.pipelines_building >= 1);
        if (i == 0) {
            building = stats.pipelines_building;
            continue;
        }

        test_int(stats.pipelines_building, building);
        test_int(stats.pipelines, before.pipelines);
        const InProgressTaskItem *item = box_task_find(t, BOX_TASK_SHADERS);
        test_assert(item != NULL);
        test_bool(item->blocking, true);
        test_assert(item->total >= 1);
        test_assert(item->done < item->total);
        test_assert(t->blocking >= 1);
    }

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_AT_ONCE), true);
    for (i = 0; i < 10 && box_task_find(t, BOX_TASK_SHADERS); i ++) {
        t = task_frame(world);
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.pipelines_building, 0);
    test_assert(stats.pipelines >= before.pipelines + building);
    test_assert(stats.frames > frames);

    ecs_fini(world);
}

void InProgressTask_fini_with_held_pipeline(void) {
    ecs_world_t *world = task_world();
    box_render_gpu_stats_t stats;
    int32_t i;
    ecs_singleton_set(world, Scene, {.shader_features.Extended.blend_modes = true});
    for (i = 0; i < 20; i ++) {
        task_frame(world);
    }

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Position, {-1, 0, 0});
    BlendMode blend = Screen;
    ecs_set_ptr(world, cube, BlendMode, &blend);
    task_frame(world);
    task_frame(world);
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_assert(stats.pipelines_building >= 1);

    ecs_fini(world);
}

void InProgressTask_background_pipelines(void) {
    ecs_world_t *world = task_world_new(false);
    box_render_gpu_stats_t stats;
    const InProgressTask *t = task_frame(world);
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_assert(stats.pipelines_building >= 1);
    test_assert(box_task_find(t, BOX_TASK_SHADERS) != NULL);

    int32_t i, frames = stats.frames;
    for (i = 0; i < 1000 && box_task_find(t, BOX_TASK_SHADERS); i ++) {
        t = task_frame(world);
        test_bool(box_render_gpu_stats(world, &stats), true);
        test_assert(stats.frames > frames);
        frames = stats.frames;
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    test_int(stats.pipelines_building, 0);
    test_int(stats.errors, 0);
    test_int(stats.shader_errors, 0);

    int32_t draws = stats.draws;
    task_frame(world);
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_assert(stats.draws > draws);
    test_int(stats.pipelines_building, 0);

    ecs_fini(world);
}

void InProgressTask_failed_pipeline_ends_task(void) {
    ecs_world_t *world = task_world_new(false);
    box_render_gpu_stats_t before, stats;
    int32_t i;
    const InProgressTask *t = task_frame(world);
    for (i = 0; i < 1000 && box_task_find(t, BOX_TASK_SHADERS); i ++) {
        t = task_frame(world);
    }

    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    test_bool(box_render_gpu_stats(world, &before), true);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *unbound = flecs_asprintf("%s/task_unbound.wgsl", tmp);
    FILE *f = fopen(unbound, "w");
    test_assert(f != NULL);
    fputs(task_unbound, f);
    fclose(f);

    ecs_entity_t mesh = ecs_lookup(world, "box.shader.builtin.mesh");
    test_assert(mesh != 0);
    ecs_set(world, mesh, Shader, {.path = unbound});
    ecs_log_set_level(-4);
    t = task_frame(world);
    test_assert(box_task_find(t, BOX_TASK_SHADERS) != NULL);
    for (i = 0; i < 1000 && box_task_find(t, BOX_TASK_SHADERS); i ++) {
        t = task_frame(world);
    }

    ecs_log_set_level(-1);
    test_assert(box_task_find(t, BOX_TASK_SHADERS) == NULL);
    const ShaderStatus *st = ecs_get(world, mesh, ShaderStatus);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "99") != NULL);
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.pipelines_building, 0);
    test_int(stats.errors, before.errors);

    int32_t draws = stats.draws;
    task_frame(world);
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_assert(stats.draws > draws);
    test_int(stats.errors, before.errors);

    ecs_os_free(unbound);
    ecs_fini(world);
}

void InProgressTask_bake_reports_probes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 1, .compute = true});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    int32_t frame, seen = 0, full = 0, total = 0, last_done = 0;
    bool regressed = false;
    const LightBakeStatus *status = NULL;
    const InProgressTask *t = NULL;
    for (frame = 0; frame < 5000; frame ++) {
        t = task_frame(world);
        status = ecs_get(world, scene, LightBakeStatus);
        const InProgressTaskItem *item = box_task_find(t, BOX_TASK_PROBES);
        if (item) {
            test_bool(item->blocking, false);
            test_assert(item->total > 0);
            test_assert(item->done <= item->total);
            regressed |= item->done < last_done;
            last_done = item->done;
            total = item->total;
            seen ++;
            full += item->done == item->total;
        }

        if (status->converged && seen && !item) {
            break;
        }
    }

    test_assert(status->converged);
    test_assert(seen > 1);
    test_assert(full >= 1);
    test_bool(regressed, false);
    test_assert(total > 0 && total % 2 == 0);
    test_assert(box_task_find(t, BOX_TASK_PROBES) == NULL);

    t = task_frame(world);
    test_assert(box_task_find(t, BOX_TASK_PROBES) == NULL);

    ecs_fini(world);
}
