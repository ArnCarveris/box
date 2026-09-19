/**
 * @file box/ui.h
 * @brief Ui component: the root of a retained UI tree and the view it draws in.
 *
 * An entity with a Ui component roots a UI tree: the entity and everything
 * below it is drawn by a view with an orthographic camera in logical pixels,
 * composited over the views of the 3D scene. The tree reuses Position, Rotation,
 * Scale and the 2D shapes, so nothing in the geometry path is UI specific.
 *
 * The view shows Ui.height logical pixels from top to bottom and as many across
 * as its aspect ratio asks for, with the origin in the middle of the view, x to
 * the right and y up, which is the orientation the rest of the engine draws in.
 * A height of zero follows the surface: the view then shows the points of the
 * window divided by Ui.scale, so the tree keeps its size in points when the
 * window is resized and when it moves to a display with another resolution
 * scale.
 *
 * Depth inside a tree comes from the order of the nodes in the walk of the tree
 * and not from their positions: every node is drawn over its parent and over
 * the subtrees of its earlier siblings, so a tree needs no authored z and
 * cannot z fight. The bias is a fixed step per node toward the camera, which
 * leaves room for BOX_UI_NODE_MAX nodes in one tree.
 *
 * A tree is walked again when it is marked: setting Ui marks it, and so does
 * every change of the tree itself. Reordering the children of a node with
 * ecs_set_child_order() raises no event, so a tree that was reordered is marked
 * by setting Ui on its root again.
 *
 * Every node of a tree joins the layer of its tree, which is what keeps the two
 * apart: the UI view draws the members of that layer only, and every other view
 * hides them. A world whose only views are UI views still draws its active
 * camera, so a scene does not lose its image when it gains a UI.
 */

#ifndef BOX_UI_H
#define BOX_UI_H

#include "types.h"

#ifdef BOX_UI_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Roots a UI tree, which a view with an orthographic camera draws */
ECS_STRUCT(Ui, {
    ecs_entity_t view; /**< View that draws the tree, 0 creates one with a camera */
    float height;      /**< Logical pixels the view shows vertically, 0 follows the surface */
    float scale;       /**< Surface points one logical pixel covers, default 1 */
    int32_t priority;  /**< Order of a created view in the frame, default 1000 */
});

/** Order in the frame a created UI view renders at, after the views of a scene */
#define BOX_UI_PRIORITY (1000)

/** Surface points one logical pixel of a tree covers by default */
#define BOX_UI_SCALE (1.0f)

/** Distance between the camera of a created UI view and the plane of the tree */
#define BOX_UI_DEPTH (1000.0f)

/** Near plane of the camera of a created UI view */
#define BOX_UI_NEAR (1.0f)

/** Far plane of the camera of a created UI view */
#define BOX_UI_FAR (2001.0f)

/** World units one node of a tree is drawn in front of the node before it */
#define BOX_UI_STEP (0.01f)

/** Nodes one tree can hold before its deepest node reaches the near plane */
#define BOX_UI_NODE_MAX (99900)

/** Register the box.ui module.
 *
 * @param world The world
 */
void BoxUiImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
