/**
 * @file command/checkpoint.c
 * @brief The checkpoint command: write the state of the instance to a folder.
 */

#include "command_internal.h"

/** The checkpoint command: write a checkpoint and print its folder.
 *
 * The first line of the output is the folder, the lines after it name the
 * entities that no script statement declares, which a checkpoint cannot write.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_checkpoint_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *name = *(char**)argv[1].ptr;
    char *report = NULL;
    char *folder = box_checkpoint_write(world, name, &report);
    char *out = NULL;
    if (!folder) {
        out = ecs_os_strdup(
            BOX_COMMAND_ERROR_PREFIX "failed to create the checkpoint folder");
    } else if (report && report[0]) {
        out = flecs_asprintf("%s\n%s", folder, report);
    } else {
        out = ecs_os_strdup(folder);
    }

    ecs_os_free(report);
    ecs_os_free(folder);

    *(char**)result->ptr = out;
}

void box_checkpoint_cmd_init(
    ecs_world_t *world)
{
    box_command_param_t params[] = {
        { .name = "name", .type = ecs_id(ecs_string_t), .optional = true }
    };
    box_command_register(world, "checkpoint",
        "Write the state of every loaded script to a checkpoint folder",
        params, 1, box_checkpoint_command);
}
