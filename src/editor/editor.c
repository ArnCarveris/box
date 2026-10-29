/**
 * @file editor/editor.c
 * @brief Registers the box.editor module: the Editor singleton and the system that opens,
 * closes and lays out the editor from changes of it.
 */

#define BOX_EDITOR_IMPL
#include "editor.h"
#include "../app/watch.h"

ECS_COMPONENT_DECLARE(EditorHost);

/** Surface size in points the content is laid out for while the world has no Surface */
#define BOX_EDITOR_DEFAULT_WIDTH (1280.0f)

/** Surface height in points while the world has no Surface */
#define BOX_EDITOR_DEFAULT_HEIGHT (720.0f)

/** Construct Editor values: closed, a quarter of the surface width on the right.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_editor_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Editor *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Editor){
            .width = 0.25f,
            .side = EditorSideRight
        };
    }
}

/** Editor was set: lay the content out again for the new size or side.
 *
 * @param it Iterator over the Editor singleton
 */
static void box_editor_on_set(
    ecs_iter_t *it)
{
    box_editor_t *ed = it->ctx;
    ed->relayout = true;
}

/** The script of the editor changed on disk: load it again in the next update.
 *
 * @param world The world
 * @param ctx The editor state
 * @param owner The script entity
 */
static void box_editor_script_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    box_editor_t *ed = ctx;
    (void)world;
    if (owner == ed->script) {
        ed->reload = true;
    }
}

/** Find the size in points of the surface the editor shows on: the first one that does not
 * render into a texture.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_measure(
    ecs_world_t *world,
    box_editor_t *ed)
{
    float w = BOX_EDITOR_DEFAULT_WIDTH, h = BOX_EDITOR_DEFAULT_HEIGHT;
    ecs_entity_t surface = 0;
    bool found = false;
    ecs_iter_t it = ecs_query_iter(world, ed->surfaces);
    int32_t i;
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count && !found; i ++) {
            if (!s[i].texture) {
                /* Prefer the real size in points: a window resize only updates
                 * SurfaceStatus, not the requested Surface size. */
                const SurfaceStatus *st = ecs_get(world, it.entities[i], SurfaceStatus);
                if (st && st->point_width > 0 && st->point_height > 0) {
                    w = (float)st->point_width;
                    h = (float)st->point_height;
                } else {
                    w = (float)s[i].width;
                    h = (float)s[i].height;
                }
                surface = it.entities[i];
                found = true;
            }
        }
    }

    if (surface != ed->surface) {
        ed->surface = surface;
        ed->relayout = true;
    }

    if (w != ed->width || h != ed->height) {
        ed->width = w;
        ed->height = h;
        ed->relayout = true;
    }
}

/** Create the view of the editor, a view without clear, lighting, culling or effects that
 * renders after the game views and before the console at BOX_VIEW_ORDER_EDITOR.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_view_create(
    ecs_world_t *world,
    box_editor_t *ed)
{
    ed->view = ecs_entity(world, { .name = "view", .parent = box_editor_scope(world, ed) });
    ecs_set(world, ed->view, View, {
        .camera = ed->ui.camera,
        .order = BOX_VIEW_ORDER_EDITOR
    });
}

/** Show the editor: load its script, build its content when needed and create its view.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_show(
    ecs_world_t *world,
    box_editor_t *ed)
{
    ed->missing = false;
    box_editor_script_sync(world, ed);
    box_editor_ui_build(world, ed);
    box_editor_view_create(world, ed);
    ed->shown = true;
    ed->relayout = true;
}

/** Test whether an entity is the root of the editor content or below it.
 *
 * @param world The world
 * @param ed The editor state
 * @param e The entity
 * @return Whether the entity is part of the content
 */
static bool box_editor_contains(
    const ecs_world_t *world,
    const box_editor_t *ed,
    ecs_entity_t e)
{
    while (e && ecs_is_alive(world, e) && e != ed->ui.root) {
        e = ecs_get_parent(world, e);
    }

    return e != 0 && e == ed->ui.root;
}

/** Hide the editor: delete its view and clear a focus inside the content, keeping the content.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_hide(
    ecs_world_t *world,
    box_editor_t *ed)
{
    ecs_entity_t focus = ecs_script_get_focus(world);
    if (ed->view && ecs_is_alive(world, ed->view)) {
        ecs_delete(world, ed->view);
    }

    if (focus && ed->ui.root && box_editor_contains(world, ed, focus)) {
        ecs_script_set_focus(world, 0);
    }

    ed->view = 0;
    ed->shown = false;
}

/** Open, close and lay out the editor from the Editor singleton.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxEditorUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_editor_t *ed = it->ctx;
    const Editor *editor = ecs_singleton_get(world, Editor);
    if (!editor) {
        if (ed->shown) {
            box_editor_hide(world, ed);
        }

        goto done;
    }

    if (editor->open && !ed->shown) {
        box_editor_show(world, ed);
    } else if (!editor->open && ed->shown) {
        box_editor_hide(world, ed);
    }

    if (ed->shown && (!ecs_is_alive(world, ed->view) || !ecs_is_alive(world, ed->ui.root) ||
        !ecs_is_alive(world, ed->ui.panel)))
    {
        box_editor_hide(world, ed);
        box_editor_show(world, ed);
    }

    if (ed->shown) {
        box_editor_script_sync(world, ed);
        box_editor_measure(world, ed);
        editor = ecs_singleton_get(world, Editor);
        box_editor_ui_sync(world, ed, editor);
    }

done:
    return;
}

ecs_entity_t box_editor_scope(
    ecs_world_t *world,
    box_editor_t *ed)
{
    if (!ed->scope || !ecs_is_alive(world, ed->scope)) {
        ecs_entity_t prev = ecs_set_scope(world, 0);
        ed->scope = ecs_entity(world, { .name = BOX_EDITOR_SCOPE });
        ecs_set_scope(world, prev);
    }

    return ed->scope;
}

/** Free the editor state when the world is deleted.
 *
 * @param world The world
 * @param ctx The editor state
 */
static void box_editor_free(
    ecs_world_t *world,
    void *ctx)
{
    box_editor_t *ed = ctx;
    (void)world;
    ecs_os_free(ed->path);
    ecs_os_free(ed);
}

void BoxEditorImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEditor);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxText);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxPicking);
    ECS_IMPORT(world, BoxUi);

    ECS_META_COMPONENT(world, EditorSide);
    ECS_META_COMPONENT(world, Editor);
    ecs_set_hooks(world, Editor, { .ctor = box_editor_ctor });
    ECS_COMPONENT_DEFINE(world, EditorHost);
    ecs_set_hooks(world, EditorHost, { .ctor = flecs_default_ctor });

    box_editor_t *ed = ecs_os_calloc_t(box_editor_t);
    ed->module = ecs_id(BoxEditor);
    ecs_singleton_set(world, EditorHost, { ed });
    ecs_atfini(world, box_editor_free, ed);
    ecs_singleton_add(world, Editor);
    ed->watch = box_file_watch_kind(world, "editor script", box_editor_script_changed, ed);

    ed->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "EditorSurfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "EditorOnSet" }),
        .query.terms = {{ .id = ecs_id(Editor), .inout = EcsInOutNone }},
        .events = { EcsOnSet },
        .callback = box_editor_on_set,
        .ctx = ed
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "EditorUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxEditorUpdate,
        .ctx = ed,
        .immediate = true
    });
}
