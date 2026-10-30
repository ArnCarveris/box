/**
 * @file console/edit.c
 * @brief The input line, its history and the scrollback, and running a line in the session.
 */

#include "console.h"
#include <string.h>

/** Find the start of the character after a byte of the input line.
 *
 * Characters are decoded with box_utf8_next, so each maximal subpart of an ill formed
 * sequence is one character, as text draws it.
 *
 * @param con The console state
 * @param at The byte, at the start of a character
 * @return The first byte of the next character, the length at the end
 */
static int32_t box_console_next(
    const box_console_t *con,
    int32_t at)
{
    const char *ptr = &con->line[at];
    if (at < con->length) {
        box_utf8_next(&ptr);
        at = (int32_t)(ptr - con->line);
    }

    return at < con->length ? at : con->length;
}

/** Find the start of the character before a byte of the input line.
 *
 * Decodes from the start of the line, since only a forward read finds the maximal subparts.
 *
 * @param con The console state
 * @param at The byte, at the start of a character
 * @return The first byte of the character before it, 0 at the start
 */
static int32_t box_console_prev(
    const box_console_t *con,
    int32_t at)
{
    int32_t result = 0, next = 0;
    while (next < at) {
        result = next;
        next = box_console_next(con, next);
    }

    return result;
}

/** Stop walking the history, so the next Up filters on the line as it is then.
 *
 * @param con The console state
 */
static void box_console_history_leave(
    box_console_t *con)
{
    con->history_at = ecs_vec_count(&con->history);
    ecs_os_free(con->draft);
    con->draft = NULL;
}

/** Insert bytes at the cursor and move the cursor after them.
 *
 * @param con The console state
 * @param text The bytes
 * @param size Number of bytes
 */
static void box_console_insert(
    box_console_t *con,
    const char *text,
    int32_t size)
{
    if (size <= 0) {
        goto done;
    }

    con->line = ecs_os_realloc(con->line, con->length + size + 1);
    memmove(&con->line[con->cursor + size], &con->line[con->cursor],
        (size_t)(con->length - con->cursor + 1));
    memcpy(&con->line[con->cursor], text, (size_t)size);
    con->length += size;
    con->cursor += size;
    con->refresh = true;
    box_console_history_leave(con);
done:
    return;
}

/** Remove a range of bytes of the input line and put the cursor where it started.
 *
 * @param con The console state
 * @param from The first byte
 * @param to The byte after the range
 */
static void box_console_erase(
    box_console_t *con,
    int32_t from,
    int32_t to)
{
    if (to > from) {
        memmove(&con->line[from], &con->line[to], (size_t)(con->length - to + 1));
        con->length -= to - from;
        con->cursor = from;
        con->refresh = true;
        box_console_history_leave(con);
    }
}

/** Replace the input line and put the cursor at its end.
 *
 * @param con The console state
 * @param text The new line
 */
static void box_console_set_line(
    box_console_t *con,
    const char *text)
{
    ecs_os_free(con->line);
    con->line = ecs_os_strdup(text ? text : "");
    con->length = (int32_t)strlen(con->line);
    con->cursor = con->length;
    con->refresh = true;
}

void box_console_insert_text(
    box_console_t *con,
    const char *text,
    int32_t size)
{
    int32_t i, start = 0;
    for (i = 0; i <= size; i ++) {
        unsigned char ch = i < size ? (unsigned char)text[i] : 0;
        if ((ch < 0x20 || ch == 0x7f) && i > start) {
            box_console_insert(con, &text[start], i - start);
        }

        if (ch < 0x20 || ch == 0x7f) {
            start = i + 1;
        }
    }
}

/** Test whether a line of the history starts with the line typed before the walk.
 *
 * @param con The console state
 * @param at The line of the history
 * @return Whether the walk shows it
 */
static bool box_console_history_match(
    const box_console_t *con,
    int32_t at)
{
    const char *line = ecs_vec_get_t(&con->history, char*, at)[0];
    const char *filter = con->draft ? con->draft : "";

    return !strncmp(line, filter, strlen(filter));
}

