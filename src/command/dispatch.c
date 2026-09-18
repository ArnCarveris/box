/**
 * @file command/dispatch.c
 * @brief Splits command lines, converts the arguments and calls the command.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** Storage of one converted argument */
typedef union box_command_arg_t {
    char *str;            /**< Value of a string parameter */
    ecs_i32_t i32;        /**< Value of an i32 parameter */
    ecs_f64_t f64;        /**< Value of an f64 parameter */
    ecs_entity_t entity;  /**< Value of an entity parameter */
} box_command_arg_t;

/** A line to run, collected before the commands change the world */
typedef struct box_command_job_t {
    ecs_entity_t session;  /**< The session entity */
    char *line;            /**< The command line */
} box_command_job_t;

/** Split a line into a command name and its whitespace separated arguments.
 *
 * Double quotes group a single argument, the quotes are not part of it.
 *
 * @param buffer A writable copy of the line, holds the tokens
 * @param tokens Array receiving the tokens
 * @param max Size of the token array
 * @return Number of tokens
 */
static int32_t box_dispatch_split(
    char *buffer,
    char **tokens,
    int32_t max)
{
    int32_t count = 0;
    char *p = buffer;
    while (p[0] && count < max) {
        while (p[0] == ' ' || p[0] == '\t') {
            p ++;
        }

        if (!p[0]) {
            break;
        }

        char *token = p;
        if (p[0] == '"') {
            p ++;
            token = p;
            while (p[0] && p[0] != '"') {
                p ++;
            }
        } else {
            while (p[0] && p[0] != ' ' && p[0] != '\t') {
                p ++;
            }
        }

        if (p[0]) {
            p[0] = '\0';
            p ++;
        }

        tokens[count] = token;
        count ++;
    }

    return count;
}

/** Convert one argument to the type of its parameter.
 *
 * @param world The world
 * @param cwd Scope that entity arguments resolve against
 * @param param The parameter
 * @param token The argument, NULL when it was omitted
 * @param out Storage receiving the value
 * @param error Receives a one line error message on failure
 * @return 0 on success, -1 when the argument could not be converted
 */
static int box_dispatch_convert(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const ecs_script_parameter_t *param,
    const char *token,
    box_command_arg_t *out,
    char **error)
{
    int result = 0;
    if (!token) {
        goto done;
    }

    if (param->type == ecs_id(ecs_string_t)) {
        out->str = ECS_CONST_CAST(char*, token);
    } else if (param->type == ecs_id(ecs_entity_t)) {
        out->entity = ecs_lookup_path_w_sep(world, cwd, token, ".", NULL, true);
        if (!out->entity) {
            error[0] = flecs_asprintf(
                "argument '%s': unknown entity '%s'", param->name, token);
            result = -1;
        }
    } else {
        char *end = NULL;
        if (param->type == ecs_id(ecs_i32_t)) {
            out->i32 = (ecs_i32_t)strtol(token, &end, 10);
        } else if (param->type == ecs_id(ecs_f64_t)) {
            out->f64 = (ecs_f64_t)strtod(token, &end);
        } else {
            ecs_abort(ECS_INVALID_PARAMETER, "unsupported command parameter type");
        }

        if (end == token || end[0]) {
            error[0] = flecs_asprintf("argument '%s': invalid %s value '%s'",
                param->name, ecs_get_name(world, param->type), token);
            result = -1;
        }
    }

done:
    return result;
}

/** Get the scope that entity arguments of a session resolve against.
 *
 * @param world The world
 * @param session The session entity
 * @return The scope entity, 0 for the root
 */
