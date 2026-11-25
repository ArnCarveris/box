/**
 * @file ViewHistory.c
 * @brief Tests for the history of a view (temporal.c): the previous view projection and frame
 * index, the resets on a first frame, resize, new camera entity, projection change and
 * HistoryReset, and the low discrepancy sequences of the frame index.
 *
 * The cases render into the null backend and advance a history of their own with the views of
 * the packets, as the GPU backend does with the views of its targets.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "render.h"

/** Handle of the target the cases advance the histories for */
#define HISTORY_TARGET (1)

/** Create a world that renders into the null backend, with a camera 5 units in front of the
 * origin and a view of it.
 *
 * @param null Receives the backend
 * @param camera Receives the camera
 * @param view Receives the view
 * @return The world
 */
static ecs_world_t* history_world(
    box_render_null_t **null,
    ecs_entity_t *camera,
    ecs_entity_t *view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0, 0, 5});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});
    *view = ecs_new(world);
    ecs_set(world, *view, View, {.camera = *camera});

    return world;
}

/** Release a world and its backend.
 *
 * @param world The world
 * @param null The backend
 * @param histories The histories of the case
 */
static void history_fini(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_vec_t *histories)
{
    ecs_vec_fini_t(NULL, histories, box_render_history_t);
    ecs_fini(world);
    box_render_null_free(null);
}

/** Find a view in the last packet of the null backend.
 *
 * @param null The backend
 * @param view The view entity
 * @return The view, NULL when the packet does not have it
 */
static const box_render_view_t* history_packet_view(
    const box_render_null_t *null,
    ecs_entity_t view)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.views) && !result; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->view == view) {
            result = v;
        }
    }

    return result;
}

/** Advance the history of a view with the last packet of the null backend.
 *
 * @param null The backend
 * @param histories The histories of the case
 * @param view The view entity
 * @param width Render width
 * @param height Render height
 * @return What the view renders the frame with
 */
static box_render_temporal_t history_advance(
    const box_render_null_t *null,
    ecs_vec_t *histories,
    ecs_entity_t view,
    int32_t width,
    int32_t height)
{
    const box_render_view_t *v = history_packet_view(null, view);
    mat4 proj;
    test_assert(v != NULL);
    box_camera_projection(&v->camera_data, (float)width, (float)height, proj);

    return *box_render_history_update(histories, HISTORY_TARGET, v, null->last.origin,
        null->last.frame, width, height, proj);
}

/** Run a frame into the null backend and advance the history of a view of 64x64 pixels.
 *
 * @param world The world
 * @param null The backend
 * @param histories The histories of the case
 * @param view The view entity
 * @return What the view renders the frame with
 */
static box_render_temporal_t history_frame(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_vec_t *histories,
    ecs_entity_t view)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    return history_advance(null, histories, view, 64, 64);
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

void ViewHistory_first_frame_invalid(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    box_render_temporal_t t = history_frame(world, null, &histories, view);
    test_bool(t.valid, false);
    test_uint(t.frame_index, 0);
    test_int(t.prev_frame, -1);
    const box_render_history_t *h = box_render_history_find(&histories, HISTORY_TARGET, view);
    test_assert(h != NULL);
    history_test_mat4(t.prev_view_proj, h->view_proj);
    history_fini(world, null, &histories);
}

void ViewHistory_second_frame_valid(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    mat4 first;
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    const box_render_history_t *h = box_render_history_find(&histories, HISTORY_TARGET, view);
    int64_t frame = h->frame;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, h->view_proj), first);
    ecs_set(world, camera, Position, {1, 0, 5});
    box_render_temporal_t t = history_frame(world, null, &histories, view);
    test_bool(t.valid, true);
    test_uint(t.frame_index, 1);
    test_int(t.prev_frame, frame);
    history_test_mat4(t.prev_view_proj, first);
    test_flt(t.prev_camera[0], 0);
    test_flt(t.prev_camera[2], 5);
    h = box_render_history_find(&histories, HISTORY_TARGET, view);
    test_assert(memcmp(h->view_proj, first, sizeof(mat4)) != 0);
    history_fini(world, null, &histories);
}

void ViewHistory_frame_index_counts_frames(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    uint32_t i;
    for (i = 0; i < 20; i ++) {
        box_render_temporal_t t = history_frame(world, null, &histories, view);
        test_uint(t.frame_index, i);
        test_bool(t.valid, i > 0);
    }

    history_fini(world, null, &histories);
}

