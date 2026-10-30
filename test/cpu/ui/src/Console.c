/**
 * @file Console.c
 * @brief The layout of the in game console: it covers the full height of the window it is drawn
 * in, also after the window is resized, and its long lines wrap at the width of the panel.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>

/** Path of the column of the visible scrollback lines */
#define CONSOLE_LOG "console.ui.screen.stack.panel.content.log"

/** Resize the window of a surface the way the platform does: the window reports its new size
 * in points through SurfaceStatus while the requested Surface size stays the same.
 *
 * @param world The world
 * @param s The surface
 * @param w Width of the window in points
 * @param h Height of the window in points
 */
static void console_window_resize(
    ecs_world_t *world,
    ecs_entity_t s,
    int32_t w,
    int32_t h)
{
    ecs_set(world, s, SurfaceStatus, {
        .width = w, .height = h, .point_width = w, .point_height = h,
        .render_width = w, .render_height = h, .window = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Check that the panel and its scrollback lines cover the full height of the window: the
 * panel is as high as the window and the rows of the visible lines fill the log up to less than
 * one row.
 *
 * @param world The world
 * @param h Height of the window in points
 */
static void console_check_covers(
    ecs_world_t *world,
    float h)
{
    ecs_entity_t panel = ecs_lookup(world, "console.ui.screen.stack.panel");
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG);
    const Console *c = ecs_singleton_get(world, Console);
    int32_t lines = 0;
    test_assert(panel != 0);
    test_assert(log != 0);
    ecs_iter_t it = ecs_children(world, log);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            int32_t rows = box_text_line(world, it.entities[i], 0, NULL, NULL);
            lines += rows > 1 ? rows : 1;
        }
    }

    float log_h = ecs_get(world, log, LayoutBounds)->height;
    test_flt(ecs_get(world, panel, Rect)->height, h);
    test_flt(log_h, h - 2 * c->padding - c->line_height);
    test_assert((float)lines * c->line_height <= log_h);
    test_assert(log_h - (float)lines * c->line_height < c->line_height);
}

void Console_covers_height_after_window_resize(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360});
    ecs_progress(world, 0);

    // Resized before the console opens for the first time
    console_window_resize(world, s, 640, 600);
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 600);

    // Resized while the console is open, larger and smaller
    console_window_resize(world, s, 640, 800);
    console_check_covers(world, 800);
    console_window_resize(world, s, 640, 200);
    console_check_covers(world, 200);
    ecs_fini(world);
}

void Console_covers_height_after_surface_resize(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360});
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 360);

    ecs_set(world, s, Surface, {.width = 640, .height = 800});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 800);

    ecs_set(world, s, Surface, {.width = 640, .height = 200});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 200);
    ecs_fini(world);
}

/** Path of the input line of the console */
#define CONSOLE_INPUT "console.ui.screen.stack.panel.content.input"

/** Prompt of the console session at the root */
#define CONSOLE_PROMPT "~ $ "

/** Bytes of the prompt at the root */
#define CONSOLE_PROMPT_LEN (4)

/** Hint line the scrollback starts with the first time the console opens */
#define CONSOLE_HINT "Flecs Shell  type help for a list of possible commands"

/** A command line that runs as cls with arguments, long enough to wrap into several rows */
#define CONSOLE_LONG_COMMAND "cls alpha bravo charlie delta echo foxtrot golf hotel india " \
    "juliett kilo lima mike november oscar papa quebec romeo sierra tango uniform victor"

/** Width the lines of the console wrap at on a 640 point wide surface: half of it minus the
 * padding on both sides */
#define CONSOLE_WRAP (320.0f - 20.0f)

/** Rows one wheel step scrolls the console, BOX_CONSOLE_WHEEL_LINES */
#define CONSOLE_WHEEL_ROWS (3)

/** Open the console on a headless surface and lay it out.
 *
 * @param w Width of the surface in points
 * @param h Height of the surface in points
 * @param surface Receives the surface
 * @return The world
 */
static ecs_world_t* console_open(
    float w,
    float h,
    ecs_entity_t *surface)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = w, .height = h, .resolution_scale = 1});
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    if (surface) {
        *surface = s;
    }

    return world;
}

/** Apply the queued input and draw the console.
 *
 * @param world The world
 */
static void console_settle(
    ecs_world_t *world)
{
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
}

/** Get the scrollback elements of the console, top to bottom.
 *
 * @param world The world
 * @param out Receives the elements
 * @param max Size of out
 * @return Number of elements
 */
static int32_t console_log_lines(
    ecs_world_t *world,
    ecs_entity_t *out,
    int32_t max)
{
    int32_t result = 0;
    while (result < max) {
        char name[96];
        ecs_os_snprintf(name, sizeof(name), CONSOLE_LOG ".line_%d", result);
        ecs_entity_t e = ecs_lookup(world, name);
        if (!e) {
            break;
        }

        out[result ++] = e;
    }

    return result;
}

/** Find the scrollback element that shows a line.
 *
 * @param world The world
 * @param value The line
 * @return The element, 0 when no element shows it
 */
static ecs_entity_t console_find_line(
    ecs_world_t *world,
    const char *value)
{
    ecs_entity_t lines[256], result = 0;
    int32_t i, count = console_log_lines(world, lines, 256);
    for (i = 0; i < count && !result; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        if (t && t->value && !strcmp(t->value, value)) {
            result = lines[i];
        }
    }

    return result;
}

/** Get the color of the span a byte of a Text falls in, the last span that holds it.
 *
 * @param world The world
 * @param e The element
 * @param offset The byte
 * @return The span, NULL when no span holds the byte
 */
static const TextSpan* console_span_at(
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

/** Check that a scrollback element fits the panel and is as high as the rows it wraps to.
 *
 * @param world The world
 * @param e The element
 * @param wrap Width the console wraps at
 * @return The rows of the element
 */
static int32_t console_check_fits(
    ecs_world_t *world,
    ecs_entity_t e,
    float wrap)
{
    const Console *c = ecs_singleton_get(world, Console);
    int32_t rows = box_text_line(world, e, 0, NULL, NULL);
    test_assert(rows >= 1);
    test_flt(ecs_get(world, e, Text)->width, wrap);
    test_assert(ecs_get(world, e, TextBounds)->width <= wrap);
    test_flt(ecs_get(world, e, TextBounds)->height, (float)rows * c->line_height);
    test_flt(ecs_get(world, e, LayoutItem)->height.value, (float)rows * c->line_height);
    test_flt(ecs_get(world, e, LayoutBounds)->height, (float)rows * c->line_height);

    return rows;
}

void Console_long_line_wraps(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 720, NULL);
    char name[201];
    for (int32_t i = 0; i < 200; i ++) {
        name[i] = (char)('a' + i % 26);
    }

    name[200] = '\0';
    ecs_entity_t parent = ecs_entity(world, { .name = "long" });
    ecs_entity(world, { .name = name, .parent = parent });
    ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    ecs_os_free(box_console_run(world, "ls long", NULL));
    ecs_progress(world, 0);

    ecs_entity_t cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    test_assert(console_check_fits(world, cmd, CONSOLE_WRAP) >= 3);

    // A word longer than a row breaks inside
    char *path = flecs_asprintf("long.%s", name);
    ecs_entity_t word = console_find_line(world, path);
    ecs_os_free(path);
    test_assert(word != 0);
    int32_t rows = console_check_fits(world, word, CONSOLE_WRAP);
    test_assert(rows >= 4);
    for (int32_t i = 1; i < rows; i ++) {
        int32_t start = 0;
        box_text_line(world, word, i, &start, NULL);
        test_assert(start > 0);
    }

    // Every shown line fits the panel and the log is filled to less than a row
    ecs_entity_t lines[256];
    int32_t count = console_log_lines(world, lines, 256);
    const Console *c = ecs_singleton_get(world, Console);
    float log_h = ecs_get(world, ecs_lookup(world, CONSOLE_LOG), LayoutBounds)->height;
    float shown = 0;
    for (int32_t i = 0; i < count; i ++) {
        test_assert(ecs_get(world, lines[i], TextBounds)->width <= CONSOLE_WRAP);
        shown += ecs_get(world, lines[i], LayoutItem)->height.value;
    }

    test_assert(shown + 0.001f >= floorf(log_h / c->line_height) * c->line_height);
    test_assert(ecs_has(world, ecs_lookup(world, CONSOLE_LOG), ClipChildren));

    ecs_fini(world);
}

