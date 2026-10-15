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

/** Copy the start of a string, turning its trailing spaces into no-break spaces, which text
 * layout measures, so what follows them is placed after them.
 *
 * @param value The string
 * @param size Number of bytes to copy
 * @return The copy, which the caller frees
 */
static char* box_console_hold_spaces(
    const char *value,
    int32_t size)
{
    int32_t end = size, i;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    while (end > 0 && value[end - 1] == ' ') {
        end --;
    }

    ecs_strbuf_appendstrn(&buf, value, end);
    for (i = end; i < size; i ++) {
        ecs_strbuf_appendstr(&buf, "\xC2\xA0");
    }

    char *result = ecs_strbuf_get(&buf);

    return result ? result : ecs_os_strdup("");
}

/** Set the string, size and color of a text element when they differ; console lines never wrap.
 *
 * @param world The world
 * @param e The element
 * @param value The string
 * @param size Number of bytes of the string to show
 * @param console The Console singleton
 * @param color The color
 */
static void box_console_text(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *value,
    int32_t size,
    const Console *console,
    rgb color)
{
    const Text *cur = ecs_get(world, e, Text);
    char *str = box_console_hold_spaces(value, size);
    bool same = cur && cur->value && !strcmp(cur->value, str) &&
        cur->size == console->font_size && cur->line_height == console->line_height &&
        cur->wrap == TextWrapNewlines;
    if (!same) {
        if (!cur) {
            ecs_add(world, e, Text);
        }

        Text *t = ecs_get_mut(world, e, Text);
        ecs_os_free(t->value);
        t->value = str;
        str = NULL;
        t->size = console->font_size;
        t->line_height = console->line_height;
        t->wrap = TextWrapNewlines;
        ecs_modified(world, e, Text);
    }

    ecs_os_free(str);
    box_console_color(world, e, color);
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
    ui->edge = box_console_element(world, ui->panel, "edge");
    ecs_set(world, ui->edge, Rect, {0, 0});
    ui->log = box_console_element(world, ui->content, "log");
    ecs_set(world, ui->log, Layout, { .direction = DirectionColumn, .v = VAlignBottom });
    ui->input = box_console_element(world, ui->content, "input");
    ecs_set(world, ui->input, Layout, { .direction = DirectionRow, .v = VAlignMiddle });
    ui->prompt = box_console_text_element(world, ui->input, "prompt");
    ui->before = box_console_text_element(world, ui->input, "before");
    ui->cursor = box_console_element(world, ui->input, "cursor");
    ui->after = box_console_text_element(world, ui->input, "after");
done:
    return;
}

/** Make the number of scrollback line elements match the lines that fit in the panel.
 *
 * @param world The world
 * @param con The console state
 * @param count Lines that fit
 * @param console The Console singleton
 */
static void box_console_ui_lines(
    ecs_world_t *world,
    box_console_t *con,
    int32_t count,
    const Console *console)
{
    box_console_ui_t *ui = &con->ui;
    int32_t i;
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

    for (i = 0; i < count; i ++) {
        ecs_set(world, ecs_vec_get_t(&ui->lines, ecs_entity_t, i)[0], LayoutItem, {
            .height = { .mode = SizeModeFixed, .value = console->line_height }
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

/** Lay out the panel on the surface and set the look of the console.
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
    float h = console->height > 0 ? console->height * con->height : 0;
    float pad = console->padding, lh = console->line_height;
    float room = h - 2 * pad - lh;
    int32_t lines = lh > 0 && room > 0 ? (int32_t)floorf(room / lh) : 0;
    LayoutItem item = {0};
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
    ecs_set(world, ui->input, LayoutItem, {
        .width = { .mode = SizeModeGrow, .value = 1 },
        .height = { .mode = SizeModeFixed, .value = lh }
    });
    ecs_set(world, ui->cursor, Rect, {
        console->font_size > 0 ? ceilf(console->font_size / 10) : 0, console->font_size });
    box_console_color(world, ui->cursor, console->input);
    box_console_ui_lines(world, con, lines, console);
}

/** Write the visible part of the scrollback into the line elements.
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
    int32_t i, visible = ecs_vec_count(&con->ui.lines), count = ecs_vec_count(&con->lines);
    int32_t max = count - visible > 0 ? count - visible : 0;
    con->scroll = con->scroll > max ? max : con->scroll;
    con->scroll = con->scroll < 0 ? 0 : con->scroll;
    for (i = 0; i < visible; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&con->ui.lines, ecs_entity_t, i)[0];
        int32_t at = count - con->scroll - visible + i;
        const box_console_line_t *l = at >= 0 ?
            ecs_vec_get_t(&con->lines, box_console_line_t, at) : NULL;
        const char *text = l ? l->text : "";
        rgb color = console->text;
        if (l && l->kind == BoxConsoleCommand) {
            color = console->command;
        } else if (l && l->kind == BoxConsoleError) {
            color = console->error;
        }

        box_console_text(world, e, text, (int32_t)strlen(text), console, color);
    }
}

void box_console_ui_sync(
    ecs_world_t *world,
    box_console_t *con,
    const Console *console)
{
    box_console_ui_t *ui = &con->ui;
    const char *prompt = BOX_CONSOLE_PROMPT;
    if (con->relayout) {
        box_console_ui_layout(world, con, console);
        con->relayout = false;
        con->refresh = true;
    }

    if (con->refresh) {
        box_console_ui_scrollback(world, con, console);
        box_console_text(world, ui->prompt, prompt, (int32_t)strlen(prompt), console,
            console->input);
        box_console_text(world, ui->before, con->line, con->cursor, console, console->input);
        box_console_text(world, ui->after, &con->line[con->cursor],
            con->length - con->cursor, console, console->input);
        con->refresh = false;
    }
}
