/**
 * @file Select.c
 * @brief Tests for the selection: instance roots, opened instances and clicks in the scene.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;

void Select_setup(void)
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
        "using box.light\n"
        "template Tree {\n"
        "  trunk {\n"
        "    Position: {0, 1, 0}\n"
        "    Box: {1, 2, 1}\n"
        "  }\n"
        "  crown {\n"
        "    Position: {0, 2.5, 0}\n"
        "    Sphere: {radius: 1}\n"
        "  }\n"
        "}\n"
        "prefab Crate {\n"
        "  lid {\n"
        "    Position: {0, 0.6, 0}\n"
        "    Box: {1, 0.2, 1}\n"
        "  }\n"
        "}\n"
        "scene {\n"
        "  Tree t1() {\n"
        "    Position: {-4, 0, 0}\n"
        "  }\n"
        "  Tree t2() {\n"
        "    Position: {4, 0, 0}\n"
        "  }\n"
        "  crate : Crate {\n"
        "    Position: {0, -2, 0}\n"
        "  }\n"
        "  rock {\n"
        "    Position: {0, 0, 0}\n"
        "    Sphere: {radius: 1}\n"
        "  }\n"
        "  road {\n"
        "    Spline: {points: [{0, 0, 0}, {1, 0, 0}, {2, 0, 1}]}\n"
        "  }\n"
        "  sun {\n"
        "    Rotation: {0.9, -0.6, 0}\n"
        "    DirectionalLight: {intensity: 1}\n"
        "  }\n"
        "  lamp {\n"
        "    Position: {0, 4, 0}\n"
        "    PointLight: {intensity: 1, range: 10}\n"
        "  }\n"
        "}\n" });
    test_assert(s2 != 0);
    box_editor_enable(w, true);
    ecs_progress(w, 0);
}

void Select_teardown(void)
{
    ecs_fini(w);
}

void Select_select_marks_an_entity(void)
{
    // editor/module
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    test_uint(box_editor_select(w, rock), rock);
    test_uint(ecs_singleton_get(w, Editor)->selected, rock);
    test_bool(ecs_singleton_get(w, Editor)->instance, false);
}

void Select_select_zero_clears(void)
{
    // editor/module
    box_editor_select(w, ecs_lookup(w, "scene.rock"));
    test_uint(box_editor_select(w, 0), 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
}

void Select_child_of_a_template_instance_selects_the_root(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    test_assert(trunk != 0);
    test_uint(box_editor_select(w, trunk), t1);
    test_uint(ecs_singleton_get(w, Editor)->selected, t1);
}

void Select_child_of_a_prefab_instance_selects_the_root(void)
{
    // editor/templates
    ecs_entity_t crate = ecs_lookup(w, "scene.crate");
    ecs_entity_t lid = ecs_lookup(w, "scene.crate.lid");
    test_assert(lid != 0);
    test_uint(box_editor_select(w, lid), crate);
}

void Select_root_is_reported_as_an_instance(void)
{
    // editor/templates
    box_editor_select(w, ecs_lookup(w, "scene.t1"));
    test_bool(ecs_singleton_get(w, Editor)->instance, true);
    box_editor_select(w, ecs_lookup(w, "scene.crate"));
    test_bool(ecs_singleton_get(w, Editor)->instance, true);
    box_editor_select(w, ecs_lookup(w, "scene.rock"));
    test_bool(ecs_singleton_get(w, Editor)->instance, false);
}

void Select_spline_is_reported(void)
{
    // editor/spline
    box_editor_select(w, ecs_lookup(w, "scene.road"));
    test_bool(ecs_singleton_get(w, Editor)->spline, true);
    test_int(ecs_singleton_get(w, Editor)->point, -1);
    box_editor_select(w, ecs_lookup(w, "scene.rock"));
    test_bool(ecs_singleton_get(w, Editor)->spline, false);
}

void Select_open_hides_the_siblings_and_shows_the_instance(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    box_editor_select(w, t1);
    test_bool(box_editor_open(w, t1), true);
    const Editor *ed = ecs_singleton_get(w, Editor);
    test_uint(ed->opened, t1);
    test_int(ed->depth, 1);
    test_uint(ed->selected, 0);
    test_bool(ed->instance, false);
    test_assert(!ecs_has_id(w, t1, Hidden));
    test_assert(!ecs_has_id(w, ecs_lookup(w, "scene.t1.trunk"), Hidden));
    test_assert(ecs_has_id(w, ecs_lookup(w, "scene.t2"), Hidden));
    test_assert(ecs_has_id(w, ecs_lookup(w, "scene.rock"), Hidden));
    test_assert(!ecs_has_id(w, ecs_lookup(w, "scene"), Hidden));
    test_assert(!ecs_has_id(w, ecs_lookup(w, "cam"), Hidden));
    test_assert(!ecs_has_id(w, ecs_lookup(w, "box"), Hidden));
}

void Select_open_lets_the_children_be_selected(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    ecs_entity_t crown = ecs_lookup(w, "scene.t1.crown");
    box_editor_open(w, t1);
    test_uint(box_editor_select(w, trunk), trunk);
    test_uint(ecs_singleton_get(w, Editor)->selected, trunk);
    test_uint(box_editor_select(w, crown), crown);
    test_uint(ecs_singleton_get(w, Editor)->selected, crown);
}

void Select_open_refuses_to_select_the_opened_instance(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    box_editor_open(w, t1);
    box_editor_select(w, trunk);
    test_uint(box_editor_select(w, t1), 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
    test_uint(box_editor_resolve(w, t1), 0);
    test_uint(box_editor_resolve(w, trunk), trunk);
    test_bool(box_editor_close(w), true);
    test_uint(box_editor_select(w, t1), t1);
}

void Select_open_keeps_the_lights(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t sun = ecs_lookup(w, "scene.sun");
    ecs_entity_t lamp = ecs_lookup(w, "scene.lamp");
    test_assert(sun != 0);
    test_assert(lamp != 0);
    test_bool(box_editor_open(w, t1), true);
    test_assert(!ecs_has_id(w, sun, Hidden));
    test_assert(!ecs_has_id(w, lamp, Hidden));
    test_assert(ecs_has_id(w, ecs_lookup(w, "scene.rock"), Hidden));
    test_bool(box_editor_close(w), true);
    test_assert(!ecs_has_id(w, sun, Hidden));
    test_assert(!ecs_has_id(w, lamp, Hidden));
}

void Select_close_shows_the_scene_again(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    box_editor_open(w, t1);
    test_assert(ecs_has_id(w, rock, Hidden));
    test_bool(box_editor_close(w), true);
    test_assert(!ecs_has_id(w, rock, Hidden));
    const Editor *ed = ecs_singleton_get(w, Editor);
    test_uint(ed->opened, 0);
    test_int(ed->depth, 0);
    test_uint(ed->selected, t1);
    test_bool(ed->instance, true);
    test_bool(box_editor_close(w), false);
}

void Select_open_is_recursive(void)
{
    // editor/templates
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "template Leaf {\n"
        "  blade {\n"
        "    Box: {0.1, 0.5, 0.1}\n"
        "  }\n"
        "}\n"
        "template Bush {\n"
        "  Leaf a() {\n"
        "    Position: {1, 0, 0}\n"
        "  }\n"
        "  Leaf b() {\n"
        "    Position: {-1, 0, 0}\n"
        "  }\n"
        "}\n"
        "scene {\n"
        "  Bush bush() {\n"
        "    Position: {0, 5, 0}\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t bush = ecs_lookup(w, "scene.bush");
    ecs_entity_t a = ecs_lookup(w, "scene.bush.a");
    ecs_entity_t b = ecs_lookup(w, "scene.bush.b");
    ecs_entity_t blade = ecs_lookup(w, "scene.bush.a.blade");
    test_assert(bush && a && b && blade);

    test_uint(box_editor_select(w, blade), bush);
    test_bool(box_editor_open(w, bush), true);
    test_uint(box_editor_select(w, blade), a);
    test_bool(box_editor_open(w, a), true);
    test_int(ecs_singleton_get(w, Editor)->depth, 2);
    test_assert(ecs_has_id(w, b, Hidden));
    test_assert(!ecs_has_id(w, a, Hidden));
    test_uint(box_editor_select(w, blade), blade);

    test_bool(box_editor_close(w), true);
    test_assert(!ecs_has_id(w, b, Hidden));
    test_uint(ecs_singleton_get(w, Editor)->opened, bush);
    test_assert(ecs_has_id(w, ecs_lookup(w, "scene.rock"), Hidden));
    test_bool(box_editor_close(w), true);
    test_assert(!ecs_has_id(w, ecs_lookup(w, "scene.rock"), Hidden));
}

void Select_open_refuses_an_entity_that_is_not_a_root(void)
{
    // editor/templates
    test_bool(box_editor_open(w, ecs_lookup(w, "scene.rock")), false);
    test_bool(box_editor_open(w, ecs_lookup(w, "scene.t1.trunk")), false);
    test_int(ecs_singleton_get(w, Editor)->depth, 0);
}

void Select_click_in_the_scene_selects_the_hit(void)
{
    // editor/module
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640, 360} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, rock);
}

void Select_click_on_a_child_selects_the_root_until_opened(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {390, 298} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, t1);

    test_bool(box_editor_open(w, t1), true);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, trunk);
}

void Select_click_on_empty_space_clears_the_selection(void)
{
    // editor/module
    box_editor_select(w, ecs_lookup(w, "scene.rock"));
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640, 60} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
}

void Select_deleted_selection_is_cleared(void)
{
    // editor/module
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    box_editor_select(w, rock);
    ecs_delete(w, rock);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
    test_uint(ecs_singleton_get(w, EditorInspector)->entity, 0);
}
