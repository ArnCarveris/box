/**
 * @file ConsoleOutput.c
 * @brief The output of the in game console is colored by the type of the result of the command,
 * like the terminal of the flecs explorer: the TextSpans of the scrollback lines are checked
 * against the expected byte ranges and colors of the console theme.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Path of the column of the visible scrollback lines */
#define OUTPUT_LOG "console.ui.screen.stack.panel.content.log"

/** Most scrollback elements the tests look at */
#define OUTPUT_LINES_MAX (1024)

/** Open the console over the whole of a tall headless surface, so the output of a command is
 * visible without wrapping, and give every output color of the theme a value of its own so a
 * color tells which kind a span is.
 *
 * @return The world
 */
static ecs_world_t* output_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 2400, .height = 8000, .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    rgb *colors = (rgb*)&c->output;
    int32_t i, count = (int32_t)(sizeof(ConsoleOutput) / sizeof(rgb));
    for (i = 0; i < count; i ++) {
        colors[i] = (rgb){ (uint8_t)(10 + i), (uint8_t)(100 + i), (uint8_t)(200 - i) };
    }

    c->text = (rgb){ 201, 202, 203 };
    c->error = (rgb){ 250, 1, 2 };
    c->width = 1;
    c->scrollback = 2000;
    c->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Run a line in the console and draw it.
 *
 * @param world The world
 * @param line The command line
 */
static void output_run(
    ecs_world_t *world,
    const char *line)
{
    ecs_os_free(box_console_run(world, line, NULL));
    ecs_progress(world, 0);
}

/** Get the scrollback elements of the console, top to bottom.
 *
 * @param world The world
 * @param out Receives the elements
 * @param max Size of out
 * @return Number of elements
 */
static int32_t output_lines(
    ecs_world_t *world,
    ecs_entity_t *out,
    int32_t max)
{
    int32_t result = 0;
    while (result < max) {
        char name[96];
        ecs_os_snprintf(name, sizeof(name), OUTPUT_LOG ".line_%d", result);
        ecs_entity_t e = ecs_lookup(world, name);
        if (!e) {
            break;
        }

        out[result ++] = e;
    }

    return result;
}

/** Find the newest scrollback element whose text starts with a prefix.
 *
 * @param world The world
 * @param prefix The prefix, the whole line when exact is set
 * @param exact Whether the line must be the prefix
 * @return The element, 0 when no element shows such a line
 */
static ecs_entity_t output_find(
    ecs_world_t *world,
    const char *prefix,
    bool exact)
{
    static ecs_entity_t lines[OUTPUT_LINES_MAX];
    ecs_entity_t result = 0;
    int32_t i, count = output_lines(world, lines, OUTPUT_LINES_MAX);
    size_t len = strlen(prefix);
    for (i = count - 1; i >= 0 && !result; i --) {
        const Text *t = ecs_get(world, lines[i], Text);
        if (t && t->value && !strncmp(t->value, prefix, len) && (!exact || !t->value[len])) {
            result = lines[i];
        }
    }

    return result;
}

/** Get the element after a scrollback element.
 *
 * @param world The world
 * @param e The element
 * @return The next element, 0 for the last one
 */
static ecs_entity_t output_next(
    ecs_world_t *world,
    ecs_entity_t e)
{
    static ecs_entity_t lines[OUTPUT_LINES_MAX];
    ecs_entity_t result = 0;
    int32_t i, count = output_lines(world, lines, OUTPUT_LINES_MAX);
    for (i = 0; i < count - 1 && !result; i ++) {
        if (lines[i] == e) {
            result = lines[i + 1];
        }
    }

    return result;
}

/** Get the text of a scrollback element.
 *
 * @param world The world
 * @param e The element
 * @return The text
 */
static const char* output_text(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Text *t = ecs_get(world, e, Text);
    test_assert(t != NULL);
    test_assert(t->value != NULL);

    return t->value;
}

/** Get the span a byte of a Text falls in, the last span that holds it.
 *
 * @param world The world
 * @param e The element
 * @param offset The byte
 * @return The span, NULL when no span holds the byte
 */
static const TextSpan* output_span_at(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t offset)
{
    const TextSpans *spans = ecs_get(world, e, TextSpans);
    const TextSpan *result = NULL;
    int32_t i;
    for (i = 0; spans && i < ecs_vec_count(&spans->spans); i ++) {
        const TextSpan *s = ecs_vec_get_t(&spans->spans, TextSpan, i);
        if (offset >= s->start && offset < s->end) {
            result = s;
        }
    }

    return result;
}

/** Check that every byte of a range of a scrollback line has a color.
 *
 * @param world The world
 * @param e The element
 * @param start First byte
 * @param end Byte after the range, -1 for the end of the line
 * @param color The color, NULL for bytes that no span holds
 */
