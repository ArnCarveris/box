/**
 * @file ui/ui_internal.h
 * @brief Private components and helpers of the box.ui module.
 */

#ifndef BOX_UI_INTERNAL_H
#define BOX_UI_INTERNAL_H

#include <box.h>
#include <string.h>
#include "../render/render_internal.h"

/** View, camera and layer of a UI tree, added with Ui */
typedef struct UiImpl {
    ecs_entity_t view;   /**< View that draws the tree */
    ecs_entity_t camera; /**< Camera of the view, 0 while the tree has no view */
    ecs_entity_t layer;  /**< Layer every node of the tree joins */
    int32_t count;       /**< Nodes the last walk of the tree numbered */
    float height;        /**< Logical pixels the view showed after the last update */
    bool owned;          /**< The module created the view and its camera */
    bool dirty;          /**< The tree must be walked again */
} UiImpl;

/** Place of an entity in the UI tree that holds it */
typedef struct UiNode {
    ecs_entity_t root; /**< Root of the tree, the entity that has Ui */
    int32_t order;     /**< Index of the node in the walk of its tree */
} UiNode;

/** Module wide registry of box.ui, stored as a singleton */
typedef struct UiState {
    ecs_vec_t roots;    /**< ecs_entity_t roots of one update, collected first */
    ecs_vec_t stack;    /**< ecs_entity_t nodes a walk still has to visit */
    ecs_vec_t nodes;    /**< ecs_entity_t children of the node a walk visits */
    int32_t trees;      /**< Trees the world holds, zero skips the observers */
} UiState;

extern ECS_COMPONENT_DECLARE(UiImpl);
extern ECS_COMPONENT_DECLARE(UiNode);
extern ECS_COMPONENT_DECLARE(UiState);

/** Direct pointer to the registry singleton, never a deferred copy.
 *
 * @param world The world
 * @return The registry
 */
UiState* box_ui_state(
    ecs_world_t *world);

/** Root of the UI tree an entity belongs to.
 *
 * @param world The world
 * @param e The entity
 * @return The root, 0 when the entity is not in a tree
 */
ecs_entity_t box_ui_root(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Mark the tree of an entity so the next update walks it again.
 *
 * @param world The world
 * @param e An entity of the tree, the root or one of its descendants
 */
void box_ui_mark(
    ecs_world_t *world,
    ecs_entity_t e);

/** Give a tree its view, its camera and its layer, or bind the authored view.
 *
 * @param world The world
 * @param ui The Ui component of the root
 * @param impl The private state of the root
 */
void box_ui_bind(
    ecs_world_t *world,
    Ui *ui,
    UiImpl *impl);

/** Release the view, camera and layer a tree owns.
 *
 * @param world The world, the stage when this runs in an observer
 * @param impl The private state of the root
 */
void box_ui_unbind(
    ecs_world_t *world,
    UiImpl *impl);

/** Update the views and the node order of every tree of the world.
 *
 * @param world The world, the stage when this runs in a system
 */
void box_ui_update(
    ecs_world_t *world);

/** Register the observers that mark a tree when it changes.
 *
 * @param world The world
 */
void box_ui_tree_init(
    ecs_world_t *world);

/** Point the camera of a tree at its plane and size it in logical pixels.
 *
 * @param world The world
 * @param ui The Ui component of the root
 * @param impl The private state of the root
 */
void box_ui_sync(
    ecs_world_t *world,
    const Ui *ui,
    UiImpl *impl);

/** Drop the node data of an entity and its descendants, which left their tree.
 *
 * @param world The world, the stage when this runs in an observer
 * @param e The entity that left the tree
 */
void box_ui_drop(
    ecs_world_t *world,
    ecs_entity_t e);

/** Walk the tree of a root, number its nodes and join them to its layer.
 *
 * Numbering a node changes its table, the root included, so the walk reads the
 * private state of the root into a copy and writes the result back at the end.
 *
 * @param world The world
 * @param e The root of the tree
 */
void box_ui_walk(
    ecs_world_t *world,
    ecs_entity_t e);

#endif
