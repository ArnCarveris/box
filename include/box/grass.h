/**
 * @file box/grass.h
 * @brief Grass blades generated on the GPU around the camera of each lit view, on a Terrain or
 * on the plane of their entity.
 *
 * The box.grass module registers Grass. No entity exists per blade: each frame a compute pass
 * places the blades of the patches of a grid around the camera that the view sees, and the depth
 * prepass and the forward pass draw them with one indirect draw per level of detail.
 */

#ifndef BOX_GRASS_H
#define BOX_GRASS_H

#include "types.h"

#ifdef BOX_GRASS_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Levels of detail of the blades of a Grass, each with fewer vertices than the one before */
#define BOX_GRASS_LODS (3)

/** Most blades per square meter a Grass places, a larger Grass.density is clamped */
#define BOX_GRASS_DENSITY_MAX (256.0f)

/** Farthest distance in meters a Grass reaches, a larger last level distance is clamped */
#define BOX_GRASS_DISTANCE_MAX (500.0f)

/** A field of grass blades the renderer generates on the GPU around the camera of every lit view
 * of its scene, on its own entity, so several Grass entities can share one terrain.
 *
 * The blades grow on terrain, a Terrain whose heights and mask channels the compute pass reads
 * by world position (TerrainSampled, which the module adds to it), or with a terrain of 0 on the
 * horizontal plane through the position of the entity. Each blade belongs to a cell of a grid of
 * absolute world cells about 1 / sqrt(density) apart and takes its place, size, facing and shade
 * from a hash of the cell, so the blades stay where they are as the camera moves and across
 * shifts of the WorldOrigin. With mask a channel of the masks node of the terrain weighs the
 * density, so blades only grow where the channel is high; a terrain without masks grows them
 * everywhere.
 *
 * Within lod[0] meters of the camera every cell has its blade. Further out every doubling of the
 * distance keeps a quarter of the blades, the others shrink away, and the blades that stay widen
 * so the field covers the ground as densely. Level 0 draws a blade of 7 segments, level 1 of 3
 * from lod[0] and level 2 a single triangle from lod[1]; the blades end at lod[2], beyond which
 * the terrain itself shows the grass from afar.
 *
 * The blades are opaque and lit as the scene is: the sun with its shadows, the ambient light,
 * the aerial perspective and the fog. They neither cast shadows nor take part in picking.
 *
 * @code
 * meadow {
 *   Grass: {terrain: hills, mask: 1, density: 40, base_color: {40, 70, 20}}
 * }
 * @endcode
 */
ECS_STRUCT(Grass, {
    ecs_entity_t terrain;   /**< Terrain the blades grow on, 0 for the horizontal plane through
                             *   the position of the entity */
    int32_t mask;           /**< Channel of the masks of the terrain, 0 to 3, that weighs the
                             *   density; -1 (default) for none */
    float density;          /**< Blades per square meter near the camera, default 48, at most
                             *   BOX_GRASS_DENSITY_MAX */
    float min_height;       /**< Shortest blade in meters, default 0.25 */
    float max_height;       /**< Tallest blade in meters, default 0.6 */
    float min_width;        /**< Narrowest blade at its base in meters, default 0.03 */
    float max_width;        /**< Widest blade at its base in meters, default 0.06 */
    rgb base_color;         /**< Display encoded color at the base, default {38, 66, 18} */
    rgb tip_color;          /**< Display encoded color at the tip, default {150, 175, 70} */
    float lod[3];           /**< Distances in meters where levels 0 and 1 end and the blades
                             *   end, default {20, 60, 200}; each at least the one before */
    uint32_t seed;          /**< Seed of the hash of the cells, another field of blades */
});

/** Register the box.grass module.
 *
 * Grass entities make their terrain TerrainSampled when they are set.
 *
 * @param world The world
 */
void BoxGrassImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
