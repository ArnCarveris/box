/**
 * @file Projection.c
 * @brief Cross scene projection: the surface size a view projects for.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

void Projection_offscreen_surface_resize(void) {
    // An offscreen surface (with a texture) has a SurfaceStatus that is a copy of the requested
    // size from the previous render extract: the projection uses the requested size the same frame.
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 320, .height = 200, .texture = tex});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {2, 1, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});

    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const SurfaceStatus *st = ecs_get(world, s, SurfaceStatus);
    test_assert(st != NULL);
    test_bool(st->window, false);
    test_int(st->point_width, 320);
    test_int(st->point_height, 200);

    float focal = 1.0f / tanf(glm_rad(30.0f));
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabs(p->x - focal / (320.0f / 200.0f) * 0.2f * 160.0f) < 0.01f);
    test_assert(fabs(p->y - focal * 0.1f * 100.0f) < 0.01f);

    ecs_set(world, s, Surface, {.width = 640, .height = 480, .texture = tex});
    ecs_progress(world, 0);

    p = ecs_get(world, label, Position);
    test_assert(fabs(p->x - focal / (640.0f / 480.0f) * 0.2f * 320.0f) < 0.01f);
    test_assert(fabs(p->y - focal * 0.1f * 240.0f) < 0.01f);

    ecs_fini(world);
    box_render_null_free(null);
}
