/**
 * @file FpsPanel.c
 * @brief The fps panel: the fps command and the FpsPanel singleton open and close it, opening
 * creates its view and the content of etc/fps/fps.flecs, and the bars, reference lines and
 * summary follow fixed frame samples.
 */

#include <box.h>
#include <bake_test.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** Path of the Text of the mean and worst frame time in the summary */
#define FPS_TIMES BOX_FPS_SUMMARY ".times"

/** Path of the Text of the fps in the summary */
#define FPS_RATE BOX_FPS_SUMMARY ".rate"

/** Create a world with box and no surface whose frame samples stay as the test writes them.
 *
 * @return The world
 */
static ecs_world_t* fps_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    samples->hold = true;
    ecs_singleton_modified(world, FrameSamples);

    return world;
}

/** Set FpsPanel.open and run frames until the panel followed it.
 *
 * @param world The world
 * @param open Whether the panel is open
 */
static void fps_open(
    ecs_world_t *world,
    bool open)
{
    FpsPanel *p = ecs_singleton_get_mut(world, FpsPanel);
    p->open = open;
    ecs_singleton_modified(world, FpsPanel);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Replace the frame samples by fixed ones, the last the newest, and run frames until the
 * panel followed them.
 *
 * @param world The world
 * @param values The intervals in milliseconds, oldest first
 * @param count Number of intervals
 */
static void fps_samples(
    ecs_world_t *world,
    const double *values,
    int32_t count)
{
    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    int32_t i;
    ecs_os_memset_n(samples->interval, 0, double, BOX_FRAME_WINDOW);
    samples->count = 0;
    samples->next = 0;
    samples->hold = true;
    for (i = 0; i < count; i ++) {
        box_frame_samples_add(samples, values[i]);
    }

    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Fill the frame samples with one interval and run frames until the panel followed them.
 *
 * @param world The world
 * @param ms The interval in milliseconds
 * @param count Number of samples
 */
static void fps_samples_all(
    ecs_world_t *world,
    double ms,
    int32_t count)
{
    double values[BOX_FRAME_WINDOW];
    int32_t i;
    for (i = 0; i < count; i ++) {
        values[i] = ms;
    }

    fps_samples(world, values, count);
}

/** Run a command line in a session and check that it succeeds with an output.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param expect The output it must print, NULL to only free it
 */
static void fps_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *expect)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    if (expect) {
        test_str(out, expect);
    }

    ecs_os_free(out);
}

/** Check that the panel view exists and draws the panel scene as an overlay.
 *
 * @param world The world
 * @return The view
 */
static ecs_entity_t fps_check_view(
    ecs_world_t *world)
{
    ecs_entity_t view = ecs_lookup(world, BOX_FPS_VIEW);
    test_assert(view != 0);
    const View *v = ecs_get(world, view, View);
    test_assert(v != NULL);
    test_int(v->order, BOX_VIEW_ORDER_FPS);
    test_assert(v->camera == ecs_lookup(world, BOX_FPS_UI ".camera"));
    test_bool(v->clear.enabled, false);
    test_bool(v->lighting.enabled, false);
    test_bool(v->shadows.enabled, false);
    test_bool(v->culling.enabled, false);
    test_int(ecs_vec_count(&v->effects), 0);
    test_assert(ecs_has(world, ecs_lookup(world, BOX_FPS_UI), Scene));

    return view;
}

/** Get a bar of the chart.
 *
 * @param world The world
 * @param i The bar, 0 for the oldest sample, BOX_FPS_BARS - 1 for the newest
 * @return The bar
 */
