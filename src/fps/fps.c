/**
 * @file fps/fps.c
 * @brief Registers the box.fps module: the FpsPanel singleton, the FpsSummary of the last second
 * of frame samples and the system that opens and closes the fps panel, a scene and view of its
 * own whose panel is an instance of the template of etc/fps/fps.flecs.
 *
 * The script draws everything the panel shows. C creates the scene, camera and view, places the
 * panel at the top right of the surface, loads the script and loads it again when it changes.
 *
 * C provides the data: FpsSummary, the frame time of the sample of every bar, written only when
 * it changed, and the colors and limits of the bars.
 */

#define BOX_FPS_IMPL
#include <box.h>
#include "../app/watch.h"
#include "../render/preprocess.h"

/** Surface size in points the panel is placed in while the world has no Surface */
#define BOX_FPS_DEFAULT_WIDTH (1280.0f)

/** Surface height in points while the world has no Surface */
#define BOX_FPS_DEFAULT_HEIGHT (720.0f)

/** State of the fps panel of a world, owned by the module */
typedef struct box_fps_t {
    ecs_entity_t scope;         /**< Root entity of the content and the view */
    ecs_entity_t view;          /**< View of the content while the panel is open */
    ecs_entity_t root;          /**< Scene root of the content */
    ecs_entity_t camera;        /**< Orthographic camera at one point per unit */
    ecs_entity_t screen;        /**< Layout root that fills the surface */
    ecs_entity_t panel;         /**< The panel, instance of the script template */
    ecs_entity_t script;        /**< Managed script of etc/fps/fps.flecs, 0 before it loads */
    char *path;                 /**< Path the script was found at, NULL before it loads */
    int32_t watch;              /**< Id of the file watch kind of the script, 0 without box.app */
    bool reload;                /**< The script changed on disk since the last update */
    bool missing;               /**< The script was not found since the panel opened */
    bool shown;                 /**< The content is shown */
    bool relayout;              /**< The surface changed since the screen was sized */
    ecs_entity_t surface;       /**< Surface the screen fills, 0 while the world has none */
    float width;                /**< Surface width in points the screen was sized for */
    float height;               /**< Surface height in points the screen was sized for */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
    ecs_entity_t bars[BOX_FPS_BARS];    /**< Bars of the chart, oldest first */
    double bar_ms[BOX_FPS_BARS];        /**< Frame time last written to each bar */
    ecs_entity_t bar_mut;       /**< Mut variables of the bar template, BOX_FPS_BAR_MUT */
    bool bars_ready;            /**< bars, bar_mut and bar_ms are of the current content */
} box_fps_t;

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

/** Find the bars of the chart and the mut variables of the bar template that hold their ms.
 *
 * The mut variables must be ms alone, an f64, since C sets them as a whole: a mut has an
 * on_replace hook, so it cannot be changed in place.
 *
 * @param world The world
 * @param fps The panel state
 * @return Whether the script created the bars, which have the mut variables
 */
static bool box_fps_bars_resolve(
    ecs_world_t *world,
    box_fps_t *fps)
{
    ecs_entity_t mut = ecs_lookup(world, BOX_FPS_BAR_MUT);
    ecs_entity_t chart = ecs_lookup(world, BOX_FPS_CHART);
    const EcsStruct *st = mut ? ecs_get(world, mut, EcsStruct) : NULL;
    const ecs_member_t *member = st && ecs_vec_count(&st->members) == 1 ?
        ecs_vec_first_t(&st->members, ecs_member_t) : NULL;
    bool result = false;
    char name[32];
    int32_t i;
    if (!member || !chart || ecs_os_strcmp(member->name, "ms") ||
        member->type != ecs_id(ecs_f64_t))
    {
        goto done;
    }

    for (i = 0; i < BOX_FPS_BARS; i ++) {
        ecs_os_snprintf(name, sizeof(name), "bar_%d", i);
        fps->bars[i] = ecs_lookup_child(world, chart, name);
        if (!fps->bars[i] || !ecs_has_id(world, fps->bars[i], mut)) {
            goto done;
        }
    }

    fps->bar_mut = mut;
    result = true;

done:
    return result;
}

/** Whether the bars that were found are still those of the content.
 *
 * @param world The world
 * @param fps The panel state
 * @return Whether the bars and the mut variables of the bar template are alive
 */
static bool box_fps_bars_valid(
    const ecs_world_t *world,
    const box_fps_t *fps)
{
    return fps->bars_ready && ecs_is_alive(world, fps->bar_mut) &&
        ecs_is_alive(world, fps->bars[0]) && ecs_is_alive(world, fps->bars[BOX_FPS_BARS - 1]);
}

