/**
 * @file fps/fps.c
 * @brief Registers the box.fps module: the FpsPanel singleton and the system that opens, closes
 * and updates the fps panel, a chart of the frame samples in a scene and view of its own.
 *
 * The chart holds one bar of one point per frame sample at its right, the reference lines over
 * them and their labels in the gutter at its left.
 *
 * The bars and the text line are children of elements without a Layout, so changing them each
 * frame lays nothing out; the panel is laid out only when it opens or the surface changes.
 */

#define BOX_FPS_IMPL
#include <box.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Surface size in points the panel is placed in while the world has no Surface */
#define BOX_FPS_DEFAULT_WIDTH (1280.0f)

/** Surface height in points while the world has no Surface */
#define BOX_FPS_DEFAULT_HEIGHT (720.0f)

/** Points between the edges of the panel and its content */
#define BOX_FPS_PADDING (8.0f)

/** Points between the text line and the chart */
#define BOX_FPS_SPACING (4.0f)

/** Height of the text line in points */
#define BOX_FPS_LINE (16.0f)

/** Width of the chart in points */
#define BOX_FPS_CHART_W (BOX_FPS_WIDTH - 2 * BOX_FPS_PADDING)

/** Height of the chart in points */
#define BOX_FPS_CHART_H (BOX_FPS_HEIGHT - 2 * BOX_FPS_PADDING - BOX_FPS_LINE - BOX_FPS_SPACING)

/** Width of a bar in points. Whole points put the edges of the bars between pixels, so
 * neighboring bars meet without a seam of blended edges. */
#define BOX_FPS_BAR_W (1.0f)

/** Width of the plot, the bars, at the right of the chart in points */
#define BOX_FPS_PLOT_W (BOX_FPS_BARS * BOX_FPS_BAR_W)

/** Width of the gutter left of the plot that labels the reference lines in points */
#define BOX_FPS_GUTTER (BOX_FPS_CHART_W - BOX_FPS_PLOT_W)

/** Samples the strip of bars scrolls before its offsets start over, so they stay small */
#define BOX_FPS_REBASE (BOX_FPS_BARS * 64)

/** Steps per point the height of a bar is rounded to, so jitter below it sets nothing */
#define BOX_FPS_STEPS (4.0f)

/** Frame time of 60 fps in milliseconds */
#define BOX_FPS_60_MS (1000.0 / 60.0)

/** Frame time of 30 fps in milliseconds */
#define BOX_FPS_30_MS (1000.0 / 30.0)

/** Rate of a frame, the color of its bar */
typedef enum box_fps_rate_t {
    BoxFpsFast = 0,     /**< 60 fps or faster, green */
    BoxFpsSlow = 1,     /**< Under 60 fps, down to 30 fps, yellow */
    BoxFpsCritical = 2  /**< Under 30 fps, red */
} box_fps_rate_t;

/** Colors and sizes of the panel taken from UiTheme */
typedef struct box_fps_look_t {
    Rgb surface;        /**< Background of the panel */
    Rgb surface_alt;    /**< Background of the chart */
    Rgb border;         /**< Edge of the panel */
    Rgb text;           /**< The text line */
    Rgb text_muted;     /**< The reference lines */
    float font_size;    /**< Em size of the text line */
    float radius;       /**< Corner radius of the panel */
} box_fps_look_t;

/** State of the fps panel of a world, owned by the module */
typedef struct box_fps_t {
    ecs_entity_t scope;         /**< Root entity of the content and the view */
    ecs_entity_t view;          /**< View of the content while the panel is open */
    ecs_entity_t root;          /**< Scene root of the content */
    ecs_entity_t camera;        /**< Orthographic camera at one point per unit */
    ecs_entity_t screen;        /**< Layout root that fills the surface */
    ecs_entity_t panel;         /**< The panel, at the top right of the screen */
    ecs_entity_t label;         /**< The text line */
    ecs_entity_t chart;         /**< Background of the chart, parent of the bars */
    ecs_entity_t lines[2];      /**< Reference lines at 60 and 30 fps */
    ecs_entity_t marks[2];      /**< Labels of the reference lines in the gutter */
    ecs_entity_t strip;         /**< Parent of the bars, scrolls one bar left per sample */
    ecs_entity_t bars[BOX_FPS_BARS]; /**< Bar of each slot of the ring of FrameSamples */
    float heights[BOX_FPS_BARS];     /**< Height each bar was set to */
    float xs[BOX_FPS_BARS];          /**< x in the strip each bar was set to */
    int32_t laps[BOX_FPS_BARS];      /**< Samples written to each slot since the rebase */
    uint8_t rates[BOX_FPS_BARS];     /**< box_fps_rate_t each bar was colored with */
    int32_t next;               /**< FrameSamples.next of the last update, -1 for none */
    int32_t total;              /**< Samples written since the rebase */
    float strip_x;              /**< x the strip was set to */
    char text[64];              /**< String the text line was set to */
    box_fps_look_t look;        /**< Look the content was colored with */
    bool shown;                 /**< The content is shown */
    bool relayout;              /**< The surface changed since the screen was sized */
    ecs_entity_t surface;       /**< Surface the screen fills, 0 while the world has none */
    float width;                /**< Surface width in points the screen was sized for */
    float height;               /**< Surface height in points the screen was sized for */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
} box_fps_t;

