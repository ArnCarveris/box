/**
 * @file command/logs.c
 * @brief Captures flecs log output in a ring buffer and lists it with logs.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** Log messages kept in the ring */
#define BOX_LOG_MAX (1024)

/** Log messages listed when no count is given */
#define BOX_LOG_DEFAULT (50)

/** Level listed when no level is given */
#define BOX_LOG_LEVEL (-2)

/** One captured log message */
typedef struct box_log_entry_t {
    char *message;  /**< The formatted message */
    int32_t level;  /**< Flecs level the message was logged at */
} box_log_entry_t;

/** Captured log output, shared between the world and the log hook */
typedef struct box_log_buffer_t {
    box_log_entry_t ring[BOX_LOG_MAX];  /**< The captured messages */
    int32_t count;                      /**< Messages captured so far */
    FILE *file;                         /**< Open log.txt in the process folder */
} box_log_buffer_t;

/** Singleton that owns the captured log output */
typedef struct LogBuffer {
    box_log_buffer_t *buffer;  /**< Heap state that the log hook appends to */
} LogBuffer;

ECS_COMPONENT_DECLARE(LogBuffer);

/** Buffer the log hook appends to, NULL when no world captures */
static box_log_buffer_t *box_log_active;

/** Guards box_log_active and the buffer it points to */
static ecs_os_mutex_t box_log_lock;

/** Log hook that was installed before the module took over */
static ecs_os_api_log_t box_log_prev;

/** Name of a flecs log level.
 *
 * @param level The level
 * @return The level name
 */
static const char* box_log_level_name(
    int32_t level)
{
    const char *result = "debug";
    if (level <= -4) {
        result = "fatal";
    } else if (level == -3) {
        result = "error";
    } else if (level <= -1) {
        result = "warning";
    } else if (level == 0) {
        result = "info";
    }

    return result;
}

/** Log hook: append the message to the ring and forward it to the old hook.
 *
 * @param level The flecs log level
 * @param file The file that logged the message
 * @param line The line that logged the message
 * @param msg The formatted message
 */
static void box_log_hook(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    ecs_os_mutex_lock(box_log_lock);
    box_log_buffer_t *buffer = box_log_active;
    if (buffer) {
        box_log_entry_t *entry =
            &buffer->ring[box_ring_slot(buffer->count, BOX_LOG_MAX)];
        ecs_os_free(entry->message);
        entry->message = ecs_os_strdup(msg ? msg : "");
        entry->level = level;
        buffer->count ++;
        if (buffer->file) {
            fprintf(buffer->file, "%s\t%s\n",
                box_log_level_name(level), entry->message);
            fflush(buffer->file);
        }
    }

    ecs_os_api_log_t prev = box_log_prev;
    ecs_os_mutex_unlock(box_log_lock);

    if (prev) {
        prev(level, file, line, msg);
    }
}

/** Uninstall the log hook, close log.txt and free the captured messages.
 *
 * @param buffer The capture state
 */
static void box_log_buffer_fini(
    box_log_buffer_t *buffer)
{
    if (!buffer) {
        goto done;
    }

    ecs_os_mutex_lock(box_log_lock);
    if (box_log_active == buffer) {
        box_log_active = NULL;
    }

    if (ecs_os_api.log_ == box_log_hook) {
        ecs_os_api.log_ = box_log_prev;
        box_log_prev = NULL;
    }

    ecs_os_mutex_unlock(box_log_lock);

    if (buffer->file) {
        fclose(buffer->file);
    }

    int32_t i;
    for (i = 0; i < BOX_LOG_MAX; i ++) {
        ecs_os_free(buffer->ring[i].message);
    }

    ecs_os_free(buffer);
    ecs_os_mutex_free(box_log_lock);
    box_log_lock = 0;
done:
    return;
}

/** LogBuffer destructor: stop capturing and release the capture state.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_log_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    LogBuffer *log = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_log_buffer_fini(log[i].buffer);
        log[i].buffer = NULL;
    }
}

/** Open log.txt in the process folder.
 *
 * @param world The world
 * @return The open file, NULL when it could not be created
 */
static FILE* box_log_open(
    ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);
    ecs_assert(app != NULL, ECS_INVALID_OPERATION, "import box.core before box.command");
    ecs_assert(app->process_dir != NULL, ECS_INVALID_OPERATION, "no process folder");

    char *path = flecs_asprintf("%s/log.txt", app->process_dir);
    FILE *result = fopen(path, "w");
    if (!result) {
        ecs_warn("failed to open log file %s", path);
    }

    ecs_os_free(path);

    return result;
}