static ecs_entity_t fps_bar(
    ecs_world_t *world,
    int32_t i)
{
    char path[128];
    snprintf(path, sizeof(path), "%s.bar_%d", BOX_FPS_CHART, i);
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the height a bar of a frame time has: linear in the time, BOX_FPS_CHART_MS at the top
 * of the chart, rounded to a whole point, at least as high as the bar is wide.
 *
 * @param ms The frame time in milliseconds, 0 for no sample
 * @return The height in points
 */
static float fps_height(
    double ms)
{
    float result = 0;
    if (ms > 0) {
        double share = ms / BOX_FPS_CHART_MS < 1 ? ms / BOX_FPS_CHART_MS : 1;
        result = roundf((float)share * BOX_FPS_CHART_HEIGHT);
        result = result > BOX_FPS_BAR_WIDTH ? result : BOX_FPS_BAR_WIDTH;
    }

    return result;
}

/** Check the color of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param color The color it must have
 */
static void fps_check_color(
    ecs_world_t *world,
    ecs_entity_t e,
    rgb color)
{
    const Rgb *c = ecs_get(world, e, Rgb);
    test_assert(c != NULL);
    test_int(c->r, color.r);
    test_int(c->g, color.g);
    test_int(c->b, color.b);
}

/** Check a bar: its place in the chart, its height for a frame time and its color.
 *
 * @param world The world
 * @param i The bar
 * @param ms The frame time it draws, 0 for no sample
 * @param color The color it must have
 */
static void fps_check_bar(
    ecs_world_t *world,
    int32_t i,
    double ms,
    rgb color)
{
    ecs_entity_t bar = fps_bar(world, i);
    float h = fps_height(ms);
    const Rect *r = ecs_get(world, bar, Rect);
    const Position *p = ecs_get(world, bar, Position);
    test_assert(r != NULL);
    test_assert(p != NULL);
    test_flt(r->width, BOX_FPS_BAR_WIDTH);
    test_flt(r->height, h);
    test_flt(p->x, -BOX_FPS_CHART_WIDTH * 0.5f + (float)(i + 1) * BOX_FPS_BAR_SLOT -
        BOX_FPS_BAR_WIDTH * 0.5f);
    test_flt(p->y, -BOX_FPS_CHART_HEIGHT * 0.5f + h * 0.5f);
    fps_check_color(world, bar, color);
}

/** Get the Text of an element of the panel.
 *
 * @param world The world
 * @param path Path of the element
 * @return The string of its Text
 */
static const char* fps_text(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t e = ecs_lookup(world, path);
    test_assert(e != 0);
    const Text *t = ecs_get(world, e, Text);
    test_assert(t != NULL);
    test_assert(t->value != NULL);

    return t->value;
}

/** Get the UI theme.
 *
 * @param world The world
 * @return The theme
 */
static const UiTheme* fps_theme(
    ecs_world_t *world)
{
    const UiTheme *result = ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    test_assert(result != NULL);

    return result;
}

/** Count the children of an entity.
 *
 * @param world The world
 * @param parent The parent
 * @return The number of children
 */
static int32_t fps_children(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        result += it.count;
    }

    return result;
}

void FpsPanel_closed_by_default(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    ecs_progress(world, 0);

    test_bool(ecs_singleton_get(world, FpsPanel)->open, false);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_UI) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_SCRIPT) == 0);

    ecs_fini(world);
}

void FpsPanel_open_creates_view(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_check_view(world);

    ecs_entity_t script = ecs_lookup(world, BOX_FPS_SCRIPT);
    test_assert(script != 0);
    test_assert(ecs_get(world, script, EcsScript)->error == NULL);
    ecs_entity_t tmpl = ecs_lookup(world, BOX_FPS_TEMPLATE);
    test_assert(tmpl != 0);
    ecs_entity_t panel = ecs_lookup(world, BOX_FPS_PANEL);
    test_assert(panel != 0);
    test_assert(ecs_has_id(world, panel, tmpl));
    test_assert(ecs_has(world, panel, Rect));
    test_str(fps_text(world, BOX_FPS_HEADING), "Frame rate");
    test_assert(ecs_lookup(world, BOX_FPS_CHART) != 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART ".line_60") != 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART ".line_30") != 0);
    test_assert(ecs_lookup(world, FPS_TIMES) != 0);
    test_assert(ecs_lookup(world, FPS_RATE) != 0);
    fps_bar(world, 0);
    fps_bar(world, BOX_FPS_BARS - 1);

    ecs_fini(world);
}

void FpsPanel_close_removes_view(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_check_view(world);

    fps_open(world, false);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_UI) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_SCRIPT) != 0);

    ecs_fini(world);
}

void FpsPanel_reopen(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    ecs_entity_t script = ecs_lookup(world, BOX_FPS_SCRIPT);
    fps_open(world, false);
    fps_open(world, true);
    fps_check_view(world);
    test_uint(ecs_lookup(world, BOX_FPS_SCRIPT), script);
    fps_bar(world, BOX_FPS_BARS - 1);
    test_str(fps_text(world, BOX_FPS_HEADING), "Frame rate");

    fps_open(world, false);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);

    ecs_fini(world);
}

void FpsPanel_open_from_script(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    test_int(ecs_script_run(world, "fps",
        "using box.fps\n"
        "$ {\n"
        "  FpsPanel: {open: true}\n"
        "}\n", NULL), 0);
    test_bool(ecs_singleton_get(world, FpsPanel)->open, true);
    ecs_progress(world, 0);
    fps_check_view(world);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    fps_run(world, s, "fps", "false");
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);

    ecs_fini(world);
}

void FpsPanel_command_toggles(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    fps_run(world, s, "fps", "true");
    test_bool(ecs_singleton_get(world, FpsPanel)->open, true);
    ecs_progress(world, 0);
    fps_check_view(world);

    fps_run(world, s, "fps", "false");
    test_bool(ecs_singleton_get(world, FpsPanel)->open, false);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);

    fps_run(world, s, "fps", "true");
    ecs_progress(world, 0);
    fps_check_view(world);

    ecs_fini(world);
}

