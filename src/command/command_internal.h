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

/** A command result starting with this prefix sets the error flag, the prefix is stripped */
#define BOX_COMMAND_ERROR_PREFIX "error: "

/** Length of BOX_COMMAND_ERROR_PREFIX */
#define BOX_COMMAND_ERROR_PREFIX_LEN (7)

/** Slot of a ring buffer that holds the entry with an absolute index.
 *
 * Rings count every append, so entry i lives in slot i modulo the ring size.
 *
 * @param index Absolute index of the entry
 * @param max Number of slots in the ring
 * @return The slot that holds the entry
 */
static inline int32_t box_ring_slot(
    int32_t index,
    int32_t max)
{
    return index % max;
}

/** Absolute index of the first of the last count entries of a ring.
 *
 * @param total Entries appended to the ring so far
 * @param count Number of entries to read
 * @param max Number of slots in the ring
 * @return The absolute index to start reading at
 */
static inline int32_t box_ring_begin(
    int32_t total,
    int32_t count,
    int32_t max)
{
    if (count > max) {
        count = max;
    }

    int32_t result = total - count;
    if (result < 0) {
        result = 0;
    }

    return result;
}

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
    FILE *copy;                                     /**< Open copy of the log, NULL for none */
    char *copy_path;                                /**< File the copy is written to */
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

/** Copy the session log to a file and keep copying every later command.
 *
 * @param session The session state
 * @param path The file, NULL or empty to stop copying
 */
void box_terminal_copy(
    TerminalSession *session,
    const char *path);

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

/** Register the observer that runs the command file of a CommandScript.
 *
 * @param world The world
 */
void box_command_script_init(
    ecs_world_t *world);

/** Register the help and commands commands.
 *
 * @param world The world
 */
void box_command_builtin_init(
    ecs_world_t *world);

/** Register the shell command group.
 *
 * @param world The world
 */
void box_shell_init(
    ecs_world_t *world);

/** Register the search command group.
 *
 * @param world The world
 */
void box_search_init(
    ecs_world_t *world);

/** Register the logs command group.
 *
 * @param world The world
 */
void box_logs_init(
    ecs_world_t *world);

/** Register the script command group.
 *
 * @param world The world
 */
void box_script_init(
    ecs_world_t *world);

/** Register the capture command group.
 *
 * @param world The world
 */
void box_capture_init(
    ecs_world_t *world);

/** Register the http command group.
 *
 * @param world The world
 */
void box_http_init(
    ecs_world_t *world);

/** Remember the world that the box_request export routes through.
 *
 * A wasm image runs exactly one world, which is the world of the last import.
 *
 * @param world The world
 */
void box_wasm_init(
    ecs_world_t *world);

/** Run a request through the command endpoints and format its reply.
 *
 * The reply holds the status code, the content type and the body, separated by
 * newlines. A body with content type image/png is base64 encoded.
 *
 * @param world The world
 * @param method The HTTP method
 * @param path The request path with its query parameters
 * @param body The request body, NULL or empty for none
 * @return The reply, owned by the module and valid until the next call
 */
char* box_request_format(
    ecs_world_t *world,
    const char *method,
    const char *path,
    const char *body);

/** Encode bytes as standard base64 with padding.
 *
 * @param data The bytes
 * @param size Number of bytes
 * @return The encoded text, the caller frees it
 */
char* box_base64_encode(
    const void *data,
    int32_t size);

/** Apply a minimal unified diff to a text.
 *
 * The patch holds one or more `@@ -a,b +c,d @@` hunks with context, added and
 * removed lines. The `---`, `+++` and `\` lines of a diff are ignored.
 *
 * @param source The text to patch
 * @param patch The unified diff
 * @param error Receives the message of the hunk that failed, NULL to ignore
 * @return The patched text, NULL when a hunk did not apply, the caller frees it
 */
char* box_patch_apply(
    const char *source,
    const char *patch,
    char **error);

