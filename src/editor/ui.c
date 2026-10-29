/**
 * @file editor/ui.c
 * @brief The editor content: a UI scene whose panel, docked to a side of the surface, is an
 * instance of the template of etc/editor/editor.flecs, and the load of that script.
 */

#include "editor.h"
#include "../app/watch.h"
#include "../render/preprocess.h"

/** Create a named element of the editor content.
 *
 * @param world The world
 * @param parent The parent
 * @param name The name
 * @return The element
 */
static ecs_entity_t box_editor_element(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t result = ecs_entity(world, { .name = name, .parent = parent });
    ecs_set(world, result, Position, {0, 0, 0});

    return result;
}

/** Load the script of the editor as a managed script in the root scope and watch its file.
 *
 * A missing file leaves the panel empty; it is looked for again the next time the editor opens.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_script_load(
    ecs_world_t *world,
    box_editor_t *ed)
{
    char *path = box_shader_locate_cwd(BOX_EDITOR_SCRIPT_PATH);
    box_file_stamp_t stamp;
    ecs_entity_t scope, script;
    const EcsScript *loaded;
    if (!path) {
        ecs_warn("box.editor: %s not found, the editor is empty", BOX_EDITOR_SCRIPT_PATH);
        ed->missing = true;
        goto done;
    }

    box_file_stamp(path, &stamp);
    scope = ecs_set_scope(world, 0);
    script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = BOX_EDITOR_SCRIPT }),
        .filename = path
    });
    ecs_set_scope(world, scope);

    loaded = script ? ecs_get(world, script, EcsScript) : NULL;
    if (!loaded || !loaded->script || loaded->error) {
        ecs_err("box.editor: failed to load %s", path);
    }

    ed->script = script;
    ed->missing = false;
    ecs_os_free(ed->path);
    ed->path = path;
    path = NULL;
    if (ed->watch && script) {
        box_file_watch_add(world, ed->watch, script, ed->path, &stamp);
    }

done:
    ecs_os_free(path);
}

/** Load the script of the editor again from its file after it changed on disk.
 *
 * A script that fails keeps the entities of the last load that succeeded where flecs does.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_script_reload(
    ecs_world_t *world,
    box_editor_t *ed)
{
    box_file_stamp_t stamp;
    ecs_entity_t scope;
    box_file_stamp(ed->path, &stamp);
    if (stamp.sec < 0) {
        goto done;
    }

    scope = ecs_set_scope(world, 0);
    if (ecs_script_reload(world, ed->script, NULL)) {
        ecs_err("box.editor: failed to reload %s", ed->path);
    }

    ecs_set_scope(world, scope);
    if (ed->watch) {
        box_file_watch_add(world, ed->watch, ed->script, ed->path, &stamp);
    }

done:
    return;
}

void box_editor_script_sync(
    ecs_world_t *world,
    box_editor_t *ed)
{
    if (!ed->script && ed->missing) {
        goto done;
    }

    if (!ed->script || !ecs_is_alive(world, ed->script)) {
        if (ed->script && ed->watch) {
            box_file_watch_clear(world, ed->watch, ed->script);
        }

        ed->script = 0;
        ed->reload = false;
        box_editor_script_load(world, ed);
    } else if (ed->reload) {
        ed->reload = false;
        box_editor_script_reload(world, ed);
    }

done:
    return;
}

void box_editor_ui_build(
    ecs_world_t *world,
    box_editor_t *ed)
{
    box_editor_ui_t *ui = &ed->ui;
    if (ui->root && ecs_is_alive(world, ui->root) && ecs_is_alive(world, ui->panel)) {
        goto done;
    }

    if (ui->root && ecs_is_alive(world, ui->root)) {
        ecs_delete(world, ui->root);
    }

    ui->root = ecs_entity(world, { .name = "ui", .parent = box_editor_scope(world, ed) });
    ecs_set(world, ui->root, Scene, {0});
    ui->camera = box_editor_element(world, ui->root, "camera");
    ecs_set(world, ui->camera, Position, {0, 0, 100});
    ecs_set(world, ui->camera, Camera, {
        .fov = 60, .near_ = 0.1f, .far_ = 1000, .ortho = true, .ortho_height = 10,
        .ortho_scale = 1
    });

    ui->screen = box_editor_element(world, ui->root, "screen");
    ecs_set(world, ui->screen, Layout, { .direction = DirectionRow });
    ui->panel = box_editor_element(world, ui->screen, "panel");
    ecs_add(world, ui->panel, InputBlocker);
done:
    return;
}

/** Make the panel an instance of the template of the script, which the script defines.
 *
 * A template that loaded again with a new entity is not on the panel anymore, so it is added
 * again; one that was redefined in place updates the panel itself.
 *
 * @param world The world
 * @param ed The editor state
 */
static void box_editor_ui_template(
    ecs_world_t *world,
    box_editor_t *ed)
{
    ecs_entity_t tmpl = ecs_lookup(world, BOX_EDITOR_TEMPLATE);
    if (tmpl && ecs_has(world, tmpl, EcsComponent) &&
        !ecs_has_id(world, ed->ui.panel, tmpl))
    {
        ecs_add_id(world, ed->ui.panel, tmpl);
    }
}

/** Dock the panel to its side at its share of the surface width, at the full height.
 *
 * @param world The world
 * @param ed The editor state
 * @param editor The Editor singleton
 */
static void box_editor_ui_layout(
    ecs_world_t *world,
    box_editor_t *ed,
    const Editor *editor)
{
    box_editor_ui_t *ui = &ed->ui;
    float share = editor->width;
    share = share > 0 ? share : 0;
    share = share < 1 ? share : 1;
    if (ed->surface) {
        ecs_set(world, ui->screen, LayoutFill, { ed->surface });
    } else {
        ecs_remove(world, ui->screen, LayoutFill);
        ecs_set(world, ui->screen, LayoutItem, {
            .width = { .mode = SizeModeFixed, .value = ed->width },
            .height = { .mode = SizeModeFixed, .value = ed->height }
        });
    }

    ecs_set(world, ui->screen, Layout, {
        .direction = DirectionRow,
        .h = editor->side == EditorSideLeft ? AlignLeft : AlignRight
    });
    ecs_set(world, ui->panel, LayoutItem, {
        .width = { .mode = SizeModeFixed, .value = share * ed->width },
        .height = { .mode = SizeModeGrow, .value = 1 }
    });
}

void box_editor_ui_sync(
    ecs_world_t *world,
    box_editor_t *ed,
    const Editor *editor)
{
    box_editor_ui_template(world, ed);
    if (ed->relayout && editor) {
        box_editor_ui_layout(world, ed, editor);
        ed->relayout = false;
    }
}
