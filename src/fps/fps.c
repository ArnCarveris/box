/**
 * @file fps/fps.c
 * @brief Registers the box.fps module: the FpsPanel, LoadPanel and GpuPanel singletons, the
 * summaries of the last second of frame and thread samples, the renderer stats and the system
 * that opens and closes the panels, in a scene and view of their own, each an instance of a
 * template of etc/fps/fps.flecs.
 *
 * The script draws everything the panels show. C creates the scene, camera and view, places the
 * panels at the top right of the surface, loads the script and loads it again when it changes.
 *
 * C provides the data: the summaries, the time and column of every bar of a chart and the first
 * column of their parent, which rotate the bars so a new sample writes one bar and the parent,
 * written only when they changed, and the colors and limits of the bars.
 */

#define BOX_FPS_IMPL
#include <box.h>
#include "load.h"
#include "gpu.h"
#include "../app/watch.h"
#include "../render/preprocess.h"

/** Surface size in points the panel is placed in while the world has no Surface */
#define BOX_FPS_DEFAULT_WIDTH (1280.0f)

/** Surface height in points while the world has no Surface */
#define BOX_FPS_DEFAULT_HEIGHT (720.0f)

/** The bars of a chart, which show a series of samples, and what was written to them */
typedef struct box_fps_chart_t {
    const char *path;           /**< Path of the parent of the bars */
    ecs_entity_t bars[BOX_FPS_BARS];    /**< Bars of the chart by index, bar_0 first */
    double bar_ms[BOX_FPS_BARS];        /**< Sample last written to each bar */
    int32_t bar_col[BOX_FPS_BARS];      /**< Column last written to each bar */
    int32_t head;               /**< Index of the bar of the newest sample */
    ecs_entity_t bar_mut;       /**< Mut variables of the bar template, BOX_FPS_BAR_MUT */
    ecs_size_t bar_size;        /**< Size of the bar mut variables */
    ecs_size_t bar_ms_offset;   /**< Offset of ms in the bar mut variables */
    ecs_size_t bar_col_offset;  /**< Offset of col in the bar mut variables */
    ecs_entity_t parent;        /**< Parent of the bars */
    ecs_entity_t parent_mut;    /**< Mut variables of the parent template, BOX_FPS_BARS_MUT */
    int32_t first;              /**< First column last written to the parent */
    bool ready;                 /**< The bars, their parent and what was written are current */
} box_fps_chart_t;

/** A panel in the screen: the fps, load or GPU stats panel */
typedef struct box_fps_panel_t {
    const char *name;           /**< Name of the panel in the screen */
    const char *tmpl;           /**< Path of the template of the script it instantiates */
    ecs_entity_t entity;        /**< The panel while it is shown, else 0 */
    int32_t chart_count;        /**< Charts of the panel */
    box_fps_chart_t charts[BOX_LOAD_ROWS]; /**< Its charts, in the order of their samples */
} box_fps_panel_t;

/** State of the panels of a world, owned by the module */
typedef struct box_fps_t {
    ecs_entity_t scope;         /**< Root entity of the content and the view */
    ecs_entity_t view;          /**< View of the content while a panel is open */
    ecs_entity_t root;          /**< Scene root of the content */
    ecs_entity_t camera;        /**< Orthographic camera at one point per unit */
    ecs_entity_t screen;        /**< Layout root that fills the surface */
    box_fps_panel_t fps;        /**< The fps panel */
    box_fps_panel_t load;       /**< The load panel */
    box_fps_panel_t gpu;        /**< The GPU stats panel, which has no charts */
    ecs_entity_t script;        /**< Managed script of etc/fps/fps.flecs, 0 before it loads */
    char *path;                 /**< Path the script was found at, NULL before it loads */
    int32_t watch;              /**< Id of the file watch kind of the script, 0 without box.app */
    bool reload;                /**< The script changed on disk since the last update */
    bool missing;               /**< The script was not found since a panel opened */
    bool shown;                 /**< The content is shown */
    bool relayout;              /**< The surface changed since the screen was sized */
    ecs_entity_t surface;       /**< Surface the screen fills, 0 while the world has none */
    float width;                /**< Surface width in points the screen was sized for */
    float height;               /**< Surface height in points the screen was sized for */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
} box_fps_t;

