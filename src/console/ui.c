/**
 * @file console/ui.c
 * @brief The console content: a UI scene with a panel, the visible scrollback and the input line.
 */

#include "console.h"
#include <math.h>
#include <string.h>

/** Points of the line along the bottom edge of the panel */
#define BOX_CONSOLE_EDGE (2.0f)

/** Create a named element of the console content.
 *
 * @param world The world
 * @param parent The parent
 * @param name The name
 * @return The element
 */
static ecs_entity_t box_console_element(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t result = ecs_entity(world, { .name = name, .parent = parent });
    ecs_set(world, result, Position, {0, 0, 0});

    return result;
}

/** Create a named text element that starts from the Text constructor.
 *
 * @param world The world
 * @param parent The parent
 * @param name The name
 * @return The element
 */
static ecs_entity_t box_console_text_element(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t result = box_console_element(world, parent, name);
    ecs_add(world, result, Text);

    return result;
}

/** Set the color of an element when it differs.
 *
 * @param world The world
 * @param e The element
 * @param color The color
 */
static void box_console_color(
    ecs_world_t *world,
    ecs_entity_t e,
    rgb color)
{
    const Rgb *cur = ecs_get(world, e, Rgb);
    if (!cur || cur->r != color.r || cur->g != color.g || cur->b != color.b) {
        ecs_set(world, e, Rgb, { color.r, color.g, color.b });
    }
}

/** Get the Text a console line is drawn with: wrapped at the width of the panel and kept at
 * those rows by the layout.
 *
 * @param con The console state
 * @param console The Console singleton
 * @param value The string
 * @param hang Byte of the string the wrapped rows start under
 * @return The Text, which borrows the string
 */
static Text box_console_look(
    const box_console_t *con,
    const Console *console,
    const char *value,
    int32_t hang)
{
    return (Text){
        .value = ECS_CONST_CAST(char*, value),
        .size = console->font_size,
        .line_height = console->line_height,
        .width = con->wrap,
        .font = con->font,
        .wrap = TextWrapNewlines,
        .hang = hang
    };
}

/** Set the string, size, wrap width and color of a text element when they differ.
 *
 * @param world The world
 * @param e The element
 * @param value The string
 * @param hang Byte of the string the wrapped rows start under
 * @param con The console state
 * @param console The Console singleton
 * @param color The color
 */
static void box_console_text(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *value,
    int32_t hang,
    const box_console_t *con,
    const Console *console,
    rgb color)
{
    const Text *cur = ecs_get(world, e, Text);
    Text look = box_console_look(con, console, value, hang);
    bool same = cur && cur->value && !strcmp(cur->value, value) &&
        cur->size == look.size && cur->line_height == look.line_height &&
        cur->width == look.width && cur->wrap == look.wrap && cur->hang == look.hang;
    if (!same) {
        if (!cur) {
            ecs_add(world, e, Text);
        }

        Text *t = ecs_get_mut(world, e, Text);
        ecs_os_free(t->value);
        t->value = ecs_os_strdup(value);
        t->size = look.size;
        t->line_height = look.line_height;
        t->width = look.width;
        t->wrap = look.wrap;
        t->hang = look.hang;
        ecs_modified(world, e, Text);
    }

    box_console_color(world, e, color);
}

/** Get the color of a part of a command line or of output.
 *
 * @param console The Console singleton
 * @param kind What the part is
 * @return The color
 */