void Console_wrapped_line_keeps_indent(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    ecs_progress(world, 0);

    // Rows of a line that ran start after the prompt
    ecs_entity_t cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    test_int(ecs_get(world, cmd, Text)->hang, CONSOLE_PROMPT_LEN);
    TextCaret prompt, row;
    test_assert(box_text_caret(world, cmd, CONSOLE_PROMPT_LEN, &prompt));
    int32_t rows = box_text_line(world, cmd, 0, NULL, NULL);
    test_assert(rows >= 2);
    for (int32_t i = 1; i < rows; i ++) {
        int32_t start;
        box_text_line(world, cmd, i, &start, NULL);
        test_assert(box_text_caret(world, cmd, start, &row));
        test_int(row.line, i);
        test_flt(row.x, prompt.x);
    }

    ecs_fini(world);
}

void Console_wrapped_spans_keep_colors(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    ecs_progress(world, 0);

    const char *value = CONSOLE_PROMPT CONSOLE_LONG_COMMAND;
    ecs_entity_t cmd = console_find_line(world, value);
    test_assert(cmd != 0);
    test_assert(box_text_line(world, cmd, 0, NULL, NULL) >= 3);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    int32_t i, len = (int32_t)strlen(value), word = -1;
    bool last_row_colored = false;
    for (i = CONSOLE_PROMPT_LEN; i < len; i ++) {
        const TextSpan *s = console_span_at(world, cmd, i);
        if (value[i] == ' ') {
            test_assert(s == NULL);
            continue;
        }

        word += i == CONSOLE_PROMPT_LEN || value[i - 1] == ' ';
        test_assert(s != NULL);
        test_assert(s->start <= i && s->end > i);
        test_assert(s->start == CONSOLE_PROMPT_LEN || value[s->start - 1] == ' ');
        test_assert(s->end == len || value[s->end] == ' ');
        const rgb *expect = word ? &syntax->extra : &syntax->name;
        test_int(s->color.r, expect->r);
        test_int(s->color.g, expect->g);
        test_int(s->color.b, expect->b);

        TextCaret caret;
        test_assert(box_text_caret(world, cmd, i, &caret));
        last_row_colored |= caret.line == box_text_line(world, cmd, 0, NULL, NULL) - 1;
    }

    test_assert(last_row_colored);

    // The prompt keeps the colors of its scope and $, not the spaces around the $
    const TextSpan *scope = console_span_at(world, cmd, 0);
    const TextSpan *symbol = console_span_at(world, cmd, 2);
    test_assert(scope != NULL);
    test_int(scope->start, 0);
    test_int(scope->end, 1);
    test_int(scope->color.r, syntax->scope.r);
    test_int(scope->color.g, syntax->scope.g);
    test_int(scope->color.b, syntax->scope.b);
    test_assert(symbol != NULL);
    test_int(symbol->start, 2);
    test_int(symbol->end, 3);
    test_int(symbol->color.r, syntax->symbol.r);
    test_int(symbol->color.g, syntax->symbol.g);
    test_int(symbol->color.b, syntax->symbol.b);
    test_assert(console_span_at(world, cmd, 1) == NULL);
    test_assert(console_span_at(world, cmd, 3) == NULL);

    ecs_fini(world);
}

/** Check that the cursor of the console sits on the caret before a byte of the input line.
 *
 * @param world The world
 * @param offset Byte of the input line after the prompt
 * @return Row of the cursor
 */
static int32_t console_check_cursor(
    ecs_world_t *world,
    int32_t offset)
{
    const Console *c = ecs_singleton_get(world, Console);
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity_t cursor = ecs_lookup(world, CONSOLE_INPUT ".cursor");
    TextCaret caret;
    test_assert(box_text_caret(world, line, CONSOLE_PROMPT_LEN + offset, &caret));
    const LayoutItem *item = ecs_get(world, cursor, LayoutItem);
    float x = caret.x + ecs_get(world, line, TextBounds)->width * 0.5f;
    test_flt(item->x, x);
    test_flt(item->y, -((float)caret.line * c->line_height +
        (c->line_height - c->font_size) * 0.5f));

    return caret.line;
}

void Console_input_line_wraps(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    float lh = c->line_height;
    box_input_key_type(world, CONSOLE_LONG_COMMAND, 1);
    console_settle(world);

    ecs_entity_t input = ecs_lookup(world, CONSOLE_INPUT);
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    test_assert(line != 0);
    test_str(ecs_get(world, line, Text)->value, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_int(ecs_get(world, line, Text)->hang, CONSOLE_PROMPT_LEN);
    int32_t rows = box_text_line(world, line, 0, NULL, NULL);
    test_assert(rows >= 3);
    test_assert(ecs_get(world, line, TextBounds)->width <= CONSOLE_WRAP);
    test_flt(ecs_get(world, input, LayoutBounds)->height, (float)rows * lh);

    // The log gives up the rows the input line takes
    float log_h = ecs_get(world, ecs_lookup(world, CONSOLE_LOG), LayoutBounds)->height;
    test_flt(log_h, 360 - 2 * c->padding - (float)rows * lh);

    // The cursor follows the logical line over the rows
    int32_t len = (int32_t)strlen(CONSOLE_LONG_COMMAND);
    test_int(console_check_cursor(world, len), rows - 1);
    box_input_key_press(world, KeyHome, 1);
    console_settle(world);
    test_int(console_check_cursor(world, 0), 0);
    test_assert(ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".cursor"), LayoutItem)->x > 0);
    box_input_key_press(world, KeyRight, 1);
    console_settle(world);
    test_int(console_check_cursor(world, 1), 0);

    int32_t start;
    box_text_line(world, line, 1, &start, NULL);
    test_assert(start > CONSOLE_PROMPT_LEN);
    box_input_key_press(world, KeyHome, 1);
    for (int32_t i = 0; i < start - CONSOLE_PROMPT_LEN; i ++) {
        box_input_key_press(world, KeyRight, 1);
    }

    console_settle(world);
    test_int(console_check_cursor(world, start - CONSOLE_PROMPT_LEN), 1);
    box_input_key_press(world, KeyLeft, 1);
    console_settle(world);
    test_int(console_check_cursor(world, start - CONSOLE_PROMPT_LEN - 1), 0);
    box_input_key_press(world, KeyEnd, 1);
    console_settle(world);
    test_int(console_check_cursor(world, len), rows - 1);

    // Running the line gives the rows back to the log
    box_input_key_press(world, KeyEnter, 1);
    console_settle(world);
    test_str(ecs_get(world, line, Text)->value, CONSOLE_PROMPT);
    test_flt(ecs_get(world, input, LayoutBounds)->height, lh);
    test_assert(console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND) != 0);

    ecs_fini(world);
}

