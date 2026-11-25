/**
 * @file box/world.h
 * @brief Open world: the focus that streaming systems follow, the chunk grids they stream and
 * the scatters that place content in them.
 *
 * Positions are f64 (box/transform.h), so content keeps its precision anywhere in a large world
 * and never moves to follow the camera: the renderer draws relative to an origin near it.
 */

#ifndef BOX_WORLD_H
#define BOX_WORLD_H

#include "types.h"

#ifdef BOX_WORLD_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Marks the entities that streaming systems follow, such as a player or a camera.
 *
 * The systems take every WorldFocus entity; without any they follow the camera of the first
 * View. The renderer draws relative to an origin that follows the first of them
 * (box_world_focus).
 */
extern ECS_TAG_DECLARE(WorldFocus);

/** Most ring radii of a ChunkGrid */
#define BOX_WORLD_CHUNK_LEVELS (8)

/** Streams a template around the foci: a child entity per cell of a 2D or 3D grid in range.
 *
 * Cells are child entities of the grid entity with the template set on them. The template gets
 * the props it declares of cell_x, cell_y, cell_z (i32 absolute cell), level (i32), size (cell
 * size, a vec3 or the X of it for a number) and seed; the Position of a cell is its corner.
 *
 * A cell is at level l when its nearest point is within levels[l] of a focus, in 3D for a 3D
 * grid, horizontally for a 2D grid. Loads go nearest first, more detail before less, within the
 * budget; a cell moves past a radius by hysteresis before it changes level or unloads.
 */
ECS_STRUCT(ChunkGrid, {
    ecs_entity_t template; /**< Script template set on every cell */
    vec3 cell_size;      /**< Size of a cell in meters, Y 0 for a 2D grid in XZ, default 32 0 32 */
    float levels[8];     /**< Increasing ring radii in meters, the first 0 ends them; beyond the
                          *   last a cell unloads, default 128 */
    float hysteresis;    /**< Meters past a radius before a cell changes level or unloads,
                          *   default 8 */
    int32_t budget;      /**< Cells created or changed per frame, 0 for no limit, default 16 */
    float budget_ms;     /**< Milliseconds of creations, level changes and deletions per
                          *   frame, 0 for no limit, default 0; at least one cell loads and
                          *   one unloads per frame, the others wait for the next frames */
    int32_t extent[3];   /**< Cells span -extent to extent - 1 along an axis, 0 for unbounded */
    int32_t seed;        /**< Set as the seed prop of the cells */
    ecs_entity_t ground; /**< Terrain whose heights under a cell must be resident at its
                          *   query_spacing before the cell loads or changes level, so its
                          *   template can read them with box.terrain.height; 0 for none.
                          *   The grid asks the terrain for them (box_terrain_request) */
});

/** Cells of a ChunkGrid, written by the engine next to it in every frame in which the grid
 * has work and in the frame after it */
ECS_STRUCT(ChunkGridStatus, {
    int32_t loaded[8];   /**< Loaded cells per level */
    int32_t cells;       /**< Loaded cells */
    int32_t pending;     /**< Cells waiting to load or change level */
    int32_t held;        /**< Of the pending cells, those a ChunkGridGate holds back */
    int32_t created;     /**< Cells created this frame */
    int32_t changed;     /**< Cells that changed level this frame */
    int32_t deleted;     /**< Cells deleted this frame */
    int32_t unloading;   /**< Cells out of range that wait for the budget_ms of a later frame
                          *   to be deleted */
    float time_ms;       /**< Milliseconds the grid took this frame */
    float create_ms;     /**< Of time_ms, the creations and level changes: the evaluation of
                          *   the template and everything its entities trigger */
    float delete_ms;     /**< Of time_ms, the deletions of cells */
});

/** Cell of a ChunkGrid, set by the grid on every cell entity before its template */
ECS_STRUCT(ChunkGridCell, {
    int32_t cell[3];     /**< Absolute cell coordinate */
    int32_t level;       /**< Level of the cell */
    vec3 size;           /**< Cell size of the grid, its absolute corner is cell * size */
});