/** Show an older line of the history that starts with the line typed before the first Up,
 * keeping that line as the draft.
 *
 * @param con The console state
 */
static void box_console_history_prev(
    box_console_t *con)
{
    int32_t at, count = ecs_vec_count(&con->history);
    if (con->history_at >= count) {
        ecs_os_free(con->draft);
        con->draft = ecs_os_strdup(con->line);
        con->history_at = count;
    }

    for (at = con->history_at - 1; at >= 0 && !box_console_history_match(con, at); at --) { }
    if (at >= 0) {
        con->history_at = at;
        box_console_set_line(con, ecs_vec_get_t(&con->history, char*, at)[0]);
    }

    if (con->history_at >= count) {
        box_console_history_leave(con);
    }
}

/** Show a newer line of the history that starts with the draft, or the draft after the newest
 * one.
 *
 * @param con The console state
 */
static void box_console_history_next(
    box_console_t *con)
{
    int32_t at, count = ecs_vec_count(&con->history);
    if (con->history_at >= count) {
        goto done;
    }

    for (at = con->history_at + 1; at < count && !box_console_history_match(con, at); at ++) { }
    if (at < count) {
        con->history_at = at;
        box_console_set_line(con, ecs_vec_get_t(&con->history, char*, at)[0]);
    } else {
        box_console_set_line(con, con->draft);
        box_console_history_leave(con);
    }

done:
    return;
}

const char* box_console_ghost(
    const box_console_t *con)
{
    const char *result = NULL;
    int32_t at = ecs_vec_count(&con->history) - 1;
    if (!con->length || con->cursor != con->length || con->history_at < at + 1) {
        goto done;
    }

    for (; at >= 0 && !result; at --) {
        const char *line = ecs_vec_get_t(&con->history, char*, at)[0];
        if ((int32_t)strlen(line) > con->length &&
            !strncmp(line, con->line, (size_t)con->length))
        {
            result = &line[con->length];
        }
    }

done:
    return result;
}

/** Add a line to the history unless it repeats the newest one, and leave the history walk.
 *
 * @param con The console state
 * @param line The line that ran
 */
static void box_console_history_add(
    box_console_t *con,
    const char *line)
{
    int32_t count = ecs_vec_count(&con->history);
    char **last = count ? ecs_vec_get_t(&con->history, char*, count - 1) : NULL;
    if (!last || strcmp(*last, line)) {
        if (count >= BOX_CONSOLE_HISTORY_MAX) {
            ecs_os_free(ecs_vec_first_t(&con->history, char*)[0]);
            ecs_vec_remove_ordered_t(&con->history, char*, 0);
        }

        ecs_vec_append_t(NULL, &con->history, char*)[0] = ecs_os_strdup(line);
    }

    box_console_history_leave(con);
}

/** Free the text and spans of a scrollback line.
 *
 * @param l The line
 */
static void box_console_line_fini(
    box_console_line_t *l)
{
    ecs_os_free(l->text);
    ecs_vec_fini_t(NULL, &l->spans, box_console_span_t);
}

/** Give a scrollback line the part of the spans of a text that falls on it.
 *
 * @param l The line
 * @param spans The box_console_span_t of the text, NULL for none
 * @param from Byte of the text the line starts at
 * @param size Bytes of the text on the line
 * @param shift Bytes in front of that part on the line, the prompt
 */
static void box_console_line_spans(
    box_console_line_t *l,
    const ecs_vec_t *spans,
    int32_t from,
    int32_t size,
    int32_t shift)
{
    int32_t i, count = spans ? ecs_vec_count(spans) : 0;
    for (i = 0; i < count; i ++) {
        box_console_span_t s = *ecs_vec_get_t(spans, box_console_span_t, i);
        s.start = s.start > from ? s.start : from;
        s.end = s.end < from + size ? s.end : from + size;
        if (s.end > s.start) {
            s.start += shift - from;
            s.end += shift - from;
            *ecs_vec_append_t(NULL, &l->spans, box_console_span_t) = s;
        }
    }
}

