/**
 * @file command/command_script.c
 * @brief Runs the command lines of a file on a terminal session.
 */

#include "command_internal.h"
#include <string.h>

/** A command file to run, collected before the lines change the world */
typedef struct box_command_script_job_t {
    ecs_entity_t session;  /**< The session entity */
    char *path;            /**< The command file */
} box_command_script_job_t;

/** Read a file into a string.
 *
 * @param path The file, relative paths resolve against the working directory
 * @return The contents, NULL when the file cannot be read, the caller frees it
 */
static char* box_command_script_read(
    const char *path)
{
    char *result = NULL;
    FILE *file = fopen(path, "r");
    if (!file) {
        goto done;
    }

    int32_t size = 0, cap = 4096;
    result = ecs_os_malloc(cap + 1);

    size_t read;
    while ((read = fread(&result[size], 1, (size_t)(cap - size), file))) {
        size += (int32_t)read;
        if (size == cap) {
            cap *= 2;
            result = ecs_os_realloc(result, cap + 1);
        }
    }

    result[size] = '\0';
    fclose(file);

done:
    return result;
}

/** Log an error and publish it as the output of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param message The message, the function takes ownership of it
 */
static void box_command_script_fail(
    ecs_world_t *world,
    ecs_entity_t session,
    char *message)
{
    ecs_err("%s", message);

    TerminalOutput *out = ecs_ensure(world, session, TerminalOutput);
    ecs_os_free(out->result);
    out->result = message;
    out->error = true;
    ecs_modified(world, session, TerminalOutput);
}

/** Test whether a line leaves a quoted argument open.
 *
 * @param line The line
 * @return true when the line holds an odd number of double quotes
 */
static bool box_command_script_quoted(
    const char *line)
{
    bool result = false;
    const char *p;
    for (p = line; p[0]; p ++) {
        if (p[0] == '"') {
            result = !result;
        }
    }

    return result;
}

/** Run the command line that a buffer holds and empty the buffer.
 *
 * @param world The world
 * @param session The session entity
 * @param buf The buffer with the command line
 */
static void box_command_script_flush(
    ecs_world_t *world,
    ecs_entity_t session,
    ecs_strbuf_t *buf)
{
    char *line = ecs_strbuf_get(buf);
    if (line) {
        ecs_os_free(box_command_run(world, session, line, NULL));
        ecs_os_free(line);
    }
}

/** Run every command line of a command file on a session.
 *
 * Blank lines and lines that start with a # are skipped. A line that leaves a
 * quoted argument open continues on the next line, which is kept as it is.
 *
 * @param world The world
 * @param session The session entity
 * @param code The file contents, split in place
 */
static void box_command_script_exec(
    ecs_world_t *world,
    ecs_entity_t session,
    char *code)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    bool open = false, pending = false;
    char *line = code;
    while (line) {
        char *next = strchr(line, '\n');
        if (next) {
            next[0] = '\0';
            next ++;
        }

        size_t length = strlen(line);
        if (length && line[length - 1] == '\r') {
            line[length - 1] = '\0';
        }

        bool append = open;
        if (!open) {
            while (line[0] == ' ' || line[0] == '\t') {
                line ++;
            }

            append = line[0] && line[0] != '#';
        }

        if (append) {
            if (pending) {
                ecs_strbuf_appendch(&buf, '\n');
            }

            ecs_strbuf_appendstr(&buf, line);
            pending = true;
            if (box_command_script_quoted(line)) {
                open = !open;
            }
        }

        if (pending && !open) {
            box_command_script_flush(world, session, &buf);
            pending = false;
        }

        line = next;
    }

    if (pending) {
        box_command_script_flush(world, session, &buf);
    }
}

/** Run the command file of a session, publishing an error when it is missing.
 *
 * @param world The world
 * @param session The session entity
 * @param path The command file
 */
static void box_command_script_load(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path)
{
    char *code = NULL;
    if (!ecs_has(world, session, Terminal)) {
        box_command_script_fail(world, session, flecs_asprintf(
            "command file '%s' is not set on a terminal session", path));
        goto done;
    }

    code = box_command_script_read(path);
    if (!code) {
        box_command_script_fail(world, session, flecs_asprintf(
            "failed to read command file '%s'", path));
        goto done;
    }

    box_command_script_exec(world, session, code);

done:
    ecs_os_free(code);
}

/** CommandScript was set: run the lines of its file on the session.
 *
 * @param it Iterator over the sessions with a new command file
 */
static void box_command_script_on_set(
    ecs_iter_t *it)
{
    CommandScript *script = ecs_field(it, CommandScript, 0);
    ecs_vec_t jobs;
    ecs_vec_init_t(NULL, &jobs, box_command_script_job_t, it->count);

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_command_script_job_t *job =
            ecs_vec_append_t(NULL, &jobs, box_command_script_job_t);
        job->session = it->entities[i];
        job->path = ecs_os_strdup(script[i].path ? script[i].path : "");
    }

    for (i = 0; i < ecs_vec_count(&jobs); i ++) {
        box_command_script_job_t *job =
            ecs_vec_get_t(&jobs, box_command_script_job_t, i);
        box_command_script_load(it->world, job->session, job->path);
        ecs_os_free(job->path);
    }

    ecs_vec_fini_t(NULL, &jobs, box_command_script_job_t);
}

void box_command_script_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(CommandScript) }},
        .events = { EcsOnSet },
        .callback = box_command_script_on_set
    });
}
