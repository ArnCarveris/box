/**
 * @file map.c
 * @brief The map: the tile grid, the conversions between tiles and world positions, and the
 * deposits that register in the grid.
 *
 * Tile (row, col) is centered at x = x0 + col * tile, z = z0 + row * tile (cfg.x0, cfg.z0,
 * cfg.tile). A deposit registers in its tile when its OreDeposit is set; a later deposit on a
 * taken tile is deleted, so the first one generated keeps it.
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

/** Give the map the cells of a grid, empty, unless it has them already.
 *
 * @param map The tile grid
 * @param grid The grid of cfg
 */
static void ore_map_fit(
    OreMap *map,
    const ore_grid_t *grid)
{
    int32_t count = grid->rows * grid->cols;
    if (count > 0 && (!map->deposits || map->rows != grid->rows || map->cols != grid->cols)) {
        ecs_os_free(map->deposits);
        ecs_os_free(map->buildings);
        map->deposits = ecs_os_calloc_n(ecs_entity_t, count);
        map->buildings = ecs_os_calloc_n(ecs_entity_t, count);
        map->rows = grid->rows;
        map->cols = grid->cols;
    }
}

/** Free the cells of a map */
static ECS_DTOR(OreMap, ptr, {
    ecs_os_free(ptr->deposits);
    ecs_os_free(ptr->buildings);
})

/** Move the cells of a map, leaving the source without them */
static ECS_MOVE(OreMap, dst, src, {
    ecs_os_free(dst->deposits);
    ecs_os_free(dst->buildings);
    *dst = *src;
    src->deposits = NULL;
    src->buildings = NULL;
})

/** Register deposits in the tiles of their positions. A deposit on a tile another deposit
 * already holds is deleted; a deposit off the grid keeps row and col at -1.
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
        ecs_entity_t *cell = ore_map_deposit(map, row, col);
        d[i].row = cell ? row : -1;
        d[i].col = cell ? col : -1;
        d[i].capacity = d[i].capacity < d[i].amount ? d[i].amount : d[i].capacity;
        if (cell && *cell && *cell != e && ecs_is_alive(it->world, *cell)) {
            ecs_delete(it->world, e);
        } else if (cell) {
            *cell = e;
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
}
