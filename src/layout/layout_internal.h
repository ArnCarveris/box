/**
 * @file layout/layout_internal.h
 * @brief Private types of the box.layout module: the dirty marks and the tree solver.
 */

#ifndef BOX_LAYOUT_INTERNAL_H
#define BOX_LAYOUT_INTERNAL_H

#include <box.h>

/** One element of a layout tree, stored in breadth first order */
typedef struct {
    ecs_entity_t e;     /**< The element */
    int32_t first;      /**< Index of the first child in the tree */
    int32_t count;      /**< Number of children the element lays out */
    Layout layout;      /**< Layout of the element, zero when it has none */
    LayoutItem item;    /**< LayoutItem of the element, fit on both axes when it has none */
    float shape[2];     /**< Size of the shape or text of the element */
    float pos[3];       /**< Position the element had */
    float fit[2];       /**< Size of the content plus padding, or of the shape */
    float base[2];      /**< Fit size within min and max, what the element adds to its parent */
    float size[2];      /**< Computed size */
    float at[2];        /**< Computed position in the space of the parent */
    bool has_layout;    /**< The element has a Layout */
    bool has_item;      /**< The element has a LayoutItem */
    bool has_pos;       /**< The element has a Position */
    bool has_rect;      /**< The element has a Rect */
    bool rect_input;    /**< The Rect is the input of the element, not written */
    bool placed;        /**< The layout writes the position of the element */
    bool frozen;        /**< The grow share of the element hit a bound */
} box_layout_node_t;

/** State of the layout module, shared by its observers and its system */
typedef struct {
    ecs_vec_t marks;              /**< Elements changed since the last layout */
    ecs_vec_t roots;              /**< Roots of the trees to lay out this frame */
    ecs_vec_t nodes;              /**< Elements of the tree being laid out */
    ecs_entity_t text_bounds;     /**< The TextBounds component, 0 until it exists */
    bool writing;                 /**< The layout is writing its results */
} box_layout_state_t;

/** Singleton that owns the state of the layout module, freed after every observer ran */
typedef struct LayoutRegistry {
    box_layout_state_t *state; /**< The state */
} LayoutRegistry;

/** Lay out the tree under a root and write the results to its elements.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param root The root of the tree
 */
void box_layout_tree(
    ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t root);

#endif
