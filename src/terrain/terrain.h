/**
 * @file terrain/terrain.h
 * @brief The grid image of a TileTerrain, shared by box.terrain and the render extract.
 */

#ifndef BOX_TERRAIN_INTERNAL_H
#define BOX_TERRAIN_INTERNAL_H

#include <box.h>

/** The image the grid of a TileTerrain makes, on the terrain entity, set by the engine */
typedef struct TileTerrainGrid {
    ecs_entity_t texture;   /**< Child texture entity with the TexturePixels of the grid: an R8
                             *   pixel per tile with the index of its kind, rows from -z */
    int32_t columns;        /**< Tiles along x */
    int32_t rows;           /**< Tiles along z */
} TileTerrainGrid;

/** Component id of TileTerrainGrid */
extern ECS_COMPONENT_DECLARE(TileTerrainGrid);

/** Count the tiles of the grid of a TileTerrain.
 *
 * @param tiles The grid text, NULL for none
 * @param columns Receives the symbols of the longest row, at least 1
 * @param rows Receives the rows with a symbol, at least 1
 */
void box_terrain_dims(
    const char *tiles,
    int32_t *columns,
    int32_t *rows);

/** Surface kind of a tile of a Terrain in box_surface_data_t.p[0][0], which the vertex stages
 * place with terrain_vertex (terrain.wgsl) */
#define BOX_TERRAIN_SURFACE (16)

/** Texels of the heights of a tile beyond each of its edges, for the normals and the morph */
#define BOX_TERRAIN_BORDER (2)

/** A tile of a Terrain as the renderer draws it, written by the engine.
 *
 * Tiles are not entities. The renderer draws the tiles of a terrain as instances of the grid
 * mesh of the TerrainTile shape, which is registered for the component so that the mesh cache
 * makes the grid: resolution vertices per edge over the unit square in XZ, with skirts (vertices
 * at y -1) below its edges, scaled to size. The surface of an instance tells the vertex stage
 * where the heights of the tile are and how it morphs (box_terrain_tile_surface).
 */
typedef struct TerrainTile {
    ecs_entity_t terrain;   /**< The terrain entity */
    ecs_entity_t texture;   /**< Entity with the TextureGraph of the heights */
    ecs_entity_t masks;     /**< Entity with the TextureGraph of the masks over the window of the
                             *   heights, 0 without Terrain.masks */
    ecs_entity_t coarse;    /**< Entity with the TextureGraph of the masks of the next coarser
                             *   level at every second texel of the window, 0 without
                             *   Terrain.masks */
    ecs_entity_t rough;     /**< Entity with the TextureGraph of the heights at every second
                             *   texel of the window, as the next coarser level has them */
    int32_t level;          /**< Level of detail, 0 the finest */
    int32_t ix;             /**< Column of the tile in the grid of its level */
    int32_t iz;             /**< Row of the tile in the grid of its level */
    int32_t resolution;     /**< Vertices along an edge */
    int32_t slot;           /**< Slot of its heights in the surface assets of the grid mesh */
    float size;             /**< Edge in meters */
    float focus[2];         /**< Nearest focus relative to the center of the tile, x and z */
    float morph[2];         /**< Distances of the focus where the morph into the coarser grid
                             *   starts and ends */
    float skirt;            /**< Depth of the skirts in meters */
    float min_height;       /**< Bottom of the culling bounds */
    float max_height;       /**< Top of the culling bounds */
    bool bounded;           /**< The terrain is bounded: clip is the extent */
    bool masked;            /**< The masks weigh the layers of the TextureBlend of the terrain:
                             *   it has both, and the tile draws with masks */
    float clip[4];          /**< Extent of a bounded terrain relative to the center of the tile:
                             *   min x, min z, max x, max z */
} TerrainTile;

/** Component id of TerrainTile */
extern ECS_COMPONENT_DECLARE(TerrainTile);

/** A tile of a Terrain as the engine keeps it */
typedef struct box_terrain_tile_t {
    TerrainTile shape;      /**< What the renderer draws */
    double position[3];     /**< World position of the center of the tile */
    ecs_entity_t texture;   /**< Entity with the TextureGraph of the heights, a child of the
                             *   container of the tiles */
    ecs_entity_t masks;     /**< Entity with the TextureGraph of the masks, 0 for none */
    ecs_entity_t coarse;    /**< Entity with the TextureGraph of the masks of the next coarser
                             *   level, 0 for none */
    ecs_entity_t rough;     /**< Entity with the TextureGraph of the heights of the next coarser
                             *   level */
    int32_t level;          /**< Level of detail */
    int32_t ix;             /**< Column in the grid of its level */
    int32_t iz;             /**< Row in the grid of its level */
    int32_t slot;           /**< Slot of its heights */
    double distance;        /**< Distance to the nearest focus when it was last wanted */
    float low;              /**< Lowest of its heights and rough heights once measured */
    float high;             /**< Highest of its heights and rough heights once measured */
    int32_t revisions[2];   /**< Revisions of the heights and rough heights measured */
    bool measured;          /**< low and high hold the heights read back */
    bool ready;             /**< Its heights and masks are computed */
    bool wanted;            /**< The focus needs it this frame */
    bool drawn;             /**< It draws this frame */
    bool keep;              /**< It stands in for a wanted tile that is not ready */
    bool enabled;           /**< The renderer draws it */
    uint8_t before;         /**< Its wanted, drawn and keep before the update that runs */
} box_terrain_tile_t;

