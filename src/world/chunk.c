/**
 * @file world/chunk.c
 * @brief ChunkGrid: streams a template per cell of a 2D or 3D grid around the foci, in rings of
 * detail, nearest first within a per-frame budget, with hysteresis.
 */

#include "world.h"
#include <math.h>
#include <stdlib.h>

/** A loaded cell of a grid */
typedef struct box_chunk_cell_t {
    int32_t c[3];                  /**< Absolute cell coordinate */
    int32_t level;                 /**< Level the cell was given */
    ecs_entity_t entity;           /**< The cell entity */
    uint64_t seen;                 /**< Scan that last found it in range */
    struct box_chunk_cell_t *next; /**< Next cell with the same hash */
} box_chunk_cell_t;

/** A load or level change waiting for the budget */
typedef struct box_chunk_work_t {
    int32_t c[3];                  /**< Absolute cell coordinate */
    int32_t level;                 /**< Level the cell gets */
    double distance;               /**< Distance of its nearest point from the nearest focus */
    box_chunk_cell_t *cell;        /**< The loaded cell for a level change, NULL for a load */
} box_chunk_work_t;

/** The cells of a grid and what they were made from */
typedef struct box_chunk_state_t {
    ecs_map_t cells;               /**< Hash of the cell coordinate to box_chunk_cell_t* */
    int32_t count;                 /**< Loaded cells */
    ChunkGrid config;              /**< Config of the last scan */
    bool scanned;                  /**< Whether config holds a scan */
    ecs_vec_t foci;                /**< double[3], absolute foci of the last scan */
    ecs_vec_t work;                /**< box_chunk_work_t of the last scan */
    int32_t unloading;             /**< Cells out of range that wait for the budget */
    uint64_t stamp;                /**< Number of the last scan */
    int32_t loaded[BOX_WORLD_CHUNK_LEVELS]; /**< Loaded cells per level */
    double slack;                  /**< Distance below which a move of the foci since the last
                                    *   scan changes neither the cells it visits nor a level */
    bool idle;                     /**< Whether the status holds a frame without work */
} box_chunk_state_t;

/** Engine state of a ChunkGrid, added with it */
typedef struct ChunkGridState {
    box_chunk_state_t *state;      /**< The cells, created by the first scan */
} ChunkGridState;

static ECS_COMPONENT_DECLARE(ChunkGridState);

/** A focus of a run of the stream */
typedef struct box_chunk_focus_t {
    ecs_entity_t entity;           /**< The focus entity */
    ecs_entity_t scene;            /**< Its scene */
    double p[3];                   /**< Its absolute position */
} box_chunk_focus_t;

/** What the stream of all grids ran on last, so a frame without a change returns at once */
typedef struct box_chunk_stream_t {
    uint64_t version;              /**< Bumped when a grid, a template or a scene changes */
    uint64_t seen;                 /**< Version of the last run */
    bool ran;                      /**< Whether the stream ran before */
    bool active;                   /**< Whether a grid has work, deletions or a status left */
    ecs_vec_t foci;                /**< box_chunk_focus_t of the last run */
    ecs_vec_t now;                 /**< box_chunk_focus_t of this frame */
    ecs_vec_t own;                 /**< double[3], scratch for the foci of a grid */
    int64_t updates;               /**< Grids the stream looked at so far */
    int64_t scans;                 /**< Scans of the cells of a grid so far */
} box_chunk_stream_t;

/** Hash a cell coordinate.
 *
 * @param c The cell
 * @return The hash
 */
static uint64_t box_chunk_hash(
    const int32_t c[3])
{
    uint64_t h = (uint64_t)(uint32_t)c[0] * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)c[1] * 0xC2B2AE3D27D4EB4Full;
    h ^= (uint64_t)(uint32_t)c[2] * 0x165667B19E3779F9ull;
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;

    return h;
}

/** Find a loaded cell.
 *
 * @param state The state of the grid
 * @param c The cell
 * @return The cell, NULL when it is not loaded
 */
static box_chunk_cell_t* box_chunk_find(
    const box_chunk_state_t *state,
    const int32_t c[3])
{
    ecs_map_val_t *head = ecs_map_get(&state->cells, box_chunk_hash(c));
    box_chunk_cell_t *cell = head ? (box_chunk_cell_t*)(uintptr_t)*head : NULL;
    while (cell && (cell->c[0] != c[0] || cell->c[1] != c[1] || cell->c[2] != c[2])) {
        cell = cell->next;
    }

    return cell;
}

/** Add a loaded cell.
 *
 * @param state The state of the grid
 * @param c The cell
 * @param level The level of the cell
 * @param entity The cell entity
 * @return The cell
 */
static box_chunk_cell_t* box_chunk_insert(
    box_chunk_state_t *state,
    const int32_t c[3],
    int32_t level,
    ecs_entity_t entity)
{
    box_chunk_cell_t *cell = ecs_os_calloc_t(box_chunk_cell_t);
    ecs_map_val_t *head = ecs_map_ensure(&state->cells, box_chunk_hash(c));
    ecs_os_memcpy_n(cell->c, c, int32_t, 3);
    cell->level = level;
    cell->entity = entity;
    cell->seen = state->stamp;
    cell->next = (box_chunk_cell_t*)(uintptr_t)*head;
    *head = (ecs_map_val_t)(uintptr_t)cell;
    state->count ++;
    if (level >= 0 && level < BOX_WORLD_CHUNK_LEVELS) {
        state->loaded[level] ++;
    }

    return cell;
}

