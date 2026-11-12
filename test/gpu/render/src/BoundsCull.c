/**
 * @file BoundsCull.c
 * @brief Tests for culling whole batches by their world bounds (CullSettings.bounds): the
 * batches a view or a cascade skips, the counters of the skips and the images it renders.
 *
 * Boxes share one mesh and so one batch, a sphere is a batch of its own. The orthographic cases
 * look down -z from z = 10 with ortho_height 10 into a 64x64 image, so world x and y from -5 to
 * 5 fill the image and a unit is 6.4 pixels. The shapes are unlit, so white shows white.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Create a world that renders on the main thread without a window */
static ecs_world_t* bounds_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    return world;
}

/** Create the orthographic camera and its view */
static ecs_entity_t bounds_ortho_view(
    ecs_world_t *world,
    ecs_entity_t *camera_out,
    bool bounds,
    bool occlusion)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = occlusion, .min_dimension_px = 1,
            .bounds = bounds},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    if (camera_out) {
        *camera_out = camera;
    }

    return view;
}

/** Create a white box */
static ecs_entity_t bounds_box(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {255, 255, 255});
    return e;
}

/** Create a white sphere, a batch apart from the boxes */
static ecs_entity_t bounds_sphere(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set(world, e, Sphere, {.segments = 16, .radius = 0.5f});
    ecs_set(world, e, Rgb, {255, 255, 255});
    return e;
}

/** Load a PNG the surface wrote into the temporary folder */
static unsigned char* bounds_load(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, 64);
    test_int(h, 64);
    ecs_os_free(path);
    return result;
}

/** Run frames until the CullStatus of a view has a number of skipped batches */
static const CullStatus* bounds_status(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t skipped)
{
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 50; i ++) {
        const CullStatus *st = ecs_get(world, view, CullStatus);
        if (st && st->batches_skipped == skipped && st->instances) {
            break;
        }

        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    return ecs_get(world, view, CullStatus);
}

void BoundsCull_outside_batch_skipped(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, false);
    bounds_box(world, -2, 0, 0);
    bounds_box(world, 2, 0, 0);
    bounds_sphere(world, 20, 0, 0);
    bounds_sphere(world, 30, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "outside.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.tested, 4);
    test_int(stats.visible, 2);

    unsigned char *px = bounds_load(world, "outside.png");
    test_assert(px[(32 * 64 + 19) * 4] > 200);
    test_assert(px[(32 * 64 + 44) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4] < 20);
    stbi_image_free(px);
    ecs_fini(world);
}

void BoundsCull_outside_batch_skipped_with_occlusion(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, true);
    bounds_box(world, -2, 0, 0);
    bounds_sphere(world, 20, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.tested, 2);
    test_int(stats.visible, 1);
    ecs_fini(world);
}

void BoundsCull_partly_inside_drawn(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, false);
    bounds_box(world, -2, 0, 0);
    bounds_sphere(world, 2, 0, 0);
    bounds_sphere(world, 40, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "partly.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.tested, 3);
    test_int(stats.visible, 2);

    unsigned char *px = bounds_load(world, "partly.png");
    test_assert(px[(32 * 64 + 19) * 4] > 200);
    test_assert(px[(32 * 64 + 44) * 4] > 200);
    stbi_image_free(px);
    ecs_fini(world);
}

void BoundsCull_edge_batch_drawn(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, false);
    bounds_box(world, 0, 0, 0);
    bounds_sphere(world, 5.3f, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "edge.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.visible, 2);

    unsigned char *px = bounds_load(world, "edge.png");
    test_assert(px[(32 * 64 + 63) * 4] > 200);
    stbi_image_free(px);
    ecs_fini(world);
}

void BoundsCull_disabled_skips_nothing(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, false, false);
    bounds_box(world, -2, 0, 0);
    bounds_sphere(world, 20, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.casters_skipped, 0);
    test_int(stats.tested, 2);
    test_int(stats.visible, 1);

    const CullStatus *st = bounds_status(world, view, 0);
    test_assert(st != NULL);
    test_int(st->batches_skipped, 0);
    test_int(st->casters_skipped, 0);
    test_int(st->instances, 2);
    test_int(st->visible, 1);
    test_int(st->frustum_culled, 1);
    ecs_fini(world);
}

void BoundsCull_needs_enabled(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .culling = {.bounds = true}});
    bounds_box(world, -2, 0, 0);
    bounds_sphere(world, 20, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.tested, 2);
    test_int(stats.visible, 2);
    ecs_fini(world);
}

