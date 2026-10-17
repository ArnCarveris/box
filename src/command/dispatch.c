/**
 * @file command/dispatch.c
 * @brief Splits a command line, converts its arguments and calls the command function.
 */

#include "command.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/** Most tokens of a command line: the command name and one per parameter */
#define BOX_DISPATCH_TOKENS (FLECS_SCRIPT_FUNCTION_ARGS_MAX)

/** Storage of one converted argument */
typedef union box_command_arg_t {
    char *str;              /**< Value of a string parameter */
    ecs_i32_t i32;          /**< Value of an i32 parameter */
    ecs_f64_t f64;          /**< Value of an f64 parameter */
    ecs_bool_t b;           /**< Value of a bool parameter */
    ecs_entity_t entity;    /**< Value of an entity parameter */
} box_command_arg_t;

/** Outcome of a command call */
typedef struct box_command_status_t {
    bool quiet;     /**< The command does not log its result */
    bool failed;    /**< The command failed */
} box_command_status_t;

/** A command line split into tokens */
typedef struct box_command_line_t {
    char *buffer;                           /**< Copy of the line that holds the tokens */
    char *tokens[BOX_DISPATCH_TOKENS];      /**< The tokens, unquoted */
    int32_t offsets[BOX_DISPATCH_TOKENS];   /**< Offset of each token in the line */
    int32_t count;                          /**< Number of tokens */
} box_command_line_t;

/** Nesting change of a character: 1 for an opening bracket, -1 for a closing one.
 *
 * @param c The character
 * @return The change of nesting depth
 */
static int32_t box_dispatch_bracket(
    char c)
{
    int32_t result = 0;
    if (c == '(' || c == '{' || c == '[') {
        result = 1;
    } else if (c == ')' || c == '}' || c == ']') {
        result = -1;
    }

    return result;
}

/** Test whether a character separates tokens.
 *
 * @param c The character
 * @return Whether it is a space, tab or line break
 */
