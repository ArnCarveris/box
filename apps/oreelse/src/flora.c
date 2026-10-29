/**
 * @file flora.c
 * @brief The flora in the tile grid: plants register in their tiles, plants on a deposit or on
 * a tile another plant took are deleted, and plants under a building sink out of sight.
 *
 * A plant is an entity of scenery.flecs with a Frondstalk, Ashcup or Quillbulb and OreFlora. It
 * registers in the tile of its position when its OreFlora is set, the first plant on a tile
 * keeping it. It is decoration: nothing collides with it.
 *
 * ore_flora_sync, run by ore_map_purge every frame and by restart() after its reload, settles
 * the plants with the rest of the run once every script created its entities: a plant on a
 * deposit's tile is deleted, and a plant whose tile has a building sinks cfg.floraSink below the
 * ground, coming back to it when the building is gone. The bookkeeping is the plant's OreFlora
 * and the cells of OreMap, which clear themselves when their entities are deleted, so a restart
 * starts from the plants and deposits of the new map.
 */

#include "game.h"

/** Sink a plant under a building on its tile out of sight, or raise it back to the ground.
 *
 * @param world The world or a stage
 * @param map The tile grid
 * @param plant The plant
 * @param row Row of its tile
 * @param col Column of its tile
 * @param sink Depth a plant under a building sinks to, cfg.floraSink
 */
static void ore_flora_place(
    ecs_world_t *world,
    OreMap *map,
    ecs_entity_t plant,
    int32_t row,
    int32_t col,
    float sink)
{
    ecs_entity_t *building = ore_map_building(map, row, col);
    bool under = building && *building && ecs_is_alive(world, *building);
    const Position *p = ecs_get(world, plant, Position);
    float y = under ? -sink : 0;
    if (p && p->y != y) {
        ecs_set(world, plant, Position, { p->x, y, p->z });
    }
}

void ore_flora_sync(
    ecs_world_t *world,
    OreMap *map)
{
    const ecs_world_t *real = ecs_get_world(world);
    float sink = ORE_CFG(real, "floraSink");
    int32_t i, fresh = ecs_vec_count(&map->flora_new);
    int32_t dirty = ecs_vec_count(&map->flora_dirty);
    for (i = 0; i < fresh; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&map->flora_new, ecs_entity_t, i);
        const OreFlora *f = ecs_is_alive(world, e) ? ecs_get(world, e, OreFlora) : NULL;
        ecs_entity_t *cell = f ? ore_map_flora(map, f->row, f->col) : NULL;
        ecs_entity_t *deposit = f ? ore_map_deposit(map, f->row, f->col) : NULL;
        if (!cell || *cell != e) {
            continue;
        }

        if (deposit && *deposit && ecs_is_alive(world, *deposit)) {
            *cell = 0;
            ecs_delete(world, e);
        } else {
            ore_flora_place(world, map, e, f->row, f->col, sink);
        }
    }

    for (i = 0; map->cols > 0 && i < dirty; i ++) {
        int32_t index = *ecs_vec_get_t(&map->flora_dirty, int32_t, i);
        int32_t row = index / map->cols;
        int32_t col = index % map->cols;
        ecs_entity_t *cell = ore_map_flora(map, row, col);
        if (cell && *cell && ecs_is_alive(world, *cell)) {
            ore_flora_place(world, map, *cell, row, col, sink);
        }
    }

    if (fresh) {
        ecs_vec_clear(&map->flora_new);
    }

    if (dirty) {
        ecs_vec_clear(&map->flora_dirty);
    }
}

/** Register plants in the tiles of their positions. A plant on a tile another plant already
 * holds is rejected, row and col -1, and deleted by ore_map_purge; a plant off the grid keeps
 * row and col at -1 and stays. A registered plant waits for the next ore_flora_sync, which
 * checks its tile for a deposit and a building.
 *
 * @param it Iterator over the plants whose OreFlora was set
 */
static void OreFloraRegister(
    ecs_iter_t *it)
{
    OreFlora *f = ecs_field(it, OreFlora, 0);
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
        ecs_entity_t *prev = ore_map_flora(map, f[i].row, f[i].col);
        ecs_entity_t *cell = ore_map_flora(map, row, col);
        if (prev && *prev == e && prev != cell) {
            *prev = 0;
        }

        f[i].row = cell ? row : -1;
        f[i].col = cell ? col : -1;
        if (cell && *cell && *cell != e && ecs_is_alive(it->world, *cell)) {
            f[i].row = -1;
            f[i].col = -1;
            ecs_vec_append_t(NULL, &map->rejected, ecs_entity_t)[0] = e;
        } else if (cell) {
            *cell = e;
            ecs_vec_append_t(NULL, &map->flora_new, ecs_entity_t)[0] = e;
        }
    }
}

/** Clear the tiles of plants that are removed.
 *
 * @param it Iterator over the plants that lose OreFlora
 */
static void OreFloraUnregister(
    ecs_iter_t *it)
{
    const OreFlora *f = ecs_field(it, OreFlora, 0);
    OreMap *map = ecs_singleton_get_mut(it->world, OreMap);
    int32_t i;
    for (i = 0; map && i < it->count; i ++) {
        ecs_entity_t *cell = ore_map_flora(map, f[i].row, f[i].col);
        if (cell && *cell == it->entities[i]) {
            *cell = 0;
        }
    }
}

void ore_flora_import(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreFloraRegister" }),
        .query.terms = {{ .id = ecs_id(OreFlora), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = OreFloraRegister
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreFloraUnregister" }),
        .query.terms = {{ .id = ecs_id(OreFlora), .src.id = EcsSelf, .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = OreFloraUnregister
    });
}