/** Remove a loaded cell and free it, without deleting its entity.
 *
 * @param state The state of the grid
 * @param cell The cell
 */
static void box_chunk_remove(
    box_chunk_state_t *state,
    box_chunk_cell_t *cell)
{
    uint64_t key = box_chunk_hash(cell->c);
    ecs_map_val_t *head = ecs_map_get(&state->cells, key);
    box_chunk_cell_t *prev = NULL;
    box_chunk_cell_t *cur = head ? (box_chunk_cell_t*)(uintptr_t)*head : NULL;
    while (cur && cur != cell) {
        prev = cur;
        cur = cur->next;
    }

    if (cur && prev) {
        prev->next = cur->next;
    } else if (cur && cur->next) {
        *head = (ecs_map_val_t)(uintptr_t)cur->next;
    } else if (cur) {
        ecs_map_remove(&state->cells, key);
    }

    if (cur) {
        state->count --;
        if (cur->level >= 0 && cur->level < BOX_WORLD_CHUNK_LEVELS) {
            state->loaded[cur->level] --;
        }

        ecs_os_free(cur);
    }
}

/** Collect the loaded cells of a grid.
 *
 * @param state The state of the grid
 * @param out Receives box_chunk_cell_t*
 */
static void box_chunk_collect(
    const box_chunk_state_t *state,
    ecs_vec_t *out)
{
    ecs_map_iter_t it = ecs_map_iter(&state->cells);
    ecs_vec_init_t(NULL, out, box_chunk_cell_t*, state->count);
    while (ecs_map_next(&it)) {
        box_chunk_cell_t *cell = (box_chunk_cell_t*)(uintptr_t)ecs_map_value(&it);
        while (cell) {
            *ecs_vec_append_t(NULL, out, box_chunk_cell_t*) = cell;
            cell = cell->next;
        }
    }
}

/** Free the cells of a grid state, without deleting their entities.
 *
 * @param state The state
 */
static void box_chunk_state_clear(
    box_chunk_state_t *state)
{
    ecs_vec_t cells;
    int32_t i;
    box_chunk_collect(state, &cells);
    for (i = 0; i < ecs_vec_count(&cells); i ++) {
        ecs_os_free(*ecs_vec_get_t(&cells, box_chunk_cell_t*, i));
    }

    ecs_vec_fini_t(NULL, &cells, box_chunk_cell_t*);
    ecs_map_clear(&state->cells);
    state->count = 0;
    ecs_os_memset_n(state->loaded, 0, int32_t, BOX_WORLD_CHUNK_LEVELS);
}

/** Free a grid state.
 *
 * @param state The state, may be NULL
 */
static void box_chunk_state_free(
    box_chunk_state_t *state)
{
    if (state) {
        box_chunk_state_clear(state);
        ecs_map_fini(&state->cells);
        ecs_vec_fini(NULL, &state->foci, ECS_SIZEOF(double) * 3);
        ecs_vec_fini_t(NULL, &state->work, box_chunk_work_t);
        ecs_os_free(state);
    }
}

/** Destruct ChunkGridState values, freeing their cells.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_chunk_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ChunkGridState *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_chunk_state_free(values[i].state);
        values[i].state = NULL;
    }
}

/** Move ChunkGridState values, leaving the source empty.
 *
 * @param dst The destination
 * @param src The source
 * @param count Number of components
 * @param ti Type info
 */
static void box_chunk_state_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

/** Construct ChunkGrid values with the default cell size, ring, hysteresis and budget.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_chunk_grid_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ChunkGrid *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ChunkGrid){
            .cell_size = {32, 0, 32},
            .levels = {128},
            .hysteresis = 8,
            .budget = 16
        };
    }
}

/** Count the rings of a grid: the leading radii above 0.
 *
 * @param grid The grid
 * @return Number of levels
 */
static int32_t box_chunk_level_count(
    const ChunkGrid *grid)
{
    int32_t result = 0;
    while (result < BOX_WORLD_CHUNK_LEVELS && grid->levels[result] > 0) {
        result ++;
    }

    return result;
}

/** Get the first level whose radius plus an offset holds a distance.
 *
 * @param grid The grid
 * @param count Number of levels
 * @param d The distance
 * @param offset Added to every radius
 * @return The level, -1 beyond the last
 */
static int32_t box_chunk_level_at(
    const ChunkGrid *grid,
    int32_t count,
    double d,
    double offset)
{
    int32_t result = -1, l;
    for (l = 0; l < count && result < 0; l ++) {
        if (d <= (double)grid->levels[l] + offset) {
            result = l;
        }
    }

    return result;
}

/** Get the level a loaded cell moves to: it only leaves its level when its distance is past
 * the radius by the hysteresis.
 *
 * @param grid The grid
 * @param count Number of levels
 * @param level The level of the cell
 * @param d The distance of the cell
 * @return The new level, -1 to unload
 */
static int32_t box_chunk_level_next(
    const ChunkGrid *grid,
    int32_t count,
    int32_t level,
    double d)
{
    double h = grid->hysteresis > 0 ? (double)grid->hysteresis : 0.0;
    int32_t result = level;
    if (level < 0 || level >= count || d > (double)grid->levels[level] + h) {
        result = box_chunk_level_at(grid, count, d, h);
    } else if (level > 0 && d <= (double)grid->levels[level - 1] - h) {
        result = box_chunk_level_at(grid, count, d, -h);
    }

    return result;
}

