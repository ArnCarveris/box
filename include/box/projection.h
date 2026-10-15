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

/** Added while the projection of a ProjectedAnchor is not visible.
 *
 * Behind the camera, past its far plane, off view or without target, view or camera. The
 * entity keeps its last Position and the world transforms of its subtree collapse to a point.
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
