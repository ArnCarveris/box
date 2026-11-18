/**
 * @file portal/shading.h
 * @brief Packs how the areas of a scene light and fog what lies in them, as one view sees them,
 * into the block the mesh shaders read (areas.wgsl).
 *
 * A fragment takes the area whose box holds it. The area sets its ambient, its unshadowed sun,
 * which lights reach it through portals and the fog of the air between it and the eye.
 */

#ifndef BOX_PORTAL_SHADING_H
#define BOX_PORTAL_SHADING_H

#include "light.h"

#ifdef __cplusplus
extern "C" {
#endif

/** vec4 of a shading block at most, the size the mesh shaders bind */
#define BOX_PORTAL_SHADE_MAX (4096)

/** vec4 of the record of an area: ambient and sun, fog, where its entries and lights are */
#define BOX_PORTAL_SHADE_RECORD (3)

/** vec4 of a light that shines into an area: light, pass, and three per opening */
#define BOX_PORTAL_SHADE_THROUGH (2 + 3 * BOX_PORTAL_LIGHT_DEPTH)

/** Portals of the chain an entry is seen through whose air fogs the view ray at most */
#define BOX_PORTAL_SHADE_LINKS (4)

/** Entries of an area in a shading block at most */
#define BOX_PORTAL_SHADE_AREA_ENTRIES (4)

/** Entries of the outdoors in a shading block at most */
#define BOX_PORTAL_SHADE_OUTDOOR_ENTRIES (8)

/** Areas whose lights through portals a block holds at most when no traversal says which are
 * seen; the nearest to the eye */
#define BOX_PORTAL_SHADE_NEAR_AREAS (24)

/** What a view sees of the areas of its scene, for its shading block */
typedef struct box_portal_shade_desc_t {
    const box_portal_index_t *index;     /**< The index */
    ecs_entity_t scene;                  /**< Scene of the view, 0 for the default scene */
    const box_portal_vis_t *vis;         /**< What its camera saw, NULL when no traversal ran */
    const ecs_vec_t *apertures;          /**< The openings of the portals (box_portal_apertures) */
    const box_portal_light_t *lights;    /**< The lights of the scene, in the order of its light
                                          *   buffer */
    int32_t light_count;                 /**< Lights */
    const float *sun;                    /**< Unit direction toward the sun, NULL for none */
    const float *eye;                    /**< World position of the eye */
} box_portal_shade_desc_t;

/** Pack the shading block of a view.
 *
 * The block is a run of vec4, laid out as shading.c describes, with indices relative to its
 * first vec4. Entries and lights that do not fit are left out.
 *
 * @param world The world, which holds the VisArea of each area
 * @param desc What the view sees
 * @param out Receives the block (vec4), appended
 * @param max vec4 the block may take at most
 * @return Whether a block was written: false for a scene without areas or when the boxes and
 * the records of the areas do not fit
 */
bool box_portal_shade_pack(
    const ecs_world_t *world,
    const box_portal_shade_desc_t *desc,
    ecs_vec_t *out,
    int32_t max);

/** Find the area a shading block puts a point in, as the mesh shaders do.
 *
 * @param block The block box_portal_shade_pack wrote
 * @param p The point, in world space
 * @return Index of the area, 0 for the outdoors
 */
int32_t box_portal_shade_area(
    const vec4 *block,
    const float *p);

#ifdef __cplusplus
}
#endif

#endif