static rgb box_console_syntax_color(
    const Console *console,
    box_console_syntax_t kind)
{
    const ConsoleSyntax *c = &console->syntax;
    const ConsoleOutput *o = &console->output;
    rgb result = console->text;
    switch (kind) {
    case BoxSyntaxName: result = c->name; break;
    case BoxSyntaxUnknown: result = c->unknown; break;
    case BoxSyntaxEntity: result = c->entity; break;
    case BoxSyntaxMissing: result = c->missing; break;
    case BoxSyntaxNumber: result = c->number; break;
    case BoxSyntaxString: result = c->string; break;
    case BoxSyntaxBoolean: result = c->boolean; break;
    case BoxSyntaxValue: result = c->value; break;
    case BoxSyntaxExtra: result = c->extra; break;
    case BoxSyntaxScope: result = c->scope; break;
    case BoxSyntaxSymbol: result = c->symbol; break;
    case BoxSyntaxGhost: result = c->ghost; break;
    case BoxOutputText: result = console->text; break;
    case BoxOutputError: result = console->error; break;
    case BoxOutputDim: result = o->dim; break;
    case BoxOutputEntity: result = o->entity; break;
    case BoxOutputComponent: result = o->component; break;
    case BoxOutputKey: result = o->key; break;
    case BoxOutputNumber: result = o->number; break;
    case BoxOutputString: result = o->string; break;
    case BoxOutputBoolean: result = o->boolean; break;
    case BoxOutputKeyword: result = o->keyword; break;
    case BoxOutputCommand: result = o->command; break;
    case BoxOutputParam: result = o->param; break;
    case BoxOutputType: result = o->type; break;
    case BoxOutputBrief: result = o->brief; break;
    case BoxOutputGroup: result = o->group; break;
    case BoxOutputMatch: result = o->match; break;
    case BoxOutputAdded: result = o->added; break;
    case BoxOutputRemoved: result = o->removed; break;
    case BoxOutputRed: result = o->red; break;
    case BoxOutputGreen: result = o->green; break;
    case BoxOutputYellow: result = o->yellow; break;
    case BoxOutputBlue: result = o->blue; break;
    case BoxOutputMagenta: result = o->magenta; break;
    case BoxOutputCyan: result = o->cyan; break;
    case BoxOutputWhite: result = o->white; break;
    case BoxOutputGrey: result = o->grey; break;
    case BoxSpanNone: break;
    }

    return result;
}

/** Test whether the TextSpans of an element are the given spans.
 *
 * @param cur The TextSpans, NULL when the element has none
 * @param spans The spans
 * @param count Number of spans
 * @return Whether they are the same
 */
static bool box_console_spans_same(
    const TextSpans *cur,
    const TextSpan *spans,
    int32_t count)
{
    int32_t i, have = cur ? ecs_vec_count(&cur->spans) : 0;
    bool result = have == count;
    for (i = 0; result && i < count; i ++) {
        const TextSpan *a = ecs_vec_get_t(&cur->spans, TextSpan, i);
        const TextSpan *b = &spans[i];
        result = a->start == b->start && a->end == b->end && a->color.r == b->color.r &&
            a->color.g == b->color.g && a->color.b == b->color.b && a->color.a == b->color.a;
    }

    return result;
}

/** Set the TextSpans of a text element to the spans of the line it shows, when they differ.
 * Only the colors of the glyphs change, so the text is not laid out again. The ranges are bytes
 * of the string, so they hold on every row the string wraps to.
 *
 * @param world The world
 * @param e The element
 * @param spans The box_console_span_t of the line, NULL for none
 * @param shift Bytes in front of the line in the string of the element, the prompt
 * @param console The Console singleton
 */
static void box_console_spans(
    ecs_world_t *world,
    ecs_entity_t e,
    const ecs_vec_t *spans,
    int32_t shift,
    const Console *console)
{
    int32_t i, count = 0, total = spans ? ecs_vec_count(spans) : 0;
    TextSpan *out = total ? ecs_os_malloc_n(TextSpan, total) : NULL;
    const TextSpans *cur = ecs_get(world, e, TextSpans);
    for (i = 0; i < total; i ++) {
        const box_console_span_t *s = ecs_vec_get_t(spans, box_console_span_t, i);
        if (s->end > s->start) {
            rgb c = box_console_syntax_color(console, s->kind);
            out[count ++] = (TextSpan){
                .start = s->start + shift,
                .end = s->end + shift,
                .color = { c.r, c.g, c.b, 255 }
            };
        }
    }

    if (box_console_spans_same(cur, out, count)) {
        goto done;
    }

    if (!count) {
        ecs_remove(world, e, TextSpans);
        goto done;
    }

    TextSpans *ts = ecs_ensure(world, e, TextSpans);
    ecs_vec_clear(&ts->spans);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &ts->spans, TextSpan) = out[i];
    }

    ecs_modified(world, e, TextSpans);
