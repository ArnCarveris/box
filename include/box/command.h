/**
 * @file box/command.h
 * @brief Terminal sessions that run command lines from agents.
 *
 * The box.command module gives every Terminal entity a session folder and runs
 * the lines set in TerminalInput as functions under box.command.commands.
 */

#ifndef BOX_COMMAND_H
#define BOX_COMMAND_H

#include <flecs.h>

#ifdef BOX_COMMAND_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** A terminal session, session_dir is written by the engine */
ECS_STRUCT(Terminal, {
    char *cwd;          /**< Scope that entity arguments resolve against, NULL for root */
    char *session_dir;  /**< Folder with the session files, written by the engine */
});

/** A command line to run on a terminal session */
ECS_STRUCT(TerminalInput, {
    char *line;  /**< Command name followed by its arguments */
});

/** Result of the last command line of a session, written by the engine */
ECS_STRUCT(TerminalOutput, {
    char *result;  /**< Text result of the command */
    bool error;    /**< The command failed */
});

/** Register the box.command module.
 *
 * @param world The world
 */
void BoxCommandImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
