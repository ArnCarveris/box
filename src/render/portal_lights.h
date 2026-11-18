/**
 * @file render/portal_lights.h
 * @brief Lighting of portal-room scenes in the light extract: the area of every point and spot
 * light, the lights no view can see through portals, and the shading block of every view.
 *
 * Each frame every perspective view of a scene with areas walks the areas its camera sees
 * (box_portal_traverse). A light in an area no view sees, which cannot shine into a seen area
 * through open portals either, is culled.
 */

#ifndef BOX_RENDER_PORTAL_LIGHTS_H
#define BOX_RENDER_PORTAL_LIGHTS_H

#include "render.h"
#include "../portal/shading.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Aspect ratio of the traversal frustum of a view, wide enough for any surface */
#define BOX_RENDER_PORTAL_LIGHTS_ASPECT (4.0f)

/** What one view of the packet sees of the areas of its scene */
typedef struct box_render_portal_view_t {
    ecs_entity_t scene;     /**< Scene of its camera */
    bool walked;            /**< Its traversal ran and saw every area it reaches */
    box_portal_vis_t vis;   /**< What its camera saw */
    bool *seen;             /**< Per area of the index, whether it is seen, NULL when not walked */
    int32_t seen_count;     /**< Areas seen has room for */
} box_render_portal_view_t;

/** Lighting of the portal-room scenes in one frame */
typedef struct box_render_portal_lights_t {
    const box_portal_index_t *index; /**< The index, NULL when the world has no areas */
    ecs_vec_t views;        /**< box_render_portal_view_t, one per view of the packet */
    ecs_vec_t apertures;    /**< box_portal_aperture_t, the openings of the portals */
    ecs_vec_t lights;       /**< box_portal_light_t, scratch of the shading blocks */
} box_render_portal_lights_t;

/** Create the state.
 *
 * @return The state
 */
box_render_portal_lights_t* box_render_portal_lights_new(void);

/** Free the state.
 *
 * @param s The state, may be NULL
 */
void box_render_portal_lights_free(
    box_render_portal_lights_t *s);

/** Walk the areas every view of the packet sees and find the openings of the portals.
 *
 * @param world The world
 * @param s The state
 * @param p The packet, with the views of the frame
 */
void box_render_portal_lights_begin(
    ecs_world_t *world,
    box_render_portal_lights_t *s,
    const box_render_packet_t *p);

/** Check whether a point or spot light lights an area a view sees, directly or through up to
 * BOX_PORTAL_LIGHT_DEPTH open portals.
 *
 * @param s The state
 * @param view Index of the view in the packet
 * @param scene Scene of the light
 * @param center World position of the light
 * @param range Range of the light in world units
 * @return Whether it may light what the view sees: true when the view walked no areas of the
 * scene of the light
 */
bool box_render_portal_lights_seen(
    const box_render_portal_lights_t *s,
    int32_t view,
    ecs_entity_t scene,
    const float *center,
    float range);

/** Write the area of every point and spot light of a list into the light (box_render_light_t
 * area), 0 for a scene without areas.
 *
 * @param s The state
 * @param scene Scene of the lights
 * @param lights box_render_light_t, the lights of the scene
 */
void box_render_portal_lights_tag(
    const box_render_portal_lights_t *s,
    ecs_entity_t scene,
    ecs_vec_t *lights);

/** Pack the shading block of a view into the areas of the packet and point the view at it.
 *
 * @param world The world
 * @param s The state
 * @param p The packet
 * @param view Index of the view in the packet
 * @param lights box_render_light_t, the lights the view is lit with, in the order of its light
 * buffer, NULL for none
 * @param sun Unit direction toward the first directional light, NULL for none
 */
void box_render_portal_lights_view(
    ecs_world_t *world,
    box_render_portal_lights_t *s,
    box_render_packet_t *p,
    int32_t view,
    const ecs_vec_t *lights,
    const float *sun);

#ifdef __cplusplus
}
#endif

#endif