/** Get the rate of a frame from its wall time.
 *
 * @param interval_ms The wall time in milliseconds
 * @return The rate
 */
static box_fps_rate_t box_fps_rate(
    double interval_ms)
{
    box_fps_rate_t result = BoxFpsCritical;
    if (interval_ms <= BOX_FPS_60_MS + BOX_FPS_TOLERANCE_MS) {
        result = BoxFpsFast;
    } else if (interval_ms <= BOX_FPS_30_MS) {
        result = BoxFpsSlow;
    }

    return result;
}

/** Get the color of a rate.
 *
 * @param rate The rate
 * @return The color of its bars
 */
static rgb box_fps_rate_color(
    box_fps_rate_t rate)
{
    rgb result = BOX_FPS_RED;
    if (rate == BoxFpsFast) {
        result = BOX_FPS_GREEN;
    } else if (rate == BoxFpsSlow) {
        result = BOX_FPS_YELLOW;
    }

    return result;
}

rgb box_fps_bar_color(
    double interval_ms)
{
    return box_fps_rate_color(box_fps_rate(interval_ms));
}

float box_fps_bar_height(
    double interval_ms,
    float chart_height)
{
    double ms = interval_ms < BOX_FPS_CHART_MS ? interval_ms : BOX_FPS_CHART_MS;
    float result = 0;
    if (interval_ms > 0 && chart_height > 0) {
        result = (float)(ms / BOX_FPS_CHART_MS) * chart_height;
    }

    return result;
}

/** Get the look of the panel from the UI theme, or a dark look without one.
 *
 * @param world The world
 * @param look Receives the look
 */
static void box_fps_theme(
    const ecs_world_t *world,
    box_fps_look_t *look)
{
    ecs_entity_t e = ecs_lookup(world, "box.ui.theme");
    const UiTheme *t = e ? ecs_get(world, e, UiTheme) : NULL;
    ecs_os_memset_t(look, 0, box_fps_look_t);
    if (t) {
        look->surface = t->surface;
        look->surface_alt = t->surface_alt;
        look->border = t->border;
        look->text = t->text;
        look->text_muted = t->text_muted;
        look->font_size = t->small_font_size > 0 ? t->small_font_size : t->font_size;
        look->radius = t->radius;
    } else {
        look->surface = (Rgb){ 32, 35, 41 };
        look->surface_alt = (Rgb){ 22, 24, 29 };
        look->border = (Rgb){ 60, 64, 72 };
        look->text = (Rgb){ 220, 222, 228 };
        look->text_muted = (Rgb){ 120, 125, 135 };
        look->font_size = 12;
        look->radius = 4;
    }

    look->font_size = look->font_size > 0 ? look->font_size : 12;
}

/** Get the root entity of the content and the view, creating it when needed.
 *
 * @param world The world
 * @param fps The panel state
 * @return The root entity
 */
static ecs_entity_t box_fps_scope(
    ecs_world_t *world,
    box_fps_t *fps)
{
    if (!fps->scope || !ecs_is_alive(world, fps->scope)) {
        ecs_entity_t prev = ecs_set_scope(world, 0);
        fps->scope = ecs_entity(world, { .name = BOX_FPS_SCOPE });
        ecs_set_scope(world, prev);
    }

    return fps->scope;
}

/** Create a named element of the panel content.
 *
 * @param world The world
 * @param parent The parent
 * @param name The name
 * @return The element
 */