/** Write the frame time of its sample to the mut ms of every bar whose sample changed, so a new
 * sample evaluates only the bars that show another sample. Bars found again get all of them.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_bars_sync(
    ecs_world_t *world,
    box_fps_t *fps)
{
    const FrameSamples *samples = ecs_singleton_get(world, FrameSamples);
    bool all = false;
    int32_t i;
    if (!box_fps_bars_valid(world, fps)) {
        fps->bars_ready = box_fps_bars_resolve(world, fps);
        all = true;
    }

    if (!fps->bars_ready) {
        goto done;
    }

    for (i = 0; i < BOX_FPS_BARS; i ++) {
        double ms = samples ? box_frame_sample(samples, BOX_FPS_BARS - 1 - i) : 0;
        if (all || ms != fps->bar_ms[i]) {
            ecs_set_id(world, fps->bars[i], fps->bar_mut, sizeof(double), &ms);
            fps->bar_ms[i] = ms;
        }
    }

done:
    return;
}

/** FrameSamples was set or notified: summarize them again and write the samples of the bars
 * that changed while the panel is open.
 *
 * @param it Iterator of the observer
 */
static void box_fps_samples_on_set(
    ecs_iter_t *it)
{
    box_fps_t *fps = it->ctx;
    if (fps->shown) {
        box_fps_summary_sync(it->world);
        box_fps_bars_sync(it->world, fps);
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
        fps->bars_ready = false;
        box_fps_script_load(world, fps);
    } else if (fps->reload) {
        fps->reload = false;
        fps->bars_ready = false;
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

/** Create the scene, camera, screen and panel of the content. The script fills the panel.
 *
 * The scene has SpatialIgnore: the bars cost no spatial update and picks pass through.
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
    ecs_set(world, fps->screen, Layout, {
        .direction = DirectionRow,
        .padding = { BOX_FPS_MARGIN, BOX_FPS_MARGIN, BOX_FPS_MARGIN, BOX_FPS_MARGIN },
        .h = AlignRight,
        .v = VAlignTop
    });
    fps->panel = box_fps_element(world, fps->screen, "panel");
}

/** Make the panel an instance of the template of the script, which the script defines.
 *
 * A template that loaded again with a new entity is not on the panel anymore, so it is added
 * again; one that was redefined in place updates the panel itself.
 *
 * @param world The world
 * @param fps The panel state
 */
static void box_fps_template(
    ecs_world_t *world,
    box_fps_t *fps)
{
    ecs_entity_t tmpl = ecs_lookup(world, BOX_FPS_TEMPLATE);
    if (tmpl && ecs_has(world, tmpl, EcsComponent) && !ecs_has_id(world, fps->panel, tmpl)) {
        ecs_add_id(world, fps->panel, tmpl);
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

/** Show the panel: load its script, build its content and create its view, a view without
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
    box_fps_summary_sync(world);
}

/** Hide the panel: delete its view and its content, so a closed panel costs nothing. The
 * script stays loaded.
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
    fps->panel = 0;
    fps->shown = false;
    fps->bars_ready = false;
}

/** Open and close the panel from the FpsPanel singleton, keep its script loaded and place it.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxFpsUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_fps_t *fps = it->ctx;
    const FpsPanel *panel = ecs_singleton_get(world, FpsPanel);
    bool open = panel && panel->open;
    if (!open && fps->shown) {
        box_fps_hide(world, fps);
    }

    if (!open) {
        goto done;
    }

    if (fps->shown && (!ecs_is_alive(world, fps->view) || !ecs_is_alive(world, fps->root) ||
        !ecs_is_alive(world, fps->panel)))
    {
        box_fps_hide(world, fps);
    }

    if (!fps->shown) {
        box_fps_show(world, fps);
    }

    box_fps_script_sync(world, fps);
    box_fps_template(world, fps);
    if (!box_fps_bars_valid(world, fps)) {
        box_fps_bars_sync(world, fps);
    }

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

    ECS_META_COMPONENT(world, FpsPanel);
    ECS_META_COMPONENT(world, FpsSummary);
    ecs_set_hooks(world, FpsPanel, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, FpsSummary, { .ctor = flecs_default_ctor });

    ecs_entity_t module = ecs_id(BoxFps);
    box_fps_export_color(world, module, "green", BOX_FPS_GREEN);
    box_fps_export_color(world, module, "yellow", BOX_FPS_YELLOW);
    box_fps_export_color(world, module, "red", BOX_FPS_RED);
    box_fps_export_ms(world, module, "fast_ms", BOX_FPS_FAST_MS);
    box_fps_export_ms(world, module, "slow_ms", BOX_FPS_SLOW_MS);

    box_fps_t *fps = ecs_os_calloc_t(box_fps_t);
    ecs_atfini(world, box_fps_free, fps);
    ecs_singleton_add(world, FpsPanel);
    ecs_singleton_set(world, FpsSummary, {0});
    fps->watch = box_file_watch_kind(world, "fps script", box_fps_script_changed, fps);

    fps->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "FpsSurfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "FpsSamplesOnSet" }),
        .query.terms = {{
            .id = ecs_id(FrameSamples),
            .src.id = ecs_id(FrameSamples),
            .inout = EcsInOutNone
        }},
        .events = { EcsOnSet },
        .callback = box_fps_samples_on_set,
        .ctx = fps
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FpsUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxFpsUpdate,
        .ctx = fps,
        .immediate = true
    });
}
