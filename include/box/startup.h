/**
 * @file box/startup.h
 * @brief The startup overlay: while an app starts and box works on a blocking task, a panel in
 * the middle of the surface lists the tasks of InProgressTask with their progress.
 *
 * A long first load, such as the cold shader compiles of a D3D device, then reads as progress
 * instead of a frozen or empty window. The overlay closes once no blocking task is left and
 * stays closed, so later background work such as a rebake after an edit does not bring it back.
 *
 * Like the fps panel it is a scene under BOX_STARTUP_UI drawn by BOX_STARTUP_VIEW at
 * BOX_VIEW_ORDER_STARTUP, whose panel is the template BOX_STARTUP_TEMPLATE of the script
 * BOX_STARTUP_SCRIPT_PATH, reloaded when it changes. The script reads InProgressTask.
 */

#ifndef BOX_STARTUP_H
#define BOX_STARTUP_H

#include "types.h"
#include "task.h"

#ifdef BOX_STARTUP_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Name of the root entity of the overlay content and its view */
#define BOX_STARTUP_SCOPE "startup"

/** Path of the scene root of the overlay content while it is open */
#define BOX_STARTUP_UI "startup.ui"

/** Path of the view of the overlay content while it is open */
#define BOX_STARTUP_VIEW "startup.view"

/** Path of the layout root that fills the surface */
#define BOX_STARTUP_SCREEN "startup.ui.screen"

/** Path of the panel in the middle of the surface, the instance of BOX_STARTUP_TEMPLATE */
#define BOX_STARTUP_PANEL "startup.ui.screen.panel"

/** The script of the overlay content, found in the working directory or an ancestor of it */
#define BOX_STARTUP_SCRIPT_PATH "etc/startup/startup.flecs"

/** Path of the managed script entity of the overlay script */
#define BOX_STARTUP_SCRIPT "box.startup.script"

/** Path of the template of the script that the panel instantiates. Its children are heading,
 * a Text, and task_0 to task_N, one per task of InProgressTask in its order, whose children
 * name and progress are Texts. */
#define BOX_STARTUP_TEMPLATE "box.startup.StartupWidget"

/** Frames in a row without a blocking task that end the startup: the overlay closes, or never
 * opens when no blocking task came in that time once the app started */
#define BOX_STARTUP_FRAMES (10)

/** The startup overlay singleton.
 *
 * The overlay may open in a run of box_app_run with a window that writes no capture (App.out),
 * or when enabled is set. It opens while a task of InProgressTask is blocking, and once none
 * is left it closes and the startup is finished.
 */
ECS_STRUCT(StartupOverlay, {
    bool enabled;       /**< The overlay may open in any world, such as one that tests it */
    bool disabled;      /**< The overlay never opens, an app that shows its own progress */
    bool open;          /**< The overlay is shown, written by box.startup */
    bool finished;      /**< The startup is over and the overlay does not open again, written
                         *   by box.startup */
});

/** Set the script of the overlay content, for a test that loads its own: a path relative to
 * the working directory or one of its ancestors, as BOX_STARTUP_SCRIPT_PATH is.
 *
 * Applies the next time the overlay opens without its script loaded.
 *
 * @param world The world
 * @param path The script, NULL for BOX_STARTUP_SCRIPT_PATH
 */
void box_startup_script(
    ecs_world_t *world,
    const char *path);

/** Register the box.startup module.
 *
 * Creates the StartupOverlay singleton, closed. The content, its view and the script are
 * created when the overlay first opens.
 *
 * @param world The world
 */
void BoxStartupImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
