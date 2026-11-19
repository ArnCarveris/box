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
    uint64_t stamp;                /**< Number of the last scan */
} box_chunk_state_t;

/** Engine state of a ChunkGrid, added with it */
typedef struct ChunkGridState {
    box_chunk_state_t *state;      /**< The cells, created by the first scan */
} ChunkGridState;

static ECS_COMPONENT_DECLARE(ChunkGridState);

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

/** Ask the gates of a grid whether a cell may go ahead: the gate of the grid entity and the
 * singleton gate.
 *
 * @param world The world
 * @param grid The grid entity
 * @param work The cell
 * @return Whether it goes ahead
 */
static bool box_chunk_ready(
    ecs_world_t *world,
    ecs_entity_t grid,
    const box_chunk_work_t *work)
{
    const ChunkGridGate *own = ecs_get(world, grid, ChunkGridGate);
    const ChunkGridGate *all = ecs_singleton_get(world, ChunkGridGate);
    bool result = true;
    if (own && own->ready) {
        result = own->ready(world, grid, work->c, work->level, own->ctx);
    }

    if (result && all && all != own && all->ready) {
        result = all->ready(world, grid, work->c, work->level, all->ctx);
    }

    return result;
}

/** Create the entity of a cell: a child of the grid at its corner with the template set.
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

/** Set the level prop of a loaded cell, which the template re-evaluates.
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
    const void *ptr = ecs_get_id(world, cell->entity, config->template);
    const ecs_type_info_t *ti = ecs_get_type_info(world, config->template);
    cell->level = level;
    if (ptr && ti) {
        ecs_value_t value = ecs_value_init(world, config->template, ptr);
        box_chunk_prop_int(world, config->template, value.ptr, "level", level);
        ecs_set_id(world, cell->entity, config->template, (size_t)ti->size, value.ptr);
        ecs_value_fini(world, &value);
    }
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
            ecs_delete(world, cell->entity);
            status->deleted ++;
        }
    }

    ecs_vec_fini_t(NULL, &cells, box_chunk_cell_t*);
    box_chunk_state_clear(state);
}

/** Get the absolute positions of the foci of a grid: those in its scene, else all of them.
 *
 * @param world The world
 * @param grid The grid entity
 * @param out Receives double[3] per focus
 */
static void box_chunk_foci(
    const ecs_world_t *world,
    ecs_entity_t grid,
    ecs_vec_t *out)
{
    int32_t count = box_world_focus(world, NULL, 0), i, own = 0;
    box_world_focus_t *foci = ecs_os_calloc_n(box_world_focus_t, count ? count : 1);
    ecs_entity_t scene = box_world_scene_of(world, grid);
    count = box_world_focus(world, foci, count);
    for (i = 0; i < count; i ++) {
        own += foci[i].scene == scene;
    }

    ecs_vec_clear(out);
    for (i = 0; i < count; i ++) {
        if (own && foci[i].scene != scene) {
            continue;
        }

        double *p = ecs_vec_append(NULL, out, ECS_SIZEOF(double) * 3);
        box_world_to_absolute(world, foci[i].position, p);
    }

    ecs_os_free(foci);
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

/** Visit a cell in range of a scan: queue its load or level change, or unload it.
 *
 * @param world The world
 * @param state The state of the grid
 * @param config The grid
 * @param c The cell
 * @param d Distance of the cell from the nearest focus
 * @param status Counts the deleted cells
 */
static void box_chunk_visit(
    ecs_world_t *world,
    box_chunk_state_t *state,
    const ChunkGrid *config,
    const int32_t c[3],
    double d,
    ChunkGridStatus *status)
{
    int32_t count = box_chunk_level_count(config);
    box_chunk_cell_t *cell = box_chunk_find(state, c);
    int32_t level;
    if (cell && !ecs_is_alive(world, cell->entity)) {
        box_chunk_remove(state, cell);
        cell = NULL;
    }

    if (cell) {
        cell->seen = state->stamp;
        level = box_chunk_level_next(config, count, cell->level, d);
    } else {
        level = box_chunk_level_at(config, count, d, 0.0);
    }

    if (cell && level < 0) {
        ecs_delete(world, cell->entity);
        box_chunk_remove(state, cell);
        status->deleted ++;
    } else if (level >= 0 && (!cell || level != cell->level)) {
        box_chunk_work_t *work = ecs_vec_append_t(NULL, &state->work, box_chunk_work_t);
        ecs_os_memcpy_n(work->c, c, int32_t, 3);
        work->level = level;
        work->distance = d;
        work->cell = cell;
    }
}

/** Visit the cells in range of every focus once each, the union of the foci.
 *
 * @param world The world
 * @param state The state of the grid, its foci set
 * @param config The grid
 * @param status Counts the deleted cells
 */
static void box_chunk_scan(
    ecs_world_t *world,
    box_chunk_state_t *state,
    const ChunkGrid *config,
    ChunkGridStatus *status)
{
    int32_t count = box_chunk_level_count(config), nf = ecs_vec_count(&state->foci), f, g;
    double h = config->hysteresis > 0 ? (double)config->hysteresis : 0.0;
    double reach = count ? (double)config->levels[count - 1] + h : -1.0;
    const double *foci = ecs_vec_first(&state->foci);
    int64_t (*lo)[3] = ecs_os_calloc(ECS_SIZEOF(int64_t) * 3 * (nf ? nf : 1));
    int64_t (*hi)[3] = ecs_os_calloc(ECS_SIZEOF(int64_t) * 3 * (nf ? nf : 1));
    bool *valid = ecs_os_calloc_n(bool, nf ? nf : 1);
    int64_t x, y, z;
    for (f = 0; f < nf && reach >= 0; f ++) {
        valid[f] = box_chunk_range(config, &foci[f * 3], reach, lo[f], hi[f]);
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
                        box_chunk_visit(world, state, config, c, d, status);
                    }
                }
            }
        }
    }

    ecs_os_free(lo);
    ecs_os_free(hi);
    ecs_os_free(valid);
}