/** Append text to the scrollback, one line per newline, without a trailing empty line.
 *
 * @param con The console state
 * @param prefix Text in front of the first line, NULL for none
 * @param text The text
 * @param kind What the lines show
 * @param spans The box_console_span_t of the text the lines keep, NULL for none
 */
static void box_console_append(
    box_console_t *con,
    const char *prefix,
    const char *text,
    box_console_kind_t kind,
    const ecs_vec_t *spans)
{
    const char *p = text;
    bool first = true;
    while (p[0] || first) {
        const char *nl = strchr(p, '\n');
        int32_t size = nl ? (int32_t)(nl - p) : (int32_t)strlen(p);
        int32_t shift = first && prefix ? (int32_t)strlen(prefix) : 0;
        box_console_line_t *l = ecs_vec_append_t(NULL, &con->lines, box_console_line_t);
        l->kind = kind;
        l->rows = 0;
        l->hang = shift ? shift : (int32_t)strspn(p, " \t");
        l->hang = l->hang < size + shift ? l->hang : size + shift;
        ecs_vec_init_t(NULL, &l->spans, box_console_span_t, 0);
        box_console_line_spans(l, spans, (int32_t)(p - text), size, shift);
        if (first && prefix) {
            l->text = flecs_asprintf("%s%.*s", prefix, size, p);
        } else {
            l->text = flecs_asprintf("%.*s", size, p);
        }

        first = false;
        p += size;
        if (nl) {
            p ++;
        }
    }
}

static void box_console_trim(
    ecs_world_t *world,
    box_console_t *con);

/** Append a line that ran or was abandoned to the scrollback after the prompt it was typed
 * at, which the line keeps with the colors of its scope and $.
 *
 * @param con The console state
 * @param prompt The prompt
 * @param prompt_spans The box_console_span_t of the prompt
 * @param line The command line
 * @param spans The box_console_span_t of the command line
 * @param suffix Text after the line, NULL for none
 */
static void box_console_echo(
    box_console_t *con,
    const char *prompt,
    const ecs_vec_t *prompt_spans,
    const char *line,
    const ecs_vec_t *spans,
    const char *suffix)
{
    int32_t i, first = ecs_vec_count(&con->lines);
    char *text = flecs_asprintf("%s%s", line, suffix ? suffix : "");
    box_console_line_t *l;
    box_console_append(con, prompt, text, BoxConsoleCommand, spans);
    l = ecs_vec_get_t(&con->lines, box_console_line_t, first);
    for (i = 0; i < ecs_vec_count(prompt_spans); i ++) {
        *ecs_vec_append_t(NULL, &l->spans, box_console_span_t) =
            *ecs_vec_get_t(prompt_spans, box_console_span_t, i);
    }

    ecs_os_free(text);
}