void Console_scroll_counts_rows(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    float lh = c->line_height;
    for (int32_t i = 0; i < 40; i ++) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "many.c%02d", i);
        ecs_entity(world, { .name = name });
    }

    ecs_os_free(box_console_run(world, "ls many", NULL));
    char *out = box_console_run(world, CONSOLE_LONG_COMMAND, NULL);
    ecs_progress(world, 0);

    // The newest output and the long line before it are fully visible
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG);
    ecs_entity_t lines[256];
    int32_t count = console_log_lines(world, lines, 256);
    test_assert(count >= 3);
    test_str(ecs_get(world, lines[count - 1], Text)->value, out);
    int32_t output = box_text_line(world, lines[count - 1], 0, NULL, NULL);
    test_assert(output >= 1);
    test_assert(output < CONSOLE_WHEEL_ROWS);
    ecs_entity_t cmd = lines[count - 2];
    test_str(ecs_get(world, cmd, Text)->value, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    int32_t rows = box_text_line(world, cmd, 0, NULL, NULL);
    test_assert(rows > CONSOLE_WHEEL_ROWS);
    test_flt(ecs_get(world, log, Layout)->padding.bottom, 0);
    int32_t visible = (int32_t)floorf(ecs_get(world, log, LayoutBounds)->height / lh);
    test_assert(visible > rows + output);
    test_str(ecs_get(world, lines[count - 3], Text)->value, "many.c39");

    // The other rows of the window are the newest short lines
    int32_t above = visible - output - rows;
    char expect[32];
    ecs_os_snprintf(expect, sizeof(expect), "many.c%02d", 40 - above);
    test_str(ecs_get(world, lines[0], Text)->value, expect);

    // One wheel step scrolls three rows: the output and the last rows of the long line go
    // under the bottom edge of the log
    box_input_mouse_scroll(world, 0, 1);
    console_settle(world);
    count = console_log_lines(world, lines, 256);
    test_str(ecs_get(world, lines[count - 1], Text)->value, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_flt(ecs_get(world, log, Layout)->padding.bottom,
        -(float)(CONSOLE_WHEEL_ROWS - output) * lh);
    ecs_os_snprintf(expect, sizeof(expect), "many.c%02d", 40 - above - CONSOLE_WHEEL_ROWS);
    test_str(ecs_get(world, lines[0], Text)->value, expect);

    // Scrolled past the long line, the short lines before it fill the window
    for (int32_t i = 0; i < rows; i ++) {
        box_input_mouse_scroll(world, 0, 1);
        console_settle(world);
    }

    int32_t names = CONSOLE_WHEEL_ROWS * (rows + 1) - output - rows;
    test_assert(names > 0);
    count = console_log_lines(world, lines, 256);
    test_int(count, visible);
    ecs_os_snprintf(expect, sizeof(expect), "many.c%02d", 39 - names);
    test_str(ecs_get(world, lines[count - 1], Text)->value, expect);
    test_flt(ecs_get(world, log, Layout)->padding.bottom, 0);

    // Page Down comes back to the newest output, a page is the rows of the window but one
    int32_t pages = (CONSOLE_WHEEL_ROWS * (rows + 1) + visible - 2) / (visible - 1);
    for (int32_t i = 0; i < pages; i ++) {
        box_input_key_press(world, KeyPageDown, 1);
    }

    console_settle(world);
    count = console_log_lines(world, lines, 256);
    test_str(ecs_get(world, lines[count - 1], Text)->value, out);
    test_flt(ecs_get(world, log, Layout)->padding.bottom, 0);

    ecs_os_free(out);
    ecs_fini(world);
}

void Console_scrollback_limit_counts_rows(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    ecs_progress(world, 0);
    ecs_entity_t cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t rows = box_text_line(world, cmd, 0, NULL, NULL);
    test_assert(rows >= 3);

    // Room for the long line and its output, not for one more line
    ecs_singleton_get_mut(world, Console)->scrollback = rows + 2;
    ecs_singleton_modified(world, Console);
    ecs_os_free(box_console_run(world, "cls x", NULL));
    ecs_progress(world, 0);

    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), kept = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        kept += t->value && t->value[0] ? box_text_line(world, lines[i], 0, NULL, NULL) : 0;
    }

    test_assert(kept <= rows + 2);
    test_assert(console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND) == 0);
    test_assert(console_find_line(world, CONSOLE_PROMPT "cls x") != 0);

    ecs_fini(world);
}

/** Count the rows of the scrollback lines the console shows, without the empty rows that fill
 * the top of the log.
 *
 * @param world The world
 * @return The rows
 */
static int32_t console_shown_rows(
    ecs_world_t *world)
{
    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), result = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        result += t->value && t->value[0] ? box_text_line(world, lines[i], 0, NULL, NULL) : 0;
    }

    return result;
}

/** Count the scrollback elements that show a line.
 *
 * @param world The world
 * @param value The line
 * @return The elements
 */
static int32_t console_count_line(
    ecs_world_t *world,
    const char *value)
{
    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), result = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        result += t->value && !strcmp(t->value, value);
    }

    return result;
}

/** Count the times the Text of an entity is set.
 *
 * @param it Iterator over the entities whose Text was set
 */
static void console_count_sets(
    ecs_iter_t *it)
{
    int32_t *count = it->ctx;
    ecs_entity_t *target = it->callback_ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        count[0] += it->entities[i] == *target;
    }
}

void Console_resize_rewraps(void) {
    // console/wrap
    ecs_entity_t s;
    ecs_world_t *world = console_open(640, 360, &s);
    ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    ecs_progress(world, 0);
    ecs_entity_t cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t narrow = console_check_fits(world, cmd, CONSOLE_WRAP);
    test_assert(narrow >= 3);

    int32_t sets = 0;
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Text) }},
        .events = { EcsOnSet },
        .callback = console_count_sets,
        .ctx = &sets,
        .callback_ctx = &cmd
    });

    // Frames without a change, and a slightly taller surface that fits as many rows, do not
    // lay the line out again
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_set(world, s, Surface, {.width = 640, .height = 365, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND) == cmd);
    test_int(sets, 0);

    // A wider surface wraps it at the new width
    ecs_set(world, s, Surface, {.width = 1280, .height = 365, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t wide = console_check_fits(world, cmd, 640 - 20);
    test_assert(wide < narrow);
    test_assert(sets > 0);

    // So does a smaller share of the surface
    ecs_singleton_get_mut(world, Console)->width = 0.25f;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(console_check_fits(world, cmd, CONSOLE_WRAP) == narrow);
    test_flt(ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".line"), Text)->width,
        CONSOLE_WRAP);

    ecs_fini(world);
}

/** Hold Ctrl, tap a key and release Ctrl, then draw the console.
 *
 * @param world The world
 * @param key The key
 */
static void console_ctrl(
    ecs_world_t *world,
    Key key)
{
    box_input_key(world, KeyCtrlLeft, true);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_input_key_press(world, key, 1);
    box_input_key(world, KeyCtrlLeft, false);
    console_settle(world);
}

/** Tap a key a number of times, then draw the console.
 *
 * @param world The world
 * @param key The key
 * @param count Number of taps
 */
static void console_press(
    ecs_world_t *world,
    Key key,
    int32_t count)
{
    for (int32_t i = 0; i < count; i ++) {
        box_input_key_press(world, key, 1);
    }

    console_settle(world);
}

/** Type text into the console, then draw it.
 *
 * @param world The world
 * @param text The text
 */
static void console_type(
    ecs_world_t *world,
    const char *text)
{
    box_input_key_type(world, text, 1);
    console_settle(world);
}

/** Get the string of the input line of the console, its prompt and suggestion included.
 *
 * @param world The world
 * @return The string
 */
static const char* console_value(
    ecs_world_t *world)
{
    const Text *t = ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".line"), Text);
    test_assert(t != NULL && t->value != NULL);

    return t->value;
}

/** Find the span of the input line in the color of the history suggestion.
 *
 * @param world The world
 * @return The span, NULL when the input line shows no suggestion
 */
static const TextSpan* console_ghost(
    ecs_world_t *world)
{
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    const TextSpans *spans = ecs_get(world, line, TextSpans);
    const rgb *ghost = &ecs_singleton_get(world, Console)->syntax.ghost;
    const TextSpan *result = NULL;
    for (int32_t i = 0; spans && i < ecs_vec_count(&spans->spans); i ++) {
        const TextSpan *s = ecs_vec_get_t(&spans->spans, TextSpan, i);
        if (s->color.r == ghost->r && s->color.g == ghost->g && s->color.b == ghost->b) {
            result = s;
        }
    }

    return result;
}

/** Get the input line of the console after the root prompt, without the history suggestion.
 *
 * @param world The world
 * @return The text, valid until the next call
 */
static const char* console_typed(
    ecs_world_t *world)
{
    static char buf[512];
    const char *value = console_value(world);
    const TextSpan *ghost = console_ghost(world);
    int32_t end = ghost ? ghost->start : (int32_t)strlen(value);
    test_assert(!strncmp(value, CONSOLE_PROMPT, CONSOLE_PROMPT_LEN));
    test_assert(end - CONSOLE_PROMPT_LEN < (int32_t)sizeof(buf));
    ecs_os_snprintf(buf, sizeof(buf), "%.*s", end - CONSOLE_PROMPT_LEN,
        &value[CONSOLE_PROMPT_LEN]);

    return buf;
}

/** Check the color of a span of a text element.
 *
 * @param world The world
 * @param e The element
 * @param start First byte of the span
 * @param end Byte after the span
 * @param color The color
 */
