/**
 * @file ReflectionCapture.c
 * @brief Tests for the captures of the reflection probes into the cubemap array of a scene.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

/** Create a world that renders with the GPU, optionally headless and on the main thread.
 *
 * @param headless Whether the run is headless
 * @param main_thread Whether the main thread renders, so the backend can be read
 * @return The world
 */
static ecs_world_t* world_new(
    bool headless,
    bool main_thread)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    if (headless) {
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);
    }

    if (main_thread) {
        ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    }

    return world;
}

/** Create a scene with a ReflectionBake, a static floor, a camera with a view and a surface.
 *
 * @param world The world
 * @param bake The ReflectionBake of the scene
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    const ReflectionBake *bake)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set_ptr(world, scene, ReflectionBake, bake);
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    return scene;
}

/** Create a probe in a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position along x
 * @return The probe entity
 */
static ecs_entity_t probe_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x)
{
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {x, 1, 0});
    ecs_add(world, probe, ReflectionProbe);

    return probe;
}

/** Find the slot of the resident probe captured at a position along x.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position along x of the capture point
 * @return The slot, -1 when no resident probe is there
 */
static int32_t slot_at(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x)
{
    int32_t i, count, result = -1;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    for (i = 0; i < count; i ++) {
        if (!probes[i].scene && probes[i].capture[0] == x) {
            result = (int32_t)probes[i].slot;
        }
    }

    return result;
}

/** Run frames until the reflection probes of a scene converged.
 *
 * @param world The world
 * @param scene The scene
 * @param max Frames to run at most
 * @return Frames it took, max when they did not converge
 */
static int32_t converge(
    ecs_world_t *world,
    ecs_entity_t scene,
    int32_t max)
{
    int32_t frame;
    for (frame = 0; frame < max; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        if (ecs_get(world, scene, ReflectionBakeStatus)->converged) {
            break;
        }
    }

    return frame;
}

/** Get the cubemap slots of a scene on the device of a world, rendering until it exists.
 *
 * @param world The world
 * @return maxTextureArrayLayers of the device over the faces of a cubemap
 */
static int32_t capacity_of(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats = {0};
    int32_t frame;
    for (frame = 0; frame < 50 && !stats.texture_layers; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_stats(world, &stats);
    }

    test_assert(stats.texture_layers > 0);

    return stats.texture_layers / 6;
}

void ReflectionCapture_converges_headless(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, false);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 16, .budget_ms = 1, .scene_probe = true, .scene_probe_height = 2});
    probe_new(world, scene, -1);
    probe_new(world, scene, 1);

    int32_t frame, captured = 0;
    const ReflectionBakeStatus *status = NULL;
    for (frame = 0; frame < 50; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, scene, ReflectionBakeStatus);
        test_assert(status->captured >= captured);
        test_assert(status->captured <= status->resident);
        captured = status->captured;
        if (status->converged) {
            break;
        }
    }

    test_bool(status->converged, true);
    test_int(status->probes, 3);
    test_int(status->resident, 3);
    test_int(status->captured, 3);
    test_assert(status->error == NULL);
    test_assert(box_render_reflect_texture(world, scene) != 0);
    test_int(box_render_reflect_pending(world), 0);

    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, ReflectionBakeStatus);
    test_bool(status->converged, true);
    test_int(status->captured, 3);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void ReflectionCapture_converges_with_light_bake(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, false);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 16, .budget_ms = 1, .scene_probe = true, .scene_probe_height = 2});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 1});
    probe_new(world, scene, 1);

    int32_t frame;
    const ReflectionBakeStatus *status = NULL;
    const LightBakeStatus *light = NULL;
    for (frame = 0; frame < 50; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, scene, ReflectionBakeStatus);
        light = ecs_get(world, scene, LightBakeStatus);
        if (status->converged && light->converged) {
            break;
        }
    }

    test_bool(status->converged, true);
    test_bool(light->converged, true);
    test_int(status->captured, 2);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void ReflectionCapture_zero_budget_captures_nothing(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, false);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 16, .budget_ms = 0, .scene_probe = true, .scene_probe_height = 2});
    probe_new(world, scene, 1);

    int32_t frame;
    for (frame = 0; frame < 30; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_int(status->probes, 2);
    test_int(status->resident, 2);
    test_int(status->captured, 0);
    test_bool(status->converged, false);
    test_int(box_render_reflect_pending(world), 0);

    ecs_fini(world);
}

void ReflectionCapture_probes_beyond_capacity_not_captured(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, false);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 8, .budget_ms = 1, .scene_probe = false});
    int32_t i, capacity = capacity_of(world);
    for (i = 0; i <= capacity; i ++) {
        probe_new(world, scene, (float)i * 0.01f);
    }

    test_assert(converge(world, scene, 100) < 100);
    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_int(status->probes, capacity + 1);
    test_int(status->resident, capacity);
    test_int(status->captured, capacity);
    test_bool(status->converged, true);

    int32_t count;
    box_render_reflect_probes(world, scene, &count);
    test_int(count, capacity);

    ecs_fini(world);
}

