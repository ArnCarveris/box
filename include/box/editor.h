/**
 * @file box/editor.h
 * @brief The scene editor: its state singleton and the models its script UI reads.
 *
 * The editor command sets Editor.enabled, which shows the overlay that
 * BOX_EDITOR_SCRIPT defines and turns on the system that fills EditorTree and
 * EditorInspector and draws the gizmos through box.draw.
 *
 * The script calls the functions of the editor scope to select, expand, open
 * and assign, so the widgets stay script and the engine only answers them. While
 * the editor is off its system is disabled and its script keeps no entities.
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

/** Tool the editor applies to the selection */
ECS_ENUM(EditorTool, {
    ToolSelect = 0, /**< Clicks select, no gizmo is drawn */
    ToolMove = 1,   /**< The move gizmo translates the selection */
    ToolRotate = 2, /**< The rotate gizmo turns the selection */
    ToolScale = 3   /**< The scale gizmo resizes the selection */
});

/** Kind of an entity, which picks the icon of its row in the tree */
ECS_ENUM(EditorKind, {
    KindEntity = 0,   /**< An ordinary entity */
    KindModule = 1,   /**< A module */
    KindComponent = 2,/**< A component with data */
    KindTag = 3,      /**< A component without data */
    KindTemplate = 4, /**< A script template */
    KindPrefab = 5,   /**< A prefab */
    KindSystem = 6,   /**< A system */
    KindQuery = 7,    /**< A query */
    KindObserver = 8, /**< An observer */
    KindInstance = 9  /**< The root of a prefab or template instance */
});

/** Widget a field of the inspector is edited with */
ECS_ENUM(EditorFieldKind, {
    FieldHeader = 0,  /**< Name of a component, above its members */
    FieldText = 1,    /**< A string, edited with a text input */
    FieldNumber = 2,  /**< A number, edited with a text input */
    FieldBool = 3,    /**< A boolean, edited with a checkbox */
    FieldEnum = 4,    /**< An enum, edited with a dropdown of its constants */
    FieldEntity = 5,  /**< An entity, edited as a path in a text input */
    FieldValue = 6,   /**< Any other value, shown as text and not edited */
    FieldTag = 7      /**< A tag or a pair, shown by name */
});

/** State of the editor, a singleton the command switches and the UI reads */
ECS_STRUCT(Editor, {
    bool enabled;           /**< The overlay is shown and the systems run */
    EditorTool tool;        /**< Tool applied to the selection */
    ecs_entity_t selected;  /**< Selected entity, 0 for none, written by editor.select */
    ecs_entity_t opened;    /**< Instance opened with editor.open, 0 for the whole scene */
    int32_t point;          /**< Selected control point of a spline, -1 for none */
    bool instance;          /**< The selection is an instance root Open applies to */
    bool spline;            /**< The selection carries a Spline the spline editor edits */
    bool transform;         /**< Position, Rotation and Scale of the selection are editable */
    int32_t depth;          /**< Instances opened inside each other, 0 for none */
});

/** One visible row of the tree view */
ECS_STRUCT(EditorNode, {
    ecs_entity_t entity; /**< The entity of the row */
    int64_t id;          /**< Number of the entity, which names the row of the tree */
    char *name;          /**< Name of the entity, or its id when it has none */
    int32_t depth;       /**< Ancestors of the entity, indents the row */
    EditorKind kind;     /**< What the entity is, picks the icon */
    bool expanded;       /**< The children of the entity are shown below it */
    bool children;       /**< The entity has children */
    bool selected;       /**< The entity is the selection */
});

/** The rows of the tree view, written by the engine while the editor is on */
ECS_STRUCT(EditorTree, {
    ecs_vec(EditorNode) rows; /**< Visible rows, in tree order */
    int32_t version;          /**< Incremented on every rebuild */
});

/** One row of the property grid of the inspector */
ECS_STRUCT(EditorField, {
    ecs_entity_t component;       /**< Component the field belongs to */
    char *member;                 /**< Dotted member path in the component, empty for a header */
    char *label;                  /**< Text shown left of the widget */
    EditorFieldKind kind;         /**< Widget the field is edited with */
    char *value;                  /**< Value as text */
    ecs_vec(char*) options;       /**< Constants of an enum field */
    int32_t option;               /**< Index of the value among the options */
    bool editable;                /**< The value may be changed */
});

/** The property grid of the selection, written by the engine while the editor is on */
ECS_STRUCT(EditorInspector, {
    ecs_entity_t entity;         /**< Entity the fields belong to, 0 for none */
    char *name;                  /**< Name of that entity */
    ecs_vec(EditorField) fields; /**< Rows of the grid, headers before their members */
    int32_t version;             /**< Incremented on every rebuild */
});

/** What a gizmo handle does when it is dragged, set by the gizmo templates */
ECS_ENUM(HandleMode, {
    HandleMove = 0,   /**< Translate along the axes */
    HandleRotate = 1, /**< Rotate about the axes */
    HandleScale = 2,  /**< Scale along the axes */
    HandlePoint = 3   /**< Move the control point of a spline */
});

/** Handle of a gizmo: what dragging it changes */
ECS_STRUCT(GizmoHandle, {
    HandleMode mode; /**< What the handle changes */
    int32_t axes;    /**< Bit 1 for x, 2 for y, 4 for z; all three for free handles */
    int32_t index;   /**< Control point of a spline handle */
});

/** Binds a text input of the inspector to a member: the line it submits is assigned.
 *
 * The overlay script puts it on the input of a field. The engine assigns every
 * submitted line to the member on the selected entity and shows the value that
 * resulted, and disables the input maps while the input has the focus.
 */
ECS_STRUCT(EditorBinding, {
    ecs_entity_t component; /**< Component the input edits */
    char *member;           /**< Dotted member path inside the component */
});

/** Script that defines the editor overlay, loaded at import into box.editor.ui */
#define BOX_EDITOR_SCRIPT "etc/editor/editor.flecs"

/** Script that defines the gizmo templates, loaded at import into box.editor.gizmos */
#define BOX_EDITOR_GIZMOS "etc/editor/gizmos.flecs"

/** Rows the tree view shows at most */
#define BOX_EDITOR_TREE_MAX (2000)

/** Children of one entity the tree view shows at most */
#define BOX_EDITOR_CHILDREN_MAX (400)

/** Frames between two rebuilds of the tree that nothing asked for */
#define BOX_EDITOR_TREE_INTERVAL (30)

/** Fraction of the distance to the camera a gizmo spans */
#define BOX_EDITOR_GIZMO_SIZE (0.15f)

/** Register the box.editor module.
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