/** Get the tiles of a terrain, for tests.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @param count Receives the number of tiles
 * @return The tiles, NULL without any
 */
box_terrain_tile_t* const* box_terrain_tiles(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *count);

/** Get the version of the tiles of a terrain, which changes when the record, the position or
 * the drawing of a tile changed or a tile was dropped, so the renderer writes its instances
 * again.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @return The version, 0 without tiles
 */
int32_t box_terrain_tiles_version(
    const ecs_world_t *world,
    ecs_entity_t terrain);

/** Get how often the tiles of a terrain were updated and selected, for tests: an update that
 * found nothing changed and a selection for the same foci and settings do not count.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @param updates Receives the updates that went over the tiles
 * @param selects Receives the selections of the nodes the foci want
 */
void box_terrain_counters(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *updates,
    int32_t *selects);

/** Get a counter of the world that changes whenever an answer of box_terrain_scene,
 * box_terrain_ground or of a height, normal, mask or ray query of any terrain could differ from
 * before, and not while none can.
 *
 * It changes when a terrain got or lost query tiles, when its settings, scene or position
 * changed (seen by the system of the query tiles in EcsPostUpdate), when a CPU copy of a
 * texture arrived, changed or was freed.
 *
 * @param world The world
 * @return The counter
 */
uint64_t box_terrain_revision(
    const ecs_world_t *world);

/** Get how often the query tiles of a terrain were updated and their status was computed, for
 * tests: a frame in which nothing they depend on changed does not count.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @param updates Receives the updates that selected the query tiles
 * @param statuses Receives the computations of the TerrainQueryStatus
 */
void box_terrain_query_counters(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *updates,
    int32_t *statuses);

/** Get how often the render extract built the heights of the tiles that draw and the grid of a
 * terrain with TerrainSampled, for tests: a frame that appends what was built before does not
 * count.
 *
 * @param world The world
 * @param heights Receives the builds of the heights
 * @param grids Receives the builds of the grids
 */
void box_render_terrain_counters(
    const ecs_world_t *world,
    int32_t *heights,
    int32_t *grids);

/** Get the vertices per edge of a tile from a resolution: odd, 5 to 257, 65 for 0 or less.
 *
 * @param resolution The resolution
 * @return Vertices per edge
 */
int32_t box_terrain_vertices(
    int32_t resolution);

/** Register the TerrainTile shape, after TerrainTile.
 *
 * @param world The world
 */
void box_terrain_tile_shape(
    ecs_world_t *world);

/** Register the TerrainTile shape and the system that keeps the tiles of every terrain, part of
 * BoxTerrainImport after Terrain and TerrainStatus.
 *
 * @param world The world
 */
void box_terrain_tiles_import(
    ecs_world_t *world);

/** Texels along an edge of the image of the masks of the next coarser level of a tile, every
 * second texel of its heights.
 *
 * @param resolution Vertices along an edge of the tile
 * @return Texels
 */
int32_t box_terrain_coarse_texels(
    int32_t resolution);

/** Get the texture graph node the tiles of a level of a terrain read their masks from: the
 * masks node at level 0, a TexBlur of it of about a texel of the level above, made on first use.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @param level The level, up to the number of levels for the coarse masks of the coarsest
 * @return The node, 0 without masks
 */
ecs_entity_t box_terrain_mask_node(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t level);

/** Fill the surface of a tile of a Terrain.
 *
 * p[1] is the slot, the floats per row of the heights in the surface assets, the texels per row
 * and the resolution; a slot holds the rows of the heights and after them the rows of the rough
 * heights; p[2] the focus relative to the center of the tile and the morph distances;
 * p[3].x 1 when the tile has masks; p[4] the size, the skirt depth, the level and 1 when bounded;
 * p[5] the clip rectangle.
 *
 * @param tile The tile
 * @param data The surface to fill
 * @param min The minimum corner of the local bounds
 * @param max The maximum corner of the local bounds
 */
void box_terrain_tile_surface(
    const TerrainTile *tile,
    box_surface_data_t *data,
    vec3 min,
    vec3 max);

/** Bytes of a row of the heights of a tile of a resolution in the surface assets of its grid
 * mesh: the texels of a row of its height image, 4 bytes each, aligned to 256 bytes as a copy
 * from a texture into a buffer requires. The rows of the rough heights are as long.
 *
 * @param resolution Vertices along an edge of the tile
 * @return Bytes of a row
 */
