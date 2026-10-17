/**
 * @file ConsoleSyntax.c
 * @brief Coloring of command lines in the console: the input line and the lines that ran.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define SYNTAX_INPUT "console.ui.screen.stack.panel.content.input"
#define SYNTAX_LOG "console.ui.screen.stack.panel.content.log"

/** Create a headless world with a surface and open the console.
 *
 * @return The world
 */
static ecs_world_t* syntax_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    c->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);

    return world;
}

/** Run frames until the synthetic input queue is empty, then one more.
 *
 * @param world The world
 */
static void syntax_flush(
    ecs_world_t *world)
{
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
}

/** Type text into the input line of the console.
 *
 * @param world The world
 * @param text The text
 */
static void syntax_type(
    ecs_world_t *world,
    const char *text)
{
    box_input_key_type(world, text, 1);
    syntax_flush(world);
}

/** Press a key a number of times.
 *
 * @param world The world
 * @param key The key
 * @param times Presses
 */
static void syntax_press(
    ecs_world_t *world,
    Key key,
    int32_t times)
{
    int32_t i;
    for (i = 0; i < times; i ++) {
        box_input_key_press(world, key, 1);
    }

    syntax_flush(world);
}

/** Empty the input line: move to its end and erase every character.
 *
 * @param world The world
 */
static void syntax_clear(
    ecs_world_t *world)
{
    ecs_entity_t before = ecs_lookup(world, SYNTAX_INPUT ".before");
    box_input_key_press(world, KeyEnd, 1);
    syntax_flush(world);
    syntax_press(world, KeyBackspace,
        (int32_t)strlen(ecs_get(world, before, Text)->value));
}

/** Get the element of the input line before the cursor.
 *
 * @param world The world
 * @return The element
 */
static ecs_entity_t syntax_before(
    ecs_world_t *world)
{
    return ecs_lookup(world, SYNTAX_INPUT ".before");
}

/** Get the element of the input line after the cursor.
 *
 * @param world The world
 * @return The element
 */
static ecs_entity_t syntax_after(
    ecs_world_t *world)
{
    return ecs_lookup(world, SYNTAX_INPUT ".after");
}

/** Get a line element of the console log counted from the bottom.
 *
 * @param world The world
 * @param from_bottom 0 for the newest line
 * @return The element
 */
static ecs_entity_t syntax_log_line(
    ecs_world_t *world,
    int32_t from_bottom)
{
    int32_t count = 0;
    char name[64];
    while (true) {
        ecs_os_snprintf(name, sizeof(name), SYNTAX_LOG ".line_%d", count);
        if (!ecs_lookup(world, name)) {
            break;
        }

        count ++;
    }

    ecs_os_snprintf(name, sizeof(name), SYNTAX_LOG ".line_%d", count - 1 - from_bottom);

    return ecs_lookup(world, name);
}

/** Find the line element of the console log that shows a text.
 *
 * @param world The world
 * @param text The text
 * @return The newest element that shows it
 */
static ecs_entity_t syntax_find_line(
    ecs_world_t *world,
    const char *text)
{
    ecs_entity_t result = 0, e;
    int32_t i;
    for (i = 0; !result && (e = syntax_log_line(world, i)); i ++) {
        const Text *t = ecs_get(world, e, Text);
        result = t && t->value && !strcmp(t->value, text) ? e : 0;
    }

    test_assert(result != 0);

    return result;
}

/** Get the number of TextSpans of an element.
 *
 * @param world The world
 * @param e The element
 * @return Number of spans, 0 when it has no TextSpans
 */
static int32_t syntax_count(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const TextSpans *ts = ecs_get(world, e, TextSpans);

    return ts ? ecs_vec_count(&ts->spans) : 0;
}

/** Check a span of an element.
 *
 * @param world The world
 * @param e The element
 * @param i Index of the span
 * @param start Expected first byte
 * @param end Expected byte after the span
 * @param color Expected color
 */