/** Delete the cells that no focus reached in the last scan, past the last radius.
 *
 * @param world The world
 * @param state The state of the grid
 * @param status Counts the deleted cells
 */
static void box_chunk_unload_unseen(
    ecs_world_t *world,
    box_chunk_state_t *state,
    ChunkGridStatus *status)
{
    ecs_vec_t cells;
    int32_t i;
    box_chunk_collect(state, &cells);
    for (i = 0; i < ecs_vec_count(&cells); i ++) {
        box_chunk_cell_t *cell = *ecs_vec_get_t(&cells, box_chunk_cell_t*, i);
        if (cell->seen != state->stamp) {
            if (ecs_is_alive(world, cell->entity)) {
                ecs_delete(world, cell->entity);
                status->deleted ++;
            }

            box_chunk_remove(state, cell);
        }
    }

    ecs_vec_fini_t(NULL, &cells, box_chunk_cell_t*);
}

/** Load and change cells from the queued work, in order, within the budget.
 *
 * @param world The world
 * @param grid The grid entity
 * @param state The state of the grid
 * @param config The grid
 * @param status Counts the created, changed, pending and held cells
 */
static void box_chunk_apply(
    ecs_world_t *world,
    ecs_entity_t grid,
    box_chunk_state_t *state,
    const ChunkGrid *config,
    ChunkGridStatus *status)
{
    int32_t count = ecs_vec_count(&state->work), i, done = 0;
    box_chunk_work_t *work = ecs_vec_first_t(&state->work, box_chunk_work_t);
    double anchor[3] = {0, 0, 0};
    vec3 local = {0, 0, 0};
    ecs_time_t start, t;
    ecs_os_get_time(&start);
    box_world_position(world, grid, local);
    box_world_to_absolute(world, local, anchor);
    if (count > 1) {
        qsort(work, (size_t)count, sizeof(box_chunk_work_t), box_chunk_work_compare);
    }

    for (i = 0; i < count; i ++) {
        t = start;
        bool over = (config->budget > 0 && done >= config->budget) || (done > 0 &&
            config->budget_ms > 0 && ecs_time_measure(&t) * 1000.0 >= config->budget_ms);
        if (over) {
            status->pending += count - i;
            break;
        }

        if (!box_chunk_ready(world, grid, &work[i])) {
            status->pending ++;
            status->held ++;
        } else if (work[i].cell) {
            box_chunk_set_level(world, config, work[i].cell, work[i].level);
            status->changed ++;
            done ++;
        } else {
            ecs_entity_t e = box_chunk_create(world, grid, config, anchor, &work[i]);
            box_chunk_insert(state, work[i].c, work[i].level, e);
            status->created ++;
            done ++;
        }
    }
}