done:
    ecs_os_free(out);
}

void box_console_ui_build(
    ecs_world_t *world,
    box_console_t *con)
{
    box_console_ui_t *ui = &con->ui;
    if (ui->root && ecs_is_alive(world, ui->root)) {
        goto done;
    }

    ecs_vec_clear(&ui->lines);
    ui->root = ecs_entity(world, { .name = "ui", .parent = box_console_scope(world, con) });
    ecs_set(world, ui->root, Scene, {0});
    ui->camera = box_console_element(world, ui->root, "camera");
    ecs_set(world, ui->camera, Position, {0, 0, 100});
    ecs_set(world, ui->camera, Camera, {
        .fov = 60, .near_ = 0.1f, .far_ = 1000, .ortho = true, .ortho_height = 10,
        .ortho_scale = 1
    });

    ui->screen = box_console_element(world, ui->root, "screen");
    ecs_set(world, ui->screen, Layout, { .direction = DirectionRow });
    ui->stack = box_console_element(world, ui->screen, "stack");
    ecs_set(world, ui->stack, Layout, { .direction = DirectionColumn });
    ui->panel = box_console_element(world, ui->stack, "panel");
    ecs_set(world, ui->panel, Layout, { .direction = DirectionNone });
    ui->below = box_console_element(world, ui->stack, "below");
    ui->beside = box_console_element(world, ui->screen, "beside");
    ui->content = box_console_element(world, ui->panel, "content");
    ecs_set(world, ui->content, Layout, { .direction = DirectionColumn });
    ecs_add(world, ui->content, ClipChildren);
    ui->edge = box_console_element(world, ui->panel, "edge");
    ecs_set(world, ui->edge, Rect, {0, 0});
    ui->log = box_console_element(world, ui->content, "log");
    ecs_set(world, ui->log, Layout, { .direction = DirectionColumn, .v = VAlignBottom });
    ecs_add(world, ui->log, ClipChildren);
    ui->input = box_console_element(world, ui->content, "input");
    ecs_set(world, ui->input, Layout, { .direction = DirectionNone });
    ui->line = box_console_text_element(world, ui->input, "line");
    ecs_set(world, ui->line, LayoutItem, { .h = AlignLeft, .v = VAlignTop });
    ui->cursor = box_console_element(world, ui->input, "cursor");
    ecs_set(world, ui->cursor, LayoutItem, { .h = AlignLeft, .v = VAlignTop });
    ui->menu = box_console_text_element(world, ui->content, "menu");
    if (!con->font) {
        con->font = ecs_get(world, ui->line, Text)->font;
    }

done:
    return;
}

/** Make the number of scrollback elements match the lines and empty rows that are shown.
 *
 * @param world The world
 * @param con The console state
 * @param count Elements shown
 */
static void box_console_ui_lines(
    ecs_world_t *world,
    box_console_t *con,
    int32_t count)
{
    box_console_ui_t *ui = &con->ui;
    while (ecs_vec_count(&ui->lines) > count) {
        ecs_delete(world, ecs_vec_last_t(&ui->lines, ecs_entity_t)[0]);
        ecs_vec_remove_last(&ui->lines);
    }

    while (ecs_vec_count(&ui->lines) < count) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "line_%d", ecs_vec_count(&ui->lines));
        ecs_vec_append_t(NULL, &ui->lines, ecs_entity_t)[0] =
            box_console_text_element(world, ui->log, name);
    }
}

/** Set the height of an element to a number of rows when it differs.
 *
 * @param world The world
 * @param e The element
 * @param rows The rows
 * @param console The Console singleton
 */
static void box_console_rows_height(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t rows,
    const Console *console)
{
    float height = (float)rows * console->line_height;
    const LayoutItem *cur = ecs_get(world, e, LayoutItem);
    if (!cur || cur->height.mode != SizeModeFixed || cur->height.value != height) {
        ecs_set(world, e, LayoutItem, {
            .height = { .mode = SizeModeFixed, .value = height }
        });
    }
}