static bool box_dispatch_space(
    char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/** Find the next token of a line.
 *
 * Double quotes group a token, a backslash escapes the next character in it and a quote that
 * is not closed runs to the end of the line. Brackets keep their contents together.
 *
 * @param line The command line
 * @param at Byte to search from
 * @param out Receives the range of the token
 * @return Whether there is a token
 */
static bool box_dispatch_scan(
    const char *line,
    int32_t at,
    box_command_token_t *out)
{
    int32_t depth = 0;
    while (box_dispatch_space(line[at])) {
        at ++;
    }

    out->start = at;
    out->quoted = line[at] == '"';
    if (out->quoted) {
        at ++;
        while (line[at] && line[at] != '"') {
            at += line[at] == '\\' && line[at + 1] ? 2 : 1;
        }

        at += line[at] != '\0';
    } else {
        while (line[at] && (depth > 0 || !box_dispatch_space(line[at]))) {
            depth += box_dispatch_bracket(line[at]);
            at ++;
        }
    }

    out->end = at;

    return out->end > out->start;
}

int32_t box_command_tokens(
    const char *line,
    box_command_token_t *tokens,
    int32_t max)
{
    box_command_token_t t;
    int32_t result = 0, at = 0;
    while (box_dispatch_scan(line, at, &t)) {
        if (result < max) {
            tokens[result] = t;
        }

        result ++;
        at = t.end;
    }

    return result;
}

/** Write the unquoted text of a token, removing the quotes and the backslash escapes.
 *
 * @param line The command line
 * @param t The token
 * @param out Receives the text and a terminator, at least t->end - t->start + 1 bytes
 */
static void box_dispatch_unquote(
    const char *line,
    const box_command_token_t *t,
    char *out)
{
    int32_t at = t->start + t->quoted;
    while (at < t->end && !(t->quoted && line[at] == '"')) {
        if (t->quoted && line[at] == '\\' && at + 1 < t->end) {
            at ++;
        }

        out[0] = line[at];
        out ++;
        at ++;
    }

    out[0] = '\0';
}

char* box_command_token_text(
    const char *line,
    const box_command_token_t *t)
{
    char *result = ecs_os_malloc(t->end - t->start + 1);
    box_dispatch_unquote(line, t, result);

    return result;
}

/** Split a line into its tokens, unquoted, as box_command_tokens finds them.
 *
 * @param line The command line
 * @param out Receives the tokens
 */
static void box_dispatch_split(
    const char *line,
    box_command_line_t *out)
{
    box_command_token_t t;
    int32_t at = 0;
    out->buffer = ecs_os_strdup(line);
    out->count = 0;
    while (out->count < BOX_DISPATCH_TOKENS && box_dispatch_scan(line, at, &t)) {
        char *token = &out->buffer[t.start];
        box_dispatch_unquote(line, &t, token);
        out->offsets[out->count] = t.start;
        out->tokens[out->count] = token;
        out->count ++;
        at = t.end;
    }
}

int32_t box_command_rest_end(
    const char *line)
{
    int32_t result = ecs_os_strlen(line);
    while (result && box_dispatch_space(line[result - 1])) {
        result --;
    }

    return result;
}

/** Copy the remainder of a line from a token on, without trailing whitespace.
 *
 * @param line The command line
 * @param offset Offset of the first token of the remainder
 * @return The remainder, which the caller frees
 */
static char* box_dispatch_rest(
    const char *line,
    int32_t offset)
{
    int32_t len = box_command_rest_end(line) - offset;
    len = len > 0 ? len : 0;
    char *result = ecs_os_malloc(len + 1);
    ecs_os_memcpy(result, line + offset, len);
    result[len] = '\0';

    return result;
}

/** Convert a number or bool argument.
 *
 * @param world The world
 * @param param The parameter
 * @param token The argument
 * @param out Receives the value
 * @return NULL on success, an error message the caller frees otherwise
 */
static char* box_dispatch_scalar(
    ecs_world_t *world,
    const ecs_script_parameter_t *param,
    const char *token,
    box_command_arg_t *out)
{
    char *end = NULL, *result = NULL;
    long long i = 0;
    double f = 0;
    errno = 0;
    if (param->type == ecs_id(ecs_i32_t)) {
        i = strtoll(token, &end, 10);
    } else if (param->type == ecs_id(ecs_f64_t)) {
        f = strtod(token, &end);
    } else if (!strcmp(token, "true") || !strcmp(token, "1") || !strcmp(token, "on")) {
        out->b = true;
    } else if (!strcmp(token, "false") || !strcmp(token, "0") || !strcmp(token, "off")) {
        out->b = false;
    } else {
        end = ECS_CONST_CAST(char*, token);
    }

    if (end && (end == token || end[0])) {
        result = flecs_asprintf(BOX_COMMAND_ERROR "%s: invalid %s '%s'",
            param->name, ecs_get_name(world, param->type), token);
    } else if (param->type == ecs_id(ecs_i32_t) &&
        (errno == ERANGE || i < INT32_MIN || i > INT32_MAX))
    {
        result = flecs_asprintf(BOX_COMMAND_ERROR "%s: %s is out of range, %d to %d",
            param->name, token, INT32_MIN, INT32_MAX);
    } else if (param->type == ecs_id(ecs_f64_t) && !isfinite(f)) {
        result = flecs_asprintf(BOX_COMMAND_ERROR "%s: %s is not a finite number",
            param->name, token);
    } else if (param->type == ecs_id(ecs_i32_t)) {
        out->i32 = (ecs_i32_t)i;
    } else if (param->type == ecs_id(ecs_f64_t)) {
        out->f64 = f;
    }

    return result;
}

/** Convert one argument to the type of its parameter.
 *
 * @param world The world
 * @param session The session entity
 * @param param The parameter
 * @param token The argument, NULL when it was omitted
 * @param out Receives the value
 * @return NULL on success, an error message the caller frees otherwise
 */
static char* box_dispatch_convert(
    ecs_world_t *world,
    ecs_entity_t session,
    const ecs_script_parameter_t *param,
    char *token,
    box_command_arg_t *out)
{
    char *result = NULL;
    if (!token) {
        goto done;
    }

    if (param->type == ecs_id(ecs_string_t)) {
        out->str = token;
    } else if (param->type == ecs_id(ecs_entity_t)) {
        ecs_entity_t cwd = box_terminal_cwd(world, session);
        out->entity = ecs_lookup_path_w_sep(world, cwd, token, ".", NULL, true);
        if (!out->entity) {
            result = flecs_asprintf(
                BOX_COMMAND_ERROR "%s: no entity '%s'", param->name, token);
        }
    } else {
        result = box_dispatch_scalar(world, param, token, out);
    }

done:
    return result;
}

/** Check the argument count of a command.
 *
 * @param name The command name
 * @param info The parameter rules
 * @param params Number of parameters after the session
 * @param provided Number of arguments on the line
 * @return NULL when the count is valid, an error message the caller frees otherwise
 */
static char* box_dispatch_arity(
    const char *name,
    const CommandInfo *info,
    int32_t params,
    int32_t provided)
{
    char *result = NULL;
    if (provided >= info->required && (provided <= params || info->rest)) {
        goto done;
    }

    if (info->required == params) {
        result = flecs_asprintf(BOX_COMMAND_ERROR "%s takes %d argument%s, got %d",
            name, params, params == 1 ? "" : "s", provided);
    } else {
        result = flecs_asprintf(BOX_COMMAND_ERROR "%s takes %d to %d arguments, got %d",
            name, info->required, params, provided);
    }

done:
    return result;
}

/** Look up the command of a line and call it with the converted arguments.
 *
 * @param world The world
 * @param session The session entity
 * @param line The command line
 * @param cl The tokens of the line
 * @param st Receives whether the command is quiet and whether it failed
 * @return The command output, which the caller frees
 */
static char* box_dispatch_call(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    box_command_line_t *cl,
    box_command_status_t *st)
{
    box_terminal_host_t *host = box_terminal_host(world);
    ecs_entity_t scope = ecs_lookup(world, BOX_COMMAND_SCOPE);
    ecs_entity_t cmd = ecs_lookup_path_w_sep(world, scope, cl->tokens[0], ".", NULL, false);
    const EcsScriptFunction *f = cmd ? ecs_get(world, cmd, EcsScriptFunction) : NULL;
    const CommandInfo *info = cmd ? ecs_get(world, cmd, CommandInfo) : NULL;
    const ecs_script_parameter_t *params;
    box_command_arg_t storage[FLECS_SCRIPT_FUNCTION_ARGS_MAX] = {{0}};
    ecs_value_t argv[FLECS_SCRIPT_FUNCTION_ARGS_MAX];
    ecs_value_t out = { .type = ecs_id(ecs_string_t) };
    char *rest = NULL, *result = NULL;
    int32_t i, argc, provided = cl->count - 1;
    bool failed = host->failed;
    st->failed = true;
    if (!f || !info) {
        result = flecs_asprintf(
            BOX_COMMAND_ERROR "unknown command '%s', see help", cl->tokens[0]);
        goto done;
    }

    st->quiet = info->quiet;
    argc = ecs_vec_count(&f->params);
    result = box_dispatch_arity(cl->tokens[0], info, argc - 1, provided);
    if (result) {
        goto done;
    }

    params = ecs_vec_first(&f->params);
    argv[0] = (ecs_value_t){ .type = ecs_id(ecs_entity_t), .ptr = &session };
    for (i = 1; i < argc; i ++) {
        char *token = i <= provided ? cl->tokens[i] : NULL;
        if (token && info->rest && i == argc - 1 && i < cl->count - 1) {
            token = rest = box_dispatch_rest(line, cl->offsets[i]);
        }

        result = box_dispatch_convert(world, session, &params[i], token, &storage[i]);
        if (result) {
            goto done;
        }

        argv[i] = (ecs_value_t){ .type = params[i].type, .ptr = &storage[i] };
    }

    out.ptr = &result;
    host->failed = false;
    if (ecs_function_call(world, cmd, argc, argv, &out)) {
        ecs_os_free(result);
        result = flecs_asprintf(BOX_COMMAND_ERROR "%s failed", cl->tokens[0]);
        host->failed = true;
    }

    st->failed = host->failed;
    host->failed = failed;
done:
    ecs_os_free(rest);

    return result;
}

void box_command_error(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const char *fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    char *msg = flecs_vasprintf(fmt, args);
    va_end(args);

    char **out = result->ptr;
    ecs_os_free(out[0]);
    out[0] = flecs_asprintf(BOX_COMMAND_ERROR "%s", msg);
    ecs_os_free(msg);
    box_terminal_host_t *host = box_terminal_host(ctx->world);
    if (host) {
        host->failed = true;
    }
}

char* box_terminal_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error)
{
    box_command_line_t cl = {0};
    box_command_status_t st = { .failed = true };
    char *result = NULL;
    if (!box_terminal_host(world)) {
        result = ecs_os_strdup(BOX_COMMAND_ERROR BOX_COMMAND_NOT_IMPORTED);
        goto done;
    }

    if (!ecs_is_alive(world, session) || !ecs_has(world, session, TerminalSession)) {
        result = ecs_os_strdup(BOX_COMMAND_ERROR "not a terminal session");
        goto done;
    }

    st.failed = false;
    box_dispatch_split(line ? line : "", &cl);
    if (!cl.count) {
        goto done;
    }

    result = box_dispatch_call(world, session, line, &cl, &st);
    if (!result) {
        result = ecs_os_strdup("");
    }

    if (ecs_is_alive(world, session)) {
        box_terminal_log(world, session, line,
            st.quiet && !st.failed ? NULL : result, st.failed);
    }

done:
    ecs_os_free(cl.buffer);
    if (!result) {
        result = ecs_os_strdup("");
    }

    if (error) {
        error[0] = st.failed;
    }

    return result;
}
