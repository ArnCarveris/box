/**
 * @file LayoutFill.c
 * @brief LayoutFill: an element takes the size in points of the surface it fills.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void LayoutFill_offscreen_surface_resize(void) {
    // An offscreen surface (with a texture) has a SurfaceStatus that is a copy of the requested
    // size from the previous render extract: the fill follows the requested size the same frame.
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 320, .height = 200, .texture = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, LayoutFill, {.surface = s});

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const SurfaceStatus *st = ecs_get(world, s, SurfaceStatus);
    test_assert(st != NULL);
    test_bool(st->window, false);
    test_int(st->point_width, 320);
    test_int(st->point_height, 200);
    const LayoutItem *item = ecs_get(world, e, LayoutItem);
    test_assert(item != NULL);
    test_flt(item->width.value, 320);
    test_flt(item->height.value, 200);

    ecs_set(world, s, Surface, {.width = 640, .height = 480, .texture = tex});
    ecs_progress(world, 0);

    item = ecs_get(world, e, LayoutItem);
    test_flt(item->width.value, 640);
    test_flt(item->height.value, 480);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Progress an offscreen surface with a LayoutFill until it has a SurfaceStatus, then check the
 * fill has the size in points of the target that is rendered. */
static void LayoutFill_check_offscreen_size(
    int32_t width,
    int32_t height,
    int32_t expect_width,
    int32_t expect_height)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = width, .height = height, .texture = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, LayoutFill, {.surface = s});

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const SurfaceStatus *st = ecs_get(world, s, SurfaceStatus);
    test_assert(st != NULL);
    test_bool(st->window, false);
    test_int(st->point_width, expect_width);
    test_int(st->point_height, expect_height);
    const LayoutItem *item = ecs_get(world, e, LayoutItem);
    test_assert(item != NULL);
    test_flt(item->width.value, (float)st->point_width);
    test_flt(item->height.value, (float)st->point_height);

    ecs_fini(world);
    box_render_null_free(null);
}

void LayoutFill_offscreen_surface_zero_size(void) {
    // A 0 x 0 offscreen surface renders a 1 x 1 target: the fill has that size, not 0 x 0.
    LayoutFill_check_offscreen_size(0, 0, 1, 1);
}

void LayoutFill_offscreen_surface_oversized(void) {
    // A width over the maximum renders a target clamped to 8192: the fill has that width.
    LayoutFill_check_offscreen_size(10000, 100, 8192, 100);
}