/** Paths of the parents of the bars of the load panel, in the order of LoadSamples */
static const char *box_load_bars[BOX_LOAD_ROWS] = {
    BOX_LOAD_MAIN ".chart.bars",
    BOX_LOAD_RENDER ".chart.bars",
    BOX_LOAD_GPU ".chart.bars",
    BOX_LOAD_AUDIO ".chart.bars",
    BOX_LOAD_JOBS ".chart.bars"
};

void box_fps_summarize(
    const FrameSamples *samples,
    FpsSummary *out)
{
    double sum = 0, worst = 0;
    int32_t age, frames = 0;
    for (age = 0; samples && age < BOX_FRAME_WINDOW && sum < BOX_FPS_SUMMARY_MS; age ++) {
        double ms = box_frame_sample(samples, age);
        if (ms <= 0) {
            break;
        }

        sum += ms;
        worst = ms > worst ? ms : worst;
        frames ++;
    }

    out->frames = frames;
    out->mean_ms = frames ? sum / (double)frames : 0;
    out->worst_ms = worst;
    out->fps = sum > 0 ? 1000.0 * (double)frames / sum : 0;
}

/** Write the summary of the frame samples to FpsSummary when it differs from it.
 *
 * @param world The world
 */
static void box_fps_summary_sync(
    ecs_world_t *world)
{
    FpsSummary sum;
    const FpsSummary *cur = ecs_singleton_get(world, FpsSummary);
    box_fps_summarize(ecs_singleton_get(world, FrameSamples), &sum);
    if (!cur || cur->frames != sum.frames || cur->mean_ms != sum.mean_ms ||
        cur->worst_ms != sum.worst_ms || cur->fps != sum.fps)
    {
        ecs_singleton_set_ptr(world, FpsSummary, &sum);
    }
}

/** Largest size of the bar mut variables, which C fills on the stack */
#define BOX_FPS_BAR_MUT_MAX (64)

/** Find a member of the struct of mut variables.
 *
 * @param world The world
 * @param mut The mut variables
 * @param count Number of members the struct must have
 * @param name Name of the member
 * @param type Type of the member
 * @return The member, NULL when the struct has another number of members or not this one
 */
static const ecs_member_t* box_fps_member(
    const ecs_world_t *world,
    ecs_entity_t mut,
    int32_t count,
    const char *name,
    ecs_entity_t type)
{
    const EcsStruct *st = mut ? ecs_get(world, mut, EcsStruct) : NULL;
    const ecs_member_t *result = NULL;
    int32_t i;
    if (!st || ecs_vec_count(&st->members) != count) {
        goto done;
    }

    for (i = 0; i < count && !result; i ++) {
        const ecs_member_t *m = ecs_vec_get_t(&st->members, ecs_member_t, i);
        if (!ecs_os_strcmp(m->name, name) && m->type == type) {
            result = m;
        }
    }

done:
    return result;
}

/** Find the bars, their parent and the mut variables of the bar and parent templates.
 *
 * The bar mut variables must be ms, an f64, and col, an i32, and the parent mut variables first,
 * an i32, alone, since C sets them as a whole: a mut has an on_replace hook, so it cannot be
 * changed in place.
 *
 * @param world The world
 * @param chart The chart
 * @return Whether the script created the bars and their parent, which have the mut variables
 */
static bool box_fps_bars_resolve(
    ecs_world_t *world,
    box_fps_chart_t *chart)
{
    ecs_entity_t mut = ecs_lookup(world, BOX_FPS_BAR_MUT);
    ecs_entity_t parent_mut = ecs_lookup(world, BOX_FPS_BARS_MUT);
    ecs_entity_t parent = ecs_lookup(world, chart->path);
    const ecs_member_t *ms = box_fps_member(world, mut, 2, "ms", ecs_id(ecs_f64_t));
    const ecs_member_t *col = box_fps_member(world, mut, 2, "col", ecs_id(ecs_i32_t));
    const ecs_member_t *first = box_fps_member(world, parent_mut, 1, "first",
        ecs_id(ecs_i32_t));
    const ecs_type_info_t *ti = ms ? ecs_get_type_info(world, mut) : NULL;
    bool result = false;
    char name[32];
    int32_t i;
    if (!ms || !col || !first || !ti || ti->size > BOX_FPS_BAR_MUT_MAX || !parent ||
        !ecs_has_id(world, parent, parent_mut))
    {
        goto done;
    }

    for (i = 0; i < BOX_FPS_BARS; i ++) {
        ecs_os_snprintf(name, sizeof(name), "bar_%d", i);
        chart->bars[i] = ecs_lookup_child(world, parent, name);
        if (!chart->bars[i] || !ecs_has_id(world, chart->bars[i], mut)) {
            goto done;
        }
    }

    chart->bar_mut = mut;
    chart->bar_size = ti->size;
    chart->bar_ms_offset = ms->offset;
    chart->bar_col_offset = col->offset;
    chart->parent = parent;
    chart->parent_mut = parent_mut;
    result = true;

done:
    return result;
}