/** Get the scope that the paths of a session resolve against.
 *
 * @param world The world
 * @param session The session entity
 * @return The Terminal.cwd entity, 0 for the root
 */
ecs_entity_t box_path_cwd(
    ecs_world_t *world,
    ecs_entity_t session);

/** Resolve an entity path against a scope.
 *
 * Paths separate their elements with `.`, are absolute when they start with a
 * `.`, use `..` or an empty element for the parent and `/` or an empty path for
 * the root.
 *
 * @param world The world
 * @param cwd Scope that relative paths resolve against, 0 for the root
 * @param path The path
 * @param out Receives the entity the path resolves to, 0 for the root
 * @return 0 on success, -1 when an element of the path does not exist
 */
int box_path_lookup(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path,
    ecs_entity_t *out);

/** Resolve an entity path against a scope and create the missing elements.
 *
 * @param world The world
 * @param cwd Scope that relative paths resolve against, 0 for the root
 * @param path The path
 * @return The entity the path resolves to, 0 for the root
 */
ecs_entity_t box_path_ensure(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path);

/** Split a path into the scope that holds its last element and its name.
 *
 * @param world The world
 * @param cwd Scope that relative paths resolve against, 0 for the root
 * @param path The path
 * @param scope Receives the scope of the last element, 0 for the root
 * @param name Receives the name of the last element, the caller frees it
 * @return 0 on success, -1 when the path has no name or its scope is missing
 */
int box_path_split(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path,
    ecs_entity_t *scope,
    char **name);

/** Get the path of an entity the way commands print it.
 *
 * @param world The world
 * @param e The entity, 0 for the root
 * @return The path, `/` for the root, the caller frees it
 */
char* box_path_str(
    ecs_world_t *world,
    ecs_entity_t e);

/** Private state of the command server */
typedef struct HttpEndpoint {
    ecs_http_server_t *server;  /**< The server, NULL when no port is set */
    ecs_world_t *world;         /**< World the request handler operates on */
    char *reply;                /**< Reply of the last box_request_format call */
    bool started;               /**< The server thread accepts connections */
    int32_t next;               /**< Counter of the generated session names */
} HttpEndpoint;

extern ECS_COMPONENT_DECLARE(HttpEndpoint);

/** Get the state of the command server.
 *
 * @param world The world
 * @return The endpoint state, NULL when the world has none
 */
HttpEndpoint* box_http_endpoint(
    ecs_world_t *world);

/** Get the running command server.
 *
 * @param world The world
 * @return The server, NULL when no port is set
 */
ecs_http_server_t* box_http_server(
    ecs_world_t *world);

/** Serve one request of the command endpoints.
 *
 * @param req The request
 * @param reply The reply
 * @param ctx The world
 * @return Always true, every endpoint sets its own status code
 */
bool box_http_endpoint_request(
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply,
    void *ctx);

/** Resolve the camera of a capture request.
 *
 * Without a path the camera is the active camera, the camera that the extract
 * system renders. Paths resolve against the working entity of the session.
 *
 * @param world The world
 * @param session The session entity
 * @param path The camera path, NULL or empty for the active camera
 * @param out Receives the camera entity, 0 on failure
 * @return NULL on success, a one line error without prefix the caller frees
 */
char* box_capture_camera(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    ecs_entity_t *out);

/** Build the path of a capture file in the resources folder of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param file Name of the file in the resources folder
 * @return The path, the caller frees it
 */
char* box_capture_path(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *file);

/** Request a capture of a camera to a file.
 *
 * The path is handed to the Capture component, which owns it until the extract
 * system consumes the request. A pending request of the camera is replaced.
 *
 * @param world The world
 * @param camera The camera entity
 * @param path The file the frame is written to
 */
void box_capture_request(
    ecs_world_t *world,
    ecs_entity_t camera,
    char *path);

#endif
