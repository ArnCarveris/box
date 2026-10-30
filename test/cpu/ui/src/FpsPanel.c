/**
 * @file FpsPanel.c
 * @brief The fps panel: the fps command and the FpsPanel singleton open and close it, opening
 * creates its view and closing deletes it, and the bars follow fixed frame samples.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

/** Get a bar of the chart by the age of its sample: bar_N draws the slot N of the ring.
 *
 * @param world The world
 * @param i The bar, 0 for the oldest, BOX_FPS_BARS - 1 for the newest
 * @return The bar
 */
static ecs_entity_t fps_bar(
    ecs_world_t *world,
    int32_t i)
{
    char path[64];
    int32_t next = ecs_singleton_get(world, FrameSamples)->next;
    int32_t slot = (next + i) % BOX_FPS_BARS;
    snprintf(path, sizeof(path), "%s.bars.bar_%d", BOX_FPS_CHART, slot);
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Check whether two colors are the same.
 *
 * @param a The first color
 * @param b The second color
 * @return Whether every channel is equal
 */
static bool fps_same(
    rgb a,
    rgb b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/** Check the color of a bar entity.
 *
 * @param world The world
 * @param bar The bar
 * @param color The color it must have
 */
static void fps_check_bar_color(
    ecs_world_t *world,
    ecs_entity_t bar,
    rgb color)
{
    const Rgb *c = ecs_get(world, bar, Rgb);
    test_assert(c != NULL);
    test_int(c->r, color.r);
    test_int(c->g, color.g);
    test_int(c->b, color.b);
}

void FpsPanel_closed_by_default(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    ecs_progress(world, 0);

    test_bool(ecs_singleton_get(world, FpsPanel)->open, false);
    test_assert(ecs_lookup(world, BOX_FPS_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_FPS_UI) == 0);

    ecs_fini(world);
}

void FpsPanel_open_creates_view(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_check_view(world);

    ecs_entity_t panel = ecs_lookup(world, BOX_FPS_PANEL);
    test_assert(panel != 0);
    test_assert(ecs_has(world, panel, Rect));
    test_assert(ecs_lookup(world, BOX_FPS_LABEL) != 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART) != 0);
    fps_bar(world, 0);
    fps_bar(world, BOX_FPS_BARS - 1);
    test_assert(ecs_lookup(world, BOX_FPS_CHART ".line_60") != 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART ".line_30") != 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART ".mark_60") != 0);
    test_assert(ecs_lookup(world, BOX_FPS_CHART ".mark_30") != 0);

    const Rect *chart = ecs_get(world, ecs_lookup(world, BOX_FPS_CHART), Rect);
    ecs_entity_t newest = fps_bar(world, BOX_FPS_BARS - 1);
    test_flt(ecs_get(world, newest, Rect)->width, 1);
    ecs_entity_t strip = ecs_lookup(world, BOX_FPS_CHART ".bars");
    test_flt(ecs_get(world, strip, Position)->x + ecs_get(world, newest, Position)->x,
        chart->width * 0.5f - 0.5f);

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

    ecs_fini(world);
}

void FpsPanel_reopen(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);
    fps_open(world, false);
    fps_open(world, true);
    fps_check_view(world);
    fps_bar(world, BOX_FPS_BARS - 1);

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

    ecs_entity_t panel = ecs_lookup(world, BOX_FPS_PANEL);
    const LayoutBounds *b = ecs_get(world, panel, LayoutBounds);
    test_assert(b != NULL);
    test_flt(b->width, BOX_FPS_WIDTH);
    test_flt(b->height, BOX_FPS_HEIGHT);
    const Position *p = ecs_get(world, panel, Position);
    test_flt(p->x, 1280 * 0.5f - BOX_FPS_MARGIN - BOX_FPS_WIDTH * 0.5f);
    test_flt(p->y, 720 * 0.5f - BOX_FPS_MARGIN - BOX_FPS_HEIGHT * 0.5f);

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
    test_flt(p->x, 800 * 0.5f - BOX_FPS_MARGIN - BOX_FPS_WIDTH * 0.5f);
    test_flt(p->y, 500 * 0.5f - BOX_FPS_MARGIN - BOX_FPS_HEIGHT * 0.5f);

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
    test_assert(ecs_has(world, ecs_lookup(world, BOX_FPS_UI), SpatialIgnore));

    ecs_fini(world);
}