/** Append the input line to the scrollback after the current prompt, with ^C after it, and
 * clear it without running it.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_interrupt(
    ecs_world_t *world,
    box_console_t *con)
{
    ecs_vec_t spans, prompt_spans;
    char *prompt;
    ecs_vec_init_t(NULL, &spans, box_console_span_t, 0);
    ecs_vec_init_t(NULL, &prompt_spans, box_console_span_t, 0);
    box_console_syntax(world, con, con->line, &spans);
    prompt = box_console_prompt(world, con, &prompt_spans);
    box_console_echo(con, prompt, &prompt_spans, con->line, &spans, BOX_CONSOLE_INTERRUPT);
    ecs_vec_fini_t(NULL, &spans, box_console_span_t);
    ecs_vec_fini_t(NULL, &prompt_spans, box_console_span_t);
    ecs_os_free(prompt);
    box_console_set_line(con, "");
    box_console_history_leave(con);
    box_console_trim(world, con);
    con->scroll = 0;
}

void box_console_greet(
    ecs_world_t *world,
    box_console_t *con)
{
    const char *hint = BOX_CONSOLE_HINT;
    const char *help = strstr(hint, " " BOX_CONSOLE_HINT_HELP " ") + 1;
    int32_t count = ecs_vec_count(&con->lines);
    box_console_line_t *l;
    if (con->greeted) {
        goto done;
    }

    con->greeted = true;
    ecs_vec_append_t(NULL, &con->lines, box_console_line_t);
    l = ecs_vec_first_t(&con->lines, box_console_line_t);
    memmove(&l[1], &l[0], (size_t)count * sizeof(box_console_line_t));
    l->text = ecs_os_strdup(hint);
    l->kind = BoxConsoleOutput;
    l->hang = 0;
    l->rows = 0;
    ecs_vec_init_t(NULL, &l->spans, box_console_span_t, 0);
    *ecs_vec_append_t(NULL, &l->spans, box_console_span_t) = (box_console_span_t){
        .start = (int32_t)(help - hint),
        .end = (int32_t)(help - hint) + (int32_t)strlen(BOX_CONSOLE_HINT_HELP),
        .kind = BoxSyntaxName };
    box_console_trim(world, con);
    con->refresh = true;
done:
    return;
}

/** Drop the oldest lines of the scrollback while it has more rows than the console keeps.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_trim(
    ecs_world_t *world,
    box_console_t *con)
{
    const Console *console = ecs_singleton_get(world, Console);
    int32_t keep = console && console->scrollback > 0 ? console->scrollback : 0;
    int32_t i, count = ecs_vec_count(&con->lines), drop = 0;
    int32_t rows = console ? box_console_scrollback_rows(world, con, console) : count;
    box_console_line_t *lines = ecs_vec_first_t(&con->lines, box_console_line_t);
    while (drop < count && rows > keep) {
        rows -= console ? box_console_line_rows(world, con, console, &lines[drop]) : 1;
        drop ++;
    }

    if (!drop) {
        goto done;
    }

    for (i = 0; i < drop; i ++) {
        box_console_line_fini(&lines[i]);
    }

    memmove(lines, &lines[drop], (size_t)(count - drop) * sizeof(box_console_line_t));
    ecs_vec_set_count_t(NULL, &con->lines, box_console_line_t, count - drop);
done:
    return;
}

/** Get the session of the console, opening it when it does not exist.
 *
 * @param world The world
 * @param con The console state
 * @return The session entity, 0 when it could not be opened
 */
static ecs_entity_t box_console_session(
    ecs_world_t *world,
    box_console_t *con)
{
    if (!con->session || !ecs_is_alive(world, con->session)) {
        con->session = ecs_shell_session_init(world, &(ecs_shell_session_desc_t){
            .entity = ecs_entity(world, {
                .name = "session", .parent = box_console_scope(world, con) })
        });
    }

    return con->session;
}

/** A command the console runs itself, without passing the line to its session.
 *
 * @param world The world
 * @param con The console state
 * @param args Number of arguments after the command name
 * @param error Set to whether the command failed
 * @return The output, which the caller frees
 */
typedef char* (*box_console_command_cb)(
    ecs_world_t *world,
    box_console_t *con,
    int32_t args,
    bool *error);

/** A console command, listed by help in the console group */
typedef struct box_console_command_t {
    const char *name;               /**< Command name */
    const char *brief;              /**< One line description */
    box_console_command_cb run;     /**< Runs after the line is in the scrollback */
} box_console_command_t;

/** Spaces between the name column and the brief, as help lists the session commands */
#define BOX_CONSOLE_HELP_GAP (2)

/** Name of the help group of the console commands */
#define BOX_CONSOLE_HELP_GROUP "console"

/** The cls command: empty the scrollback, including the line of cls itself.
 *
 * @param world The world
 * @param con The console state
 * @param args Number of arguments, cls takes none
 * @param error Set to whether the command failed
 * @return Nothing, or the error for arguments
 */