static ecs_entity_t box_fps_element(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t result = ecs_entity(world, { .name = name, .parent = parent });
    ecs_set(world, result, Position, {0, 0, 0});

    return result;
}

/** Get the x of the center of the bar of a slot in the strip. A write moves the bar of the
 * slot one plot width to the right, past the newest bar.
 *
 * @param fps The panel state
 * @param slot The slot of the ring of FrameSamples
 * @return The x in points
 */
static float box_fps_bar_x(
    const box_fps_t *fps,
    int32_t slot)
{
    return ((float)(fps->laps[slot] * BOX_FPS_BARS + slot) + 0.5f) * BOX_FPS_BAR_W;
}

/** Get the x of the strip in the chart, whose origin is its center: one bar further left for
 * every sample written, so the newest bar is against the right edge of the chart.
 *
 * @param fps The panel state
 * @return The x in points
 */
static float box_fps_strip_x(
    const box_fps_t *fps)
{
    return BOX_FPS_CHART_W * 0.5f - (float)(fps->total + BOX_FPS_BARS) * BOX_FPS_BAR_W;
}

/** Create the strip of bars, empty, the reference lines over them and their labels in the
 * gutter. A reference line is centered in a row of points, so its 1 point covers whole pixels.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_chart_build(
    ecs_world_t *world,
    box_fps_t *fps)
{
    float bottom = -BOX_FPS_CHART_H * 0.5f;
    float left = BOX_FPS_CHART_W * 0.5f - BOX_FPS_PLOT_W;
    double refs[2] = { BOX_FPS_60_MS, BOX_FPS_30_MS };
    const char *names[2] = { "line_60", "line_30" };
    const char *marks[2] = { "mark_60", "mark_30" };
    const char *texts[2] = { "60 fps", "30 fps" };
    rgb green = BOX_FPS_GREEN;
    char name[32];
    int32_t i;
    fps->strip = ecs_entity(world, { .name = "bars", .parent = fps->chart });
    fps->strip_x = 0;
    ecs_set(world, fps->strip, Position, { fps->strip_x, bottom, 0 });
    for (i = 0; i < BOX_FPS_BARS; i ++) {
        ecs_os_snprintf(name, sizeof(name), "bar_%d", i);
        fps->bars[i] = ecs_entity(world, { .name = name, .parent = fps->strip });
        ecs_set(world, fps->bars[i], Position, { 0, 0, 0 });
        ecs_set(world, fps->bars[i], Rect, { BOX_FPS_BAR_W, 0 });
        ecs_set(world, fps->bars[i], Rgb, { green.r, green.g, green.b });
        fps->heights[i] = 0;
        fps->xs[i] = 0;
        fps->rates[i] = BoxFpsFast;
    }

    fps->next = -1;

    for (i = 0; i < 2; i ++) {
        float y = floorf(bottom + box_fps_bar_height(refs[i], BOX_FPS_CHART_H)) + 0.5f;
        fps->lines[i] = ecs_entity(world, { .name = names[i], .parent = fps->chart });
        ecs_set(world, fps->lines[i], Position, { 0, y, 0 });
        ecs_set(world, fps->lines[i], Line, { left, 0, BOX_FPS_CHART_W * 0.5f, 0, 1 });
        fps->marks[i] = ecs_entity(world, { .name = marks[i], .parent = fps->chart });
        ecs_set(world, fps->marks[i], Position, {
            -BOX_FPS_CHART_W * 0.5f + BOX_FPS_GUTTER * 0.5f, y, 0 });
        ecs_add(world, fps->marks[i], Text);
        ecs_get_mut(world, fps->marks[i], Text)->value = ecs_os_strdup(texts[i]);
        ecs_modified(world, fps->marks[i], Text);
    }
}

/** Create the scene, camera, screen, panel, text line and chart of the content.
 *
 * The text line and the bars are children of elements without a Layout, out of the layout tree,
 * and the scene has SpatialIgnore: moving bars cost no spatial update and picks pass through.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_build(
    ecs_world_t *world,
    box_fps_t *fps)
{
    float pad = BOX_FPS_PADDING;
    ecs_entity_t header;
    fps->root = ecs_entity(world, { .name = "ui", .parent = box_fps_scope(world, fps) });
    ecs_set(world, fps->root, Scene, {0});
    ecs_add(world, fps->root, SpatialIgnore);
    fps->camera = box_fps_element(world, fps->root, "camera");
    ecs_set(world, fps->camera, Position, {0, 0, 100});
    ecs_set(world, fps->camera, Camera, {
        .fov = 60, .near_ = 0.1f, .far_ = 1000, .ortho = true, .ortho_height = 10,
        .ortho_scale = 1
    });

    fps->screen = box_fps_element(world, fps->root, "screen");
    ecs_set(world, fps->screen, Layout, {
        .direction = DirectionRow,
        .padding = { BOX_FPS_MARGIN, BOX_FPS_MARGIN, BOX_FPS_MARGIN, BOX_FPS_MARGIN },
        .h = AlignRight,
        .v = VAlignTop
    });
    fps->panel = box_fps_element(world, fps->screen, "panel");
    ecs_set(world, fps->panel, Layout, {
        .direction = DirectionColumn,
        .padding = { pad, pad, pad, pad },
        .spacing = BOX_FPS_SPACING,
        .h = AlignLeft,
        .v = VAlignTop
    });
    ecs_set(world, fps->panel, LayoutItem, {
        .width = { .mode = SizeModeFixed, .value = BOX_FPS_WIDTH },
        .height = { .mode = SizeModeFixed, .value = BOX_FPS_HEIGHT }
    });
    ecs_set(world, fps->panel, Rect, { BOX_FPS_WIDTH, BOX_FPS_HEIGHT });

    header = box_fps_element(world, fps->panel, "header");
    ecs_set(world, header, LayoutItem, {
        .width = { .mode = SizeModeFixed, .value = BOX_FPS_CHART_W },
        .height = { .mode = SizeModeFixed, .value = BOX_FPS_LINE }
    });
    fps->label = box_fps_element(world, header, "label");
    ecs_add(world, fps->label, Text);

    fps->chart = box_fps_element(world, fps->panel, "chart");
    ecs_set(world, fps->chart, LayoutItem, {
        .width = { .mode = SizeModeFixed, .value = BOX_FPS_CHART_W },
        .height = { .mode = SizeModeFixed, .value = BOX_FPS_CHART_H }
    });
    ecs_set(world, fps->chart, Rect, { BOX_FPS_CHART_W, BOX_FPS_CHART_H });
    box_fps_chart_build(world, fps);
    fps->text[0] = '\0';
    ecs_os_memset_t(&fps->look, 0, box_fps_look_t);
}

/** Set the size of a text of the panel when it differs.
 *
 * @param world The world
 * @param e The entity with the Text
 * @param size The em size
 */
