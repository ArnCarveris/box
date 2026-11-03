/**
 * @file ShapeTransform.c
 * @brief Tests that a drawable child without Position, Rotation or Scale follows its parent.
 *
 * Only the pose components bring a WorldTransform. A child with only a shape (or a light) has
 * none, and the draw and light queries take WorldTransform as optional, so it is drawn with an
 * identity transform at the world origin instead of at its parent.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Create a world that renders with a null backend and has a view.
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
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Check that an entity has a WorldTransform that translates by a position.
 *
 * @param world The world
 * @param e The entity
 * @param x Expected x translation
 * @param y Expected y translation
 * @param z Expected z translation
 */
static void expect_translation(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y,
    float z)
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    test_assert(t != NULL);
    test_assert(fabsf(t->m[3][0] - x) < 1e-3f);
    test_assert(fabsf(t->m[3][1] - y) < 1e-3f);
    test_assert(fabsf(t->m[3][2] - z) < 1e-3f);
}

void ShapeTransform_box_child_follows_parent(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {5, 1, -3});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Box, {1, 1, 1});

    frame(world);

    expect_translation(world, child, 5, 1, -3);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShapeTransform_box_child_follows_moved_parent(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {5, 1, -3});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Box, {1, 1, 1});

    frame(world);

    ecs_set(world, parent, Position, {-2, 4, 7});

    frame(world);

    expect_translation(world, child, -2, 4, 7);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShapeTransform_box_child_follows_dynamic_parent(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {5, 1, -3});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Box, {1, 1, 1});

    frame(world);

    expect_translation(world, child, 5, 1, -3);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShapeTransform_rect_child_follows_parent(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {3, 2, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Rect, {1, 1});

    frame(world);

    expect_translation(world, child, 3, 2, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShapeTransform_point_light_child_follows_parent(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {5, 1, -3});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, PointLight, {.intensity = 1, .range = 10});

    frame(world);

    expect_translation(world, child, 5, 1, -3);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShapeTransform_template_box_child_follows_parent(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    /* The shape of the bug report: a template places its entity with Position and gives a
     * child a shape but no Position, next to a child that has one. */
    test_assert(ecs_script_run(world, "shape_transform",
        "using box.transform\n"
        "using box.geometry\n"
        "template Area {\n"
        "  floor_ { Box: {4, 0.1, 4} }\n"
        "  rim_ { Position: {0, 0, 0}\n Box: {4, 0.2, 0.2} }\n"
        "}\n"
        "square {\n"
        "  Position: {20, 0, -8}\n"
        "  Area: {}\n"
        "}\n", NULL) == 0);

    frame(world);

    ecs_entity_t rim = ecs_lookup(world, "square.rim_");
    ecs_entity_t floor = ecs_lookup(world, "square.floor_");
    test_assert(rim != 0);
    test_assert(floor != 0);

    expect_translation(world, rim, 20, 0, -8);
    expect_translation(world, floor, 20, 0, -8);

    ecs_fini(world);
    box_render_null_free(null);
}