void FpsPanel_command_rate_keeps_panel(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    fps_run(world, s, "fps 50", "cap 50 fps\n");
    test_flt(ecs_singleton_get(world, App)->fps, 50);
    test_bool(ecs_singleton_get(world, FpsPanel)->open, false);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);

    fps_run(world, s, "fps", "true");
    ecs_progress(world, 0);
    fps_run(world, s, "fps 0", "no cap\n");
    test_flt(ecs_singleton_get(world, App)->fps, 0);
    test_bool(ecs_singleton_get(world, FpsPanel)->open, true);
    ecs_progress(world, 0);
    fps_check_view(world);

    ecs_fini(world);
}

void FpsPanel_help_brief(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, s, "help fps", NULL);
    test_assert(out != NULL);
    test_assert(strstr(out, "fps panel") != NULL);
    test_assert(strstr(out, "toggle the panel") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void FpsPanel_view_order(void) {
    // fps/panel
    test_assert(BOX_VIEW_ORDER_EDITOR < BOX_VIEW_ORDER_FPS);
    test_assert(BOX_VIEW_ORDER_FPS < BOX_VIEW_ORDER_CONSOLE);
}

void FpsPanel_top_right(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    const UiTheme *t = fps_theme(world);
    ecs_entity_t panel = ecs_lookup(world, BOX_FPS_PANEL);
    const LayoutBounds *b = ecs_get(world, panel, LayoutBounds);
    test_assert(b != NULL);
    test_flt(b->width, BOX_FPS_CHART_WIDTH + 2 * t->padding);
    test_assert(b->height > BOX_FPS_CHART_HEIGHT + 2 * t->padding);
    float w = b->width, h = b->height;
    const Position *p = ecs_get(world, panel, Position);
    test_flt(p->x, 1280 * 0.5f - BOX_FPS_MARGIN - w * 0.5f);
    test_flt(p->y, 720 * 0.5f - BOX_FPS_MARGIN - h * 0.5f);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360});
    ecs_progress(world, 0);
    ecs_set(world, s, SurfaceStatus, {
        .width = 800, .height = 500, .point_width = 800, .point_height = 500,
        .render_width = 800, .render_height = 500, .window = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    p = ecs_get(world, panel, Position);
    test_flt(p->x, 800 * 0.5f - BOX_FPS_MARGIN - w * 0.5f);
    test_flt(p->y, 500 * 0.5f - BOX_FPS_MARGIN - h * 0.5f);

    ecs_fini(world);
}

void FpsPanel_no_input(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    ecs_entity_t panel = ecs_lookup(world, BOX_FPS_PANEL);
    test_assert(!ecs_has(world, panel, InputBlocker));
    test_assert(!ecs_has(world, panel, Interactive));
    test_assert(!ecs_has(world, panel, Focusable));
    test_assert(!ecs_has(world, panel, KeyCapture));
    test_assert(!ecs_has(world, ecs_lookup(world, BOX_FPS_CHART), InputBlocker));
    test_assert(!ecs_has(world, fps_bar(world, 0), Interactive));
    test_assert(!ecs_has(world, ecs_lookup(world, FPS_RATE), Interactive));
    test_assert(ecs_has(world, ecs_lookup(world, BOX_FPS_UI), SpatialIgnore));

    ecs_fini(world);
}

void FpsPanel_panel_theme(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    const UiTheme *t = fps_theme(world);
    ecs_entity_t panel = ecs_lookup(world, BOX_FPS_PANEL);
    fps_check_color(world, panel, (rgb){ t->surface.r, t->surface.g, t->surface.b });
    const Border *border = ecs_get(world, panel, Border);
    test_assert(border != NULL);
    test_flt(border->width, 1);
    test_flt(border->radius, t->radius);
    test_int(border->color.r, t->border.r);
    test_int(border->color.g, t->border.g);
    test_int(border->color.b, t->border.b);
    test_int(border->color.a, 255);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL ".backsplash") == 0);
    test_int(fps_children(world, ecs_lookup(world, BOX_FPS_PANEL)), 3);

    ecs_fini(world);
}

void FpsPanel_exports_colors(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    const char *names[] = { "box.fps.green", "box.fps.yellow", "box.fps.red" };
    rgb colors[] = { BOX_FPS_GREEN, BOX_FPS_YELLOW, BOX_FPS_RED };
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_lookup(world, names[i]);
        test_assert(e != 0);
        ecs_value_t v = ecs_const_var_get(world, e);
        test_uint(v.type, ecs_id(Rgb));
        const Rgb *c = v.ptr;
        test_int(c->r, colors[i].r);
        test_int(c->g, colors[i].g);
        test_int(c->b, colors[i].b);
    }

    ecs_value_t fast = ecs_const_var_get(world, ecs_lookup(world, "box.fps.fast_ms"));
    ecs_value_t slow = ecs_const_var_get(world, ecs_lookup(world, "box.fps.slow_ms"));
    test_assert(fast.ptr != NULL);
    test_assert(slow.ptr != NULL);
    test_flt(*(double*)fast.ptr, 1000.0 / 60.0 + BOX_FPS_TOLERANCE_MS);
    test_flt(*(double*)slow.ptr, 1000.0 / 30.0);

    ecs_fini(world);
}