uint32_t box_terrain_row_bytes(
    int32_t resolution);

/** Foci a terrain follows at most */
#define BOX_TERRAIN_FOCI (8)

/** Get the world position of a terrain: the Position of a root, else the translation of its
 * world transform, the origin of the world without a transform.
 *
 * @param world The world
 * @param e The terrain
 * @param out Receives the position
 */
void box_terrain_position(
    const ecs_world_t *world,
    ecs_entity_t e,
    double out[3]);

/** Get the foci of a terrain in its frame: those of its scene, else every focus.
 *
 * @param world The world
 * @param e The terrain
 * @param local The local position of the terrain
 * @param out Receives x and z of each focus
 * @param heights Receives y of each focus, may be NULL
 * @return Number of foci
 */
int32_t box_terrain_foci(
    const ecs_world_t *world,
    ecs_entity_t e,
    const double local[3],
    double out[BOX_TERRAIN_FOCI][2],
    double *heights);

/** Most cells along an edge of the window of a level of a sampled Terrain */
#define BOX_TERRAIN_WINDOW_CELLS (64)

/** Cells beyond the range of a level on each side of the window of the level of a sampled
 * Terrain, for the tiles that stand in for wanted ones and for the foci moving between frames */
#define BOX_TERRAIN_WINDOW_MARGIN (4)

/** The windows of the levels of a Terrain with TerrainSampled, in which the renderer indexes
 * the tiles that draw (terrain_sample.wgsl): per level a square of cells cells, a cell per tile of
 * the level, from the tile first, centered on the first focus. */
typedef struct box_terrain_window_t {
    int32_t levels;         /**< Levels of detail */
    int32_t cells;          /**< Cells along an edge of the window of every level */
    int32_t resolution;     /**< Vertices along an edge of a tile */
    int32_t focus_count;    /**< Foci in foci */
    double tile_size;       /**< Edge of a tile of level 0 in meters */
    double extent;          /**< Half size of a bounded terrain, 0 unbounded */
    double foci[BOX_TERRAIN_FOCI][2]; /**< Foci the tiles morph around, x and z in the frame of
                             *   the terrain */
    int32_t first[BOX_TERRAIN_LEVELS][2]; /**< Column and row of the first tile of the window of
                             *   each level */
    float morph[BOX_TERRAIN_LEVELS][2]; /**< Distances of a focus from the center of a tile of each
                             *   level where its morph into the coarser grid starts and ends */
} box_terrain_window_t;

/** Get the windows of the levels of a terrain, around the foci its tiles morph around.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @param out Receives the windows
 * @return Whether the terrain has tiles to index: its settings are applied and it has a focus
 */
bool box_terrain_window(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    box_terrain_window_t *out);

/** Cells along the edge of a query tile of a Terrain */
#define BOX_TERRAIN_QUERY_CELLS (64)

/** Texels of the heights of a query tile beyond each of its edges */
#define BOX_TERRAIN_QUERY_BORDER (1)

/** A query tile of a Terrain: square windows of its height node, and of its masks node when it
 * has one, with a TextureReadback each, set by the engine */
typedef struct box_terrain_query_tile_t {
    ecs_entity_t heights;   /**< Texture entity of the GraphFloat heights */
    ecs_entity_t masks;     /**< Texture entity of the GraphFloat4 masks, 0 without */
    int32_t level;          /**< Level, 0 at query_spacing */
    int32_t ix;             /**< Column of the tile in the grid of its level */
    int32_t iz;             /**< Row of the tile in the grid of its level */
    int32_t texels;         /**< Texels along an edge of its windows */
    double window[3];       /**< X and z of the first texel in the frame of the terrain and the
                             *   meters from the first texel of a row to the last */
    double distance;        /**< Distance to the nearest focus when it was last wanted */
    bool wanted;            /**< The foci or a request want it this frame */
    bool before;            /**< Its wanted before the update that runs */
} box_terrain_query_tile_t;

/** Get the query tiles of a terrain, for tests.
 *
 * @param world The world
 * @param terrain The terrain entity
 * @param count Receives the number of tiles
 * @return The tiles, NULL without any
 */
box_terrain_query_tile_t* const* box_terrain_query_tiles(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t *count);

/** Get the terrains of a scene whose heights can be queried.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param out Receives the terrain entities
 * @param max Entities out holds
 * @return Number of terrains written
 */
int32_t box_terrain_scene(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t *out,
    int32_t max);

/** Register TerrainQueryStatus, the system that keeps the query tiles of every terrain and the
 * script functions of the queries, part of BoxTerrainImport after Terrain.
 *
 * @param world The world
 */
void box_terrain_query_import(
    ecs_world_t *world);

#endif