/** Whether a grid is 3D: its cells have a height.
 *
 * @param grid The grid
 * @return Whether the grid is 3D
 */
static bool box_chunk_is_3d(
    const ChunkGrid *grid)
{
    return grid->cell_size[1] > 0;
}

/** Get the distance of the nearest point of a cell from a focus, horizontal for a 2D grid.
 *
 * @param grid The grid
 * @param c The cell
 * @param p The absolute focus
 * @return The distance
 */
static double box_chunk_distance(
    const ChunkGrid *grid,
    const int32_t c[3],
    const double p[3])
{
    double sum = 0;
    int32_t a;
    for (a = 0; a < 3; a ++) {
        double s = (double)grid->cell_size[a];
        if (a == 1 && !box_chunk_is_3d(grid)) {
            continue;
        }

        double lo = (double)c[a] * s, hi = lo + s;
        double d = p[a] < lo ? lo - p[a] : p[a] > hi ? p[a] - hi : 0.0;
        sum += d * d;
    }

    return sqrt(sum);
}

/** Get the range of cells around a focus that a scan visits, within the extent.
 *
 * @param grid The grid
 * @param p The absolute focus
 * @param reach Distance from the focus that is visited
 * @param lo Receives the first cell along each axis
 * @param hi Receives the last cell along each axis
 * @return Whether the range holds a cell
 */
static bool box_chunk_range(
    const ChunkGrid *grid,
    const double p[3],
    double reach,
    int64_t lo[3],
    int64_t hi[3])
{
    bool result = true;
    int32_t a;
    for (a = 0; a < 3; a ++) {
        double s = (double)grid->cell_size[a];
        if (a == 1 && !box_chunk_is_3d(grid)) {
            lo[a] = hi[a] = 0;
            continue;
        }

        lo[a] = (int64_t)ceil((p[a] - reach) / s) - 1;
        hi[a] = (int64_t)floor((p[a] + reach) / s);
        if (grid->extent[a] > 0) {
            lo[a] = lo[a] < -grid->extent[a] ? -grid->extent[a] : lo[a];
            hi[a] = hi[a] > grid->extent[a] - 1 ? grid->extent[a] - 1 : hi[a];
        }

        lo[a] = lo[a] < INT32_MIN ? INT32_MIN : lo[a];
        hi[a] = hi[a] > INT32_MAX ? INT32_MAX : hi[a];
        result = result && lo[a] <= hi[a];
    }

    return result;
}

/** Set a numeric prop of a template value when the template declares it.
 *
 * @param world The world
 * @param type The template
 * @param ptr The value
 * @param name The prop
 * @param value The number
 */
static void box_chunk_prop_int(
    const ecs_world_t *world,
    ecs_entity_t type,
    void *ptr,
    const char *name,
    int64_t value)
{
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, type, ptr);
    if (!ecs_meta_push(&cur) && !ecs_meta_try_member(&cur, name)) {
        ecs_meta_set_int(&cur, value);
    }
}

/** Set the size prop of a template value when the template declares it: the cell size for a
 * struct such as vec3, the X of it for a number.
 *
 * @param world The world
 * @param type The template
 * @param ptr The value
 * @param size The cell size
 */
static void box_chunk_prop_size(
    const ecs_world_t *world,
    ecs_entity_t type,
    void *ptr,
    const vec3 size)
{
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, type, ptr);
    int32_t a;
    if (ecs_meta_push(&cur) || ecs_meta_try_member(&cur, "size")) {
        goto done;
    }

    if (!ecs_has(world, ecs_meta_get_type(&cur), EcsStruct)) {
        ecs_meta_set_float(&cur, (double)size[0]);
    } else if (!ecs_meta_push(&cur)) {
        for (a = 0; a < 3; a ++) {
            if (ecs_meta_set_float(&cur, (double)size[a]) || ecs_meta_next(&cur)) {
                break;
            }
        }
    }

done:
    return;
}

/** Check whether an entity is a script template that a cell can instantiate.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is a template
 */
static bool box_chunk_is_template(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const EcsScript *script = e && ecs_is_alive(world, e) ? ecs_get(world, e, EcsScript) : NULL;

    return script && script->template_ && ecs_get_type_info(world, e);
}

/** Check whether the terrain a grid stands on has the heights under a cell, asking for them
 * when not: the rectangle of the cell in XZ, in the frame of the terrain.
 *
 * @param world The world
 * @param config The grid
 * @param work The cell
 * @return Whether the cell may go ahead, always without a ground
 */
static bool box_chunk_ground_ready(
    ecs_world_t *world,
    const ChunkGrid *config,
    const box_chunk_work_t *work)
{
    const WorldOrigin *o = ecs_id(WorldOrigin) ? ecs_singleton_get(world, WorldOrigin) : NULL;
    double frame[3], r[4];
    bool result = true;
    if (!config->ground || !ecs_id(Terrain) || !ecs_is_alive(world, config->ground) ||
        !ecs_has(world, config->ground, Terrain))
    {
        goto done;
    }

    box_terrain_origin(world, config->ground, frame);
    r[0] = (double)work->c[0] * (double)config->cell_size[0] - (o ? o->x : 0.0) - frame[0];
    r[1] = (double)work->c[2] * (double)config->cell_size[2] - (o ? o->z : 0.0) - frame[2];
    r[2] = r[0] + (double)config->cell_size[0];
    r[3] = r[1] + (double)config->cell_size[2];
    result = box_terrain_resident(world, config->ground, r[0], r[1], r[2], r[3]);
    if (!result) {
        box_terrain_request(world, config->ground, r[0], r[1], r[2], r[3]);
    }

done:
    return result;
}