void FpsPanel_bars_count_and_order(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    ecs_entity_t chart = ecs_lookup(world, BOX_FPS_CHART);
    test_int(fps_children(world, chart), BOX_FPS_BARS + 2);
    char path[128];
    snprintf(path, sizeof(path), "%s.bar_%d", BOX_FPS_CHART, BOX_FPS_BARS);
    test_assert(ecs_lookup(world, path) == 0);

    const LayoutBounds *b = ecs_get(world, chart, LayoutBounds);
    test_assert(b != NULL);
    test_flt(b->width, BOX_FPS_CHART_WIDTH);
    test_flt(b->height, BOX_FPS_CHART_HEIGHT);

    ecs_entity_t bar_tmpl = ecs_lookup(world, BOX_FPS_BAR);
    test_assert(bar_tmpl != 0);
    float prev = -BOX_FPS_CHART_WIDTH;
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        test_assert(ecs_has_id(world, fps_bar(world, i), bar_tmpl));
        const Position *p = ecs_get(world, fps_bar(world, i), Position);
        test_assert(p != NULL);
        if (i) {
            test_flt(p->x - prev, BOX_FPS_BAR_SLOT);
        }

        prev = p->x;
    }

    const Position *newest = ecs_get(world, fps_bar(world, BOX_FPS_BARS - 1), Position);
    test_flt(newest->x + BOX_FPS_BAR_WIDTH * 0.5f, BOX_FPS_CHART_WIDTH * 0.5f);
    const Position *oldest = ecs_get(world, fps_bar(world, 0), Position);
    test_flt(oldest->x - BOX_FPS_BAR_WIDTH * 0.5f,
        -BOX_FPS_CHART_WIDTH * 0.5f + BOX_FPS_BAR_SLOT - BOX_FPS_BAR_WIDTH);

    ecs_fini(world);
}

void FpsPanel_bars_newest_right(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    double values[] = { 10.0, 20.0, 40.0 };
    fps_samples(world, values, 3);
    fps_check_bar(world, BOX_FPS_BARS - 3, 10.0, BOX_FPS_GREEN);
    fps_check_bar(world, BOX_FPS_BARS - 2, 20.0, BOX_FPS_YELLOW);
    fps_check_bar(world, BOX_FPS_BARS - 1, 40.0, BOX_FPS_RED);
    fps_check_bar(world, BOX_FPS_BARS - 4, 0, BOX_FPS_GREEN);
    fps_check_bar(world, 0, 0, BOX_FPS_GREEN);

    ecs_fini(world);
}

void FpsPanel_bars_heights(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    double values[] = { 0.5, 2.0, 10.0, 25.0, 40.0, 49.0, 50.0, 80.0, 400.0 };
    int32_t count = sizeof(values) / sizeof(values[0]);
    fps_samples(world, values, count);
    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t bar = fps_bar(world, BOX_FPS_BARS - count + i);
        test_flt(ecs_get(world, bar, Rect)->height, fps_height(values[i]));
    }

    test_flt(fps_height(0.5), BOX_FPS_BAR_WIDTH);
    test_flt(fps_height(10.0), 12);
    test_flt(fps_height(25.0), 30);
    test_flt(fps_height(400.0), BOX_FPS_CHART_HEIGHT);
    ecs_entity_t hitch = fps_bar(world, BOX_FPS_BARS - 1);
    test_flt(ecs_get(world, hitch, Rect)->height, BOX_FPS_CHART_HEIGHT);
    test_flt(ecs_get(world, hitch, Position)->y, 0);

    ecs_fini(world);
}

void FpsPanel_bars_colors(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    double values[] = {
        8.0, 1000.0 / 60.0, 1000.0 / 60.0 + 0.3, 1000.0 / 58.0, 1000.0 / 45.0,
        1000.0 / 30.0, 1000.0 / 29.0, 250.0
    };
    rgb colors[] = {
        BOX_FPS_GREEN, BOX_FPS_GREEN, BOX_FPS_GREEN, BOX_FPS_YELLOW, BOX_FPS_YELLOW,
        BOX_FPS_YELLOW, BOX_FPS_RED, BOX_FPS_RED
    };
    int32_t count = sizeof(values) / sizeof(values[0]);
    fps_samples(world, values, count);
    for (int32_t i = 0; i < count; i ++) {
        fps_check_bar(world, BOX_FPS_BARS - count + i, values[i], colors[i]);
    }

    ecs_fini(world);
}

void FpsPanel_bars_full_window(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    double values[BOX_FRAME_WINDOW];
    for (int32_t i = 0; i < BOX_FRAME_WINDOW; i ++) {
        values[i] = 1.0 + (double)(i % 45);
    }

    fps_samples(world, values, BOX_FRAME_WINDOW);
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        double ms = values[BOX_FRAME_WINDOW - BOX_FPS_BARS + i];
        rgb c = ms > BOX_FPS_SLOW_MS ? BOX_FPS_RED
            : (ms > BOX_FPS_FAST_MS ? BOX_FPS_YELLOW : BOX_FPS_GREEN);
        fps_check_bar(world, i, ms, c);
    }

    ecs_fini(world);
}

