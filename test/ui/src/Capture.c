/**
 * @file Capture.c
 * @brief Tests for the UiCapture singleton the hit test of the frame writes.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void Capture_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 960;
    s->height = 540;
}

void Capture_teardown(void)
{
    ecs_fini(w);
}

void Capture_a_world_without_a_tree_captures_nothing(void)
{
    // ore_engine/input_layers
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    test_bool(box_ui_mouse_captured(w), false);
    test_assert(ecs_singleton_get(w, UiCapture) != NULL);
    test_uint(ecs_singleton_get(w, UiCapture)->element, 0);
}

void Capture_the_cursor_over_an_element_captures_the_mouse(void)
{
    // ore_engine/input_layers
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {120, 60});
    ecs_progress(w, 0);
    test_bool(box_ui_mouse_captured(w), false);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    test_bool(box_ui_mouse_captured(w), true);
}

void Capture_the_capture_names_the_element_under_the_cursor(void)
{
    // ore_engine/input_layers
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {120, 60});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    test_uint(ecs_singleton_get(w, UiCapture)->element, button);
    test_bool(ecs_singleton_get(w, UiCapture)->mouse, true);
}

void Capture_the_cursor_beside_the_tree_captures_nothing(void)
{
    // ore_engine/input_layers
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {120, 60});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(box_ui_mouse_captured(w), true);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {900, 500} });
    ecs_progress(w, 0);

    test_bool(box_ui_mouse_captured(w), false);
    test_uint(ecs_singleton_get(w, UiCapture)->element, 0);
}

void Capture_a_collider_of_the_scene_does_not_capture(void)
{
    // ore_engine/input_layers
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .priority = 0, .enabled = true});
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, ball, Hovered), true);
    test_bool(box_ui_mouse_captured(w), false);
}

void Capture_an_element_that_leaves_the_tree_releases_the_capture(void)
{
    // ore_engine/input_layers
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {120, 60});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(box_ui_mouse_captured(w), true);

    ecs_delete(w, button);
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    test_bool(box_ui_mouse_captured(w), false);
}