static void box_fps_text_size(
    ecs_world_t *world,
    ecs_entity_t e,
    float size)
{
    Text *t = ecs_get_mut(world, e, Text);
    if (t && (t->size != size || t->line_height != BOX_FPS_LINE)) {
        t->size = size;
        t->line_height = BOX_FPS_LINE;
        ecs_modified(world, e, Text);
    }
}

/** Color the panel, the chart, the text line and the reference lines from the UI theme when
 * it changed since they were colored.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_theme_sync(
    ecs_world_t *world,
    box_fps_t *fps)
{
    box_fps_look_t look;
    int32_t i;
    box_fps_theme(world, &look);
    if (!memcmp(&look, &fps->look, sizeof(look))) {
        goto done;
    }

    fps->look = look;
    ecs_set(world, fps->panel, Rgb, { look.surface.r, look.surface.g, look.surface.b });
    ecs_set(world, fps->panel, Border, {
        .width = 1,
        .radius = look.radius,
        .color = { look.border.r, look.border.g, look.border.b, 255 }
    });
    ecs_set(world, fps->chart, Rgb, {
        look.surface_alt.r, look.surface_alt.g, look.surface_alt.b });
    ecs_set(world, fps->label, Rgb, { look.text.r, look.text.g, look.text.b });
    for (i = 0; i < 2; i ++) {
        ecs_set(world, fps->lines[i], Rgb, {
            look.text_muted.r, look.text_muted.g, look.text_muted.b });
        ecs_set(world, fps->marks[i], Rgb, {
            look.text_muted.r, look.text_muted.g, look.text_muted.b });
        box_fps_text_size(world, fps->marks[i], look.font_size);
    }

    box_fps_text_size(world, fps->label, look.font_size);

done:
    return;
}

/** Write the fps, the mean of the samples as the frames command computes it, and the last
 * frame time into the text line when the string changed.
 *
 * @param world The world
 * @param fps The panel state
 * @param samples The frame samples, NULL for none
 */
