/**
 * @file command/command.h
 * @brief Session state, the command host and the helpers shared by the box.command sources.
 */

#ifndef BOX_COMMAND_INTERNAL_H
#define BOX_COMMAND_INTERNAL_H

#include <box.h>
#include <stdio.h>
#include "../app/app.h"

/** Error of the command functions when the world has no TerminalHost */
#define BOX_COMMAND_NOT_IMPORTED "box.command is not imported"

/** Path of the scope that holds the command functions */
#define BOX_COMMAND_SCOPE "box.command.commands"

/** Path of the scope that holds the sessions opened with box_terminal_new */
#define BOX_TERMINAL_SCOPE "box.command.terminals"

/** Name of the session subfolder that the resource endpoint serves */
#define BOX_TERMINAL_RESOURCES "resources"

/** Name of the file in the session folder that every command line is appended to */
#define BOX_TERMINAL_LOG "commands.log"

/** Packets after the last submitted one by which the renderer has written or dropped an image
 * that a command names now: a capture goes into the next packet, a render one packet later
 * at most */
#define BOX_TERMINAL_IMAGE_PACKETS (2)

/** box_terminal_image result: the session keeps no images */
#define BOX_TERMINAL_IMAGE_NONE (-1)

/** box_terminal_image result: every image the session keeps is still being written */
#define BOX_TERMINAL_IMAGE_PENDING (-2)

/** An image a render command named, which the renderer writes in a later frame */
typedef struct box_terminal_image_t {
    char *path;         /**< The PNG file */
    int32_t packet;     /**< RenderStatus.rendered from which the renderer is done with it */
} box_terminal_image_t;

/** One command line a session ran and its result */
typedef struct box_command_entry_t {
    char *line;         /**< The command line */
    char *result;       /**< Output of the command, NULL for a quiet command */
    bool error;         /**< The command failed */
} box_command_entry_t;

/** Private state of a terminal session; its limits are read from TerminalLimits on open */
typedef struct TerminalSession {
    char *dir;          /**< The session folder, removed with the session */
    FILE *log;          /**< Open commands.log in the session folder */
    ecs_vec_t entries;  /**< Ring of box_command_entry_t of the last lines the session ran */
    ecs_vec_t images;   /**< box_terminal_image_t of the images the session keeps, oldest first */
    int64_t total;      /**< Lines the session ran since it opened */
    int32_t history;    /**< Size of the entries ring */
    int32_t images_max; /**< Size of the images ring */
    int32_t log_size;   /**< Bytes of commands.log before it moves to commands.log.1 */
    double active;      /**< Wall clock seconds of the last command, or of the open */
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
    char *root;                 /**< Temporary folder opened for the sessions, or NULL */
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
 * @return The host, NULL when box.command is not imported or its TerminalHost was removed
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

/** Get the number of lines in the history ring of a session.
 *
 * @param s The session
 * @return Lines the ring holds, at most TerminalLimits.history of the session
 */
int32_t box_terminal_history_count(
    const TerminalSession *s);

/** Get a line of the history ring of a session.
 *
 * @param s The session
 * @param i Index from the oldest line in the ring
 * @return The line
 */
const box_command_entry_t* box_terminal_history_get(
    const TerminalSession *s,
    int32_t i);

/** Keep an image a render command names in the resources folder of a session.
 *
 * An image counts against TerminalLimits.images of the session until it is deleted. When the
 * session keeps that many, the oldest image the renderer is done with is deleted; an image
 * that is still being written is never evicted, so its file cannot appear untracked later.
 *
 * @param world The world
 * @param session The session entity
 * @param path The image file
 * @return 0 when the image is kept, BOX_TERMINAL_IMAGE_NONE when the session keeps no
 * images, BOX_TERMINAL_IMAGE_PENDING when every image it keeps is still being written
 */
int box_terminal_image(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path);

/** Test whether box_terminal_new may not open another session.
 *
 * @param world The world
 * @param open Receives the number of open sessions, NULL to ignore
 * @return Whether TerminalLimits.sessions sessions are open
 */
bool box_terminal_full(
    ecs_world_t *world,
    int32_t *open);

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

/** Register the search commands: query, iter, next, done, has, grep and grep_recur.
 *
 * @param world The world
 */
void box_search_init(
    ecs_world_t *world);

/** Access a shell command needs to the entity of a path */
typedef enum box_shell_access_t {
    BoxShellScope,      /**< Any entity or the root */
    BoxShellRead,       /**< Any entity but the root */
    BoxShellWrite,      /**< An entity outside the flecs scope */
    BoxShellSet,        /**< A writable entity outside sessions */
    BoxShellDelete      /**< A writable entity outside modules and sessions */
} box_shell_access_t;

/** Test whether an entity is a module, a session or lives in the scope of one.
 *
 * @param world The world
 * @param e The entity
 * @return true when changing the type of the entity could break a module or a session
 */
bool box_shell_protected(
    const ecs_world_t *world,
    ecs_entity_t e);

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

/** Resolve a component or pair argument such as Position, box.transform.Position or (Rel, Tgt).
 *
 * Names that are not a path from the root resolve as a symbol, then as a child of a module.
 * Struct members are not components, so Position.x resolves to nothing.
 *
 * @param world The world
 * @param expr The component or pair
 * @return The id, 0 when the expression names none
 */
ecs_id_t box_command_component(
    ecs_world_t *world,
    const char *expr);

/** Register the script_read, script_write, script_grep and script_patch commands.
 *
 * @param world The world
 */
void box_cmd_script_init(
    ecs_world_t *world);

/** Register the key, mouse, input and action commands.
 *
 * @param world The world
 */
void box_cmd_input_init(
    ecs_world_t *world);

/** Register the capture and render commands and the system that completes renders.
 *
 * @param world The world
 */
void box_cmd_render_init(
    ecs_world_t *world);

#endif
