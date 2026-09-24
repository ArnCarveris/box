/**
 * @file Source.c
 * @brief Tests for the literal check of template values and the write through to the script.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;

void Source_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 1280;
    s->height = 720;
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t s2 = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "template Tree {\n"
        "  prop height: f32 = 2\n"
        "  trunk {\n"
        "    Position: {0, 1, 0}\n"
        "    Scale: {1, height, 1}\n"
        "    Box: {1, 1, 1}\n"
        "  }\n"
        "  crown {\n"
        "    Position: {0, height * 0.5, 0}\n"
        "    Sphere: {radius: 1}\n"
        "  }\n"
        "  leaf {\n"
        "    Position: {x: 0.5, y: 2, z: -0.25}\n"
        "    Rotation: {0, 1.5, 0}\n"
        "    Scale: {1, 1, 1}\n"
        "    Box: {0.1, 0.1, 0.1}\n"
        "  }\n"
        "}\n"
        "scene {\n"
        "  Tree t1() {\n"
        "    Position: {-4, 0, 0}\n"
        "  }\n"
        "  Tree t2() {\n"
        "    Position: {4, 0, 0}\n"
        "  }\n"
        "  rock {\n"
        "    Position: {0, 0, 0}\n"
        "    Sphere: {radius: 1}\n"
        "  }\n"
        "}\n" });
    test_assert(s2 != 0);
    box_editor_enable(w, true);
    ecs_progress(w, 0);
}

void Source_teardown(void)
{
    ecs_fini(w);
}

void Source_literal_value_is_literal(void)
{
    // editor/templates
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    ecs_entity_t leaf = ecs_lookup(w, "scene.t1.leaf");
    test_int((int)box_editor_source(w, trunk, ecs_id(Position)), (int)BoxEditorSourceLiteral);
    test_int((int)box_editor_source(w, leaf, ecs_id(Position)), (int)BoxEditorSourceLiteral);
    test_int((int)box_editor_source(w, leaf, ecs_id(Rotation)), (int)BoxEditorSourceLiteral);
    test_int((int)box_editor_source(w, trunk, ecs_id(Box)), (int)BoxEditorSourceLiteral);
}

void Source_computed_value_is_computed(void)
{
    // editor/templates
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    ecs_entity_t crown = ecs_lookup(w, "scene.t1.crown");
    test_int((int)box_editor_source(w, trunk, ecs_id(Scale)), (int)BoxEditorSourceComputed);
    test_int((int)box_editor_source(w, crown, ecs_id(Position)), (int)BoxEditorSourceComputed);
}

void Source_missing_statement_is_none(void)
{
    // editor/templates
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    test_int((int)box_editor_source(w, trunk, ecs_id(Rotation)), (int)BoxEditorSourceNone);
    test_int((int)box_editor_source(w, ecs_lookup(w, "scene.rock"), ecs_id(Position)),
        (int)BoxEditorSourceLiteral);
}

void Source_scene_entity_is_editable(void)
{
    // editor/templates
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    test_bool(box_editor_editable(w, rock, ecs_id(Position)), true);
    test_bool(box_editor_editable(w, rock, ecs_id(Rotation)), true);
    test_bool(box_editor_editable(w, ecs_lookup(w, "scene.t1"), ecs_id(Position)), true);
}

void Source_template_child_with_a_literal_is_editable(void)
{
    // editor/templates
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    test_bool(box_editor_editable(w, trunk, ecs_id(Position)), true);
    test_bool(box_editor_editable(w, trunk, ecs_id(Rotation)), true);
}

void Source_template_child_with_a_prop_is_not(void)
{
    // editor/templates
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    ecs_entity_t crown = ecs_lookup(w, "scene.t1.crown");
    test_bool(box_editor_editable(w, trunk, ecs_id(Scale)), false);
    test_bool(box_editor_editable(w, crown, ecs_id(Position)), false);
}

void Source_transform_flag_follows_the_source(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    box_editor_select(w, ecs_lookup(w, "scene.rock"));
    test_bool(ecs_singleton_get(w, Editor)->transform, true);
    box_editor_open(w, t1);
    box_editor_select(w, ecs_lookup(w, "scene.t1.trunk"));
    test_bool(ecs_singleton_get(w, Editor)->transform, false);
    box_editor_select(w, ecs_lookup(w, "scene.t1.crown"));
    test_bool(ecs_singleton_get(w, Editor)->transform, false);
    box_editor_select(w, ecs_lookup(w, "scene.t1.leaf"));
    test_bool(ecs_singleton_get(w, Editor)->transform, true);
}

void Source_write_through_updates_every_instance(void)
{
    // editor/templates
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    box_editor_open(w, t1);
    ecs_entity_t trunk = ecs_lookup(w, "scene.t1.trunk");
    box_editor_select(w, trunk);
    test_bool(box_editor_assign(w, trunk, ecs_id(Position), "y", "5"), true);
    ecs_progress(w, 0);
    ecs_entity_t other = ecs_lookup(w, "scene.t2.trunk");
    test_assert(other != 0);
    test_flt(ecs_get(w, other, Position)->y, 5);
    test_flt(ecs_get(w, ecs_lookup(w, "scene.t1.trunk"), Position)->y, 5);
    ecs_entity_t owner = ecs_script_entity_owner(w, ecs_lookup(w, "scene.t1.trunk"));
    test_assert(owner != 0);
    test_assert(strstr(ecs_get(w, owner, EcsScript)->script->code, "Position: {0, 5, 0}") != NULL);
}

void Source_assign_on_a_computed_member_is_refused(void)
{
    // editor/templates
    ecs_entity_t crown = ecs_lookup(w, "scene.t1.crown");
    test_bool(box_editor_assign(w, crown, ecs_id(Position), "y", "3"), false);
    test_flt(ecs_get(w, crown, Position)->y, 1);
}