/** Ask the gates of a grid whether a cell may go ahead: the terrain it stands on, the gate of
 * the grid entity and the singleton gate.
 *
 * @param world The world
 * @param grid The grid entity
 * @param config The grid
 * @param work The cell
 * @return Whether it goes ahead
 */
static bool box_chunk_ready(
    ecs_world_t *world,
    ecs_entity_t grid,
    const ChunkGrid *config,
    const box_chunk_work_t *work)
{
    const ChunkGridGate *own = ecs_get(world, grid, ChunkGridGate);
    const ChunkGridGate *all = ecs_singleton_get(world, ChunkGridGate);
    bool result = box_chunk_ground_ready(world, config, work);
    if (result && own && own->ready) {
        result = own->ready(world, grid, work->c, work->level, own->ctx);
    }

    if (result && all && all != own && all->ready) {
        result = all->ready(world, grid, work->c, work->level, all->ctx);
    }

    return result;
}

/** Create the entity of a cell: a child of the grid at its corner with its ChunkGridCell and
 * the template set.
 *
 * @param world The world
 * @param grid The grid entity
 * @param config The grid
 * @param anchor Absolute position of the grid entity
 * @param work The cell
 * @return The cell entity
 */
static ecs_entity_t box_chunk_create(
    ecs_world_t *world,
    ecs_entity_t grid,
    const ChunkGrid *config,
    const double anchor[3],
    const box_chunk_work_t *work)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, grid);
    double corner[3];
    int32_t a;
    for (a = 0; a < 3; a ++) {
        corner[a] = (double)work->c[a] * (double)config->cell_size[a];
    }

    ecs_set(world, result, Position, {
        (float)(corner[0] - anchor[0]),
        (float)(corner[1] - anchor[1]),
        (float)(corner[2] - anchor[2])
    });

    ecs_set(world, result, ChunkGridCell, {
        .cell = {work->c[0], work->c[1], work->c[2]},
        .level = work->level,
        .size = {config->cell_size[0], config->cell_size[1], config->cell_size[2]}
    });

    ecs_value_t value = ecs_value_new(world, config->template);
    box_chunk_prop_int(world, config->template, value.ptr, "cell_x", work->c[0]);
    box_chunk_prop_int(world, config->template, value.ptr, "cell_y", work->c[1]);
    box_chunk_prop_int(world, config->template, value.ptr, "cell_z", work->c[2]);
    box_chunk_prop_int(world, config->template, value.ptr, "level", work->level);
    box_chunk_prop_int(world, config->template, value.ptr, "seed", config->seed);
    box_chunk_prop_size(world, config->template, value.ptr, config->cell_size);
    const ecs_type_info_t *ti = ecs_get_type_info(world, config->template);
    ecs_set_id(world, result, config->template, (size_t)ti->size, value.ptr);
    ecs_value_fini(world, &value);

    return result;
}

/** Set the level of a loaded cell in its ChunkGridCell and its level prop, which the template
 * re-evaluates.
 *
 * @param world The world
 * @param config The grid
 * @param cell The cell
 * @param level The new level
 */
static void box_chunk_set_level(
    ecs_world_t *world,
    const ChunkGrid *config,
    box_chunk_cell_t *cell,
    int32_t level)
{
    const void *ptr = NULL;
    const ecs_type_info_t *ti = ecs_get_type_info(world, config->template);
    ChunkGridCell *own = ecs_get_mut(world, cell->entity, ChunkGridCell);
    cell->level = level;
    if (own) {
        own->level = level;
        ecs_modified(world, cell->entity, ChunkGridCell);
    }

    ptr = ecs_get_id(world, cell->entity, config->template);
    if (ptr && ti) {
        ecs_value_t value = ecs_value_init(world, config->template, ptr);
        box_chunk_prop_int(world, config->template, value.ptr, "level", level);
        ecs_set_id(world, cell->entity, config->template, (size_t)ti->size, value.ptr);
        ecs_value_fini(world, &value);
    }
}

/** Delete the entity of a cell and count the time it took.
 *
 * @param world The world
 * @param e The cell entity
 * @param status Counts the deleted cell and its time
 */
static void box_chunk_delete(
    ecs_world_t *world,
    ecs_entity_t e,
    ChunkGridStatus *status)
{
    ecs_time_t t;
    ecs_os_get_time(&t);
    ecs_delete(world, e);
    status->delete_ms += (float)(ecs_time_measure(&t) * 1000.0);
    status->deleted ++;
}

/** Delete every cell of a grid.
 *
 * @param world The world
 * @param state The state of the grid
 * @param status Counts the deleted cells
 */
static void box_chunk_unload_all(
    ecs_world_t *world,
    box_chunk_state_t *state,
    ChunkGridStatus *status)
{
    ecs_vec_t cells;
    int32_t i;
    box_chunk_collect(state, &cells);
    for (i = 0; i < ecs_vec_count(&cells); i ++) {
        box_chunk_cell_t *cell = *ecs_vec_get_t(&cells, box_chunk_cell_t*, i);
        if (ecs_is_alive(world, cell->entity)) {
            box_chunk_delete(world, cell->entity, status);
        }
    }

    ecs_vec_fini_t(NULL, &cells, box_chunk_cell_t*);
    box_chunk_state_clear(state);
    state->unloading = 0;
}

