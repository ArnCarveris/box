/**
 * @file editor/editor_internal.h
 * @brief Private state of the box.editor module and the functions its parts share.
 */

#ifndef BOX_EDITOR_INTERNAL_H
#define BOX_EDITOR_INTERNAL_H

#include <box.h>

/** Where the value of a component of a script entity comes from */
typedef enum box_editor_source_t {
    BoxEditorSourceNone = 0,  /**< No script statement assigns the component */
    BoxEditorSourceLiteral,   /**< The statement assigns literal values only */
    BoxEditorSourceComputed   /**< The statement assigns a computed expression */
} box_editor_source_t;

/** One instance opened with editor.open and what was hidden for it */
typedef struct box_editor_level_t {
    ecs_entity_t instance; /**< The opened instance */
    ecs_vec_t hidden;      /**< ecs_entity_t, the entities Hidden was added to */
} box_editor_level_t;

/** The drag of a gizmo handle in progress */
typedef struct box_editor_grab_t {
    bool active;          /**< A handle is held */
    HandleMode mode;      /**< What the handle changes */
    int32_t axes;         /**< Axes bits of the handle */
    int32_t index;        /**< Control point of a spline handle */
    ecs_entity_t target;  /**< Entity the drag changes */
    vec3 origin;          /**< World position of the target when it was grabbed */
    vec3 axis[3];         /**< World axes of the gizmo */
    vec3 normal;          /**< Normal of the plane the cursor is projected on */
    vec3 start;           /**< Point or axis position the drag started at */
    float angle;          /**< Angle on the rotation plane the drag started at */
    vec2 cursor;          /**< Normalized cursor the drag started at */
    Position p0;          /**< Position of the target when it was grabbed */
    Rotation r0;          /**< Rotation of the target when it was grabbed */
    Scale s0;             /**< Scale of the target when it was grabbed */
    vec3 point0;          /**< Control point when it was grabbed */
    mat4 parent_inverse;  /**< Maps world space into the space of the parent of the target */
    mat4 parent_rotation; /**< Rotation part of the parent transform */
    float size;           /**< World size of the gizmo when it was grabbed */
} box_editor_grab_t;

/** Private state of the module, a singleton */
typedef struct EditorState {
    ecs_map_t expanded;      /**< Entities whose children the tree shows */
    ecs_vec_t levels;        /**< box_editor_level_t, the opened instances */
    ecs_vec_t systems;       /**< System entities enabled with the editor */
    ecs_vec_t captured;      /**< Input maps disabled while a field has the focus */
    ecs_query_t *roots;      /**< Entities without a parent */
    ecs_entity_t script;     /**< The overlay script */
    ecs_entity_t gizmos;     /**< The gizmo script */
    bool tree_dirty;         /**< The tree must be rebuilt */
    int32_t tree_frames;     /**< Frames since the tree was rebuilt */
    char *inspector_text;    /**< Fields of the last inspector rebuild, as one string */
    box_editor_grab_t grab;  /**< The drag in progress */
    bool capturing;          /**< The input maps are captured by a focused field */
} EditorState;

extern ECS_COMPONENT_DECLARE(EditorState);

/** Get the state of the module.
 *
 * @param world The world
 * @return The state, NULL when box.editor is not imported
 */
EditorState* box_editor_state(
    ecs_world_t *world);

/** Get the Editor singleton for writing.
 *
 * @param world The world
 * @return The singleton
 */
Editor* box_editor_get(
    ecs_world_t *world);

/** Mark the Editor singleton modified so the scripts that read it update.
 *
 * @param world The world
 */
void box_editor_modified(
    ecs_world_t *world);

/** Switch the editor on or off, leaving the scene as the editor found it.
 *
 * @param world The world
 * @param enabled Whether the editor is on
 */
void box_editor_enable(
    ecs_world_t *world,
    bool enabled);

/** Tell whether an entity is the root of a prefab or template instance.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity was instantiated from a prefab or a template
 */