/** Callback of a ChunkGridGate: whether a cell may load or change level now.
 *
 * @param world The world
 * @param grid The ChunkGrid entity
 * @param cell The absolute cell
 * @param level The level it would get
 * @param ctx The ctx of the gate
 * @return Whether the cell goes ahead, else it waits and asks again next frame
 */
typedef bool (*box_chunk_ready_t)(
    ecs_world_t *world,
    ecs_entity_t grid,
    const int32_t cell[3],
    int32_t level,
    void *ctx);

/** Holds cells back until a condition is met, on a ChunkGrid entity or as a singleton for all.
 *
 * Every gate that applies must agree. A held cell does not count against the budget.
 */
typedef struct ChunkGridGate {
    box_chunk_ready_t ready; /**< Whether a cell may go ahead, NULL for always */
    void *ctx;               /**< Passed to ready */
} ChunkGridGate;

extern ECS_COMPONENT_DECLARE(ChunkGridGate);

/** Prefabs a Scatter places at most */
#define BOX_WORLD_SCATTER_ITEMS (16)

/** Scatter entities a Scatter keeps clear of at most */
#define BOX_WORLD_SCATTER_AVOID (4)

/** How a Scatter spreads its candidates */
ECS_ENUM(ScatterMode, {
    ScatterJittered,     /**< One candidate per cell of a grid as large as the density asks,
                          *   jittered within it but spacing from the edges of the cell */
    ScatterPoisson       /**< Poisson disk: candidates on a grid of spacing / sqrt(2), each
                          *   kept when no candidate within spacing has a higher priority */
});

/** A prefab of a Scatter and how often it is picked. In script {tree, 3}. */
ECS_STRUCT(ScatterItem, {
    ecs_entity_t prefab; /**< The prefab, 0 for an unused slot */
    float weight;        /**< Weight of the prefab among the items, 0 or less for never */
});

/** Places instances of weighted prefabs over a region, as IsA children of the entity.
 *
 * The region is the cell of the nearest ChunkGrid ancestor (ChunkGridCell), or extent meters
 * along X and Z from the entity. Placement is a function of absolute positions and the seed, so
 * adjacent cells join without seams or duplicates and a revisited cell is the same.
 *
 * Candidates are thinned by density, then filtered by a mask channel of the ground (its soft
 * edge thins them gradually), height and slope, and kept clear of the instances the avoid
 * entities would place. Instances get Position, Rotation and Scale and are created in bulk.
 *
 * @par Ground
 * A ground Terrain places them on its heights through its CPU queries and holds the scatter
 * until they are resident under the region, asking the terrain for them; without one they
 * stand on the plane Y 0 of the parent of the entity, flat. Positions assume that the entity
 * and its ancestors up to the cell are neither rotated nor scaled.
 *
 * @code
 * Scatter: {items: [{oak, 2}, {birch, 1}], density: 2, spacing: 4, ground: hills,
 *           mask_channel: 1, mask_min: 0.5, mask_max: 1, mask_edge: 0.2, slope_max: 30}
 * @endcode
 */