/** Get the absolute positions of the foci of a grid: those in its scene, else all of them.
 *
 * @param world The world
 * @param grid The grid entity
 * @param foci The foci of the frame
 * @param out Receives double[3] per focus
 */
static void box_chunk_foci(
    const ecs_world_t *world,
    ecs_entity_t grid,
    const ecs_vec_t *foci,
    ecs_vec_t *out)
{
    int32_t count = ecs_vec_count(foci), i, own = 0;
    const box_chunk_focus_t *src = ecs_vec_first_t(foci, box_chunk_focus_t);
    ecs_entity_t scene = box_world_scene_of(world, grid);
    for (i = 0; i < count; i ++) {
        own += src[i].scene == scene;
    }

    ecs_vec_clear(out);
    for (i = 0; i < count; i ++) {
        if (own && src[i].scene != scene) {
            continue;
        }

        double *p = ecs_vec_append(NULL, out, ECS_SIZEOF(double) * 3);
        ecs_os_memcpy_n(p, src[i].p, double, 3);
    }
}

/** Lower the slack of a grid to a margin.
 *
 * @param state The state of the grid
 * @param margin Distance a focus can move before something changes
 */
static void box_chunk_slack(
    box_chunk_state_t *state,
    double margin)
{
    if (!(margin >= state->slack)) {
        state->slack = margin > 0 ? margin : 0;
    }
}

/** Order work by level, more detail first, then nearest first.
 *
 * @param a The first work
 * @param b The second work
 * @return Order of a relative to b
 */
static int box_chunk_work_compare(
    const void *a,
    const void *b)
{
    const box_chunk_work_t *wa = a, *wb = b;
    int result = (wa->level > wb->level) - (wa->level < wb->level);
    if (!result) {
        result = (wa->distance > wb->distance) - (wa->distance < wb->distance);
    }

    return result;
}

/** Visit a cell in range of a scan: queue its load or level change. A loaded cell past the
 * last ring is left unseen, which box_chunk_unload_unseen deletes. Lowers the slack of the grid
 * to the distance the cell is from its next load, level change or unload.
 *
 * @param world The world
 * @param state The state of the grid
 * @param config The grid
 * @param c The cell
 * @param d Distance of the cell from the nearest focus
 */
static void box_chunk_visit(
    ecs_world_t *world,
    box_chunk_state_t *state,
    const ChunkGrid *config,
    const int32_t c[3],
    double d)
{
    int32_t count = box_chunk_level_count(config), l;
    double h = config->hysteresis > 0 ? (double)config->hysteresis : 0.0;
    box_chunk_cell_t *cell = box_chunk_find(state, c);
    int32_t level;
    if (cell && !ecs_is_alive(world, cell->entity)) {
        box_chunk_remove(state, cell);
        cell = NULL;
    }

    if (cell) {
        level = box_chunk_level_next(config, count, cell->level, d);
        cell->seen = level >= 0 ? state->stamp : cell->seen;
    } else {
        level = box_chunk_level_at(config, count, d, 0.0);
    }

    if (level >= 0) {
        box_chunk_slack(state, (double)config->levels[level] + h - d);
        if (level > 0) {
            box_chunk_slack(state, d - ((double)config->levels[level - 1] - h));
        }
    } else {
        for (l = 0; l < count; l ++) {
            box_chunk_slack(state, d - (double)config->levels[l]);
        }
    }

    if (level >= 0 && (!cell || level != cell->level)) {
        box_chunk_work_t *work = ecs_vec_append_t(NULL, &state->work, box_chunk_work_t);
        ecs_os_memcpy_n(work->c, c, int32_t, 3);
        work->level = level;
        work->distance = d;
        work->cell = cell;
    }
}

/** Visit the cells in range of every focus once each, the union of the foci. Sets the slack
 * of the grid: while every focus moved less than it since the scan, a scan would visit the same
 * cells and find no load, level change or unload.
 *
 * @param world The world
 * @param state The state of the grid, its foci set
 * @param config The grid
 */
static void box_chunk_scan(
    ecs_world_t *world,
    box_chunk_state_t *state,
    const ChunkGrid *config)
{
    int32_t count = box_chunk_level_count(config), nf = ecs_vec_count(&state->foci), f, g;
    double h = config->hysteresis > 0 ? (double)config->hysteresis : 0.0;
    double reach = count ? (double)config->levels[count - 1] + h : -1.0;
    const double *foci = ecs_vec_first(&state->foci);
    int64_t (*lo)[3] = ecs_os_calloc(ECS_SIZEOF(int64_t) * 3 * (nf ? nf : 1));
    int64_t (*hi)[3] = ecs_os_calloc(ECS_SIZEOF(int64_t) * 3 * (nf ? nf : 1));
    bool *valid = ecs_os_calloc_n(bool, nf ? nf : 1);
    int64_t x, y, z;
    int32_t a;
    state->slack = INFINITY;
    for (f = 0; f < nf && reach >= 0; f ++) {
        valid[f] = box_chunk_range(config, &foci[f * 3], reach, lo[f], hi[f]);
        for (a = 0; a < 3; a ++) {
            double s = (double)config->cell_size[a];
            double u = foci[f * 3 + a] - reach, v = foci[f * 3 + a] + reach;
            if (a == 1 && !box_chunk_is_3d(config)) {
                continue;
            }

            double first = (ceil(u / s) - 1.0) * s, last = floor(v / s) * s;
            box_chunk_slack(state, u - first);
            box_chunk_slack(state, first + s - u);
            box_chunk_slack(state, v - last);
            box_chunk_slack(state, last + s - v);
        }

        for (x = lo[f][0]; valid[f] && x <= hi[f][0]; x ++) {
            for (y = lo[f][1]; y <= hi[f][1]; y ++) {
                for (z = lo[f][2]; z <= hi[f][2]; z ++) {
                    int32_t c[3] = { (int32_t)x, (int32_t)y, (int32_t)z };
                    bool visited = false;
                    double d = INFINITY;
                    for (g = 0; g < nf; g ++) {
                        visited = visited || (g < f && valid[g] &&
                            x >= lo[g][0] && x <= hi[g][0] && y >= lo[g][1] &&
                            y <= hi[g][1] && z >= lo[g][2] && z <= hi[g][2]);
                        double dg = box_chunk_distance(config, c, &foci[g * 3]);
                        d = dg < d ? dg : d;
                    }

                    if (!visited) {
                        box_chunk_visit(world, state, config, c, d);
                    }
                }
            }
        }
    }

    ecs_os_free(lo);
    ecs_os_free(hi);
    ecs_os_free(valid);
}