void BoundsCull_status_counts_skipped(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, false);
    bounds_box(world, -2, 0, 0);
    bounds_box(world, 2, 0, 0);
    bounds_sphere(world, 20, 0, 0);
    bounds_sphere(world, 30, 0, 0);
    bounds_sphere(world, 40, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const CullStatus *st = bounds_status(world, view, 1);
    test_assert(st != NULL);
    test_int(st->batches_skipped, 1);
    test_int(st->casters_skipped, 0);
    test_int(st->instances, 5);
    test_int(st->visible, 2);
    test_int(st->frustum_culled, 3);
    test_int(st->occlusion_culled, 0);
    ecs_fini(world);
}

void BoundsCull_status_counts_skipped_with_occlusion(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, true);
    bounds_box(world, -2, 0, 0);
    bounds_sphere(world, 20, 0, 0);
    bounds_sphere(world, 30, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const CullStatus *st = bounds_status(world, view, 1);
    test_assert(st != NULL);
    test_int(st->batches_skipped, 1);
    test_int(st->instances, 3);
    test_int(st->visible, 1);
    test_int(st->frustum_culled, 2);
    test_int(st->occlusion_culled, 0);
    ecs_fini(world);
}

void BoundsCull_min_dimension(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .min_dimension_px = 8, .bounds = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    bounds_box(world, -1, 0, -5);
    bounds_sphere(world, 4, 0, -20);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.tested, 2);
    test_int(stats.visible, 1);

    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .min_dimension_px = 1, .bounds = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.visible, 2);
    ecs_fini(world);
}

void BoundsCull_static_moves_in(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, true);
    bounds_box(world, -2, 0, 0);
    ecs_entity_t sphere = bounds_sphere(world, 20, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "moves_in.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.visible, 1);

    ecs_set(world, sphere, Position, {2, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.visible, 2);

    unsigned char *px = bounds_load(world, "moves_in.png");
    test_assert(px[(32 * 64 + 44) * 4] > 200);
    stbi_image_free(px);
    ecs_fini(world);
}

void BoundsCull_camera_moves_to_skipped(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t camera = 0;
    ecs_entity_t view = bounds_ortho_view(world, &camera, true, true);
    bounds_box(world, 0, 0, 0);
    bounds_sphere(world, 30, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "camera.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.visible, 1);

    ecs_set(world, camera, Position, {30, 0, 10});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.visible, 1);

    unsigned char *px = bounds_load(world, "camera.png");
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The sphere has a pipeline of its own (BackFacePlain). Its draws are skipped while it is out
 * of the view, yet the view keeps requesting its pipeline, so with the outcomes held as the
 * browser holds them it still draws in the frame it enters the view. */
void BoundsCull_skipped_keeps_pipelines(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t camera = 0;
    ecs_entity_t view = bounds_ortho_view(world, &camera, true, false);
    bounds_box(world, 0, 0, 0);
    ecs_entity_t sphere = bounds_sphere(world, 30, 0, 0);
    ecs_set(world, sphere, BackFaceMode, {BackFacePlain});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "pipelines.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);

    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_set(world, camera, Position, {30, 0, 10});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.visible, 1);

    unsigned char *px = bounds_load(world, "pipelines.png");
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    stbi_image_free(px);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_AT_ONCE);
    ecs_fini(world);
}

