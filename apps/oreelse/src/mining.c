/**
 * @file mining.c
 * @brief Mining: while the Mine action is held, the astronaut swings at the deposit on the
 * hover tile within cfg.reach, and every swing moves 1 of its ore into the inventory.
 *
 * A swing takes the mining time of the ore (OreMined) and is keyed to its target: a new target
 * restarts it, releasing the action, leaving reach or losing the target resets it. A tile
 * marker shows the target while playing, the TileMarkerBad one when it is out of reach.
 */

#include "game.h"

/** Height of the tile marker above the ground */
#define ORE_MARKER_Y (0.14f)

bool ore_deposit_mine(
    ecs_world_t *world,
    ecs_entity_t deposit)
{
    OreDeposit *d = deposit && ecs_is_alive(world, deposit) ?
        ecs_get_mut(world, deposit, OreDeposit) : NULL;
    bool result = d && d->amount > 0;
    if (!result || !ore_inventory_add(world, d->resource, 1)) {
        goto done;
    }

    d->amount --;
    if (d->amount <= 0) {
        result = false;
    } else {
        ecs_modified(world, deposit, OreDeposit);
    }

done:
    if (d && d->amount <= 0) {
        ecs_delete(world, deposit);
    }

    return result;
}

/** Show a marker on a tile or hide it; it is only written when that changes.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param marker The marker entity, 0 for none
 * @param show Whether to show it
 * @param x X of the tile center
 * @param z Z of the tile center
 */
static void ore_marker_show(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t marker,
    bool show,
    float x,
    float z)
{
    const Position *p = NULL;
    bool alive = marker && ecs_is_alive(world, marker);
    if (alive && show) {
        p = ecs_get(world, marker, Position);
        if (!p || p->x != x || p->y != ORE_MARKER_Y || p->z != z) {
            ecs_set(stage, marker, Position, { x, ORE_MARKER_Y, z });
        }
    }

    if (alive && ecs_has_id(world, marker, EcsDisabled) == show) {
        ecs_enable(stage, marker, show);
    }
}

/** Publish the state of the swing; the component is only marked when it changes.
 *
 * @param stage The stage of the system
 * @param e The astronaut
 * @param mining Its mining
 * @param next The new state
 */
static void ore_mining_publish(
    ecs_world_t *stage,
    ecs_entity_t e,
    OreMining *mining,
    const OreMining *next)
{
    if (mining->target != next->target || mining->left != next->left ||
        mining->progress != next->progress)
    {
        *mining = *next;
        ecs_modified(stage, e, OreMining);
    }
}

/** Swing at the deposit on the hover tile while the Mine action is held: within reach a swing
 * takes the mining time of its ore, and when it completes 1 of the ore goes into the inventory
 * and comes off the deposit. The marker shows on the target while the action is held.
 *
 * @param it Iterator over the players, with OreActions, OreHover, OreMap and OreMineMarker,
 * while playing
 */
static void OrePlayerMine(
    ecs_iter_t *it)
{
    OreMining *mining = ecs_field(it, OreMining, 0);
    const Position *p = ecs_field(it, Position, 1);
    const OreActions *a = ecs_field(it, OreActions, 2);
    const OreHover *hover = ecs_field(it, OreHover, 3);
    OreMap *map = ecs_field(it, OreMap, 4);
    const OreMineMarker *marker = ecs_field(it, OreMineMarker, 5);
    ore_grid_t grid = ore_grid_get(it->world);
    float reach = ORE_CFG(it->world, "reach");
    bool holding = ore_action_down(it->world, a->mine);
    ecs_entity_t *cell = holding && hover->on_grid ?
        ore_map_deposit(map, hover->row, hover->col) : NULL;
    ecs_entity_t target = cell && *cell && ecs_is_alive(it->world, *cell) ? *cell : 0;
    const OreDeposit *deposit = target ? ecs_get(it->world, target, OreDeposit) : NULL;
    const OreMined *mined = deposit ? ecs_get(it->world, deposit->resource, OreMined) : NULL;
    float tx = target ? ore_tile_x(&grid, hover->col) : 0;
    float tz = target ? ore_tile_z(&grid, hover->row) : 0;
    float time = mined && mined->time > 0 ? mined->time : 1.0f;
    bool in_reach = false;
    OreMining next;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        next = (OreMining){ 0 };
        in_reach = target &&
            (tx - p[i].x) * (tx - p[i].x) + (tz - p[i].z) * (tz - p[i].z) <= reach * reach;
        if (in_reach && mined) {
            next = mining[i];
            if (next.target != target) {
                next.target = target;
                next.left = time;
            }

            next.left -= it->delta_time;
            next.progress = 1.0f - next.left / time;
            if (next.left <= 0) {
                next.left = time;
                next.progress = 0;
                if (!ore_deposit_mine(it->stage, target)) {
                    next.target = 0;
                }
            }
        }

        ore_mining_publish(it->stage, it->entities[i], &mining[i], &next);
    }

    ore_marker_show(it->world, it->stage, marker->good, target && in_reach, tx, tz);
    ore_marker_show(it->world, it->stage, marker->bad, target && !in_reach, tx, tz);
}

/** Hide the markers outside of play: like the original's markers, which it drew every frame
 * of play, they do not stay on the pause or end screens.
 *
 * @param it Iterator with OreMineMarker, while not playing
 */
static void OreMineMarkerHide(
    ecs_iter_t *it)
{
    const OreMineMarker *marker = ecs_field(it, OreMineMarker, 0);
    ore_marker_show(it->world, it->stage, marker->good, false, 0, 0);
    ore_marker_show(it->world, it->stage, marker->bad, false, 0, 0);
}

void ore_mining_import(
    ecs_world_t *world)
{
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(OreMining));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerMine" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreMining) },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreActions), .src.id = ecs_id(OreActions), .inout = EcsIn },
            { .id = ecs_id(OreHover), .src.id = ecs_id(OreHover), .inout = EcsIn },
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsIn },
            { .id = ecs_id(OreMineMarker), .src.id = ecs_id(OreMineMarker), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePlayerMine
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreMineMarkerHide" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreMineMarker), .src.id = ecs_id(OreMineMarker), .inout = EcsIn },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OreMineMarkerHide
    });
}