static void syntax_span(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t i,
    int32_t start,
    int32_t end,
    rgb color)
{
    const TextSpans *ts = ecs_get(world, e, TextSpans);
    test_assert(ts != NULL);
    test_assert(i < ecs_vec_count(&ts->spans));
    const TextSpan *s = ecs_vec_get_t(&ts->spans, TextSpan, i);
    test_int(s->start, start);
    test_int(s->end, end);
    test_int(s->color.r, color.r);
    test_int(s->color.g, color.g);
    test_int(s->color.b, color.b);
    test_int(s->color.a, 255);
}

/** Get the colors of the console.
 *
 * @param world The world
 * @return The syntax colors
 */
static const ConsoleSyntax* syntax_colors(
    ecs_world_t *world)
{
    return &ecs_singleton_get(world, Console)->syntax;
}

/** Get the byte offset of a substring of a line.
 *
 * @param line The line
 * @param part The substring
 * @return The offset
 */
static int32_t syntax_at(
    const char *line,
    const char *part)
{
    const char *p = strstr(line, part);
    test_assert(p != NULL);

    return (int32_t)(p - line);
}

void ConsoleSyntax_command_name_known(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "ls");
    test_str(ecs_get(world, syntax_before(world), Text)->value, "ls");
    test_int(syntax_count(world, syntax_before(world)), 1);
    syntax_span(world, syntax_before(world), 0, 0, 2, c->name);
    test_int(syntax_count(world, syntax_after(world)), 0);

    syntax_clear(world);
    test_int(syntax_count(world, syntax_before(world)), 0);
    syntax_type(world, "  mouse.scroll");
    syntax_span(world, syntax_before(world), 0, 2, 14, c->name);

    syntax_clear(world);
    syntax_type(world, "\"ls\"");
    syntax_span(world, syntax_before(world), 0, 0, 4, c->name);

    ecs_fini(world);
}

void ConsoleSyntax_command_name_unknown(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "lss things");
    test_int(syntax_count(world, syntax_before(world)), 1);
    syntax_span(world, syntax_before(world), 0, 0, 3, c->unknown);
    test_int(c->unknown.r, ecs_singleton_get(world, Console)->error.r);
    test_int(c->unknown.g, ecs_singleton_get(world, Console)->error.g);
    test_int(c->unknown.b, ecs_singleton_get(world, Console)->error.b);

    syntax_clear(world);
    syntax_type(world, "mouse");
    syntax_span(world, syntax_before(world), 0, 0, 5, c->unknown);

    ecs_fini(world);
}

void ConsoleSyntax_console_command(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "cls");
    test_int(syntax_count(world, syntax_before(world)), 1);
    syntax_span(world, syntax_before(world), 0, 0, 3, c->name);

    syntax_type(world, " x {a b}");
    test_int(syntax_count(world, syntax_before(world)), 3);
    syntax_span(world, syntax_before(world), 1, 4, 5, c->extra);
    syntax_span(world, syntax_before(world), 2, 6, 11, c->extra);

    ecs_os_free(box_console_run(world, "cls x", NULL));
    ecs_progress(world, 0);
    ecs_entity_t echo = syntax_log_line(world, 1);
    test_str(ecs_get(world, echo, Text)->value, "> cls x");
    test_int(syntax_count(world, echo), 2);
    syntax_span(world, echo, 0, 2, 5, c->name);
    syntax_span(world, echo, 1, 6, 7, c->extra);

    ecs_fini(world);
}

void ConsoleSyntax_entity_found_and_missing(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    ecs_entity(world, { .name = "things.alpha" });
    syntax_type(world, "get things.alpha");
    test_int(syntax_count(world, syntax_before(world)), 2);
    syntax_span(world, syntax_before(world), 0, 0, 3, c->name);
    syntax_span(world, syntax_before(world), 1, 4, 16, c->entity);

    syntax_clear(world);
    syntax_type(world, "get things.beta");
    syntax_span(world, syntax_before(world), 1, 4, 15, c->missing);

    syntax_clear(world);
    syntax_type(world, "cd things");
    syntax_flush(world);
    syntax_press(world, KeyEnter, 1);
    syntax_type(world, "rm alpha");
    syntax_span(world, syntax_before(world), 1, 3, 8, c->entity);

    syntax_clear(world);
    syntax_type(world, "ls ../things");
    syntax_span(world, syntax_before(world), 1, 3, 12, c->entity);

    syntax_clear(world);
    syntax_type(world, "ls /alpha");
    syntax_span(world, syntax_before(world), 1, 3, 9, c->missing);

    ecs_fini(world);
}

