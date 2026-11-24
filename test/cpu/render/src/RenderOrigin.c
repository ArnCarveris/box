/**
 * @file RenderOrigin.c
 * @brief Tests for the render origin (WorldOrigin.render): the packet carries it in f64, the
 * mover table is sent relative to it, a WorldCell root it follows is at exactly 0, and moving it
 * every frame costs a record per mover, whatever the descendants of the movers.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Create a world that renders with a null backend, with a camera at a position and a view.
 *
 * @param null Receives the backend
 * @param camera Receives the camera
 * @param x X of the camera
 * @return The world
 */
static ecs_world_t* ro_world(
    box_render_null_t **null,
    ecs_entity_t *camera,
    float x)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {x, 2, 10});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = *camera});

    return world;
}

/** Free a world and its backend.
 *
 * @param world The world
 * @param null The backend
 */
static void ro_fini(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_fini(world);
    box_render_null_free(null);
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void ro_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
}

/** Add a mover with a box child per count, laid out on a grid.
 *
 * @param world The world
 * @param x X of the mover
 * @param count Children
 * @return The mover
 */
static ecs_entity_t ro_mover(
    ecs_world_t *world,
    float x,
    int32_t count)
{
    ecs_entity_t mover = ecs_new(world);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {x, 0, 0});
    ecs_bulk_desc_t desc = {
        .count = count,
        .ids = { ecs_pair(EcsChildOf, mover), ecs_id(Position), ecs_id(Box) }
    };
    const ecs_entity_t *children = ecs_bulk_init(world, &desc);
    for (int32_t i = 0; i < count; i ++) {
        ecs_set(world, children[i], Position, {(float)(i % 100), 0, -(float)(i / 100)});
        ecs_set(world, children[i], Box, {0.5f, 0.5f, 0.5f});
    }

    return mover;
}

/** Get the mover table the last packet uploaded.
 *
 * @param null The backend
 * @param count Receives the records
 * @return The records, NULL when the packet uploaded none
 */
static const box_render_transform_t* ro_movers(
    const box_render_null_t *null,
    int32_t *count)
{
    const box_render_transform_t *result = NULL;
    *count = 0;
    for (int32_t i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_MOVERS) {
            result = box_render_packet_data(&null->last, u->data);
            *count = (int32_t)(u->size / sizeof(box_render_transform_t));
        }
    }

    return result;
}

void RenderOrigin_off_by_default(void) {
    // renderer/render_origin
    box_render_null_t *null;
    ecs_entity_t camera;
    ecs_world_t *world = ro_world(&null, &camera, 300);
    ecs_singleton_set(world, WorldOrigin, {.x = 1e6, .enabled = false});
    ro_mover(world, 310, 4);
    ro_frame(world);
    test_assert(null->last.local.origin[0] == 1e6);
    test_assert(null->last.local.scale == 1.0);
    test_assert(null->last.local.delta[0] == 0);
    test_assert(null->last.local.delta[1] == 0);
    test_assert(null->last.local.delta[2] == 0);

    int32_t count;
    const box_render_transform_t *m = ro_movers(null, &count);
    test_int(count, 2);
    test_flt(m[0].rows[0][3], 0);
    test_flt(m[1].rows[0][3], 310);
    ro_fini(world, null);
}

void RenderOrigin_follow_camera(void) {
    // renderer/render_origin
    box_render_null_t *null;
    ecs_entity_t camera;
    ecs_world_t *world = ro_world(&null, &camera, 300.25f);
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true});
    ro_mover(world, 310, 4);
    ro_frame(world);
    test_assert(null->last.local.delta[0] == 300.25);
    test_assert(null->last.local.delta[1] == 2);
    test_assert(null->last.local.delta[2] == 10);

    /* The identity holds minus the render origin, the mover its pose relative to it */
    int32_t count;
    const box_render_transform_t *m = ro_movers(null, &count);
    test_int(count, 2);
    test_flt(m[0].rows[0][0], 1);
    test_flt(m[0].rows[0][3], -300.25f);
    test_flt(m[0].rows[1][3], -2);
    test_flt(m[0].rows[2][3], -10);
    test_flt(m[1].rows[0][3], 9.75f);
    test_flt(m[1].rows[1][3], -2);
    test_flt(m[1].rows[2][3], -10);

    /* A camera that moves takes the render origin and the mover table with it */
    ecs_set(world, camera, Position, {301.25f, 2, 10});
    ro_frame(world);
    m = ro_movers(null, &count);
    test_int(count, 2);
    test_flt(m[1].rows[0][3], 8.75f);

    /* Neither moves: nothing is sent */
    ro_frame(world);
    m = ro_movers(null, &count);
    test_int(count, 0);
    ro_fini(world, null);
}

