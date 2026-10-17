/**
 * @file ReflectionPrefilter.c
 * @brief Tests for the prefilter of the reflection probes: the SH irradiance of each slot.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** E_00 of a uniform radiance of 1: pi times the integral of Y_00, 2 sqrt(pi) */
#define BAND0 (GLM_PI * 2.0 * 1.7724538509)

/** E_1-1 of a radiance of 1.5 y: pi times the integral of y Y_1-1, 2 sqrt(pi / 3) */
#define BAND1 (GLM_PI * 2.0 * 1.0233267079)

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Create a scene with a ReflectionBake, an ambient light, a camera with a view and a surface.
 *
 * @param world The world
 * @param bake The ReflectionBake of the scene
 * @param ambient The AmbientLight of the scene
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    const ReflectionBake *bake,
    const AmbientLight *ambient)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set_ptr(world, scene, ReflectionBake, bake);
    ecs_set_ptr(world, scene, AmbientLight, ambient);
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    return scene;
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

/** Read the SH irradiance of the slot of the scene probe or of the first other probe.
 *
 * @param world The world
 * @param scene The scene
 * @param scene_probe Whether to read the scene probe
 * @param out Receives the coefficients
 */
static void read_sh(
    ecs_world_t *world,
    ecs_entity_t scene,
    bool scene_probe,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    int32_t i, count, slot = -1;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    for (i = 0; i < count && slot == -1; i ++) {
        if ((probes[i].scene != 0) == scene_probe) {
            slot = (int32_t)probes[i].slot;
        }
    }

    uint32_t texture = box_render_reflect_texture(world, scene);
    test_assert(slot >= 0);
    test_assert(texture != 0);
    test_assert(box_render_gpu_read_reflection_sh(world, texture, (uint32_t)slot, out));
}

/** Check that SH coefficients match the expected ones within a fraction of the largest.
 *
 * @param sh The coefficients
 * @param expected The expected coefficients
 */
static void sh_expect(
    float sh[BOX_RENDER_SH_COEFFICIENTS][4],
    float expected[BOX_RENDER_SH_COEFFICIENTS][4])
{
    float largest = 0;
    int32_t k, c;
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 3; c ++) {
            largest = glm_max(largest, fabsf(expected[k][c]));
        }
    }

    test_assert(largest > 0);
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(sh[k][c] - expected[k][c]) < 0.02f * largest);
        }
    }
}

/* Six emissive walls with a black base color enclose the probe and no light reaches them, so
 * every direction sees the radiance of their emission: the SH of a uniform environment L is
 * E_00 = pi L 2 sqrt(pi) and nothing else. */
void ReflectionPrefilter_uniform_emissive_matches_uniform_environment(void) {
    // reflection_probes/prefilter
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world,
        &(ReflectionBake){.resolution = 16, .budget_ms = 1, .scene_probe = false},
        &(AmbientLight){.intensity = 0});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    int32_t i;
    for (i = 0; i < 6; i ++) {
        int32_t axis = i / 2;
        float position[3] = {0, 1, 0}, size[3] = {2.6f, 2.6f, 2.6f};
        position[axis] += i % 2 ? -1.1f : 1.1f;
        size[axis] = 0.2f;
        ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, wall, Position, {position[0], position[1], position[2]});
        ecs_set(world, wall, Box, {size[0], size[1], size[2]});
        ecs_set(world, wall, Rgb, {0, 0, 0});
        ecs_set(world, wall, Emissive, {.strength = 2, .color = {255, 255, 0}});
    }

    test_assert(converge(world, scene, 50) < 50);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, 1);

    float sh[BOX_RENDER_SH_COEFFICIENTS][4], expected[BOX_RENDER_SH_COEFFICIENTS][4] = {
        {(float)(2 * BAND0), (float)(2 * BAND0), 0, 0}
    };
    read_sh(world, scene, false, sh);
    sh_expect(sh, expected);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

/* Nothing covers a texel of the scene probe of an empty scene, so it sees the hemisphere
 * ambient, whose irradiance over pi is a + b y with a the mean and b half the difference of sky
 * and ground: E_00 = pi a 2 sqrt(pi), E_1-1 = pi b 2 sqrt(pi / 3) and nothing else. */
void ReflectionPrefilter_empty_scene_matches_hemisphere_ambient(void) {
    // reflection_probes/prefilter
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world,
        &(ReflectionBake){.resolution = 16, .budget_ms = 1, .scene_probe = true,
            .scene_probe_height = 2},
        &(AmbientLight){.sky = {255, 255, 0}, .ground = {0, 255, 255}, .intensity = 1});

    test_assert(converge(world, scene, 50) < 50);
    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_int(status->probes, 1);
    test_int(status->captured, 1);

    float sky[3] = {1, 1, 0}, ground[3] = {0, 1, 1};
    float sh[BOX_RENDER_SH_COEFFICIENTS][4], expected[BOX_RENDER_SH_COEFFICIENTS][4] = {{0}};
    int32_t c;
    for (c = 0; c < 3; c ++) {
        expected[0][c] = (float)((sky[c] + ground[c]) * 0.5 * BAND0);
        expected[1][c] = (float)((sky[c] - ground[c]) * 0.5 * BAND1);
    }

    read_sh(world, scene, true, sh);
    sh_expect(sh, expected);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}