static void box_fps_label_sync(
    ecs_world_t *world,
    box_fps_t *fps,
    const FrameSamples *samples)
{
    char text[64];
    double sum = 0, mean, last = samples ? box_frame_sample(samples, 0) : 0;
    int32_t i, count = samples ? samples->count : 0;
    Text *t;
    count = count < BOX_FRAME_WINDOW ? count : BOX_FRAME_WINDOW;
    count = count > 0 ? count : 0;
    for (i = 0; i < count; i ++) {
        sum += box_frame_sample(samples, i);
    }

    mean = count ? sum / (double)count : 0;
    ecs_os_snprintf(text, sizeof(text), "%.1f fps  %.1f ms", mean > 0 ? 1000.0 / mean : 0.0,
        last);
    if (!strcmp(text, fps->text)) {
        goto done;
    }

    ecs_os_strcpy(fps->text, text);
    t = ecs_get_mut(world, fps->label, Text);
    if (t) {
        ecs_os_free(t->value);
        t->value = ecs_os_strdup(text);
        ecs_modified(world, fps->label, Text);
    }

done:
    return;
}

/** Follow the ring of the samples: count the slots written since the last update, or start
 * the offsets over on the first update and after BOX_FPS_REBASE samples.
 *
 * @param fps The panel state
 * @param next FrameSamples.next, in 0..BOX_FPS_BARS - 1
 */
static void box_fps_advance(
    box_fps_t *fps,
    int32_t next)
{
    int32_t i, count;
    if (fps->next < 0 || fps->total >= BOX_FPS_REBASE) {
        fps->total = next;
        for (i = 0; i < BOX_FPS_BARS; i ++) {
            fps->laps[i] = i < next;
        }
    } else {
        count = (next - fps->next + BOX_FPS_BARS) % BOX_FPS_BARS;
        for (i = 0; i < count; i ++) {
            fps->laps[(fps->next + i) % BOX_FPS_BARS] ++;
        }

        fps->total += count;
    }

    fps->next = next;
}

/** Set the strip and the bars from the samples, the newest on the right.
 *
 * The bar of a slot draws the sample of that slot of the ring. A new sample scrolls the strip
 * one bar left and moves only the bar of its slot, to the right end, so a frame sets the strip
 * and the few bars whose slot was written, rounded height or rate changed.
 *
 * @param world The world
 * @param fps The panel state
 * @param samples The frame samples, NULL for none
 */
static void box_fps_bars_sync(
    ecs_world_t *world,
    box_fps_t *fps,
    const FrameSamples *samples)
{
    int32_t next = samples ? samples->next % BOX_FPS_BARS : 0;
    float x;
    int32_t i;
    next = next < 0 ? next + BOX_FPS_BARS : next;
    box_fps_advance(fps, next);
    x = box_fps_strip_x(fps);
    if (x != fps->strip_x) {
        fps->strip_x = x;
        ecs_set(world, fps->strip, Position, { x, -BOX_FPS_CHART_H * 0.5f, 0 });
    }

    for (i = 0; i < BOX_FPS_BARS; i ++) {
        int32_t age = (next - 1 - i + 2 * BOX_FPS_BARS) % BOX_FPS_BARS;
        double ms = samples ? box_frame_sample(samples, age) : 0;
        float h = roundf(box_fps_bar_height(ms, BOX_FPS_CHART_H) * BOX_FPS_STEPS) /
            BOX_FPS_STEPS;
        box_fps_rate_t rate = box_fps_rate(ms);
        bool resized = h != fps->heights[i];
        x = box_fps_bar_x(fps, i);
        if (resized) {
            fps->heights[i] = h;
            ecs_set(world, fps->bars[i], Rect, { BOX_FPS_BAR_W, h });
        }

        if (resized || x != fps->xs[i]) {
            fps->xs[i] = x;
            ecs_set(world, fps->bars[i], Position, { x, h * 0.5f, 0 });
        }

        if ((uint8_t)rate != fps->rates[i]) {
            rgb c = box_fps_rate_color(rate);
            fps->rates[i] = (uint8_t)rate;
            ecs_set(world, fps->bars[i], Rgb, { c.r, c.g, c.b });
        }
    }
}

