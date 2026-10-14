/**
 * @file box/command.h
 * @brief Terminal sessions that run command lines over HTTP.
 *
 * Commands are flecs script functions under box.command.commands. Every session is an
 * entity with a Terminal component, its own folder and a log of the lines it ran.
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

/** Prefix of the message of a failed command */
#define BOX_COMMAND_ERROR "error: "

/** A terminal session; adding it creates the session folder, removing it deletes it */
ECS_STRUCT(Terminal, {
    char *cwd;          /**< Scope that entity arguments resolve against, NULL for the root */
});

/** State of a terminal session, written by the engine */
ECS_STRUCT(TerminalStatus, {
    char *dir;          /**< Session folder, /tmp/box/<pid>/<session> */
    int32_t commands;   /**< Command lines the session ran */
});

/** Singleton that serves the terminal, resource and REST endpoints on a port */
ECS_STRUCT(TerminalServer, {
    int32_t port;       /**< Port to listen on, 0 stops the server */
});

/** A parameter of a command, after the session parameter every command has */
typedef struct box_command_param_t {
    const char *name;   /**< Parameter name */
    ecs_entity_t type;  /**< ecs_string_t, ecs_i32_t, ecs_f64_t, ecs_bool_t or ecs_entity_t */
    bool optional;      /**< The argument may be omitted and is then zero */
    bool rest;          /**< Last string parameter that takes the remainder of the line */
} box_command_param_t;

/** Describes a command for box_command_register */
typedef struct box_command_desc_t {
    const char *name;                       /**< Command name, may contain dots */
    const char *brief;                      /**< One line description */
    box_command_param_t params[FLECS_SCRIPT_FUNCTION_ARGS_MAX - 1]; /**< Parameters */
    ecs_function_callback_t callback;       /**< Writes a char* into result->ptr */
    void *ctx;                              /**< Passed to the callback as ctx->ctx */
    bool quiet;                             /**< The result is not logged, only the line */
} box_command_desc_t;

/** Register the box.command module.
 *
 * @param world The world
 */
void BoxCommandImport(
    ecs_world_t *world);

/** Register a command as a script function under box.command.commands.
 *
 * The function gets the session entity as its first argument, then the parameters.
 *
 * @param world The world
 * @param desc The command
 * @return The function entity
 */
ecs_entity_t box_command_register(
    ecs_world_t *world,
    const box_command_desc_t *desc);

/** Fail a command: set its result to an error message.
 *
 * @param ctx The call context of the command
 * @param result The result of the command
 * @param fmt Format of the message, which gets the BOX_COMMAND_ERROR prefix
 * @param ... The format arguments
 */
void box_command_error(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *fmt,
    ...);

/** Open a terminal session with a unique handle.
 *
 * @param world The world
 * @return The session entity, its name is the handle
 */
ecs_entity_t box_terminal_new(
    ecs_world_t *world);

/** Find a terminal session by its handle.
 *
 * @param world The world
 * @param handle The session handle
 * @return The session entity, 0 when there is none
 */
ecs_entity_t box_terminal_lookup(
    ecs_world_t *world,
    const char *handle);

/** Run a command line in a session and log it.
 *
 * @param world The world
 * @param session The session entity
 * @param line The command name followed by its arguments
 * @param error Set to whether the command failed, NULL to ignore
 * @return The command output, which the caller frees
 */
char* box_terminal_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error);

/** Serve one request of the terminal, resource or REST endpoints without a socket.
 *
 * @param world The world
 * @param method The HTTP method, such as "GET"
 * @param path The URL encoded path with its query, such as "/terminal/new"
 * @param body The request body, NULL for none
 * @param reply Receives the reply, initialized with ECS_HTTP_REPLY_INIT by the caller
 * @return The HTTP status code
 */
int box_command_request(
    ecs_world_t *world,
    const char *method,
    const char *path,
    const char *body,
    ecs_http_reply_t *reply);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