static void console_check_span(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t start,
    int32_t end,
    rgb color)
{
    const TextSpan *s = console_span_at(world, e, start);
    test_assert(s != NULL);
    test_int(s->start, start);
    test_int(s->end, end);
    test_int(s->color.r, color.r);
    test_int(s->color.g, color.g);
    test_int(s->color.b, color.b);
}

void Console_prompt_root_and_scope(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity(world, { .name = "parent.child" });

    // At the root the prompt is ~ and $, in their own colors
    test_str(console_value(world), CONSOLE_PROMPT);
    test_int(ecs_get(world, line, Text)->hang, CONSOLE_PROMPT_LEN);
    console_check_span(world, line, 0, 1, syntax->scope);
    console_check_span(world, line, 2, 3, syntax->symbol);
    test_assert(console_span_at(world, line, 1) == NULL);
    console_check_cursor(world, 0);

    // cd from the input line moves the prompt right away
    console_type(world, "cd parent.child");
    console_press(world, KeyEnter, 1);
    test_str(console_value(world), "parent.child $ ");
    test_int(ecs_get(world, line, Text)->hang, 15);
    console_check_span(world, line, 0, 12, syntax->scope);
    console_check_span(world, line, 13, 14, syntax->symbol);
    test_assert(console_find_line(world, CONSOLE_PROMPT "cd parent.child") != 0);

    // Typed text follows the prompt and the cursor stays on the input
    console_type(world, "ls");
    test_str(console_value(world), "parent.child $ ls");
    TextCaret caret;
    test_assert(box_text_caret(world, line, 17, &caret));

    // cd from a script does the same, back at the root
    ecs_os_free(box_console_run(world, "cd", NULL));
    ecs_progress(world, 0);
    test_str(console_value(world), CONSOLE_PROMPT "ls");

    ecs_fini(world);
}

void Console_lines_keep_scope(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity(world, { .name = "parent" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_os_free(box_console_run(world, "pwd", NULL));
    ecs_os_free(box_console_run(world, "cd", NULL));
    ecs_os_free(box_console_run(world, "pwd", NULL));
    ecs_progress(world, 0);

    // Every line keeps the prompt it ran at, also after the scope moved on
    ecs_entity_t cd = console_find_line(world, CONSOLE_PROMPT "cd parent");
    ecs_entity_t pwd = console_find_line(world, "parent $ pwd");
    ecs_entity_t back = console_find_line(world, "parent $ cd");
    ecs_entity_t root = console_find_line(world, CONSOLE_PROMPT "pwd");
    test_assert(cd != 0);
    test_assert(pwd != 0);
    test_assert(back != 0);
    test_assert(root != 0);
    test_int(ecs_get(world, pwd, Text)->hang, 9);
    console_check_span(world, pwd, 0, 6, syntax->scope);
    console_check_span(world, pwd, 7, 8, syntax->symbol);
    console_check_span(world, pwd, 9, 12, syntax->name);
    console_check_span(world, cd, 0, 1, syntax->scope);
    test_str(console_value(world), CONSOLE_PROMPT);

    // Going back into the scope does not change the lines that ran at the root
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);
    root = console_find_line(world, CONSOLE_PROMPT "pwd");
    test_assert(root != 0);
    test_int(ecs_get(world, root, Text)->hang, CONSOLE_PROMPT_LEN);
    console_check_span(world, root, 0, 1, syntax->scope);
    test_assert(console_find_line(world, "parent $ pwd") != 0);
    test_str(console_value(world), "parent $ ");

    ecs_fini(world);
}

void Console_prompt_scope_deleted(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_os_free(box_console_run(world, "pwd", NULL));
    ecs_progress(world, 0);
    test_str(console_value(world), "parent $ ");

    // Deleting the scope outside a command moves the prompt back to the root without a key
    ecs_delete(world, parent);
    ecs_progress(world, 0);
    test_str(console_value(world), CONSOLE_PROMPT);
    test_int(ecs_get(world, line, Text)->hang, CONSOLE_PROMPT_LEN);

    // The line that ran in the scope keeps its prompt
    test_assert(console_find_line(world, "parent $ pwd") != 0);

    ecs_fini(world);
}

void Console_prompt_scope_renamed(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);
    console_type(world, "ls");
    test_str(console_value(world), "parent $ ls");

    // Renaming the scope shows the new name, the typed text stays
    ecs_set_name(world, parent, "renamed");
    ecs_progress(world, 0);
    test_str(console_value(world), "renamed $ ls");
    test_assert(console_find_line(world, CONSOLE_PROMPT "cd parent") != 0);

    ecs_fini(world);
}

void Console_prompt_scope_moved(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity_t child = ecs_entity(world, { .name = "parent.child" });
    ecs_entity_t parent = ecs_get_target(world, child, EcsChildOf, 0);
    ecs_entity_t other = ecs_entity(world, { .name = "other" });
    ecs_os_free(box_console_run(world, "cd parent.child", NULL));
    ecs_progress(world, 0);
    test_str(console_value(world), "parent.child $ ");

    // Moving the parent of the scope shows the new path
    test_assert(parent != 0);
    ecs_add_pair(world, parent, EcsChildOf, other);
    ecs_progress(world, 0);
    test_str(console_value(world), "other.parent.child $ ");

    // Moving the scope itself does the same
    ecs_add_pair(world, child, EcsChildOf, other);
    ecs_progress(world, 0);
    test_str(console_value(world), "other.child $ ");

    ecs_fini(world);
}

void Console_prompt_scope_spans(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "parent.item" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);
    console_type(world, "ls item");
    test_str(console_value(world), "parent $ ls item");
    console_check_span(world, line, 0, 6, syntax->scope);
    console_check_span(world, line, 12, 16, syntax->entity);

    // Deleting the scope colors the prompt and resolves the typed entity from the root
    ecs_delete(world, parent);
    ecs_progress(world, 0);
    test_str(console_value(world), CONSOLE_PROMPT "ls item");
    console_check_span(world, line, 0, 1, syntax->scope);
    console_check_span(world, line, 2, 3, syntax->symbol);
    console_check_span(world, line, 7, 11, syntax->missing);

    ecs_fini(world);
}

void Console_arg_entity_created(void) {
    // console/syntax
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity(world, { .name = "parent" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);
    console_type(world, "ls item");
    test_str(console_value(world), "parent $ ls item");
    console_check_span(world, line, 12, 16, syntax->missing);

    // Creating the entity outside a command colors the argument without a key
    ecs_entity(world, { .name = "parent.item" });
    ecs_progress(world, 0);
    test_str(console_value(world), "parent $ ls item");
    console_check_span(world, line, 12, 16, syntax->entity);

    ecs_fini(world);
}

void Console_arg_entity_deleted(void) {
    // console/syntax
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity(world, { .name = "parent" });
    ecs_entity_t item = ecs_entity(world, { .name = "parent.item" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);
    console_type(world, "ls item");
    console_check_span(world, line, 12, 16, syntax->entity);

    // Deleting the entity colors the argument missing
    ecs_delete(world, item);
    ecs_progress(world, 0);
    test_str(console_value(world), "parent $ ls item");
    console_check_span(world, line, 12, 16, syntax->missing);

    // Creating it again under a new id colors it again
    ecs_entity(world, { .name = "parent.item" });
    ecs_progress(world, 0);
    console_check_span(world, line, 12, 16, syntax->entity);

    ecs_fini(world);
}

void Console_arg_entity_renamed(void) {
    // console/syntax
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity(world, { .name = "parent" });
    ecs_entity_t item = ecs_entity(world, { .name = "parent.item" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);
    console_type(world, "ls item");
    console_check_span(world, line, 12, 16, syntax->entity);

    // Renaming the entity colors the argument missing
    ecs_set_name(world, item, "renamed");
    ecs_progress(world, 0);
    test_str(console_value(world), "parent $ ls item");
    console_check_span(world, line, 12, 16, syntax->missing);

    ecs_fini(world);
}

void Console_ctrl_a_e(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "alpha beta");
    console_check_cursor(world, 10);
    console_ctrl(world, KeyA);
    console_check_cursor(world, 0);
    test_str(console_typed(world), "alpha beta");

    // Typing goes to the start
    console_type(world, "x");
    test_str(console_typed(world), "xalpha beta");
    console_check_cursor(world, 1);
    console_ctrl(world, KeyE);
    console_check_cursor(world, 11);
    console_type(world, "y");
    test_str(console_typed(world), "xalpha betay");

    ecs_fini(world);
}

void Console_ctrl_u_k(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "one two three");
    console_press(world, KeyLeft, 5);
    console_check_cursor(world, 8);

    // Ctrl+K deletes to the end, the cursor stays
    console_ctrl(world, KeyK);
    test_str(console_typed(world), "one two ");
    console_check_cursor(world, 8);

    // Ctrl+U deletes to the start
    console_press(world, KeyLeft, 4);
    console_ctrl(world, KeyU);
    test_str(console_typed(world), "two ");
    console_check_cursor(world, 0);

    // At the start Ctrl+U and at the end Ctrl+K delete nothing
    console_ctrl(world, KeyU);
    test_str(console_typed(world), "two ");
    console_ctrl(world, KeyE);
    console_ctrl(world, KeyK);
    test_str(console_typed(world), "two ");
    console_check_cursor(world, 4);

    ecs_fini(world);
}

void Console_ctrl_w(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "get cube  Position");

    // Ctrl+W deletes the word before the cursor, with the spaces between it and the cursor
    console_ctrl(world, KeyW);
    test_str(console_typed(world), "get cube  ");
    console_ctrl(world, KeyW);
    test_str(console_typed(world), "get ");
    console_check_cursor(world, 4);

    // From inside a word only the part before the cursor goes
    console_type(world, "cube");
    console_press(world, KeyLeft, 2);
    console_ctrl(world, KeyW);
    test_str(console_typed(world), "get be");
    console_check_cursor(world, 4);
    console_ctrl(world, KeyW);
    console_ctrl(world, KeyW);
    test_str(console_typed(world), "be");
    console_check_cursor(world, 0);

    ecs_fini(world);
}