/** Find the size in points of the surface the panel shows on: the first one that does not
 * render into a texture.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_measure(
    ecs_world_t *world,
    box_fps_t *fps)
{
    float w = BOX_FPS_DEFAULT_WIDTH, h = BOX_FPS_DEFAULT_HEIGHT;
    ecs_entity_t surface = 0;
    bool found = false;
    ecs_iter_t it = ecs_query_iter(world, fps->surfaces);
    int32_t i;
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count && !found; i ++) {
            if (!s[i].texture) {
                box_surface_point_size(&s[i],
                    ecs_get(world, it.entities[i], SurfaceStatus), &w, &h);
                surface = it.entities[i];
                found = true;
            }
        }
    }

    if (surface != fps->surface || w != fps->width || h != fps->height) {
        fps->surface = surface;
        fps->width = w;
        fps->height = h;
        fps->relayout = true;
    }
}

/** Make the screen fill the surface, or the default size without one.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_layout(
    ecs_world_t *world,
    box_fps_t *fps)
{
    if (fps->surface) {
        ecs_set(world, fps->screen, LayoutFill, { fps->surface });
    } else {
        ecs_remove(world, fps->screen, LayoutFill);
        ecs_set(world, fps->screen, LayoutItem, {
            .width = { .mode = SizeModeFixed, .value = fps->width },
            .height = { .mode = SizeModeFixed, .value = fps->height }
        });
    }

    fps->relayout = false;
}

/** Show the panel: build its content and create its view, a view without clear, lighting,
 * culling or effects at BOX_VIEW_ORDER_FPS.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_show(
    ecs_world_t *world,
    box_fps_t *fps)
{
    box_fps_build(world, fps);
    fps->view = ecs_entity(world, { .name = "view", .parent = box_fps_scope(world, fps) });
    ecs_set(world, fps->view, View, {
        .camera = fps->camera,
        .order = BOX_VIEW_ORDER_FPS
    });
    fps->shown = true;
    fps->relayout = true;
    fps->surface = 0;
}

/** Hide the panel: delete its view and its content, so a closed panel costs nothing.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_hide(
    ecs_world_t *world,
    box_fps_t *fps)
{
    if (fps->view && ecs_is_alive(world, fps->view)) {
        ecs_delete(world, fps->view);
    }

    if (fps->root && ecs_is_alive(world, fps->root)) {
        ecs_delete(world, fps->root);
    }

    fps->view = 0;
    fps->root = 0;
    fps->shown = false;
}

/** Open, close and update the panel from the FpsPanel singleton and the frame samples.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxFpsUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_fps_t *fps = it->ctx;
    const FpsPanel *panel = ecs_singleton_get(world, FpsPanel);
    const FrameSamples *samples;
    bool open = panel && panel->open;
    if (!open && fps->shown) {
        box_fps_hide(world, fps);
    }

    if (!open) {
        goto done;
    }

    if (fps->shown && (!ecs_is_alive(world, fps->view) || !ecs_is_alive(world, fps->root) ||
        !ecs_is_alive(world, fps->chart) || !ecs_is_alive(world, fps->label)))
    {
        box_fps_hide(world, fps);
    }

    if (!fps->shown) {
        box_fps_show(world, fps);
    }

    box_fps_measure(world, fps);
    if (fps->relayout) {
        box_fps_layout(world, fps);
    }

    samples = ecs_singleton_get(world, FrameSamples);
    box_fps_theme_sync(world, fps);
    box_fps_label_sync(world, fps, samples);
    box_fps_bars_sync(world, fps, samples);
done:
    return;
}

/** Free the panel state when the world is deleted.
 *
 * @param world The world
 * @param ctx The panel state
 */
static void box_fps_free(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    ecs_os_free(ctx);
}

void BoxFpsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxFps);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxText);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxUi);
    ECS_IMPORT(world, BoxSpatial);

    ECS_META_COMPONENT(world, FpsPanel);
    ecs_set_hooks(world, FpsPanel, { .ctor = flecs_default_ctor });

    box_fps_t *fps = ecs_os_calloc_t(box_fps_t);
    ecs_atfini(world, box_fps_free, fps);
    ecs_singleton_add(world, FpsPanel);

    fps->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "FpsSurfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FpsUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxFpsUpdate,
        .ctx = fps,
        .immediate = true
    });
}