static void output_check(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t start,
    int32_t end,
    const rgb *color)
{
    int32_t i;
    test_assert(e != 0);
    end = end < 0 ? (int32_t)strlen(output_text(world, e)) : end;
    test_assert(end > start);
    test_assert(end <= (int32_t)strlen(output_text(world, e)));
    for (i = start; i < end; i ++) {
        const TextSpan *s = output_span_at(world, e, i);
        if (!color) {
            test_assert(s == NULL);
            continue;
        }

        test_assert(s != NULL);
        test_int(s->color.r, color->r);
        test_int(s->color.g, color->g);
        test_int(s->color.b, color->b);
        test_int(s->color.a, 255);
    }
}

/** Check the color of the first occurrence of a part of a scrollback line.
 *
 * @param world The world
 * @param e The element
 * @param part The part
 * @param color The color
 * @return Byte the part starts at
 */
static int32_t output_check_part(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *part,
    const rgb *color)
{
    test_assert(e != 0);
    const char *text = output_text(world, e);
    const char *at = strstr(text, part);
    test_assert(at != NULL);
    int32_t start = (int32_t)(at - text);
    output_check(world, e, start, start + (int32_t)strlen(part), color);

    return start;
}

/** Get the output colors of the theme.
 *
 * @param world The world
 * @return The colors
 */
static const ConsoleOutput* output_colors(
    ecs_world_t *world)
{
    return &ecs_singleton_get(world, Console)->output;
}

/** Callback of the test command colors: returns a string with ANSI color codes.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the string
 */
static void output_colors_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv;
    *(char**)result->ptr = ecs_os_strdup(
        "\033[0;31mred\033[0m plain [[0;32mgreen[[0m \033[0;90mgrey\033[0m");
}

/** Callback of the test command plain: returns a string without colors.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the string
 */
static void output_plain_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv;
    *(char**)result->ptr = ecs_os_strdup("just some text 42 true");
}

/** Callback of the test command diffy: returns a unified diff.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the diff
 */
static void output_diff_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv;
    *(char**)result->ptr = ecs_os_strdup(
        "--- a.flecs\n+++ b.flecs\n@@ -1 +1 @@\n-old line\n+new line\n same line");
}

/** Callback of the test command fail: logs an error with a source excerpt below it.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Unused
 */
static void output_fail_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv; (void)result;
    ecs_err("bad thing happened\n    1 + \n        ^");
}

/** Callback of the test command failnl: logs an error with a source excerpt below it that ends
 * in a newline.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Unused
 */
static void output_fail_nl_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv; (void)result;
    ecs_err("bad thing happened\n    1 + \n        ^\n");
}

/** Callback of the test command failone: logs a one line error that ends in a newline.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Unused
 */
static void output_fail_one_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv; (void)result;
    ecs_err("just this\n");
}

/** Add a component with a string member, Label { label }, to an entity.
 *
 * @param world The world
 * @param e The entity
 * @param label The string
 */
static void output_label(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *label)
{
    ecs_entity_t c = ecs_lookup(world, "Label");
    if (!c) {
        c = ecs_struct(world, {
            .entity = ecs_entity(world, { .name = "Label" }),
            .members = {{ .name = "label", .type = ecs_id(ecs_string_t) }}
        });
    }

    test_assert(c != 0);
    char *str = (char*)label;
    ecs_set_id(world, e, c, sizeof(char*), &str);
}

/** Register a test command in the scope of the box commands.
 *
 * @param world The world
 * @param name The command name
 * @param callback The callback
 * @param string Whether the command returns a string
 */
static void output_command(
    ecs_world_t *world,
    const char *name,
    ecs_function_callback_t callback,
    bool string)
{
    ecs_entity_t f = ecs_function_init(world, &(ecs_function_desc_t){
        .name = name,
        .parent = ecs_lookup(world, BOX_COMMAND_SCOPE),
        .return_type = string ? ecs_id(ecs_string_t) : 0,
        .callback = callback
    });
    test_assert(f != 0);
}

void ConsoleOutput_ls_entities(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity(world, { .name = "things.beta" });
    output_run(world, "ls things");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t alpha = output_find(world, "things.alpha", true);
    output_check(world, alpha, 0, 7, &o->dim);
    output_check(world, alpha, 7, 12, &o->entity);
    ecs_entity_t beta = output_find(world, "things.beta", true);
    output_check(world, beta, 0, 7, &o->dim);
    output_check(world, beta, 7, 11, &o->entity);

    // The kind of the line keeps the text color under the spans
    const Rgb *color = ecs_get(world, alpha, Rgb);
    test_int(color->r, 201);

    ecs_fini(world);
}