void FpsPanel_bars_follow_new_sample(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_samples_all(world, 10.0, BOX_FRAME_WINDOW);

    ecs_entity_t chart = ecs_lookup(world, BOX_FPS_CHART);
    ecs_entity_t bars[BOX_FPS_BARS];
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        bars[i] = fps_bar(world, i);
    }

    for (int32_t f = 0; f < 10; f ++) {
        FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
        box_frame_samples_add(samples, f % 2 ? 12.0 : 45.0);
        ecs_singleton_modified(world, FrameSamples);
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        test_uint(fps_bar(world, i), bars[i]);
    }

    fps_check_bar(world, BOX_FPS_BARS - 1, 12.0, BOX_FPS_GREEN);
    fps_check_bar(world, BOX_FPS_BARS - 2, 45.0, BOX_FPS_RED);
    fps_check_bar(world, BOX_FPS_BARS - 10, 45.0, BOX_FPS_RED);
    fps_check_bar(world, BOX_FPS_BARS - 11, 10.0, BOX_FPS_GREEN);
    test_int(fps_children(world, chart), BOX_FPS_BARS + 2);

    ecs_fini(world);
}

void FpsPanel_bars_not_laid_out(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    ecs_entity_t chart = ecs_lookup(world, BOX_FPS_CHART);
    test_assert(!ecs_has(world, chart, Layout));
    test_assert(ecs_has(world, chart, LayoutItem));
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        ecs_entity_t bar = fps_bar(world, i);
        test_assert(!ecs_has(world, bar, Layout));
        test_assert(!ecs_has(world, bar, LayoutItem));
        test_assert(!ecs_has(world, bar, LayoutBounds));
    }

    ecs_fini(world);
}

/** Count the bars of the chart whose Rect was set in the int32_t of the observer context.
 *
 * @param it Iterator of the observer
 */
static void fps_rect_on_set(
    ecs_iter_t *it)
{
    int32_t *fired = it->ctx;
    ecs_entity_t chart = ecs_lookup(it->world, BOX_FPS_CHART);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (ecs_get_parent(it->world, it->entities[i]) == chart) {
            (*fired) ++;
        }
    }
}

void FpsPanel_bars_set_when_changed(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_samples_all(world, 10.0, BOX_FRAME_WINDOW);

    int32_t fired = 0;
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Rect) }},
        .events = { EcsOnSet },
        .callback = fps_rect_on_set,
        .ctx = &fired
    });

    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    box_frame_samples_add(samples, 10.0);
    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    test_int(fired, 0);

    samples = ecs_singleton_get_mut(world, FrameSamples);
    box_frame_samples_add(samples, 10.2);
    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    test_int(fired, 0);

    samples = ecs_singleton_get_mut(world, FrameSamples);
    box_frame_samples_add(samples, 45.0);
    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    test_int(fired, 1);
    fps_check_bar(world, BOX_FPS_BARS - 1, 45.0, BOX_FPS_RED);

    fired = 0;
    samples = ecs_singleton_get_mut(world, FrameSamples);
    box_frame_samples_add(samples, 10.0);
    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    test_int(fired, 2);
    fps_check_bar(world, BOX_FPS_BARS - 2, 45.0, BOX_FPS_RED);
    fps_check_bar(world, BOX_FPS_BARS - 1, 10.0, BOX_FPS_GREEN);

    ecs_fini(world);
}

void FpsPanel_reference_lines(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    const UiTheme *t = fps_theme(world);
    const char *names[] = { BOX_FPS_CHART ".line_60", BOX_FPS_CHART ".line_30" };
    double ms[] = { 1000.0 / 60.0, 1000.0 / 30.0 };
    for (int32_t i = 0; i < 2; i ++) {
        ecs_entity_t line = ecs_lookup(world, names[i]);
        test_assert(line != 0);
        const Line *l = ecs_get(world, line, Line);
        test_assert(l != NULL);
        test_flt(l->x1, -BOX_FPS_CHART_WIDTH * 0.5f);
        test_flt(l->x2, BOX_FPS_CHART_WIDTH * 0.5f);
        test_flt(l->y1, 0);
        test_flt(l->y2, 0);
        test_flt(l->width, 1);
        float y = -BOX_FPS_CHART_HEIGHT * 0.5f +
            (float)(ms[i] / BOX_FPS_CHART_MS) * BOX_FPS_CHART_HEIGHT;
        test_flt(ecs_get(world, line, Position)->y, floorf(y) + 0.5f);
        fps_check_color(world, line, (rgb){ t->text_muted.r, t->text_muted.g, t->text_muted.b });
        const Opacity *o = ecs_get(world, line, Opacity);
        test_assert(o != NULL);
        test_assert(o->value > 0);
        test_assert(o->value < 1);
    }

    test_assert(ecs_get(world, ecs_lookup(world, names[0]), Position)->y <
        ecs_get(world, ecs_lookup(world, names[1]), Position)->y);

    ecs_fini(world);
}

