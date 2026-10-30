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
 * panel is as high as the window and the visible lines fill the log up to less than one line.
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
        lines += it.count;
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

    ecs_entity_t cmd = console_find_line(world, "> " CONSOLE_LONG_COMMAND);
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
    ecs_entity_t cmd = console_find_line(world, "> " CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    test_int(ecs_get(world, cmd, Text)->hang, 2);
    TextCaret prompt, row;
    test_assert(box_text_caret(world, cmd, 2, &prompt));
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

    const char *value = "> " CONSOLE_LONG_COMMAND;
    ecs_entity_t cmd = console_find_line(world, value);
    test_assert(cmd != 0);
    test_assert(box_text_line(world, cmd, 0, NULL, NULL) >= 3);
    const ConsoleSyntax *syntax = &ecs_singleton_get(world, Console)->syntax;
    int32_t i, len = (int32_t)strlen(value), word = -1;
    bool last_row_colored = false;
    for (i = 2; i < len; i ++) {
        const TextSpan *s = console_span_at(world, cmd, i);
        if (value[i] == ' ') {
            test_assert(s == NULL);
            continue;
        }

        word += i == 2 || value[i - 1] == ' ';
        test_assert(s != NULL);
        test_assert(s->start <= i && s->end > i);
        test_assert(s->start == 2 || value[s->start - 1] == ' ');
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
    test_assert(console_span_at(world, cmd, 0) == NULL);

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
    test_assert(box_text_caret(world, line, 2 + offset, &caret));
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
    test_str(ecs_get(world, line, Text)->value, "> " CONSOLE_LONG_COMMAND);
    test_int(ecs_get(world, line, Text)->hang, 2);
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
    test_assert(start > 2);
    box_input_key_press(world, KeyHome, 1);
    for (int32_t i = 0; i < start - 2; i ++) {
        box_input_key_press(world, KeyRight, 1);
    }

    console_settle(world);
    test_int(console_check_cursor(world, start - 2), 1);
    box_input_key_press(world, KeyLeft, 1);
    console_settle(world);
    test_int(console_check_cursor(world, start - 3), 0);
    box_input_key_press(world, KeyEnd, 1);
    console_settle(world);
    test_int(console_check_cursor(world, len), rows - 1);

    // Running the line gives the rows back to the log
    box_input_key_press(world, KeyEnter, 1);
    console_settle(world);
    test_str(ecs_get(world, line, Text)->value, "> ");
    test_flt(ecs_get(world, input, LayoutBounds)->height, lh);
    test_assert(console_find_line(world, "> " CONSOLE_LONG_COMMAND) != 0);

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
    test_str(ecs_get(world, cmd, Text)->value, "> " CONSOLE_LONG_COMMAND);
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
    test_str(ecs_get(world, lines[count - 1], Text)->value, "> " CONSOLE_LONG_COMMAND);
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
    ecs_entity_t cmd = console_find_line(world, "> " CONSOLE_LONG_COMMAND);
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
    test_assert(console_find_line(world, "> " CONSOLE_LONG_COMMAND) == 0);
    test_assert(console_find_line(world, "> cls x") != 0);

    ecs_fini(world);
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
    ecs_entity_t cmd = console_find_line(world, "> " CONSOLE_LONG_COMMAND);
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
    test_assert(console_find_line(world, "> " CONSOLE_LONG_COMMAND) == cmd);
    test_int(sets, 0);

    // A wider surface wraps it at the new width
    ecs_set(world, s, Surface, {.width = 1280, .height = 365, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    cmd = console_find_line(world, "> " CONSOLE_LONG_COMMAND);
    test_assert(cmd != 0);
    int32_t wide = console_check_fits(world, cmd, 640 - 20);
    test_assert(wide < narrow);
    test_assert(sets > 0);

    // So does a smaller share of the surface
    ecs_singleton_get_mut(world, Console)->width = 0.25f;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    cmd = console_find_line(world, "> " CONSOLE_LONG_COMMAND);
    test_assert(console_check_fits(world, cmd, CONSOLE_WRAP) == narrow);
    test_flt(ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".line"), Text)->width,
        CONSOLE_WRAP);

    ecs_fini(world);
}
