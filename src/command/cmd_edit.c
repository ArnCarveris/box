/**
 * @file command/cmd_edit.c
 * @brief The edit command: open and close the editor.
 *
 * It only changes Editor.open, box.editor opens and closes the editor from that change in the
 * next frame, as it does for a change from a script or the REST API.
 */

#include "command.h"

/** Path of the Editor singleton, looked up because box.editor is imported after box.command */
#define BOX_CMD_EDIT_COMPONENT "box.editor.Editor"

/** The edit command: toggle Editor.open, or set it with on or off, and return the state.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The state, omitted to toggle
 * @param result Receives whether the editor is open
 */
static void box_cmd_edit(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    const char *state = argc > 0 ? *(const char**)argv[0].ptr : NULL;
    ecs_entity_t comp = ecs_lookup(world, BOX_CMD_EDIT_COMPONENT);
    Editor *editor;
    bool open;
    if (!comp || comp != ecs_id(Editor)) {
        ecs_err("box.editor is not imported");
        goto done;
    }

    editor = ecs_singleton_get_mut(world, Editor);
    if (!editor) {
        ecs_err("the world has no Editor");
        goto done;
    }

    if (!state || !state[0]) {
        open = !editor->open;
    } else if (!ecs_os_strcmp(state, "on")) {
        open = true;
    } else if (!ecs_os_strcmp(state, "off")) {
        open = false;
    } else {
        ecs_err("invalid state '%s', expected on or off", state);
        goto done;
    }

    editor->open = open;
    ecs_singleton_modified(world, Editor);
    *(bool*)result->ptr = open;
done:
    return;
}

void box_cmd_edit_init(
    ecs_world_t *world)
{
    box_command_register(world, "Open or close the editor, toggle without a state",
        (ecs_function_desc_t){
            .name = "edit",
            .return_type = ecs_id(ecs_bool_t),
            .params = {
                { .name = "state", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "on or off (default: toggle)" }
            },
            .callback = box_cmd_edit
        });
}
