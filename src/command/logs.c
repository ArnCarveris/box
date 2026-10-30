/**
 * @file command/logs.c
 * @brief Captures the flecs log output of the process and lists it with the logs command.
 */

#include "command.h"
#include <stdlib.h>
#include <string.h>

/** Log messages the capture keeps */
#define BOX_LOGS_MAX (1024)

/** Messages the logs command lists when no count is given */
#define BOX_LOGS_DEFAULT (20)

/** One captured log message */
typedef struct box_log_entry_t {
    char *msg;          /**< The message without color codes */
    int32_t level;      /**< Flecs log level of the message */
} box_log_entry_t;

/** A log level name and the flecs level it stands for */
typedef struct box_log_level_t {
    const char *name;   /**< Level name */
    int32_t level;      /**< Flecs log level */
} box_log_level_t;

/** Level names, most severe first */
static const box_log_level_t box_log_levels[] = {
    { "fatal", -4 },
    { "error", -3 },
    { "warning", -2 },
    { "info", 0 },
    { "debug", 1 },
    { NULL, 0 }
};

/** Ring of captured messages, shared by every world of the process */
static box_log_entry_t box_log_ring[BOX_LOGS_MAX];

/** Messages captured since the capture was installed */
static int32_t box_log_count;

/** Worlds that use the capture, the hook is removed when the last one is deleted */
static int32_t box_log_users;

/** Guards the ring and the count */
static ecs_os_mutex_t box_log_lock;

/** Log hook that was installed before the capture */
static ecs_os_api_log_t box_log_prev;

/** Get the name of a flecs log level.
 *
 * @param level The flecs log level
 * @return The name
 */
static const char* box_log_level_name(
    int32_t level)
{
    const char *result = "debug";
    if (level <= -4) {
        result = "fatal";
    } else if (level == -3) {
        result = "error";
    } else if (level < 0) {
        result = "warning";
    } else if (level == 0) {
        result = "info";
    }

    return result;
}

/** Copy a message without its ANSI color codes.
 *
 * @param msg The message
 * @return The copy, which the caller frees
 */
static char* box_log_strip(
    const char *msg)
{
    char *result = ecs_os_strdup(msg ? msg : "");
    char *in = result, *out = result;
    while (in[0]) {
        if (in[0] == '\033' && in[1] == '[') {
            in += 2;
            while (in[0] && !((in[0] >= 'a' && in[0] <= 'z') || (in[0] >= 'A' && in[0] <= 'Z'))) {
                in ++;
            }

            in += in[0] ? 1 : 0;
            continue;
        }

        *out++ = *in++;
    }

    out[0] = '\0';

    return result;
}

/** Log hook: store the message in the ring and pass it on to the previous hook.
 *
 * @param level The flecs log level
 * @param file The file that logged
 * @param line The line that logged
 * @param msg The message
 */
static void box_log_hook(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    char *copy = box_log_strip(msg);
    ecs_os_mutex_lock(box_log_lock);
    box_log_entry_t *entry = &box_log_ring[box_log_count % BOX_LOGS_MAX];
    char *old = entry->msg;
    entry->msg = copy;
    entry->level = level;
    box_log_count ++;
    ecs_os_api_log_t prev = box_log_prev;
    ecs_os_mutex_unlock(box_log_lock);

    ecs_os_free(old);
    if (prev) {
        prev(level, file, line, msg);
    }
}

/** Remove the capture when the last world that uses it is deleted.
 *
 * @param world The world
 * @param ctx Unused
 */
static void box_log_fini(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    int32_t i;
    box_log_users --;
    if (box_log_users) {
        goto done;
    }

    if (ecs_os_api.log_ == box_log_hook) {
        ecs_os_api.log_ = box_log_prev;
    }

    box_log_prev = NULL;
    for (i = 0; i < BOX_LOGS_MAX; i ++) {
        ecs_os_free(box_log_ring[i].msg);
        box_log_ring[i].msg = NULL;
    }

    box_log_count = 0;
    ecs_os_mutex_free(box_log_lock);
    box_log_lock = 0;
done:
    return;
}

/** Convert a level name or number to a flecs log level.
 *
 * @param name The name, NULL or empty for every level
 * @param out Receives the level
 * @return 0 on success, -1 when the name is not a level
 */
static int box_log_parse_level(
    const char *name,
    int32_t *out)
{
    char *end = NULL;
    int result = 0;
    int32_t i;
    out[0] = INT32_MAX;
    if (!name || !name[0]) {
        goto done;
    }

    for (i = 0; box_log_levels[i].name; i ++) {
        if (!strcmp(name, box_log_levels[i].name)) {
            out[0] = box_log_levels[i].level;
            goto done;
        }
    }

    out[0] = (int32_t)strtol(name, &end, 10);
    if (end == name || end[0]) {
        result = -1;
    }

done:
    return result;
}

/** The logs command: list the last count messages at a level or more severe.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The level and the count
 * @param result Receives the output
 */
static void box_log_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx;
    const char *name = argc > 0 ? *(char**)argv[0].ptr : NULL;
    int32_t count = argc > 1 ? *(ecs_i32_t*)argv[1].ptr : 0;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t level, i, first, matched = 0;
    if (box_log_parse_level(name, &level)) {
        ecs_err("unknown log level '%s'", name);
        goto done;
    }

    count = count > 0 ? count : BOX_LOGS_DEFAULT;
    ecs_os_mutex_lock(box_log_lock);
    first = box_log_count > BOX_LOGS_MAX ? box_log_count - BOX_LOGS_MAX : 0;
    for (i = box_log_count - 1; i >= first && matched < count; i --) {
        matched += box_log_ring[i % BOX_LOGS_MAX].level <= level;
    }

    for (i ++; i < box_log_count; i ++) {
        const box_log_entry_t *e = &box_log_ring[i % BOX_LOGS_MAX];
        if (e->level <= level) {
            ecs_strbuf_append(&buf, "%s%s: %s", ecs_strbuf_written(&buf) ? "\n" : "",
                box_log_level_name(e->level), e->msg);
        }
    }

    ecs_os_mutex_unlock(box_log_lock);
    /* An empty string, not NULL, without messages: the shell returns a NULL string as null */
    *(char**)result->ptr = ecs_strbuf_written(&buf) ?
        ecs_strbuf_get(&buf) : ecs_os_strdup("");
done:
    return;
}

void box_logs_init(
    ecs_world_t *world)
{
    if (!box_log_users) {
        box_log_lock = ecs_os_mutex_new();
        box_log_prev = ecs_os_api.log_;
        ecs_os_api.log_ = box_log_hook;
    }

    box_log_users ++;
    ecs_atfini(world, box_log_fini, NULL);

    box_command_register(world, "Show the last count log messages at level or more severe",
        (ecs_function_desc_t){
            .name = "logs",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "level", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "fatal, error, warning, info, debug or a number (default: all)" },
                { .name = "count", .type = ecs_id(ecs_i32_t), .optional = true,
                  .brief = "Messages to show (default: 20)" }
            },
            .callback = box_log_command
        });
}
