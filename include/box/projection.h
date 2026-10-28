/**
 * @file box/projection.h
 * @brief Positions an entity of one scene at the projection of an entity of another scene.
 *
 * The only cross scene reference: world anchored screen space UI such as nameplates and
 * markers follows an entity of a 3D scene as its camera sees it through a view.
 */

#ifndef BOX_PROJECTION_H
#define BOX_PROJECTION_H

#include "types.h"

#ifdef BOX_PROJECTION_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Places the entity at the projection of a target entity through the camera of a view.
 *
 * Every frame Position x and y become surface pixels of the view, origin at the center and y
 * up (a UI scene with an ortho camera at ortho_scale 1), z is kept, and the WorldTransform of
 * the entity and its subtree follow before the render extract.
 */
ECS_STRUCT(ProjectedAnchor, {
    ecs_entity_t target; /**< Entity whose world position is projected */
    ecs_entity_t view;   /**< View whose camera projects the target */
    vec3 offset;         /**< World units added to the position of the target */
});

/** Makes a ProjectedAnchor follow and size to the projection of a box around its target.
 *
 * Every frame the 8 corners of the box are projected into ProjectedBounds and the entity goes
 * to the top center of that rectangle plus offset pixels. Elements are centered on Position:
 * a bar of height h sits 6 pixels above the box with offset {0, 6 + h / 2}.
 *
 * The part of the box behind the near plane is clipped away, the corners in front and where
 * edges cross the plane are projected. The entity is ProjectedHidden when nothing is left, all
 * of it is past the far plane or the rectangle misses the surface.
 */
ECS_STRUCT(ProjectedBox, {
    vec3 min;        /**< Smallest corner, world units along the world axes from the target
                      *   position plus the anchor offset; target rotation and scale ignored */
    vec3 max;        /**< Largest corner, the same way as min */
    vec2 offset;     /**< Surface pixels from the top center of the rectangle to the entity */
    bool fit_width;  /**< Make the width of the LayoutItem of the entity Fixed at the width of
                      *   the rectangle; its tree is laid out again the same frame, so Grow
                      *   children split it and their world transforms are final */
    bool fit_height; /**< Make the height of the LayoutItem Fixed at the rectangle, as width */
});

/** Projection of the ProjectedBox of an entity, written by the module every frame.
 *
 * Surface pixels with the origin at the center and y up, as the Position of a ProjectedAnchor,
 * not clamped to the surface. Added with ProjectedBox, written without OnSet, kept while hidden.
 */
ECS_STRUCT(ProjectedBounds, {
    float left;   /**< Smallest x of the projected box */
    float right;  /**< Largest x of the projected box */
    float top;    /**< Largest y of the projected box */
    float bottom; /**< Smallest y of the projected box */
});

/** Added while the projection of a ProjectedAnchor is not visible.
 *
 * Behind the camera, past its far plane, off view or without target, view or camera; for a
 * ProjectedBox when all of the box is. The entity keeps its last Position and the world
 * transforms of its subtree collapse to a point.
 */
extern ECS_TAG_DECLARE(ProjectedHidden);

/** Test whether an entity or one of its ChildOf ancestors has a ProjectedAnchor.
 *
 * Anchored subtrees move every frame by projection, so the bake does not capture them, the
 * same as shapes with DynamicTransform or NoBake.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is anchored or below an anchored entity
 */
bool box_projection_anchored(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Register the box.projection module.
 *
 * @param world The world
 */
void BoxProjectionImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