ECS_STRUCT(Scatter, {
    ScatterItem items[16]; /**< Prefabs with their weights, unused slots 0 */
    float density;       /**< Instances per 100 square meters, default 1; spacing caps it */
    float spacing;       /**< Least distance between two instances in meters, default 0 */
    ScatterMode mode;    /**< How candidates spread, default ScatterJittered */
    int32_t seed;        /**< Seed of the placement */
    float extent[2];     /**< Size of the region along X and Z from the entity, 0 0 for the
                          *   cell of the ChunkGrid ancestor */
    ecs_entity_t ground; /**< Terrain to place on, 0 for the plane Y 0 of the parent */
    int32_t mask_channel; /**< Channel 0 to 3 of the masks of the ground Terrain that filters,
                          *   -1 for none, default -1 */
    float mask_min;      /**< Least value of the mask channel, default 0.5 */
    float mask_max;      /**< Greatest value of the mask channel, default 1 */
    float mask_edge;     /**< Width of the band outside min and max over which the density
                          *   fades to 0, default 0 */
    float height_min;    /**< Least height of the ground, applies when below height_max */
    float height_max;    /**< Greatest height of the ground, default 0 for no height filter */
    float slope_min;     /**< Least slope of the ground in degrees, default 0 */
    float slope_max;     /**< Greatest slope of the ground in degrees, default 90 */
    float align;         /**< 0 stands upright, 1 along the normal of the ground, default 0 */
    float yaw;           /**< Random yaw range in degrees, default 360 */
    float scale_min;     /**< Least uniform scale, default 1 */
    float scale_max;     /**< Greatest uniform scale, default 1 */
    float sink;          /**< Meters an instance is lowered into the ground, default 0 */
    ecs_entity_t avoid[4]; /**< Scatter entities whose instances these keep clear of: their
                          *   placement is evaluated where it would be, across cells */
    float avoid_radius;  /**< Least distance from the instances of avoid in meters */
    bool spatial_ignore; /**< Add box.spatial.SpatialIgnore to the instances */
});

/** What the last evaluation of a Scatter placed, written by the engine next to it */
ECS_STRUCT(ScatterStatus, {
    int32_t instances;   /**< Instances placed */
    int32_t candidates;  /**< Candidates in the region */
    int32_t thinned;     /**< Candidates removed by the density or the Poisson spacing */
    int32_t filtered;    /**< Candidates removed by the mask, height or slope filters, or
                          *   where the ground is not known */
    int32_t avoided;     /**< Candidates removed by avoid */
    int32_t evaluations; /**< Evaluations since the Scatter was added */
    bool waiting;        /**< Waits for the heights of the ground under its region */
    float time_ms;       /**< Milliseconds of the last evaluation, deletion included */
    float create_ms;     /**< Of time_ms, the bulk creation of the instances */
    float delete_ms;     /**< Of time_ms, the deletion of the instances before */
    float per_ms;        /**< Instances placed per millisecond of time_ms */
});

/** A focus of the streaming systems: a WorldFocus entity or the camera of the first View */
typedef struct box_world_focus_t {
    ecs_entity_t entity; /**< The focus entity */
    ecs_entity_t scene;  /**< Scene of the entity, 0 for the default scene */
    dvec3 position;      /**< World position: the Position of a root, else the translation
                          *   of its WorldTransform */
} box_world_focus_t;

/** Get the focus positions that streaming systems follow: every WorldFocus entity with a
 * place in the world, or without any the camera of the first View by order.
 *
 * @param world The world
 * @param out Receives the foci, NULL to count them
 * @param max Foci out holds
 * @return Number of foci, which can be more than max
 */
int32_t box_world_focus(
    const ecs_world_t *world,
    box_world_focus_t *out,
    int32_t max);

/** Get the loaded cell of a ChunkGrid at an absolute cell coordinate.
 *
 * @param world The world
 * @param grid The ChunkGrid entity
 * @param cell The absolute cell, Y 0 for a 2D grid
 * @param level Receives the level of the cell, NULL to ignore it
 * @return The cell entity, 0 when the cell is not loaded
 */
ecs_entity_t box_chunk_grid_cell(
    const ecs_world_t *world,
    ecs_entity_t grid,
    const int32_t cell[3],
    int32_t *level);

/** Get how much the stream of the ChunkGrids did so far, for tests: nothing in a frame in
 * which no focus moved, no grid, template or scene changed and no grid has work left.
 *
 * @param world The world
 * @param updates Receives the grids the stream looked at, may be NULL
 * @param scans Receives the scans of the cells of a grid, may be NULL
 */
void box_chunk_grid_work(
    const ecs_world_t *world,
    int64_t *updates,
    int64_t *scans);

/** Get the Scatters the scatter system looked at so far, for tests: those added, set or
 * enabled since its last run and those that wait for their ground.
 *
 * @param world The world
 * @return Number of scatters it compared with their last evaluation
 */
int64_t box_scatter_visits(
    const ecs_world_t *world);

/** Register the box.world module.
 *
 * @param world The world
 */
void BoxWorldImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