void ViewHistory_same_frame_twice(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    box_render_temporal_t a = history_frame(world, null, &histories, view);
    box_render_temporal_t b = history_advance(null, &histories, view, 64, 64);
    test_uint(a.frame_index, 1);
    test_uint(b.frame_index, 1);
    test_bool(b.valid, true);
    test_assert(!memcmp(a.prev_view_proj, b.prev_view_proj, sizeof(mat4)));
    history_fini(world, null, &histories);
}

void ViewHistory_moving_camera_keeps_history(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    int32_t i;
    for (i = 1; i < 5; i ++) {
        ecs_set(world, camera, Position, {(float)i, 0, 5});
        ecs_set(world, camera, Rotation, {0, 0.1f * (float)i, 0});
        box_render_temporal_t t = history_frame(world, null, &histories, view);
        test_bool(t.valid, true);
        test_uint(t.frame_index, (uint32_t)i);
    }

    history_fini(world, null, &histories);
}

void ViewHistory_resize_resets(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    history_frame(world, null, &histories, view);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_temporal_t t = history_advance(null, &histories, view, 128, 64);
    test_bool(t.valid, false);
    test_uint(t.frame_index, 0);
    t = history_advance(null, &histories, view, 128, 64);
    test_bool(t.valid, false);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    t = history_advance(null, &histories, view, 128, 64);
    test_bool(t.valid, true);
    test_uint(t.frame_index, 1);
    history_fini(world, null, &histories);
}

void ViewHistory_camera_entity_resets(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    history_frame(world, null, &histories, view);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {0, 0, 5});
    ecs_set(world, other, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_set(world, view, View, {.camera = other});
    box_render_temporal_t t = history_frame(world, null, &histories, view);
    test_bool(t.valid, false);
    test_uint(t.frame_index, 0);
    t = history_frame(world, null, &histories, view);
    test_bool(t.valid, true);
    test_uint(t.frame_index, 1);
    history_fini(world, null, &histories);
}

void ViewHistory_projection_change_resets(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    Camera changes[4] = {
        {.fov = 50, .near_ = 0.1f},
        {.fov = 50, .near_ = 0.2f},
        {.fov = 50, .near_ = 0.2f, .far_ = 100},
        {.ortho = true, .ortho_scale = 10, .near_ = 0.2f, .far_ = 100}
    };
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    history_frame(world, null, &histories, view);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_set_ptr(world, camera, Camera, &changes[i]);
        box_render_temporal_t t = history_frame(world, null, &histories, view);
        test_bool(t.valid, false);
        test_uint(t.frame_index, 0);
        t = history_frame(world, null, &histories, view);
        test_bool(t.valid, true);
        test_uint(t.frame_index, 1);
    }

    changes[3].ortho_scale = 20;
    ecs_set_ptr(world, camera, Camera, &changes[3]);
    box_render_temporal_t t = history_frame(world, null, &histories, view);
    test_bool(t.valid, false);
    history_fini(world, null, &histories);
}

void ViewHistory_history_reset_resets(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    history_frame(world, null, &histories, view);
    test_bool(history_packet_view(null, view)->history_reset, false);
    ecs_add(world, view, HistoryReset);
    box_render_temporal_t t = history_frame(world, null, &histories, view);
    test_bool(history_packet_view(null, view)->history_reset, true);
    test_bool(ecs_has(world, view, HistoryReset), false);
    test_bool(t.valid, false);
    test_uint(t.frame_index, 0);
    t = history_frame(world, null, &histories, view);
    test_bool(history_packet_view(null, view)->history_reset, false);
    test_bool(t.valid, true);
    test_uint(t.frame_index, 1);
    history_fini(world, null, &histories);
}

void ViewHistory_history_reset_from_script(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    ecs_set_name(world, view, "main_view");
    history_frame(world, null, &histories, view);
    history_frame(world, null, &histories, view);
    test_assert(ecs_script_run(world, "reset", "using box.view\nmain_view { HistoryReset }",
        NULL) == 0);
    test_bool(ecs_has(world, view, HistoryReset), true);
    box_render_temporal_t t = history_frame(world, null, &histories, view);
    test_bool(t.valid, false);
    test_bool(ecs_has(world, view, HistoryReset), false);
    history_fini(world, null, &histories);
}

