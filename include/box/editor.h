/**
 * @file box/editor.h
 * @brief The in process editor: a panel docked to a side of the surface whose UI is flecs script.
 *
 * The editor is opened, closed and resized by changing the Editor singleton, from C, a script,
 * the REST API or the edit command of the terminal (edit, edit on, edit off). Nothing else
 * opens it.
 *
 * While open, its content is UI in its own scene under BOX_EDITOR_UI, drawn by its own view
 * BOX_EDITOR_VIEW without clear, lighting, culling or effects at BOX_VIEW_ORDER_EDITOR: over the
 * views of the game and under the console when the console is open.
 *
 * C creates the scene root, its camera, the screen that fills the surface and the panel, which
 * it keeps docked to Editor.side at Editor.width of the surface. What the panel shows is the
 * template box.editor.EditorPanel of etc/editor/editor.flecs (script module box.editor, built
 * from the box.widgets library and theme), loaded when the editor first opens and loaded again
 * when the file changes on disk, as the other files box watches (FileWatch).
 *
 * Closing the editor deletes its view and keeps the content, so it opens again without
 * loading the script or instantiating the template again. Content that was deleted is created
 * again when the editor opens or the next frame it is open.
 *
 * The panel is an InputBlocker: the pointer events over it (hover, presses, clicks, the wheel)
 * go to the editor and not to the game views under it, the game keeps the mouse elsewhere. The
 * editor takes the keyboard only while one of its widgets has the focus; closing the editor
 * clears a focus inside it. While the console is open it keeps its own input rules.
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
 * A change applies in the next frame. The panel takes the full height of the surface and width
 * times its width, follows the surface when it is resized; a width of zero or less is a panel
 * of zero width, one or more fills the surface.
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
