/**
 * @file WorldAnchor.c
 * @brief Tests for pinning an element to the screen projection of a world point.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;
static ecs_entity_t root;

void WorldAnchor_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    ECS_IMPORT(w, BoxMovement);
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 960;
    surface->height = 540;
    root = ecs_new(w);
    ecs_set(w, root, Ui, {.height = 540});
}

void WorldAnchor_teardown(void)
{
    ecs_fini(w);
}

void WorldAnchor_projects_a_world_point_into_the_view(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.offset = {5, 2.5f, 0}});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, bar, Bounds)->x, 240);
    test_flt(ecs_get(w, bar, Bounds)->y, 135);
    test_flt(ecs_get(w, bar, Position)->x, 240);
    test_flt(ecs_get(w, bar, Position)->y, 135);
    test_assert(!ecs_has_id(w, bar, Hidden));
}

void WorldAnchor_a_perspective_camera_projects_the_centre(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.offset = {0, 0, 5}});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, bar, Bounds)->x, 0);
    test_flt(ecs_get(w, bar, Bounds)->y, 0);

    ecs_set(w, bar, WorldAnchor, {.offset = {0, 15.0f * 0.57735f, 5}});
    ecs_progress(w, 0.016f);

    test_assert(ecs_get(w, bar, Bounds)->y > 269.0f);
    test_assert(ecs_get(w, bar, Bounds)->y < 271.0f);
}

void WorldAnchor_follows_the_entity_it_names(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, Position, {0, 2.5f, 0});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.entity = target});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, bar, Bounds)->y, 135);

    ecs_set(w, target, Position, {0, -2.5f, 0});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, bar, Bounds)->y, -135);
}

void WorldAnchor_offset_adds_world_units(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, Position, {2.5f, 2.5f, 0});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.entity = target, .offset = {2.5f, -2.5f, 0}});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, bar, Bounds)->x, 240);
    test_flt(ecs_get(w, bar, Bounds)->y, 0);
}

void WorldAnchor_hides_behind_the_camera(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.offset = {0, 0, -20}});
    ecs_progress(w, 0.016f);

    test_assert(ecs_has_id(w, bar, Hidden));

    ecs_set(w, bar, WorldAnchor, {.offset = {0, 0, 0}});
    ecs_progress(w, 0.016f);

    test_assert(!ecs_has_id(w, bar, Hidden));
}

void WorldAnchor_hides_outside_the_view(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, Position, {0, 10, 0});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.entity = target});
    ecs_progress(w, 0.016f);

    test_assert(ecs_has_id(w, bar, Hidden));

    ecs_set(w, target, Position, {0, 4, 0});
    ecs_progress(w, 0.016f);

    test_assert(!ecs_has_id(w, bar, Hidden));
    test_flt(ecs_get(w, bar, Bounds)->y, 216);
}

void WorldAnchor_keeps_a_hidden_an_author_set(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, Position, {0, 0, 0});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_add_id(w, bar, Hidden);
    ecs_set(w, bar, WorldAnchor, {.entity = target});
    ecs_progress(w, 0.016f);

    test_assert(ecs_has_id(w, bar, Hidden));

    ecs_set(w, target, Position, {0, 10, 0});
    ecs_progress(w, 0.016f);
    ecs_set(w, target, Position, {0, 0, 0});
    ecs_progress(w, 0.016f);

    test_assert(ecs_has_id(w, bar, Hidden));
}

void WorldAnchor_takes_no_room_in_the_flow(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, row, Layout, {.direction = DirectionRow, .spacing = 10});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, bar, Rect, {100, 10});
    ecs_set(w, bar, WorldAnchor, {.offset = {5, 0, 0}});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, b, Rect, {40, 20});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, row, Bounds)->width, 90);
    test_flt(ecs_get(w, a, Bounds)->x, -25);
    test_flt(ecs_get(w, b, Bounds)->x, 25);
    test_flt(ecs_get(w, bar, Bounds)->x, 240);
}

void WorldAnchor_children_keep_their_layout(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Layout, {
        .direction = DirectionColumn, .spacing = 4, .h = AlignCenter});
    ecs_set(w, bar, WorldAnchor, {.offset = {5, 2.5f, 0}});
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, bar);
    ecs_set(w, label, Rect, {60, 12});
    ecs_entity_t track = ecs_new_w_pair(w, EcsChildOf, bar);
    ecs_set(w, track, Rect, {80, 8});
    ecs_progress(w, 0.016f);

    test_flt(ecs_get(w, bar, Bounds)->x, 240);
    test_flt(ecs_get(w, bar, Bounds)->y, 135);
    test_flt(ecs_get(w, bar, Bounds)->height, 24);
    test_flt(ecs_get(w, label, Bounds)->y, 141);
    test_flt(ecs_get(w, track, Bounds)->y, 127);
    test_flt(ecs_get(w, label, Bounds)->x, 240);
}

void WorldAnchor_a_moving_target_moves_the_element_in_the_same_frame(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t target = ecs_new(w);
    ecs_add(w, target, DynamicTransform);
    ecs_set(w, target, Position, {0, 0, 0});
    ecs_set(w, target, Velocity, {0, 1, 0});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.entity = target});
    ecs_progress(w, 0.5f);

    test_flt(ecs_get(w, target, Position)->y, 0.5f);
    test_flt(ecs_get(w, bar, Bounds)->y, 27);

    ecs_progress(w, 0.5f);

    test_flt(ecs_get(w, target, Position)->y, 1.0f);
    test_flt(ecs_get(w, bar, Bounds)->y, 54);
}

void WorldAnchor_a_still_scene_lays_out_nothing(void)
{
    // ore_engine/world_anchor
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {
        .orthographic = true, .ortho_height = 10, .aspect_ratio = 2});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, Position, {0, 1, 0});
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {40, 10});
    ecs_set(w, bar, WorldAnchor, {.entity = target});
    ecs_progress(w, 0.016f);
    ecs_progress(w, 0.016f);

    test_assert(!ecs_get(w, root, UiImpl)->relayout);
    test_assert(!box_ui_state(w)->anchors_moved);
    test_flt(ecs_get(w, bar, Bounds)->y, 54);
}
