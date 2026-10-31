/**
 * @file console/output.c
 * @brief Output of the console: the result of a command laid out and colored by its type, like
 * the terminal widgets of the flecs explorer, and errors and ANSI color codes turned into colors.
 */

#include "console.h"
#include <string.h>

/** Prefix of the types of the results of the shell commands */
#define BOX_CONSOLE_SHELL_TYPE "flecs.script.shell."

/** The result types of the shell commands the console lays out itself */
typedef enum box_console_type_t {
    BoxTypeCommandInfo,     /**< help */
    BoxTypeTreeNode,        /**< tree */
    BoxTypeComponentValue,  /**< Component and value */
    BoxTypeEntityInfo,      /**< inspect, cat */
    BoxTypeDocInfo,         /**< doc, man */
    BoxTypeQueryRow,        /**< query */
    BoxTypeGrepMatch,       /**< grep */
    BoxTypeScriptInfo,      /**< scripts */
    BoxTypeScriptLine,      /**< script_read, script_replace, script_patch, script_set */
    BoxTypeScriptMatch,     /**< script_grep */
    BoxTypeScriptLocation,  /**< script_where */
    BoxTypeScriptError,     /**< script_errors */
    BoxTypeCount            /**< Number of types */
} box_console_type_t;

/** Names of the result types, in the order of box_console_type_t */
static const char *box_console_type_names[BoxTypeCount] = {
    "CommandInfo", "TreeNode", "ComponentValue", "EntityInfo", "DocInfo", "QueryRow",
    "GrepMatch", "ScriptInfo", "ScriptLine", "ScriptMatch", "ScriptLocation", "ScriptError"
};

/** Keywords of flecs script, colored in script lines */
static const char *box_console_script_keywords[] = {
    "using", "module", "template", "prop", "const", "if", "else", "for", "in", "with",
    "match", "new", "export", "default", NULL
};

/** Which part of an entity path the pattern of a command is highlighted in */
typedef enum box_console_match_t {
    BoxMatchNone,           /**< No part */
    BoxMatchPath,           /**< The whole shown path, grep name matches */
    BoxMatchName            /**< The name after the parent part, find matches */
} box_console_match_t;

/** State of formatting one result */
typedef struct box_console_fmt_t {
    const ecs_world_t *world;   /**< The world */
    box_console_out_t *out;     /**< The output */
    ecs_entity_t scope;         /**< Scope of the session, paths are shown relative to it */
    const char *command;        /**< Name of the command, NULL for none */
    const char *pattern;        /**< First argument of the command, the text a grep looks for */
    int32_t number_width;       /**< Digits of the widest line number of script lines */
    bool comment;               /**< Whether the next script line starts inside a block comment */
    ecs_entity_t types[BoxTypeCount]; /**< The result types, 0 for a type that is not there */
} box_console_fmt_t;

void box_console_out_init(
    box_console_out_t *out)
{
    out->buf = (ecs_strbuf_t)ECS_STRBUF_INIT;
    ecs_vec_init_t(NULL, &out->spans, box_console_span_t, 0);
}

void box_console_out_fini(
    box_console_out_t *out)
{
    ecs_strbuf_reset(&out->buf);
    ecs_vec_fini_t(NULL, &out->spans, box_console_span_t);
}

int32_t box_console_out_len(
    box_console_out_t *out)
{
    return ecs_strbuf_written(&out->buf);
}

void box_console_out_mark(
    box_console_out_t *out,
    box_console_syntax_t kind,
    int32_t start,
    int32_t end)
{
    if (kind != BoxSpanNone && end > start) {
        *ecs_vec_append_t(NULL, &out->spans, box_console_span_t) = (box_console_span_t){
            .start = start, .end = end, .kind = kind };
    }
}

void box_console_out_add(
    box_console_out_t *out,
    box_console_syntax_t kind,
    const char *text,
    int32_t len)
{
    int32_t start = box_console_out_len(out);
    len = len < 0 ? (int32_t)strlen(text) : len;
    if (len > 0) {
        ecs_strbuf_appendstrn(&out->buf, text, len);
        box_console_out_mark(out, kind, start, start + len);
    }
}

char* box_console_out_text(
    box_console_out_t *out)
{
    char *result = ecs_strbuf_get(&out->buf);

    return result ? result : ecs_os_strdup("");
}

/** Get the color of an ANSI color code.
 *
 * @param code The code, 30 to 37 or 90 to 97
 * @param kind Receives the color, unchanged for a code that is not a color
 */
static void box_console_ansi_color(
    int32_t code,
    box_console_syntax_t *kind)
{
    switch (code >= 90 ? code - 60 : code) {
    case 30: *kind = BoxOutputGrey; break;
    case 31: *kind = BoxOutputRed; break;
    case 32: *kind = BoxOutputGreen; break;
    case 33: *kind = BoxOutputYellow; break;
    case 34: *kind = BoxOutputBlue; break;
    case 35: *kind = BoxOutputMagenta; break;
    case 36: *kind = BoxOutputCyan; break;
    case 37: *kind = BoxOutputWhite; break;
    default: break;
    }
}

/** Read an ANSI color code: ESC[ or [[, numbers separated by ;, then m.
 *
 * @param text The text at the code
 * @param len Bytes left in the text
 * @param base What text is after a reset
 * @param kind The current color, receives the color after the code
 * @return Bytes of the code, 0 when the text does not start with one
 */
static int32_t box_console_ansi_code(
    const char *text,
    int32_t len,
    box_console_syntax_t base,
    box_console_syntax_t *kind)
{
    box_console_syntax_t next = *kind;
    int32_t at = 2, code = 0, result = 0;
    bool digits = false;
    if (len < 3 || !((text[0] == '\033' || text[0] == '[') && text[1] == '[')) {
        goto done;
    }

    while (at < len && ((text[at] >= '0' && text[at] <= '9') || text[at] == ';')) {
        if (text[at] == ';') {
            next = !code ? base : next;
            box_console_ansi_color(code, &next);
            code = 0;
        } else {
            code = code * 10 + (text[at] - '0');
            digits = true;
        }

        at ++;
    }

    if (at < len && text[at] == 'm' && (digits || text[0] == '\033')) {
        if (!code || code == 39 || code == 49) {
            next = base;
        }

        box_console_ansi_color(code, &next);
        *kind = next;
        result = at + 1;
    }

done:
    return result;
}

