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
 * With depth_prepass, a lit view whose camera has a depth range draws its opaque 3D meshes depth
 * only first (twice when it culls by occlusion), then shades them once per pixel with an Equal
 * depth test; otherwise it shades every fragment that passes. Gtao needs the prepass.
 *
 * baked, reflections and atmosphere only take effect for the features the Scene of the view
 * enables; turning them off does not compile other pipelines.
 */
ECS_STRUCT(LightSettings, {
    bool enabled;           /**< Enable lighting for the view */
    bool baked;             /**< Sample the baked lighting of the scene, View ctor sets true */
    bool reflections;       /**< Sample the reflection probes of the scene, View ctor sets
                             *   true */
    bool depth_prepass;     /**< Draw the opaque meshes depth only before shading them, View
                             *   ctor sets true */
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
    float min_caster_texels; /**< Projected size in texels of a cascade below which a caster is
                          *   not drawn into that cascade; needs CullSettings.enabled, default 0
                          *   for none */
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

/** Settings of the infinite grid a view draws in its debug pass: the plane y = height, with
 * anti aliased lines at every cell, major lines every major_every cells and the world X (red)
 * and Z (blue) axes, faded out with the distance from the camera and toward the horizon.
 *
 * The lines are drawn in their colors as given, not tone mapped, fogged or bloomed; the depth of
 * the view hides the grid behind geometry. A cell_size of 0, as in a C literal, draws no grid.
 */
ECS_STRUCT(GridSettings, {
    bool enabled;           /**< Draw the grid */
    float height;           /**< World Y of the plane, default 0 */
    float cell_size;        /**< World size of a cell, default 1 */
    int32_t major_every;    /**< Cells between major lines, default 10, 0 for none */
    float fade_distance;    /**< Distance from the camera at which it is gone, default 100 */
    float line_width;       /**< Width of the lines in pixels, default 1 */
    rgba color;             /**< Minor lines, default gray with alpha 128 */
    rgba major_color;       /**< Major lines, default gray with alpha 200 */
});

/** Wireframe of the 3D meshes of a view (ViewDebug.wireframe): anti aliased lines along the
 * edges of the triangles of the instances the view draws after its culling, from barycentric
 * coordinates, so it needs no line polygon mode. 2D shapes, text and particles are not wired.
 *
 * A line is drawn inside the triangles on either side of its edge, half on each. An edge with a
 * line on one side only, on the border of an open mesh or the silhouette of a closed one, draws
 * the whole width on that side, so every line has the width of the view.
 *
 * WireframeOnly culls by frustum and size only and renders no shadows, ambient occlusion or
 * transmission; the sky, the particles and the 2D shapes still draw.
 */
ECS_ENUM(WireframeMode, {
    WireframeOff = 0,     /**< No wireframe */
    WireframeOverlay = 1, /**< Over the shaded image, hidden by what is in front */
    WireframeOnly = 2     /**< Only the lines: see through, no shaded meshes */
});

/** Overdraw heat map of a view (ViewDebug.overdraw).
 *
 * It replays the depth prepass of the view in its draw order and with its depth test, counting
 * per pixel the opaque mesh fragments that pass it and, alpha tested, pass their alpha test.
 *
 * A heat map goes from black for none through blue, green, yellow and red to white for 8 or
 * more. Blended meshes, 2D shapes, text, particles and the sky do not count.
 */
ECS_ENUM(OverdrawMode, {
    OverdrawOff = 0,      /**< No heat map */
    OverdrawAll = 1,      /**< Fragments of the plain and the alpha tested meshes */
    OverdrawPlain = 2,    /**< Fragments of the meshes without an alpha test */
    OverdrawCutout = 3    /**< Fragments of the alpha tested meshes (AlphaCutoff) */
});

/** Debug drawing of a view, in a pass after its effects into the color of its target, with the
 * depth of the view read only. All zero (a C literal View) draws nothing and records no pass.
 *
 * A screenshot (capture) of a camera that a view renders includes its debug drawing, as on
 * screen. The probe and reflection captures, the bakes and a screenshot of a camera that no
 * view renders do not draw it.
 *
 * The probe views show only what the view samples: light_probes the probes of the resident
 * volume of its scene with LightSettings.baked, reflection_probes the ReflectionProbes of its
 * scene with LightSettings.reflections. A view with those off shows nothing.
 */
ECS_STRUCT(ViewDebug, {
    GridSettings grid;      /**< Infinite grid */
    WireframeMode wireframe; /**< Wireframe of the meshes, default WireframeOff */
    rgba wireframe_color;   /**< Color of the wireframe lines, default white with alpha 200 */
    float wireframe_width;  /**< Width of the wireframe lines in pixels, default 1 */
    OverdrawMode overdraw;  /**< Heat map of the mesh fragments over each pixel that pass the
                             *   depth test of the prepass, default OverdrawOff */
    bool light_probes;      /**< Spheres at the probes of the volume the view samples, lit by
                             *   their SH, unshadowed; a probe invalid for every cell around it
                             *   (inside static geometry) in a dark red hatch */
    bool reflection_probes; /**< Mirror spheres of the reflection probes the view samples at
                             *   their capture points, gray until captured, with the influence
                             *   box in yellow and the projection box, if any, in cyan.
                             *
                             *   The scene probe of a ReflectionBake has no sphere and no box:
                             *   it has no position of its own in view. At most 65536 probes of
                             *   a kind draw, with a warning naming the view past it. */
    float probe_size;       /**< World radius of the spheres, 0 for a quarter of the probe
                             *   spacing for a light probe and 0.25 for a reflection probe;
                             *   default 0 */
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

/** Order of the view of the startup overlay: over the fps panel, under the console */
#define BOX_VIEW_ORDER_STARTUP (1750000)

/** Order of the view of the in game console, which renders after every other view */
#define BOX_VIEW_ORDER_CONSOLE (2000000)

/** What a frame draws: a camera, the target it draws into and its settings.
 *
 * Views render in ascending order and are picked and get input in descending order, so a view
 * of a higher order draws over and is hit first. Ties keep entity id (creation) order. A
 * changed order takes effect in the next frame.
 *
 * The debug settings draw over the image after the effects of the view, in colors that are not
 * tone mapped; a view of a higher order draws over them. A screenshot of the camera of the view
 * includes them, the probe and reflection captures and the bakes do not.
 */
ECS_STRUCT(View, {
    ecs_entity_t camera;            /**< Camera the view renders, a view without one is skipped */
    ClearSettings clear;            /**< Clear settings */
    LightSettings lighting;         /**< Lighting settings */
    ShadowSettings shadows;         /**< Shadow settings */
    CullSettings culling;           /**< Cull settings */
    ecs_vec(ViewEffect) effects;    /**< Post process effects */
    ViewDebug debug;                /**< Debug drawing, after the effects */
    int32_t order;                  /**< Render order, default 0; box's overlays use
                                     *   BOX_VIEW_ORDER_EDITOR, BOX_VIEW_ORDER_FPS and
                                     *   BOX_VIEW_ORDER_CONSOLE */
});

/** Component added to scene root.
 * A view renders only the scene of its camera, lit by the lights of that scene.
 *
 * An entity belongs to its nearest ancestor with Scene (itself included), so scenes nest;
 * entities outside any Scene belong to the default scene, whose features the singleton holds.
 *
 * Its members are the features the mesh shaders of the scene support, all false by default for
 * the smallest shaders. A shape that needs a feature that is off draws as if it did not have it.
 */
ECS_STRUCT(Scene, {
    bool baked;             /**< Probe volume irradiance with the visibility of the probes, for
                             *   the views with LightSettings.baked */
    bool reflections;       /**< Reflection probes, normalized by the probe volume with baked, for
                             *   the views with LightSettings.reflections */
    bool punctual_shadows;  /**< Shadows of the spot and point lights with a tile in the shadow
                             *   atlas, for the views with ShadowSettings.enabled */
    bool cookies;           /**< The LightCookie of the first directional light */
    bool environment;       /**< The prefiltered cubemap and SH ambient of the environment of a
                             *   view (LightSettings.environment), else the hemisphere ambient */
    bool aerial_perspective; /**< Aerial perspective of the Atmosphere on the 3D meshes, for the
                             *   views with LightSettings.atmosphere */
    bool alpha_cutout;      /**< AlphaCutoff, else alpha tested shapes draw opaque */
    bool blend_modes;       /**< BlendMode and Opacity below 1, else the shapes draw opaque */
    bool transmission;      /**< Transmission */
    bool texture_blend;     /**< TextureBlend, else the shapes draw with their PbrTextures */
    bool triplanar;         /**< A TextureMapping blend above 0, else the hard projection */
    bool sheen;             /**< Sheen */
    bool clear_coat;        /**< ClearCoat */
    bool anisotropy;        /**< SpecularAnisotropy */
    bool diffuse_transmission; /**< DiffuseTransmission */
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
 * View gets a constructor that enables lighting, baked lighting, reflections, the depth prepass,
 * the atmosphere and the fog volumes, sets the environment intensity to 1 and the ShadowSettings
 * defaults with shadows disabled, enables culling with occlusion and a smallest size of 1 pixel.
 *
 * It sets the GridSettings defaults with the grid disabled, and the wireframe defaults with the
 * wireframe off. A C literal View starts from zero
 * instead: unlit, drawing every instance, without debug drawing.
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
