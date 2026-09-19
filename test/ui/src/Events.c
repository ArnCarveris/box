/**
 * @file Events.c
 * @brief Tests for the script events a UI element raises and the actions it fires.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;
static ecs_entity_t fired_tag;
static ecs_entity_t leaf_tag;

void Events_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 960;
    s->height = 540;
    fired_tag = ecs_entity(w, { .name = "Fired" });
    leaf_tag = ecs_entity(w, { .name = "LeafFired" });
}

void Events_teardown(void)
{
    ecs_fini(w);
}

void Events_hovered_marks_the_element_under_the_cursor(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {120, 60});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, Hovered), true);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {660, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, Hovered), false);
}

void Events_hover_resumes_a_handler(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.ui\n"
        "template Button {\n"
        "  mut hits = 0\n"
        "  Rect: {120, 60}\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button button\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t button = ecs_lookup(w, "ui.button");
    test_assert(button != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {660, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, fired_tag), false);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, fired_tag), true);
}

void Events_click_resumes_a_handler(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.ui\n"
        "template Button {\n"
        "  mut hits = 0\n"
        "  Rect: {120, 60}\n"
        "  async {\n"
        "    await on.click(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button button\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t button = ecs_lookup(w, "ui.button");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, fired_tag), false);

    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, fired_tag), true);
}

void Events_click_bubbles_to_the_parent(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.ui\n"
        "template Panel {\n"
        "  mut hits = 0\n"
        "  Rect: {400, 200}\n"
        "  async {\n"
        "    await on.click(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "  button {\n"
        "    Rect: {120, 60}\n"
        "  }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Panel panel\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t panel = ecs_lookup(w, "ui.panel");
    ecs_entity_t button = ecs_lookup(w, "ui.panel.button");
    test_assert(button != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, Hovered), true);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, panel, fired_tag), true);
}

void Events_a_child_handler_consumes_the_click(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.ui\n"
        "template Leaf {\n"
        "  mut hits = 0\n"
        "  Rect: {120, 60}\n"
        "  async {\n"
        "    await on.click(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    LeafFired\n"
        "  }\n"
        "}\n"
        "template Panel {\n"
        "  mut hits = 0\n"
        "  Rect: {400, 200}\n"
        "  async {\n"
        "    await on.click(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "  Leaf button\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Panel panel\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t panel = ecs_lookup(w, "ui.panel");
    ecs_entity_t button = ecs_lookup(w, "ui.panel.button");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, button, leaf_tag), true);
    test_bool(ecs_has_id(w, panel, fired_tag), false);
}

void Events_drag_resumes_a_handler(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.ui\n"
        "template Button {\n"
        "  mut hits = 0\n"
        "  Rect: {400, 200}\n"
        "  async {\n"
        "    await on.drag(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button button\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t button = ecs_lookup(w, "ui.button");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 3 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, fired_tag), false);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {520, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, button, fired_tag), true);
}

void Events_an_element_consumes_the_pick_before_the_scene(void)
{
    // ui/ui_events
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .priority = 0, .enabled = true});
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {60, 40});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, button, Hovered), true);
    test_bool(ecs_has_id(w, ball, Hovered), false);
}

void Events_the_scene_is_picked_beside_the_tree(void)
{
    // ui/ui_events
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .priority = 0, .enabled = true});
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {60, 40});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {540, 270} });
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, button, Hovered), false);
    test_bool(ecs_has_id(w, ball, Hovered), true);
}

void Events_a_click_emits_the_action_of_the_element(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.input\n"
        "using box.picking\n"
        "using box.ui\n"
        "actions {\n"
        "  Save { InputAction: {description: \"Save the world\"} }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  button {\n"
        "    Rect: {120, 60}\n"
        "    Action: {action: actions.Save}\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t save = ecs_lookup(w, "actions.Save");
    test_assert(save != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->pressed, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->pressed, false);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->pressed, true);
    test_bool(ecs_get(w, save, InputAction)->released, true);
}

void Events_the_action_of_an_ancestor_fires(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.geometry\n"
        "using box.input\n"
        "using box.picking\n"
        "using box.ui\n"
        "actions {\n"
        "  Save { InputAction: {description: \"Save the world\"} }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  button {\n"
        "    Rect: {120, 60}\n"
        "    Action: {action: actions.Save}\n"
        "    label {\n"
        "      Rect: {40, 20}\n"
        "    }\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t save = ecs_lookup(w, "actions.Save");
    ecs_entity_t label = ecs_lookup(w, "ui.button.label");
    test_assert(label != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, label, Hovered), true);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->pressed, true);
}

void Events_a_handler_receives_the_point_in_logical_pixels(void)
{
    // ui/ui_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.ui\n"
        "template Button {\n"
        "  mut px = 0\n"
        "  mut py = 0\n"
        "  Rect: {400, 200}\n"
        "  async {\n"
        "    const hit = await on.hover(this)\n"
        "    px = hit.point.x\n"
        "    py = hit.point.y\n"
        "  }\n"
        "  probe {\n"
        "    Position: {px, py, 0}\n"
        "  }\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button button\n"
        "}\n" });
    test_assert(s != 0);

    ecs_entity_t probe = ecs_lookup(w, "ui.button.probe");
    test_assert(probe != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {580, 220} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    const Position *p = ecs_get(w, probe, Position);
    test_flt(p->x, 100);
    test_flt(p->y, 50);
}

void Events_an_element_that_appears_under_a_still_cursor_is_hovered(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    ecs_entity_t button = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, button, Rect, {120, 60});
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, button, Hovered), true);
}