void ConsoleSyntax_entity_param_type(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    ecs_entity(world, { .name = "things.alpha" });
    syntax_type(world, "has things.alpha Position");
    test_int(syntax_count(world, syntax_before(world)), 2);
    syntax_span(world, syntax_before(world), 1, 4, 16, c->entity);

    syntax_clear(world);
    syntax_type(world, "has nothing Position");
    syntax_span(world, syntax_before(world), 1, 4, 11, c->missing);

    ecs_fini(world);
}

void ConsoleSyntax_type_from_parameters(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "get 5");
    test_int(syntax_count(world, syntax_before(world)), 2);
    syntax_span(world, syntax_before(world), 1, 4, 5, c->missing);

    syntax_clear(world);
    syntax_type(world, "mouse.scroll 5 1");
    test_int(syntax_count(world, syntax_before(world)), 3);
    syntax_span(world, syntax_before(world), 1, 13, 14, c->number);
    syntax_span(world, syntax_before(world), 2, 15, 16, c->number);

    syntax_clear(world);
    syntax_type(world, "key.type 5");
    test_int(syntax_count(world, syntax_before(world)), 1);

    ecs_fini(world);
}

void ConsoleSyntax_numbers_and_booleans(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "commands 10");
    syntax_span(world, syntax_before(world), 1, 9, 11, c->number);

    syntax_clear(world);
    syntax_type(world, "mouse.move 10,20 true");
    test_int(syntax_count(world, syntax_before(world)), 2);
    syntax_span(world, syntax_before(world), 1, 17, 21, c->boolean);

    syntax_clear(world);
    syntax_type(world, "input.focus off");
    syntax_span(world, syntax_before(world), 1, 12, 15, c->boolean);

    ecs_fini(world);
}

void ConsoleSyntax_strings(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "key.type \"a \\\"b\\\"\" 2");
    test_str(ecs_get(world, syntax_before(world), Text)->value, "key.type \"a \\\"b\\\"\" 2");
    test_int(syntax_count(world, syntax_before(world)), 3);
    syntax_span(world, syntax_before(world), 1, 9, 18, c->string);
    syntax_span(world, syntax_before(world), 2, 19, 20, c->number);

    syntax_clear(world);
    syntax_type(world, "key.type \"a  ");
    test_int(syntax_count(world, syntax_before(world)), 2);
    syntax_span(world, syntax_before(world), 1, 9, 15, c->string);
    test_int((int32_t)strlen(ecs_get(world, syntax_before(world), Text)->value), 15);

    ecs_fini(world);
}

void ConsoleSyntax_value_expression(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    const char *line = "set e Position {x: 1.5, s: \"a b\", on: true, v: [-2, 3e-1]}";
    syntax_type(world, line);
    ecs_entity_t e = syntax_before(world);
    test_str(ecs_get(world, e, Text)->value, line);
    test_int(syntax_count(world, e), 8);
    syntax_span(world, e, 0, 0, 3, c->name);
    syntax_span(world, e, 1, 4, 5, c->missing);
    syntax_span(world, e, 2, 15, (int32_t)strlen(line), c->value);
    syntax_span(world, e, 3, syntax_at(line, "1.5"), syntax_at(line, "1.5") + 3, c->number);
    syntax_span(world, e, 4, syntax_at(line, "\"a b\""), syntax_at(line, "\"a b\"") + 5,
        c->string);
    syntax_span(world, e, 5, syntax_at(line, "true"), syntax_at(line, "true") + 4,
        c->boolean);
    syntax_span(world, e, 6, syntax_at(line, "-2"), syntax_at(line, "-2") + 2, c->number);
    syntax_span(world, e, 7, syntax_at(line, "3e-1"), syntax_at(line, "3e-1") + 4,
        c->number);

    syntax_clear(world);
    syntax_type(world, "set e Position [1, x2]");
    e = syntax_before(world);
    test_int(syntax_count(world, e), 4);
    syntax_span(world, e, 2, 15, 22, c->value);
    syntax_span(world, e, 3, 16, 17, c->number);

    ecs_fini(world);
}

