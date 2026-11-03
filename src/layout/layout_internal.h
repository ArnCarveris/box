/**
 * @file layout/layout_internal.h
 * @brief Private types of the box.layout module: the dirty marks and the tree solver.
 */

#ifndef BOX_LAYOUT_INTERNAL_H
#define BOX_LAYOUT_INTERNAL_H

#include "layout_text.h"

/** One element of a layout tree, stored in breadth first order */
typedef struct {
    ecs_entity_t e;     /**< The element */
    int32_t first;      /**< Index of the first child in the tree */
    int32_t count;      /**< Number of children in the flow of the element */
    int32_t floats;     /**< Number of floating children, after the others */
    int32_t parent;     /**< Index of the parent in the tree, -1 for the root */
    int32_t float_root; /**< Index of the nearest floating element at or above, -1 for none */
    Layout layout;      /**< Layout of the element, zero when it has none */
    LayoutItem item;    /**< LayoutItem of the element, fit on both axes when it has none */
    LayoutScroll scroll; /**< LayoutScroll of the element, zero when it has none */
    LayoutFloat fl;     /**< LayoutFloat of the element, zero when it does not float */
    float shape[2];     /**< Size of the shape or text of the element */
    float pos[3];       /**< Position the element had */
    float base[2];      /**< Fit size within min and max, what the element adds to its parent */
    float cmin[2];      /**< Narrowest the content of the element allows, within min and max */
    float longest;      /**< Widest word of the text of the element, its narrowest width */
    float size[2];      /**< Computed size */
    float at[2];        /**< Computed position in the space of the parent */
    float content[2];   /**< Size of the content of a scrolling element, with padding */
    float offset[2];    /**< Scroll offset the layout applied, clamped to the content */
    float anchor[2];    /**< Size of the anchor of a floating element */
    bool has_layout;    /**< The element has a Layout */
    bool has_item;      /**< The element has a LayoutItem */
    bool has_pos;       /**< The element has a Position */
    bool has_rect;      /**< The element has a Rect */
    bool rect_input;    /**< The Rect is the input of the element, not written */
    bool has_text;      /**< The element is a text that takes the width of the layout */
    bool has_scroll;    /**< The element has a LayoutScroll and a Layout */
    bool is_float;      /**< The element floats, out of the flow of its parent */
    bool placed;        /**< The layout writes the position of the element */
    bool frozen;        /**< The grow share of the element hit a bound */
} box_layout_node_t;

/** A floating element to place, with its depth in the hierarchy */
typedef struct {
    int32_t depth;  /**< Number of ancestors */
    ecs_entity_t e; /**< The element */
} box_layout_placing_t;

/** State of the layout module, shared by its observers and its system */
typedef struct {
    ecs_vec_t marks;              /**< Elements changed since the last layout */
    ecs_vec_t roots;              /**< Roots of the trees to lay out this frame */
    ecs_vec_t nodes;              /**< Elements of the tree being laid out */
    ecs_vec_t floating;           /**< Floating children of the element being collected */
    ecs_vec_t scrolls;            /**< Elements whose LayoutScroll was set since the last layout */
    ecs_vec_t shifts;             /**< Of those, the ones whose children only move */
    ecs_vec_t floats;             /**< Elements with LayoutFloat */
    ecs_vec_t ordered;            /**< Parents that got OrderedChildren since the last layout,
                                   *   with their children */
    ecs_vec_t placing;            /**< Floats being placed, sorted by depth */
    ecs_map_t scrolled;           /**< Laid out scrolling element to the axes it scrolled */
    ecs_map_t anchor_sizes;       /**< Float to the anchor size it got, two packed floats */
    ecs_map_t parents;            /**< Element laid out through Parent to its layout parent */
    ecs_map_t warned;             /**< Elements warned about a parent without Layout, until fixed */
    ecs_vec_t untagged;           /**< Children collect found without the child tag */
    ecs_entity_t child_tag;       /**< Tag of the children of a Layout the change query watches */
    const ecs_table_t *child_table; /**< Table of the last child collect checked */
    bool child_prefab;            /**< That table is a prefab, its children are skipped */
    bool child_kept;              /**< That table has Parent, its children are kept */
    bool child_tagged;            /**< That table has the child tag */
    ecs_query_t *moved;           /**< Change query of the Position of tagged children */
    box_layout_text_t text;       /**< Hooks of the texts, zero without box.text */
    bool has_text;                /**< The tree being laid out has a text to wrap */
    bool writing;                 /**< The layout is writing its results */
    ecs_entity_t writing_rect;    /**< Element whose Rect the layout is writing, 0 for none */
} box_layout_state_t;

/** Singleton that owns the state of the layout module, freed after every observer ran */
typedef struct LayoutRegistry {
    box_layout_state_t *state; /**< The state */
} LayoutRegistry;

/** Create the change query of the Position of tagged children once the first child is tagged.
 *
 * Only tables the query matches track changes, so Position sets elsewhere cost nothing.
 *
 * @param world The world
 * @param state The state of the layout module
 */
void box_layout_observe_children(
    ecs_world_t *world,
    box_layout_state_t *state);

/** Place the floating elements against their anchors, outer floats first.
 *
 * Runs after the trees are laid out, every frame, so a float follows an anchor that moved,
 * scrolled or resized. A float whose anchor in another tree changed size is laid out again.
 *
 * @param world The world
 * @param state The state of the layout module
 */
void box_layout_floats(
    ecs_world_t *world,
    box_layout_state_t *state);

/** Move the children of a scrolling element whose offset changed, without measuring.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param e The element
 */
void box_layout_scroll_shift(
    ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t e);

/** Find the root of the layout tree of an element.
 *
 * @param world The world
 * @param e The element
 * @return The root, 0 when the element is not laid out
 */
ecs_entity_t box_layout_root(
    const ecs_world_t *world,
    ecs_entity_t e);

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