/** Whether the bars that were found are still those of the content.
 *
 * @param world The world
 * @param chart The chart
 * @return Whether the bars and the mut variables of the bar template are alive
 */
static bool box_fps_bars_valid(
    const ecs_world_t *world,
    const box_fps_chart_t *chart)
{
    return chart->ready && ecs_is_alive(world, chart->bar_mut) &&
        ecs_is_alive(world, chart->parent_mut) && ecs_is_alive(world, chart->parent) &&
        ecs_is_alive(world, chart->bars[0]) &&
        ecs_is_alive(world, chart->bars[BOX_FPS_BARS - 1]);
}

/** Get the bar that shows a sample.
 *
 * @param chart The chart
 * @param age Age of the sample, 0 for the newest, at most BOX_FPS_BARS - 1
 * @return Index of the bar
 */
static int32_t box_fps_bar_of(
    const box_fps_chart_t *chart,
    int32_t age)
{
    return (chart->head - age + BOX_FPS_BARS) % BOX_FPS_BARS;
}

/** Write the sample and column of a bar to its mut variables.
 *
 * @param world The world
 * @param chart The chart
 * @param i Index of the bar
 * @param ms The sample in milliseconds, 0 without a sample
 * @param col The column
 */
static void box_fps_bar_write(
    ecs_world_t *world,
    box_fps_chart_t *chart,
    int32_t i,
    double ms,
    int32_t col)
{
    char value[BOX_FPS_BAR_MUT_MAX];
    ecs_os_memset(value, 0, chart->bar_size);
    ecs_os_memcpy(value + chart->bar_ms_offset, &ms, sizeof(double));
    ecs_os_memcpy(value + chart->bar_col_offset, &col, sizeof(int32_t));
    ecs_set_id(world, chart->bars[i], chart->bar_mut, chart->bar_size, value);
    chart->bar_ms[i] = ms;
    chart->bar_col[i] = col;
}

/** Write the first column to the mut variables of the parent of the bars.
 *
 * @param world The world
 * @param chart The chart
 * @param first The column of the bar at the left of the chart
 */
static void box_fps_first_write(
    ecs_world_t *world,
    box_fps_chart_t *chart,
    int32_t first)
{
    ecs_set_id(world, chart->parent, chart->parent_mut, sizeof(int32_t), &first);
    chart->first = first;
}

/** Make the bars of a chart show a series of samples, writing only what changed.
 *
 * Samples the bars show already write nothing. Those shifted by one, a new sample, go to the bar
 * of the oldest sample with the column right of the newest bar, and the parent moves one slot
 * left, so a new sample evaluates one bar and the parent, whatever the samples are.
 *
 * Otherwise, and when first would reach BOX_FPS_BARS, bar_i gets the sample of age BOX_FPS_BARS
 * - 1 - i and column i and the parent first 0, so columns stay below 2 * BOX_FPS_BARS. Every bar
 * whose sample or column changed is written, every bar when the bars were found again.
 *
 * @param world The world
 * @param chart The chart
 * @param samples The samples, NULL for none
 */
