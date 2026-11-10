/**
 * @file box/terrain.h
 * @brief Tile terrain: a flat grid of tiles of a kind, drawn with hand painted transition
 * atlases where kinds meet.
 *
 * The box.terrain module registers TileTerrain as a shape and turns its grid into the image the
 * texture_blend variant of the mesh pipeline reads.
 */

#ifndef BOX_TERRAIN_H
#define BOX_TERRAIN_H

#include "types.h"

#ifdef BOX_TERRAIN_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Kinds a TileTerrain draws with at most */
#define BOX_TERRAIN_KINDS (16)

/** A kind of tile of a TileTerrain: the symbol that stands for it in the grid and the entity
 * whose PbrTextures hold its atlases. In script {'#', cobble}. */
ECS_STRUCT(TileKind, {
    char symbol;            /**< Character of the kind in TileTerrain.tiles, not whitespace */
    ecs_entity_t textures;  /**< Entity with the PbrTextures atlases of the kind, 0 for white */
});

/** Flat terrain of square tiles in the XZ plane, centered on the entity, each tile of a kind.
 *
 * Kinds draw in list order, each over the ones before it, on the dual grid: a cell between the
 * centers of four tiles draws the lowest of their kinds whole and each higher one through the
 * variant of its atlas for the corners of that kind or higher.
 *
 * Needs a scene with texture_blend. The size of the shape follows the grid; Rgb, PbrMaterial,
 * Tint and Emissive apply as they do with a TextureBlend.
 *
 * @par Grid
 * tiles has a line per row of tiles from -z to +z and a symbol per tile from -x to +x,
 * whitespace ignored. Rows shorter than the longest are padded with the first kind, as is an
 * unknown symbol, which is an error.
 *
 * @par Atlas
 * The albedo, ORM and normal of the PbrTextures of a kind are a grid of 4 by 4 variants,
 * variant i in column i % 4 and row i / 4. Bit 0 of i is the top left corner of a cell (-x,
 * -z), bit 1 the top right, bit 2 the bottom left and bit 3 the bottom right; the albedo alpha
 * covers what is below. Variant 15 is the whole tile and 0 is unused. An atlas twice as wide
 * holds 16 more whole tiles, variants 16..31, which half of the whole cells pick at random.
 * Atlases are scaled to the largest of a terrain; cells of a power of two pixels keep each mip
 * level within its cell. A whole tile tiles with itself and with the edges of every variant.
 *
 * @par A road through grass:
 * @code
 * grass { PbrTextures: {albedo: grass_atlas, normal: grass_atlas_n} }
 * road { PbrTextures: {albedo: road_atlas, normal: road_atlas_n} }
 * terrain {
 *   TileTerrain: {size: 1, kinds: [{'.', grass}, {'#', road}], tiles: `
 *     ...##...
 *     ...##...
 *     ..##....
 *   `}
 * }
 * @endcode
 */
ECS_STRUCT(TileTerrain, {
    float size;             /**< Edge of a tile in world units, not positive or finite is 1 */
    ecs_vec(TileKind) kinds; /**< Kinds in the order they draw, at most BOX_TERRAIN_KINDS */
    char *tiles;            /**< The grid, a line of symbols per row */
});

/** Register the box.terrain module.
 *
 * @param world The world
 */
void BoxTerrainImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