void Console_ctrl_c(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity(world, { .name = "things.alpha" });
    console_type(world, "ls things");
    console_press(world, KeyLeft, 3);
    console_ctrl(world, KeyC);

    // The line is echoed with ^C after it and cleared, nothing runs
    test_str(console_value(world), CONSOLE_PROMPT);
    console_check_cursor(world, 0);
    ecs_entity_t echo = console_find_line(world, CONSOLE_PROMPT "ls things^C");
    test_assert(echo != 0);
    test_int(ecs_get(world, echo, Text)->hang, CONSOLE_PROMPT_LEN);
    test_assert(console_find_line(world, "things.alpha") == 0);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    console_check_span(world, echo, 0, 1, syntax->scope);
    console_check_span(world, echo, 4, 6, syntax->name);

    // The line is not in the history
    console_press(world, KeyUp, 1);
    test_str(console_value(world), CONSOLE_PROMPT);

    // An empty line is echoed too
    console_ctrl(world, KeyC);
    ecs_entity_t lines[256];
    int32_t count = console_log_lines(world, lines, 256);
    test_str(ecs_get(world, lines[count - 1], Text)->value, CONSOLE_PROMPT "^C");

    ecs_fini(world);
}

void Console_ctrl_l(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity(world, { .name = "things.alpha" });
    ecs_os_free(box_console_run(world, "ls things", NULL));
    ecs_progress(world, 0);
    test_assert(console_find_line(world, CONSOLE_PROMPT "ls things") != 0);
    test_assert(console_find_line(world, "things.alpha") != 0);

    console_type(world, "abc");
    console_ctrl(world, KeyL);
    test_assert(console_find_line(world, CONSOLE_PROMPT "ls things") == 0);
    test_assert(console_find_line(world, "things.alpha") == 0);
    test_assert(console_find_line(world, CONSOLE_HINT) == 0);
    test_str(console_typed(world), "abc");
    test_bool(ecs_singleton_get(world, Console)->open, true);

    ecs_fini(world);
}

void Console_history_filter(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_os_free(box_console_run(world, "ls alpha", NULL));
    ecs_os_free(box_console_run(world, "pwd", NULL));
    ecs_os_free(box_console_run(world, "ls gamma", NULL));
    ecs_os_free(box_console_run(world, "help", NULL));
    ecs_progress(world, 0);

    // Up walks the lines that start with the text typed before it
    console_type(world, "ls");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "ls gamma");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "ls alpha");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "ls alpha");

    // Down walks back and past the newest restores the typed text
    console_press(world, KeyDown, 1);
    test_str(console_typed(world), "ls gamma");
    console_press(world, KeyDown, 1);
    test_str(console_typed(world), "ls");
    console_check_cursor(world, 2);
    console_press(world, KeyDown, 1);
    test_str(console_typed(world), "ls");

    // Text that starts no line walks nothing
    console_type(world, "x");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "lsx");

    // An empty line walks every line
    console_ctrl(world, KeyU);
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "help");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "ls gamma");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "pwd");

    // Editing a line of the walk filters the next walk on the edited line
    console_press(world, KeyBackspace, 2);
    test_str(console_typed(world), "p");
    console_press(world, KeyUp, 1);
    test_str(console_typed(world), "pwd");
    console_press(world, KeyDown, 1);
    test_str(console_typed(world), "p");

    ecs_fini(world);
}

void Console_autosuggest(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_os_free(box_console_run(world, "ls things", NULL));
    ecs_os_free(box_console_run(world, "help", NULL));
    ecs_progress(world, 0);
    test_assert(console_ghost(world) == NULL);

    // The rest of the newest line that starts with the input is drawn dimmed after the cursor
    console_type(world, "ls");
    test_str(console_value(world), CONSOLE_PROMPT "ls things");
    console_check_span(world, line, 6, 13, syntax->ghost);
    test_str(console_typed(world), "ls");
    console_check_cursor(world, 2);

    // Right accepts it
    console_press(world, KeyRight, 1);
    test_str(console_value(world), CONSOLE_PROMPT "ls things");
    test_assert(console_ghost(world) == NULL);
    console_check_cursor(world, 9);

    // It goes when the input no longer matches and comes back when it does
    console_press(world, KeyBackspace, 6);
    test_str(console_typed(world), "ls ");
    test_assert(console_ghost(world) != NULL);
    console_type(world, "x");
    test_str(console_value(world), CONSOLE_PROMPT "ls x");
    test_assert(console_ghost(world) == NULL);
    console_press(world, KeyBackspace, 1);
    test_assert(console_ghost(world) != NULL);

    // End accepts it too
    console_press(world, KeyEnd, 1);
    test_str(console_value(world), CONSOLE_PROMPT "ls things");
    test_assert(console_ghost(world) == NULL);

    // Only with the cursor at the end
    console_ctrl(world, KeyU);
    console_type(world, "h");
    test_str(console_value(world), CONSOLE_PROMPT "help");
    console_press(world, KeyLeft, 1);
    test_str(console_value(world), CONSOLE_PROMPT "h");
    test_assert(console_ghost(world) == NULL);
    console_press(world, KeyRight, 1);
    console_check_cursor(world, 1);
    test_str(console_value(world), CONSOLE_PROMPT "help");
    console_press(world, KeyRight, 1);
    test_str(console_value(world), CONSOLE_PROMPT "help");
    test_assert(console_ghost(world) == NULL);
    console_check_cursor(world, 4);

    // Enter runs only the typed text; a line no longer than the input suggests nothing, an
    // older longer one still does
    console_ctrl(world, KeyU);
    console_type(world, "ls");
    test_assert(console_ghost(world) != NULL);
    console_press(world, KeyEnter, 1);
    test_assert(console_find_line(world, CONSOLE_PROMPT "ls") != 0);
    test_assert(console_ghost(world) == NULL);
    console_type(world, "ls");
    test_str(console_value(world), CONSOLE_PROMPT "ls things");
    console_ctrl(world, KeyU);
    console_type(world, "help");
    test_assert(console_ghost(world) == NULL);
    test_str(console_value(world), CONSOLE_PROMPT "help");

    // Walking the history shows no suggestion
    console_ctrl(world, KeyU);
    console_press(world, KeyUp, 2);
    test_str(console_value(world), CONSOLE_PROMPT "help");
    test_assert(console_ghost(world) == NULL);

    ecs_fini(world);
}