bool box_editor_is_root(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Resolve the entity a selection lands on: the instance root unless it is opened.
 *
 * @param world The world
 * @param e The entity that was clicked or picked from the tree
 * @return The entity to select
 */
ecs_entity_t box_editor_resolve(
    ecs_world_t *world,
    ecs_entity_t e);

/** Select an entity, resolving instance children to their root.
 *
 * @param world The world
 * @param e The entity, 0 to clear the selection
 * @return The entity that was selected
 */
ecs_entity_t box_editor_select(
    ecs_world_t *world,
    ecs_entity_t e);

/** Open an instance: show it and hide the rest of the scene.
 *
 * @param world The world
 * @param e The instance root
 * @return true when the instance was opened
 */
bool box_editor_open(
    ecs_world_t *world,
    ecs_entity_t e);

/** Close the innermost opened instance and show what it hid.
 *
 * @param world The world
 * @return true when an instance was closed
 */
bool box_editor_close(
    ecs_world_t *world);

/** Close every opened instance.
 *
 * @param world The world
 */
void box_editor_close_all(
    ecs_world_t *world);

/** Refresh the flags of the Editor singleton that describe the selection.
 *
 * @param world The world
 */
void box_editor_describe(
    ecs_world_t *world);

/** Append the children of an entity, or the entities of the root, to a vector.
 *
 * @param world The world
 * @param parent The parent, 0 for the root of the world
 * @param out Vector of ecs_entity_t the children are appended to
 */
void box_editor_children(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_vec_t *out);

/** Kind of an entity, for its icon in the tree.
 *
 * @param world The world
 * @param e The entity
 * @return The kind
 */
EditorKind box_editor_kind(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Show or hide the children of an entity in the tree.
 *
 * @param world The world
 * @param e The entity
 * @param expanded Whether its children are shown
 */
void box_editor_expand(
    ecs_world_t *world,
    ecs_entity_t e,
    bool expanded);

/** Expand every ancestor of an entity so its row is visible.
 *
 * @param world The world
 * @param e The entity
 */
void box_editor_reveal(
    ecs_world_t *world,
    ecs_entity_t e);

/** Rebuild the rows of the tree when they are dirty or stale.
 *
 * @param world The world
 * @param state The module state
 */
void box_editor_tree_update(
    ecs_world_t *world,
    EditorState *state);

/** Rebuild the fields of the inspector when the selection changed.
 *
 * @param world The world
 * @param state The module state
 */
void box_editor_inspector_update(
    ecs_world_t *world,
    EditorState *state);

/** Assign a value written as text to a member of a component of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 * @param member Dotted member path, empty for the whole component
 * @param value The value as script text
 * @return true when the value was parsed and assigned
 */
bool box_editor_assign(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component,
    const char *member,
    const char *value);

/** Apply the lines the bound text inputs submitted and capture the input maps.
 *
 * @param world The world
 * @param state The module state
 */
void box_editor_bindings_update(
    ecs_world_t *world,
    EditorState *state);

/** Give the input maps back that a focused field captured.
 *
 * @param world The world
 * @param state The module state
 */
void box_editor_release_maps(
    ecs_world_t *world,
    EditorState *state);

/** Find what the script that declares an entity assigns to one of its components.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 * @return Where the value comes from
 */
box_editor_source_t box_editor_source(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component);

/** Tell whether a component of an entity may be edited.
 *
 * A component of an entity a template body declares is editable when that
 * body assigns it literal values or does not assign it at all.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 * @return true when the editor may write the component
 */
bool box_editor_editable(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component);

/** Write the current value of a component back into the script that declares it.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 * @return true when the script was updated
 */
bool box_editor_write_through(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component);

/** Draw the gizmo of the tool and the handles of a selected spline.
 *
 * @param world The world
 * @param state The module state
 */
void box_editor_gizmo_draw(
    ecs_world_t *world,
    EditorState *state);

/** Start dragging a handle.
 *
 * @param world The world
 * @param handle The handle entity
 * @param hit The press that grabbed it
 * @return true when the handle belongs to a gizmo of the selection
 */
bool box_editor_grab(
    ecs_world_t *world,
    ecs_entity_t handle,
    const PickHit *hit);

/** Move the selection with a drag of the held handle.
 *
 * @param world The world
 * @param handle The handle entity
 * @param hit The drag, whose cursor is where the pointer is now
 * @return true when the selection changed
 */
bool box_editor_pull(
    ecs_world_t *world,
    ecs_entity_t handle,
    const PickHit *hit);

/** End the drag of a handle, writing the change through to its script.
 *
 * @param world The world
 * @return true when a handle was held
 */
bool box_editor_drop(
    ecs_world_t *world);

/** Show the guide curve of a spline over the scene, or hide it.
 *
 * @param world The world
 * @param spline The spline entity, 0 to hide the guide
 * @param transform World transform of the spline entity, NULL when hiding
 */
void box_editor_spline_guide(
    ecs_world_t *world,
    ecs_entity_t spline,
    const mat4 *transform);

/** Select a control point of the selected spline.
 *
 * @param world The world
 * @param index The point, -1 for none
 * @return true when the selection has a spline with that point
 */
bool box_editor_point(
    ecs_world_t *world,
    int32_t index);

/** Add a control point after the selected one, or at the end.
 *
 * @param world The world
 * @return true when a point was added
 */
bool box_editor_point_add(
    ecs_world_t *world);

/** Remove the selected control point.
 *
 * @param world The world
 * @return true when a point was removed
 */
bool box_editor_point_remove(
    ecs_world_t *world);

/** Register the functions of the editor scope scripts call.
 *
 * @param world The world
 * @param module The module entity
 */
void box_editor_functions_init(
    ecs_world_t *world,
    ecs_entity_t module);

#endif