/** Delete the cells that the last scan left unseen, out of range of every focus, within the
 * budget_ms of the frame. The cells over the budget stay loaded and unseen until a later scan
 * deletes them, or finds them in range again and keeps them.
 *
 * @param world The world
 * @param state The state of the grid
 * @param config The grid
 * @param start Start of the work of the grid this frame
 * @param status Counts the deleted cells and those left unloading
 */
static void box_chunk_unload_unseen(
    ecs_world_t *world,
    box_chunk_state_t *state,
    const ChunkGrid *config,
    const ecs_time_t *start,
    ChunkGridStatus *status)
{
    ecs_vec_t cells;
    int32_t i, done = 0;
    box_chunk_collect(state, &cells);
    state->unloading = 0;
    for (i = 0; i < ecs_vec_count(&cells); i ++) {
        box_chunk_cell_t *cell = *ecs_vec_get_t(&cells, box_chunk_cell_t*, i);
        if (cell->seen == state->stamp) {
            continue;
        }

        ecs_time_t t = *start;
        if (done > 0 && config->budget_ms > 0 &&
            ecs_time_measure(&t) * 1000.0 >= config->budget_ms)
        {
            state->unloading ++;
            continue;
        }

        if (ecs_is_alive(world, cell->entity)) {
            box_chunk_delete(world, cell->entity, status);
            done ++;
        }

        box_chunk_remove(state, cell);
    }

    status->unloading = state->unloading;
    ecs_vec_fini_t(NULL, &cells, box_chunk_cell_t*);
}

/** Load and change cells from the queued work, in order, within the budget.
 *
 * The budget_ms counts from the start of the work of the grid, the deletions of the frame
 * included, but at least one cell goes per frame.
 *
 * @param world The world
 * @param grid The grid entity
 * @param state The state of the grid
 * @param config The grid
 * @param start Start of the work of the grid this frame
 * @param status Counts the created, changed, pending and held cells
 */
static void box_chunk_apply(
    ecs_world_t *world,
    ecs_entity_t grid,
    box_chunk_state_t *state,
    const ChunkGrid *config,
    const ecs_time_t *start,
    ChunkGridStatus *status)
{
    int32_t count = ecs_vec_count(&state->work), i, done = 0;
    box_chunk_work_t *work = ecs_vec_first_t(&state->work, box_chunk_work_t);
    double anchor[3] = {0, 0, 0};
    vec3 local = {0, 0, 0};
    ecs_time_t t, op;
    box_world_position(world, grid, local);
    box_world_to_absolute(world, local, anchor);
    if (count > 1) {
        qsort(work, (size_t)count, sizeof(box_chunk_work_t), box_chunk_work_compare);
    }

    for (i = 0; i < count; i ++) {
        t = *start;
        bool over = (config->budget > 0 && done >= config->budget) || (done > 0 &&
            config->budget_ms > 0 && ecs_time_measure(&t) * 1000.0 >= config->budget_ms);
        if (over) {
            status->pending += count - i;
            break;
        }

        if (!box_chunk_ready(world, grid, config, &work[i])) {
            status->pending ++;
            status->held ++;
            continue;
        }

        ecs_os_get_time(&op);
        if (work[i].cell) {
            int32_t from = work[i].cell->level;
            box_chunk_set_level(world, config, work[i].cell, work[i].level);
            if (from >= 0 && from < BOX_WORLD_CHUNK_LEVELS) {
                state->loaded[from] --;
            }

            state->loaded[work[i].level] ++;
            status->changed ++;
        } else {
            ecs_entity_t e = box_chunk_create(world, grid, config, anchor, &work[i]);
            box_chunk_insert(state, work[i].c, work[i].level, e);
            status->created ++;
        }

        status->create_ms += (float)(ecs_time_measure(&op) * 1000.0);
        done ++;
    }
}

/** Check whether a grid needs a scan: its config changed, work or deletions are pending, or
 * its foci changed and one moved by its slack or more since the last scan.
 *
 * @param state The state of the grid
 * @param config The grid
 * @param foci The absolute foci of this frame
 * @return Whether to scan
 */
