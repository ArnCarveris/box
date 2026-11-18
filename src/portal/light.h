/**
 * @file portal/light.h
 * @brief Light through portals: the openings of the portals as light sees them, the lights of
 * the areas behind the open portals of an area, the sun through portals to the outdoors, and
 * whether a light reaches a seen area at all.
 *
 * An area is lit by its own lights and, through each open portal, by those of the areas behind
 * it, up to BOX_PORTAL_LIGHT_DEPTH portals away; an area gets the sun the same way through
 * portals out to the outdoors. Glass tints and dims what passes it.
 */

#ifndef BOX_PORTAL_LIGHT_H
#define BOX_PORTAL_LIGHT_H

#include "portal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Portals a light passes on its way into an area at most */
#define BOX_PORTAL_LIGHT_DEPTH (2)

/** Lights, the sun included, that shine into an area through its portals at most */
#define BOX_PORTAL_THROUGH_MAX (8)

/** Size of a light that sets how soft the edges of the openings it shines through are */
#define BOX_PORTAL_LIGHT_SIZE (0.1f)

/** Penumbra of the sun per unit of distance from an opening */
#define BOX_PORTAL_SUN_SOFT (0.01f)

/** Distance toward the sun at which the shader traces it as a point light */
#define BOX_PORTAL_SUN_FAR (1000.0f)

/** Distance from the eye at which an opening counts half in the ranking of the lights */
#define BOX_PORTAL_LIGHT_NEAR (15.0f)

/** Weight of the sun against a light of intensity 1 in the ranking of the lights */
#define BOX_PORTAL_SUN_WEIGHT (8.0f)

/** Pane of a glazed portal without a tint: color in x, y, z and opacity in w */
#define BOX_PORTAL_GLASS_DEFAULT ((vec4){0.62f, 0.7f, 0.78f, 0.18f})

/** The opening of a portal as light sees it, the part a door left open */
typedef struct box_portal_aperture_t {
    vec3 center;            /**< Center of the open part */
    vec3 right;             /**< Half its width along the width of the portal */
    vec3 up;                /**< Half its height along the height of the portal */
    vec3 normal;            /**< Unit normal of the portal */
    vec3 pass;              /**< Color it lets through: white, or what its pane passes */
    bool open;              /**< Light passes it */
} box_portal_aperture_t;

/** A light of a scene as the portals see it */
typedef struct box_portal_light_t {
    vec3 position;          /**< World position */
    float range;            /**< Distance at which it lights nothing */
    float intensity;        /**< Its brightest color channel times its intensity */
    int32_t area;           /**< Index of the area that holds it, 0 for the outdoors */
} box_portal_light_t;

/** A light that shines into an area through its portals */
typedef struct box_portal_through_t {
    int32_t light;          /**< Index of the light, -1 for the sun */
    float weight;           /**< How bright it is at the opening it comes in by, near the eye
                             *   counting more */
    vec3 pass;              /**< Color the openings let through together */
    int32_t count;          /**< Portals it passes */
    int32_t portals[BOX_PORTAL_LIGHT_DEPTH]; /**< Indices of the portals, the one into the area
                             *   last */
} box_portal_through_t;

/** Get the opening of every portal of an index as light sees it.
 *
 * A shut portal passes nothing. A partly open portal passes light through the part its cover
 * left clear (VisPortal.open and slide), and a glazed one the color of its pane.
 *
 * @param world The world, which holds the VisPortal of each portal
 * @param index The index
 * @param out Receives one box_portal_aperture_t per portal of the index, cleared first
 */
void box_portal_apertures(
    const ecs_world_t *world,
    const box_portal_index_t *index,
    ecs_vec_t *out);

/** Find the area of a light: the area that holds it, else one just below it, for a lamp in
 * the ceiling.
 *
 * @param index The index
 * @param scene The scene, 0 for the default scene
 * @param p World position of the light
 * @return Index of the area, 0 for the outdoors
 */
int32_t box_portal_light_area(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const float *p);

/** Find the lights that shine into an area through its open portals, and the sun through
 * portals to the outdoors, brightest first.
 *
 * @param index The index
 * @param apertures The openings of its portals (box_portal_apertures)
 * @param area Index of the area
 * @param lights The lights of its scene
 * @param light_count Lights
 * @param sun Unit direction toward the sun, NULL for no sun
 * @param eye World position of the eye, near which the openings count more
 * @param out Receives the lights
 * @param max Lights to write at most
 * @return Lights written
 */
int32_t box_portal_through(
    const box_portal_index_t *index,
    const ecs_vec_t *apertures,
    int32_t area,
    const box_portal_light_t *lights,
    int32_t light_count,
    const float *sun,
    const float *eye,
    box_portal_through_t *out,
    int32_t max);

/** Check whether a light lights an area that is seen: its own, or one it shines into through
 * up to BOX_PORTAL_LIGHT_DEPTH open portals within its range.
 *
 * @param index The index
 * @param apertures The openings of its portals (box_portal_apertures)
 * @param light The light
 * @param seen Per area of the index, whether it is seen
 * @return Whether it lights a seen area
 */
bool box_portal_light_reaches(
    const box_portal_index_t *index,
    const ecs_vec_t *apertures,
    const box_portal_light_t *light,
    const bool *seen);

#ifdef __cplusplus
}
#endif

#endif