static ecs_entity_t box_dispatch_cwd(
    ecs_world_t *world,
    ecs_entity_t session)
{
    const Terminal *t = ecs_get(world, session, Terminal);
    ecs_assert(t != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");

    ecs_entity_t result = 0;
    if (t->cwd && t->cwd[0]) {
        result = ecs_lookup(world, t->cwd);
    }

    return result;
}

/** Look up the command of a line and call it with the converted arguments.
 *
 * @param world The world
 * @param session The session entity
 * @param tokens The command name followed by its arguments
 * @param count Number of tokens
 * @param error Set to true when the command failed
 * @return The command output, the caller frees it
 */
static char* box_dispatch_call(
    ecs_world_t *world,
    ecs_entity_t session,
    char **tokens,
    int32_t count,
    bool *error)
{
    char *result = NULL;
    error[0] = true;

    ecs_entity_t cmd = ecs_lookup_child(world, box_command_scope(world), tokens[0]);
    if (!cmd) {
        result = flecs_asprintf("unknown command '%s'", tokens[0]);
        goto done;
    }

    const EcsScriptFunction *f = ecs_get(world, cmd, EcsScriptFunction);
    const CommandInfo *info = ecs_get(world, cmd, CommandInfo);
    ecs_assert(f != NULL, ECS_INVALID_OPERATION, "command is not a function");
    ecs_assert(info != NULL, ECS_INVALID_OPERATION, "command without parameter info");

    int32_t argc = ecs_vec_count(&f->params);
    int32_t provided = count - 1;
    if (provided < info->required || provided > argc - 1) {
        if (info->required == argc - 1) {
            result = flecs_asprintf("command '%s' takes %d arguments, got %d",
                tokens[0], info->required, provided);
        } else {
            result = flecs_asprintf("command '%s' takes %d to %d arguments, got %d",
                tokens[0], info->required, argc - 1, provided);
        }

        goto done;
    }

    box_command_arg_t storage[FLECS_SCRIPT_FUNCTION_ARGS_MAX];
    ecs_value_t argv[FLECS_SCRIPT_FUNCTION_ARGS_MAX];
    const ecs_script_parameter_t *params = ecs_vec_first(&f->params);
    ecs_entity_t cwd = box_dispatch_cwd(world, session);
    ecs_os_memset(storage, 0, ECS_SIZEOF(storage));
    argv[0] = (ecs_value_t){ .type = ecs_id(ecs_entity_t), .ptr = &session };

    int32_t i;
    for (i = 1; i < argc; i ++) {
        const char *token = i <= provided ? tokens[i] : NULL;
        if (box_dispatch_convert(
            world, cwd, &params[i], token, &storage[i], &result))
        {
            goto done;
        }

        argv[i] = (ecs_value_t){ .type = params[i].type, .ptr = &storage[i] };
    }

    char *out = NULL;
    ecs_value_t value = { .type = ecs_id(ecs_string_t), .ptr = &out };
    if (ecs_function_call(world, cmd, argc, argv, &value)) {
        ecs_os_free(out);
        result = flecs_asprintf("command '%s' failed", tokens[0]);
        goto done;
    }

    error[0] = false;
    result = out;

done:
    return result;
}

/** Run a command line on a session, log it and publish the output.
 *
 * @param world The world
 * @param session The session entity
 * @param line The command line
 */
static void box_dispatch_exec(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line)
{
    char *tokens[FLECS_SCRIPT_FUNCTION_ARGS_MAX];
    char *buffer = ecs_os_strdup(line);
    int32_t count = box_dispatch_split(
        buffer, tokens, FLECS_SCRIPT_FUNCTION_ARGS_MAX);

    bool error = false;
    char *result = NULL;
    if (count) {
        result = box_dispatch_call(world, session, tokens, count, &error);
    }

    if (!result) {
        result = ecs_os_strdup("");
    }

    ecs_os_free(buffer);
    box_terminal_log(world, session, line, result, error);

    TerminalOutput *out = ecs_ensure(world, session, TerminalOutput);
    ecs_os_free(out->result);
    out->result = result;
    out->error = error;
    ecs_modified(world, session, TerminalOutput);
}

/** TerminalInput was set: run the line on its session.
 *
 * @param it Iterator over the sessions with new input
 */
static void box_dispatch_on_set(
    ecs_iter_t *it)
{
    TerminalInput *in = ecs_field(it, TerminalInput, 0);
    ecs_vec_t jobs;
    ecs_vec_init_t(NULL, &jobs, box_command_job_t, it->count);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_command_job_t *job = ecs_vec_append_t(NULL, &jobs, box_command_job_t);
        job->session = it->entities[i];
        job->line = ecs_os_strdup(in[i].line ? in[i].line : "");
    }

    for (i = 0; i < ecs_vec_count(&jobs); i ++) {
        box_command_job_t *job = ecs_vec_get_t(&jobs, box_command_job_t, i);
        box_dispatch_exec(it->world, job->session, job->line);
        ecs_os_free(job->line);
    }

    ecs_vec_fini_t(NULL, &jobs, box_command_job_t);
}

char* box_command_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error)
{
    TerminalInput *in = ecs_ensure(world, session, TerminalInput);
    ecs_os_free(in->line);
    in->line = ecs_os_strdup(line);
    ecs_modified(world, session, TerminalInput);

    const TerminalOutput *out = ecs_get(world, session, TerminalOutput);
    ecs_assert(out != NULL, ECS_INVALID_OPERATION, "command produced no output");
    if (error) {
        error[0] = out->error;
    }

    return ecs_os_strdup(out->result ? out->result : "");
}

void box_dispatch_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TerminalInput) }},
        .events = { EcsOnSet },
        .callback = box_dispatch_on_set
    });
}
