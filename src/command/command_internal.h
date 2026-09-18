/**
 * @file command/command_internal.h
 * @brief Private types and helpers of the box.command module.
 */

#ifndef BOX_COMMAND_INTERNAL_H
#define BOX_COMMAND_INTERNAL_H

#include <box.h>
#include <stdio.h>
#include "../core/core_internal.h"

/** Command lines kept per session */
#define BOX_COMMAND_LOG_MAX (64)

/** Command lines listed when no count is given */
#define BOX_COMMAND_LOG_DEFAULT (20)

/** One executed command line and its result */
typedef struct box_command_entry_t {
    char *line;    /**< The command line */
    char *result;  /**< Text result of the command */
    bool error;    /**< The command failed */
} box_command_entry_t;

/** A command parameter, passed to box_command_register */
typedef struct box_command_param_t {
    const char *name;   /**< Parameter name, shown by help */
    ecs_entity_t type;  /**< Parameter type: string, i32, f64 or entity */
    bool optional;      /**< Argument may be omitted, the command gets zero */
} box_command_param_t;

/** Private state of a terminal session */
typedef struct TerminalSession {
    char *dir;                                      /**< The session folder */
    FILE *log;                                      /**< Open commands.log */
    box_command_entry_t ring[BOX_COMMAND_LOG_MAX];  /**< Last executed lines */
    int32_t count;                                  /**< Lines executed so far */
} TerminalSession;

/** Parameter count of a registered command */
typedef struct CommandInfo {
    int32_t required;  /**< Arguments that may not be omitted */
} CommandInfo;

extern ECS_COMPONENT_DECLARE(TerminalSession);
extern ECS_COMPONENT_DECLARE(CommandInfo);

/** Get the scope that holds the command functions.
 *
 * @param world The world
 * @return The box.command.commands entity
 */
ecs_entity_t box_command_scope(
    const ecs_world_t *world);

/** Register a command as a script function under box.command.commands.
 *
 * The session entity is added as the first parameter of the function.
 *
 * @param world The world
 * @param name The command name
 * @param brief One line description, shown by help
 * @param params The parameters after the session, optional ones last
 * @param count Number of parameters
 * @param callback The command implementation
 * @return The command entity
 */
ecs_entity_t box_command_register(
    ecs_world_t *world,
    const char *name,
    const char *brief,
    const box_command_param_t *params,
    int32_t count,
    ecs_function_callback_t callback);

/** Run a command line on a session and return its output.
 *
 * @param world The world
 * @param session The session entity
 * @param line The command line
 * @param error Set to true when the command failed, NULL to ignore
 * @return The command output, the caller frees it
 */
char* box_command_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error);

/** Register the observers that manage the session folders.
 *
 * @param world The world
 */
void box_terminal_init(
    ecs_world_t *world);

/** Close the log, remove the session folder and free the ring.
 *
 * @param session The session state
 */
void box_terminal_session_fini(
    TerminalSession *session);

/** Append a command line and its result to the session log and ring.
 *
 * @param world The world
 * @param session The session entity
 * @param line The command line
 * @param result Text result of the command
 * @param error The command failed
 */
void box_terminal_log(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *result,
    bool error);

/** Register the observer that runs the lines set in TerminalInput.
 *
 * @param world The world
 */
void box_dispatch_init(
    ecs_world_t *world);

/** Register the help and commands commands.
 *
 * @param world The world
 */
void box_command_builtin_init(
    ecs_world_t *world);

#endif
