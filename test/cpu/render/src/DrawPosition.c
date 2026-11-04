/**
 * @file DrawPosition.c
 * @brief Tests that only entities with a place in the world are drawn: a shape, light or text
 * without Position (or Rotation or Scale) has no WorldTransform and is not extracted, and is
 * drawn once it gets a Position.
 *
 * Every case renders with the null backend and reads the batches and the lights of the extract,
 * or the spatial index.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include "spatial.h"

/** Create a world that renders headless with a null backend and has a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Run a few frames and get the instances in batches.
 *
 * @param world The world
 * @return Static and dynamic instances of the last frame
 */
static int32_t frame_instances(
    ecs_world_t *world)
{
    box_render_draws_stats_t stats;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_draws_stats(world, &stats);

    return stats.instances + stats.dynamic;
}

/** Get the point and spot lights of the default scene in the last packet.
 *
 * @param null The backend
 * @return The lights that are not directional
 */
static int32_t packet_local_lights(
    const box_render_null_t *null)
{
    test_assert(ecs_vec_count(&null->last.views) > 0);
    const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);

    return (int32_t)(v->lights.count - v->lights.directional);
}

void DrawPosition_box_without_position(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {200, 100, 50});
    test_bool(ecs_has(world, e, WorldTransform), false);
    test_int(frame_instances(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    test_int(frame_instances(world), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_dynamic_box_without_position(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_add(world, e, DynamicTransform);
    test_int(frame_instances(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    test_int(frame_instances(world), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_rect_without_position(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rect, {10, 10});
    ecs_set(world, e, Rgb, {0, 255, 0});
    test_bool(ecs_has(world, e, WorldTransform), false);
    test_int(frame_instances(world), 0);

    ecs_set(world, e, Position, {0, 0, 0});
    test_int(frame_instances(world), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_point_light_without_position(void) {
    // lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PointLight, {.intensity = 1, .range = 10});
    ecs_set(world, e, Rgb, {255, 200, 100});
    test_bool(ecs_has(world, e, WorldTransform), false);
    frame_instances(world);
    test_int(packet_local_lights(null), 0);

    ecs_set(world, e, Position, {0, 2, 0});
    frame_instances(world);
    test_int(packet_local_lights(null), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_text_without_position(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {
        .value = "nowhere", .size = 16, .font = ecs_lookup(world, "box.text.DefaultFont")
    });
    test_bool(ecs_has(world, e, WorldTransform), false);
    test_int(frame_instances(world), 0);

    ecs_set(world, e, Position, {0, 1, 0});
    test_int(frame_instances(world), 7);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_layout_children_drawn(void) {
    // ui/layout
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t row = ecs_new(world);
    ecs_set(world, row, Position, {0, 0, 0});
    ecs_set(world, row, Layout, {.direction = DirectionRow, .spacing = 2});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, row);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, row);
    ecs_set(world, b, Rect, {10, 10});
    test_int(frame_instances(world), 2);
    test_bool(ecs_has(world, a, WorldTransform), true);
    test_bool(ecs_has(world, b, WorldTransform), true);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_layout_direction_none_child_drawn(void) {
    // ui/layout
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t stack = ecs_new(world);
    ecs_set(world, stack, Position, {5, 0, 0});
    ecs_set(world, stack, Layout, {.direction = DirectionNone});
    ecs_set(world, stack, Rect, {40, 40});
    ecs_entity_t loose = ecs_new_w_pair(world, EcsChildOf, stack);
    ecs_set(world, loose, Rect, {10, 10});
    ecs_entity_t placed = ecs_new_w_pair(world, EcsChildOf, stack);
    ecs_set(world, placed, Position, {3, 4, 0});
    ecs_set(world, placed, Rect, {10, 10});
    test_int(frame_instances(world), 3);

    const Position *p = ecs_get(world, loose, Position);
    test_assert(p != NULL);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    p = ecs_get(world, placed, Position);
    test_assert(p != NULL);
    test_flt(p->x, 3);
    test_flt(p->y, 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void DrawPosition_shape_without_position_not_indexed(void) {
    // spatial
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    vec3 min, max;
    frame_instances(world);
    test_bool(box_spatial_bounds(world, e, NULL, min, max), false);

    ecs_set(world, e, Position, {4, 0, 0});
    frame_instances(world);
    test_bool(box_spatial_bounds(world, e, NULL, min, max), true);
    test_flt(min[0], 3.5f);
    test_flt(max[0], 4.5f);

    ecs_fini(world);
    box_render_null_free(null);
}
