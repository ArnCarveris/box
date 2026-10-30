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
