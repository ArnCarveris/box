/**
 * @file command/editor.c
 * @brief The editor command: switch the scene editor on or off.
 *
 * Every call but the off form turns the editor on.
 */

#include "command_internal.h"
#include "../editor/editor_internal.h"

/** The editor command: switch the editor.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_editor_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *arg = *(char**)argv[1].ptr;
    bool on = arg && !ecs_os_strcmp(arg, "on");
    bool off = arg && !ecs_os_strcmp(arg, "off");
    char *out = NULL;
    if (arg && arg[0] && !on && !off) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX
            "expected 'on' or 'off', got '%s'", arg);
        goto done;
    }

    box_editor_enable(world, !off);
    out = flecs_asprintf("editor %s\n", off ? "off" : "on");
done:
    *(char**)result->ptr = out;
}

void box_editor_cmd_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxEditor);

    box_command_param_t params[] = {{
        .name = "state", .type = ecs_id(ecs_string_t), .optional = true
    }};

    box_command_register(world, "editor",
        "Switch the scene editor on or off",
        params, 1, box_editor_command);
}
