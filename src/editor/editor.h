/**
 * @file editor/editor.h
 * @brief Private state of the box.editor module and the functions its sources share.
 */

#ifndef BOX_EDITOR_INTERNAL_H
#define BOX_EDITOR_INTERNAL_H

#include <box.h>

/** Entities of the editor content, created the first time it opens */
typedef struct box_editor_ui_t {
    ecs_entity_t root;          /**< Scene root */
    ecs_entity_t camera;        /**< Orthographic camera at one point per unit */
    ecs_entity_t screen;        /**< Layout root that fills the surface */
    ecs_entity_t panel;         /**< Panel docked to a side, instance of the script template */
} box_editor_ui_t;

/** State of the editor of a world, owned by the module */
typedef struct box_editor_t {
    ecs_entity_t module;        /**< The box.editor module */
    ecs_entity_t scope;         /**< Root entity of the content and the view */
    ecs_entity_t view;          /**< View of the content while the editor is open */
    box_editor_ui_t ui;         /**< The content */
    ecs_entity_t script;        /**< Managed script of etc/editor/editor.flecs, 0 before it loads */
    char *path;                 /**< Path the script was found at, NULL before it loads */
    int32_t watch;              /**< Id of the file watch kind of the script, 0 without box.app */
    bool reload;                /**< The script changed on disk since the last update */
    bool missing;               /**< The script was not found since the editor opened */
    bool shown;                 /**< The content is shown */
    bool relayout;              /**< Editor or surface size changed since the last sync */
    ecs_entity_t surface;       /**< Surface the content fills, 0 while the world has none */
    float width;                /**< Surface width in points the content was laid out for */
    float height;               /**< Surface height in points the content was laid out for */
    ecs_query_t *surfaces;      /**< Entities with a Surface */
} box_editor_t;

/** Private singleton that points to the editor state of a world */
typedef struct EditorHost {
    box_editor_t *ed;   /**< The state, freed when the world is deleted */
} EditorHost;

extern ECS_COMPONENT_DECLARE(EditorHost);

/** Get the root entity of the content and the view, creating it when needed.
 *
 * It is not under the module, so it is deleted with the entities of the application.
 *
 * @param world The world
 * @param ed The editor state
 * @return The root entity
 */
ecs_entity_t box_editor_scope(
    ecs_world_t *world,
    box_editor_t *ed);

/** Load etc/editor/editor.flecs when it did not load yet, and load it again when it changed.
 *
 * A file that was not found is not looked for again until the editor opens again.
 *
 * @param world The world
 * @param ed The editor state
 */
void box_editor_script_sync(
    ecs_world_t *world,
    box_editor_t *ed);

/** Create the scene, camera, screen and panel of the editor content when they do not exist.
 *
 * @param world The world
 * @param ed The editor state
 */
void box_editor_ui_build(
    ecs_world_t *world,
    box_editor_t *ed);

/** Lay the panel out on the surface and make it an instance of the script template.
 *
 * @param world The world
 * @param ed The editor state
 * @param editor The Editor singleton
 */
void box_editor_ui_sync(
    ecs_world_t *world,
    box_editor_t *ed,
    const Editor *editor);

#endif
