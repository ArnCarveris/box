/**
 * @file ProbeZh3.c
 * @brief Tests for the ZH3 the probes of a light probe volume are stored as: bands 0 and 1 of
 * the L2 bake and one zonal band 2 coefficient per channel along the luminance axis of band 1.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "environment.h"
#include "bake.h"

/** Frames a wait renders at most */
#define WAIT_FRAMES (500)

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* zh_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Render a frame and wait until the GPU rendered it.
 *
 * @param world The world
 */
static void zh_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Create a scene without ambient light with a small black static box at the origin, a view of
 * it and a surface.
 *
 * @param world The world
 * @param compute LightBake.compute
 * @param environment LightBake.environment
 * @return The scene
 */
static ecs_entity_t zh_scene(
    ecs_world_t *world,
    bool compute,
    ecs_entity_t environment)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 1,
        .capture_size = 16, .budget_ms = 2, .bounces = 0, .environment = environment,
        .compute = compute});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {0.2f, 0.2f, 0.2f});
    ecs_set(world, box, Rgb, {0, 0, 0});
    ecs_set(world, box, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    return scene;
}

/** Render frames until the bake of a scene converged.
 *
 * @param world The world
 * @param scene The scene
 */
static void zh_converge(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    int32_t frame;
    for (frame = 0; frame < 5000; frame ++) {
        zh_frame(world);
        if (ecs_get(world, scene, LightBakeStatus)->converged) {
            break;
        }
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    zh_frame(world);
}

/** Get the volume of the bake of a scene, once the backend has it.
 *
 * @param world The world
 * @param scene The scene
 * @return The volume
 */
static const box_bake_volume_t* zh_volume(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    const box_bake_volume_t *v = box_bake_find(box_bake_state(world), scene);
    test_assert(v != NULL);
    test_assert(v->handle != 0);
    test_assert(v->probes > 0);
    return v;
}

/** Sloan's real SH basis up to band 2, as basis() of etc/shaders/bake_sh.wgsl.
 *
 * @param d A unit direction
 * @param out Receives the 9 values
 */
static void zh_basis(
    const float d[3],
    float out[BOX_RENDER_SH_COEFFICIENTS])
{
    out[0] = 0.2820947918f;
    out[1] = 0.4886025119f * d[1];
    out[2] = 0.4886025119f * d[2];
    out[3] = 0.4886025119f * d[0];
    out[4] = 1.0925484306f * d[0] * d[1];
    out[5] = 1.0925484306f * d[1] * d[2];
    out[6] = 0.3153915653f * (3.0f * d[2] * d[2] - 1.0f);
    out[7] = 1.0925484306f * d[0] * d[2];
    out[8] = 0.5462742153f * (d[0] * d[0] - d[1] * d[1]);
}

/** Largest difference of two sets of L2 coefficients over the rgb channels.
 *
 * @param a The first coefficients
 * @param b The second coefficients
 * @return The largest absolute difference
 */
static float zh_difference(
    const float a[BOX_RENDER_SH_COEFFICIENTS][4],
    const float b[BOX_RENDER_SH_COEFFICIENTS][4])
{
    float result = 0;
    int32_t k, c;
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 3; c ++) {
            result = glm_max(result, fabsf(a[k][c] - b[k][c]));
        }
    }

    return result;
}

/** The L2 coefficients a ZH3 probe packed from some reads back as.
 *
 * @param sh The L2 coefficients
 * @param out Receives the expansion of their ZH3
 */
static void zh_expected(
    const float sh[BOX_RENDER_SH_COEFFICIENTS][4],
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    float zh3[BOX_RENDER_ZH3_VECTORS][4];
    box_render_sh_to_zh3(sh, zh3);
    box_render_zh3_to_sh(zh3, out);
}

/* A probe lit by a single baked point light holds the L2 bake of that light in bands 0 and 1,
 * and in band 2 its zonal projection along the light, which for one light is band 2 itself. */
