/**
 * @file Toolbar.c
 * @brief Tests for the toolbar of the overlay and the panels that keep clicks from the scene.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;

void Toolbar_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 1280;
    s->height = 720;
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_entity(w, { .name = "main" });
    ecs_set(w, view, View, { .camera = cam, .enabled = true });
    ecs_entity_t s2 = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.spline\n"
        "template Tree {\n"
        "  trunk {\n"
        "    Position: {0, 1, 0}\n"
        "    Box: {1, 2, 1}\n"
        "  }\n"
        "}\n"
        "scene {\n"
        "  Tree t1() {\n"
        "    Position: {-4, 0, 0}\n"
        "  }\n"
        "  rock {\n"
        "    Position: {0, 0, 0}\n"
        "    Sphere: {radius: 1}\n"
        "  }\n"
        "  road {\n"
        "    Position: {0, -3, 0}\n"
        "    Spline: {points: [{0, 0, 0}, {1, 0, 0}, {2, 0, 1}]}\n"
        "  }\n"
        "}\n" });
    test_assert(s2 != 0);
    box_editor_enable(w, true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
}

void Toolbar_teardown(void)
{
    ecs_fini(w);
}

void Toolbar_tool_buttons_switch_the_tool(void)
{
    // editor/ui
    ecs_entity_t move = ecs_lookup(w, "box.editor.ui.overlay.toolbar.move");
    test_assert(move != 0);
    const Bounds *b = ecs_get(w, move, Bounds);
    test_assert(b != NULL);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int((int)ecs_singleton_get(w, Editor)->tool, (int)ToolMove);
}

void Toolbar_active_tool_is_highlighted(void)
{
    // editor/ui
    ecs_entity_t select = ecs_lookup(w, "box.editor.ui.overlay.toolbar.select");
    ecs_entity_t rotate = ecs_lookup(w, "box.editor.ui.overlay.toolbar.rotate");
    test_int(ecs_get(w, select, Rgb)->r, 66);
    test_int(ecs_get(w, rotate, Rgb)->r, 30);
    box_editor_get(w)->tool = ToolRotate;
    box_editor_modified(w);
    ecs_progress(w, 0);
    test_int(ecs_get(w, select, Rgb)->r, 30);
    test_int(ecs_get(w, rotate, Rgb)->r, 66);
}

void Toolbar_open_button_appears_for_an_instance(void)
{
    // editor/templates
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.open") == 0);
    box_editor_select(w, ecs_lookup(w, "scene.t1"));
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.open") != 0);
    box_editor_select(w, ecs_lookup(w, "scene.rock"));
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.open") == 0);
}

void Toolbar_open_button_opens_the_instance(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    box_editor_select(w, t1);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t open = ecs_lookup(w, "box.editor.ui.overlay.toolbar.open");
    const Bounds *b = ecs_get(w, open, Bounds);
    test_assert(b != NULL);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->opened, t1);
    test_assert(ecs_has_id(w, ecs_lookup(w, "scene.rock"), Hidden));
}

void Toolbar_close_button_appears_while_opened(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.close") == 0);
    box_editor_select(w, t1);
    box_editor_open(w, t1);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t close = ecs_lookup(w, "box.editor.ui.overlay.toolbar.close");
    test_assert(close != 0);
    const Bounds *b = ecs_get(w, close, Bounds);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->opened, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.close") == 0);
}

void Toolbar_spline_buttons_appear_for_a_spline(void)
{
    // editor/spline
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.add_point") == 0);
    box_editor_select(w, ecs_lookup(w, "scene.road"));
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.add_point") != 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar.remove_point") == 0);
    box_editor_point(w, 1);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t remove = ecs_lookup(w, "box.editor.ui.overlay.toolbar.remove_point");
    test_assert(remove != 0);
    const Bounds *b = ecs_get(w, remove, Bounds);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_vec_count(&ecs_get(w, ecs_lookup(w, "scene.road"), Spline)->points), 2);
}

void Toolbar_panels_keep_clicks_from_the_scene(void)
{
    // editor/ui
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {130, 700} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, rock);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640, 60} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
}