void FpsPanel_summary_text(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_samples_all(world, 10.0, BOX_FRAME_WINDOW);

    const UiTheme *t = fps_theme(world);
    test_str(fps_text(world, FPS_TIMES), "mean 10.0 ms, worst 10.0 ms");
    test_str(fps_text(world, FPS_RATE), "100 fps");
    fps_check_color(world, ecs_lookup(world, FPS_TIMES),
        (rgb){ t->text_muted.r, t->text_muted.g, t->text_muted.b });
    fps_check_color(world, ecs_lookup(world, FPS_RATE), BOX_FPS_GREEN);
    const Text *rate = ecs_get(world, ecs_lookup(world, FPS_RATE), Text);
    test_int(rate->align, AlignRight);

    const FpsSummary *sum = ecs_singleton_get(world, FpsSummary);
    test_int(sum->frames, 100);
    test_flt(sum->mean_ms, 10);
    test_flt(sum->worst_ms, 10);
    test_flt(sum->fps, 100);

    ecs_fini(world);
}

void FpsPanel_summary_colors(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    fps_samples_all(world, 20.0, 100);
    test_str(fps_text(world, FPS_TIMES), "mean 20.0 ms, worst 20.0 ms");
    test_str(fps_text(world, FPS_RATE), "50 fps");
    fps_check_color(world, ecs_lookup(world, FPS_RATE), BOX_FPS_YELLOW);

    fps_samples_all(world, 40.0, 100);
    test_str(fps_text(world, FPS_TIMES), "mean 40.0 ms, worst 40.0 ms");
    test_str(fps_text(world, FPS_RATE), "25 fps");
    fps_check_color(world, ecs_lookup(world, FPS_RATE), BOX_FPS_RED);

    fps_samples_all(world, 1000.0 / 60.0, 100);
    test_str(fps_text(world, FPS_RATE), "60 fps");
    fps_check_color(world, ecs_lookup(world, FPS_RATE), BOX_FPS_GREEN);

    ecs_fini(world);
}

void FpsPanel_summary_last_second(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    double values[BOX_FRAME_WINDOW];
    int32_t i, count = 0;
    values[count ++] = 40.0;
    for (i = 0; i < 120; i ++) {
        values[count ++] = 10.0;
    }

    fps_samples(world, values, count);
    test_str(fps_text(world, FPS_TIMES), "mean 10.0 ms, worst 10.0 ms");
    test_str(fps_text(world, FPS_RATE), "100 fps");

    values[count ++] = 40.0;
    fps_samples(world, values, count);
    const FpsSummary *sum = ecs_singleton_get(world, FpsSummary);
    test_int(sum->frames, 97);
    test_flt(sum->worst_ms, 40);
    test_str(fps_text(world, FPS_TIMES), "mean 10.3 ms, worst 40.0 ms");
    test_str(fps_text(world, FPS_RATE), "97 fps");
    fps_check_color(world, ecs_lookup(world, FPS_RATE), BOX_FPS_GREEN);

    ecs_fini(world);
}

void FpsPanel_summary_no_samples(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_samples(world, NULL, 0);

    const FpsSummary *sum = ecs_singleton_get(world, FpsSummary);
    test_int(sum->frames, 0);
    test_flt(sum->mean_ms, 0);
    test_flt(sum->worst_ms, 0);
    test_flt(sum->fps, 0);
    test_str(fps_text(world, FPS_TIMES), "mean 0.0 ms, worst 0.0 ms");
    test_str(fps_text(world, FPS_RATE), "0 fps");
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        test_flt(ecs_get(world, fps_bar(world, i), Rect)->height, 0);
    }

    ecs_fini(world);
}

void FpsPanel_summarize(void) {
    // fps/panel
    FrameSamples samples = {0};
    FpsSummary sum;
    box_fps_summarize(&samples, &sum);
    test_int(sum.frames, 0);
    test_flt(sum.fps, 0);
    box_fps_summarize(NULL, &sum);
    test_int(sum.frames, 0);

    box_frame_samples_add(&samples, 30.0);
    box_frame_samples_add(&samples, 10.0);
    box_fps_summarize(&samples, &sum);
    test_int(sum.frames, 2);
    test_flt(sum.mean_ms, 20);
    test_flt(sum.worst_ms, 30);
    test_flt(sum.fps, 50);

    for (int32_t i = 0; i < BOX_FRAME_WINDOW; i ++) {
        box_frame_samples_add(&samples, 2.0);
    }

    box_fps_summarize(&samples, &sum);
    test_int(sum.frames, BOX_FRAME_WINDOW);
    test_flt(sum.mean_ms, 2);
    test_flt(sum.fps, 500);

    for (int32_t i = 0; i < 9; i ++) {
        box_frame_samples_add(&samples, 250.0);
    }

    box_fps_summarize(&samples, &sum);
    test_int(sum.frames, 4);
    test_flt(sum.worst_ms, 250);
    test_flt(sum.fps, 4);
}

