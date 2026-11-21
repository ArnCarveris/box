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

/** Share of Grass.fold_height below which blades are fully folded, above it they part less */
#define BOX_GRASS_FOLD_BLEND (0.7f)

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
 * Each blade is a cubic bezier that leans along its facing, bends over toward its tip and curls
 * a little around its length. The blades gather in clumps, the cells of a voronoi pattern of
 * clump_size meters over the world: with clump_follow 1 the blades of a clump share its facing,
 * height and lean, with 0 every blade takes its own. Over tens of meters the color and the height
 * of the field vary with the absolute world position (grass_field_variation of
 * etc/shaders/include/grass_field.wgsl, which the far field of a terrain shares), as much as
 * variation says. Blades seen edge on widen in view space so they do not vanish, and no blade
 * draws thinner than about a pixel: the place pass widens thinner ones and drops as many of them
 * as it widens.
 *
 * Blades shorter than fold_height fold into a V, as two halves that part from a fold between
 * their first and second segment, each leaning out on its own side, so short grass looks about
 * twice as dense for the blades it places. The halves share the vertices of the blade at its
 * level, so at level 2, a single triangle, the blade widens instead. Blades fold more the
 * shorter they are below fold_height, from straight at fold_height to fully parted at
 * BOX_GRASS_FOLD_BLEND times it, so the field shows no step between short and tall blades.
 *
 * The blades are opaque and lit as the scene is: the sun with its shadows, the ambient light,
 * the aerial perspective and the fog. Their normals round across their width, their roots are
 * darker and occluded, they reflect a soft specular and the light behind them shines through, as
 * a DiffuseTransmission does for meshes. Normals and specular flatten with distance, so a field
 * does not shimmer without temporal anti-aliasing. They neither cast shadows nor take part in
 * picking.
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
    float fold_height;      /**< Height in meters below which blades fold into a V, default
                             *   0.35; 0 for none */
    float min_width;        /**< Narrowest blade at its base in meters, default 0.03 */
    float max_width;        /**< Widest blade at its base in meters, default 0.06 */
    rgb base_color;         /**< Display encoded color at the base, default {38, 66, 18} */
    rgb tip_color;          /**< Display encoded color at the tip, default {150, 175, 70} */
    float clump_size;       /**< Distance in meters between the centers of the clumps the blades
                             *   gather in, default 1.2 */
    float clump_follow;     /**< How strongly the blades take the facing, height and lean of
                             *   their clump, 0 to 1, default 0.7 */
    float variation;        /**< How much the color and height vary over the world, 0 to 1,
                             *   default 0.5 */
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
