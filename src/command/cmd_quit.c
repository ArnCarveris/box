/**
 * @file command/cmd_quit.c
 * @brief The quit command: end the run after the current frame.
 *
 * It quits the world, so the frame that runs the command completes (an HTTP reply goes out in
 * it) and the next ecs_progress returns false. World fini runs as for a run that reached its
 * frame count, so a headless run removes its temporary folder.
 */

#include "command.h"

/** The quit command: quit the world.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output, left empty
 */
static void box_cmd_quit(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    (void)result;
    ecs_quit(ctx->world);
}

void box_cmd_quit_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "quit",
        .brief = "End the run after this frame",
        .callback = box_cmd_quit
    });
}