void Console_hint_on_first_open(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    ecs_entity_t hint = console_find_line(world, CONSOLE_HINT);
    test_assert(hint != 0);
    console_check_span(world, hint, 18, 22, syntax->name);

    // Lines that run come after it, closing and opening again does not add it again
    ecs_os_free(box_console_run(world, "pwd", NULL));
    ecs_singleton_get_mut(world, Console)->open = false;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), found = 0, at = -1;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        if (!strcmp(t->value, CONSOLE_HINT)) {
            found ++;
            at = i;
        }
    }

    test_int(found, 1);
    test_str(ecs_get(world, lines[at + 1], Text)->value, CONSOLE_PROMPT "pwd");

    ecs_fini(world);
}

void Console_type_from_script(void) {
    // console/prompt
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_os_free(box_console_run(world, "cd items", NULL));
    ecs_os_free(box_console_run(world, "cd", NULL));
    test_assert(ecs_script_run(world, "type",
        "const typed: string = box.console.type(\"cd i\")\n", NULL) == 0);
    ecs_progress(world, 0);
    test_str(console_typed(world), "cd i");

    // Control characters are dropped, the suggestion follows the typed text
    char *line = box_console_type(world, "\tt\n");
    test_str(line, "cd it");
    ecs_os_free(line);
    ecs_progress(world, 0);
    test_str(console_value(world), CONSOLE_PROMPT "cd items");
    test_str(console_typed(world), "cd it");
    console_check_cursor(world, 5);

    ecs_fini(world);
}

void Console_rewrap_trims_scrollback(void) {
    // console/wrap
    ecs_entity_t s;
    ecs_world_t *world = console_open(640, 360, &s);
    const Console *c = ecs_singleton_get(world, Console);
    float lh = c->line_height;
    int32_t limit = 24;
    Console *w = ecs_singleton_get_mut(world, Console);
    w->width = 1;
    w->scrollback = limit;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    }

    ecs_progress(world, 0);
    ecs_entity_t cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t wide = box_text_line(world, cmd, 0, NULL, NULL);

    // Scrolled to the oldest line that is kept
    for (int32_t i = 0; i < 20; i ++) {
        box_input_mouse_scroll(world, 0, 1);
    }

    console_settle(world);

    // Half the width wraps the lines to more rows: the oldest lines are dropped without
    // running another command
    ecs_singleton_get_mut(world, Console)->width = 0.5f;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t narrow = console_check_fits(world, cmd, CONSOLE_WRAP);
    test_assert(narrow > wide);

    // The scroll offset stays valid: the lines shown fill the window
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG);
    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), rows = 0;
    int32_t visible = (int32_t)floorf(ecs_get(world, log, LayoutBounds)->height / lh);
    float under = -ecs_get(world, log, Layout)->padding.bottom;
    test_assert(count > 0);
    test_assert(under >= 0);
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        test_assert(t->value && t->value[0]);
        rows += box_text_line(world, lines[i], 0, NULL, NULL);
    }

    test_assert(under < (float)box_text_line(world, lines[count - 1], 0, NULL, NULL) * lh);
    test_assert(rows - (int32_t)(under / lh + 0.5f) >= visible);

    // A taller surface shows the whole scrollback, which keeps at most the limit of rows
    ecs_set(world, s, Surface, {.width = 640, .height = 1200, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(console_shown_rows(world) <= limit);
    test_assert(console_shown_rows(world) > limit - narrow - 1);
    test_flt(ecs_get(world, log, Layout)->padding.bottom, 0);
    test_assert(console_count_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND) > 0);

    ecs_fini(world);
}

void Console_rewrap_trim_waits_for_font(void) {
    // console/wrap
    // A surface high enough to show the whole scrollback
    ecs_entity_t s;
    ecs_world_t *world = console_open(640, 1200, &s);
    const Console *c = ecs_singleton_get(world, Console);
    float lh = c->line_height;
    int32_t limit = 16, runs = 4;
    Console *w = ecs_singleton_get_mut(world, Console);
    w->width = 1;
    w->scrollback = limit;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    for (int32_t i = 0; i < runs; i ++) {
        ecs_os_free(box_console_run(world, CONSOLE_LONG_COMMAND, NULL));
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t wide = box_text_line(world, cmd, 0, NULL, NULL);
    test_assert(console_find_line(world, CONSOLE_HINT) != 0);
    test_int(console_count_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND), runs);
    test_assert(console_shown_rows(world) <= limit);
    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), before = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        before += t->value && t->value[0];
    }

    test_int(before, 1 + 2 * runs);

    // The default font loads again from the start: without its Font its face is dropped, and
    // the next measure requests the new Font, which has no face until the load completes
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    test_assert(font != 0);
    Font f = *ecs_get(world, font, Font);
    char *path = ecs_os_strdup(f.path);
    ecs_remove(world, font, Font);
    f.path = path;
    ecs_set_ptr(world, font, Font, &f);
    ecs_os_free(path);

    // Half the width wraps the lines to more rows than the limit, but while the font loads
    // they cannot be measured: the trim waits instead of counting one row per line
    ecs_singleton_get_mut(world, Console)->width = 0.5f;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    const FontStatus *status = ecs_get(world, font, FontStatus);
    test_assert(status != NULL);
    test_int(status->state, FontLoading);
    test_assert(console_find_line(world, CONSOLE_HINT) != 0);
    test_int(console_count_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND), runs);
    count = console_log_lines(world, lines, 256);
    int32_t loading = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        loading += t->value && t->value[0];
    }

    test_int(loading, before);

    // Once the font is loaded the trim happens without another command
    for (i = 0; i < 100; i ++) {
        status = ecs_get(world, font, FontStatus);
        if (status && status->state != FontLoading) {
            break;
        }

        ecs_progress(world, 0);
    }

    status = ecs_get(world, font, FontStatus);
    test_assert(status != NULL);
    test_int(status->state, FontReady);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    cmd = console_find_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t narrow = console_check_fits(world, cmd, CONSOLE_WRAP);
    test_assert(narrow > wide);
    test_assert(runs * narrow > limit);

    // The oldest lines were dropped: the hint and the first commands
    test_assert(console_find_line(world, CONSOLE_HINT) == 0);
    int32_t kept = console_count_line(world, CONSOLE_PROMPT CONSOLE_LONG_COMMAND);
    test_assert(kept > 0);
    test_assert(kept < runs);
    test_assert(console_shown_rows(world) <= limit);
    test_assert(console_shown_rows(world) > limit - narrow - 1);
    count = console_log_lines(world, lines, 256);
    int32_t after = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        after += t->value && t->value[0];
    }

    test_assert(after < before);
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG);
    test_flt(ecs_get(world, log, Layout)->padding.bottom, 0);

    // The scroll offset stays valid: scrolled to the oldest line on a short surface, the
    // lines shown fill the window
    ecs_set(world, s, Surface, {.width = 640, .height = 160, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    for (i = 0; i < 20; i ++) {
        box_input_mouse_scroll(world, 0, 1);
    }

    console_settle(world);
    count = console_log_lines(world, lines, 256);
    int32_t rows = 0;
    int32_t visible = (int32_t)floorf(ecs_get(world, log, LayoutBounds)->height / lh);
    float under = -ecs_get(world, log, Layout)->padding.bottom;
    test_assert(count > 0);
    test_assert(under >= 0);
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        test_assert(t->value && t->value[0]);
        rows += box_text_line(world, lines[i], 0, NULL, NULL);
    }

    test_assert(rows <= limit);
    test_assert(under < (float)box_text_line(world, lines[count - 1], 0, NULL, NULL) * lh);
    test_assert(rows - (int32_t)(under / lh + 0.5f) >= visible);

    ecs_fini(world);
}

