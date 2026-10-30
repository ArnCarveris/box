/**
 * @file console/syntax.c
 * @brief Colored parts of a command line, from the tokens of the flecs script shell and the
 * parameter types of its command.
 */

#include "console.h"
#include <string.h>

/** Byte range of a token of a command line, as the shell splits the line */
typedef struct box_console_token_t {
    int32_t start;  /**< First byte, the opening quote or backticks of a quoted token */
    int32_t end;    /**< Byte after the token, after the closing quote when there is one */
    bool quoted;    /**< The token is a string in double quotes or three backticks */
} box_console_token_t;

/** Test whether a character separates tokens.
 *
 * @param c The character
 * @return Whether it is a space, tab or line break
 */
static bool box_console_syntax_space(
    char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/** Skip a double quoted string, a quote that is not closed runs to the end of the line.
 *
 * @param line The command line
 * @param at The opening quote
 * @return The byte after the closing quote
 */
static int32_t box_console_syntax_skip_string(
    const char *line,
    int32_t at)
{
    at ++;
    while (line[at] && line[at] != '"') {
        at += line[at] == '\\' && line[at + 1] ? 2 : 1;
    }

    return at + (line[at] != '\0');
}

/** Find the next token of a line as the shell splits it.
 *
 * Three backticks group a token up to the next three backticks, double quotes up to the next
 * unescaped quote, and brackets keep their contents together. Unclosed groups run to the end
 * of the line.
 *
 * @param line The command line
 * @param at Byte to search from
 * @param out Receives the range of the token
 * @return Whether there is a token
 */
static bool box_console_syntax_scan(
    const char *line,
    int32_t at,
    box_console_token_t *out)
{
    int32_t depth = 0;
    while (box_console_syntax_space(line[at])) {
        at ++;
    }

    out->start = at;
    out->quoted = line[at] == '"' || !strncmp(&line[at], "```", 3);
    if (!strncmp(&line[at], "```", 3)) {
        const char *end = strstr(&line[at + 3], "```");
        at = end ? (int32_t)(end - line) + 3 : (int32_t)strlen(line);
    } else if (line[at] == '"') {
        at = box_console_syntax_skip_string(line, at);
    } else {
        while (line[at] && (depth > 0 || !box_console_syntax_space(line[at]))) {
            if (line[at] == '"') {
                at = box_console_syntax_skip_string(line, at);
                continue;
            }

            if (line[at] == '(' || line[at] == '{' || line[at] == '[') {
                depth ++;
            } else if ((line[at] == ')' || line[at] == '}' || line[at] == ']') && depth) {
                depth --;
            }

            at ++;
        }
    }

    out->end = at;

    return out->end > out->start;
}

/** Find the tokens of a command line.
 *
 * @param line The command line
 * @param tokens Receives the ranges of up to max tokens
 * @param max Size of the tokens array
 * @return Number of tokens of the line, which may be more than max
 */
static int32_t box_console_syntax_tokens(
    const char *line,
    box_console_token_t *tokens,
    int32_t max)
{
    box_console_token_t t;
    int32_t result = 0, at = 0;
    while (box_console_syntax_scan(line, at, &t)) {
        if (result < max) {
            tokens[result] = t;
        }

        result ++;
        at = t.end;
    }

    return result;
}

/** Copy the text of a token as the shell passes it: without its quotes or backticks.
 *
 * @param line The command line
 * @param t The token
 * @return The text, which the caller frees
 */
static char* box_console_syntax_unquote(
    const char *line,
    const box_console_token_t *t)
{
    int32_t start = t->start, end = t->end;
    if (!strncmp(&line[start], "```", 3)) {
        start += 3;
        end -= end - start >= 3 && !strncmp(&line[end - 3], "```", 3) ? 3 : 0;
    } else if (line[start] == '"') {
        start ++;
        end -= end > start && line[end - 1] == '"';
    }

    return flecs_asprintf("%.*s", end - start, &line[start]);
}

/** Copy the text of a token.
 *
 * @param line The command line
 * @param t The token
 * @return The text, which the caller frees
 */
static char* box_console_syntax_text(
    const char *line,
    const box_console_token_t *t)
{
    return flecs_asprintf("%.*s", t->end - t->start, &line[t->start]);
}

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

ecs_entity_t box_console_syntax_scope(
    const ecs_world_t *world,
    const box_console_t *con)
{
    const EcsCommandSession *s = NULL;
    ecs_entity_t result = 0;
    if (con->session && ecs_is_alive(world, con->session)) {
        s = ecs_get(world, con->session, EcsCommandSession);
    }

    if (s && s->scope && ecs_is_alive(world, s->scope)) {
        result = s->scope;
    }

    return result;
}

/** Test whether an entity argument resolves as the shell resolves it.
 *
 * $ is the singleton, a path that starts with . or .. is relative to the scope, other names
 * resolve from the scope and its parents, then from the root.
 *
 * @param world The world
 * @param scope The scope of the session, 0 for the root
 * @param text The argument
 * @return Whether the argument names an entity
 */
static bool box_console_syntax_resolves(
    const ecs_world_t *world,
    ecs_entity_t scope,
    const char *text)
{
    bool relative = text[0] == '.' && (!text[1] || text[1] == '/' ||
        (text[1] == '.' && (!text[2] || text[2] == '/')));
    bool result = true;
    ecs_entity_t e = scope;
    const char *ptr = text;
    if (!strcmp(text, "$")) {
        goto done;
    }

    if (!relative) {
        result = (scope && ecs_lookup_path_w_sep(world, scope, text, ".", NULL, true)) ||
            ecs_lookup(world, text);
        goto done;
    }

    while (ptr[0] && result) {
        const char *slash = strchr(ptr, '/');
        int32_t len = slash ? (int32_t)(slash - ptr) : (int32_t)strlen(ptr);
        if (len == 2 && !strncmp(ptr, "..", 2)) {
            e = e ? ecs_get_parent(world, e) : 0;
        } else if (len && (len != 1 || ptr[0] != '.')) {
            char *name = flecs_asprintf("%.*s", len, ptr);
            e = ecs_lookup_child(world, e, name);
            ecs_os_free(name);
            result = e != 0;
        }

        ptr += len + (ptr[len] == '/');
    }

done:
    return result;
}

/** Color an argument by the type of the parameter it goes to.
 *
 * @param world The world
 * @param scope Scope entity arguments resolve against, 0 for the root
 * @param param The parameter
 * @param line The command line
 * @param t The token of the argument
 * @param spans The box_console_span_t of the line
 */
static void box_console_syntax_arg(
    const ecs_world_t *world,
    ecs_entity_t scope,
    const ecs_script_parameter_t *param,
    const char *line,
    const box_console_token_t *t,
    ecs_vec_t *spans)
{
    const EcsPrimitive *prim = ecs_get(world, param->type, EcsPrimitive);
    char c = line[t->start];
    bool entity = param->type == ecs_id(ecs_entity_t) ||
        (param->type == ecs_id(ecs_id_t) && c != '(');
    if (entity) {
        char *text = box_console_syntax_text(line, t);
        box_console_syntax_add(spans, t->start, t->end,
            box_console_syntax_resolves(world, scope, text) ? BoxSyntaxEntity : BoxSyntaxMissing);
        ecs_os_free(text);
    } else if (t->quoted) {
        box_console_syntax_add(spans, t->start, t->end, BoxSyntaxString);
    } else if (c == '{' || c == '[' || c == '(') {
        box_console_syntax_value(line, t->start, t->end, spans);
    } else if (prim && prim->kind == EcsBool) {
        box_console_syntax_add(spans, t->start, t->end, BoxSyntaxBoolean);
    } else if (prim && prim->kind != EcsString && prim->kind != EcsChar &&
        prim->kind != EcsEntity && prim->kind != EcsId)
    {
        box_console_syntax_add(spans, t->start, t->end, BoxSyntaxNumber);
    }
}

ecs_entity_t box_console_syntax_command(
    const ecs_world_t *world,
    const char *name)
{
    const EcsScriptShell *shell = ecs_singleton_get(world, EcsScriptShell);
    const ecs_entity_t *paths = shell ? ecs_vec_first(&shell->command_paths) : NULL;
    int32_t i, count = shell ? ecs_vec_count(&shell->command_paths) : 0;
    ecs_entity_t result = 0;
    for (i = 0; i < count && !result; i ++) {
        ecs_entity_t f = paths[i] && ecs_is_alive(world, paths[i]) ?
            ecs_lookup_child(world, paths[i], name) : 0;
        if (f && ecs_has(world, f, EcsScriptFunction)) {
            result = f;
        }
    }

    return result;
}

/** Color the name and arguments of a line of a shell command.
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
    const box_console_token_t *tokens,
    int32_t count,
    ecs_vec_t *spans)
{
    char *name = box_console_syntax_unquote(line, &tokens[0]);
    ecs_entity_t cmd = box_console_syntax_command(world, name);
    const EcsScriptFunction *f = cmd ? ecs_get(world, cmd, EcsScriptFunction) : NULL;
    const ecs_script_parameter_t *params;
    ecs_entity_t scope;
    int32_t i, argc;
    if (!f || !f->callback) {
        box_console_syntax_add(spans, tokens[0].start, tokens[0].end, BoxSyntaxUnknown);
        goto done;
    }

    scope = box_console_syntax_scope(world, con);
    box_console_syntax_add(spans, tokens[0].start, tokens[0].end, BoxSyntaxName);
    argc = ecs_vec_count(&f->params);
    params = ecs_vec_first(&f->params);
    for (i = 1; i < count; i ++) {
        if (i > argc) {
            box_console_syntax_add(spans, tokens[i].start, tokens[i].end, BoxSyntaxExtra);
        } else {
            box_console_syntax_arg(world, scope, &params[i - 1], line, &tokens[i], spans);
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
    int32_t i, count = box_console_syntax_tokens(line, NULL, 0);
    box_console_token_t *tokens = NULL;
    ecs_vec_clear(spans);
    if (!count) {
        goto done;
    }

    tokens = ecs_os_malloc_n(box_console_token_t, count);
    box_console_syntax_tokens(line, tokens, count);
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

char* box_console_syntax_token(
    const char *line,
    int32_t index)
{
    box_console_token_t t;
    int32_t at = 0, i = 0;
    char *result = NULL;
    while (!result && box_console_syntax_scan(line, at, &t)) {
        if (i == index) {
            result = box_console_syntax_unquote(line, &t);
        }

        at = t.end;
        i ++;
    }

    return result;
}