void box_console_out_ansi(
    box_console_out_t *out,
    box_console_syntax_t base,
    const char *text,
    int32_t len)
{
    box_console_syntax_t kind = base;
    int32_t at = 0, from = 0, code;
    len = len < 0 ? (int32_t)strlen(text) : len;
    while (at < len) {
        box_console_syntax_t next = kind;
        code = box_console_ansi_code(&text[at], len - at, base, &next);
        if (code) {
            box_console_out_add(out, kind, &text[from], at - from);
            kind = next;
            at += code;
            from = at;
        } else {
            at ++;
        }
    }

    box_console_out_add(out, kind, &text[from], len - from);
}

void box_console_out_error(
    box_console_out_t *out,
    const char *msg)
{
    int32_t prefix = (int32_t)strlen(BOX_COMMAND_ERROR), len, first;
    const char *nl;
    msg = msg ? msg : "";
    if (!strncmp(msg, BOX_COMMAND_ERROR, (size_t)prefix)) {
        msg += prefix;
    }

    box_console_out_add(out, BoxOutputError, BOX_COMMAND_ERROR, prefix);
    len = (int32_t)strlen(msg);
    while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
        len --;
    }

    nl = memchr(msg, '\n', (size_t)len);
    first = nl ? (int32_t)(nl - msg) : len;
    while (first > 0 && msg[first - 1] == '\r') {
        first --;
    }

    box_console_out_ansi(out, BoxOutputText, msg, first);
    if (nl) {
        box_console_out_ansi(out, BoxOutputDim, nl, len - (int32_t)(nl - msg));
    }
}

/** Test whether a byte is a decimal digit.
 *
 * @param c The byte
 * @return Whether it is 0 to 9
 */
static bool box_console_out_digit(
    char c)
{
    return c >= '0' && c <= '9';
}

/** Test whether a byte starts a word: a letter or an underscore.
 *
 * @param c The byte
 * @return Whether it starts a word
 */