void Console_trailing_spaces_move_cursor(void) {
    // console/wrap
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    float lh = c->line_height;
    ecs_entity_t input = ecs_lookup(world, CONSOLE_INPUT);
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    ecs_entity_t cursor = ecs_lookup(world, CONSOLE_INPUT ".cursor");
    char typed[256] = {0};
    int32_t len = 0;

    // A line that fills the first row
    while (len < 200) {
        console_type(world, "x");
        typed[len ++] = 'x';
        if (box_text_line(world, line, 0, NULL, NULL) > 1) {
            break;
        }
    }

    test_assert(len < 200);
    console_press(world, KeyBackspace, 1);
    typed[-- len] = '\0';
    test_int(box_text_line(world, line, 0, NULL, NULL), 1);
    test_flt(ecs_get(world, input, LayoutBounds)->height, lh);
    test_int(console_check_cursor(world, len), 0);

    // Spaces past the right edge move the cursor to the next row, under the start of the line
    console_type(world, "            ");
    int32_t spaces = len + 12;
    test_int(box_text_line(world, line, 0, NULL, NULL), 2);
    test_flt(ecs_get(world, input, LayoutBounds)->height, 2 * lh);
    test_int(console_check_cursor(world, spaces), 1);
    TextCaret hang;
    test_assert(box_text_caret(world, line, CONSOLE_PROMPT_LEN, &hang));
    float x = hang.x + ecs_get(world, line, TextBounds)->width * 0.5f;
    test_flt(ecs_get(world, cursor, LayoutItem)->x, x);
    test_assert(x > 0);

    // The log gives up the row
    float log_h = ecs_get(world, ecs_lookup(world, CONSOLE_LOG), LayoutBounds)->height;
    test_flt(log_h, 360 - 2 * c->padding - 2 * lh);

    // A word typed after them starts the next row there
    console_type(world, "y");
    test_int(box_text_line(world, line, 0, NULL, NULL), 2);
    TextCaret word;
    test_assert(box_text_caret(world, line, CONSOLE_PROMPT_LEN + spaces, &word));
    test_int(word.line, 1);
    test_flt(word.x + ecs_get(world, line, TextBounds)->width * 0.5f, x);
    test_int(console_check_cursor(world, spaces + 1), 1);

    // Back to the spaces, the line ran with them does not gain an empty row in the scrollback
    console_press(world, KeyBackspace, 1);
    test_int(console_check_cursor(world, spaces), 1);
    console_press(world, KeyEnter, 1);
    test_flt(ecs_get(world, input, LayoutBounds)->height, lh);
    ecs_entity_t lines[256];
    int32_t i, count = console_log_lines(world, lines, 256), found = 0;
    for (i = 0; i < count; i ++) {
        const Text *t = ecs_get(world, lines[i], Text);
        if (t->value && !strncmp(t->value, CONSOLE_PROMPT, CONSOLE_PROMPT_LEN) &&
            !strncmp(t->value + CONSOLE_PROMPT_LEN, typed, (size_t)len))
        {
            found ++;
            test_int(box_text_line(world, lines[i], 0, NULL, NULL), 1);
            test_flt(ecs_get(world, lines[i], LayoutItem)->height.value, lh);
        }
    }

    test_int(found, 1);

    ecs_fini(world);
}

/** Path of the candidate list of Tab completion under the input line */
#define CONSOLE_MENU "console.ui.screen.stack.panel.content.menu"

/** Hold Shift, tap Tab and release Shift, then draw the console.
 *
 * @param world The world
 */
static void console_shift_tab(
    ecs_world_t *world)
{
    box_input_key(world, KeyShiftLeft, true);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_input_key_press(world, KeyTab, 1);
    box_input_key(world, KeyShiftLeft, false);
    console_settle(world);
}

/** Get the string of the candidate list of the console.
 *
 * @param world The world
 * @return The string, empty while the list is closed
 */
static const char* console_menu(
    ecs_world_t *world)
{
    ecs_entity_t menu = ecs_lookup(world, CONSOLE_MENU);
    test_assert(menu != 0);
    const Text *t = ecs_get(world, menu, Text);
    test_assert(t != NULL && t->value != NULL);

    return t->value;
}

/** Get the height the candidate list takes under the input line.
 *
 * @param world The world
 * @return The height in points, 0 while the list is closed
 */
static float console_menu_height(
    ecs_world_t *world)
{
    const LayoutItem *item = ecs_get(world, ecs_lookup(world, CONSOLE_MENU), LayoutItem);
    test_assert(item != NULL);

    return item->height.value;
}

/** Find the byte a word starts at in the candidate list.
 *
 * @param world The world
 * @param word The word
 * @return The byte, -1 when the list does not show it
 */
static int32_t console_menu_at(
    ecs_world_t *world,
    const char *word)
{
    const char *value = console_menu(world);
    const char *p = strstr(value, word);

    return p ? (int32_t)(p - value) : -1;
}

void Console_complete_command_unique(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "pw");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "pwd ");
    console_check_cursor(world, 4);
    test_str(console_menu(world), "");
    test_flt(console_menu_height(world), 0);

    // The console commands complete too
    console_ctrl(world, KeyU);
    console_type(world, "cl");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "cl");
    test_assert(console_menu_at(world, "cls") >= 0);
    test_assert(console_menu_at(world, "clear") >= 0);
    console_type(world, "s");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "cls ");

    // Completing the name does not run the line
    test_assert(console_find_line(world, CONSOLE_PROMPT "pwd") == 0);

    ecs_fini(world);
}

void Console_complete_command_ambiguous(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    console_type(world, "sche");

    // The common prefix of schema and schema_value is inserted, the list stays closed
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "schema");
    test_str(console_menu(world), "");

    // The prefix adds nothing now: the list opens under the prompt, the line stays
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "schema");
    test_str(console_menu(world), "schema  schema_value");
    test_flt(console_menu_height(world), c->line_height);
    ecs_entity_t menu = ecs_lookup(world, CONSOLE_MENU);
    console_check_span(world, menu, 0, 6, c->output.command);
    console_check_span(world, menu, 8, 20, c->output.command);

    // The list takes its rows from the scrollback
    float log_h = ecs_get(world, ecs_lookup(world, CONSOLE_LOG), LayoutBounds)->height;
    test_flt(log_h, 360 - 2 * c->padding - 2 * c->line_height);

    // A word nothing starts with rings the bell on the prompt and changes nothing
    console_ctrl(world, KeyU);
    test_str(console_menu(world), "");
    console_type(world, "zzz");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "zzz");
    test_str(console_menu(world), "");
    ecs_entity_t line = ecs_lookup(world, CONSOLE_INPUT ".line");
    console_check_span(world, line, 0, 1, c->error);

    ecs_fini(world);
}

void Console_complete_help_topic(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "help pw");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "help pwd ");

    // The help group of the console commands is a topic too
    console_ctrl(world, KeyU);
    console_type(world, "help con");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "help console ");

    // The second argument of help completes nothing
    console_type(world, "pw");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "help console pw");

    // Neither does a string parameter of another command
    console_ctrl(world, KeyU);
    console_type(world, "eval pw");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "eval pw");

    ecs_fini(world);
}

void Console_complete_entity_in_scope(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    ecs_entity(world, { .name = "parent.child_alpha" });
    ecs_entity(world, { .name = "parent.child_beta" });
    ecs_entity(world, { .name = "parent.other" });
    ecs_entity(world, { .name = "outside" });
    ecs_os_free(box_console_run(world, "cd parent", NULL));
    ecs_progress(world, 0);

    // Names complete from the children of the session scope
    console_type(world, "cat ot");
    console_press(world, KeyTab, 1);
    test_str(console_value(world), "parent $ cat other ");

    // Only from that one parent
    console_ctrl(world, KeyU);
    console_type(world, "cat outs");
    console_press(world, KeyTab, 1);
    test_str(console_value(world), "parent $ cat outs");

    // Several children: the common prefix, then the list in the entity color
    console_ctrl(world, KeyU);
    console_type(world, "cat c");
    console_press(world, KeyTab, 1);
    test_str(console_value(world), "parent $ cat child_");
    console_press(world, KeyTab, 1);
    test_str(console_menu(world), "child_alpha  child_beta");
    console_check_span(world, ecs_lookup(world, CONSOLE_MENU), 0, 11, c->output.entity);

    // An entity argument after the first completes too, a component in the component color
    console_ctrl(world, KeyU);
    ecs_os_free(box_console_run(world, "cd", NULL));
    ecs_progress(world, 0);
    ecs_entity_t pos = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Position2" }),
        .type = { .size = 8, .alignment = 4 }
    });
    test_assert(pos != 0);
    ecs_entity(world, { .name = "Position3" });
    console_type(world, "get outside Posi");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "get outside Position");
    console_press(world, KeyTab, 1);
    int32_t at = console_menu_at(world, "Position2");
    test_assert(at >= 0);
    console_check_span(world, ecs_lookup(world, CONSOLE_MENU), at, at + 9,
        c->output.component);

    ecs_fini(world);
}