void ConsoleOutput_ls_relative_to_scope(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "things.alpha.leaf" });
    output_run(world, "cd things");
    output_run(world, "ls");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t alpha = output_find(world, "alpha", true);
    output_check(world, alpha, 0, 5, &o->entity);

    output_run(world, "ls alpha");
    ecs_entity_t leaf = output_find(world, "alpha.leaf", true);
    output_check(world, leaf, 0, 6, &o->dim);
    output_check(world, leaf, 6, 10, &o->entity);

    // The scope itself keeps its full path
    output_run(world, "pwd");
    ecs_entity_t pwd = output_find(world, "things", true);
    output_check(world, pwd, 0, 6, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_tree_guides(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "things.alpha.leaf.deep" });
    ecs_entity(world, { .name = "things.beta.one" });
    ecs_entity(world, { .name = "things.beta.two" });
    output_run(world, "tree things");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t alpha = output_find(world, "alpha", true);
    output_check(world, alpha, 0, 5, &o->entity);
    ecs_entity_t leaf = output_find(world, "`- leaf", true);
    output_check(world, leaf, 0, 3, &o->dim);
    output_check(world, leaf, 3, 7, &o->entity);
    ecs_entity_t deep = output_find(world, "   `- deep", true);
    output_check(world, deep, 0, 6, &o->dim);
    output_check(world, deep, 6, 10, &o->entity);

    // Of two siblings the first has a guide that continues, the last one ends
    ecs_entity_t one = output_find(world, "|- one", true);
    ecs_entity_t two = output_find(world, "`- two", true);
    if (!one) {
        one = output_find(world, "`- one", true);
        two = output_find(world, "|- two", true);
    }

    output_check(world, one, 0, 3, &o->dim);
    output_check(world, one, 3, 6, &o->entity);
    output_check(world, two, 0, 3, &o->dim);
    output_check(world, two, 3, 6, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_query_rows(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t e = ecs_entity(world, { .name = "things.alpha" });
    ecs_set(world, e, Position, {1, 2, 3});
    output_run(world, "query box.transform.Position");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t row = output_find(world, "things.alpha  box.transform.Position: {", false);
    output_check(world, row, 0, 7, &o->dim);
    output_check(world, row, 7, 12, &o->entity);
    output_check(world, row, 12, 14, NULL);
    output_check(world, row, 14, 36, &o->component);
    output_check(world, row, 36, 38, &o->dim);
    output_check_part(world, row, "\"x\"", &o->key);
    output_check_part(world, row, "\"z\"", &o->key);
    int32_t at = output_check_part(world, row, "\"y\"", &o->key);
    const char *text = output_text(world, row);
    const char *two = strchr(&text[at], '2');
    test_assert(two != NULL);
    output_check(world, row, (int32_t)(two - text), (int32_t)(two - text) + 1, &o->number);

    ecs_fini(world);
}

void ConsoleOutput_help_command(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_run(world, "help ls");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t usage = output_find(world, "ls [scope]  ", false);
    output_check(world, usage, 0, 2, &o->command);
    output_check(world, usage, 3, 4, &o->dim);
    output_check(world, usage, 4, 9, &o->param);
    output_check(world, usage, 9, 10, &o->dim);
    output_check(world, usage, 10, 12, NULL);
    output_check(world, usage, 12, -1, &o->brief);

    ecs_entity_t param = output_find(world, "    scope: entity  ", false);
    output_check(world, param, 0, 4, NULL);
    output_check(world, param, 4, 9, &o->param);
    output_check(world, param, 9, 11, &o->dim);
    output_check(world, param, 11, 17, &o->type);
    output_check(world, param, 19, -1, &o->brief);

    ecs_fini(world);
}

void ConsoleOutput_help_groups(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_run(world, "help");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t box = output_find(world, "box", true);
    output_check(world, box, 0, 3, &o->group);
    ecs_entity_t core = output_find(world, "core", true);
    output_check(world, core, 0, 4, &o->group);

    // The commands of box and of the shell look the same
    ecs_entity_t key = output_find(world, "key_press <key> [dur]  ", false);
    output_check(world, key, 0, 9, &o->command);
    output_check(world, key, 10, 11, &o->dim);
    output_check(world, key, 11, 14, &o->param);
    ecs_entity_t ls = output_find(world, "ls [scope]  ", false);
    output_check(world, ls, 0, 2, &o->command);
    output_check(world, ls, 4, 9, &o->param);

    // The console group is last, colored as the other groups
    ecs_entity_t console = output_find(world, "console (console only)", true);
    output_check(world, console, 0, 7, &o->group);
    output_check(world, console, 7, 22, &o->dim);
    ecs_entity_t cls = output_find(world, "cls  ", false);
    output_check(world, cls, 0, 3, &o->command);
    output_check(world, cls, 3, 5, NULL);
    output_check(world, cls, 5, -1, &o->brief);
    test_assert(output_next(world, cls) == 0 ||
        !strcmp(output_text(world, output_next(world, cls)), ""));

    ecs_fini(world);
}

void ConsoleOutput_inspect_entity(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t e = ecs_entity(world, { .name = "things.alpha" });
    ecs_set(world, e, Position, {1, 2, 3});
    output_run(world, "cat things.alpha");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t head = output_find(world, "things.alpha  #", false);
    output_check(world, head, 0, 7, &o->dim);
    output_check(world, head, 7, 12, &o->entity);
    output_check(world, head, 12, -1, &o->dim);

    ecs_entity_t pos = output_find(world, "  box.transform.Position: {", false);
    output_check(world, pos, 0, 2, NULL);
    output_check(world, pos, 2, 24, &o->component);
    output_check(world, pos, 24, 26, &o->dim);
    output_check_part(world, pos, "\"x\"", &o->key);

    ecs_fini(world);
}

void ConsoleOutput_get_value(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t e = ecs_entity(world, { .name = "cube" });
    ecs_set(world, e, Position, {1, 2.5f, -3});
    output_run(world, "get cube box.transform.Position");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t value = output_find(world, "{\"x\":", false);
    output_check(world, value, 0, 1, NULL);
    output_check_part(world, value, "\"x\"", &o->key);
    output_check_part(world, value, "\"y\"", &o->key);
    output_check_part(world, value, "2.5", &o->number);
    output_check_part(world, value, "-3", &o->number);

    ecs_fini(world);
}

void ConsoleOutput_grep_match(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity(world, { .name = "things.beta" });
    output_run(world, "grep alp");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t alpha = output_find(world, "things.alpha", true);
    output_check(world, alpha, 0, 7, &o->dim);
    output_check(world, alpha, 7, 10, &o->match);
    output_check(world, alpha, 10, 12, &o->entity);
    test_assert(output_find(world, "things.beta", true) == 0);

    ecs_fini(world);
}

void ConsoleOutput_script_lines(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "level" }),
        .code = "e {\n  // note\n  f {}\n}"
    });
    test_assert(s != 0);
    output_run(world, "script_read level");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t first = output_find(world, "1  e {", true);
    output_check(world, first, 0, 1, &o->dim);
    output_check(world, first, 3, 4, &o->entity);
    ecs_entity_t note = output_find(world, "2    // note", true);
    output_check(world, note, 0, 1, &o->dim);
    output_check(world, note, 5, -1, &o->dim);
    ecs_entity_t f = output_find(world, "3    f {}", true);
    output_check(world, f, 5, 6, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_diff_lines(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_command(world, "diffy", output_diff_cmd, true);
    output_run(world, "diffy");

    const ConsoleOutput *o = output_colors(world);
    output_check(world, output_find(world, "--- a.flecs", true), 0, -1, &o->dim);
    output_check(world, output_find(world, "+++ b.flecs", true), 0, -1, &o->dim);
    output_check(world, output_find(world, "@@ -1 +1 @@", true), 0, -1, &o->dim);
    output_check(world, output_find(world, "-old line", true), 0, -1, &o->removed);
    output_check(world, output_find(world, "+new line", true), 0, -1, &o->added);
    output_check(world, output_find(world, " same line", true), 0, -1, NULL);

    ecs_fini(world);
}

void ConsoleOutput_error_detail_lines(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_command(world, "fail", output_fail_cmd, false);
    output_run(world, "fail");

    const Console *c = ecs_singleton_get(world, Console);
    const ConsoleOutput *o = &c->output;
    ecs_entity_t err = output_find(world, "error: bad thing happened", true);
    output_check(world, err, 0, 7, &c->error);
    output_check(world, err, 7, -1, &c->text);
    ecs_entity_t detail = output_next(world, err);
    test_str(output_text(world, detail), "    1 + ");
    output_check(world, detail, 0, -1, &o->dim);
    ecs_entity_t caret = output_next(world, detail);
    test_str(output_text(world, caret), "        ^");
    output_check(world, caret, 0, -1, &o->dim);

    // A one line error of a builtin command
    output_run(world, "get nothing");
    ecs_entity_t one = output_find(world, "error: ", false);
    output_check(world, one, 0, 7, &c->error);
    output_check(world, one, 7, -1, &c->text);

    ecs_fini(world);
}

void ConsoleOutput_ansi_string(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_command(world, "colors", output_colors_cmd, true);
    output_run(world, "colors");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t line = output_find(world, "red plain green grey", true);
    test_assert(line != 0);
    output_check(world, line, 0, 3, &o->red);
    output_check(world, line, 3, 10, NULL);
    output_check(world, line, 10, 15, &o->green);
    output_check(world, line, 15, 16, NULL);
    output_check(world, line, 16, 20, &o->grey);

    ecs_fini(world);
}

void ConsoleOutput_string_plain(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_command(world, "plain", output_plain_cmd, true);
    output_run(world, "plain");

    ecs_entity_t line = output_find(world, "just some text 42 true", true);
    test_assert(line != 0);
    test_assert(!ecs_has(world, line, TextSpans));
    const Rgb *color = ecs_get(world, line, Rgb);
    test_int(color->r, 201);
    test_int(color->g, 202);
    test_int(color->b, 203);

    ecs_fini(world);
}

void ConsoleOutput_restyle_keeps_text(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "things.alpha" });
    output_run(world, "ls things");
    ecs_entity_t alpha = output_find(world, "things.alpha", true);
    const Text *text = ecs_get(world, alpha, Text);
    char *before = text->value;

    Console *c = ecs_singleton_get_mut(world, Console);
    c->output.entity = (rgb){ 1, 2, 3 };
    c->output.dim = (rgb){ 4, 5, 6 };
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);

    // Only the spans change, the text of the line is not set again
    test_assert(output_find(world, "things.alpha", true) == alpha);
    test_assert(ecs_get(world, alpha, Text)->value == before);
    output_check(world, alpha, 0, 7, &(rgb){ 4, 5, 6 });
    output_check(world, alpha, 7, 12, &(rgb){ 1, 2, 3 });

    ecs_fini(world);
}

