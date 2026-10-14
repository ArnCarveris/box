/**
 * @file command/command.h
 * @brief Session state, the command host and the helpers shared by the box.command sources.
 */

#ifndef BOX_COMMAND_INTERNAL_H
#define BOX_COMMAND_INTERNAL_H

#include <box.h>
#include <stdio.h>

/** Path of the scope that holds the command functions */
#define BOX_COMMAND_SCOPE "box.command.commands"

/** Path of the scope that holds the sessions opened with box_terminal_new */
#define BOX_TERMINAL_SCOPE "box.command.terminals"

/** Name of the session subfolder that the resource endpoint serves */
#define BOX_TERMINAL_RESOURCES "resources"

/** Name of the file in the session folder that every command line is appended to */
#define BOX_TERMINAL_LOG "commands.log"

/** One command line a session ran and its result */
typedef struct box_command_entry_t {
    char *line;         /**< The command line */
    char *result;       /**< Output of the command, NULL for a quiet command */
    bool error;         /**< The command failed */
} box_command_entry_t;

/** Private state of a terminal session */
typedef struct TerminalSession {
    char *dir;          /**< The session folder, removed with the session */
    FILE *log;          /**< Open commands.log in the session folder */
    ecs_vec_t entries;  /**< box_command_entry_t of every line the session ran */
} TerminalSession;

/** Parameter rules of a registered command */
typedef struct CommandInfo {
    int32_t required;   /**< Arguments that may not be omitted */
    bool rest;          /**< The last parameter takes the remainder of the line */
    bool quiet;         /**< The result is not logged */
} CommandInfo;

/** Process wide state of the module: the HTTP server and the session folders */
typedef struct box_terminal_host_t {
    ecs_world_t *world;         /**< World the endpoints operate on */
    ecs_http_server_t *server;  /**< Server of the endpoints, listening when started */
    ecs_http_server_t *rest;    /**< REST server that unknown requests are forwarded to */
    ecs_time_t clock;           /**< When the server was last dequeued */
    int32_t port;               /**< Port the server listens on, 0 when stopped */
    int32_t port_wanted;        /**< Port of the TerminalServer singleton */
    int32_t next;               /**< Counter of generated session handles */
    bool failed;                /**< The running command called box_command_error */
    char *root;                 /**< Folder this module created for the sessions, or NULL */
} box_terminal_host_t;

/** Singleton that points to the host of the world */
typedef struct TerminalHost {
    box_terminal_host_t *host;  /**< The host, freed when the world is deleted */
} TerminalHost;

extern ECS_COMPONENT_DECLARE(TerminalSession);
extern ECS_COMPONENT_DECLARE(CommandInfo);
extern ECS_COMPONENT_DECLARE(TerminalHost);

/** Get the host of a world.
 *
 * @param world The world
 * @return The host
 */
box_terminal_host_t* box_terminal_host(
    const ecs_world_t *world);

/** Get the folder the session folders are created in, creating it when needed.
 *
 * @param world The world
 * @return The folder, NULL when it could not be created
 */
const char* box_terminal_root(
    ecs_world_t *world);

/** Remove a folder and everything in it.
 *
 * @param path The folder
 */
void box_terminal_rmtree(
    const char *path);

/** Append a command line and its result to the log of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param line The command line
 * @param result The output, NULL to log only the line
 * @param error The command failed
 */
void box_terminal_log(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *result,
    bool error);

/** Get the scope that entity arguments of a session resolve against.
 *
 * @param world The world
 * @param session The session entity
 * @return The scope, 0 for the root
 */
ecs_entity_t box_terminal_cwd(
    ecs_world_t *world,
    ecs_entity_t session);

/** Register the session observers.
 *
 * @param world The world
 * @param host The host
 */
void box_terminal_init(
    ecs_world_t *world,
    box_terminal_host_t *host);

/** Register the HTTP server, its endpoints and the system that serves them.
 *
 * @param world The world
 * @param host The host
 */
void box_endpoint_init(
    ecs_world_t *world,
    box_terminal_host_t *host);

/** Stop the HTTP server and release the REST server.
 *
 * @param host The host
 */
void box_endpoint_fini(
    box_terminal_host_t *host);

/** Install the log capture and register the logs command.
 *
 * @param world The world
 */
void box_logs_init(
    ecs_world_t *world);

/** Register the commands command.
 *
 * @param world The world
 */
void box_history_init(
    ecs_world_t *world);

/** Access a shell command needs to the entity of a path */
typedef enum box_shell_access_t {
    BoxShellScope,      /**< Any entity or the root */
    BoxShellRead,       /**< Any entity but the root */
    BoxShellWrite,      /**< An entity outside the flecs scope */
    BoxShellDelete      /**< A writable entity outside modules and sessions */
} box_shell_access_t;

/** Resolve a path against the current entity of a session, failing the command when it can't.
 *
 * A leading / starts at the root, ../ goes up one level, the rest is a dotted flecs path.
 *
 * @param ctx The call context of the command
 * @param result The result of the command, which gets the error
 * @param session The session entity
 * @param path The path, NULL for the current entity
 * @param access The access the command needs
 * @param out Receives the entity, 0 for the root
 * @return 0 on success, -1 when the command failed
 */
int box_shell_resolve(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    const char *path,
    box_shell_access_t access,
    ecs_entity_t *out);

/** Register the shell commands cd, ls, touch, rm, mv, man and the component commands.
 *
 * @param world The world
 */
void box_shell_init(
    ecs_world_t *world);

/** Register the component commands add, remove, set, get and clear.
 *
 * @param world The world
 */
void box_component_init(
    ecs_world_t *world);

#endif