void FpsPanel_summary_only_while_open(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_samples_all(world, 20.0, 50);
    test_int(ecs_singleton_get(world, FpsSummary)->frames, 0);

    fps_open(world, true);
    test_int(ecs_singleton_get(world, FpsSummary)->frames, 50);
    test_flt(ecs_singleton_get(world, FpsSummary)->mean_ms, 20);

    fps_samples_all(world, 10.0, 50);
    test_flt(ecs_singleton_get(world, FpsSummary)->mean_ms, 10);

    fps_open(world, false);
    fps_samples_all(world, 40.0, 50);
    test_flt(ecs_singleton_get(world, FpsSummary)->mean_ms, 10);

    ecs_fini(world);
}

/** Read the fps script of the repository from the working directory or an ancestor of it.
 *
 * @param fonts Receives the absolute path of etc/fonts next to it, PATH_MAX bytes
 * @return The code, NULL when it is not found
 */
static char* fps_read_script(
    char *fonts)
{
    char path[512] = BOX_FPS_SCRIPT_PATH;
    char *result = NULL;
    int i;
    for (i = 0; i < 8 && !result; i ++) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            result = ecs_os_malloc(size + 1);
            test_int((long)fread(result, 1, size, f), size);
            result[size] = '\0';
            fclose(f);
            snprintf(path + strlen(path) - strlen("fps/fps.flecs"),
                sizeof(path) - strlen(path), "fonts");
            test_assert(realpath(path, fonts) != NULL);
        } else {
            char prev[512];
            ecs_os_strcpy(prev, path);
            snprintf(path, sizeof(path), "../%s", prev);
        }
    }

    return result;
}

/** Write the fps script with the given heading to etc/fps/fps.flecs of the working directory,
 * a copy of the script of the repository with another default title.
 *
 * @param src The script of the repository
 * @param title The default title
 */
static void fps_write_script(
    const char *src,
    const char *title)
{
    char *code = ecs_os_strdup(src);
    char *at = strstr(code, "\"Frame rate\"");
    test_assert(at != NULL);
    *at = '\0';
    FILE *f = fopen("etc/fps/fps.flecs", "w");
    test_assert(f != NULL);
    fprintf(f, "%s\"%s\"%s", code, title, at + strlen("\"Frame rate\""));
    fclose(f);
    ecs_os_free(code);
}

void FpsPanel_script_reload(void) {
    // fps/panel
    char cwd[4096], dir[] = "/tmp/box_fps_XXXXXX";
    test_assert(getcwd(cwd, sizeof(cwd)) != NULL);
    ecs_world_t *world = fps_world();
    ecs_singleton_set(world, FileWatch, { .interval = 0 });
    char fonts[PATH_MAX];
    char *src = fps_read_script(fonts);
    test_assert(src != NULL);

    test_assert(mkdtemp(dir) != NULL);
    test_int(chdir(dir), 0);
    test_int(mkdir("etc", 0700), 0);
    test_int(mkdir("etc/fps", 0700), 0);
    test_int(symlink(fonts, "etc/fonts"), 0);
    fps_write_script(src, "Before");

    fps_open(world, true);
    test_str(fps_text(world, BOX_FPS_HEADING), "Before");

    fps_write_script(src, "After reload");
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_str(fps_text(world, BOX_FPS_HEADING), "After reload");
    test_assert(ecs_get(world, ecs_lookup(world, BOX_FPS_SCRIPT), EcsScript)->error == NULL);
    fps_check_view(world);
    fps_bar(world, BOX_FPS_BARS - 1);

    fps_open(world, false);
    fps_write_script(src, "Closed");
    ecs_progress(world, 0);
    fps_open(world, true);
    ecs_progress(world, 0);
    test_str(fps_text(world, BOX_FPS_HEADING), "Closed");

    ecs_fini(world);
    ecs_os_free(src);
    test_int(remove("etc/fps/fps.flecs"), 0);
    test_int(rmdir("etc/fps"), 0);
    test_int(unlink("etc/fonts"), 0);
    test_int(rmdir("etc"), 0);
    test_int(chdir(cwd), 0);
    test_int(rmdir(dir), 0);
}

