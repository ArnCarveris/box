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
    char *out = box_terminal_run(world, s, line, &error);
    test_bool(error, false);
    test_str(out, expect);
    ecs_os_free(out);
}

void Edit_toggle(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    edit_run(world, s, "edit", "editor open");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    edit_run(world, s, "edit", "editor closed");
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    edit_run(world, s, "edit", "editor open");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    ecs_fini(world);
}

void Edit_on_off(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    edit_run(world, s, "edit on", "editor open");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    edit_run(world, s, "edit on", "editor open");
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    edit_run(world, s, "edit off", "editor closed");
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    edit_run(world, s, "edit off", "editor closed");
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    ecs_fini(world);
}

void Edit_keeps_other_members(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, Editor, { .width = 0.4f, .side = EditorSideLeft });
    ecs_entity_t s = box_terminal_new(world);
    edit_run(world, s, "edit on", "editor open");
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

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "edit maybe", &error);
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

    ecs_entity_t s = box_terminal_new(world);
    edit_run(world, s, "edit", "editor closed");

    ecs_fini(world);
}

void Edit_help_lists_edit(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t f = ecs_lookup(world, "box.command.commands.edit");
    test_assert(f != 0);
    test_assert(ecs_has(world, f, EcsScriptFunction));

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "help", NULL);
    test_assert(!strncmp(out, "edit ", 5) || strstr(out, "\nedit ") != NULL);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help edit", NULL);
    test_assert(strstr(out, "edit [state]") != NULL);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "man edit", NULL);
    test_str(out, "usage: edit [state]\nbrief: Open or close the editor, toggle without a state");
    ecs_os_free(out);

    ecs_fini(world);
}

void Edit_http_exec(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *path = flecs_asprintf("/terminal/exec?session=%s&cmd=edit%%20on",
        ecs_get_name(world, s));
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "editor open");
    ecs_os_free(body);
    ecs_os_free(path);
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    ecs_fini(world);
}

void Edit_from_console(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    bool error = true;
    char *out = box_console_run(world, "edit", &error);
    test_bool(error, false);
    test_str(out, "editor open");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, Editor)->open, true);

    out = box_console_run(world, "edit off", &error);
    test_bool(error, false);
    test_str(out, "editor closed");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, Editor)->open, false);

    ecs_fini(world);
}

void Edit_logged(void) {
    // command/edit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    edit_run(world, s, "edit on", "editor open");

    char *out = box_terminal_run(world, s, "commands", NULL);
    test_assert(strstr(out, "$ edit on") != NULL);
    test_assert(strstr(out, "editor open") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}