void ProbeZh3_single_light_band2_zonal(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = zh_world();
    ecs_entity_t scene = zh_scene(world, true, 0);
    vec3 lamp_at = {3, 4, 2};
    ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, lamp, Position, {lamp_at[0], lamp_at[1], lamp_at[2]});
    ecs_set(world, lamp, PointLight, {.intensity = 20, .range = 100,
        .bake = LightBakeModeBaked});
    zh_converge(world, scene);

    const box_bake_volume_t *v = zh_volume(world, scene);
    float (*sh)[BOX_RENDER_SH_COEFFICIENTS][4] = ecs_os_calloc(
        v->probes * (int32_t)sizeof(sh[0]));
    float brightest = 0;
    int32_t i, k, c, checked = 0;
    for (i = 0; i < v->probes; i ++) {
        if (box_bake_valid(v, (uint32_t)i)) {
            test_assert(box_render_gpu_read_volume_sh(world, v->handle, (uint32_t)v->display,
                (uint32_t)i, sh[i]));
            brightest = glm_max(brightest, sh[i][0][1]);
        }
    }

    test_assert(brightest > 0);
    for (i = 0; i < v->probes; i ++) {
        if (!box_bake_valid(v, (uint32_t)i) || sh[i][0][1] < brightest * 0.5f) {
            continue;
        }

        vec3 pos, d;
        float y[BOX_RENDER_SH_COEFFICIENTS], again[BOX_RENDER_SH_COEFFICIENTS][4];
        box_bake_probe_position(v, (uint32_t)i, pos);
        glm_vec3_sub(lamp_at, pos, d);
        glm_vec3_normalize(d);
        zh_basis(d, y);
        for (k = 1; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
            /* E_lm / E_00 of a delta light: A_l Y_lm(d) / (A_0 Y_00) */
            float band = k < 4 ? 2.0f / 3.0f : 0.25f;
            for (c = 0; c < 3; c ++) {
                float expected = sh[i][0][c] * band * y[k] / y[0];
                float error = fabsf(sh[i][k][c] - expected);
                if (error > 0.03f * sh[i][0][c] + 1e-5f) {
                    printf("probe %d coefficient %d channel %d: %f, expected %f\n", i, k, c,
                        (double)sh[i][k][c], (double)expected);
                }

                test_assert(error <= 0.03f * sh[i][0][c] + 1e-5f);
            }
        }

        /* What reads back is already a ZH3 expansion: packing it again changes nothing */
        zh_expected(sh[i], again);
        test_assert(zh_difference(sh[i], again) <= 1e-5f * sh[i][0][1] + 1e-7f);
        checked ++;
    }

    test_assert(checked > 0);
    ecs_os_free(sh);
    ecs_fini(world);
}

/* Two equal baked lights on opposite sides of a probe cancel its band 1, so its axis falls back
 * to +z, and no probe holds a NaN. */
void ProbeZh3_opposite_lights_no_nan(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = zh_world();
    ecs_entity_t scene = zh_scene(world, true, 0);
    zh_frame(world);

    const box_bake_volume_t *v = box_bake_find(box_bake_state(world), scene);
    test_assert(v != NULL);
    test_assert(v->probes > 0);
    int32_t i, k, c, top = -1;
    float height = -1e9f;
    vec3 center;
    for (i = 0; i < v->probes; i ++) {
        vec3 pos;
        box_bake_probe_position(v, (uint32_t)i, pos);
        if (box_bake_valid(v, (uint32_t)i) && pos[1] > height + 1e-4f) {
            height = pos[1];
            top = i;
        }
    }

    test_assert(top >= 0);
    box_bake_probe_position(v, (uint32_t)top, center);
    test_assert(center[1] > 0.1f);
    for (i = 0; i < 2; i ++) {
        ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, lamp, Position, {center[0] + (i ? 3.0f : -3.0f), center[1], center[2]});
        ecs_set(world, lamp, PointLight, {.intensity = 20, .range = 100,
            .bake = LightBakeModeBaked});
    }

    zh_converge(world, scene);
    v = zh_volume(world, scene);
    float sh[BOX_RENDER_SH_COEFFICIENTS][4], probe[BOX_RENDER_SH_COEFFICIENTS][4];
    for (i = 0; i < v->probes; i ++) {
        test_assert(box_render_gpu_read_volume_sh(world, v->handle, (uint32_t)v->display,
            (uint32_t)i, sh));
        for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
            for (c = 0; c < 4; c ++) {
                test_assert(isfinite(sh[k][c]));
            }
        }

        if (i == top) {
            ecs_os_memcpy(probe, sh, ECS_SIZEOF(sh));
        }
    }

    test_assert(probe[0][1] > 0);
    for (k = 1; k < 4; k ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(probe[k][c]) <= 0.05f * probe[0][c]);
        }
    }

    ecs_fini(world);
}

