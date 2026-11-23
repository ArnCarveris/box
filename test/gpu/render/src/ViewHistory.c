/**
 * @file ViewHistory.c
 * @brief Tests for the temporal foundation of the GPU backend: the history of a view that the
 * View uniforms carry (prev_view_proj and temporal), its resets, the history texture pairs of
 * the temporal passes and the history of Bloom, their first consumer.
 *
 * Most cases render a view of a camera 5 units in front of the origin into a 64x64 capture.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** History pairs a case reads at most */
#define HISTORY_PAIRS (8)

/** Create a world that renders headless, with a camera 5 units in front of the origin.
 *
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* history_world(
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 0, 5});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});

    return world;
}

/** Create an unlit view with a black clear, with one effect or none.
 *
 * @param world The world
 * @param camera The camera
 * @param effect The effect entity, 0 for none
 * @return The view
 */
static ecs_entity_t history_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t effect)
{
    ecs_entity_t result = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    if (effect) {
        *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
            .enabled = true, .effect = effect };
    }

    ecs_set_ptr(world, result, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);

    return result;
}

/** Create the surface the views render into.
 *
 * @param world The world
 * @param width Width in pixels
 * @param height Height in pixels
 * @return The surface
 */
static ecs_entity_t history_surface(
    ecs_world_t *world,
    int32_t width,
    int32_t height)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Surface, {.width = width, .height = height,
        .write_to_file = "history.png", .resolution_scale = 1});

    return result;
}

/** Create a Bloom that keeps most of the previous frame.
 *
 * @param world The world
 * @param temporal Blend with the previous frame
 * @return The effect
 */
static ecs_entity_t history_bloom(
    ecs_world_t *world,
    float temporal)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Bloom, {.intensity = 0.5f, .threshold = 1, .temporal = temporal,
        .high_pass_frequency = 1, .max_mip_dimension = 512});

    return result;
}

/** Render a frame and wait for it.
 *
 * @param world The world
 */
static void history_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
}

/** Get the history of a view after the last frame.
 *
 * @param world The world
 * @param view The view
 * @return The history
 */
static box_render_history_t history_of(
    ecs_world_t *world,
    ecs_entity_t view)
{
    box_render_history_t result;
    test_assert(box_render_gpu_view_history(world, view, &result));

    return result;
}

/** Get the history pair of the consumer entity of a view.
 *
 * @param world The world
 * @param entity The consumer entity, such as a Bloom
 * @param out Receives the pair
 * @return Whether the pair exists
 */
static bool history_pair(
    ecs_world_t *world,
    ecs_entity_t entity,
    box_render_gpu_history_pair_t *out)
{
    box_render_gpu_history_pair_t pairs[HISTORY_PAIRS];
    int32_t i, count = box_render_gpu_history_pairs(world, pairs, HISTORY_PAIRS);
    bool result = false;
    for (i = 0; i < count && i < HISTORY_PAIRS && !result; i ++) {
        if (pairs[i].entity == entity) {
            *out = pairs[i];
            result = true;
        }
    }

    return result;
}

/** Check that two matrices are equal.
 *
 * @param a The first matrix
 * @param b The second matrix
 */
static void history_test_mat4(
    const mat4 a,
    const mat4 b)
{
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 4; j ++) {
            test_flt(a[i][j], b[i][j]);
        }
    }
}

/** Check that the history of a view was reset in the last frame and is valid in the next.
 *
 * @param world The world
 * @param view The view
 */
static void history_test_reset(
    ecs_world_t *world,
    ecs_entity_t view)
{
    history_frame(world);
    box_render_history_t h = history_of(world, view);
    test_bool(h.current.valid, false);
    test_uint(h.current.frame_index, 0);
    history_test_mat4(h.current.prev_view_proj, h.view_proj);
    history_frame(world);
    h = history_of(world, view);
    test_bool(h.current.valid, true);
    test_uint(h.current.frame_index, 1);
}

void ViewHistory_first_frame_invalid(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    history_frame(world);
    box_render_history_t h = history_of(world, view);
    test_bool(h.current.valid, false);
    test_uint(h.current.frame_index, 0);
    test_int(h.width, 64);
    test_int(h.height, 64);
    test_uint(h.camera, camera);
    history_test_mat4(h.current.prev_view_proj, h.view_proj);
    ecs_fini(world);
}