static bool box_console_out_alpha(
    char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/** Test whether a byte continues a word: a letter, digit, underscore or dot.
 *
 * @param c The byte
 * @return Whether it continues a word
 */
static bool box_console_out_word(
    char c)
{
    return box_console_out_alpha(c) || box_console_out_digit(c) || c == '.' ||
        (unsigned char)c >= 0x80;
}

/** Find the end of a number, with its sign, fraction and exponent.
 *
 * @param text The text
 * @param at The first byte of the number
 * @param len Bytes of the text
 * @return The byte after the number
 */
static int32_t box_console_out_number(
    const char *text,
    int32_t at,
    int32_t len)
{
    at ++;
    while (at < len && (box_console_out_digit(text[at]) || text[at] == '.' ||
        text[at] == 'e' || text[at] == 'E' ||
        ((text[at] == '-' || text[at] == '+') && (text[at - 1] == 'e' || text[at - 1] == 'E'))))
    {
        at ++;
    }

    return at;
}

/** Find the end of a quoted string, a string that is not closed runs to the end.
 *
 * @param text The text
 * @param at The opening quote
 * @param len Bytes of the text
 * @return The byte after the closing quote
 */
static int32_t box_console_out_string(
    const char *text,
    int32_t at,
    int32_t len)
{
    char quote = text[at];
    at ++;
    while (at < len && text[at] != quote) {
        at += text[at] == '\\' && at + 1 < len ? 2 : 1;
    }

    return at < len ? at + 1 : len;
}

/** Skip spaces.
 *
 * @param text The text
 * @param at The byte to start at
 * @param len Bytes of the text
 * @return The first byte that is not a space or tab
 */
static int32_t box_console_out_skip(
    const char *text,
    int32_t at,
    int32_t len)
{
    while (at < len && (text[at] == ' ' || text[at] == '\t')) {
        at ++;
    }

    return at;
}

/** Test whether the contents of a JSON string name an entity: a plain name or path that looks
 * up from the root.
 *
 * @param fmt The formatting state
 * @param text The contents, without the quotes
 * @param len Bytes of the contents
 * @return Whether it names an entity
 */
static bool box_console_fmt_names_entity(
    const box_console_fmt_t *fmt,
    const char *text,
    int32_t len)
{
    bool result = false;
    int32_t i;
    char *name;
    if (!len || !box_console_out_alpha(text[0])) {
        goto done;
    }

    for (i = 0; i < len; i ++) {
        if (text[i] == ' ' || text[i] == '\\' || text[i] == '"') {
            goto done;
        }
    }

    name = flecs_asprintf("%.*s", len, text);
    result = ecs_lookup(fmt->world, name) != 0;
    ecs_os_free(name);
done:
    return result;
}

/** Append a value as JSON, like terminal-value.vue: member names, numbers, strings, booleans,
 * null and strings that name entities in their colors.
 *
 * @param fmt The formatting state
 * @param json The JSON
 */
static void box_console_fmt_json(
    box_console_fmt_t *fmt,
    const char *json)
{
    box_console_out_t *out = fmt->out;
    int32_t start = box_console_out_len(out), len = (int32_t)strlen(json), at = 0, from;
    box_console_out_add(out, BoxSpanNone, json, len);
    while (at < len) {
        char c = json[at], next = at + 1 < len ? json[at + 1] : '\0';
        from = at;
        if (c == '"') {
            at = box_console_out_string(json, at, len);
            int32_t after = box_console_out_skip(json, at, len);
            box_console_syntax_t kind = BoxOutputString;
            if (after < len && json[after] == ':') {
                kind = BoxOutputKey;
            } else if (box_console_fmt_names_entity(fmt, &json[from + 1], at - from - 2)) {
                kind = BoxOutputEntity;
            }

            box_console_out_mark(out, kind, start + from, start + at);
        } else if (box_console_out_digit(c) || (c == '-' && box_console_out_digit(next))) {
            at = box_console_out_number(json, at, len);
            box_console_out_mark(out, BoxOutputNumber, start + from, start + at);
        } else if (box_console_out_alpha(c)) {
            while (at < len && box_console_out_word(json[at])) {
                at ++;
            }

            if ((at - from == 4 && !strncmp(&json[from], "true", 4)) ||
                (at - from == 5 && !strncmp(&json[from], "false", 5)))
            {
                box_console_out_mark(out, BoxOutputBoolean, start + from, start + at);
            } else if (at - from == 4 && !strncmp(&json[from], "null", 4)) {
                box_console_out_mark(out, BoxOutputKeyword, start + from, start + at);
            }
        } else {
            at ++;
        }
    }
}

/** Color the occurrences of the pattern of the command in text, like the match of a grep in the
 * explorer. The case is folded for grep_i only, grep, find and script_grep compare case sensitive
 * as their flecs commands do.
 *
 * @param fmt The formatting state
 * @param text The text that is matched
 * @param len Bytes of the text
 * @param at Byte of the output the shown text starts at
 * @param map Byte of the shown text each byte of the text starts at, len + 1 entries, NULL when
 * the text is the shown text
 */
static void box_console_fmt_matches_map(
    box_console_fmt_t *fmt,
    const char *text,
    int32_t len,
    int32_t at,
    const int32_t *map)
{
    int32_t i, j, plen = fmt->pattern ? (int32_t)strlen(fmt->pattern) : 0;
    bool fold = fmt->command && !strcmp(fmt->command, "grep_i");
    if (!plen) {
        goto done;
    }

    for (i = 0; i + plen <= len; i ++) {
        for (j = 0; j < plen; j ++) {
            char a = text[i + j], b = fmt->pattern[j];
            if (fold) {
                a = a >= 'A' && a <= 'Z' ? (char)(a - 'A' + 'a') : a;
                b = b >= 'A' && b <= 'Z' ? (char)(b - 'A' + 'a') : b;
            }

            if (a != b) {
                break;
            }
        }

        if (j == plen) {
            int32_t from = map ? map[i] : i, to = map ? map[i + plen] : i + plen;
            box_console_out_mark(fmt->out, BoxOutputMatch, at + from, at + to);
            i += plen - 1;
        }
    }

done:
    return;
}

/** Color the occurrences of the pattern of the command in text that was appended.
 *
 * @param fmt The formatting state
 * @param text The text
 * @param len Bytes of the text
 * @param at Byte of the output the text starts at
 */
static void box_console_fmt_matches(
    box_console_fmt_t *fmt,
    const char *text,
    int32_t len,
    int32_t at)
{
    box_console_fmt_matches_map(fmt, text, len, at, NULL);
}

/** Color the occurrences of the pattern in a path that was appended. The flecs commands match the
 * names of entities, so the pattern is matched against the unescaped path: ecs_get_path writes a
 * '.' in a name as "\.", and a match that includes such a '.' covers its backslash too.
 *
 * @param fmt The formatting state
 * @param shown The path as it was appended
 * @param len Bytes of the path
 * @param at Byte of the output the path starts at
 */
static void box_console_fmt_matches_path(
    box_console_fmt_t *fmt,
    const char *shown,
    int32_t len,
    int32_t at)
{
    char *text;
    int32_t *map, i, count = 0;
    if (!memchr(shown, '\\', (size_t)len)) {
        box_console_fmt_matches(fmt, shown, len, at);
        goto done;
    }

    text = ecs_os_malloc(len + 1);
    map = ecs_os_malloc_n(int32_t, len + 1);
    for (i = 0; i < len; i ++) {
        map[count] = i;
        if (shown[i] == '\\' && i + 1 < len && shown[i + 1] == '.') {
            i ++;
        }

        text[count ++] = shown[i];
    }

    map[count] = len;
    text[count] = '\0';
    box_console_fmt_matches_map(fmt, text, count, at, map);
    ecs_os_free(map);
    ecs_os_free(text);

done:
    return;
}

/** Append an entity as terminal-entity.vue shows it: its path relative to the scope of the
 * session with the parent part dimmed and the name in the entity color.
 *
 * @param fmt The formatting state
 * @param e The entity
 * @param match The part of the path to color the occurrences of the pattern in
 */
static void box_console_fmt_entity(
    box_console_fmt_t *fmt,
    ecs_entity_t e,
    box_console_match_t match)
{
    const ecs_world_t *world = fmt->world;
    char *path = NULL, *scope = NULL;
    const char *shown;
    int32_t i, len, name = 0, start = box_console_out_len(fmt->out);
    if (!e || !ecs_is_alive(world, e)) {
        char id[32];
        ecs_os_snprintf(id, sizeof(id), "#%u", (uint32_t)e);
        box_console_out_add(fmt->out, BoxOutputDim, id, -1);
        goto done;
    }

    path = ecs_get_path(world, e);
    shown = path;
    if (fmt->scope && e != fmt->scope && ecs_is_alive(world, fmt->scope)) {
        scope = ecs_get_path(world, fmt->scope);
        len = (int32_t)strlen(scope);
        if (!strncmp(path, scope, (size_t)len) && path[len] == '.') {
            shown = &path[len + 1];
        }
    }

    len = (int32_t)strlen(shown);
    for (i = 0; i < len; i ++) {
        if (shown[i] == '\\' && i + 1 < len && shown[i + 1] == '.') {
            i ++;
        } else if (shown[i] == '.') {
            name = i + 1;
        }
    }

    box_console_out_add(fmt->out, BoxOutputDim, shown, name);
    box_console_out_add(fmt->out, BoxOutputEntity, &shown[name], len - name);
    if (match == BoxMatchPath) {
        box_console_fmt_matches_path(fmt, shown, len, start);
    } else if (match == BoxMatchName) {
        box_console_fmt_matches_path(fmt, &shown[name], len - name, start + name);
    }

done:
    ecs_os_free(scope);
    ecs_os_free(path);
}

/** Append the name of an entity, or its id when it has no name.
 *
 * @param fmt The formatting state
 * @param e The entity
 */
static void box_console_fmt_name(
    box_console_fmt_t *fmt,
    ecs_entity_t e)
{
    const char *name = e && ecs_is_alive(fmt->world, e) ? ecs_get_name(fmt->world, e) : NULL;
    if (name) {
        box_console_out_add(fmt->out, BoxOutputEntity, name, -1);
    } else {
        box_console_fmt_entity(fmt, e, BoxMatchNone);
    }
}

/** Append a component or id.
 *
 * @param fmt The formatting state
 * @param id The id
 */
static void box_console_fmt_id(
    box_console_fmt_t *fmt,
    ecs_id_t id)
{
    char *str = ecs_id_str(fmt->world, id);
    box_console_out_add(fmt->out, BoxOutputComponent, str ? str : "0", -1);
    ecs_os_free(str);
}

/** Append text that continues on further lines, each further line indented.
 *
 * @param fmt The formatting state
 * @param kind What the text is
 * @param text The text
 * @param indent Spaces in front of the further lines
 */
static void box_console_fmt_lines(
    box_console_fmt_t *fmt,
    box_console_syntax_t kind,
    const char *text,
    int32_t indent)
{
    const char *p = text;
    while (p) {
        const char *nl = strchr(p, '\n');
        box_console_out_ansi(fmt->out, kind, p, nl ? (int32_t)(nl - p) : -1);
        if (nl) {
            box_console_out_add(fmt->out, BoxSpanNone, "\n", 1);
            ecs_strbuf_append(&fmt->out->buf, "%*s", indent, "");
        }

        p = nl ? nl + 1 : NULL;
    }
}

/** Append a line of flecs script code with the colors of flecsTerminal.highlightScript: comments,
 * strings, numbers, keywords, booleans, components, entities and member names.
 *
 * @param fmt The formatting state
 * @param text The code
 * @param comment Whether the line starts inside a block comment, receives whether the next line
 * does, NULL for code that does not continue on a next line
 */
static void box_console_fmt_script(
    box_console_fmt_t *fmt,
    const char *text,
    bool *comment)
{
    box_console_out_t *out = fmt->out;
    int32_t start = box_console_out_len(out), len = (int32_t)strlen(text), at = 0, from, i;
    box_console_out_add(out, BoxSpanNone, text, len);
    if (comment && *comment) {
        const char *end = strstr(text, "*/");
        at = end ? (int32_t)(end - text) + 2 : len;
        box_console_out_mark(out, BoxOutputDim, start, start + at);
        *comment = end == NULL;
    }

    while (at < len) {
        char c = text[at], next = at + 1 < len ? text[at + 1] : '\0';
        bool after_word = at > 0 && box_console_out_word(text[at - 1]);
        from = at;
        if (c == '/' && next == '/') {
            box_console_out_mark(out, BoxOutputDim, start + at, start + len);
            at = len;
        } else if (c == '/' && next == '*') {
            const char *end = strstr(&text[at + 2], "*/");
            at = end ? (int32_t)(end - text) + 2 : len;
            box_console_out_mark(out, BoxOutputDim, start + from, start + at);
            if (comment && !end) {
                *comment = true;
            }
        } else if (c == '"' || c == '`') {
            at = box_console_out_string(text, at, len);
            box_console_out_mark(out, BoxOutputString, start + from, start + at);
        } else if (!after_word && (box_console_out_digit(c) ||
            (c == '-' && box_console_out_digit(next))))
        {
            at = box_console_out_number(text, at, len);
            box_console_out_mark(out, BoxOutputNumber, start + from, start + at);
        } else if (box_console_out_alpha(c)) {
            box_console_syntax_t kind = BoxSpanNone;
            bool keyword = false;
            int32_t after;
            while (at < len && box_console_out_word(text[at])) {
                at ++;
            }

            after = box_console_out_skip(text, at, len);
            for (i = 0; box_console_script_keywords[i]; i ++) {
                const char *kw = box_console_script_keywords[i];
                if ((int32_t)strlen(kw) == at - from &&
                    !strncmp(&text[from], kw, (size_t)(at - from)))
                {
                    keyword = true;
                }
            }

            if (keyword) {
                kind = BoxOutputKeyword;
            } else if ((at - from == 4 && !strncmp(&text[from], "true", 4)) ||
                (at - from == 5 && !strncmp(&text[from], "false", 5)))
            {
                kind = BoxOutputBoolean;
            } else if (c >= 'A' && c <= 'Z') {
                kind = BoxOutputComponent;
            } else if (after < len && text[after] == ':') {
                kind = BoxOutputKey;
            } else if (after < len && text[after] == '{') {
                kind = BoxOutputEntity;
            }

            box_console_out_mark(out, kind, start + from, start + at);
        } else {
            at ++;
        }
    }
}

/** Append a value as JSON.
 *
 * @param fmt The formatting state
 * @param type The type
 * @param ptr The value
 */
static void box_console_fmt_value_json(
    box_console_fmt_t *fmt,
    ecs_entity_t type,
    const void *ptr)
{
    char *json = type && ptr ? ecs_ptr_to_json(fmt->world, type, ptr) : NULL;
    if (json) {
        box_console_fmt_json(fmt, json);
    } else {
        box_console_out_add(fmt->out, BoxOutputKeyword, "null", 4);
    }

    ecs_os_free(json);
}

/** Append a command as terminal-help.vue lists it: its name, its parameters, its description and
 * a line for every parameter with its type and description.
 *
 * @param fmt The formatting state
 * @param info The command
 */
static void box_console_fmt_info(
    box_console_fmt_t *fmt,
    const ecs_shell_info_t *info)
{
    box_console_out_t *out = fmt->out;
    const ecs_shell_param_info_t *params = ecs_vec_first(&info->params);
    int32_t i, count = ecs_vec_count(&info->params);
    box_console_out_add(out, BoxOutputCommand, info->name ? info->name : "", -1);
    for (i = 0; i < count; i ++) {
        box_console_out_add(out, BoxSpanNone, " ", 1);
        box_console_out_add(out, BoxOutputDim, params[i].optional ? "[" : "<", 1);
        box_console_out_add(out, BoxOutputParam, params[i].name ? params[i].name : "", -1);
        box_console_out_add(out, BoxOutputDim, params[i].optional ? "]" : ">", 1);
    }

    if (info->brief) {
        box_console_out_add(out, BoxSpanNone, "  ", 2);
        box_console_out_add(out, BoxOutputBrief, info->brief, -1);
    }

    for (i = 0; i < count; i ++) {
        const char *type = params[i].type && ecs_is_alive(fmt->world, params[i].type) ?
            ecs_get_name(fmt->world, params[i].type) : NULL;
        box_console_out_add(out, BoxSpanNone, "\n    ", 5);
        box_console_out_add(out, BoxOutputParam, params[i].name ? params[i].name : "", -1);
        if (type) {
            box_console_out_add(out, BoxOutputDim, ": ", 2);
            box_console_out_add(out, BoxOutputType, type, -1);
        }

        if (params[i].brief) {
            box_console_out_add(out, BoxSpanNone, "  ", 2);
            box_console_out_add(out, BoxOutputBrief, params[i].brief, -1);
        }
    }
}

/** Get the help group of a command: the scope of command_paths it is in.
 *
 * @param fmt The formatting state
 * @param info The command
 * @return The scope, 0 when it is in none of them
 */
static ecs_entity_t box_console_fmt_group(
    box_console_fmt_t *fmt,
    const ecs_shell_info_t *info)
{
    ecs_entity_t f = info->name ? box_console_syntax_command(fmt->world, info->name) : 0;

    return f ? ecs_get_parent(fmt->world, f) : 0;
}

/** Append the header of a help group: the name of its scope, box for the box commands.
 *
 * @param fmt The formatting state
 * @param group The scope, 0 for commands outside of the command paths
 */
static void box_console_fmt_group_header(
    box_console_fmt_t *fmt,
    ecs_entity_t group)
{
    const char *name = "other";
    if (group && group == ecs_lookup(fmt->world, BOX_COMMAND_SCOPE)) {
        name = "box";
    } else if (group && ecs_get_name(fmt->world, group)) {
        name = ecs_get_name(fmt->world, group);
    }

    box_console_out_add(fmt->out, BoxOutputGroup, name, -1);
}

/** Append the commands help returned, grouped by the scope they are in in the order of
 * EcsScriptShell.command_paths, a blank line between the groups. A single command has no
 * group header.
 *
 * @param fmt The formatting state
 * @param infos The commands
 * @param count Number of commands
 */
static void box_console_fmt_help(
    box_console_fmt_t *fmt,
    const ecs_shell_info_t *infos,
    int32_t count)
{
    const EcsScriptShell *shell = ecs_singleton_get(fmt->world, EcsScriptShell);
    const ecs_entity_t *paths = shell ? ecs_vec_first(&shell->command_paths) : NULL;
    int32_t p, i, path_count = shell ? ecs_vec_count(&shell->command_paths) : 0;
    ecs_entity_t *groups;
    bool *done;
    if (count == 1) {
        box_console_fmt_info(fmt, &infos[0]);
        goto end;
    }

    groups = ecs_os_malloc_n(ecs_entity_t, count ? count : 1);
    done = ecs_os_calloc_n(bool, count ? count : 1);
    for (i = 0; i < count; i ++) {
        groups[i] = box_console_fmt_group(fmt, &infos[i]);
    }

    for (p = 0; p <= path_count; p ++) {
        bool header = false;
        for (i = 0; i < count; i ++) {
            bool in = p < path_count ? groups[i] == paths[p] : !done[i];
            if (done[i] || !in) {
                continue;
            }

            if (!header) {
                if (box_console_out_len(fmt->out)) {
                    box_console_out_add(fmt->out, BoxSpanNone, "\n\n", 2);
                }

                box_console_fmt_group_header(fmt, p < path_count ? paths[p] : 0);
                header = true;
            }

            box_console_out_add(fmt->out, BoxSpanNone, "\n", 1);
            box_console_fmt_info(fmt, &infos[i]);
            done[i] = true;
        }
    }

    ecs_os_free(groups);
    ecs_os_free(done);
end:
    return;
}

/** Find for every node of a tree and every depth from 1 to its own whether the ancestor of the
 * node at that depth, or the node itself, has a next sibling.
 *
 * One backward pass keeps per depth whether the first later node at that depth or above is at it.
 *
 * @param nodes The nodes, in depth first order
 * @param count Number of nodes
 * @param offsets Receives for every node where its depths start in the result
 * @return For every node and depth 1 to its depth whether there is a next sibling at the depth,
 * free with ecs_os_free
 */
static bool* box_console_tree_more(
    const ecs_shell_tree_node_t *nodes,
    int32_t count,
    int32_t *offsets)
{
    int32_t i, d, total = 0, max = 0, top = 0;
    bool *result, *more;
    for (i = 0; i < count; i ++) {
        int32_t depth = nodes[i].depth > 0 ? nodes[i].depth : 0;
        offsets[i] = total;
        total += depth;
        max = depth > max ? depth : max;
    }

    result = ecs_os_calloc_n(bool, total ? total : 1);
    more = ecs_os_calloc_n(bool, max + 1);
    for (i = count - 1; i >= 0; i --) {
        int32_t depth = nodes[i].depth > 0 ? nodes[i].depth : 0;
        for (d = 1; d <= depth; d ++) {
            result[offsets[i] + d - 1] = more[d];
        }

        for (d = depth + 1; d <= top; d ++) {
            more[d] = false;
        }

        more[depth] = true;
        top = depth;
    }

    ecs_os_free(more);

    return result;
}

/** Append a tree as terminal-tree.vue shows it (flecsTerminal.treeLines): one entity name per
 * line under dimmed guides, the children of the scope without guides.
 *
 * @param fmt The formatting state
 * @param nodes The nodes, in depth first order
 * @param count Number of nodes
 */
static void box_console_fmt_tree(
    box_console_fmt_t *fmt,
    const ecs_shell_tree_node_t *nodes,
    int32_t count)
{
    int32_t i, d;
    int32_t *offsets = ecs_os_malloc_n(int32_t, count ? count : 1);
    bool *more = box_console_tree_more(nodes, count, offsets);
    for (i = 0; i < count; i ++) {
        const bool *m = &more[offsets[i]];
        int32_t start;
        if (i) {
            box_console_out_add(fmt->out, BoxSpanNone, "\n", 1);
        }

        start = box_console_out_len(fmt->out);
        for (d = 1; d < nodes[i].depth; d ++) {
            ecs_strbuf_appendstr(&fmt->out->buf, m[d - 1] ? "|  " : "   ");
        }

        if (nodes[i].depth > 0) {
            ecs_strbuf_appendstr(&fmt->out->buf, m[nodes[i].depth - 1] ? "|- " : "`- ");
        }

        box_console_out_mark(fmt->out, BoxOutputDim, start, box_console_out_len(fmt->out));
        box_console_fmt_name(fmt, nodes[i].entity);
    }

    ecs_os_free(more);
    ecs_os_free(offsets);
}

/** Append a component and its value, Component: value.
 *
 * @param fmt The formatting state
 * @param cv The component and value
 */
static void box_console_fmt_component(
    box_console_fmt_t *fmt,
    const ecs_shell_component_value_t *cv)
{
    box_console_fmt_id(fmt, cv->id);
    if (cv->value.type && cv->value.ptr) {
        box_console_out_add(fmt->out, BoxOutputDim, ": ", 2);
        box_console_fmt_value_json(fmt, cv->value.type, cv->value.ptr);
    }
}

/** Append a query row as terminal-query.vue shows it: the entity, then every field with its
 * component and value.
 *
 * @param fmt The formatting state
 * @param row The row
 */
static void box_console_fmt_row(
    box_console_fmt_t *fmt,
    const ecs_shell_query_row_t *row)
{
    const ecs_shell_component_value_t *fields = ecs_vec_first(&row->fields);
    int32_t i, count = ecs_vec_count(&row->fields);
    if (row->entity) {
        box_console_fmt_entity(fmt, row->entity, BoxMatchNone);
    }

    for (i = 0; i < count; i ++) {
        if (i || row->entity) {
            box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
        }

        box_console_fmt_component(fmt, &fields[i]);
    }
}

/** Append entity information: the entity and its id, then a line for every component with its
 * value and for every id without a value.
 *
 * @param fmt The formatting state
 * @param info The entity information
 */
static void box_console_fmt_inspect(
    box_console_fmt_t *fmt,
    const ecs_shell_entity_info_t *info)
{
    const ecs_shell_component_value_t *cv = ecs_vec_first(&info->components);
    const ecs_id_t *ids = ecs_vec_first(&info->ids);
    int32_t i;
    char id[32];
    box_console_fmt_entity(fmt, info->entity, BoxMatchNone);
    ecs_os_snprintf(id, sizeof(id), "  #%llu", (unsigned long long)info->id);
    box_console_out_add(fmt->out, BoxOutputDim, id, -1);
    for (i = 0; i < ecs_vec_count(&info->components); i ++) {
        box_console_out_add(fmt->out, BoxSpanNone, "\n  ", 3);
        box_console_fmt_component(fmt, &cv[i]);
    }

    for (i = 0; i < ecs_vec_count(&info->ids); i ++) {
        box_console_out_add(fmt->out, BoxSpanNone, "\n  ", 3);
        box_console_fmt_id(fmt, ids[i]);
    }
}

/** Append a field of documentation, label: text, when it is set.
 *
 * @param fmt The formatting state
 * @param label The label
 * @param text The text, NULL to append nothing
 * @param kind What the text is
 */
static void box_console_fmt_field(
    box_console_fmt_t *fmt,
    const char *label,
    const char *text,
    box_console_syntax_t kind)
{
    if (text) {
        int32_t indent = (int32_t)strlen(label) + 4;
        box_console_out_add(fmt->out, BoxSpanNone, "\n  ", 3);
        box_console_out_add(fmt->out, BoxOutputKey, label, -1);
        box_console_out_add(fmt->out, BoxOutputDim, ": ", 2);
        box_console_fmt_lines(fmt, kind, text, indent);
    }
}

/** Append documentation: the entity, then its name, brief, detail, link and color.
 *
 * @param fmt The formatting state
 * @param doc The documentation
 */
static void box_console_fmt_doc(
    box_console_fmt_t *fmt,
    const ecs_shell_doc_info_t *doc)
{
    box_console_fmt_entity(fmt, doc->entity, BoxMatchNone);
    box_console_fmt_field(fmt, "name", doc->name, BoxSpanNone);
    box_console_fmt_field(fmt, "brief", doc->brief, BoxSpanNone);
    box_console_fmt_field(fmt, "detail", doc->detail, BoxSpanNone);
    box_console_fmt_field(fmt, "link", doc->link, BoxOutputString);
    box_console_fmt_field(fmt, "color", doc->color, BoxOutputString);
}

/** Append a grep match: the entity, then the component and the text that matched, with the
 * pattern of the grep highlighted.
 *
 * @param fmt The formatting state
 * @param m The match
 */
static void box_console_fmt_grep(
    box_console_fmt_t *fmt,
    const ecs_shell_grep_match_t *m)
{
    int32_t start, len;
    box_console_fmt_entity(fmt, m->entity, m->id ? BoxMatchNone : BoxMatchPath);
    if (m->id) {
        box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
        box_console_fmt_id(fmt, m->id);
        if (m->text) {
            box_console_out_add(fmt->out, BoxOutputDim, ": ", 2);
            start = box_console_out_len(fmt->out);
            len = (int32_t)strlen(m->text);
            box_console_out_add(fmt->out, BoxSpanNone, m->text, len);
            box_console_fmt_matches(fmt, m->text, len, start);
        }
    }
}

/** Append the line number of a line of script code, dimmed and right aligned.
 *
 * @param fmt The formatting state
 * @param line The line number
 */
static void box_console_fmt_line_number(
    box_console_fmt_t *fmt,
    int32_t line)
{
    char number[32];
    ecs_os_snprintf(number, sizeof(number), "%*d", fmt->number_width, line);
    box_console_out_add(fmt->out, BoxOutputDim, number, -1);
}

/** Get the digits of a number.
 *
 * @param n The number
 * @return The digits, at least one
 */
static int32_t box_console_digits(
    int32_t n)
{
    int32_t result = 1;
    while (n >= 10 || n <= -10) {
        n /= 10;
        result ++;
    }

    return result;
}

/** Append a script of the scripts command: the script, its file, its lines, whether it has
 * unsaved changes and its error.
 *
 * @param fmt The formatting state
 * @param s The script
 */
static void box_console_fmt_script_info(
    box_console_fmt_t *fmt,
    const ecs_shell_script_info_t *s)
{
    char lines[32];
    box_console_fmt_entity(fmt, s->script, BoxMatchNone);
    if (s->filename) {
        box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
        box_console_out_add(fmt->out, BoxOutputString, s->filename, -1);
    }

    ecs_os_snprintf(lines, sizeof(lines), "%d", s->lines);
    box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
    box_console_out_add(fmt->out, BoxOutputNumber, lines, -1);
    box_console_out_add(fmt->out, BoxOutputDim, s->lines == 1 ? " line" : " lines", -1);
    if (s->unsaved) {
        box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
        box_console_out_add(fmt->out, BoxOutputKeyword, "unsaved", -1);
    }

    if (s->error) {
        box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
        box_console_out_error(fmt->out, s->error);
    }
}

/** Append a line of script code: its dimmed line number and the code in script colors.
 *
 * @param fmt The formatting state
 * @param l The line
 */
static void box_console_fmt_script_line(
    box_console_fmt_t *fmt,
    const ecs_shell_script_line_t *l)
{
    box_console_fmt_line_number(fmt, l->line);
    box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
    box_console_fmt_script(fmt, l->text ? l->text : "", &fmt->comment);
}

/** Append a script and a line of it: script:line.
 *
 * @param fmt The formatting state
 * @param script The script
 * @param line The line
 * @param column The column, 0 for none
 */
static void box_console_fmt_location(
    box_console_fmt_t *fmt,
    ecs_entity_t script,
    int32_t line,
    int32_t column)
{
    char at[32];
    box_console_fmt_entity(fmt, script, BoxMatchNone);
    if (column) {
        ecs_os_snprintf(at, sizeof(at), ":%d:%d", line, column);
    } else {
        ecs_os_snprintf(at, sizeof(at), ":%d", line);
    }

    box_console_out_add(fmt->out, BoxOutputDim, at, -1);
}

/** Append a script_grep match: the script and line, then the code with the pattern highlighted.
 *
 * @param fmt The formatting state
 * @param m The match
 */
static void box_console_fmt_script_match(
    box_console_fmt_t *fmt,
    const ecs_shell_script_match_t *m)
{
    const char *text = m->text ? m->text : "";
    int32_t start;
    box_console_fmt_location(fmt, m->script, m->line, 0);
    box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
    start = box_console_out_len(fmt->out);
    box_console_fmt_script(fmt, text, NULL);
    box_console_fmt_matches(fmt, text, (int32_t)strlen(text), start);
}

/** Append the location of an entity statement: script:line:column, the statement in script
 * colors and the template it is in.
 *
 * @param fmt The formatting state
 * @param l The location
 */
static void box_console_fmt_script_location(
    box_console_fmt_t *fmt,
    const ecs_shell_script_location_t *l)
{
    box_console_fmt_location(fmt, l->script, l->line, l->column);
    if (l->statement) {
        box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
        box_console_fmt_script(fmt, l->statement, NULL);
    }

    if (l->template_) {
        box_console_out_add(fmt->out, BoxSpanNone, "\n  ", 3);
        box_console_out_add(fmt->out, BoxOutputKey, "template", -1);
        box_console_out_add(fmt->out, BoxOutputDim, ": ", 2);
        box_console_fmt_entity(fmt, l->template_, BoxMatchNone);
    }
}

/** Append a script error: the script, then the error.
 *
 * @param fmt The formatting state
 * @param e The error
 */
static void box_console_fmt_script_error(
    box_console_fmt_t *fmt,
    const ecs_shell_script_error_t *e)
{
    box_console_fmt_entity(fmt, e->script, BoxMatchNone);
    box_console_out_add(fmt->out, BoxSpanNone, "  ", 2);
    box_console_out_error(fmt->out, e->error);
}

/** Test whether a string is a unified diff: the output of script_diff, or text that starts
 * like one.
 *
 * @param fmt The formatting state
 * @param str The string
 * @return Whether to color it as a diff
 */
static bool box_console_fmt_is_diff(
    const box_console_fmt_t *fmt,
    const char *str)
{
    return (fmt->command && !strcmp(fmt->command, "script_diff")) ||
        !strncmp(str, "--- ", 4) || !strncmp(str, "@@ ", 3);
}

/** Append a unified diff: added lines, removed lines and the headers of files and hunks in
 * their colors.
 *
 * @param fmt The formatting state
 * @param str The diff
 */
static void box_console_fmt_diff(
    box_console_fmt_t *fmt,
    const char *str)
{
    box_console_out_t *out = fmt->out;
    int32_t start = box_console_out_len(out);
    const char *p = str;
    box_console_out_add(out, BoxSpanNone, str, -1);
    while (p[0]) {
        const char *nl = strchr(p, '\n');
        int32_t len = nl ? (int32_t)(nl - p) : (int32_t)strlen(p);
        int32_t at = start + (int32_t)(p - str);
        box_console_syntax_t kind = BoxSpanNone;
        if (!strncmp(p, "+++", 3) || !strncmp(p, "---", 3) || !strncmp(p, "@@", 2)) {
            kind = BoxOutputDim;
        } else if (p[0] == '+') {
            kind = BoxOutputAdded;
        } else if (p[0] == '-') {
            kind = BoxOutputRemoved;
        }

        box_console_out_mark(out, kind, at, at + len);
        p += len + (nl != NULL);
    }
}

/** Find the result type a type is.
 *
 * @param fmt The formatting state
 * @param type The type
 * @return The result type, BoxTypeCount for a type the console does not lay out itself
 */
static box_console_type_t box_console_fmt_type(
    const box_console_fmt_t *fmt,
    ecs_entity_t type)
{
    int32_t i;
    box_console_type_t result = BoxTypeCount;
    for (i = 0; i < BoxTypeCount && result == BoxTypeCount; i ++) {
        if (type && fmt->types[i] == type) {
            result = (box_console_type_t)i;
        }
    }

    return result;
}

static void box_console_fmt_value(
    box_console_fmt_t *fmt,
    ecs_entity_t type,
    const void *ptr);

/** Append the elements of a vector or array, one per line, a tree and help as a whole.
 *
 * @param fmt The formatting state
 * @param type The element type
 * @param elems The elements
 * @param count Number of elements
 */
static void box_console_fmt_elements(
    box_console_fmt_t *fmt,
    ecs_entity_t type,
    const void *elems,
    int32_t count)
{
    const ecs_type_info_t *ti = ecs_get_type_info(fmt->world, type);
    box_console_type_t t = box_console_fmt_type(fmt, type);
    int32_t i, max = 0;
    if (!ti || !count) {
        goto done;
    }

    if (t == BoxTypeTreeNode) {
        box_console_fmt_tree(fmt, elems, count);
        goto done;
    }

    if (t == BoxTypeCommandInfo) {
        box_console_fmt_help(fmt, elems, count);
        goto done;
    }

    if (t == BoxTypeScriptLine) {
        const ecs_shell_script_line_t *lines = elems;
        for (i = 0; i < count; i ++) {
            max = lines[i].line > max ? lines[i].line : max;
        }

        fmt->number_width = box_console_digits(max);
        fmt->comment = false;
    }

    for (i = 0; i < count; i ++) {
        if (i) {
            box_console_out_add(fmt->out, BoxSpanNone, "\n", 1);
        }

        box_console_fmt_value(fmt, type, ECS_ELEM(elems, ti->size, i));
    }

    fmt->comment = false;
done:
    return;
}

/** Append a value by its type, like flecsTerminal.widgetFor picks the widget of a result.
 *
 * @param fmt The formatting state
 * @param type The type
 * @param ptr The value
 */
static void box_console_fmt_value(
    box_console_fmt_t *fmt,
    ecs_entity_t type,
    const void *ptr)
{
    const ecs_world_t *world = fmt->world;
    const EcsVector *vt = ecs_get(world, type, EcsVector);
    const EcsArray *at = ecs_get(world, type, EcsArray);
    if (type == ecs_id(ecs_string_t)) {
        const char *str = *(char* const*)ptr;
        str = str ? str : "";
        if (box_console_fmt_is_diff(fmt, str)) {
            box_console_fmt_diff(fmt, str);
        } else {
            box_console_out_ansi(fmt->out, BoxSpanNone, str, -1);
        }
    } else if (type == ecs_id(ecs_entity_t)) {
        bool find = fmt->command && !strcmp(fmt->command, "find");
        box_console_fmt_entity(fmt, *(const ecs_entity_t*)ptr,
            find ? BoxMatchName : BoxMatchNone);
    } else if (type == ecs_id(ecs_id_t)) {
        box_console_fmt_id(fmt, *(const ecs_id_t*)ptr);
    } else if (type == ecs_id(ecs_value_t)) {
        const ecs_value_t *v = ptr;
        if (v->type && v->ptr) {
            box_console_fmt_value(fmt, v->type, v->ptr);
        } else {
            box_console_out_add(fmt->out, BoxOutputKeyword, "null", 4);
        }
    } else if (vt) {
        const ecs_vec_t *v = ptr;
        box_console_fmt_elements(fmt, vt->type, v->array, v->count);
    } else if (at) {
        box_console_fmt_elements(fmt, at->type, ptr, at->count);
    } else {
        switch (box_console_fmt_type(fmt, type)) {
        case BoxTypeCommandInfo: box_console_fmt_info(fmt, ptr); break;
        case BoxTypeTreeNode: box_console_fmt_tree(fmt, ptr, 1); break;
        case BoxTypeComponentValue: box_console_fmt_component(fmt, ptr); break;
        case BoxTypeEntityInfo: box_console_fmt_inspect(fmt, ptr); break;
        case BoxTypeDocInfo: box_console_fmt_doc(fmt, ptr); break;
        case BoxTypeQueryRow: box_console_fmt_row(fmt, ptr); break;
        case BoxTypeGrepMatch: box_console_fmt_grep(fmt, ptr); break;
        case BoxTypeScriptInfo: box_console_fmt_script_info(fmt, ptr); break;
        case BoxTypeScriptLine:
            if (!fmt->number_width) {
                fmt->number_width = box_console_digits(
                    ((const ecs_shell_script_line_t*)ptr)->line);
            }

            box_console_fmt_script_line(fmt, ptr);
            break;
        case BoxTypeScriptMatch: box_console_fmt_script_match(fmt, ptr); break;
        case BoxTypeScriptLocation: box_console_fmt_script_location(fmt, ptr); break;
        case BoxTypeScriptError: box_console_fmt_script_error(fmt, ptr); break;
        case BoxTypeCount:
        default:
            box_console_fmt_value_json(fmt, type, ptr);
            break;
        }
    }
}

void box_console_out_run(
    ecs_world_t *world,
    box_console_t *con,
    ecs_entity_t session,
    const char *line,
    box_console_out_t *out,
    bool *error)
{
    ecs_shell_result_t r = ecs_shell_run(world, session, line);
    char **errors = ecs_vec_first(&r.errors);
    int32_t i, count = ecs_vec_count(&r.errors);
    int32_t at = 0, start, end;
    box_console_fmt_t fmt = {
        .world = world,
        .out = out,
        .scope = box_console_session_scope(world, con)
    };

    for (i = 0; i < BoxTypeCount; i ++) {
        char *path = flecs_asprintf(BOX_CONSOLE_SHELL_TYPE "%s", box_console_type_names[i]);
        fmt.types[i] = ecs_lookup(world, path);
        ecs_os_free(path);
    }

    ecs_value_t *results = ecs_vec_first(&r.results);
    int32_t result_count = count ? 0 : ecs_vec_count(&r.results);
    for (i = 0; i < result_count; i ++) {
        char *command = NULL, *pattern = NULL;
        if (box_console_syntax_split(line, at, &start, &end)) {
            char *text = flecs_asprintf("%.*s", end - start, &line[start]);
            command = box_console_syntax_token(text, 0);
            pattern = box_console_syntax_token(text, 1);
            ecs_os_free(text);
            at = end;
        }

        fmt.command = command;
        fmt.pattern = pattern;
        fmt.number_width = 0;
        fmt.comment = false;
        if (results[i].type && results[i].ptr) {
            if (box_console_out_len(out)) {
                box_console_out_add(out, BoxSpanNone, "\n", 1);
            }

            box_console_fmt_value(&fmt, results[i].type, results[i].ptr);
        }

        ecs_os_free(command);
        ecs_os_free(pattern);
    }

    for (i = 0; i < count; i ++) {
        if (box_console_out_len(out)) {
            box_console_out_add(out, BoxSpanNone, "\n", 1);
        }

        box_console_out_error(out, errors[i]);
    }

    if (error) {
        error[0] = count != 0;
    }

    ecs_shell_result_fini(world, &r);
}
