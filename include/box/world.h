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
 * Absolute = origin + local. When the first focus is beyond threshold along an axis, the origin
 * moves along it by the whole steps nearest to the focus and the content of the scene of the
 * focus moves back by as much, in EcsPostUpdate before the transforms are computed.
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
});

/** Shifts of the world origin so far, written by the engine next to WorldOrigin */
ECS_STRUCT(WorldOriginStatus, {
    int32_t shifts;      /**< Shifts since the singleton was added */
    double x;            /**< X the origin moved by in the last shift */
    double y;            /**< Y the origin moved by in the last shift */
    double z;            /**< Z the origin moved by in the last shift */
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

/** Get the absolute world position of a local position: origin + local.
 *
 * @param world The world
 * @param local The local position
 * @param out Receives the absolute position
 */
void box_world_to_absolute(
    const ecs_world_t *world,
    const vec3 local,
    double out[3]);

/** Get the local position of an absolute world position: absolute - origin.
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