void FpsPanel_bar_colors(void) {
    // fps/panel
    test_assert(fps_same(box_fps_bar_color(8.0), BOX_FPS_GREEN));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 60.0), BOX_FPS_GREEN));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 60.0 - 0.3), BOX_FPS_GREEN));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 60.0 + 0.3), BOX_FPS_GREEN));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 58.0), BOX_FPS_YELLOW));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 45.0), BOX_FPS_YELLOW));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 31.0), BOX_FPS_YELLOW));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 30.0), BOX_FPS_YELLOW));
    test_assert(fps_same(box_fps_bar_color(1000.0 / 29.0), BOX_FPS_RED));
    test_assert(fps_same(box_fps_bar_color(250.0), BOX_FPS_RED));
    test_assert(!fps_same(BOX_FPS_GREEN, BOX_FPS_YELLOW));
    test_assert(!fps_same(BOX_FPS_YELLOW, BOX_FPS_RED));
}

void FpsPanel_bar_height_clamp(void) {
    // fps/panel
    test_flt(box_fps_bar_height(0, 80), 0);
    test_flt(box_fps_bar_height(BOX_FPS_CHART_MS * 0.25, 80), 20);
    test_flt(box_fps_bar_height(BOX_FPS_CHART_MS * 0.5, 80), 40);
    test_flt(box_fps_bar_height(BOX_FPS_CHART_MS, 80), 80);
    test_flt(box_fps_bar_height(BOX_FPS_CHART_MS * 2, 80), 80);
    test_flt(box_fps_bar_height(1000, 80), 80);
    test_assert(box_fps_bar_height(1000.0 / 30.0, 80) < 80);
    test_assert(box_fps_bar_height(1000.0 / 60.0, 80) <
        box_fps_bar_height(1000.0 / 30.0, 80));
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

void FpsPanel_bars_follow_samples(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    double values[] = { 10.0, 1000.0 / 60.0 + 0.2, 1000.0 / 50.0, 1000.0 / 25.0, 400.0 };
    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    for (int32_t i = 0; i < 5; i ++) {
        box_frame_samples_add(samples, values[i]);
    }

    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);

    rgb colors[] = { BOX_FPS_GREEN, BOX_FPS_GREEN, BOX_FPS_YELLOW, BOX_FPS_RED, BOX_FPS_RED };
    float top = 0;
    for (int32_t i = 0; i < 5; i ++) {
        ecs_entity_t bar = fps_bar(world, BOX_FPS_BARS - 5 + i);
        fps_check_bar_color(world, bar, colors[i]);
        const Rect *r = ecs_get(world, bar, Rect);
        test_assert(r != NULL);
        test_assert(r->height > 0);
        test_assert(r->height >= top || i == 4);
        top = r->height;
    }

    const Rect *slow = ecs_get(world, fps_bar(world, BOX_FPS_BARS - 2), Rect);
    const Rect *hitch = ecs_get(world, fps_bar(world, BOX_FPS_BARS - 1), Rect);
    test_flt(hitch->height, slow->height);
    test_flt(ecs_get(world, fps_bar(world, 0), Rect)->height, 0);

    const Text *t = ecs_get(world, ecs_lookup(world, BOX_FPS_LABEL), Text);
    test_assert(t != NULL && t->value != NULL);
    test_assert(strstr(t->value, " fps") != NULL);
    test_assert(strstr(t->value, "400.0 ms") != NULL);

    ecs_fini(world);
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

void FpsPanel_bars_reused(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    ecs_entity_t chart = ecs_lookup(world, BOX_FPS_CHART);
    ecs_entity_t strip = ecs_lookup(world, BOX_FPS_CHART ".bars");
    ecs_entity_t bars[BOX_FPS_BARS];
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        bars[i] = fps_bar(world, i);
    }

    ecs_entity_t label = ecs_lookup(world, BOX_FPS_LABEL);
    test_assert(strip != 0);
    test_int(fps_children(world, chart), 5);
    test_int(fps_children(world, strip), BOX_FPS_BARS);
    for (int32_t f = 0; f < 10; f ++) {
        FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
        box_frame_samples_add(samples, f % 2 ? 12.0 : 45.0);
        ecs_singleton_modified(world, FrameSamples);
        ecs_progress(world, 0);
    }

    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        test_assert(fps_bar(world, i) == bars[(i + 10) % BOX_FPS_BARS]);
    }

    test_assert(ecs_lookup(world, BOX_FPS_LABEL) == label);
    fps_check_bar_color(world, fps_bar(world, BOX_FPS_BARS - 1), BOX_FPS_GREEN);
    fps_check_bar_color(world, fps_bar(world, BOX_FPS_BARS - 2), BOX_FPS_RED);
    test_int(fps_children(world, chart), 5);
    test_int(fps_children(world, strip), BOX_FPS_BARS);
    test_assert(!ecs_has(world, ecs_get_parent(world, label), Layout));
    test_assert(!ecs_has(world, chart, Layout));

    ecs_fini(world);
}

