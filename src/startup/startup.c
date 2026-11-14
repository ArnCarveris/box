/**
 * @file startup/startup.c
 * @brief Registers the box.startup module: the StartupOverlay singleton and the system that
 * opens the startup overlay while a task of InProgressTask blocks, a scene and view of its own
 * whose panel is an instance of the template of etc/startup/startup.flecs.
 *
 * The script draws everything the panel shows from InProgressTask, which box.task writes. C
 * creates the scene, camera and view, places the panel in the middle of the surface, loads the
 * script and loads it again when it changes.
 */

#define BOX_STARTUP_IMPL
#include <box.h>
#include "../app/watch.h"
#include "../render/preprocess.h"

/** Surface width in points the panel is placed in while the world has no Surface */
#define BOX_STARTUP_DEFAULT_WIDTH (1280.0f)

/** Surface height in points while the world has no Surface */
#define BOX_STARTUP_DEFAULT_HEIGHT (720.0f)

/** State of the startup overlay of a world, owned by the module */
typedef struct box_startup_t {
    ecs_entity_t scope;         /**< Root entity of the content and the view */
    ecs_entity_t view;          /**< View of the content while the overlay is open */
    ecs_entity_t root;          /**< Scene root of the content */
    ecs_entity_t camera;        /**< Orthographic camera at one point per unit */
    ecs_entity_t screen;        /**< Layout root that fills the surface */
    ecs_entity_t panel;         /**< The panel, instance of the script template */
    ecs_entity_t script;        /**< Managed script of the overlay, 0 before it loads */
    char *script_path;          /**< Script set by box_startup_script, NULL for the default */
    char *path;                 /**< Path the script was found at, NULL before it loads */
    int32_t watch;              /**< Id of the file watch kind of the script */
    bool reload;                /**< The script changed on disk since the last update */
    bool missing;               /**< The script was not found since the overlay opened */
    bool shown;                 /**< The content is shown */
    bool relayout;              /**< The surface changed since the screen was sized */
    int32_t idle;               /**< Frames in a row without a blocking task */
    ecs_entity_t surface;       /**< Surface the screen fills, 0 while the world has none */
    float width;                /**< Surface width in points the screen was sized for */
    float height;               /**< Surface height in points the screen was sized for */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
} box_startup_t;

/** The script of the overlay changed on disk: load it again in the next update.
 *
 * @param world The world
 * @param ctx The overlay state
 * @param owner The script entity
 */
static void box_startup_script_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    box_startup_t *st = ctx;
    (void)world;
    if (owner == st->script) {
        st->reload = true;
    }
}

/** Load the script of the overlay as a managed script in the root scope and watch its file.
 *
 * A missing file leaves the panel empty; it is looked for again the next time it opens.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_script_load(
    ecs_world_t *world,
    box_startup_t *st)
{
    const char *rel = st->script_path ? st->script_path : BOX_STARTUP_SCRIPT_PATH;
    char *path = box_shader_locate_cwd(rel);
    box_file_stamp_t stamp;
    ecs_entity_t scope, script;
    const EcsScript *loaded;
    if (!path) {
        ecs_warn("box.startup: %s not found, the startup overlay is empty", rel);
        st->missing = true;
        goto done;
    }

    box_file_stamp(path, &stamp);
    scope = ecs_set_scope(world, 0);
    script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = BOX_STARTUP_SCRIPT }),
        .filename = path
    });
    ecs_set_scope(world, scope);

    loaded = script ? ecs_get(world, script, EcsScript) : NULL;
    if (!loaded || !loaded->script || loaded->error) {
        ecs_err("box.startup: failed to load %s", path);
    }

    st->script = script;
    st->missing = false;
    ecs_os_free(st->path);
    st->path = path;
    path = NULL;
    if (st->watch && script) {
        box_file_watch_add(world, st->watch, script, st->path, &stamp);
    }

done:
    ecs_os_free(path);
}

/** Load the script of the overlay again from its file after it changed on disk.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_script_reload(
    ecs_world_t *world,
    box_startup_t *st)
{
    box_file_stamp_t stamp;
    ecs_entity_t scope;
    box_file_stamp(st->path, &stamp);
    if (stamp.sec < 0) {
        goto done;
    }

    scope = ecs_set_scope(world, 0);
    if (ecs_script_reload(world, st->script, NULL)) {
        ecs_err("box.startup: failed to reload %s", st->path);
    }

    ecs_set_scope(world, scope);
    if (st->watch) {
        box_file_watch_add(world, st->watch, st->script, st->path, &stamp);
    }

done:
    return;
}

/** Load the script when it did not load yet, and load it again when it changed.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_script_sync(
    ecs_world_t *world,
    box_startup_t *st)
{
    if (!st->script && st->missing) {
        goto done;
    }

    if (!st->script || !ecs_is_alive(world, st->script)) {
        if (st->script && st->watch) {
            box_file_watch_clear(world, st->watch, st->script);
        }

        st->script = 0;
        st->reload = false;
        box_startup_script_load(world, st);
    } else if (st->reload) {
        st->reload = false;
        box_startup_script_reload(world, st);
    }

done:
    return;
}

/** Get the root entity of the content and the view, creating it when needed.
 *
 * @param world The world
 * @param st The overlay state
 * @return The root entity
 */