void ConsoleOutput_text_differs_from_command(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "things.alpha.leaf" });
    char *console = box_console_run(world, "tree things", NULL);
    test_str(console, "alpha\n`- leaf");
    ecs_os_free(console);

    // The session of the console still returns the plain text to box_command_run
    ecs_entity_t session = ecs_lookup(world, BOX_CONSOLE_SESSION);
    test_assert(session != 0);
    char *plain = box_command_run(world, session, "tree things", NULL);
    test_assert(strstr(plain, "`- ") == NULL);
    test_assert(strstr(plain, "\"depth\"") != NULL);
    ecs_os_free(plain);

    ecs_fini(world);
}

void ConsoleOutput_grep_case(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t e = ecs_entity(world, { .name = "alpha" });
    output_label(world, e, "Alpha alpine");
    output_run(world, "grep Alp");

    // grep is case sensitive: the name does not match, only the exact case is marked
    const ConsoleOutput *o = output_colors(world);
    test_assert(output_find(world, "alpha", true) == 0);
    ecs_entity_t row = output_find(world, "alpha  Label: ", false);
    output_check(world, row, 0, 5, &o->entity);
    output_check_part(world, row, "Alp", &o->match);
    const char *text = output_text(world, row);
    const char *alpine = strstr(text, "alpine");
    test_assert(alpine != NULL);
    output_check(world, row, (int32_t)(alpine - text), (int32_t)(alpine - text) + 3, NULL);

    // grep_i folds the case, both occurrences and the name are marked
    output_run(world, "grep_i Alp");
    ecs_entity_t name = output_find(world, "alpha", true);
    output_check(world, name, 0, 3, &o->match);
    output_check(world, name, 3, 5, &o->entity);
    row = output_find(world, "alpha  Label: ", false);
    output_check(world, row, 0, 5, &o->entity);
    output_check_part(world, row, "Alp", &o->match);
    text = output_text(world, row);
    alpine = strstr(text, "alpine");
    test_assert(alpine != NULL);
    output_check(world, row, (int32_t)(alpine - text), (int32_t)(alpine - text) + 3, &o->match);

    ecs_fini(world);
}

