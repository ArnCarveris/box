/**
* @file box/view.h
* @brief View and render target components: what a frame draws and where.
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

/** Clear settings for a view */
ECS_STRUCT(ClearSettings, {
    bool enabled;           /**< Clear view */
    rgba color;             /**< Clear color */
});

/** The environment a view lights its scene with: an entity with Environment, such as an
 * EnvironmentImage */
ECS_STRUCT(EnvironmentSettings, {
    ecs_entity_t source;    /**< Entity with Environment, 0 for no environment */
    float intensity;        /**< Scale of the environment light, View ctor sets 1 */
    float yaw;              /**< Rotation of the environment about +Y in radians, as
                             *   Rotation.y */
    bool background;        /**< Draw the environment behind the geometry in place of the
                             *   clear color */
});

/** Light settings for a view.
 *
 * A lit view whose camera has a depth range draws its opaque and alpha tested 3D meshes into the
 * depth buffer first (two prepasses when it culls by occlusion), then shades them once per pixel
 * with an Equal depth test; an unlit view shades every opaque fragment passing the depth test.
 */
ECS_STRUCT(LightSettings, {
    bool enabled;           /**< Enable lighting for the view */
    bool baked;             /**< Sample the baked lighting of the scene, View ctor sets true */
    bool reflections;       /**< Sample the reflection probes of the scene, View ctor sets
                             *   true */
    bool atmosphere;        /**< Render the Atmosphere of the scene: the sky behind the
                             *   geometry and the aerial perspective of the 3D meshes, View
                             *   ctor sets true */
    bool fog_volumes;       /**< Render the FogVolumes of the scene, View ctor sets true */
    EnvironmentSettings environment; /**< Environment the view is lit with, used only when
                                      *   the view is lit */
    ecs_entity_t scene;     /**< Scene whose lights and shadows light the view, 0 for its own */
});

/** Shadow settings of a view: the cascades of the first directional light of its scene and the
 * tiles of its shadow casting spot lights (SpotLight.shadows).
 *
 * depth_bias, normal_offset and filter_size apply to the spot lights in texels of their tile;
 * max_distance does not limit them, they reach as far as the range of the light.
 */
ECS_STRUCT(ShadowSettings, {
    bool enabled;        /**< The view renders shadows, those of the spot lights included */
    int32_t cascades;    /**< Cascades of the shadow map, default 4, at most 4 */
    int32_t resolution;  /**< Edge of one cascade in texels, default 2048 */
    float max_distance;  /**< View distance the shadows reach, default 200 */
    float split_lambda;  /**< 0 splits uniformly, 1 logarithmically, default 0.7 */
    float depth_bias;    /**< Depth offset of the comparison in texels of the cascade, default 2 */
    float normal_offset; /**< Offset along the geometric normal (before the normal map) in
                          *   texels of the cascade times 1 - N.L: none facing the light, all
                          *   of it at grazing light; default 2 */
    int32_t filter_size; /**< Tent filter width in texels: 1 is one bilinear comparison (2x2
                          *   PCF), 3 four, 5 nine, 7 sixteen; even values round up to the next
                          *   odd one; default 3, 1 to 7 */
    float fade;          /**< Fraction of the last cascade that fades out, default 0.2 */
    float blend;         /**< Fraction of the half extent of a cascade at the edge of its box
                          *   that blends into the next cascade, 0 for hard seams, the last one
                          *   fades instead; default 0.1, 0 to 0.5 */
});

/** Culling settings of a view, applied as specified: all zero draws every instance.
 *
 * Occlusion tests against what the view drew first in the same frame, so an instance that comes
 * into view is drawn at once however fast the camera moves. Cascades cull by frustum only.
 */
ECS_STRUCT(CullSettings, {
    bool enabled;           /**< Cull by frustum and size, false draws every instance */
    bool occlusion;         /**< Also cull instances hidden behind what the view drew */
    float min_dimension_px; /**< Projected size in pixels below which an instance is culled */
});

/** Element in view effects array. */
ECS_STRUCT(ViewEffect, {
    bool enabled;           /**< Whether effect is enabled. */
    ecs_entity_t effect;    /**< Entity with effect settings. */
});

/** Order of the view of the editor: over the views of a game, under the console.
 *
 * Game views use orders below BOX_VIEW_ORDER_EDITOR, normally 0 or small numbers around it.
 */
#define BOX_VIEW_ORDER_EDITOR (1000000)

/** Order of the view of the fps panel: over the editor, under the console */
#define BOX_VIEW_ORDER_FPS (1500000)

/** Order of the view of the in game console, which renders after every other view */
#define BOX_VIEW_ORDER_CONSOLE (2000000)

/** What a frame draws: a camera, the target it draws into and its settings.
 *
 * Views render in ascending order and are picked and get input in descending order, so a view
 * of a higher order draws over and is hit first. Ties keep entity id (creation) order.
 *
 * A changed order takes effect in the next frame.
 */
ECS_STRUCT(View, {
    ecs_entity_t camera;            /**< Camera the view renders, a view without one is skipped */
    ClearSettings clear;            /**< Clear settings */
    LightSettings lighting;         /**< Lighting settings */
    ShadowSettings shadows;         /**< Shadow settings */
    CullSettings culling;           /**< Cull settings */
    ecs_vec(ViewEffect) effects;    /**< Post process effects */
    int32_t order;                  /**< Render order, default 0; box's overlays use
                                     *   BOX_VIEW_ORDER_EDITOR, BOX_VIEW_ORDER_FPS and
                                     *   BOX_VIEW_ORDER_CONSOLE */
});

/** Component added to scene root.
 * A view renders only the scene of its camera, lit by the lights of that scene.
 *
 * An entity belongs to its nearest ancestor with Scene (itself included), so
 * scenes nest; entities outside any Scene belong to the default scene.
 */
ECS_STRUCT(Scene, {
    int32_t dummy_; /**< Unused, gives the component a size */
});

/** Compare the render order of two views: by order, then by entity id.
 *
 * @param order_a Order of the first view
 * @param a The first view
 * @param order_b Order of the second view
 * @param b The second view
 * @return Negative when a renders first, positive when b does, 0 for the same view
 */
int box_view_compare(
    int32_t order_a,
    ecs_entity_t a,
    int32_t order_b,
    ecs_entity_t b);

/** Register the box.view module.
 *
 * View gets a constructor that enables lighting, baked lighting, reflections, the
 * atmosphere and the fog volumes, sets the environment intensity to 1 and the ShadowSettings defaults with shadows
 * disabled and enables culling with occlusion and a smallest size of one pixel.
 *
 * A C literal View starts from zero instead: unlit, drawing every instance.
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