/** Check whether a grid needs a scan: its foci or config changed or work is pending.
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
    bool result = !state->scanned || ecs_vec_count(&state->work) > 0 ||
        ecs_os_memcmp(&state->config, config, ECS_SIZEOF(ChunkGrid)) != 0 ||
        ecs_vec_count(foci) != ecs_vec_count(&state->foci);
    if (!result) {
        result = ecs_os_memcmp(ecs_vec_first(foci), ecs_vec_first(&state->foci),
            ECS_SIZEOF(double) * 3 * ecs_vec_count(foci)) != 0;
    }

    return result;
}

/** Count the loaded cells of a grid per level.
 *
 * @param state The state of the grid
 * @param status Receives the counts
 */
static void box_chunk_count(
    const box_chunk_state_t *state,
    ChunkGridStatus *status)
{
    ecs_vec_t cells;
    int32_t i;
    box_chunk_collect(state, &cells);
    for (i = 0; i < ecs_vec_count(&cells); i ++) {
        box_chunk_cell_t *cell = *ecs_vec_get_t(&cells, box_chunk_cell_t*, i);
        if (cell->level >= 0 && cell->level < BOX_WORLD_CHUNK_LEVELS) {
            status->loaded[cell->level] ++;
        }
    }

    status->cells = state->count;
    ecs_vec_fini_t(NULL, &cells, box_chunk_cell_t*);
}

/** Stream the cells of one grid for this frame.
 *
 * @param world The world
 * @param grid The grid entity
 */
static void box_chunk_update(
    ecs_world_t *world,
    ecs_entity_t grid)
{
    ChunkGridState *s = ecs_get_mut(world, grid, ChunkGridState);
    const ChunkGrid *ptr = ecs_get(world, grid, ChunkGrid);
    ChunkGridStatus status = {0};
    ecs_vec_t foci;
    ecs_time_t start;
    ecs_os_get_time(&start);
    ecs_vec_init(NULL, &foci, ECS_SIZEOF(double) * 3, 0);
    if (!s || !ptr) {
        goto done;
    }

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
        box_chunk_unload_all(world, state, &status);
        ecs_vec_clear(&state->work);
        state->scanned = valid && state->scanned;
    }

    box_chunk_foci(world, grid, &foci);
    if (valid && ecs_vec_count(&foci) && box_chunk_dirty(state, &config, &foci)) {
        state->stamp ++;
        state->config = config;
        state->scanned = true;
        ecs_vec_clear(&state->foci);
        ecs_os_memcpy(ecs_vec_grow(NULL, &state->foci, ECS_SIZEOF(double) * 3,
            ecs_vec_count(&foci)), ecs_vec_first(&foci),
            ECS_SIZEOF(double) * 3 * ecs_vec_count(&foci));
        ecs_vec_clear(&state->work);
        box_chunk_scan(world, state, &config, &status);
        box_chunk_unload_unseen(world, state, &status);
        box_chunk_apply(world, grid, state, &config, &status);
        if (!status.pending) {
            ecs_vec_clear(&state->work);
        }
    }

    box_chunk_count(state, &status);
done:
    status.time_ms = (float)(ecs_time_measure(&start) * 1000.0);
    if (ptr && ecs_is_alive(world, grid)) {
        ecs_set_ptr(world, grid, ChunkGridStatus, &status);
    }

    ecs_vec_fini(NULL, &foci, ECS_SIZEOF(double) * 3);
}

/** Stream the cells of every ChunkGrid around the foci.
 *
 * @param it The iterator of the system
 */
static void BoxWorldChunkStream(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    ecs_vec_t grids;
    int32_t i;
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
            box_chunk_update(world, grid);
        }
    }

    ecs_vec_fini_t(NULL, &grids, ecs_entity_t);
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
    ECS_COMPONENT_DEFINE(world, ChunkGridGate);
    ECS_COMPONENT_DEFINE(world, ChunkGridState);

    ecs_set_hooks(world, ChunkGrid, { .ctor = box_chunk_grid_ctor });
    ecs_set_hooks(world, ChunkGridState, {
        .ctor = flecs_default_ctor,
        .dtor = box_chunk_state_dtor,
        .move = box_chunk_state_move
    });

    ecs_add_pair(world, ecs_id(ChunkGrid), EcsWith, ecs_id(ChunkGridState));
    ecs_add_pair(world, ecs_id(ChunkGrid), EcsWith, ecs_id(ChunkGridStatus));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ChunkGridStream" }),
        .phase = EcsPostUpdate,
        .run = BoxWorldChunkStream,
        .immediate = true
    });
}