void RenderOrigin_follow_cell_exact(void) {
    // renderer/render_origin
    /* A cell a thousand cells out with the origin 0.3 units off it: its Position rounds to the
     * float grid of 4e6, the render origin and its record do not. */
    box_render_null_t *null;
    ecs_entity_t camera;
    ecs_world_t *world = ro_world(&null, &camera, 0);
    ecs_entity_t root = ecs_new(world);
    ecs_singleton_set(world, WorldOrigin, {.x = 0.3, .enabled = false, .render = true,
        .render_follow = root});
    ecs_set(world, root, WorldCell, {1000, 0, 0});
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, child, Position, {(float)i, 0, 0});
        ecs_set(world, child, Box, {0.5f, 0.5f, 0.5f});
    }

    ro_frame(world);
    test_assert(null->last.local.delta[0] == 1000.0 * BOX_WORLD_CELL_SIZE - 0.3);
    test_assert((double)ecs_get(world, root, Position)->x != null->last.local.delta[0]);

    int32_t count;
    const box_render_transform_t *m = ro_movers(null, &count);
    test_int(count, 2);
    test_assert(m[1].rows[0][3] == 0.0f);
    test_assert(m[1].rows[1][3] == 0.0f);
    test_assert(m[1].rows[2][3] == 0.0f);

    /* A shift leaves the record of the root at 0 */
    box_world_origin_shift(world, 0, (double[3]){ 1000.0 * BOX_WORLD_CELL_SIZE, 0, 0 });
    ro_frame(world);
    test_assert(fabs(null->last.local.delta[0] + 0.3) < 1e-6);
    m = ro_movers(null, &count);
    test_assert(count == 0 || m[1].rows[0][3] == 0.0f);
    ro_fini(world, null);
}

/** Measure what moving the render origin with the camera costs a frame, with a mover of a
 * number of children.
 *
 * @param children Children of the mover
 * @param bytes Receives the bytes the last frame uploaded into the instance and mover buffers
 * @param ms Receives the milliseconds of the last frame, the median of 9
 */
static void ro_cost(
    int32_t children,
    int64_t *bytes,
    float *ms)
{
    box_render_null_t *null;
    ecs_entity_t camera;
    float times[9];
    ecs_world_t *world = ro_world(&null, &camera, 0);
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true});
    ro_mover(world, 10, children);
    ro_frame(world);
    ro_frame(world);
    for (int32_t i = 0; i < 9; i ++) {
        ecs_set(world, camera, Position, {(float)i * 0.25f, 2, 10});
        ecs_time_t t = {0};
        ecs_time_measure(&t);
        ro_frame(world);
        times[i] = (float)(ecs_time_measure(&t) * 1000.0);
    }

    for (int32_t i = 0; i < 9; i ++) {
        for (int32_t j = i + 1; j < 9; j ++) {
            if (times[j] < times[i]) {
                float tmp = times[i];
                times[i] = times[j];
                times[j] = tmp;
            }
        }
    }

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    *bytes = stats.upload_bytes;
    *ms = times[4];
    int32_t count;
    ro_movers(null, &count);
    test_int(count, 2);
    ro_fini(world, null);
}

void RenderOrigin_cost_per_mover(void) {
    // renderer/render_origin
    int64_t bytes[2];
    float ms[2];
    ro_cost(1000, &bytes[0], &ms[0]);
    ro_cost(100000, &bytes[1], &ms[1]);
    printf("render origin every frame: 1k children %lld bytes %.3f ms, "
        "100k children %lld bytes %.3f ms\n",
        (long long)bytes[0], ms[0], (long long)bytes[1], ms[1]);
    test_assert(bytes[0] == bytes[1]);
    test_assert(bytes[0] <= 2 * (int64_t)sizeof(box_render_transform_t));
}
