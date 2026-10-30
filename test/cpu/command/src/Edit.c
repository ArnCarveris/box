/**
 * @file Edit.c
 * @brief Tests for the edit command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Run a command line in a session and check that it succeeds with an output.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param expect The output it must print
 */
static void edit_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *expect)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_str(out, expect);
    ecs_os_free(out);
}

void Edit_toggle(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    edit_run(world, s, "edit", "true");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    edit_run(world, s, "edit", "false");
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    edit_run(world, s, "edit", "true");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    ecs_fini(world);
}

void Edit_on_off(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_run(world, s, "edit on", "true");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    edit_run(world, s, "edit on", "true");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    edit_run(world, s, "edit off", "false");
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    edit_run(world, s, "edit off", "false");
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    ecs_fini(world);
}

void Edit_keeps_other_members(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, Editor, { .width = 0.4f, .side = EditorSideLeft });
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_run(world, s, "edit on", "true");
    const Editor *e = ecs_singleton_get(world, Editor);
    test_bool(e->open, true);
    test_flt(e->width, 0.4f);
    test_int(e->side, EditorSideLeft);

    ecs_fini(world);
}

void Edit_invalid_state(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "edit maybe", &error);
    test_bool(error, true);
    test_assert(!strncmp(out, BOX_COMMAND_ERROR, strlen(BOX_COMMAND_ERROR)));
    test_assert(strstr(out, "maybe") != NULL);
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    ecs_fini(world);
}

void Edit_default_editor(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const Editor *e = ecs_singleton_get(world, Editor);
    test_assert(e != NULL);
    test_bool(e->open, false);
    test_flt(e->width, 0.25f);
    test_int(e->side, EditorSideRight);

    ecs_fini(world);
}

void Edit_set_from_script(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_script_run(world, "edit",
        "using box.editor\n"
        "$ {\n"
        "  Editor: {open: true, width: 0.3, side: Left}\n"
        "}\n", NULL), 0);
    const Editor *e = ecs_singleton_get(world, Editor);
    test_bool(e->open, true);
    test_flt(e->width, 0.3f);
    test_int(e->side, EditorSideLeft);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_run(world, s, "edit", "false");

    ecs_fini(world);
}

void Edit_help_lists_edit(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t f = ecs_lookup(world, "box.command.commands.edit");
    test_assert(f != 0);
    test_assert(ecs_has(world, f, EcsScriptFunction));

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, s, "help", NULL);
    test_assert(!strncmp(out, "edit ", 5) || strstr(out, "\nedit ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "help edit", NULL);
    test_assert(strstr(out, "edit [state]") != NULL);
    ecs_os_free(out);


    ecs_fini(world);
}

void Edit_from_console(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    bool error = true;
    char *out = box_console_run(world, "edit", &error);
    test_bool(error, false);
    test_str(out, "true");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    out = box_console_run(world, "edit off", &error);
    test_bool(error, false);
    test_str(out, "false");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    ecs_fini(world);
}