static char* box_console_cls(
    ecs_world_t *world,
    box_console_t *con,
    int32_t args,
    bool *error)
{
    char *result = NULL;
    (void)world;
    if (args) {
        result = flecs_asprintf(BOX_COMMAND_ERROR "cls takes no arguments, got %d", args);
        error[0] = true;
    } else {
        box_console_clear(con);
        result = ecs_os_strdup("");
    }

    return result;
}

/** The commands only the console runs */
static const box_console_command_t box_console_commands[] = {
    { "cls", "Clear the scrollback of the console, Ctrl+L does the same", box_console_cls }
};

/** Number of console commands */
#define BOX_CONSOLE_COMMAND_COUNT \
    ((int32_t)(sizeof(box_console_commands) / sizeof(box_console_commands[0])))

/** Find the next whitespace separated word of a line.
 *
 * @param p The text after the previous word, moved after the word
 * @param len Receives the length of the word
 * @return The start of the word, NULL at the end of the line
 */
static const char* box_console_word(
    const char **p,
    int32_t *len)
{
    const char *result = *p + strspn(*p, " \t\n\r");
    *len = (int32_t)strcspn(result, " \t\n\r");
    *p = result + *len;

    return *len ? result : NULL;
}

/** Find the console command of a word.
 *
 * @param word The word
 * @param len Length of the word
 * @return The command, NULL when the word is not a console command
 */
static const box_console_command_t* box_console_command(
    const char *word,
    int32_t len)
{
    const box_console_command_t *result = NULL;
    int32_t i;
    for (i = 0; word && i < BOX_CONSOLE_COMMAND_COUNT && !result; i ++) {
        const char *name = box_console_commands[i].name;
        if ((int32_t)strlen(name) == len && !strncmp(word, name, (size_t)len)) {
            result = &box_console_commands[i];
        }
    }

    return result;
}

bool box_console_is_command(
    const char *word,
    int32_t len)
{
    return box_console_command(word, len) != NULL;
}

/** Append the console group of help: a header and the console commands with their briefs, in
 * the colors of the groups and commands of the session help.
 *
 * @param out The output
 * @param only The command to list, NULL for all of them
 */
static void box_console_help(
    box_console_out_t *out,
    const box_console_command_t *only)
{
    int32_t i, len, width = 0;
    box_console_out_add(out, BoxOutputGroup, BOX_CONSOLE_HELP_GROUP, -1);
    box_console_out_add(out, BoxOutputDim, " (console only)", -1);
    for (i = 0; i < BOX_CONSOLE_COMMAND_COUNT; i ++) {
        len = (int32_t)strlen(box_console_commands[i].name);
        width = len > width ? len : width;
    }

    for (i = 0; i < BOX_CONSOLE_COMMAND_COUNT; i ++) {
        const box_console_command_t *cmd = &box_console_commands[i];
        if (!only || only == cmd) {
            len = (int32_t)strlen(cmd->name);
            box_console_out_add(out, BoxSpanNone, "\n", 1);
            box_console_out_add(out, BoxOutputCommand, cmd->name, len);
            ecs_strbuf_append(&out->buf, "%*s", width + BOX_CONSOLE_HELP_GAP - len, "");
            box_console_out_add(out, BoxOutputBrief, cmd->brief, -1);
        }
    }
}

