/**
 * @file Command.c
 * @brief Tests for the editor command and what switching the editor does.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/command/command_internal.h"
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Command_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 1280;
    s->height = 720;
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 3, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
    ecs_progress(w, 0);
}

void Command_teardown(void)
{
    ecs_fini(w);
}

void Command_on_turns_the_editor_on(void)
{
    // editor/module
    bool error = true;
    char *out = box_command_run(w, session, "editor on", &error);
    test_bool(error, false);
    test_str(out, "editor on\n");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(w, Editor)->enabled, true);
}

void Command_no_argument_turns_it_on(void)
{
    // editor/module
    bool error = true;
    char *out = box_command_run(w, session, "editor", &error);
    test_bool(error, false);
    test_str(out, "editor on\n");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(w, Editor)->enabled, true);
}

void Command_off_turns_it_off(void)
{
    // editor/module
    bool error = true;
    ecs_os_free(box_command_run(w, session, "editor on", &error));
    char *out = box_command_run(w, session, "editor off", &error);
    test_bool(error, false);
    test_str(out, "editor off\n");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(w, Editor)->enabled, false);
}

void Command_off_clears_the_selection_and_the_opened_instance(void)
{
    // editor/module
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "template Tree {\n"
        "  trunk {\n"
        "    Position: {0, 1, 0}\n"
        "    Box: {1, 2, 1}\n"
        "  }\n"
        "}\n"
        "scene {\n"
        "  Tree t1()\n"
        "  rock {\n"
        "    Position: {3, 0, 0}\n"
        "    Box: {1, 1, 1}\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    bool error = true;
    ecs_os_free(box_command_run(w, session, "editor on", &error));
    ecs_entity_t t1 = ecs_lookup(w, "scene.t1");
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    box_editor_select(w, t1);
    test_bool(box_editor_open(w, t1), true);
    ecs_progress(w, 0);
    test_assert(ecs_has_id(w, rock, Hidden));

    ecs_os_free(box_command_run(w, session, "editor off", &error));
    const Editor *ed = ecs_singleton_get(w, Editor);
    test_uint(ed->selected, 0);
    test_uint(ed->opened, 0);
    test_int(ed->depth, 0);
    test_assert(!ecs_has_id(w, rock, Hidden));
}

void Command_bad_argument_is_an_error(void)
{
    // editor/module
    bool error = false;
    char *out = box_command_run(w, session, "editor sideways", &error);
    test_bool(error, true);
    ecs_os_free(out);
    test_bool(ecs_singleton_get(w, Editor)->enabled, false);
}

void Command_overlay_exists_while_on(void)
{
    // editor/ui
    bool error = true;
    ecs_os_free(box_command_run(w, session, "editor on", &error));
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay") != 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.toolbar") != 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.tree_panel") != 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.inspector") != 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay.picker") != 0);
    test_assert(ecs_has(w, ecs_lookup(w, "box.editor.ui.overlay"), Ui));
}

void Command_overlay_is_gone_while_off(void)
{
    // editor/ui
    bool error = true;
    test_assert(ecs_lookup(w, "box.editor.ui.overlay") == 0);
    ecs_os_free(box_command_run(w, session, "editor on", &error));
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay") != 0);
    ecs_os_free(box_command_run(w, session, "editor off", &error));
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "box.editor.ui.overlay") == 0);
}

void Command_system_does_nothing_while_off(void)
{
    // editor/module
    int32_t i;
    for (i = 0; i < 40; i ++) {
        ecs_progress(w, 0);
    }

    test_int(ecs_singleton_get(w, EditorTree)->version, 0);
    test_int(ecs_singleton_get(w, EditorInspector)->version, 0);
    test_int(ecs_count(w, DrawInstance), 0);
}