void ConsoleSyntax_extra_arguments(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    syntax_type(world, "mouse.scroll 1 2 3 \"four\"");
    ecs_entity_t e = syntax_before(world);
    test_int(syntax_count(world, e), 5);
    syntax_span(world, e, 3, 17, 18, c->extra);
    syntax_span(world, e, 4, 19, 25, c->extra);

    syntax_clear(world);
    syntax_type(world, "fps now");
    syntax_span(world, syntax_before(world), 1, 4, 7, c->extra);

    ecs_fini(world);
}

void ConsoleSyntax_rest_parameter(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    ecs_entity(world, { .name = "things.alpha" });
    syntax_type(world, "has things.alpha Position, \"x y\" 5  ");
    ecs_entity_t e = syntax_before(world);
    test_int(syntax_count(world, e), 3);
    syntax_span(world, e, 1, 4, 16, c->entity);
    syntax_span(world, e, 2, 17, 34, c->string);

    syntax_clear(world);
    syntax_type(world, "set e Position {x: 1} {y: 2}");
    e = syntax_before(world);
    test_int(syntax_count(world, e), 3);
    syntax_span(world, e, 2, 15, 28, c->string);

    ecs_fini(world);
}

void ConsoleSyntax_updates_while_typing(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity_t e = syntax_before(world);
    box_input_key_type(world, "l", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, e, Text)->value, "l");
    syntax_span(world, e, 0, 0, 1, c->unknown);
    box_input_key_type(world, "s", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, e, Text)->value, "ls");
    syntax_span(world, e, 0, 0, 2, c->name);
    syntax_type(world, " things.alph");
    syntax_span(world, e, 1, 3, 14, c->missing);
    box_input_key_type(world, "a", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    syntax_span(world, e, 1, 3, 15, c->entity);
    syntax_press(world, KeyBackspace, 1);
    syntax_span(world, e, 1, 3, 14, c->missing);

    ecs_fini(world);
}

void ConsoleSyntax_cursor_split_and_history(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity_t before = syntax_before(world), after = syntax_after(world);
    syntax_type(world, "get things.alpha");
    syntax_press(world, KeyLeft, 3);
    test_str(ecs_get(world, before, Text)->value, "get things.al");
    test_str(ecs_get(world, after, Text)->value, "pha");
    test_int(syntax_count(world, before), 2);
    syntax_span(world, before, 0, 0, 3, c->name);
    syntax_span(world, before, 1, 4, 13, c->entity);
    test_int(syntax_count(world, after), 1);
    syntax_span(world, after, 0, 0, 3, c->entity);

    syntax_press(world, KeyHome, 1);
    test_str(ecs_get(world, before, Text)->value, "");
    test_int(syntax_count(world, before), 0);
    test_int(syntax_count(world, after), 2);
    syntax_span(world, after, 0, 0, 3, c->name);
    syntax_span(world, after, 1, 4, 16, c->entity);

    syntax_press(world, KeyRight, 4);
    test_str(ecs_get(world, before, Text)->value, "get\xC2\xA0");
    test_int(syntax_count(world, before), 1);
    syntax_span(world, before, 0, 0, 3, c->name);
    syntax_span(world, after, 0, 0, 12, c->entity);

    syntax_press(world, KeyEnter, 1);
    test_int(syntax_count(world, before), 0);
    test_int(syntax_count(world, after), 0);
    syntax_press(world, KeyUp, 1);
    test_str(ecs_get(world, before, Text)->value, "get things.alpha");
    test_int(syntax_count(world, before), 2);
    syntax_span(world, before, 1, 4, 16, c->entity);

    ecs_fini(world);
}

