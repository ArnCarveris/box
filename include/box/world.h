/**
 * @file box/world.h
 * @brief Open world: the floating world origin and the focus that streaming systems follow.
 *
 * Position, Rotation and WorldTransform are f32, so content far from the origin loses
 * precision. WorldOrigin holds the absolute position of the local origin in f64; when the focus
 * moves beyond its threshold the content of its scene shifts back by whole steps.
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

/** Distance of the focus from the local origin along an axis that shifts it, by default */
#define BOX_WORLD_ORIGIN_THRESHOLD (2048.0f)

/** Shifts are whole multiples of this distance, by default */
#define BOX_WORLD_ORIGIN_STEP (256.0f)

/** Size of a WorldCell along each axis in meters, a power of two so cell * size is exact */
#define BOX_WORLD_CELL_SIZE (4096.0)

/** Marks the entities that streaming systems follow, such as a player or a camera.
 *
 * The systems take every WorldFocus entity; without any they follow the camera of the first
 * View. The world origin follows the first of them (box_world_focus).
 */
extern ECS_TAG_DECLARE(WorldFocus);

/** Keeps a root entity and its descendants where they are when the world origin shifts, for
 * content placed relative to the local origin rather than in the world */
extern ECS_TAG_DECLARE(WorldOriginFixed);

/** Singleton with the absolute world position of the local origin and when it shifts.
 *
 * Absolute = origin + R * local * meters_per_unit. Past threshold the origin moves by whole
 * steps toward the first focus and its scene moves back, in EcsPostUpdate; a change of the
 * rotation or scale (rotate, scale) moves it to the focus and turns and scales the roots.
 *
 * Without the singleton nothing shifts and the origin is zero. The constructor sets the
 * defaults; values apply as specified, a threshold or step of 0 or less never shifts.
 */
ECS_STRUCT(WorldOrigin, {
    double x;            /**< Absolute X of the local origin */
    double y;            /**< Absolute Y of the local origin */
    double z;            /**< Absolute Z of the local origin */
    float threshold;     /**< Distance of the focus along an axis that shifts, default 2048 */
    float step;          /**< Shifts are whole multiples of it, default 256 */
    bool enabled;        /**< Shift when the focus passes the threshold, default true */
    bool every_frame;    /**< Stress: shift by the whole steps nearest to the focus every frame
                          *   it is a step or more away, default false */
    bool from_zero;      /**< Debug: pin the origin to the absolute origin, shifting back to it
                          *   and never away, to show the precision lost far from it, default
                          *   false */
    double q[4];         /**< Rotation of the frame of the local origin in the absolute world,
                          *   a quaternion x y z w; 0 0 0 0 reads as the identity */
    double meters_per_unit; /**< Scale of the frame: absolute meters per local unit, 0 or less
                          *   reads as 1 */
    bool rotate;         /**< Turn the frame so its +Y stays on the local vertical of the nearest
                          *   WorldBody while the focus is within rotate_range radii of its
                          *   surface; off turns it back to the identity, default false */
    float rotate_angle;  /**< Degrees between the up of the frame and the vertical that turn
                          *   it, default 0.25 */
    float rotate_range;  /**< Altitude in radii of the body within which the frame turns,
                          *   default 3 */
    bool scale;          /**< Keep meters_per_unit at the power of two that follows the
                          *   altitude over the nearest WorldBody; off sets it back to 1,
                          *   default false */
    float scale_base;    /**< Altitude in local units the scale aims at, default 64 */
    int32_t scale_max;   /**< Greatest exponent of the scale, default 44 */
});

/** Shifts of the world origin so far, written by the engine next to WorldOrigin */
ECS_STRUCT(WorldOriginStatus, {
    int32_t shifts;      /**< Shifts since the singleton was added */
    double x;            /**< X the origin moved by in the last shift */
    double y;            /**< Y the origin moved by in the last shift */
    double z;            /**< Z the origin moved by in the last shift */
    char reason;         /**< Why the last shift happened: 'T' the threshold, 'O' every_frame,
                          *   'Z' from_zero, 'M' a box_world_origin_shift call, 'R' the
                          *   rotation of the frame, 'S' its scale, 0 for none */
    int32_t written;     /**< Components the last shift wrote */
    float ms;            /**< Milliseconds the last shift took */
    ecs_entity_t body;   /**< WorldBody nearest to the focus this frame, 0 for none */
    double altitude;     /**< Altitude of the focus over the surface of body in meters,
                          *   computed in f64 from absolute positions */
});

/** A sphere such as a planet that the frame of the world origin follows (WorldOrigin.rotate
 * and WorldOrigin.scale).
 *
 * Its center is the absolute position of its entity: the corner of the cell of a WorldCell
 * root, exact at any distance, else the world position of the entity.
 */
ECS_STRUCT(WorldBody, {
    double radius;       /**< Radius in meters */
});

/** Anchors a root entity at a whole cell of the absolute world: its frame is at the absolute
 * position cell * BOX_WORLD_CELL_SIZE, exact at any distance, and its descendants are placed
 * relative to it in f32.
 *
 * The engine owns the Position of the root: the corner of the cell relative to the local
 * origin in f64, set with the WorldCell and on every shift. It has DynamicTransform, so a
 * shift writes the root and none of its descendants, which move with it on the GPU.
 *
 * It has Rotation and Scale too, which the engine sets to the inverse of the rotation and scale
 * of the frame of the local origin (WorldOrigin.rotate, WorldOrigin.scale).
 */
ECS_STRUCT(WorldCell, {
    int32_t x;           /**< Absolute cell along X */
    int32_t y;           /**< Absolute cell along Y */
    int32_t z;           /**< Absolute cell along Z */
});

/** Event emitted on the WorldOrigin singleton entity with every shift, its payload the shift.
 *
 * Observe it with a term on WorldOrigin and the event ecs_id(WorldOriginShift); local positions
 * moved by minus the offset in the frame of the event.
 */
ECS_STRUCT(WorldOriginShift, {
    double x;            /**< X the origin moved by */
    double y;            /**< Y the origin moved by */
    double z;            /**< Z the origin moved by */
    ecs_entity_t scene;  /**< Scene whose content moved, 0 for the default scene */
    bool frame;          /**< The rotation or scale of the frame changed too, so local
                          *   positions moved by more than the offset */
});

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

/** Cells of a ChunkGrid, written by the engine next to it every frame */
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
    vec3 position;       /**< Local world position: the Position of a root, else the
                          *   translation of its WorldTransform */
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

/** Get the absolute world position of a local position: origin + R * local *
 * meters_per_unit.
 *
 * @param world The world
 * @param local The local position
 * @param out Receives the absolute position
 */
void box_world_to_absolute(
    const ecs_world_t *world,
    const vec3 local,
    double out[3]);

/** Get the local position of an absolute world position:
 * R^T * (absolute - origin) / meters_per_unit.
 *
 * @param world The world
 * @param absolute The absolute position
 * @param out Receives the local position
 */
void box_world_to_local(
    const ecs_world_t *world,
    const double absolute[3],
    vec3 out);

/** Shift the world origin of a scene now: the origin moves by offset, the root positions,
 * cameras, particles, spatial index and render caches of the scene by minus offset.
 *
 * The rebase system calls it with whole steps; a call with other offsets applies as given.
 * The transforms follow in EcsPreStore, as for any change of Position.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param offset Distance the origin moves along each axis
 */
void box_world_origin_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3]);

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

/** Register the box.world module.
 *
 * Adds the script functions box.world.absolute_x, absolute_y and absolute_z, which take a
 * local coordinate, and local_x, local_y and local_z, which take an absolute one, all f64.
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
