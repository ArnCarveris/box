/**
 * @file BatchBounds.c
 * @brief Tests for the world bounds of batches the views that cull batches by their bounds
 * (CullSettings.bounds) test: what the packet holds and when the extract computes them.
 *
 * Every case renders with the null backend and reads the packets it received. Unit boxes share
 * one mesh from -0.5 to 0.5 and so one batch.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"

/** Create a world that renders with a null backend and has a view.
 *
 * @param null Receives the backend
 * @param bounds Whether the view culls batches by their bounds
 * @return The world
 */
static ecs_world_t* bounds_world_new(
    box_render_null_t **null,
    bool bounds)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .bounds = bounds}});

    return world;
}

/** Add a unit box.
 *
 * @param world The world
 * @param x Its x position
 * @param dynamic Whether it has DynamicTransform
 * @return The box
 */
static ecs_entity_t bounds_box_new(
    ecs_world_t *world,
    float x,
    bool dynamic)
{
    ecs_entity_t e = ecs_new(world);
    if (dynamic) {
        ecs_add(world, e, DynamicTransform);
    }

    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});

    return e;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void bounds_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get the batch of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* bounds_batch_get(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Find the draw of the batch of a shape in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The shape
 * @return The draw
 */
static const box_render_batch_t* bounds_draw_of(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    const box_draw_batch_t *b = bounds_batch_get(world, e);
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *draw = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (draw->mesh == b->key.mesh && draw->view == -1 &&
            draw->first_instance == b->first)
        {
            result = draw;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Check that a value is at or past an expected bound by less than a small padding.
 *
 * @param value The value
 * @param expect The exact bound
 * @param below Whether the value pads below the bound, else above
 */
static void bounds_near(
    float value,
    float expect,
    bool below)
{
    if (below) {
        test_assert(value <= expect);
    } else {
        test_assert(value >= expect);
    }

    test_assert(fabsf(value - expect) < 0.01f);
}

void BatchBounds_none_without_view_bounds(void) {
    // culling/bounds
    box_render_null_t *null;
    ecs_world_t *world = bounds_world_new(&null, false);
    ecs_entity_t e = bounds_box_new(world, 0, false);
    bounds_frame(world);

    test_bool(bounds_draw_of(world, null, e)->bounds, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchBounds_cover_instances(void) {
    // culling/bounds
    box_render_null_t *null;
    ecs_world_t *world = bounds_world_new(&null, true);
    ecs_entity_t e = bounds_box_new(world, 0, false);
    bounds_box_new(world, 4, false);
    bounds_frame(world);

    const box_render_batch_t *draw = bounds_draw_of(world, null, e);
    test_bool(draw->bounds, true);
    test_int(draw->instance_count, 2);
    bounds_near(draw->min[0], -0.5f, true);
    bounds_near(draw->max[0], 4.5f, false);
    bounds_near(draw->min[1], -0.5f, true);
    bounds_near(draw->max[1], 0.5f, false);
    bounds_near(draw->min[2], -0.5f, true);
    bounds_near(draw->max[2], 0.5f, false);
    bounds_near(draw->radius, sqrtf(0.75f), false);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchBounds_follow_scale_and_rotation(void) {
    // culling/bounds
    box_render_null_t *null;
    ecs_world_t *world = bounds_world_new(&null, true);
    ecs_entity_t e = bounds_box_new(world, 0, false);
    ecs_set(world, e, Scale, {4, 1, 1});
    ecs_set(world, e, Rotation, {0, 0, (float)GLM_PI_2});
    bounds_frame(world);

    const box_render_batch_t *draw = bounds_draw_of(world, null, e);
    test_bool(draw->bounds, true);
    bounds_near(draw->min[0], -0.5f, true);
    bounds_near(draw->max[0], 0.5f, false);
    bounds_near(draw->min[1], -2.0f, true);
    bounds_near(draw->max[1], 2.0f, false);
    bounds_near(draw->radius, sqrtf(4.0f + 0.25f + 0.25f), false);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchBounds_static_follow_change(void) {
    // culling/bounds
    box_render_null_t *null;
    ecs_world_t *world = bounds_world_new(&null, true);
    ecs_entity_t e = bounds_box_new(world, 0, false);
    ecs_entity_t other = bounds_box_new(world, 2, false);
    bounds_frame(world);
    test_bool(bounds_batch_get(world, e)->world_dirty, false);
    bounds_near(bounds_draw_of(world, null, e)->max[0], 2.5f, false);

    bounds_frame(world);
    test_bool(bounds_batch_get(world, e)->world_dirty, false);
    bounds_near(bounds_draw_of(world, null, e)->max[0], 2.5f, false);

    ecs_set(world, other, Position, {10, 0, 0});
    bounds_frame(world);
    bounds_near(bounds_draw_of(world, null, e)->max[0], 10.5f, false);

    ecs_delete(world, other);
    bounds_frame(world);
    bounds_near(bounds_draw_of(world, null, e)->max[0], 0.5f, false);

    bounds_box_new(world, -6, false);
    bounds_frame(world);
    bounds_near(bounds_draw_of(world, null, e)->min[0], -6.5f, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchBounds_dynamic_every_frame(void) {
    // culling/bounds
    box_render_null_t *null;
    ecs_world_t *world = bounds_world_new(&null, true);
    ecs_entity_t e = bounds_box_new(world, 0, true);
    bounds_frame(world);
    test_bool(bounds_draw_of(world, null, e)->bounds, true);
    bounds_near(bounds_draw_of(world, null, e)->max[0], 0.5f, false);

    ecs_set(world, e, Position, {7, 0, 0});
    bounds_frame(world);
    bounds_near(bounds_draw_of(world, null, e)->min[0], 6.5f, true);
    bounds_near(bounds_draw_of(world, null, e)->max[0], 7.5f, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void BatchBounds_view_turns_bounds_on(void) {
    // culling/bounds
    box_render_null_t *null;
    ecs_world_t *world = bounds_world_new(&null, false);
    ecs_entity_t e = bounds_box_new(world, 3, false);
    bounds_frame(world);
    test_bool(bounds_draw_of(world, null, e)->bounds, false);

    ecs_entity_t view = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        view = it.count ? it.entities[0] : view;
    }

    test_assert(view != 0);
    ecs_get_mut(world, view, View)->culling.bounds = true;
    ecs_modified(world, view, View);

    bounds_frame(world);
    const box_render_batch_t *draw = bounds_draw_of(world, null, e);
    test_bool(draw->bounds, true);
    bounds_near(draw->min[0], 2.5f, true);
    bounds_near(draw->max[0], 3.5f, false);

    ecs_fini(world);
    box_render_null_free(null);
}
