/**
 * @file console/syntax.c
 * @brief Colored parts of a command line, from the dispatcher's tokens and parameter types.
 */

#include "console.h"
#include "../command/command.h"
#include <string.h>

/** Add a colored range to the spans of a line.
 *
 * @param spans The box_console_span_t of the line
 * @param start First byte
 * @param end Byte after the range
 * @param kind What the range is
 */
static void box_console_syntax_add(
    ecs_vec_t *spans,
    int32_t start,
    int32_t end,
    box_console_syntax_t kind)
{
    if (end > start) {
        *ecs_vec_append_t(NULL, spans, box_console_span_t) = (box_console_span_t){
            .start = start, .end = end, .kind = kind };
    }
}

/** Test whether a byte continues a word of a value expression.
 *
 * @param c The byte
 * @return Whether it is a letter, digit, underscore, dot or part of a multibyte character
 */
static bool box_console_syntax_word(
    char c)
{
    unsigned char u = (unsigned char)c;

    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
        u == '_' || u == '.' || u >= 0x80;
}

/** Test whether a byte is a decimal digit.
 *
 * @param c The byte
 * @return Whether it is 0 to 9
 */
static bool box_console_syntax_digit(
    char c)
{
    return c >= '0' && c <= '9';
}

/** Find the end of a number of a value expression, with its sign, exponent and suffix.
 *
 * @param line The command line
 * @param at The first byte of the number
 * @param end Byte after the value expression
 * @return The byte after the number
 */
static int32_t box_console_syntax_number(
    const char *line,
    int32_t at,
    int32_t end)
{
    at ++;
    while (at < end && (box_console_syntax_word(line[at]) ||
        ((line[at] == '-' || line[at] == '+') && (line[at - 1] == 'e' || line[at - 1] == 'E'))))
    {
        at ++;
    }

    return at;
}

/** Color the numbers, strings and booleans inside a value expression.
 *
 * @param line The command line
 * @param start First byte of the expression
 * @param end Byte after the expression
 * @param spans The box_console_span_t of the line
 */
static void box_console_syntax_value(
    const char *line,
    int32_t start,
    int32_t end,
    ecs_vec_t *spans)
{
    int32_t at = start, from;
    box_console_syntax_add(spans, start, end, BoxSyntaxValue);
    while (at < end) {
        char c = line[at], next = at + 1 < end ? line[at + 1] : '\0';
        from = at;
        if (c == '"') {
            at ++;
            while (at < end && line[at] != '"') {
                at += line[at] == '\\' && at + 1 < end ? 2 : 1;
            }

            at += at < end;
            box_console_syntax_add(spans, from, at, BoxSyntaxString);
        } else if (box_console_syntax_digit(c) ||
            ((c == '-' || c == '+' || c == '.') && box_console_syntax_digit(next)))
        {
            at = box_console_syntax_number(line, at, end);
            box_console_syntax_add(spans, from, at, BoxSyntaxNumber);
        } else if (box_console_syntax_word(c)) {
            while (at < end && box_console_syntax_word(line[at])) {
                at ++;
            }

            if ((at - from == 4 && !strncmp(&line[from], "true", 4)) ||
                (at - from == 5 && !strncmp(&line[from], "false", 5)))
            {
                box_console_syntax_add(spans, from, at, BoxSyntaxBoolean);
            }
        } else {
            at ++;
        }
    }
}

/** Color an argument by the type of the parameter it goes to.
 *
 * @param world The world
 * @param cwd Scope entity arguments resolve against, 0 for the root
 * @param param The parameter
 * @param path The parameter is a path parameter, a string that names an entity
 * @param line The command line
 * @param t The token of the argument
 * @param spans The box_console_span_t of the line
 */