static void box_fps_bars_sync(
    ecs_world_t *world,
    box_fps_chart_t *chart,
    const FrameSamples *samples)
{
    double ms[BOX_FPS_BARS];
    bool all = false, same = true, shifted;
    int32_t age, i;
    if (!box_fps_bars_valid(world, chart)) {
        chart->ready = box_fps_bars_resolve(world, chart);
        all = true;
    }

    if (!chart->ready) {
        goto done;
    }

    shifted = !all && chart->first + 1 < BOX_FPS_BARS;
    for (age = 0; age < BOX_FPS_BARS; age ++) {
        ms[age] = samples ? box_frame_sample(samples, age) : 0;
        same = same && ms[age] == chart->bar_ms[box_fps_bar_of(chart, age)];
        shifted = shifted && (!age || ms[age] == chart->bar_ms[box_fps_bar_of(chart, age - 1)]);
    }

    if (!all && same) {
        goto done;
    }

    if (shifted) {
        i = box_fps_bar_of(chart, BOX_FPS_BARS - 1);
        box_fps_bar_write(world, chart, i, ms[0], chart->bar_col[chart->head] + 1);
        chart->head = i;
        box_fps_first_write(world, chart, chart->first + 1);
        goto done;
    }

    for (i = 0; i < BOX_FPS_BARS; i ++) {
        double bar_ms = ms[BOX_FPS_BARS - 1 - i];
        if (all || bar_ms != chart->bar_ms[i] || i != chart->bar_col[i]) {
            box_fps_bar_write(world, chart, i, bar_ms, i);
        }
    }

    chart->head = BOX_FPS_BARS - 1;
    if (all || chart->first) {
        box_fps_first_write(world, chart, 0);
    }

done:
    return;
}

/** Make the charts of a panel show their samples, the frame samples for the fps panel and the
 * series of LoadSamples for the load panel.
 *
 * @param world The world
 * @param fps The panel state
 * @param panel The panel
 * @param stale Only the charts whose bars were not found or are gone
 */
static void box_fps_panel_bars_sync(
    ecs_world_t *world,
    box_fps_t *fps,
    box_fps_panel_t *panel,
    bool stale)
{
    const LoadSamples *load = ecs_singleton_get(world, LoadSamples);
    const FrameSamples *series[BOX_LOAD_ROWS] = {0};
    int32_t i;
    if (panel == &fps->fps) {
        series[0] = ecs_singleton_get(world, FrameSamples);
    } else if (load) {
        series[0] = &load->main;
        series[1] = &load->render;
        series[2] = &load->gpu;
        series[3] = &load->audio;
        series[4] = &load->jobs;
    }

    for (i = 0; panel->entity && i < panel->chart_count; i ++) {
        if (!stale || !box_fps_bars_valid(world, &panel->charts[i])) {
            box_fps_bars_sync(world, &panel->charts[i], series[i]);
        }
    }
}

/** Forget what was written to the charts of a panel, so they are found and written again.
 *
 * @param panel The panel
 */
static void box_fps_panel_reset(
    box_fps_panel_t *panel)
{
    int32_t i;
    for (i = 0; i < panel->chart_count; i ++) {
        panel->charts[i].ready = false;
    }
}

/** FrameSamples was set or notified: summarize them again and write the samples of the bars
 * that changed while the fps panel is open.
 *
 * @param it Iterator of the observer
 */
static void box_fps_samples_on_set(
    ecs_iter_t *it)
{
    box_fps_t *fps = it->ctx;
    if (fps->fps.entity) {
        box_fps_summary_sync(it->world);
        box_fps_panel_bars_sync(it->world, fps, &fps->fps, false);
    }
}

/** LoadSamples was set or notified: summarize them again and write the samples of the bars that
 * changed while the load panel is open.
 *
 * @param it Iterator of the observer
 */
static void box_fps_load_on_set(
    ecs_iter_t *it)
{
    box_fps_t *fps = it->ctx;
    if (fps->load.entity) {
        box_fps_load_summary_sync(it->world);
        box_fps_panel_bars_sync(it->world, fps, &fps->load, false);
    }
}

/** The script of the panel changed on disk: load it again in the next update.
 *
 * @param world The world
 * @param ctx The panel state
 * @param owner The script entity
 */
static void box_fps_script_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    box_fps_t *fps = ctx;
    (void)world;
    if (owner == fps->script) {
        fps->reload = true;
    }
}