void ConsoleOutput_find_match(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    ecs_entity(world, { .name = "alps.beta" });
    output_run(world, "find alp");

    // find matches names only, the parent part of the path is not marked
    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t alpha = output_find(world, "alps.alpha", true);
    output_check(world, alpha, 0, 5, &o->dim);
    output_check(world, alpha, 5, 8, &o->match);
    output_check(world, alpha, 8, 10, &o->entity);
    ecs_entity_t alps = output_find(world, "alps", true);
    output_check(world, alps, 0, 3, &o->match);
    output_check(world, alps, 3, 4, &o->entity);
    test_assert(output_find(world, "alps.beta", true) == 0);

    // Other commands that return entities mark nothing
    output_run(world, "ls alps");
    alpha = output_find(world, "alps.alpha", true);
    output_check(world, alpha, 0, 5, &o->dim);
    output_check(world, alpha, 5, 10, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_multi_grep_i_first(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t e = ecs_entity(world, { .name = "alpha" });
    output_label(world, e, "Alpha alphabet");
    ecs_entity(world, { .name = "things.alps.one" });
    output_run(world, "grep_i Alph; tree things");

    // The grep_i result is case folded and marked as when grep_i runs alone
    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t name = output_find(world, "alpha", true);
    output_check(world, name, 0, 4, &o->match);
    output_check(world, name, 4, 5, &o->entity);
    ecs_entity_t row = output_find(world, "alpha  Label: ", false);
    output_check(world, row, 0, 5, &o->entity);
    output_check_part(world, row, "Alph", &o->match);
    const char *text = output_text(world, row);
    const char *alphabet = strstr(text, "alphabet");
    test_assert(alphabet != NULL);
    output_check(world, row, (int32_t)(alphabet - text), (int32_t)(alphabet - text) + 4,
        &o->match);

    // The tree result is formatted as when tree runs alone, without match spans
    ecs_entity_t alps = output_find(world, "alps", true);
    output_check(world, alps, 0, 4, &o->entity);
    ecs_entity_t one = output_find(world, "`- one", true);
    output_check(world, one, 0, 3, &o->dim);
    output_check(world, one, 3, 6, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_multi_grep_i_second(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t e = ecs_entity(world, { .name = "alpha" });
    output_label(world, e, "Alpha alphabet");
    ecs_entity(world, { .name = "things.alps.one" });
    output_run(world, "tree things; grep_i Alph");

    // The tree result is formatted as when tree runs alone
    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t alps = output_find(world, "alps", true);
    output_check(world, alps, 0, 4, &o->entity);
    ecs_entity_t one = output_find(world, "`- one", true);
    output_check(world, one, 0, 3, &o->dim);
    output_check(world, one, 3, 6, &o->entity);

    // The grep_i result is case folded and marked as when grep_i runs alone
    ecs_entity_t name = output_find(world, "alpha", true);
    output_check(world, name, 0, 4, &o->match);
    output_check(world, name, 4, 5, &o->entity);
    ecs_entity_t row = output_find(world, "alpha  Label: ", false);
    output_check(world, row, 0, 5, &o->entity);
    output_check_part(world, row, "Alph", &o->match);
    const char *text = output_text(world, row);
    const char *alphabet = strstr(text, "alphabet");
    test_assert(alphabet != NULL);
    output_check(world, row, (int32_t)(alphabet - text), (int32_t)(alphabet - text) + 4,
        &o->match);

    ecs_fini(world);
}

void ConsoleOutput_multi_find(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    ecs_entity(world, { .name = "alps.beta" });

    // A find second in the line marks its name matches
    const ConsoleOutput *o = output_colors(world);
    output_run(world, "ls alps; find alp");
    ecs_entity_t alpha = output_find(world, "alps.alpha", true);
    output_check(world, alpha, 0, 5, &o->dim);
    output_check(world, alpha, 5, 8, &o->match);
    output_check(world, alpha, 8, 10, &o->entity);

    // A find first in the line does not mark the result of the next command
    output_run(world, "find alp ; ls alps");
    alpha = output_find(world, "alps.alpha", true);
    output_check(world, alpha, 0, 5, &o->dim);
    output_check(world, alpha, 5, 10, &o->entity);
    ecs_entity_t beta = output_find(world, "alps.beta", true);
    output_check(world, beta, 0, 5, &o->dim);
    output_check(world, beta, 5, 9, &o->entity);

    output_run(world, "find alp; ls alps");
    alpha = output_find(world, "alps.alpha", true);
    output_check(world, alpha, 0, 5, &o->dim);
    output_check(world, alpha, 5, 10, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_find_match_escaped(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t things = ecs_entity(world, { .name = "things" });
    ecs_entity_t xy = ecs_new_w_pair(world, EcsChildOf, things);
    ecs_set_name(world, xy, "x.y");
    ecs_entity_t wz = ecs_new_w_pair(world, EcsChildOf, things);
    ecs_set_name(world, wz, "w\\z.q");

    // A backslash in the pattern matches a backslash of the name, never the escape of a '.'
    const ConsoleOutput *o = output_colors(world);
    char *text = box_console_run(world, "find \\", NULL);
    ecs_progress(world, 0);
    test_assert(strstr(text, "x\\.y") == NULL);
    ecs_os_free(text);
    test_assert(output_find(world, "things.x\\.y", true) == 0);
    ecs_entity_t row = output_find(world, "things.w\\z\\.q", true);
    output_check(world, row, 0, 7, &o->dim);
    output_check(world, row, 7, 8, &o->entity);
    output_check(world, row, 8, 9, &o->match);
    output_check(world, row, 9, 13, &o->entity);

    // A pattern over an escaped '.' marks the escape with the '.'
    output_run(world, "find x.y");
    row = output_find(world, "things.x\\.y", true);
    output_check(world, row, 0, 7, &o->dim);
    output_check(world, row, 7, 11, &o->match);
    output_run(world, "find z.q");
    row = output_find(world, "things.w\\z\\.q", true);
    output_check(world, row, 0, 7, &o->dim);
    output_check(world, row, 7, 9, &o->entity);
    output_check(world, row, 9, 13, &o->match);

    // A backslash of the name before its '.' is shown as "\\.", the name part is all of it
    ecs_entity_t vu = ecs_new_w_pair(world, EcsChildOf, things);
    ecs_set_name(world, vu, "v\\.u");
    output_run(world, "find \\.u");
    row = output_find(world, "things.v\\\\.u", true);
    output_check(world, row, 0, 7, &o->dim);
    output_check(world, row, 7, 8, &o->entity);
    output_check(world, row, 8, 12, &o->match);

    // Name matches of grep too
    output_run(world, "grep x.y");
    row = output_find(world, "things.x\\.y", true);
    output_check(world, row, 0, 7, &o->dim);
    output_check(world, row, 7, 11, &o->match);

    ecs_fini(world);
}

void ConsoleOutput_tree_many_children(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t big = ecs_entity(world, { .name = "things.big" });
    ecs_entity_t last = 0;
    int32_t i;
    for (i = 0; i < 20000; i ++) {
        char name[16];
        ecs_os_snprintf(name, sizeof(name), "c%05d", i);
        last = ecs_entity(world, { .name = name, .parent = big });
    }

    ecs_entity(world, { .name = "leaf", .parent = last });
    char *text = box_console_run(world, "tree things", NULL);
    ecs_progress(world, 0);
    test_assert(!strncmp(text, "big\n|- c00000\n|- c00001\n", 24));
    test_assert(strstr(text, "\n|- c10000\n") != NULL);
    size_t len = strlen(text);
    const char *end = "\n|- c19998\n`- c19999\n   `- leaf";
    test_assert(len > strlen(end));
    test_str(&text[len - strlen(end)], end);
    ecs_os_free(text);

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t row = output_find(world, "`- c19999", true);
    output_check(world, row, 0, 3, &o->dim);
    output_check(world, row, 3, 9, &o->entity);
    ecs_entity_t leaf = output_find(world, "   `- leaf", true);
    output_check(world, leaf, 0, 6, &o->dim);
    output_check(world, leaf, 6, 10, &o->entity);
    row = output_find(world, "|- c19998", true);
    output_check(world, row, 0, 3, &o->dim);
    output_check(world, row, 3, 9, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_error_no_blank_line(void) {
    // console/output
    ecs_world_t *world = output_world();
    output_command(world, "failnl", output_fail_nl_cmd, false);
    char *text = box_console_run(world, "failnl", NULL);
    ecs_progress(world, 0);
    size_t len = strlen(text);
    test_assert(len > 0);
    test_assert(text[len - 1] == '^');
    ecs_os_free(text);

    const Console *c = ecs_singleton_get(world, Console);
    const ConsoleOutput *o = &c->output;
    ecs_entity_t err = output_find(world, "error: bad thing happened", true);
    ecs_entity_t detail = output_next(world, err);
    test_str(output_text(world, detail), "    1 + ");
    ecs_entity_t caret = output_next(world, detail);
    test_str(output_text(world, caret), "        ^");
    output_check(world, caret, 0, -1, &o->dim);
    test_assert(output_find(world, "        ^", true) == caret);

    // The error of a builtin command with its source excerpt
    text = box_console_run(world, "rm nothing", NULL);
    ecs_progress(world, 0);
    len = strlen(text);
    test_assert(len > 0);
    test_assert(text[len - 1] != '\n');
    test_assert(text[len - 1] != '\r');
    ecs_os_free(text);
    err = output_find(world, "error: 1: unresolved identifier 'nothing'", true);
    output_check(world, err, 0, 7, &c->error);
    output_check(world, err, 7, -1, &c->text);
    detail = output_next(world, err);
    test_str(output_text(world, detail), "nothing");
    output_check(world, detail, 0, -1, &o->dim);
    caret = output_next(world, detail);
    test_str(output_text(world, caret), "^");
    output_check(world, caret, 0, -1, &o->dim);
    ecs_entity_t after = output_next(world, caret);
    test_assert(!after || output_text(world, after)[0] != '\0');

    // A one line error that ends in a newline has no detail
    output_command(world, "failone", output_fail_one_cmd, false);
    text = box_console_run(world, "failone", NULL);
    test_str(text, "error: just this");
    ecs_os_free(text);

    ecs_fini(world);
}

void ConsoleOutput_script_block_comment(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "level" }),
        .code = "e {}\n/* one\n   two\n   three */ f {}\n// a /* b\nh {}"
    });
    test_assert(s != 0);
    output_run(world, "script_read level");

    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t first = output_find(world, "2  /* one", true);
    output_check(world, first, 0, 1, &o->dim);
    output_check(world, first, 3, -1, &o->dim);
    ecs_entity_t two = output_find(world, "3     two", true);
    output_check(world, two, 0, 1, &o->dim);
    output_check(world, two, 3, -1, &o->dim);
    ecs_entity_t three = output_find(world, "4     three */ f {}", true);
    output_check(world, three, 3, 14, &o->dim);
    output_check(world, three, 14, 15, NULL);
    output_check(world, three, 15, 16, &o->entity);

    // A /* in a line comment does not start a block comment
    ecs_entity_t note = output_find(world, "5  // a /* b", true);
    output_check(world, note, 3, -1, &o->dim);
    ecs_entity_t h = output_find(world, "6  h {}", true);
    output_check(world, h, 3, 4, &o->entity);
    output_check(world, h, 4, -1, NULL);

    // The state does not carry into the next command
    output_run(world, "script_read level");
    ecs_entity_t e = output_find(world, "1  e {}", true);
    output_check(world, e, 3, 4, &o->entity);

    ecs_fini(world);
}

/** Callback of the test command minus: returns a string that starts with a '-'.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the string
 */
static void output_minus_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx; (void)argc; (void)argv;
    *(char**)result->ptr = ecs_os_strdup("-not a diff\n+nor this");
}

void ConsoleOutput_multi_cd_ls(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    ecs_entity(world, { .name = "alps.beta" });
    output_run(world, "cd alps; ls; cd ..");

    // The ls rows are relative to alps, the scope the ls ran in
    const ConsoleOutput *o = output_colors(world);
    test_assert(output_find(world, "alps.alpha", true) == 0);
    test_assert(output_find(world, "alps.beta", true) == 0);
    ecs_entity_t alpha = output_find(world, "alpha", true);
    output_check(world, alpha, 0, 5, &o->entity);
    ecs_entity_t beta = output_find(world, "beta", true);
    output_check(world, beta, 0, 4, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_multi_ls_cd(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    ecs_entity(world, { .name = "alps.beta" });
    output_run(world, "ls alps; cd alps");

    // The ls rows are relative to the root, as when ls alps runs alone
    const ConsoleOutput *o = output_colors(world);
    test_assert(output_find(world, "alpha", true) == 0);
    ecs_entity_t alpha = output_find(world, "alps.alpha", true);
    output_check(world, alpha, 0, 5, &o->dim);
    output_check(world, alpha, 5, 10, &o->entity);
    ecs_entity_t beta = output_find(world, "alps.beta", true);
    output_check(world, beta, 0, 5, &o->dim);
    output_check(world, beta, 5, 9, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_multi_cd_find(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    ecs_entity(world, { .name = "alps.beta" });
    output_run(world, "cd alps; find alp; cd ..");

    // The find result is relative to alps, its name match is on the shown text
    const ConsoleOutput *o = output_colors(world);
    test_assert(output_find(world, "alps.alpha", true) == 0);
    ecs_entity_t alpha = output_find(world, "alpha", true);
    output_check(world, alpha, 0, 3, &o->match);
    output_check(world, alpha, 3, 5, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_multi_cd_grep(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    output_run(world, "cd alps; grep alp; cd ..");

    // The grep name match is relative to alps and marked on the shown text
    const ConsoleOutput *o = output_colors(world);
    test_assert(output_find(world, "alps.alpha", true) == 0);
    ecs_entity_t alpha = output_find(world, "alpha", true);
    output_check(world, alpha, 0, 3, &o->match);
    output_check(world, alpha, 3, 5, &o->entity);

    ecs_fini(world);
}

void ConsoleOutput_multi_script_diff(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "level" }),
        .code = "e {}\n"
    });
    test_assert(s != 0);
    test_assert(ecs_script_update(world, s, 0, "e {}\ng {}\n") == 0);
    output_command(world, "minus", output_minus_cmd, true);
    output_run(world, "script_diff level; minus");

    // The script_diff result is colored as a diff
    const ConsoleOutput *o = output_colors(world);
    ecs_entity_t added = output_find(world, "+g {}", true);
    output_check(world, added, 0, -1, &o->added);

    // The string of the next command is not a diff
    ecs_entity_t minus = output_find(world, "-not a diff", true);
    test_assert(minus != 0);
    test_assert(!ecs_has(world, minus, TextSpans));
    ecs_entity_t plus = output_find(world, "+nor this", true);
    test_assert(plus != 0);
    test_assert(!ecs_has(world, plus, TextSpans));

    ecs_fini(world);
}

void ConsoleOutput_multi_error_stops(void) {
    // console/output
    ecs_world_t *world = output_world();
    ecs_entity(world, { .name = "alps.alpha" });
    output_run(world, "ls alps; rm nothing; cd alps");

    // Only the error is shown and the commands after it do not run
    test_assert(output_find(world, "error: ", false) != 0);
    test_assert(output_find(world, "alps.alpha", true) == 0);
    output_run(world, "ls");
    test_assert(output_find(world, "alps", true) != 0);
    test_assert(output_find(world, "alpha", true) == 0);

    ecs_fini(world);
}