static void box_console_syntax_arg(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const ecs_script_parameter_t *param,
    bool path,
    const char *line,
    const box_command_token_t *t,
    ecs_vec_t *spans)
{
    char c = line[t->start];
    if (param->type == ecs_id(ecs_entity_t) || path) {
        char *text = box_command_token_text(line, t);
        ecs_entity_t e = 0;
        bool found = path ? !box_shell_lookup(world, cwd, text, &e) :
            ecs_lookup_path_w_sep(world, cwd, text, ".", NULL, true) != 0;
        box_console_syntax_add(spans, t->start, t->end,
            found ? BoxSyntaxEntity : BoxSyntaxMissing);
        ecs_os_free(text);
    } else if (param->type == ecs_id(ecs_i32_t) || param->type == ecs_id(ecs_f64_t)) {
        box_console_syntax_add(spans, t->start, t->end, BoxSyntaxNumber);
    } else if (param->type == ecs_id(ecs_bool_t)) {
        box_console_syntax_add(spans, t->start, t->end, BoxSyntaxBoolean);
    } else if (t->quoted) {
        box_console_syntax_add(spans, t->start, t->end, BoxSyntaxString);
    } else if (c == '{' || c == '[') {
        box_console_syntax_value(line, t->start, t->end, spans);
    }
}

/** Color the name and arguments of a line of a registered command.
 *
 * @param world The world
 * @param con The console state
 * @param line The command line
 * @param tokens The tokens of the line
 * @param count Number of tokens
 * @param spans The box_console_span_t of the line
 */
static void box_console_syntax_call(
    ecs_world_t *world,
    box_console_t *con,
    const char *line,
    const box_command_token_t *tokens,
    int32_t count,
    ecs_vec_t *spans)
{
    ecs_entity_t scope = ecs_lookup(world, BOX_COMMAND_SCOPE), cmd = 0, cwd = 0;
    char *name = box_command_token_text(line, &tokens[0]);
    const EcsScriptFunction *f = NULL;
    const CommandInfo *info = NULL;
    const ecs_script_parameter_t *params;
    int32_t i, argc, passed = count < FLECS_SCRIPT_FUNCTION_ARGS_MAX ?
        count : FLECS_SCRIPT_FUNCTION_ARGS_MAX;
    if (scope) {
        cmd = ecs_lookup_path_w_sep(world, scope, name, ".", NULL, false);
    }

    f = cmd ? ecs_get(world, cmd, EcsScriptFunction) : NULL;
    info = cmd ? ecs_get(world, cmd, CommandInfo) : NULL;
    if (!f || !info) {
        box_console_syntax_add(spans, tokens[0].start, tokens[0].end, BoxSyntaxUnknown);
        goto done;
    }

    if (con->session && ecs_is_alive(world, con->session)) {
        cwd = box_terminal_cwd(world, con->session);
    }

    box_console_syntax_add(spans, tokens[0].start, tokens[0].end, BoxSyntaxName);
    argc = ecs_vec_count(&f->params);
    params = ecs_vec_first(&f->params);
    for (i = 1; i < count; i ++) {
        if (i >= argc) {
            box_console_syntax_add(spans, tokens[i].start, tokens[i].end, BoxSyntaxExtra);
        } else if (info->rest && i == argc - 1 && i < passed - 1) {
            box_console_syntax_add(spans, tokens[i].start, box_command_rest_end(line),
                BoxSyntaxString);
            break;
        } else {
            box_console_syntax_arg(world, cwd, &params[i], (info->paths >> i) & 1, line,
                &tokens[i], spans);
        }
    }

done:
    ecs_os_free(name);
}

void box_console_syntax(
    ecs_world_t *world,
    box_console_t *con,
    const char *line,
    ecs_vec_t *spans)
{
    int32_t i, count = box_command_tokens(line, NULL, 0);
    box_command_token_t *tokens = NULL;
    ecs_vec_clear(spans);
    if (!count) {
        goto done;
    }

    tokens = ecs_os_malloc_n(box_command_token_t, count);
    box_command_tokens(line, tokens, count);
    if (!tokens[0].quoted &&
        box_console_is_command(&line[tokens[0].start], tokens[0].end - tokens[0].start))
    {
        box_console_syntax_add(spans, tokens[0].start, tokens[0].end, BoxSyntaxName);
        for (i = 1; i < count; i ++) {
            box_console_syntax_add(spans, tokens[i].start, tokens[i].end, BoxSyntaxExtra);
        }
    } else {
        box_console_syntax_call(world, con, line, tokens, count, spans);
    }

done:
    ecs_os_free(tokens);
}