void FpsPanel_script_missing(void) {
    // fps/panel
    char cwd[4096], dir[] = "/tmp/box_fps_XXXXXX";
    test_assert(getcwd(cwd, sizeof(cwd)) != NULL);
    ecs_world_t *world = fps_world();
    char fonts[PATH_MAX];
    char *src = fps_read_script(fonts);
    test_assert(src != NULL);

    test_assert(mkdtemp(dir) != NULL);
    test_int(chdir(dir), 0);
    test_int(mkdir("etc", 0700), 0);
    test_int(symlink(fonts, "etc/fonts"), 0);

    int32_t level = ecs_log_set_level(-3);
    fps_open(world, true);
    ecs_log_set_level(level);
    fps_check_view(world);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) != 0);
    test_assert(ecs_lookup(world, BOX_FPS_SCRIPT) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART) == 0);

    test_int(mkdir("etc/fps", 0700), 0);
    fps_write_script(src, "Found");
    fps_open(world, false);
    fps_open(world, true);
    test_assert(ecs_lookup(world, BOX_FPS_SCRIPT) != 0);
    test_str(fps_text(world, BOX_FPS_HEADING), "Found");

    ecs_fini(world);
    ecs_os_free(src);
    test_int(remove("etc/fps/fps.flecs"), 0);
    test_int(rmdir("etc/fps"), 0);
    test_int(unlink("etc/fonts"), 0);
    test_int(rmdir("etc"), 0);
    test_int(chdir(cwd), 0);
    test_int(rmdir(dir), 0);
}

void FpsPanel_samples_ring(void) {
    // fps/panel
    FrameSamples samples = {0};
    test_flt(box_frame_sample(&samples, 0), 0);
    for (int32_t i = 1; i <= BOX_FRAME_WINDOW + 10; i ++) {
        box_frame_samples_add(&samples, (double)i);
    }

    test_int(samples.count, BOX_FRAME_WINDOW);
    test_flt(box_frame_sample(&samples, 0), BOX_FRAME_WINDOW + 10);
    test_flt(box_frame_sample(&samples, 1), BOX_FRAME_WINDOW + 9);
    test_flt(box_frame_sample(&samples, BOX_FRAME_WINDOW - 1), 11);
    test_flt(box_frame_sample(&samples, BOX_FRAME_WINDOW), 0);
    test_flt(box_frame_sample(&samples, -1), 0);
}

void FpsPanel_engine_samples_open(void) {
    // fps/panel
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FpsPanel, { .open = true });
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    const FrameSamples *samples = ecs_singleton_get(world, FrameSamples);
    test_assert(samples->count > 0);
    test_bool(samples->hold, false);
    double ms = box_frame_sample(samples, 0);
    ecs_entity_t bar = fps_bar(world, BOX_FPS_BARS - 1);
    test_flt(ecs_get(world, bar, Rect)->height, fps_height(ms));
    test_flt(ecs_get(world, fps_bar(world, BOX_FPS_BARS - samples->count), Rect)->height,
        fps_height(box_frame_sample(samples, samples->count - 1)));
    test_flt(ecs_get(world, fps_bar(world, BOX_FPS_BARS - samples->count - 1), Rect)->height, 0);
    test_int(ecs_singleton_get(world, FpsSummary)->frames, samples->count);

    ecs_fini(world);
}

void FpsPanel_engine_records_samples(void) {
    // fps/panel
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const FrameSamples *samples = ecs_singleton_get(world, FrameSamples);
    test_assert(samples != NULL);
    test_assert(samples->count > 0);
    test_assert(samples->count <= 3);

    FrameSamples *held = ecs_singleton_get_mut(world, FrameSamples);
    held->hold = true;
    held->count = 0;
    held->next = 0;
    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FrameSamples)->count, 0);

    ecs_fini(world);
}

/** Count the OnSet events of FrameSamples in the int32_t of the observer context. The term has
 * a fixed source, so it->count is 0 and each call is one event.
 *
 * @param it Iterator of the observer
 */
static void fps_samples_on_set(
    ecs_iter_t *it)
{
    int32_t *fired = it->ctx;
    (*fired) ++;
}

/** Observe OnSet of FrameSamples.
 *
 * @param world The world
 * @param fired The counter of events
 */
static void fps_observe_samples(
    ecs_world_t *world,
    int32_t *fired)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FrameSamples), .src.id = ecs_id(FrameSamples) }},
        .events = { EcsOnSet },
        .callback = fps_samples_on_set,
        .ctx = fired
    });
}

void FpsPanel_engine_notifies_samples(void) {
    // fps/panel
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t fired = 0;
    fps_observe_samples(world, &fired);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const FrameSamples *samples = ecs_singleton_get(world, FrameSamples);
    test_assert(samples->count > 0);
    test_int(fired, samples->count);

    int32_t before = fired;
    int32_t count = samples->count;
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, FrameSamples)->count, count + 1);
    test_int(fired, before + 1);

    ecs_fini(world);
}

void FpsPanel_engine_hold_no_notify(void) {
    // fps/panel
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    int32_t fired = 0;
    fps_observe_samples(world, &fired);
    FrameSamples *held = ecs_singleton_get_mut(world, FrameSamples);
    held->hold = true;
    held->count = 0;
    held->next = 0;
    ecs_singleton_modified(world, FrameSamples);
    test_int(fired, 1);

    fired = 0;
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    test_int(fired, 0);
    test_int(ecs_singleton_get(world, FrameSamples)->count, 0);

    ecs_fini(world);
}