/** Load the script of the panel as a managed script in the root scope and watch its file.
 *
 * A missing file leaves the panel empty; it is looked for again the next time the panel opens.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_script_load(
    ecs_world_t *world,
    box_fps_t *fps)
{
    char *path = box_shader_locate_cwd(BOX_FPS_SCRIPT_PATH);
    box_file_stamp_t stamp;
    ecs_entity_t scope, script;
    const EcsScript *loaded;
    if (!path) {
        ecs_warn("box.fps: %s not found, the fps panel is empty", BOX_FPS_SCRIPT_PATH);
        fps->missing = true;
        goto done;
    }

    box_file_stamp(path, &stamp);
    scope = ecs_set_scope(world, 0);
    script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = BOX_FPS_SCRIPT }),
        .filename = path
    });
    ecs_set_scope(world, scope);

    loaded = script ? ecs_get(world, script, EcsScript) : NULL;
    if (!loaded || !loaded->script || loaded->error) {
        ecs_err("box.fps: failed to load %s", path);
    }

    fps->script = script;
    fps->missing = false;
    ecs_os_free(fps->path);
    fps->path = path;
    path = NULL;
    if (fps->watch && script) {
        box_file_watch_add(world, fps->watch, script, fps->path, &stamp);
    }

done:
    ecs_os_free(path);
}

/** Load the script of the panel again from its file after it changed on disk.
 *
 * A script that fails keeps the entities of the last load that succeeded where flecs does.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_script_reload(
    ecs_world_t *world,
    box_fps_t *fps)
{
    box_file_stamp_t stamp;
    ecs_entity_t scope;
    box_file_stamp(fps->path, &stamp);
    if (stamp.sec < 0) {
        goto done;
    }

    scope = ecs_set_scope(world, 0);
    if (ecs_script_reload(world, fps->script, NULL)) {
        ecs_err("box.fps: failed to reload %s", fps->path);
    }

    ecs_set_scope(world, scope);
    if (fps->watch) {
        box_file_watch_add(world, fps->watch, fps->script, fps->path, &stamp);
    }

done:
    return;
}

/** Load the script when it did not load yet, and load it again when it changed.
 *
 * A file that was not found is not looked for again until the panel opens again.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_script_sync(
    ecs_world_t *world,
    box_fps_t *fps)
{
    if (!fps->script && fps->missing) {
        goto done;
    }

    if (!fps->script || !ecs_is_alive(world, fps->script)) {
        if (fps->script && fps->watch) {
            box_file_watch_clear(world, fps->watch, fps->script);
        }

        fps->script = 0;
        fps->reload = false;
        box_fps_panel_reset(&fps->fps);
        box_fps_panel_reset(&fps->load);
        box_fps_panel_reset(&fps->gpu);
        box_fps_script_load(world, fps);
    } else if (fps->reload) {
        fps->reload = false;
        box_fps_panel_reset(&fps->fps);
        box_fps_panel_reset(&fps->load);
        box_fps_panel_reset(&fps->gpu);
        box_fps_script_reload(world, fps);
    }

done:
    return;
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

/** Create the scene, camera and screen of the content. A panel opens in the screen.
 *
 * The scene has SpatialIgnore: the bars cost no spatial update and picks pass through. The
 * screen keeps its children in order, so the fps panel stays above the load panel.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_build(
    ecs_world_t *world,
    box_fps_t *fps)
{
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
    ecs_add_id(world, fps->screen, EcsOrderedChildren);
    ecs_set(world, fps->screen, Layout, {
        .direction = DirectionColumn,
        .padding = { BOX_FPS_MARGIN, BOX_FPS_MARGIN, BOX_FPS_MARGIN, BOX_FPS_MARGIN },
        .spacing = BOX_FPS_MARGIN,
        .h = AlignRight,
        .v = VAlignTop
    });
}

/** Make a panel an instance of its template of the script, which the script defines.
 *
 * A template that loaded again with a new entity is not on the panel anymore, so it is added
 * again; one that was redefined in place updates the panel itself.
 *
 * @param world The world
 * @param panel The panel
 */
static void box_fps_template(
    ecs_world_t *world,
    box_fps_panel_t *panel)
{
    ecs_entity_t tmpl = ecs_lookup(world, panel->tmpl);
    if (tmpl && ecs_has(world, tmpl, EcsComponent) && !ecs_has_id(world, panel->entity, tmpl)) {
        ecs_add_id(world, panel->entity, tmpl);
    }
}

