/**
 * @file mining.c
 * @brief Mining: while the Mine action is held, the astronaut swings at the deposit on the
 * hover tile within cfg.reach, and every swing moves 1 of its ore into the inventory. A
 * building on the tile is the target instead: its swing demolishes it.
 *
 * A swing takes the mining time of the ore (OreMined), cfg.dozeTime for a building, and is keyed
 * to its target, the tiles of a building being one: a new target restarts it, releasing the
 * action, leaving reach or losing the target resets it.
 *
 * A tile marker shows the target while playing, the TileMarkerBad one when it is out of reach.
 * Every yield plays MineBurst and shrinks the deposit; PoofBurst plays where a deposit is used
 * up and where a building is demolished. The Luminite glints only while playing.
 */

#include "game.h"

/** Height of the tile marker above the ground */
#define ORE_MARKER_Y (0.14f)

/** Shrink a deposit with its ore: its scale goes from 1 when full down to cfg.depositScaleMin
 * when empty, the original's oreDepositMine. It is only written when it changes.
 *
 * @param world The world or the stage of a system
 * @param deposit The deposit
 * @param d Its OreDeposit
 */
static void ore_deposit_shrink(
    ecs_world_t *world,
    ecs_entity_t deposit,
    const OreDeposit *d)
{
    const Scale *scale = ecs_get(world, deposit, Scale);
    float min = ORE_CFG(ecs_get_world(world), "depositScaleMin");
    float s;
    if (d->capacity > 0) {
        s = min + (1.0f - min) * (float)d->amount / (float)d->capacity;
        if (!scale || scale->x != s || scale->y != s || scale->z != s) {
            ecs_set(world, deposit, Scale, { s, s, s });
        }
    }
}

bool ore_deposit_mine(
    ecs_world_t *world,
    ecs_entity_t deposit)
{
    const ecs_world_t *real = ecs_get_world(world);
    OreDeposit *d = deposit && ecs_is_alive(world, deposit) ?
        ecs_get_mut(world, deposit, OreDeposit) : NULL;
    const OreEffects *fx = ecs_singleton_get(world, OreEffects);
    const Position *p = d ? ecs_get(world, deposit, Position) : NULL;
    bool result = d && d->amount > 0;
    bool empty = d && d->amount <= 0;
    vec3 at = { p ? p->x : 0, 0, p ? p->z : 0 };
    if (!result || !ore_inventory_add(world, d->resource, 1)) {
        goto done;
    }

    d->amount --;
    empty = d->amount <= 0;
    at[1] = ORE_CFG(real, "mineBurstY");
    ore_burst(world, fx ? fx->mine_burst : 0, at, 1.0f);
    if (empty) {
        at[1] = ORE_CFG(real, "depositPoofY");
        ore_burst(world, fx ? fx->poof_burst : 0, at, 1.0f);
        result = false;
    } else {
        ecs_modified(world, deposit, OreDeposit);
        ore_deposit_shrink(world, deposit, d);
    }

done:
    if (empty) {
        ecs_delete(world, deposit);
    }

    return result;
}

void ore_marker_show(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t marker,
    bool show,
    float x,
    float y,
    float z)
{
    const Position *p = NULL;
    bool alive = marker && ecs_is_alive(world, marker);
    if (alive && show) {
        p = ecs_get(world, marker, Position);
        if (!p || p->x != x || p->y != y || p->z != z) {
            ecs_set(stage, marker, Position, { x, y, z });
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

/** Swing at the target on the hover tile while the Mine action is held, the building there or
 * else the deposit, and show the marker on the tile.
 *
 * Within reach a swing takes the mining time of the ore or cfg.dozeTime; when it completes 1 of
 * the ore goes from the deposit into the inventory, or the building is demolished.
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
    const OreEffects *fx = ecs_singleton_get(it->world, OreEffects);
    ore_grid_t grid = ore_grid_get(it->world);
    float reach = ORE_CFG(it->world, "reach");
    float poof_y = ORE_CFG(it->world, "demolishPoofY");
    bool holding = ore_action_down(it->world, a->mine);
    ecs_entity_t *bcell = holding && hover->on_grid ?
        ore_map_building(map, hover->row, hover->col) : NULL;
    ecs_entity_t building = bcell && *bcell && ecs_is_alive(it->world, *bcell) ? *bcell : 0;
    ecs_entity_t *cell = holding && hover->on_grid && !building ?
        ore_map_deposit(map, hover->row, hover->col) : NULL;
    ecs_entity_t deposit = cell && *cell && ecs_is_alive(it->world, *cell) ? *cell : 0;
    ecs_entity_t target = building ? building : deposit;
    const OreDeposit *d = deposit ? ecs_get(it->world, deposit, OreDeposit) : NULL;
    const OreMined *mined = d ? ecs_get(it->world, d->resource, OreMined) : NULL;
    float tx = target ? ore_tile_x(&grid, hover->col) : 0;
    float tz = target ? ore_tile_z(&grid, hover->row) : 0;
    float time = building ? ORE_CFG(it->world, "dozeTime") : (mined ? mined->time : 0);
    bool timed = building || mined;
    bool in_reach = false;
    OreMining next;
    int32_t i;
    time = time > 0 ? time : 1.0f;
    for (i = 0; i < it->count; i ++) {
        next = (OreMining){ 0 };
        in_reach = target &&
            (tx - p[i].x) * (tx - p[i].x) + (tz - p[i].z) * (tz - p[i].z) <= reach * reach;
        if (in_reach && timed) {
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
                if (building) {
                    if (ore_building_demolish(it->stage, building)) {
                        vec3 at = { tx, poof_y, tz };
                        ore_burst(it->stage, fx ? fx->poof_burst : 0, at, 1.0f);
                    }

                    next.target = 0;
                } else if (!ore_deposit_mine(it->stage, target)) {
                    next.target = 0;
                }
            }
        }

        ore_mining_publish(it->stage, it->entities[i], &mining[i], &next);
    }

    ore_marker_show(it->world, it->stage, marker->good, target && in_reach, tx, ORE_MARKER_Y,
        tz);
    ore_marker_show(it->world, it->stage, marker->bad, target && !in_reach, tx, ORE_MARKER_Y,
        tz);
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
    ore_marker_show(it->world, it->stage, marker->good, false, 0, ORE_MARKER_Y, 0);
    ore_marker_show(it->world, it->stage, marker->bad, false, 0, ORE_MARKER_Y, 0);
}

/** Play the glints of the deposits (the layers of a DepositSparkle, the emitters below an
 * OreDeposit) while playing and pause them in every other state, so they stand still with the
 * game and none show on the title screen. Their playback is only written when it changes.
 *
 * @param it Iterator over the emitters below the deposits
 */
static void OreDepositSparkle(
    ecs_iter_t *it)
{
    PlaybackState state = ore_state_is(it->world, OreGameStatePlaying) ?
        ParticlePlaying : ParticlePaused;
    const ParticlePlayback *playback;
    ParticlePlayback next;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        playback = ecs_get(it->world, it->entities[i], ParticlePlayback);
        next = playback ? *playback : (ParticlePlayback){ .state = ParticlePlaying };
        if (next.state != state) {
            next.state = state;
            next.reset = false;
            ecs_set_ptr(it->stage, it->entities[i], ParticlePlayback, &next);
        }
    }
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

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreDepositSparkle" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ParticleEmitter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreDeposit), .src.id = EcsUp, .trav = EcsChildOf,
              .inout = EcsInOutNone }
        },
        .callback = OreDepositSparkle
    });
}