void ViewHistory_second_frame_has_previous_projection(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    history_frame(world);
    box_render_history_t first = history_of(world, view);
    ecs_set(world, camera, Position, {0.5f, 0, 5});
    history_frame(world);
    box_render_history_t second = history_of(world, view);
    test_bool(second.current.valid, true);
    test_uint(second.current.frame_index, 1);
    test_int(second.current.prev_frame, first.frame);
    history_test_mat4(second.current.prev_view_proj, first.view_proj);
    test_assert(memcmp(second.view_proj, first.view_proj, sizeof(mat4)) != 0);
    test_flt(second.current.prev_camera[0], 0);
    test_flt(second.camera_position[0], 0.5f);
    ecs_fini(world);
}

void ViewHistory_frame_index_counts_frames(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    uint32_t i;
    for (i = 0; i < 10; i ++) {
        history_frame(world);
        box_render_history_t h = history_of(world, view);
        test_uint(h.current.frame_index, i);
        test_bool(h.current.valid, i > 0);
    }

    ecs_fini(world);
}

void ViewHistory_moving_camera_keeps_history(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    history_frame(world);
    int32_t i;
    for (i = 1; i < 5; i ++) {
        ecs_set(world, camera, Position, {(float)i, 0, 5});
        ecs_set(world, camera, Rotation, {0, 0.2f * (float)i, 0});
        history_frame(world);
        box_render_history_t h = history_of(world, view);
        test_bool(h.current.valid, true);
        test_uint(h.current.frame_index, (uint32_t)i);
    }

    ecs_fini(world);
}

void ViewHistory_resize_resets(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    ecs_entity_t s = history_surface(world, 64, 64);
    history_frame(world);
    history_frame(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 64, .write_to_file = "history.png",
        .resolution_scale = 1});
    box_render_history_t h = {0};
    int32_t i;
    for (i = 0; i < 3 && h.width != 96; i ++) {
        history_frame(world);
        h = history_of(world, view);
        test_bool(h.current.valid, h.width != 96);
    }

    test_int(h.width, 96);
    test_uint(h.current.frame_index, 0);
    history_frame(world);
    h = history_of(world, view);
    test_bool(h.current.valid, true);
    test_uint(h.current.frame_index, 1);
    ecs_fini(world);
}

void ViewHistory_camera_entity_resets(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    history_frame(world);
    history_frame(world);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {0, 0, 5});
    ecs_set(world, other, Camera, {.fov = 60, .near_ = 0.1f});
    View *v = ecs_get_mut(world, view, View);
    v->camera = other;
    ecs_modified(world, view, View);
    history_test_reset(world, view);
    test_uint(history_of(world, view).camera, other);
    ecs_fini(world);
}

void ViewHistory_projection_change_resets(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    history_frame(world);
    history_frame(world);
    ecs_set(world, camera, Camera, {.fov = 45, .near_ = 0.1f});
    history_test_reset(world, view);
    ecs_set(world, camera, Camera, {.fov = 45, .near_ = 0.1f, .far_ = 50});
    history_test_reset(world, view);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 10, .near_ = 0.1f,
        .far_ = 50});
    history_test_reset(world, view);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 20, .near_ = 0.1f,
        .far_ = 50});
    history_test_reset(world, view);
    ecs_fini(world);
}

void ViewHistory_history_reset_resets(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t view = history_view(world, camera, 0);
    history_surface(world, 64, 64);
    history_frame(world);
    history_frame(world);
    history_frame(world);
    test_uint(history_of(world, view).current.frame_index, 2);
    ecs_add(world, view, HistoryReset);
    history_test_reset(world, view);
    test_bool(ecs_has(world, view, HistoryReset), false);
    ecs_fini(world);
}

void ViewHistory_two_views_independent(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t a = history_view(world, camera, 0);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {0, 2, 5});
    ecs_set(world, other, Camera, {.fov = 30, .near_ = 0.1f});
    ecs_entity_t b = history_view(world, other, 0);
    View *v = ecs_get_mut(world, b, View);
    v->order = 1;
    v->clear.enabled = false;
    ecs_modified(world, b, View);
    history_surface(world, 64, 64);
    history_frame(world);
    history_frame(world);
    box_render_history_t ha = history_of(world, a), hb = history_of(world, b);
    test_assert(memcmp(ha.view_proj, hb.view_proj, sizeof(mat4)) != 0);
    ecs_add(world, b, HistoryReset);
    history_frame(world);
    ha = history_of(world, a);
    hb = history_of(world, b);
    test_bool(ha.current.valid, true);
    test_uint(ha.current.frame_index, 2);
    test_bool(hb.current.valid, false);
    test_uint(hb.current.frame_index, 0);
    test_bool(ecs_has(world, a, HistoryReset), false);
    test_bool(ecs_has(world, b, HistoryReset), false);
    ecs_fini(world);
}