/** Convert a level name or a flecs level number to a level.
 *
 * @param name The level name, NULL or empty for the default
 * @param out Receives the level
 * @return 0 on success, -1 when the name is not a level
 */
static int box_log_parse_level(
    const char *name,
    int32_t *out)
{
    int result = 0;
    out[0] = BOX_LOG_LEVEL;
    if (!name || !name[0]) {
        goto done;
    }

    if (!strcmp(name, "fatal")) {
        out[0] = -4;
    } else if (!strcmp(name, "error")) {
        out[0] = -3;
    } else if (!strcmp(name, "warning")) {
        out[0] = -2;
    } else if (!strcmp(name, "info")) {
        out[0] = 0;
    } else if (!strcmp(name, "debug")) {
        out[0] = 1;
    } else {
        char *end = NULL;
        out[0] = (int32_t)strtol(name, &end, 10);
        if (end == name || end[0]) {
            result = -1;
        }
    }

done:
    return result;
}

/** Render the last count captured messages at a level or more severe.
 *
 * @param buffer The capture state
 * @param level Least severe level to include
 * @param count Maximum number of messages
 * @return The rendered lines, oldest first, the caller frees it
 */
static char* box_log_collect(
    box_log_buffer_t *buffer,
    int32_t level,
    int32_t count)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t matches;
    ecs_vec_init_t(NULL, &matches, int32_t, 0);
    ecs_os_mutex_lock(box_log_lock);

    int32_t i = box_ring_begin(buffer->count, BOX_LOG_MAX, BOX_LOG_MAX);
    for (; i < buffer->count; i ++) {
        const box_log_entry_t *entry =
            &buffer->ring[box_ring_slot(i, BOX_LOG_MAX)];
        if (entry->message && entry->level <= level) {
            int32_t *slot = ecs_vec_append_t(NULL, &matches, int32_t);
            slot[0] = i;
        }
    }

    int32_t first = box_ring_begin(ecs_vec_count(&matches), count, BOX_LOG_MAX);
    for (i = first; i < ecs_vec_count(&matches); i ++) {
        int32_t index = ecs_vec_get_t(&matches, int32_t, i)[0];
        const box_log_entry_t *entry =
            &buffer->ring[box_ring_slot(index, BOX_LOG_MAX)];
        ecs_strbuf_append(&buf, "%s\t%s\n",
            box_log_level_name(entry->level), entry->message);
    }

    ecs_os_mutex_unlock(box_log_lock);
    ecs_vec_fini_t(NULL, &matches, int32_t);

    return ecs_strbuf_get(&buf);
}

/** The logs command: list recent messages at a level or more severe.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_log_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *name = *(char**)argv[1].ptr;
    int32_t count = *(ecs_i32_t*)argv[2].ptr;
    int32_t level = BOX_LOG_LEVEL;
    char *out = NULL;
    if (box_log_parse_level(name, &level)) {
        out = flecs_asprintf("unknown log level '%s'", name);
        goto done;
    }

    if (count <= 0) {
        count = BOX_LOG_DEFAULT;
    }

    const LogBuffer *log = ecs_singleton_get(ctx->world, LogBuffer);
    ecs_assert(log != NULL, ECS_INVALID_OPERATION, "log capture is not installed");
    out = box_log_collect(log->buffer, level, count);
done:
    *(char**)result->ptr = out;
}

void box_logs_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, LogBuffer);
    ecs_set_hooks(world, LogBuffer, {
        .ctor = flecs_default_ctor,
        .dtor = box_log_dtor
    });

    box_log_buffer_t *buffer = ecs_os_calloc_t(box_log_buffer_t);
    buffer->file = box_log_open(world);
    if (!box_log_lock) {
        box_log_lock = ecs_os_mutex_new();
    }

    ecs_os_mutex_lock(box_log_lock);
    ecs_assert(box_log_active == NULL, ECS_INVALID_OPERATION,
        "only one world can capture log output");
    box_log_active = buffer;
    if (ecs_os_api.log_ != box_log_hook) {
        box_log_prev = ecs_os_api.log_;
        ecs_os_api.log_ = box_log_hook;
    }

    ecs_os_mutex_unlock(box_log_lock);
    ecs_singleton_set(world, LogBuffer, { .buffer = buffer });

    box_command_param_t params[] = {{
        .name = "level", .type = ecs_id(ecs_string_t), .optional = true
    }, {
        .name = "count", .type = ecs_id(ecs_i32_t), .optional = true
    }};

    box_command_register(world, "logs",
        "Show recent log messages at a level or more severe", params, 2,
        box_log_command);
}
