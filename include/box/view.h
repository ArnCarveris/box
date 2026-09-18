/**
 * @file box/view.h
 * @brief View and render target components: what a frame draws and where.
 *
 * A View binds a camera to a target and carries the toggles of the frame it
 * produces. Views render in priority order, lowest first, and a world without
 * any View renders the active camera into the surface as before.
 *
 * ClusterSettings, EnvironmentLight and MaterialLayerSettings are read from the
 * View entity first and from their singleton when the view does not carry them,
 * so a singleton is the default of every view and a component on a view
 * overrides it for that view only. BakeSettings follows the same rule through
 * the first view of the frame, because the probe bake is shared by every view.
 *
 * CullSettings follows the same rule: a view without one culls with the settings
 * of the singleton, and a world without either culls with the defaults.
 *
 * A layer is any entity an entity or a light joins with the pair (Layer, layer).
 * A view lists the layers it shows and the layers that cast its shadows; both
 * lists hold at most BOX_VIEW_LAYER_MAX entries, end at the first zero and show
 * every layer when they are empty. An entity in no layer is drawn by every view.
 */

#ifndef BOX_VIEW_H
#define BOX_VIEW_H

#include "types.h"

#ifdef BOX_VIEW_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Offscreen color and depth texture a view can render into */
ECS_STRUCT(RenderTarget, {
    int32_t width;   /**< Width in pixels */
    int32_t height;  /**< Height in pixels */
});

/** Binds a camera to a render target and decides what the frame draws */
ECS_STRUCT(View, {
    ecs_entity_t camera;  /**< Camera the view renders, a view without one is skipped */
    ecs_entity_t target;  /**< RenderTarget entity, 0 renders into the surface */
    ecs_entity_t layers[8];        /**< Layers the view shows, empty shows every layer */
    ecs_entity_t shadow_layers[8]; /**< Layers that cast its shadows, empty casts from all */
    rgb clear;            /**< Color the target is cleared to */
    float clear_alpha;    /**< Alpha the target is cleared to */
    int32_t priority;     /**< Order in the frame, lower renders first */
    bool shadows;         /**< The view casts and receives shadows, default true */
    bool effects;         /**< The view runs its post process effects, default true */
    bool material_layers; /**< The view applies material layers, default true */
    bool enabled;         /**< The view is rendered, default true */
});

/** Entries of the layer lists of a view, which the ECS_STRUCT above spells out */
#define BOX_VIEW_LAYER_MAX (8)

/** Membership of an entity or a light in a layer, given as the pair (Layer, layer).
 *
 * The relationship fragments, so a view finds the entities of a layer without
 * testing every entity, and it is not exclusive, so an entity may hold more than
 * one pair. It is inherited on instantiation, so an instance of a prefab joins
 * the layers of that prefab.
 */
extern ECS_TAG_DECLARE(Layer);

/** Frustum culling of a view, with the singleton as its default */
ECS_STRUCT(CullSettings, {
    bool enabled;          /**< Instances outside the view frustum are skipped, default true */
    int32_t gpu_threshold; /**< Static instances a batch needs before a compute pass culls it */
});

/** Static instances a batch needs before the renderer culls it in a compute pass */
#define BOX_CULL_GPU_THRESHOLD (2048)

/** Red channel of the clear color a view starts out with */
#define BOX_VIEW_CLEAR_R (39)

/** Green channel of the clear color a view starts out with */
#define BOX_VIEW_CLEAR_G (39)

/** Blue channel of the clear color a view starts out with */
#define BOX_VIEW_CLEAR_B (48)

/** Register the box.view module.
 *
 * @param world The world
 */
void BoxViewImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