/** Find the size in points of the surface the panels show on: the first one that does not
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

/** Show the content: load the script, build the scene and create its view, a view without
 * clear, lighting, culling or effects at BOX_VIEW_ORDER_FPS.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_show(
    ecs_world_t *world,
    box_fps_t *fps)
{
    fps->missing = false;
    box_fps_script_sync(world, fps);
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

/** Hide the content: delete its view and its scene with the panels, so closed panels cost
 * nothing. The script stays loaded.
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
    fps->screen = 0;
    fps->fps.entity = 0;
    fps->load.entity = 0;
    fps->gpu.entity = 0;
    fps->shown = false;
    box_fps_panel_reset(&fps->fps);
    box_fps_panel_reset(&fps->load);
    box_fps_panel_reset(&fps->gpu);
}

/** Put the open panels of the screen in their order: the fps panel above the load panel above
 * the GPU stats panel.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_panels_order(
    ecs_world_t *world,
    box_fps_t *fps)
{
    ecs_entity_t order[3];
    int32_t count = 0;
    order[count] = fps->fps.entity;
    count += order[count] != 0;
    order[count] = fps->load.entity;
    count += order[count] != 0;
    order[count] = fps->gpu.entity;
    count += order[count] != 0;
    if (count > 1) {
        ecs_set_child_order(world, fps->screen, order, count);
    }
}

/** Open or close a panel in the screen of the shown content. A panel that opens summarizes its
 * samples; its charts are written once its template made them.
 *
 * @param world The world
 * @param fps The panel state
 * @param panel The panel
 * @param open Whether it is open
 */
static void box_fps_panel_sync(
    ecs_world_t *world,
    box_fps_t *fps,
    box_fps_panel_t *panel,
    bool open)
{
    if (panel->entity && !ecs_is_alive(world, panel->entity)) {
        panel->entity = 0;
        box_fps_panel_reset(panel);
    }

    if (!open && panel->entity) {
        ecs_delete(world, panel->entity);
        panel->entity = 0;
        box_fps_panel_reset(panel);
    }

    if (!open) {
        goto done;
    }

    if (!panel->entity) {
        panel->entity = box_fps_element(world, fps->screen, panel->name);
        box_fps_panels_order(world, fps);
        if (panel == &fps->fps) {
            box_fps_summary_sync(world);
        } else if (panel == &fps->load) {
            box_fps_load_summary_sync(world);
        }
    }

    box_fps_template(world, panel);
    box_fps_panel_bars_sync(world, fps, panel, true);
done:
    return;
}

/** Open and close the panels from the FpsPanel, LoadPanel and GpuPanel singletons, keep their
 * script loaded and place them. The content is shown while a panel is open.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxFpsUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_fps_t *fps = it->ctx;
    const FpsPanel *fps_panel = ecs_singleton_get(world, FpsPanel);
    const LoadPanel *load_panel = ecs_singleton_get(world, LoadPanel);
    const GpuPanel *gpu_panel = ecs_singleton_get(world, GpuPanel);
    bool fps_open = fps_panel && fps_panel->open;
    bool load_open = load_panel && load_panel->open;
    bool gpu_open = gpu_panel && gpu_panel->open;
    bool any = fps_open || load_open || gpu_open;
    if (!any && fps->shown) {
        box_fps_hide(world, fps);
    }

    if (!any) {
        goto done;
    }

    if (fps->shown && (!ecs_is_alive(world, fps->view) || !ecs_is_alive(world, fps->root) ||
        !ecs_is_alive(world, fps->screen)))
    {
        box_fps_hide(world, fps);
    }

    if (!fps->shown) {
        box_fps_show(world, fps);
    }

    box_fps_script_sync(world, fps);
    box_fps_panel_sync(world, fps, &fps->fps, fps_open);
    box_fps_panel_sync(world, fps, &fps->load, load_open);
    box_fps_panel_sync(world, fps, &fps->gpu, gpu_open);
    box_fps_measure(world, fps);
    if (fps->relayout) {
        box_fps_layout(world, fps);
    }

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
    box_fps_t *fps = ctx;
    (void)world;
    ecs_os_free(fps->path);
    ecs_os_free(fps);
}

/** Export a color to the script as a constant of the module.
 *
 * @param world The world
 * @param module The box.fps module
 * @param name Name of the constant
 * @param color The color
 */
static void box_fps_export_color(
    ecs_world_t *world,
    ecs_entity_t module,
    const char *name,
    rgb color)
{
    Rgb value = { color.r, color.g, color.b };
    ecs_const_var(world, {
        .name = name,
        .parent = module,
        .type = ecs_id(Rgb),
        .value = &value
    });
}

/** Export a number of milliseconds to the script as a constant of the module.
 *
 * @param world The world
 * @param module The box.fps module
 * @param name Name of the constant
 * @param ms The milliseconds
 */