void ViewHistory_two_views_independent(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, View, {.camera = camera, .order = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    history_advance(null, &histories, view, 64, 64);
    history_advance(null, &histories, other, 64, 64);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    history_advance(null, &histories, view, 64, 64);
    history_advance(null, &histories, other, 64, 64);
    ecs_add(world, other, HistoryReset);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_temporal_t a = history_advance(null, &histories, view, 64, 64);
    box_render_temporal_t b = history_advance(null, &histories, other, 64, 64);
    test_bool(a.valid, true);
    test_uint(a.frame_index, 2);
    test_bool(b.valid, false);
    test_uint(b.frame_index, 0);
    test_int(ecs_vec_count(&histories), 2);
    history_fini(world, null, &histories);
}

void ViewHistory_sweep_drops_unrendered_view(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera, view;
    ecs_vec_t histories = {0};
    ecs_world_t *world = history_world(&null, &camera, &view);
    history_frame(world, null, &histories, view);
    history_frame(world, null, &histories, view);
    box_render_history_sweep(&histories, null->last.frame);
    test_int(ecs_vec_count(&histories), 1);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_history_sweep(&histories, null->last.frame);
    test_int(ecs_vec_count(&histories), 0);
    box_render_temporal_t t = history_advance(null, &histories, view, 64, 64);
    test_bool(t.valid, false);
    test_uint(t.frame_index, 0);
    history_fini(world, null, &histories);
}

void ViewHistory_sequence_2d_deterministic_in_range(void) {
    // renderer/views
    uint32_t i, j;
    float a[2], b[2], points[BOX_RENDER_TEMPORAL_LENGTH][2];
    for (i = 0; i < BOX_RENDER_TEMPORAL_LENGTH; i ++) {
        box_render_temporal_sequence_2d(i, a);
        box_render_temporal_sequence_2d(i, b);
        test_assert(!memcmp(a, b, sizeof(a)));
        test_assert(a[0] >= 0.0f && a[0] < 1.0f);
        test_assert(a[1] >= 0.0f && a[1] < 1.0f);
        box_render_temporal_sequence_2d(i + BOX_RENDER_TEMPORAL_LENGTH, b);
        test_assert(!memcmp(a, b, sizeof(a)));
        points[i][0] = a[0];
        points[i][1] = a[1];
    }

    for (i = 0; i < BOX_RENDER_TEMPORAL_LENGTH; i ++) {
        for (j = i + 1; j < BOX_RENDER_TEMPORAL_LENGTH; j ++) {
            float dx = points[i][0] - points[j][0], dy = points[i][1] - points[j][1];
            test_assert(dx * dx + dy * dy > 1e-4f);
        }
    }

    box_render_temporal_sequence_2d(0, a);
    test_flt(a[0], 0.5f);
    test_flt(a[1], 0.5f);
}

void ViewHistory_sequence_3d_deterministic_in_range(void) {
    // renderer/views
    uint32_t i, j;
    float a[3], b[3], points[BOX_RENDER_TEMPORAL_LENGTH][3];
    for (i = 0; i < BOX_RENDER_TEMPORAL_LENGTH; i ++) {
        box_render_temporal_sequence_3d(i, a);
        box_render_temporal_sequence_3d(i, b);
        test_assert(!memcmp(a, b, sizeof(a)));
        for (j = 0; j < 3; j ++) {
            test_assert(a[j] > 0.0f && a[j] < 1.0f);
            points[i][j] = a[j];
        }

        box_render_temporal_sequence_3d(i + 3 * BOX_RENDER_TEMPORAL_LENGTH, b);
        test_assert(!memcmp(a, b, sizeof(a)));
    }

    for (i = 0; i < BOX_RENDER_TEMPORAL_LENGTH; i ++) {
        for (j = i + 1; j < BOX_RENDER_TEMPORAL_LENGTH; j ++) {
            float dx = points[i][0] - points[j][0], dy = points[i][1] - points[j][1];
            float dz = points[i][2] - points[j][2];
            test_assert(dx * dx + dy * dy + dz * dz > 1e-4f);
        }
    }

    box_render_temporal_sequence_3d(0, a);
    test_flt(a[0], 0.5f);
    test_flt(a[1], 1.0f / 3.0f);
    test_flt(a[2], 0.2f);
}

void ViewHistory_sequence_2d_covers_square(void) {
    // renderer/views
    int32_t quadrants[4] = {0};
    float a[2];
    uint32_t i;
    for (i = 0; i < BOX_RENDER_TEMPORAL_LENGTH; i ++) {
        box_render_temporal_sequence_2d(i, a);
        quadrants[(a[0] >= 0.5f ? 1 : 0) + (a[1] >= 0.5f ? 2 : 0)] ++;
    }

    for (i = 0; i < 4; i ++) {
        test_assert(quadrants[i] >= 2);
        test_assert(quadrants[i] <= 6);
    }
}