static bool box_chunk_dirty(
    const box_chunk_state_t *state,
    const ChunkGrid *config,
    const ecs_vec_t *foci)
{
    int32_t count = ecs_vec_count(foci), i;
    const double *now = ecs_vec_first(foci), *last = ecs_vec_first(&state->foci);
    bool result = !state->scanned || ecs_vec_count(&state->work) > 0 || state->unloading > 0 ||
        ecs_os_memcmp(&state->config, config, ECS_SIZEOF(ChunkGrid)) != 0 ||
        count != ecs_vec_count(&state->foci);
    if (!result && ecs_os_memcmp(now, last, ECS_SIZEOF(double) * 3 * count) != 0) {
        double moved = 0;
        for (i = 0; i < count; i ++) {
            double dx = now[i * 3] - last[i * 3], dy = now[i * 3 + 1] - last[i * 3 + 1];
            double dz = now[i * 3 + 2] - last[i * 3 + 2];
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            moved = d > moved ? d : moved;
        }

        result = !(moved * (1.0 + 1e-9) + 1e-9 < state->slack);
    }

    return result;
}

/** Stream the cells of one grid for this frame. The status is written in a frame with work
 * and in the frame after it.
 *
 * @param world The world
 * @param stream The stream, its now the foci of the frame
 * @param grid The grid entity
 */
static void box_chunk_update(
    ecs_world_t *world,
    box_chunk_stream_t *stream,
    ecs_entity_t grid)
{
    ChunkGridState *s = ecs_get_mut(world, grid, ChunkGridState);
    const ChunkGrid *ptr = ecs_get(world, grid, ChunkGrid);
    ChunkGridStatus status = {0};
    ecs_vec_t *foci = &stream->own;
    bool worked = false;
    ecs_time_t start;
    ecs_os_get_time(&start);
    if (!s || !ptr) {
        goto done;
    }

    stream->updates ++;
    ChunkGrid config = *ptr;
    if (!ecs_has(world, grid, Position)) {
        ecs_set(world, grid, Position, {0, 0, 0});
        s = ecs_get_mut(world, grid, ChunkGridState);
        ptr = ecs_get(world, grid, ChunkGrid);
    }

    if (!s->state) {
        s->state = ecs_os_calloc_t(box_chunk_state_t);
        ecs_map_init(&s->state->cells, NULL);
        ecs_vec_init(NULL, &s->state->foci, ECS_SIZEOF(double) * 3, 0);
        ecs_vec_init_t(NULL, &s->state->work, box_chunk_work_t, 0);
    }

    box_chunk_state_t *state = s->state;
    bool valid = box_chunk_is_template(world, config.template) && config.cell_size[0] > 0 &&
        config.cell_size[2] > 0 && config.cell_size[1] >= 0;
    bool reset = state->scanned && (state->config.template != config.template ||
        ecs_os_memcmp(state->config.cell_size, config.cell_size, ECS_SIZEOF(vec3)) ||
        state->config.seed != config.seed);
    if (reset || !valid) {
        worked = state->count > 0 || ecs_vec_count(&state->work) > 0;
        box_chunk_unload_all(world, state, &status);
        ecs_vec_clear(&state->work);
        state->scanned = valid && state->scanned;
    }

    box_chunk_foci(world, grid, &stream->now, foci);
    if (valid && ecs_vec_count(foci) && box_chunk_dirty(state, &config, foci)) {
        worked = true;
        stream->scans ++;
        state->stamp ++;
        state->config = config;
        state->scanned = true;
        ecs_vec_clear(&state->foci);
        ecs_os_memcpy(ecs_vec_grow(NULL, &state->foci, ECS_SIZEOF(double) * 3,
            ecs_vec_count(foci)), ecs_vec_first(foci),
            ECS_SIZEOF(double) * 3 * ecs_vec_count(foci));
        ecs_vec_clear(&state->work);
        box_chunk_scan(world, state, &config);
        box_chunk_unload_unseen(world, state, &config, &start, &status);
        box_chunk_apply(world, grid, state, &config, &start, &status);
        if (!status.pending) {
            ecs_vec_clear(&state->work);
        }
    }

    ecs_os_memcpy_n(status.loaded, state->loaded, int32_t, BOX_WORLD_CHUNK_LEVELS);
    status.cells = state->count;
    if (!worked && state->idle) {
        goto idle;
    }

    state->idle = !worked;
    stream->active = stream->active || worked;
done:
    status.time_ms = (float)(ecs_time_measure(&start) * 1000.0);
    if (ptr && ecs_is_alive(world, grid)) {
        ecs_set_ptr(world, grid, ChunkGridStatus, &status);
    }

idle:
    return;
}

/** Collect the foci of the frame with their scenes and absolute positions.
 *
 * @param world The world
 * @param out Receives box_chunk_focus_t
 */
static void box_chunk_stream_foci(
    const ecs_world_t *world,
    ecs_vec_t *out)
{
    box_world_focus_t local[8], *foci = local;
    int32_t count = box_world_focus(world, local, 8), i;
    if (count > 8) {
        foci = ecs_os_calloc_n(box_world_focus_t, count);
        count = box_world_focus(world, foci, count);
    }

    ecs_vec_clear(out);
    for (i = 0; i < count; i ++) {
        box_chunk_focus_t *f = ecs_vec_append_t(NULL, out, box_chunk_focus_t);
        f->entity = foci[i].entity;
        f->scene = foci[i].scene;
        box_world_to_absolute(world, foci[i].position, f->p);
    }

    if (foci != local) {
        ecs_os_free(foci);
    }
}