void ConsoleSyntax_echoed_keeps_colors(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    const ConsoleSyntax *c = syntax_colors(world);
    ecs_entity_t alpha = ecs_entity(world, { .name = "things.alpha" });
    ecs_entity(world, { .name = "things.beta" });
    syntax_type(world, "get things.alpha");
    syntax_press(world, KeyEnter, 1);
    ecs_os_free(box_console_run(world, "rm things.beta", NULL));
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "things.beta") == 0);
    ecs_entity_t rm = syntax_find_line(world, "> rm things.beta");
    test_int(syntax_count(world, rm), 2);
    syntax_span(world, rm, 0, 2, 4, c->name);
    syntax_span(world, rm, 1, 5, 16, c->entity);

    ecs_delete(world, alpha);
    syntax_type(world, "get things.alpha");
    syntax_span(world, syntax_before(world), 1, 4, 16, c->missing);
    ecs_entity_t get = syntax_find_line(world, "> get things.alpha");
    syntax_span(world, get, 1, 6, 18, c->entity);

    syntax_press(world, KeyPageUp, 1);
    syntax_press(world, KeyPageDown, 1);
    get = syntax_find_line(world, "> get things.alpha");
    syntax_span(world, get, 1, 6, 18, c->entity);
    rm = syntax_find_line(world, "> rm things.beta");
    syntax_span(world, rm, 1, 5, 16, c->entity);

    ecs_fini(world);
}

void ConsoleSyntax_output_lines_plain(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity(world, { .name = "two.a" });
    ecs_entity(world, { .name = "two.b" });
    ecs_os_free(box_console_run(world, "ls things", NULL));
    ecs_progress(world, 0);
    ecs_entity_t line = syntax_log_line(world, 1);
    test_str(ecs_get(world, line, Text)->value, "> ls things");
    test_int(syntax_count(world, line), 2);
    test_int(syntax_count(world, syntax_log_line(world, 0)), 0);

    ecs_os_free(box_console_run(world, "ls two", NULL));
    ecs_progress(world, 0);
    test_str(ecs_get(world, line, Text)->value, "a");
    test_int(syntax_count(world, line), 0);
    test_assert(!ecs_has(world, line, TextSpans));
    const Console *con = ecs_singleton_get(world, Console);
    const Rgb *color = ecs_get(world, line, Rgb);
    test_int(color->r, con->text.r);
    test_int(color->g, con->text.g);
    test_int(color->b, con->text.b);

    ecs_os_free(box_console_run(world, "get nothing", NULL));
    ecs_progress(world, 0);
    ecs_entity_t err = syntax_log_line(world, 0);
    test_int(syntax_count(world, err), 0);
    color = ecs_get(world, err, Rgb);
    test_int(color->r, con->error.r);
    test_int(color->g, con->error.g);
    test_int(color->b, con->error.b);
    color = ecs_get(world, syntax_log_line(world, 1), Rgb);
    test_int(color->r, con->command.r);
    test_int(syntax_count(world, syntax_log_line(world, 1)), 2);

    ecs_fini(world);
}

/** Count the times the Text of an element was set.
 *
 * @param it Iterator over the elements with a Text that was set
 */
static void syntax_on_text(
    ecs_iter_t *it)
{
    int32_t *count = it->ctx;
    count[0] += it->count;
}

void ConsoleSyntax_color_change_no_relayout(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    ecs_entity_t e = syntax_before(world);
    syntax_type(world, "ls");
    int32_t sets = 0;
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Text), .src.id = e }},
        .events = { EcsOnSet },
        .callback = syntax_on_text,
        .ctx = &sets
    });

    const TextBounds *b = ecs_get(world, e, TextBounds);
    float width = b ? b->width : 0;
    Console *c = ecs_singleton_get_mut(world, Console);
    c->syntax.name = (rgb){1, 2, 3};
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    syntax_span(world, e, 0, 0, 2, (rgb){1, 2, 3});
    test_int(sets, 0);
    b = ecs_get(world, e, TextBounds);
    test_flt(b ? b->width : 0, width);

    ecs_fini(world);
}