static void box_fps_export_ms(
    ecs_world_t *world,
    ecs_entity_t module,
    const char *name,
    double ms)
{
    ecs_const_var(world, {
        .name = name,
        .parent = module,
        .type = ecs_id(ecs_f64_t),
        .value = &ms
    });
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
    ECS_IMPORT(world, BoxTask);
    ECS_IMPORT(world, BoxRender);

    ecs_set_name_prefix(world, "Fps");
    ECS_META_COMPONENT(world, FpsPanel);
    ECS_META_COMPONENT(world, FpsSummary);
    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, LoadPanel);
    ECS_META_COMPONENT(world, LoadSamples);
    ECS_META_COMPONENT(world, LoadStat);
    ECS_META_COMPONENT(world, LoadSummary);
    ECS_META_COMPONENT(world, GpuPanel);
    ECS_META_COMPONENT(world, GpuSceneStats);
    ECS_META_COMPONENT(world, GpuCommandStats);
    ECS_META_COMPONENT(world, GpuPacketStats);
    ECS_META_COMPONENT(world, GpuShadowStats);
    ECS_META_COMPONENT(world, GpuTextureGraphStats);
    ECS_META_COMPONENT(world, GpuMeshGraphStats);
    ECS_META_COMPONENT(world, GpuMemoryStats);
    ECS_META_COMPONENT(world, GpuGrassLevel);
    ECS_META_COMPONENT(world, GpuGrassStats);
    ECS_META_COMPONENT(world, GpuTimeStats);
    ECS_META_COMPONENT(world, GpuStats);
    ecs_set_hooks(world, FpsPanel, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, FpsSummary, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, LoadPanel, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, LoadSamples, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, LoadSummary, { .ctor = flecs_default_ctor });

    ecs_entity_t module = ecs_id(BoxFps);
    box_fps_export_color(world, module, "green", BOX_FPS_GREEN);
    box_fps_export_color(world, module, "yellow", BOX_FPS_YELLOW);
    box_fps_export_color(world, module, "red", BOX_FPS_RED);
    box_fps_export_ms(world, module, "fast_ms", BOX_FPS_FAST_MS);
    box_fps_export_ms(world, module, "slow_ms", BOX_FPS_SLOW_MS);

    box_fps_t *fps = ecs_os_calloc_t(box_fps_t);
    int32_t i;
    fps->fps.name = "panel";
    fps->fps.tmpl = BOX_FPS_TEMPLATE;
    fps->fps.chart_count = 1;
    fps->fps.charts[0].path = BOX_FPS_CHART_BARS;
    fps->load.name = "load";
    fps->load.tmpl = BOX_LOAD_TEMPLATE;
    fps->load.chart_count = BOX_LOAD_ROWS;
    for (i = 0; i < BOX_LOAD_ROWS; i ++) {
        fps->load.charts[i].path = box_load_bars[i];
    }

    fps->gpu.name = "gpu";
    fps->gpu.tmpl = BOX_GPU_TEMPLATE;

    ecs_atfini(world, box_fps_free, fps);
    ecs_singleton_add(world, FpsPanel);
    ecs_singleton_set(world, FpsSummary, {0});
    ecs_singleton_add(world, LoadPanel);
    ecs_singleton_add(world, LoadSamples);
    ecs_singleton_set(world, LoadSummary, {0});
    fps->watch = box_file_watch_kind(world, "fps script", box_fps_script_changed, fps);

    fps->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Surfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SamplesOnSet" }),
        .query.terms = {{
            .id = ecs_id(FrameSamples),
            .src.id = ecs_id(FrameSamples),
            .inout = EcsInOutNone
        }},
        .events = { EcsOnSet },
        .callback = box_fps_samples_on_set,
        .ctx = fps
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LoadSamplesOnSet" }),
        .query.terms = {{
            .id = ecs_id(LoadSamples),
            .src.id = ecs_id(LoadSamples),
            .inout = EcsInOutNone
        }},
        .events = { EcsOnSet },
        .callback = box_fps_load_on_set,
        .ctx = fps
    });

    box_fps_load_init(world);
    box_fps_gpu_init(world, module);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .phase = EcsOnLoad,
        .run = BoxFpsUpdate,
        .ctx = fps,
        .immediate = true
    });
}