void Console_complete_dotted_path(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity(world, { .name = "galaxy.star_one" });
    ecs_entity(world, { .name = "galaxy.star_two" });
    ecs_entity(world, { .name = "galaxy.planet" });
    ecs_entity(world, { .name = "star_root" });

    // The part after the last dot completes from the children of the part before it
    console_type(world, "cat galaxy.star_t");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "cat galaxy.star_two ");

    console_ctrl(world, KeyU);
    console_type(world, "cat galaxy.");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "cat galaxy.");
    test_str(console_menu(world), "planet  star_one  star_two");

    // The parent resolves from the session scope like the shell resolves it
    console_ctrl(world, KeyU);
    ecs_os_free(box_console_run(world, "cd galaxy", NULL));
    ecs_entity(world, { .name = "galaxy.planet.moon" });
    ecs_progress(world, 0);
    console_type(world, "ls planet.m");
    console_press(world, KeyTab, 1);
    test_str(console_value(world), "galaxy $ ls planet.moon ");

    // A parent that does not resolve completes nothing
    console_ctrl(world, KeyU);
    console_type(world, "ls nothing.m");
    console_press(world, KeyTab, 1);
    test_str(console_value(world), "galaxy $ ls nothing.m");

    ecs_fini(world);
}

void Console_complete_entity_with_children(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity(world, { .name = "warehouse.shelf.box" });
    ecs_entity(world, { .name = "warehouse.crate" });

    // An entity with children gets a dot, so the next Tab completes its children
    console_type(world, "ls wareh");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "ls warehouse.");
    console_type(world, "sh");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "ls warehouse.shelf.");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "ls warehouse.shelf.box ");

    // One without children a space
    console_ctrl(world, KeyU);
    console_type(world, "ls warehouse.cr");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "ls warehouse.crate ");

    ecs_fini(world);
}

void Console_complete_not_in_string(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    ecs_entity(world, { .name = "galaxy" });

    // Not inside a string
    console_type(world, "cat \"gal");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "cat \"gal");
    test_str(console_menu(world), "");

    // Not inside a value expression
    console_ctrl(world, KeyU);
    console_type(world, "set galaxy Position {x: gal");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "set galaxy Position {x: gal");

    // Nor in a string parameter: the path of mv
    console_ctrl(world, KeyU);
    console_type(world, "mv galaxy gal");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "mv galaxy gal");

    // A bare word after a closed string still completes
    console_ctrl(world, KeyU);
    console_type(world, "get \"galaxy\" gal");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "get \"galaxy\" galaxy ");

    ecs_fini(world);
}

void Console_complete_cycle(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    console_type(world, "schema");
    console_press(world, KeyTab, 1);
    test_str(console_menu(world), "schema  schema_value");

    // Tab inserts the next candidate and highlights it
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "schema");
    test_assert(console_menu_at(world, "schema") == 0);
    ecs_entity_t menu = ecs_lookup(world, CONSOLE_MENU);
    console_check_span(world, menu, 0, 6, c->output.match);
    test_assert(strstr(console_menu(world), "\n") != NULL);
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "schema_value");
    console_check_span(world, menu, 8, 20, c->output.match);
    console_check_cursor(world, 12);

    // Past the last it wraps to the first
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "schema");

    // Shift+Tab cycles back, wrapping to the last
    console_shift_tab(world);
    test_str(console_typed(world), "schema_value");
    console_shift_tab(world);
    test_str(console_typed(world), "schema");
    console_shift_tab(world);
    test_str(console_typed(world), "schema_value");

    // Enter accepts the active candidate without running the line
    console_press(world, KeyEnter, 1);
    test_str(console_typed(world), "schema_value ");
    test_str(console_menu(world), "");
    test_assert(console_find_line(world, CONSOLE_PROMPT "schema_value") == 0);

    // With the list closed Enter runs the line again
    console_press(world, KeyEnter, 1);
    test_assert(console_find_line(world, CONSOLE_PROMPT "schema_value ") != 0);

    // Past the visible limit the list ends with the number of candidates it does not show
    for (int32_t i = 0; i < 40; i ++) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "many.c%02d", i);
        ecs_entity(world, { .name = name });
    }

    console_type(world, "ls many.");
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "ls many.c");
    console_press(world, KeyTab, 1);
    test_assert(strstr(console_menu(world), "... 16 more") != NULL);
    int32_t more = console_menu_at(world, "... 16 more");
    console_check_span(world, menu, more - 2, more + 11, c->output.dim);

    // Cycling past the limit scrolls the shown candidates
    console_shift_tab(world);
    test_str(console_typed(world), "ls many.c39");
    test_assert(console_menu_at(world, "c39") >= 0);
    test_assert(console_menu_at(world, "c00") < 0);

    ecs_fini(world);
}

void Console_complete_escape_closes_list(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "schema");
    console_press(world, KeyTab, 2);
    test_str(console_typed(world), "schema");
    test_assert(console_menu(world)[0] != '\0');

    // Escape closes the list, the console stays open with the line
    console_press(world, KeyEscape, 1);
    test_str(console_menu(world), "");
    test_flt(console_menu_height(world), 0);
    test_bool(ecs_singleton_get(world, Console)->open, true);
    test_str(console_typed(world), "schema");

    // The next Escape closes the console
    console_press(world, KeyEscape, 1);
    test_bool(ecs_singleton_get(world, Console)->open, false);

    ecs_fini(world);
}

void Console_complete_typing_closes_list(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "schema");
    console_press(world, KeyTab, 2);
    test_str(console_typed(world), "schema");
    test_assert(console_menu(world)[0] != '\0');

    // Typing closes the list and keeps the text, the candidate Tab inserted included
    console_type(world, "_");
    test_str(console_menu(world), "");
    test_str(console_typed(world), "schema_");

    console_press(world, KeyBackspace, 1);
    console_press(world, KeyTab, 1);
    console_press(world, KeyTab, 1);
    test_str(console_typed(world), "schema");
    console_press(world, KeyLeft, 1);
    test_str(console_menu(world), "");
    test_str(console_typed(world), "schema");

    // So does a line that runs from a script
    console_press(world, KeyEnd, 1);
    console_press(world, KeyTab, 1);
    test_assert(console_menu(world)[0] != '\0');
    ecs_os_free(box_console_run(world, "pwd", NULL));
    ecs_progress(world, 0);
    test_str(console_menu(world), "");

    // Completing from a script cycles like Tab
    char *line = box_console_complete(world, false);
    test_str(line, "schema");
    ecs_os_free(line);
    line = box_console_complete(world, false);
    test_str(line, "schema");
    ecs_os_free(line);
    line = box_console_complete(world, true);
    test_str(line, "schema_value");
    ecs_os_free(line);

    ecs_fini(world);
}

void Console_complete_hide_closes_list(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    console_type(world, "schema");
    console_press(world, KeyTab, 1);
    test_assert(console_menu_at(world, "schema_value") >= 0);

    // Closing and opening the console again does not bring back the old candidates
    ecs_singleton_get_mut(world, Console)->open = false;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_str(console_menu(world), "");
    test_flt(console_menu_height(world), 0);

    // Tab computes the candidates again
    console_press(world, KeyTab, 1);
    test_assert(console_menu_at(world, "schema_value") >= 0);

    ecs_fini(world);
}

void Console_complete_module_color(void) {
    // console/complete
    ecs_world_t *world = console_open(640, 360, NULL);
    const Console *c = ecs_singleton_get(world, Console);
    ecs_entity_t mod = ecs_entity(world, { .name = "zebra_mod" });
    ecs_add_id(world, mod, EcsModule);
    ecs_entity(world, { .name = "zebra_ent" });

    // A module is drawn in the scope color, another entity in the entity color
    console_type(world, "ls zebra");
    console_press(world, KeyTab, 2);
    test_str(console_typed(world), "ls zebra_");
    int32_t at_mod = console_menu_at(world, "zebra_mod");
    int32_t at_ent = console_menu_at(world, "zebra_ent");
    test_assert(at_mod >= 0);
    test_assert(at_ent >= 0);
    ecs_entity_t menu = ecs_lookup(world, CONSOLE_MENU);
    console_check_span(world, menu, at_mod, at_mod + 9, c->syntax.scope);
    console_check_span(world, menu, at_ent, at_ent + 9, c->output.entity);

    ecs_fini(world);
}