void ConsoleSyntax_session_plain_text(void) {
    // ui/console_syntax
    ecs_world_t *world = syntax_world();
    ecs_entity(world, { .name = "things.alpha" });
    syntax_type(world, "ls things");
    syntax_press(world, KeyEnter, 1);
    ecs_os_free(box_console_run(world, "ls \"things\"", NULL));
    ecs_progress(world, 0);
    test_int(syntax_count(world, syntax_find_line(world, "> ls things")), 2);
    test_int(syntax_count(world, syntax_find_line(world, "> ls \"things\"")), 2);

    ecs_entity_t session = ecs_lookup(world, BOX_CONSOLE_SESSION);
    char *out = box_terminal_run(world, session, "commands", NULL);
    test_str(out, "$ ls things\nalpha\n$ ls \"things\"\nalpha");
    ecs_os_free(out);

    const TerminalStatus *st = ecs_get(world, session, TerminalStatus);
    char *path = flecs_asprintf("%s/commands.log", st->dir);
    char log[512] = {0};
    FILE *f = fopen(path, "r");
    test_assert(f != NULL);
    fread(log, 1, sizeof(log) - 1, f);
    fclose(f);
    test_str(log, "$ ls things\nalpha\n$ ls \"things\"\nalpha\n$ commands\n");
    test_assert(strchr(log, '\x1b') == NULL);
    ecs_os_free(path);

    ecs_entity_t t = box_terminal_new(world);
    out = box_terminal_run(world, t, "ls things", NULL);
    test_str(out, "alpha");
    ecs_os_free(out);

    ecs_fini(world);
}

/** Count the pixels of a region close to a color.
 *
 * @param px The pixels, RGBA
 * @param w Width of the image
 * @param x0 Left of the region
 * @param y0 Top of the region
 * @param x1 Right of the region
 * @param y1 Bottom of the region
 * @param color The color
 * @return Number of pixels within a small distance of the color
 */
static int32_t syntax_pixels(
    const unsigned char *px,
    int32_t w,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    rgb color)
{
    int32_t x, y, result = 0;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            const unsigned char *p = &px[(y * w + x) * 4];
            result += abs((int)p[0] - color.r) < 24 && abs((int)p[1] - color.g) < 24 &&
                abs((int)p[2] - color.b) < 24;
        }
    }

    return result;
}

void ConsoleSyntax_capture(void) {
    // ui/console_syntax
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360,
        .write_to_file = "console_syntax.png", .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    c->open = true;
    c->background = (rgba){0, 0, 0, 255};
    c->input = (rgb){255, 255, 255};
    c->command = (rgb){255, 255, 255};
    c->syntax.name = (rgb){0, 255, 0};
    c->syntax.entity = (rgb){255, 0, 255};
    c->syntax.number = (rgb){255, 255, 0};
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_os_free(box_console_run(world, "ls things.alpha", NULL));
    ecs_os_free(box_console_run(world, "commands 7", NULL));
    syntax_type(world, "get things.alpha");
    int32_t i;
    for (i = 0; i < 100; i ++) {
        const TextBounds *b = ecs_get(world, syntax_before(world), TextBounds);
        if (b && b->width > 0) {
            break;
        }

        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/console_syntax.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 640);
    int32_t green = syntax_pixels(px, w, 0, 330, 320, 360, (rgb){0, 255, 0});
    int32_t magenta = syntax_pixels(px, w, 0, 330, 320, 360, (rgb){255, 0, 255});
    int32_t echo_green = syntax_pixels(px, w, 0, 0, 320, 330, (rgb){0, 255, 0});
    int32_t echo_magenta = syntax_pixels(px, w, 0, 0, 320, 330, (rgb){255, 0, 255});
    int32_t echo_yellow = syntax_pixels(px, w, 0, 0, 320, 330, (rgb){255, 255, 0});
    test_assert(green > 10);
    test_assert(magenta > 10);
    test_assert(echo_green > 10);
    test_assert(echo_magenta > 10);
    test_assert(echo_yellow > 3);
    test_int(syntax_pixels(px, w, 320, 0, 640, 360, (rgb){0, 255, 0}), 0);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