char* box_console_exec(
    ecs_world_t *world,
    box_console_t *con,
    const char *line,
    bool *error)
{
    const char *p = line, *word, *topic;
    int32_t len, topic_len, skip, args;
    const box_console_command_t *cmd, *help_cmd = NULL;
    bool failed = false, help, help_group;
    char *result = NULL, *prompt;
    box_console_out_t out;
    ecs_vec_t spans, prompt_spans;
    box_console_out_init(&out);
    ecs_vec_init_t(NULL, &spans, box_console_span_t, 0);
    ecs_vec_init_t(NULL, &prompt_spans, box_console_span_t, 0);
    box_console_syntax(world, con, line, &spans);
    prompt = box_console_prompt(world, con, &prompt_spans);
    word = box_console_word(&p, &len);
    topic = box_console_word(&p, &topic_len);
    for (args = topic != NULL; box_console_word(&p, &skip); args ++) { }
    cmd = box_console_command(word, len);
    help = word && len == 4 && !strncmp(word, "help", 4) && args <= 1;
    help_cmd = help ? box_console_command(topic, topic_len) : NULL;
    help_group = help && topic && topic_len == (int32_t)strlen(BOX_CONSOLE_HELP_GROUP) &&
        !strncmp(topic, BOX_CONSOLE_HELP_GROUP, (size_t)topic_len);
    if (help_cmd || help_group) {
        box_console_help(&out, help_cmd);
    } else if (!cmd) {
        box_console_out_run(world, con, box_console_session(world, con), line, &out, &failed);
    }

    if (help && !topic && !failed) {
        if (box_console_out_len(&out)) {
            box_console_out_add(&out, BoxSpanNone, "\n\n", 2);
        }

        box_console_help(&out, NULL);
    }

    box_console_history_add(con, line);
    box_console_echo(con, prompt, &prompt_spans, line, &spans, NULL);
    ecs_vec_fini_t(NULL, &spans, box_console_span_t);
    ecs_vec_fini_t(NULL, &prompt_spans, box_console_span_t);
    ecs_os_free(prompt);
    if (cmd) {
        char *text = cmd->run(world, con, args, &failed);
        if (failed) {
            box_console_out_error(&out, text);
        } else {
            box_console_out_ansi(&out, BoxSpanNone, text, -1);
        }

        ecs_os_free(text);
    }

    result = box_console_out_text(&out);
    if (result[0]) {
        box_console_append(con, NULL, result, failed ? BoxConsoleError : BoxConsoleOutput,
            &out.spans);
    }

    box_console_out_fini(&out);
    box_console_trim(world, con);
    con->scroll = 0;
    con->refresh = true;
    if (error) {
        error[0] = failed;
    }

    return result;
}

void box_console_clear(
    box_console_t *con)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&con->lines); i ++) {
        box_console_line_fini(ecs_vec_get_t(&con->lines, box_console_line_t, i));
    }

    ecs_vec_clear(&con->lines);
    con->scroll = 0;
    con->refresh = true;
}

/** Run the input line and clear it, or only clear it when it is blank.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_submit(
    ecs_world_t *world,
    box_console_t *con)
{
    char *line = con->line;
    bool blank = strspn(line, " \t") == (size_t)con->length;
    con->line = ecs_os_strdup("");
    con->length = con->cursor = 0;
    con->refresh = true;
    if (!blank) {
        ecs_os_free(box_console_exec(world, con, line, NULL));
    }

    ecs_os_free(line);
}

/** Scroll the scrollback by a number of rows, positive toward older rows.
 *
 * @param world The world
 * @param con The console state
 * @param rows Rows to scroll
 */
static void box_console_scroll(
    ecs_world_t *world,
    box_console_t *con,
    int32_t rows)
{
    const Console *console = ecs_singleton_get(world, Console);
    int32_t total = console ? box_console_scrollback_rows(world, con, console) :
        ecs_vec_count(&con->lines);
    int32_t max = total - con->rows;
    con->scroll += rows;
    if (con->scroll > max) {
        con->scroll = max;
    }

    if (con->scroll < 0) {
        con->scroll = 0;
    }

    con->refresh = true;
}

/** Find the start of the word before the cursor: the whitespace before the cursor and the
 * characters up to the whitespace before them, as Ctrl+W deletes.
 *
 * @param con The console state
 * @return The first byte of the word
 */
static int32_t box_console_word_start(
    const box_console_t *con)
{
    int32_t result = con->cursor;
    while (result > 0 && (con->line[result - 1] == ' ' || con->line[result - 1] == '\t')) {
        result --;
    }

    while (result > 0 && con->line[result - 1] != ' ' && con->line[result - 1] != '\t') {
        result --;
    }

    return result;
}

/** Apply the readline keys pressed with Ctrl held: move to the start or end, delete to the
 * start, the end or the start of the word, abandon the line or clear the scrollback.
 *
 * @param world The world
 * @param con The console state
 */
