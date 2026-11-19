/**
 * @file box/terrain.h
 * @brief Terrains: a flat grid of tiles of a kind, drawn with hand painted transition atlases
 * where kinds meet, and height fields of a texture graph drawn as tiles with levels of detail.
 *
 * The box.terrain module registers TileTerrain as a shape and turns its grid into the image the
 * texture_blend variant of the mesh pipeline reads. A Terrain makes the tiles around the focus
 * of the world (box/world.h), each a shape displaced on the GPU by the heights of its tile.
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

/** Levels of detail a Terrain has at most */
#define BOX_TERRAIN_LEVELS (16)

/** Height field drawn as tiles around the WorldFocus entities (or the camera of the first View)
 * with levels of detail, its heights from a node of a texture graph over the world.
 *
 * Each tile is a TextureGraph of the node on DomainWorld (GraphFloat) over its rectangle with a
 * border of two texels, its first channel the height in meters. Every tile shares the node
 * entities of the graph, so editing a node updates the whole terrain. The rectangles are in the
 * frame of the terrain entity: a terrain without a transform is at the absolute origin, a
 * function of the absolute position fixed in the world across shifts of the WorldOrigin; the
 * Position of the terrain offsets it (only its translation applies). Height fields cannot
 * overhang: cliffs are steep slopes, best textured triplanar.
 *
 * The tiles form a quadtree: a tile of level L has an edge of tile_size * 2^L meters, and splits
 * into the four tiles of level L - 1 while a focus is closer than lod_distance * 2^(L - 1).
 * Tiles are made nearest first, at most budget per frame, and a coarse tile draws until the
 * finer tiles that replace it have their heights, so the terrain has no holes. Unbounded, the
 * coarsest level reaches lod_distance * 2^(levels - 1) from each focus.
 *
 * The tiles share one grid mesh per resolution and draw in one batch; the vertex stage displaces
 * it by the heights of the tile and takes the normals from them. Near the end of the range of
 * its level a tile morphs into the grid of the next coarser level, so the levels meet without
 * cracks or pops, and skirts below the edges hide what rounding leaves. The material
 * components of the terrain entity (Rgb, PbrMaterial, PbrTextures, TextureMapping, TextureLayers,
 * ...) apply to its tiles; TextureMapping MappingWorld with a triplanar blend suits steep slopes.
 *
 * @par Masks
 * masks is a second node of the graph, a Vector whose four channels weigh the layers of a
 * TextureBlend of the terrain entity in place of TextureBlend.mask, so slope, height and noise
 * of the same graph (TexSlope, TexCurve, TexRemap on the height node, TexCells, TexNoise) paint
 * rock, grass, paths and forest floor. Each tile evaluates it as a TextureGraph (GraphLinear,
 * 0..1) over the window of its heights, and at the texels of the next coarser level, which the
 * tile fades to as its grid morphs, so tiles of adjacent levels meet without a seam in the
 * material. The tiles of a coarser level read the node through a TexBlur of about a texel of
 * their level, so distant slopes do not shimmer. The layers are mapped by the TextureMapping of
 * the terrain, MappingWorld of a size of 1 without one, continuous across tiles and shifts of
 * the WorldOrigin, with the height based contrast of the TextureBlend; a triplanar blend keeps
 * steep rock from stretching and TextureLayers apply on top. The tiles still draw in one batch.
 * A terrain without masks, or without a TextureBlend, draws as before.
 *
 * @code
 * hills {
 *   noise { nodes.Noise: {frequency: 0.004, octaves: 5, fractal: NoiseRidged} }
 *   height { nodes.Multiply: {a: noise, b_value: {120, 120, 120, 120}} }
 *   Terrain: {height: height, tile_size: 32, levels: 6, max_height: 130}
 *   Rgb: {110, 130, 80}
 * }
 * @endcode
 */
ECS_STRUCT(Terrain, {
    ecs_entity_t height;    /**< Texture graph node whose first channel is the height in meters */
    ecs_entity_t masks;     /**< Texture graph node whose four channels weigh the layers of the
                             *   TextureBlend of the terrain, 0 for TextureBlend.mask */
    float tile_size;        /**< Meters along the edge of a tile of the finest level, 0 for 32 */
    int32_t resolution;     /**< Vertices along the edge of a tile, 0 for 65: rounded up to an
                             *   odd count, 5 to 257 */
    int32_t levels;         /**< Levels of detail, each doubling the tile size, 0 for 6, at most
                             *   BOX_TERRAIN_LEVELS */
    float lod_distance;     /**< Radius around a focus of the finest level in meters, each
                             *   coarser level twice the one before; 0 for 4 tile sizes,
                             *   clamped to 3 to 16 tile sizes */
    float extent;           /**< Half size of a bounded terrain around the entity in meters along
                             *   x and z, 0 for an unbounded terrain */
    float max_height;       /**< Highest height of the terrain, the top of the culling bounds of
                             *   the tiles; with min_height 0 or above it for -256 and 1024 */
    float min_height;       /**< Lowest height of the terrain, the bottom of the culling bounds */
    int32_t budget;         /**< Tiles made per frame at most, 0 for 8; four times as many are
                             *   dropped */
});

/** Tiles of a Terrain, written by the engine on the terrain entity, read only for
 * applications */
ECS_STRUCT(TerrainStatus, {
    int32_t tiles[16];      /**< Tiles drawn per level, the finest first */
    int32_t drawn;          /**< Tiles drawn */
    int32_t pending;        /**< Tiles the focus needs whose heights are not computed yet */
    int32_t count;          /**< Tiles that exist, drawn, pending or kept until replaced */
    int32_t created;        /**< Tiles made since the terrain was set */
    int32_t dropped;        /**< Tiles deleted since the terrain was set */
    int64_t gpu_bytes;      /**< Bytes of the height and mask images of the tiles and of their
                             *   copies the shaders read */
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