/** Make an element take a share of the free space of its parent on one axis.
 *
 * A share of one or more takes all of it and a share of zero or less none of it, which the
 * weight of a growing size cannot express.
 *
 * @param item The sizing to set
 * @param share Part of the axis the element takes
 */
static void box_console_share(
    Sizing *item,
    float share)
{
    if (share <= 0) {
        *item = (Sizing){ .mode = SizeModeFixed, .value = 0 };
    } else {
        *item = (Sizing){ .mode = SizeModeGrow, .value = share };
    }
}

/** Set the size of the space next to the panel, which takes what the panel does not.
 *
 * @param world The world
 * @param e The space
 * @param share Part of the axis the panel takes
 * @param horizontal Whether the axis is the width
 */
static void box_console_space(
    ecs_world_t *world,
    ecs_entity_t e,
    float share,
    bool horizontal)
{
    LayoutItem item = {
        .width = { .mode = SizeModeFixed, .value = 0 },
        .height = { .mode = SizeModeFixed, .value = 0 }
    };
    Sizing *axis = horizontal ? &item.width : &item.height;
    if (share < 1) {
        box_console_share(axis, 1 - (share > 0 ? share : 0));
    }

    ecs_set_ptr(world, e, LayoutItem, &item);
}

/** Get the height of the panel and the width its lines wrap at, the width minus the padding.
 *
 * @param con The console state
 * @param console The Console singleton
 * @param height Receives the height of the panel
 * @return The width, 0 for a panel too narrow to hold text
 */
static float box_console_panel_size(
    const box_console_t *con,
    const Console *console,
    float *height)
{
    float share = console->width >= 1 ? 1 : (console->width > 0 ? console->width : 0);
    float width = share * con->width - 2 * console->padding;
    *height = console->height > 0 ? (console->height >= 1 ? 1 : console->height) * con->height
        : 0;

    return width > 0 ? width : 0;
}

/** Lay out the panel on the surface and set the look of the console. The rows of the
 * scrollback lines are measured again when the width they wrap at or the font size changed.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 */
static void box_console_ui_layout(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    box_console_ui_t *ui = &con->ui;
    float h, pad = console->padding;
    float wrap = box_console_panel_size(con, console, &h);
    int32_t i;
    LayoutItem item = {0};
    if (wrap != con->wrap || console->font_size != con->wrap_size) {
        con->wrap = wrap;
        con->wrap_size = console->font_size;
        for (i = 0; i < ecs_vec_count(&con->lines); i ++) {
            ecs_vec_get_t(&con->lines, box_console_line_t, i)->rows = 0;
        }
    }

    if (con->surface) {
        ecs_set(world, ui->screen, LayoutFill, { con->surface });
    } else {
        ecs_remove(world, ui->screen, LayoutFill);
        ecs_set(world, ui->screen, LayoutItem, {
            .width = { .mode = SizeModeFixed, .value = con->width },
            .height = { .mode = SizeModeFixed, .value = con->height }
        });
    }

    ecs_set(world, ui->screen, Layout, { .direction = DirectionRow });
    box_console_share(&item.width, console->width);
    item.height = (Sizing){ .mode = SizeModeGrow, .value = 1 };
    ecs_set_ptr(world, ui->stack, LayoutItem, &item);
    box_console_space(world, ui->beside, console->width, true);
    box_console_space(world, ui->below, console->height, false);
    item.width = (Sizing){ .mode = SizeModeGrow, .value = 1 };
    box_console_share(&item.height, console->height);
    ecs_set_ptr(world, ui->panel, LayoutItem, &item);
    box_console_color(world, ui->panel, (rgb){
        console->background.r, console->background.g, console->background.b });
    ecs_set(world, ui->panel, Opacity, { (float)console->background.a / 255.0f });
    if (!ecs_has(world, ui->panel, Rect)) {
        ecs_set(world, ui->panel, Rect, {0, 0});
    }

    ecs_set(world, ui->content, Layout, {
        .direction = DirectionColumn,
        .padding = { pad, pad, pad, pad },
        .h = AlignLeft
    });
    ecs_set(world, ui->content, LayoutItem, {
        .width = { .mode = SizeModeGrow, .value = 1 },
        .height = { .mode = SizeModeGrow, .value = 1 }
    });
    ecs_set(world, ui->edge, LayoutItem, {
        .width = { .mode = SizeModeFixed, .value = BOX_CONSOLE_EDGE },
        .height = { .mode = SizeModeGrow, .value = 1 },
        .h = AlignRight
    });
    box_console_color(world, ui->edge, console->border);
    ecs_set(world, ui->log, LayoutItem, {
        .width = { .mode = SizeModeGrow, .value = 1 },
        .height = { .mode = SizeModeGrow, .value = 1 }
    });
    ecs_set(world, ui->cursor, Rect, {
        console->font_size > 0 ? ceilf(console->font_size / 10) : 0, console->font_size });
    box_console_color(world, ui->cursor, console->input);
}