void BoundsCull_dynamic_follows(void) {
    // culling/bounds
    ecs_world_t *world = bounds_world();
    ecs_entity_t view = bounds_ortho_view(world, NULL, true, true);
    bounds_box(world, -2, 0, 0);
    ecs_entity_t mover = bounds_sphere(world, 2, 0, 0);
    ecs_add(world, mover, DynamicTransform);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "dynamic.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.visible, 2);

    ecs_set(world, mover, Position, {30, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 1);
    test_int(stats.visible, 1);

    ecs_set(world, mover, Position, {2, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.visible, 2);

    unsigned char *px = bounds_load(world, "dynamic.png");
    test_assert(px[(32 * 64 + 44) * 4] > 200);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Build the lit scene of the shadow cases: a floor and a caster under a camera that looks
 * down, a sphere far beside them and a sun, rendered once into a file */
static ecs_world_t* bounds_shadow_scene(
    bool bounds,
    const char *file,
    ecs_entity_t *view_out)
{
    ecs_world_t *world = bounds_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1,
            .bounds = bounds},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 5, -8});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t near = ecs_entity(world, {.name = "near"});
    ecs_set(world, near, Position, {3, 3, 3});
    ecs_set(world, near, Sphere, {.segments = 16, .radius = 1});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {500, 5, 0});
    ecs_set(world, far, Cylinder, {.segments = 16, .length = 1, .radius = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = ECS_CONST_CAST(char*, file),
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    *view_out = view;
    return world;
}

void BoundsCull_cascades_skip_outside(void) {
    // culling/bounds
    ecs_entity_t view = 0;
    ecs_world_t *world = bounds_shadow_scene(true, "cascades.png", &view);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 4);
    test_int(stats.visible, 2);
    test_int(stats.skipped, 1);
    test_assert(stats.casters_skipped >= 1);
    test_assert(stats.casters_visible > 0);

    box_render_cull_stats_t one;
    test_bool(box_render_gpu_cull_frustum(world, view, 0, &one), true);
    test_int(one.skipped, 1);
    test_assert(one.visible > 0);

    unsigned char *px = bounds_load(world, "cascades.png");
    unsigned char *under = &px[(15 * 64 + 32) * 4];
    unsigned char *open = &px[(48 * 64 + 32) * 4];
    test_assert(open[0] > 100);
    test_assert(under[0] < 30);
    stbi_image_free(px);

    const CullStatus *st = bounds_status(world, view, 1);
    test_assert(st != NULL);
    test_int(st->batches_skipped, 1);
    test_assert(st->casters_skipped >= 1);
    ecs_fini(world);
}

void BoundsCull_same_image(void) {
    // culling/bounds
    ecs_entity_t on_view = 0, off_view = 0;
    ecs_world_t *off = bounds_shadow_scene(false, "bounds_off.png", &off_view);
    unsigned char *a = bounds_load(off, "bounds_off.png");
    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(off, off_view, &stats), true);
    test_int(stats.skipped, 0);
    test_int(stats.casters_skipped, 0);
    ecs_fini(off);

    ecs_world_t *on = bounds_shadow_scene(true, "bounds_on.png", &on_view);
    unsigned char *b = bounds_load(on, "bounds_on.png");
    test_bool(box_render_gpu_cull_stats(on, on_view, &stats), true);
    test_int(stats.skipped, 1);
    ecs_fini(on);

    test_assert(!memcmp(a, b, 64 * 64 * 4));
    stbi_image_free(a);
    stbi_image_free(b);
}

/** Start a Position transition of the near sphere of the shadow scene towards a target far
 * outside the view and every cascade, then render the first frame of it. Linear over 100
 * seconds, the sphere has moved 1.24 units of the 497 and is still in the view. */
static ecs_world_t* bounds_transition_scene(
    bool bounds,
    const char *file,
    ecs_entity_t *view_out)
{
    ecs_world_t *world = bounds_shadow_scene(bounds, "transition_start.png", view_out);
    ecs_entity_t near = ecs_lookup(world, "near");
    box_transition_set(world, near, ecs_id(Position), &(TransitionSpec){ .duration = 100 });
    ecs_progress(world, 0.25f);
    box_render_sync(world);
    ecs_set(world, near, Position, {500, 3, 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0.25f);
    box_render_sync(world);
    return world;
}

/** A shape whose Position transitions out of the view keeps drawing while the resolver renders
 * it inside: its batch holds the target, so it sends no bounds while the transition runs. */
void BoundsCull_transition_out_drawn(void) {
    // culling/bounds
    ecs_entity_t on_view = 0, off_view = 0;
    box_render_cull_stats_t off_stats, stats;
    ecs_world_t *off = bounds_transition_scene(false, "transition_off.png", &off_view);
    unsigned char *a = bounds_load(off, "transition_off.png");
    test_bool(box_render_gpu_cull_stats(off, off_view, &off_stats), true);
    ecs_fini(off);

    ecs_world_t *on = bounds_transition_scene(true, "transition_on.png", &on_view);
    unsigned char *b = bounds_load(on, "transition_on.png");
    test_bool(box_render_gpu_cull_stats(on, on_view, &stats), true);
    test_flt(ecs_get(on, ecs_lookup(on, "near"), Position)->x, 500);
    test_int(stats.skipped, 1);
    test_int(stats.visible, off_stats.visible);
    test_int(stats.casters_visible, off_stats.casters_visible);
    test_assert(stats.casters_visible > 0);

    box_render_cull_stats_t one;
    test_bool(box_render_gpu_cull_frustum(on, on_view, 0, &one), true);
    test_int(one.skipped, 1);
    ecs_fini(on);

    test_assert(!memcmp(a, b, 64 * 64 * 4));
    stbi_image_free(a);
    stbi_image_free(b);
}
