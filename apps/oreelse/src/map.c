/**
 * @file map.c
 * @brief The map: the tile grid, the conversions between tiles and world positions, the
 * deposits that register in the grid and the hover tile under the cursor.
 *
 * Tile (row, col) is centered at x = x0 + col * tile, z = z0 + row * tile. A deposit registers
 * in its tile when its OreDeposit is set; a later deposit on a taken tile is rejected and
 * deleted by ore_map_purge, so the first one generated keeps it.
 *
 * The buildings (buildings.c) register in their tiles, the connectors on the edges between
 * them, the plants of the flora (flora.c) in theirs. OreHover is the hover tile.
 */

#include "game.h"
#include <math.h>

ore_grid_t ore_grid_get(
    const ecs_world_t *world)
{
    return (ore_grid_t){
        .rows = ORE_CFG_I32(world, "rows"),
        .cols = ORE_CFG_I32(world, "cols"),
        .tile = ORE_CFG(world, "tile"),
        .x0 = ORE_CFG(world, "x0"),
        .z0 = ORE_CFG(world, "z0")
    };
}

float ore_tile_x(
    const ore_grid_t *grid,
    int32_t col)
{
    return grid->x0 + (float)col * grid->tile;
}

float ore_tile_z(
    const ore_grid_t *grid,
    int32_t row)
{
    return grid->z0 + (float)row * grid->tile;
}

int32_t ore_col_at(
    const ore_grid_t *grid,
    float x)
{
    return (int32_t)floorf((x - grid->x0) / grid->tile + 0.5f);
}

int32_t ore_row_at(
    const ore_grid_t *grid,
    float z)
{
    return (int32_t)floorf((z - grid->z0) / grid->tile + 0.5f);
}

bool ore_on_grid(
    const ore_grid_t *grid,
    int32_t row,
    int32_t col)
{
    return row >= 0 && row < grid->rows && col >= 0 && col < grid->cols;
}

/** Get the index of a tile in the cells of the map.
 *
 * @param map The tile grid
 * @param row The row
 * @param col The column
 * @return The index, -1 off the grid or before the cells exist
 */
static int32_t ore_map_index(
    const OreMap *map,
    int32_t row,
    int32_t col)
{
    bool on = map->deposits && row >= 0 && row < map->rows && col >= 0 && col < map->cols;

    return on ? row * map->cols + col : -1;
}

ecs_entity_t* ore_map_deposit(
    OreMap *map,
    int32_t row,
    int32_t col)
{
    int32_t i = ore_map_index(map, row, col);

    return i < 0 ? NULL : &map->deposits[i];
}

ecs_entity_t* ore_map_building(
    OreMap *map,
    int32_t row,
    int32_t col)
{
    int32_t i = ore_map_index(map, row, col);

    return i < 0 ? NULL : &map->buildings[i];
}

ecs_entity_t* ore_map_flora(
    OreMap *map,
    int32_t row,
    int32_t col)
{
    int32_t i = ore_map_index(map, row, col);

    return i < 0 ? NULL : &map->flora[i];
}

ecs_entity_t* ore_map_link(
    OreMap *map,
    int32_t row,
    int32_t col,
    bool vertical)
{
    int32_t i = ore_map_index(map, row, col);

    return i < 0 ? NULL : &map->links[i * 2 + (vertical ? 1 : 0)];
}

void ore_map_fit(
    OreMap *map,
    const ore_grid_t *grid)
{
    int32_t count = grid->rows * grid->cols;
    if (count > 0 && (!map->deposits || map->rows != grid->rows || map->cols != grid->cols)) {
        ecs_os_free(map->deposits);
        ecs_os_free(map->buildings);
        ecs_os_free(map->links);
        ecs_os_free(map->flora);
        map->deposits = ecs_os_calloc_n(ecs_entity_t, count);
        map->buildings = ecs_os_calloc_n(ecs_entity_t, count);
        map->links = ecs_os_calloc_n(ecs_entity_t, count * 2);
        map->flora = ecs_os_calloc_n(ecs_entity_t, count);
        map->rows = grid->rows;
        map->cols = grid->cols;
        ecs_vec_clear(&map->dirty);
        ecs_vec_clear(&map->flora_dirty);
    }
}