/** Test whether the font of the console is still loading, so text it could not measure yet is
 * measured again on a later frame. A font that failed to load is not waited for.
 *
 * @param world The world
 * @param con The console state
 * @return Whether the font loads
 */
static bool box_console_font_loading(
    const ecs_world_t *world,
    const box_console_t *con)
{
    bool alive = con->font && ecs_is_alive(world, con->font);
    const FontStatus *status = alive ? ecs_get(world, con->font, FontStatus) : NULL;

    return alive && (!status || status->state == FontLoading);
}

int32_t box_console_line_rows(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console,
    box_console_line_t *l)
{
    if (!l->rows && con->wrap <= 0) {
        l->rows = 1;
    }

    if (!l->rows) {
        Text look = box_console_look(con, console, l->text, l->hang);
        l->rows = box_text_break_lines(world, &look, NULL);
        con->pending = con->pending || (!l->rows && box_console_font_loading(world, con));
    }

    return l->rows > 0 ? l->rows : 1;
}

int32_t box_console_scrollback_rows(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&con->lines); i ++) {
        result += box_console_line_rows(world, con, console,
            ecs_vec_get_t(&con->lines, box_console_line_t, i));
    }

    return result;
}

/** Write the input line after the prompt of the session into its text, wrapped at the width of
 * the panel, with the suggestion of the history dimmed after it, and put the cursor on the caret
 * of its row and column. The prompt takes the error color while the bell of Tab rings.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 * @return The rows the input line wraps to
 */