static void box_console_chords(
    ecs_world_t *world,
    box_console_t *con)
{
    const bool *chord = con->chord;
    if (chord[KeyA] || chord[KeyE]) {
        con->cursor = chord[KeyA] ? 0 : con->length;
        con->refresh = true;
    }

    if (chord[KeyU]) {
        box_console_erase(con, 0, con->cursor);
    }

    if (chord[KeyK]) {
        box_console_erase(con, con->cursor, con->length);
        con->cursor = con->length;
    }

    if (chord[KeyW]) {
        box_console_erase(con, box_console_word_start(con), con->cursor);
    }

    if (chord[KeyC]) {
        box_console_interrupt(world, con);
    }

    if (chord[KeyL]) {
        box_console_clear(con);
    }
}

/** Accept the suggestion of the history: append the rest of the history line to the input.
 *
 * @param con The console state
 * @return Whether there was a suggestion
 */
static bool box_console_accept(
    box_console_t *con)
{
    const char *ghost = box_console_ghost(con);
    char *rest = ghost ? ecs_os_strdup(ghost) : NULL;
    if (rest) {
        box_console_insert(con, rest, (int32_t)strlen(rest));
    }

    ecs_os_free(rest);

    return rest != NULL;
}

bool box_console_edit(
    ecs_world_t *world,
    box_console_t *con)
{
    const bool *key = con->pressed;
    int32_t page = con->rows - 1;
    page = page > 0 ? page : 1;
    box_console_insert_text(con, ecs_vec_first(&con->typed), ecs_vec_count(&con->typed));
    if ((key[KeyRight] || key[KeyEnd]) && box_console_accept(con)) {
        con->pressed[KeyRight] = con->pressed[KeyEnd] = false;
    }

    if (key[KeyBackspace]) {
        box_console_erase(con, box_console_prev(con, con->cursor), con->cursor);
    }

    if (key[KeyDelete]) {
        box_console_erase(con, con->cursor, box_console_next(con, con->cursor));
    }

    if (key[KeyLeft] || key[KeyRight] || key[KeyHome] || key[KeyEnd]) {
        con->cursor = key[KeyLeft] ? box_console_prev(con, con->cursor) : con->cursor;
        con->cursor = key[KeyRight] ? box_console_next(con, con->cursor) : con->cursor;
        con->cursor = key[KeyHome] ? 0 : con->cursor;
        con->cursor = key[KeyEnd] ? con->length : con->cursor;
        con->refresh = true;
    }

    if (key[KeyUp]) {
        box_console_history_prev(con);
    }

    if (key[KeyDown]) {
        box_console_history_next(con);
    }

    if (key[KeyPageUp] || key[KeyPageDown] || con->wheel != 0) {
        box_console_scroll(world, con, (key[KeyPageUp] ? page : 0) - (key[KeyPageDown] ? page : 0) +
            (int32_t)(con->wheel * BOX_CONSOLE_WHEEL_LINES));
    }

    box_console_chords(world, con);

    if (key[KeyEnter] || key[KeyKpEnter]) {
        box_console_submit(world, con);
    }

    return key[KeyEscape];
}

void box_console_edit_fini(
    box_console_t *con)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&con->lines); i ++) {
        box_console_line_fini(ecs_vec_get_t(&con->lines, box_console_line_t, i));
    }

    for (i = 0; i < ecs_vec_count(&con->history); i ++) {
        ecs_os_free(ecs_vec_get_t(&con->history, char*, i)[0]);
    }

    ecs_vec_fini_t(NULL, &con->lines, box_console_line_t);
    ecs_vec_fini_t(NULL, &con->history, char*);
    ecs_vec_fini_t(NULL, &con->ui.lines, ecs_entity_t);
    ecs_vec_fini_t(NULL, &con->typed, char);
    ecs_vec_fini_t(NULL, &con->spans, box_console_span_t);
    ecs_os_free(con->colored);
    ecs_os_free(con->draft);
    ecs_os_free(con->line);
}