void FpsPanel_new_sample_moves_one_bar(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    for (int32_t i = 0; i < BOX_FPS_BARS + 7; i ++) {
        box_frame_samples_add(samples, 1000.0 / 60.0);
    }

    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);

    ecs_entity_t strip = ecs_lookup(world, BOX_FPS_CHART ".bars");
    float strip_x = ecs_get(world, strip, Position)->x;
    Position before[BOX_FPS_BARS];
    for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
        before[i] = *ecs_get(world, fps_bar(world, i), Position);
    }

    samples = ecs_singleton_get_mut(world, FrameSamples);
    box_frame_samples_add(samples, 1000.0 / 60.0);
    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, strip, Position)->x, strip_x - 1);
    for (int32_t i = 1; i < BOX_FPS_BARS; i ++) {
        const Position *p = ecs_get(world, fps_bar(world, i - 1), Position);
        test_flt(p->x, before[i].x);
        test_flt(p->y, before[i].y);
    }

    const Rect *chart = ecs_get(world, ecs_lookup(world, BOX_FPS_CHART), Rect);
    ecs_entity_t newest = fps_bar(world, BOX_FPS_BARS - 1);
    test_flt(ecs_get(world, strip, Position)->x + ecs_get(world, newest, Position)->x,
        chart->width * 0.5f - 0.5f);

    ecs_fini(world);
}

void FpsPanel_long_run(void) {
    // fps/panel
    ecs_world_t *world = fps_world();
    fps_open(world, true);

    ecs_entity_t strip = ecs_lookup(world, BOX_FPS_CHART ".bars");
    const Rect *chart = ecs_get(world, ecs_lookup(world, BOX_FPS_CHART), Rect);
    float right = chart->width * 0.5f;
    for (int32_t f = 0; f < 100; f ++) {
        FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
        for (int32_t i = 0; i < BOX_FPS_BARS - 1; i ++) {
            box_frame_samples_add(samples, i == BOX_FPS_BARS - 2 ? 45.0 : 10.0);
        }

        ecs_singleton_modified(world, FrameSamples);
        ecs_progress(world, 0);

        float x = ecs_get(world, strip, Position)->x;
        test_assert(fabsf(x) < 1000 * BOX_FPS_BARS);
        for (int32_t i = 0; i < BOX_FPS_BARS; i ++) {
            const Position *p = ecs_get(world, fps_bar(world, i), Position);
            test_flt(x + p->x, right - (float)(BOX_FPS_BARS - i) + 0.5f);
        }

        fps_check_bar_color(world, fps_bar(world, BOX_FPS_BARS - 1), BOX_FPS_RED);
        fps_check_bar_color(world, fps_bar(world, BOX_FPS_BARS - 2), BOX_FPS_GREEN);
    }

    ecs_fini(world);
}