static int32_t box_console_ui_input(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    box_console_ui_t *ui = &con->ui;
    const char *ghost = box_console_ghost(con);
    ecs_vec_t spans, head_spans;
    ecs_vec_init_t(NULL, &spans, box_console_span_t, 0);
    ecs_vec_init_t(NULL, &head_spans, box_console_span_t, 0);
    char *head = box_console_prompt(world, con, &head_spans);
    int32_t i, prompt = (int32_t)strlen(head), rows = 0;
    char *value = flecs_asprintf("%s%s%s", head, con->line, ghost ? ghost : "");
    float lh = console->line_height, x = 0, y = 0;
    TextCaret caret = {0};
    for (i = 0; i < ecs_vec_count(&con->spans); i ++) {
        box_console_span_t sp = *ecs_vec_get_t(&con->spans, box_console_span_t, i);
        sp.start += prompt;
        sp.end += prompt;
        *ecs_vec_append_t(NULL, &spans, box_console_span_t) = sp;
    }

    for (i = 0; i < ecs_vec_count(&head_spans); i ++) {
        box_console_span_t sp = *ecs_vec_get_t(&head_spans, box_console_span_t, i);
        sp.kind = con->complete.bell > 0 ? BoxOutputError : sp.kind;
        *ecs_vec_append_t(NULL, &spans, box_console_span_t) = sp;
    }

    ecs_vec_fini_t(NULL, &head_spans, box_console_span_t);

    if (ghost) {
        *ecs_vec_append_t(NULL, &spans, box_console_span_t) = (box_console_span_t){
            .start = prompt + con->length,
            .end = prompt + con->length + (int32_t)strlen(ghost),
            .kind = BoxSyntaxGhost };
    }

    box_console_text(world, ui->line, value, prompt, con, console, console->input);
    box_console_spans(world, ui->line, &spans, 0, console);
    ecs_vec_fini_t(NULL, &spans, box_console_span_t);
    ecs_os_free(value);
    ecs_os_free(head);
    rows = box_text_line(world, ui->line, 0, NULL, NULL);
    con->pending = con->pending || (!rows && box_console_font_loading(world, con));
    rows = rows > 0 ? rows : 1;
    if (box_text_caret(world, ui->line, prompt + con->cursor, &caret)) {
        const TextBounds *b = ecs_get(world, ui->line, TextBounds);
        const Rect *r = ecs_get(world, ui->cursor, Rect);
        float right = con->wrap - (r ? r->width : 0);
        x = caret.x + (b ? b->width * 0.5f : 0);
        x = con->wrap > 0 && x > right ? right : x;
        x = x > 0 ? x : 0;
        y = (float)caret.line * lh;
    }

    y += (lh - console->font_size) * 0.5f;
    const LayoutItem *cur = ecs_get(world, ui->cursor, LayoutItem);
    if (!cur || cur->x != x || cur->y != -y) {
        ecs_set(world, ui->cursor, LayoutItem, {
            .h = AlignLeft, .v = VAlignTop, .x = x, .y = -y });
    }

    const LayoutItem *item = ecs_get(world, ui->input, LayoutItem);
    float height = (float)rows * lh;
    if (!item || item->height.value != height) {
        ecs_set(world, ui->input, LayoutItem, {
            .width = { .mode = SizeModeGrow, .value = 1 },
            .height = { .mode = SizeModeFixed, .value = height }
        });
    }

    return rows;
}

/** Write the candidate list of Tab completion under the input line, like the menu of the
 * explorer terminal: the labels in the colors of their kind, the active one highlighted, wrapped
 * at the width of the panel, a dimmed "... N more" past BOX_CONSOLE_COMPLETE_VISIBLE.
 *
 * The description of the active candidate follows on its own row. A closed list takes no rows.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 * @return The rows the list wraps to
 */
static int32_t box_console_ui_menu(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    const box_console_complete_t *cc = &con->complete;
    const box_console_candidate_t *c = ecs_vec_first(&cc->candidates);
    int32_t i, rows = 0, count = cc->open ? ecs_vec_count(&cc->candidates) : 0;
    int32_t first = cc->active >= BOX_CONSOLE_COMPLETE_VISIBLE ?
        cc->active - BOX_CONSOLE_COMPLETE_VISIBLE + 1 : 0;
    int32_t last = first + BOX_CONSOLE_COMPLETE_VISIBLE < count ?
        first + BOX_CONSOLE_COMPLETE_VISIBLE : count;
    box_console_out_t out;
    char *text;
    box_console_out_init(&out);
    for (i = first; i < last; i ++) {
        if (i > first) {
            box_console_out_add(&out, BoxSpanNone, "  ", 2);
        }

        box_console_out_add(&out, i == cc->active ? BoxOutputMatch : c[i].kind, c[i].label, -1);
    }

    if (count && (count - (last - first) > 0 || cc->capped)) {
        char *more = flecs_asprintf("  ... %d%s more", count - (last - first),
            cc->capped ? "+" : "");
        box_console_out_add(&out, BoxOutputDim, more, -1);
        ecs_os_free(more);
    }

    if (count && cc->active >= 0 && c[cc->active].brief) {
        box_console_out_add(&out, BoxSpanNone, "\n", 1);
        box_console_out_add(&out, BoxOutputBrief, c[cc->active].brief, -1);
    }

    text = box_console_out_text(&out);
    box_console_text(world, con->ui.menu, text, 0, con, console, console->text);
    box_console_spans(world, con->ui.menu, &out.spans, 0, console);
    box_console_out_fini(&out);
    ecs_os_free(text);
    if (count) {
        rows = box_text_line(world, con->ui.menu, 0, NULL, NULL);
        con->pending = con->pending || (!rows && box_console_font_loading(world, con));
        rows = rows > 0 ? rows : 1;
    }

    box_console_rows_height(world, con->ui.menu, rows, console);

    return rows;
}