void ReflectionCapture_removed_probe_frees_slot(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, false);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 8, .budget_ms = 1, .scene_probe = false});
    int32_t i, capacity = capacity_of(world);
    ecs_entity_t removed = 0;
    for (i = 0; i < capacity; i ++) {
        ecs_entity_t probe = probe_new(world, scene, (float)(i + 1) * 0.01f);
        removed = i == capacity / 2 ? probe : removed;
    }

    test_assert(converge(world, scene, 100) < 100);
    float x = (float)(capacity / 2 + 1) * 0.01f;
    int32_t slot = slot_at(world, scene, x);
    test_assert(slot >= 0);

    ecs_delete(world, removed);
    probe_new(world, scene, -1);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(slot_at(world, scene, -1), slot);
    test_int(slot_at(world, scene, x), -1);

    test_assert(converge(world, scene, 100) < 100);
    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_int(status->probes, capacity);
    test_int(status->resident, capacity);
    test_int(status->captured, capacity);
    test_int(slot_at(world, scene, -1), slot);

    ecs_fini(world);
}

/* The wall covers z from 0.5 to 2.5 at x 3, which the +x face shows left of its center in the
 * WebGPU cubemap convention, and the -x face does not show at all. */
void ReflectionCapture_wall_shows_in_facing_face(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, true);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 16, .budget_ms = 1, .scene_probe = false});
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {3, 1, 1.5f});
    ecs_set(world, wall, Box, {0.2f, 2, 2});
    ecs_set(world, wall, Rgb, {255, 0, 0});
    ecs_set(world, wall, Emissive, {.strength = 4, .color = {255, 0, 0}});

    test_assert(converge(world, scene, 50) < 50);
    uint32_t texture = box_render_reflect_texture(world, scene);
    int32_t slot = slot_at(world, scene, 0);
    test_assert(texture != 0);
    test_assert(slot >= 0);

    float texels[16 * 16 * 4];
    test_assert(box_render_gpu_read_reflection(world, texture, (uint32_t)slot, 0, texels,
        16 * 16 * 4));
    const float *red = &texels[(8 * 16 + 4) * 4];
    const float *mirrored = &texels[(8 * 16 + 11) * 4];
    test_assert(red[0] > 4 * red[1] && red[0] > 4 * red[2]);
    test_assert(mirrored[0] < 2 * mirrored[2]);

    test_assert(box_render_gpu_read_reflection(world, texture, (uint32_t)slot, 1, texels,
        16 * 16 * 4));
    const float *behind = &texels[(8 * 16 + 8) * 4];
    test_assert(behind[0] < 2 * behind[2]);

    ecs_fini(world);
}

void ReflectionCapture_perf_run_waits_for_captures(void) {
    // reflection_probes/capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 8, .budget_ms = 0.001f, .scene_probe = true, .scene_probe_height = 2});
    ecs_iter_t it = ecs_each(world, Surface);
    test_assert(ecs_each_next(&it));
    ecs_entity_t surface = it.entities[0];
    ecs_iter_fini(&it);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1,
        .texture = surface});
    int32_t i, frame;
    for (i = 0; i < 40; i ++) {
        probe_new(world, scene, (float)i * 0.1f);
    }

    ecs_singleton_set(world, PerfRun, {.fps = 120, .frames = 4, .warmup = 2});
    for (frame = 0; frame < 12; frame ++) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfWarmup);

    bool converged = false;
    for (frame = 0; frame < 5000; frame ++) {
        ecs_progress(world, 0);
        const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
        if (!converged && status->converged) {
            converged = true;
            test_int(ecs_singleton_get(world, PerfStatus)->state, PerfWarmup);
        }

        if (ecs_singleton_get(world, PerfStatus)->state == PerfDone) {
            break;
        }
    }

    test_assert(converged);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);

    ecs_fini(world);
}

void ReflectionCapture_non_finite_radiance_counts_as_sky(void) {
    // reflection_probes/capture
    ecs_world_t *world = world_new(true, true);
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = 16, .budget_ms = 1, .scene_probe = false});
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {3, 1, 0});
    ecs_set(world, wall, Box, {0.2f, 2, 2});
    ecs_set(world, wall, Emissive, {.strength = INFINITY, .color = {255, 255, 255}});

    test_assert(converge(world, scene, 50) < 50);
    uint32_t texture = box_render_reflect_texture(world, scene);
    int32_t slot = slot_at(world, scene, 0);
    test_assert(texture != 0);
    test_assert(slot >= 0);

    float texels[16 * 16 * 4];
    uint32_t face;
    int32_t i;
    for (face = 0; face < 6; face ++) {
        test_assert(box_render_gpu_read_reflection(world, texture, (uint32_t)slot, face,
            texels, 16 * 16 * 4));
        for (i = 0; i < 16 * 16 * 4; i ++) {
            test_assert(isfinite(texels[i]));
        }
    }

    float sh[BOX_RENDER_SH_COEFFICIENTS][4];
    test_assert(box_render_gpu_read_reflection_sh(world, texture, (uint32_t)slot, sh));
    for (i = 0; i < BOX_RENDER_SH_COEFFICIENTS; i ++) {
        test_assert(isfinite(sh[i][0]) && isfinite(sh[i][1]) && isfinite(sh[i][2]));
    }

    ecs_fini(world);
}