/** Stream the cells of every ChunkGrid around the foci. Returns at once in a frame in which
 * the foci are where they were, no grid, template or scene changed and no grid has work left.
 *
 * @param it The iterator of the system
 */
static void BoxWorldChunkStream(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_chunk_stream_t *stream = it->ctx;
    ecs_vec_t grids;
    int32_t i, count;
    box_chunk_stream_foci(world, &stream->now);
    count = ecs_vec_count(&stream->now);
    if (stream->ran && !stream->active && stream->seen == stream->version &&
        count == ecs_vec_count(&stream->foci) && !ecs_os_memcmp(ecs_vec_first(&stream->now),
            ecs_vec_first(&stream->foci), ECS_SIZEOF(box_chunk_focus_t) * count))
    {
        goto done;
    }

    stream->ran = true;
    stream->active = false;
    stream->seen = stream->version;
    ecs_vec_clear(&stream->foci);
    ecs_os_memcpy(ecs_vec_grow_t(NULL, &stream->foci, box_chunk_focus_t, count),
        ecs_vec_first(&stream->now), ECS_SIZEOF(box_chunk_focus_t) * count);

    ecs_vec_init_t(NULL, &grids, ecs_entity_t, 0);
    ecs_iter_t qit = ecs_each(world, ChunkGrid);
    while (ecs_each_next(&qit)) {
        for (i = 0; i < qit.count; i ++) {
            *ecs_vec_append_t(NULL, &grids, ecs_entity_t) = qit.entities[i];
        }
    }

    for (i = 0; i < ecs_vec_count(&grids); i ++) {
        ecs_entity_t grid = *ecs_vec_get_t(&grids, ecs_entity_t, i);
        if (ecs_is_alive(world, grid) && !ecs_has_id(world, grid, EcsDisabled)) {
            box_chunk_update(world, stream, grid);
        }
    }

    ecs_vec_fini_t(NULL, &grids, ecs_entity_t);
done:
    return;
}

/** Make the stream look at every grid in its next run.
 *
 * @param it The iterator of a hook or observer, its ctx the stream
 */
static void box_chunk_stream_changed(
    ecs_iter_t *it)
{
    box_chunk_stream_t *stream = it->ctx;
    stream->version ++;
}

/** Free the stream.
 *
 * @param ptr The stream
 */
static void box_chunk_stream_free(
    void *ptr)
{
    box_chunk_stream_t *stream = ptr;
    ecs_vec_fini_t(NULL, &stream->foci, box_chunk_focus_t);
    ecs_vec_fini_t(NULL, &stream->now, box_chunk_focus_t);
    ecs_vec_fini(NULL, &stream->own, ECS_SIZEOF(double) * 3);
    ecs_os_free(stream);
}

void box_chunk_grid_work(
    const ecs_world_t *world,
    int64_t *updates,
    int64_t *scans)
{
    const ecs_type_hooks_t *hooks = ecs_get_hooks(world, ChunkGrid);
    const box_chunk_stream_t *stream = hooks ? hooks->ctx : NULL;
    if (updates) {
        *updates = stream ? stream->updates : 0;
    }

    if (scans) {
        *scans = stream ? stream->scans : 0;
    }
}

ecs_entity_t box_chunk_grid_cell(
    const ecs_world_t *world,
    ecs_entity_t grid,
    const int32_t cell[3],
    int32_t *level)
{
    const ChunkGridState *s = ecs_get(world, grid, ChunkGridState);
    const box_chunk_cell_t *found = s && s->state ? box_chunk_find(s->state, cell) : NULL;
    ecs_entity_t result = 0;
    if (found && ecs_is_alive(world, found->entity)) {
        result = found->entity;
        if (level) {
            *level = found->level;
        }
    }

    return result;
}

void box_world_chunk_import(
    ecs_world_t *world)
{
    box_chunk_stream_t *stream = ecs_os_calloc_t(box_chunk_stream_t);
    ecs_vec_init_t(NULL, &stream->foci, box_chunk_focus_t, 0);
    ecs_vec_init_t(NULL, &stream->now, box_chunk_focus_t, 0);
    ecs_vec_init(NULL, &stream->own, ECS_SIZEOF(double) * 3, 0);
    ECS_COMPONENT_DEFINE(world, ChunkGridGate);
    ECS_COMPONENT_DEFINE(world, ChunkGridState);

    ecs_set_hooks(world, ChunkGrid, {
        .ctor = box_chunk_grid_ctor,
        .on_add = box_chunk_stream_changed,
        .on_set = box_chunk_stream_changed,
        .ctx = stream,
        .ctx_free = box_chunk_stream_free
    });
    ecs_set_hooks(world, ChunkGridState, {
        .ctor = flecs_default_ctor,
        .dtor = box_chunk_state_dtor,
        .move = box_chunk_state_move
    });

    ecs_add_pair(world, ecs_id(ChunkGrid), EcsWith, ecs_id(ChunkGridState));
    ecs_add_pair(world, ecs_id(ChunkGrid), EcsWith, ecs_id(ChunkGridStatus));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ChunkGrid) }, { .id = EcsDisabled }},
        .events = { EcsOnRemove },
        .callback = box_chunk_stream_changed,
        .ctx = stream
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(EcsScript) }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = box_chunk_stream_changed,
        .ctx = stream
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Scene) }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_chunk_stream_changed,
        .ctx = stream
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ChunkGridStream" }),
        .phase = EcsPostUpdate,
        .run = BoxWorldChunkStream,
        .ctx = stream,
        .immediate = true
    });
}