void ViewHistory_pair_swaps_every_frame(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t bloom = history_bloom(world, 0.9f);
    ecs_entity_t view = history_view(world, camera, bloom);
    history_surface(world, 64, 64);
    box_render_gpu_history_pair_t prev, cur;
    history_frame(world);
    test_assert(history_pair(world, bloom, &prev));
    test_uint(prev.view, view);
    test_bool(prev.valid, false);
    test_assert(prev.read != prev.write);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        history_frame(world);
        test_assert(history_pair(world, bloom, &cur));
        test_assert(cur.read == prev.write);
        test_assert(cur.write == prev.read);
        test_bool(cur.valid, true);
        test_int(cur.frame, prev.frame + 1);
        prev = cur;
    }

    test_int(box_render_gpu_history_pairs(world, NULL, 0), 1);
    ecs_fini(world);
}

void ViewHistory_pair_invalid_after_resize(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t bloom = history_bloom(world, 0.9f);
    history_view(world, camera, bloom);
    ecs_entity_t s = history_surface(world, 64, 64);
    box_render_gpu_history_pair_t before, after = {0};
    history_frame(world);
    history_frame(world);
    test_assert(history_pair(world, bloom, &before));
    test_bool(before.valid, true);
    ecs_set(world, s, Surface, {.width = 128, .height = 128, .write_to_file = "history.png",
        .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 3 && (i == 0 || after.width == before.width); i ++) {
        history_frame(world);
        test_assert(history_pair(world, bloom, &after));
        test_bool(after.valid, after.width == before.width);
    }

    test_assert(after.width > before.width);
    test_assert(after.height > before.height);
    history_frame(world);
    test_assert(history_pair(world, bloom, &after));
    test_bool(after.valid, true);
    ecs_fini(world);
}

void ViewHistory_pair_freed_when_unused(void) {
    // renderer/views
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t bloom = history_bloom(world, 0.9f);
    history_view(world, camera, bloom);
    history_surface(world, 64, 64);
    box_render_gpu_history_pair_t pair;
    history_frame(world);
    history_frame(world);
    test_int(box_render_gpu_history_pairs(world, NULL, 0), 1);
    Bloom *b = ecs_get_mut(world, bloom, Bloom);
    b->temporal = 0;
    ecs_modified(world, bloom, Bloom);
    history_frame(world);
    test_assert(history_pair(world, bloom, &pair));
    int32_t i;
    for (i = 0; i < 8; i ++) {
        history_frame(world);
    }

    test_int(box_render_gpu_history_pairs(world, NULL, 0), 0);
    b = ecs_get_mut(world, bloom, Bloom);
    b->temporal = 0.9f;
    ecs_modified(world, bloom, Bloom);
    history_frame(world);
    test_assert(history_pair(world, bloom, &pair));
    test_bool(pair.valid, false);
    ecs_fini(world);
}

/** Render an emissive cube with a temporal Bloom for 30 frames, then move the cube out of view,
 * with or without a HistoryReset of the view in that frame, and read the glow beside where the
 * cube was.
 *
 * @param reset Whether the view resets in the last frame
 * @return Red of the pixel beside the cube in the last frame
 */
static unsigned char history_bloom_glow(
    bool reset)
{
    ecs_entity_t camera;
    ecs_world_t *world = history_world(&camera);
    ecs_entity_t bloom = history_bloom(world, 0.9f);
    ecs_entity_t view = history_view(world, camera, bloom);
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Rgb, {0, 0, 0});
    ecs_set(world, cube, Emissive, {.strength = 8, .color = {255, 255, 255}});
    history_surface(world, 64, 64);
    int32_t frame;
    for (frame = 0; frame < 30; frame ++) {
        ecs_progress(world, 1.0f / 60);
    }

    ecs_set(world, cube, Position, {0, 100, 0});
    if (reset) {
        ecs_add(world, view, HistoryReset);
    }

    history_frame(world);
    char *path = flecs_asprintf("%s/history.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char result = px[(32 * 64 + 40) * 4];
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);

    return result;
}

void ViewHistory_bloom_reset_clears_temporal(void) {
    // lighting/effects
    unsigned char kept = history_bloom_glow(false);
    unsigned char reset = history_bloom_glow(true);
    test_assert(reset < 4);
    test_assert(kept > reset + 8);
}
