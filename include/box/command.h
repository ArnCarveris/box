/**
 * @file box/command.h
 * @brief Box commands for the flecs script shell: input, captures, frame timing, the editor
 * and the log, as script functions in box.command.commands.
 *
 * Sessions, parsing and the builtin commands (ls, get, set, query, ...) are the flecs shell's.
 * box.command.commands is searched first, so a box command overrides a builtin of its name.
 *
 * Sessions are opened with ecs_shell_session_init and run with ecs_shell_run, or remotely
 * with the shell endpoints of the flecs REST API (PUT /shell/session).
 */

#ifndef BOX_COMMAND_H
#define BOX_COMMAND_H

#include <flecs.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Prefix of an error line in the output of box_command_run */
#define BOX_COMMAND_ERROR "error: "

/** Path of the scope that holds the box commands */
#define BOX_COMMAND_SCOPE "box.command.commands"

/** Most steps of a cursor walk (mouse_sweep, mouse_drag, action_drag) */
#define BOX_COMMAND_STEPS_MAX (1000)

/** Most frames a command argument such as dur may span */
#define BOX_COMMAND_FRAMES_MAX (36000)

/** Register the box.command module.
 *
 * Imports the flecs script shell and adds box.command.commands to the front of
 * EcsScriptShell.command_paths.
 *
 * @param world The world
 */
void BoxCommandImport(
    ecs_world_t *world);

/** Run a command line in a shell session and format its result as text.
 *
 * The result of each command is formatted with box_command_format, one per line. When the
 * commands logged errors the output is only the errors, one per line that starts with
 * BOX_COMMAND_ERROR, as a command that logs an error still returns a value.
 *
 * @param world The world
 * @param session The session, created with ecs_shell_session_init
 * @param line The command line
 * @param error Set to whether the command logged an error, NULL to ignore
 * @return The output, which the caller frees
 */
char* box_command_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error);

/** Get the file of a resource that a command created in a session.
 *
 * The flecs shell keeps the resources of a session in .flecs/shell/<session> under the working
 * directory.
 *
 * @param world The world
 * @param session The session
 * @param name The name of the resource, as ecs_shell_resource_t.name
 * @return The path of the file, which the caller frees, NULL when the session has no resources
 */
char* box_command_resource_file(
    const ecs_world_t *world,
    ecs_entity_t session,
    const char *name);

/** Format a command result as text.
 *
 * A string is returned as is, an entity as its path, a command that help returned as its usage
 * and description, the elements of a vector or array one per line in the same way, and any
 * other value as JSON.
 *
 * @param world The world
 * @param value The value, an empty value formats as an empty string
 * @return The text, which the caller frees
 */
char* box_command_format(
    const ecs_world_t *world,
    const ecs_value_t *value);

#ifdef __cplusplus
}
#endif

#endif