/* The probe buffers of a volume hold 4 vec4, 64 bytes, per probe. */
void ProbeZh3_probe_buffer_64_bytes(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = zh_world();
    ecs_entity_t scene = zh_scene(world, false, 0);
    int32_t frame;
    for (frame = 0; frame < 5; frame ++) {
        zh_frame(world);
    }

    const box_bake_volume_t *v = zh_volume(world, scene);
    uint64_t bytes = 0;
    test_assert(box_render_gpu_volume_bytes(world, v->handle, &bytes));
    printf("probe buffer: %d probes, %llu bytes\n", v->probes, (unsigned long long)bytes);
    test_int(BOX_RENDER_ZH3_VECTORS * 4 * (int32_t)sizeof(float), 64);
    test_assert(bytes == (uint64_t)v->probes * 64u);

    ecs_fini(world);
}

/* A volume filled from the CPU with the SH of the hemisphere ambient reads back as the ZH3
 * expansion of that SH, bands 0 and 1 unchanged. */
void ProbeZh3_uploaded_sh_expands(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = zh_world();
    ecs_entity_t scene = zh_scene(world, false, 0);
    ecs_set(world, scene, AmbientLight, {.sky = {120, 160, 255}, .ground = {200, 90, 40},
        .intensity = 2});
    int32_t frame, i, k, c;
    for (frame = 0; frame < 5; frame ++) {
        zh_frame(world);
    }

    const box_bake_volume_t *v = zh_volume(world, scene);
    float expected[BOX_RENDER_SH_COEFFICIENTS][4], sh[BOX_RENDER_SH_COEFFICIENTS][4];
    test_assert(v->sh[0][2] > 0);
    zh_expected(v->sh, expected);
    for (i = 0; i < v->probes; i ++) {
        test_assert(box_render_gpu_read_volume_sh(world, v->handle, (uint32_t)v->display,
            (uint32_t)i, sh));
        for (k = 0; k < 4; k ++) {
            for (c = 0; c < 3; c ++) {
                test_flt(sh[k][c], v->sh[k][c]);
            }
        }

        test_assert(zh_difference(sh, expected) <= 1e-6f * v->sh[0][2] + 1e-7f);
    }

    ecs_fini(world);
}

/* A volume filled from a processed environment reads back as the ZH3 expansion of the L2 SH of
 * that environment. */
void ProbeZh3_environment_fill_expands(void) {
    // environment/bake
    ecs_world_t *world = zh_world();
    ecs_entity_t sky = ecs_new(world);
    ecs_set_ptr(world, sky, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_add(world, sky, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, sky);
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(40), glm_rad(30), 0});
    ecs_get_mut(world, sky, Atmosphere)->sun = sun;
    ecs_modified(world, sky, Atmosphere);
    ecs_entity_t scene = zh_scene(world, false, sky);

    float env[BOX_RENDER_SH_COEFFICIENTS][4], expected[BOX_RENDER_SH_COEFFICIENTS][4];
    float sh[BOX_RENDER_SH_COEFFICIENTS][4];
    float error = 1e9f, scale = 0;
    int32_t frame;
    for (frame = 0; frame < WAIT_FRAMES; frame ++) {
        zh_frame(world);
        const box_bake_volume_t *v = box_bake_find(box_bake_state(world), scene);
        uint32_t handle = box_render_environment_find(world, sky);
        box_render_environment_state_t state;
        if (!v || !v->handle || !v->probes || !v->filled_env || !handle ||
            !box_render_gpu_environment_state(world, handle, &state) || !state.processed)
        {
            continue;
        }

        if (!box_render_gpu_read_environment_sh(world, handle, env) ||
            !box_render_gpu_read_volume_sh(world, v->handle, (uint32_t)v->display, 0, sh))
        {
            continue;
        }

        zh_expected(env, expected);
        scale = env[0][2];
        error = zh_difference(sh, expected);
        if (scale > 0 && error <= 1e-4f * scale) {
            break;
        }
    }

    printf("environment fill: %f from the expansion, scale %f\n", (double)error,
        (double)scale);
    test_assert(scale > 0);
    test_assert(error <= 1e-4f * scale);

    ecs_fini(world);
}
