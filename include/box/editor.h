/**
 * @file box/editor.h
 * @brief The in process editor: a panel docked to a side of the surface whose UI is flecs script.
 *
 * The Editor singleton opens, closes and resizes it, from C, a script, the REST API or the edit
 * command (edit, edit on, edit off). Its content is a scene under BOX_EDITOR_UI drawn by
 * BOX_EDITOR_VIEW at BOX_VIEW_ORDER_EDITOR: over the game views, under the console.
 *
 * C creates the scene, camera, view and the panel docked to the surface; the panel shows the
 * template BOX_EDITOR_TEMPLATE of BOX_EDITOR_SCRIPT_PATH, which reloads when the file changes.
 * Closing deletes the view and keeps the content, so the editor opens again quickly.
 */

#ifndef BOX_EDITOR_H
#define BOX_EDITOR_H

#include "types.h"

#ifdef BOX_EDITOR_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Name of the root entity of the editor content and its view */
#define BOX_EDITOR_SCOPE "editor"

/** Path of the scene root of the editor content */
#define BOX_EDITOR_UI "editor.ui"

/** Path of the view of the editor content while it is open */
#define BOX_EDITOR_VIEW "editor.view"

/** Path of the layout root that fills the surface */
#define BOX_EDITOR_SCREEN "editor.ui.screen"

/** Path of the panel, the instance of BOX_EDITOR_TEMPLATE */
#define BOX_EDITOR_PANEL "editor.ui.screen.panel"

/** The script of the editor content, found in the working directory or an ancestor of it */
#define BOX_EDITOR_SCRIPT_PATH "etc/editor/editor.flecs"

/** Path of the managed script entity of BOX_EDITOR_SCRIPT_PATH */
#define BOX_EDITOR_SCRIPT "box.editor.script"

/** Path of the template of the script that the panel instantiates */
#define BOX_EDITOR_TEMPLATE "box.editor.EditorPanel"

/** Side of the surface the editor panel docks to; scripts write Right or Left */
ECS_ENUM(EditorSide, {
    EditorSideRight = 0,    /**< Against the right edge, away from the console */
    EditorSideLeft = 1      /**< Against the left edge, under the console when it is open */
});

/** The editor singleton: whether it is open and where its panel sits.
 *
 * A change applies in the next frame. The panel is the full surface height and width times the
 * surface width, clamped to [0, 1], and follows the surface when it is resized.
 *
 * The panel is an InputBlocker: the pointer over it goes to the editor, not the game. It takes
 * the keyboard only while one of its widgets has the focus; closing clears a focus inside it.
 */
ECS_STRUCT(Editor, {
    bool open;          /**< Shown, the edit command flips it */
    float width;        /**< Part of the surface width the panel covers, default 0.25 */
    EditorSide side;    /**< Side the panel docks to, default EditorSideRight */
});

/** Register the box.editor module.
 *
 * Creates the Editor singleton, closed. The content, its view and the script are created when
 * the editor first opens.
 *
 * @param world The world
 */
void BoxEditorImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