static ecs_entity_t box_startup_scope(
    ecs_world_t *world,
    box_startup_t *st)
{
    if (!st->scope || !ecs_is_alive(world, st->scope)) {
        ecs_entity_t prev = ecs_set_scope(world, 0);
        st->scope = ecs_entity(world, { .name = BOX_STARTUP_SCOPE });
        ecs_set_scope(world, prev);
    }

    return st->scope;
}

/** Create a named element of the overlay content.
 *
 * @param world The world
 * @param parent The parent
 * @param name The name
 * @return The element
 */
static ecs_entity_t box_startup_element(
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
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_build(
    ecs_world_t *world,
    box_startup_t *st)
{
    st->root = ecs_entity(world, { .name = "ui", .parent = box_startup_scope(world, st) });
    ecs_set(world, st->root, Scene, {0});
    ecs_add(world, st->root, SpatialIgnore);
    st->camera = box_startup_element(world, st->root, "camera");
    ecs_set(world, st->camera, Position, {0, 0, 100});
    ecs_set(world, st->camera, Camera, {
        .fov = 60, .near_ = 0.1f, .far_ = 1000, .ortho = true, .ortho_height = 10,
        .ortho_scale = 1
    });

    st->screen = box_startup_element(world, st->root, "screen");
    ecs_set(world, st->screen, Layout, {
        .direction = DirectionRow,
        .h = AlignCenter,
        .v = VAlignMiddle
    });
    st->panel = box_startup_element(world, st->screen, "panel");
}

/** Make the panel an instance of the template of the script, which the script defines.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_template(
    ecs_world_t *world,
    box_startup_t *st)
{
    ecs_entity_t tmpl = ecs_lookup(world, BOX_STARTUP_TEMPLATE);
    if (tmpl && ecs_has(world, tmpl, EcsComponent) && !ecs_has_id(world, st->panel, tmpl)) {
        ecs_add_id(world, st->panel, tmpl);
    }
}

/** Find the size in points of the surface the overlay shows on: the first one that does not
 * render into a texture.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_measure(
    ecs_world_t *world,
    box_startup_t *st)
{
    float w = BOX_STARTUP_DEFAULT_WIDTH, h = BOX_STARTUP_DEFAULT_HEIGHT;
    ecs_entity_t surface = 0;
    bool found = false;
    ecs_iter_t it = ecs_query_iter(world, st->surfaces);
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

    if (surface != st->surface || w != st->width || h != st->height) {
        st->surface = surface;
        st->width = w;
        st->height = h;
        st->relayout = true;
    }
}

/** Make the screen fill the surface, or the default size without one.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_layout(
    ecs_world_t *world,
    box_startup_t *st)
{
    if (st->surface) {
        ecs_set(world, st->screen, LayoutFill, { st->surface });
    } else {
        ecs_remove(world, st->screen, LayoutFill);
        ecs_set(world, st->screen, LayoutItem, {
            .width = { .mode = SizeModeFixed, .value = st->width },
            .height = { .mode = SizeModeFixed, .value = st->height }
        });
    }

    st->relayout = false;
}

/** Show the overlay: load its script, build its content and create its view, a view without
 * clear, lighting, culling or effects at BOX_VIEW_ORDER_STARTUP.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_show(
    ecs_world_t *world,
    box_startup_t *st)
{
    st->missing = false;
    box_startup_script_sync(world, st);
    box_startup_build(world, st);
    st->view = ecs_entity(world, { .name = "view", .parent = box_startup_scope(world, st) });
    ecs_set(world, st->view, View, {
        .camera = st->camera,
        .order = BOX_VIEW_ORDER_STARTUP
    });
    st->shown = true;
    st->relayout = true;
    st->surface = 0;
}

/** Hide the overlay: delete its view and its content. The script stays loaded.
 *
 * @param world The world
 * @param st The overlay state
 */
static void box_startup_hide(
    ecs_world_t *world,
    box_startup_t *st)
{
    if (st->view && ecs_is_alive(world, st->view)) {
        ecs_delete(world, st->view);
    }

    if (st->root && ecs_is_alive(world, st->root)) {
        ecs_delete(world, st->root);
    }

    st->view = 0;
    st->root = 0;
    st->panel = 0;
    st->shown = false;
}

/** Whether the overlay may open in a world: enabled, or a run of box_app_run with a window
 * that writes no capture, and not disabled.
 *
 * @param world The world
 * @param overlay The overlay singleton
 * @return Whether it may open
 */
static bool box_startup_eligible(
    const ecs_world_t *world,
    const StartupOverlay *overlay)
{
    const App *app = ecs_singleton_get(world, App);
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    bool run = app && status && status->running && !app->headless &&
        !(app->out && app->out[0]);

    return !overlay->disabled && (overlay->enabled || run);
}

/** Decide whether the overlay is open: it opens while a task blocks, and once BOX_STARTUP_FRAMES
 * frames in a row had none, the startup is over. The frames bridge the gap between one blocking
 * task and the next, such as a font that loaded and the glyph fields it needs.
 *
 * @param world The world
 * @param st The overlay state
 * @param value The overlay singleton, updated
 */
static void box_startup_decide(
    ecs_world_t *world,
    box_startup_t *st,
    StartupOverlay *value)
{
    const InProgressTask *tasks = ecs_singleton_get(world, InProgressTask);
    int32_t blocking = tasks ? tasks->blocking : 0;
    if (value->finished || !box_startup_eligible(world, value)) {
        value->open = false;
        goto done;
    }

    st->idle = blocking > 0 ? 0 : st->idle + 1;
    if (blocking > 0) {
        value->open = true;
    } else if (st->idle > BOX_STARTUP_FRAMES) {
        value->open = false;
        value->finished = true;
    }

done:
    return;
}

/** Open and close the overlay from the tasks in progress, keep its script loaded and place it.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxStartupUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_startup_t *st = it->ctx;
    const StartupOverlay *cur = ecs_singleton_get(world, StartupOverlay);
    StartupOverlay value = cur ? *cur : (StartupOverlay){0};
    box_startup_decide(world, st, &value);
    if (!cur || cur->open != value.open || cur->finished != value.finished) {
        ecs_singleton_set_ptr(world, StartupOverlay, &value);
    }

    if (!value.open && st->shown) {
        box_startup_hide(world, st);
    }

    if (!value.open) {
        goto done;
    }

    if (st->shown && (!ecs_is_alive(world, st->view) || !ecs_is_alive(world, st->root) ||
        !ecs_is_alive(world, st->panel)))
    {
        box_startup_hide(world, st);
    }

    if (!st->shown) {
        box_startup_show(world, st);
    }

    box_startup_script_sync(world, st);
    box_startup_template(world, st);
    box_startup_measure(world, st);
    if (st->relayout) {
        box_startup_layout(world, st);
    }

done:
    return;
}

void box_startup_script(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t system = ecs_lookup(world, "box.startup.StartupUpdate");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    box_startup_t *st = s ? s->ctx : NULL;
    if (st) {
        ecs_os_free(st->script_path);
        st->script_path = path ? ecs_os_strdup(path) : NULL;
        st->missing = false;
    }
}

/** Free the overlay state when its system is deleted.
 *
 * @param ctx The overlay state
 */
static void box_startup_free(
    void *ctx)
{
    box_startup_t *st = ctx;
    ecs_os_free(st->script_path);
    ecs_os_free(st->path);
    ecs_os_free(st);
}

void BoxStartupImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxStartup);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTask);
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

    ecs_set_name_prefix(world, "Startup");
    ECS_META_COMPONENT(world, StartupOverlay);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, StartupOverlay, { .ctor = flecs_default_ctor });

    box_startup_t *st = ecs_os_calloc_t(box_startup_t);
    ecs_singleton_add(world, StartupOverlay);
    st->watch = box_file_watch_kind(world, "startup script", box_startup_script_changed, st);

    st->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "StartupSurfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "StartupUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxStartupUpdate,
        .ctx = st,
        .ctx_free = box_startup_free,
        .immediate = true
    });
}