void ore_map_touch(
    OreMap *map,
    int32_t row,
    int32_t col)
{
    int32_t i = ore_map_index(map, row, col);
    if (i >= 0) {
        ecs_vec_append_t(NULL, &map->dirty, int32_t)[0] = i;
        ecs_vec_append_t(NULL, &map->flora_dirty, int32_t)[0] = i;
    }
}

/** Free the cells of a map */
static ECS_DTOR(OreMap, ptr, {
    ecs_os_free(ptr->deposits);
    ecs_os_free(ptr->buildings);
    ecs_os_free(ptr->links);
    ecs_os_free(ptr->flora);
    ecs_vec_fini_t(NULL, &ptr->rejected, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ptr->dirty, int32_t);
    ecs_vec_fini_t(NULL, &ptr->flora_new, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ptr->flora_dirty, int32_t);
})

/** Move the cells of a map, leaving the source without them */
static ECS_MOVE(OreMap, dst, src, {
    ecs_os_free(dst->deposits);
    ecs_os_free(dst->buildings);
    ecs_os_free(dst->links);
    ecs_os_free(dst->flora);
    ecs_vec_fini_t(NULL, &dst->rejected, ecs_entity_t);
    ecs_vec_fini_t(NULL, &dst->dirty, int32_t);
    ecs_vec_fini_t(NULL, &dst->flora_new, ecs_entity_t);
    ecs_vec_fini_t(NULL, &dst->flora_dirty, int32_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

void ore_map_purge(
    ecs_world_t *world)
{
    OreMap *map = ecs_singleton_get_mut(world, OreMap);
    int32_t i, count = map ? ecs_vec_count(&map->rejected) : 0;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&map->rejected, ecs_entity_t, i);
        if (ecs_is_alive(world, e)) {
            ecs_delete(world, e);
        }
    }

    if (count) {
        ecs_vec_clear(&map->rejected);
    }

    if (map) {
        ore_flora_sync(world, map);
    }
}

/** Register deposits in the tiles of their positions. A deposit on a tile another deposit
 * already holds is rejected, row and col -1, and deleted by ore_map_purge; a deposit off the
 * grid keeps row and col at -1 too.
 *
 * A plant on the tile of a deposit is checked again by the next ore_flora_sync, which deletes it.
 *
 * @param it Iterator over the deposits whose OreDeposit was set
 */
static void OreDepositRegister(
    ecs_iter_t *it)
{
    OreDeposit *d = ecs_field(it, OreDeposit, 0);
    OreMap *map = ecs_singleton_get_mut(it->world, OreMap);
    ore_grid_t grid = ore_grid_get(it->world);
    int32_t i;
    if (map) {
        ore_map_fit(map, &grid);
    }

    for (i = 0; map && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const Position *p = ecs_get(it->world, e, Position);
        int32_t row = p ? ore_row_at(&grid, p->z) : -1;
        int32_t col = p ? ore_col_at(&grid, p->x) : -1;
        ecs_entity_t *prev = ore_map_deposit(map, d[i].row, d[i].col);
        ecs_entity_t *cell = ore_map_deposit(map, row, col);
        ecs_entity_t *plant;
        if (prev && *prev == e && prev != cell) {
            *prev = 0;
        }

        d[i].row = cell ? row : -1;
        d[i].col = cell ? col : -1;
        if (cell && *cell && *cell != e && ecs_is_alive(it->world, *cell)) {
            d[i].row = -1;
            d[i].col = -1;
            ecs_vec_append_t(NULL, &map->rejected, ecs_entity_t)[0] = e;
        } else if (cell) {
            *cell = e;
            plant = ore_map_flora(map, row, col);
            if (plant && *plant) {
                ecs_vec_append_t(NULL, &map->flora_new, ecs_entity_t)[0] = *plant;
            }
        }
    }
}

/** Clear the tiles of deposits that are removed.
 *
 * @param it Iterator over the deposits that lose OreDeposit
 */
static void OreDepositUnregister(
    ecs_iter_t *it)
{
    const OreDeposit *d = ecs_field(it, OreDeposit, 0);
    OreMap *map = ecs_singleton_get_mut(it->world, OreMap);
    int32_t i;
    for (i = 0; map && i < it->count; i ++) {
        ecs_entity_t *cell = ore_map_deposit(map, d[i].row, d[i].col);
        if (cell && *cell == it->entities[i]) {
            *cell = 0;
        }
    }
}

/** Delete the deposits and plants rejected since the previous frame and sync the flora,
 * before anything draws them. Runs in every state, so a restart's deletions and the scenes'
 * buildings are covered.
 *
 * @param it Iterator with the OreMap singleton
 */
static void OreDepositPurge(
    ecs_iter_t *it)
{
    ore_map_purge(it->stage);
}

bool ore_hover_find(
    const ecs_world_t *world,
    const OreView *view,
    OreHover *hover)
{
    ore_grid_t grid = ore_grid_get(world);
    OreHover next = { .on_grid = false, .row = -1, .col = -1 };
    vec3 origin, dir;
    float t, cf, rf;
    int32_t row, col;
    glm_vec3_copy(hover->point, next.point);
    if (box_view_cursor_ray(world, view->view, origin, dir) && fabsf(dir[1]) > 1e-6f) {
        t = -origin[1] / dir[1];
        if (t >= 0) {
            next.point[0] = origin[0] + dir[0] * t;
            next.point[1] = 0;
            next.point[2] = origin[2] + dir[2] * t;
            col = ore_col_at(&grid, next.point[0]);
            row = ore_row_at(&grid, next.point[2]);
            if (ore_on_grid(&grid, row, col)) {
                cf = (next.point[0] - grid.x0) / grid.tile + 0.5f;
                rf = (next.point[2] - grid.z0) / grid.tile + 0.5f;
                next.on_grid = true;
                next.row = row;
                next.col = col;
                next.fx = cf - (float)col;
                next.fz = rf - (float)row;
            }
        }
    }

    *hover = next;

    return next.on_grid;
}

/** Find the tile under the cursor: where the cursor ray of the world view meets the ground
 * y = 0, whatever stands in between. Without a ray, a ray that misses the ground or a point off
 * the grid there is no tile. Runs in every state.
 *
 * @param it Iterator with the OreView and OreHover singletons
 */
static void OreHoverUpdate(
    ecs_iter_t *it)
{
    const OreView *view = ecs_field(it, OreView, 0);
    OreHover *hover = ecs_field(it, OreHover, 1);
    OreHover next = *hover;
    ore_hover_find(it->world, view, &next);
    if (next.on_grid != hover->on_grid || next.row != hover->row || next.col != hover->col ||
        next.fx != hover->fx || next.fz != hover->fz || next.point[0] != hover->point[0] ||
        next.point[2] != hover->point[2])
    {
        *hover = next;
        ecs_singleton_modified(it->stage, OreHover);
    }
}

void ore_map_import(
    ecs_world_t *world)
{
    ecs_set_hooks(world, OreMap, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(OreMap),
        .move = ecs_move(OreMap),
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL
    });

    ecs_singleton_ensure(world, OreMap);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreDepositRegister" }),
        .query.terms = {{ .id = ecs_id(OreDeposit) }},
        .events = { EcsOnSet },
        .callback = OreDepositRegister
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreDepositUnregister" }),
        .query.terms = {{ .id = ecs_id(OreDeposit), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = OreDepositUnregister
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreDepositPurge" }),
        .phase = EcsOnLoad,
        .query.terms = {
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsInOut }
        },
        .callback = OreDepositPurge
    });

    ecs_singleton_set(world, OreHover, { .on_grid = false, .row = -1, .col = -1 });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreHoverUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreView), .src.id = ecs_id(OreView), .inout = EcsIn },
            { .id = ecs_id(OreHover), .src.id = ecs_id(OreHover), .inout = EcsInOut }
        },
        .callback = OreHoverUpdate
    });
}