/** Write the visible part of the scrollback into the line elements.
 *
 * The window is the rows that fit, con->scroll rows up from the newest. Every line with a row
 * in it gets an element as high as its rows, and empty rows fill the top of the window.
 *
 * The rows of the newest line under the window move under the bottom edge of the log, which
 * clips them and the rows of the oldest line above the window.
 *
 * @param world The world
 * @param con The console state
 * @param console The Console singleton
 */
static void box_console_ui_scrollback(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    int32_t i, visible = con->rows, count = ecs_vec_count(&con->lines);
    int32_t total = box_console_scrollback_rows(world, con, console);
    int32_t max = total - visible > 0 ? total - visible : 0;
    int32_t below, last = count - 1, first, covered = 0, fill, shown;
    con->scroll = con->scroll > max ? max : con->scroll;
    con->scroll = con->scroll < 0 ? 0 : con->scroll;
    below = con->scroll;
    while (last >= 0) {
        int32_t rows = box_console_line_rows(world, con, console,
            ecs_vec_get_t(&con->lines, box_console_line_t, last));
        if (rows > below) {
            break;
        }

        below -= rows;
        last --;
    }

    for (first = last; first >= 0 && covered < visible + below; first --) {
        covered += box_console_line_rows(world, con, console,
            ecs_vec_get_t(&con->lines, box_console_line_t, first));
    }

    first ++;
    shown = visible > 0 && last >= 0 ? last - first + 1 : 0;
    covered = shown ? covered : 0;
    fill = visible > 0 && visible - covered + below > 0 ? visible - covered + below : 0;
    box_console_ui_lines(world, con, fill + shown);
    for (i = 0; i < fill + shown; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&con->ui.lines, ecs_entity_t, i)[0];
        box_console_line_t *l = i >= fill ?
            ecs_vec_get_t(&con->lines, box_console_line_t, first + i - fill) : NULL;
        const char *text = l ? l->text : "";
        rgb color = console->text;
        if (l && l->kind == BoxConsoleCommand) {
            color = console->command;
        } else if (l && l->kind == BoxConsoleError) {
            color = console->error;
        }

        box_console_text(world, e, text, l ? l->hang : 0, con, console, color);
        box_console_spans(world, e, l ? &l->spans : NULL, 0, console);
        box_console_rows_height(world, e, l ? box_console_line_rows(world, con, console, l) : 1,
            console);
    }

    const Layout *layout = ecs_get(world, con->ui.log, Layout);
    float under = shown ? -(float)below * console->line_height : 0;
    if (!layout || layout->padding.bottom != under) {
        ecs_set(world, con->ui.log, Layout, {
            .direction = DirectionColumn, .v = VAlignBottom, .padding = { .bottom = under }
        });
    }
}

void box_console_ui_sync(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    float h, lh = console->line_height;
    if (con->relayout) {
        box_console_ui_layout(world, con, console);
        con->relayout = false;
        con->refresh = true;
    }

    if (con->pending) {
        con->pending = false;
        con->refresh = true;
    }

    if (con->refresh && (!con->colored || strcmp(con->colored, con->line))) {
        box_console_syntax(world, con, con->line, &con->spans);
        ecs_os_free(con->colored);
        con->colored = ecs_os_strdup(con->line);
    }

    if (con->refresh) {
        int32_t input = box_console_ui_input(world, con, console);
        int32_t menu = box_console_ui_menu(world, con, console);
        float room;
        box_console_panel_size(con, console, &h);
        room = h - 2 * console->padding - (float)(input + menu) * lh;
        con->rows = lh > 0 && room > 0 ? (int32_t)floorf(room / lh) : 0;
        box_console_ui_scrollback(world, con, console);
        con->refresh = false;
    }
}
