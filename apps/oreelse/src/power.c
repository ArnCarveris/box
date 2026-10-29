/**
 * @file power.c
 * @brief Power: the production of the producers against the demand of the consumers, scaled by
 * the bars of the allocation, the all or nothing blackout, and what follows it: the drills, the
 * factories' crafters and the status lamps.
 *
 * The allocation (OrePowerAlloc) gives Mining, Weapons and Construction a level 0 to
 * ORE_POWER_LEVELS, at most cfg.powerBars in total. The multiplier of a category is its level
 * times cfg.powerStep; a building without a category has multiplier 1.
 *
 * The status lamps are written only when the power state, the allocation, the blink signals, a
 * building or a lamp changed (OreLamps), and only the values that differ.
 */

#include "game.h"

/** Get the level of a category.
 *
 * @param alloc The allocation, NULL for none
 * @param cat The category of a building, NULL for none
 * @return The level, -1 without a category or allocation
 */
static int32_t ore_power_kind_level(
    const OrePowerAlloc *alloc,
    const OrePowerCategory *cat)
{
    int32_t kind = cat ? (int32_t)cat->kind : -1;
    int32_t result = -1;
    if (alloc && kind >= 0 && kind < ORE_POWER_KINDS) {
        result = alloc->level[kind];
    }

    return result;
}

/** Get the multiplier of a category.
 *
 * @param alloc The allocation, NULL for none
 * @param cat The category of a building, NULL for none
 * @param step Multiplier of a level, cfg.powerStep
 * @return The level times step, 1 without a category
 */
static float ore_power_kind_mul(
    const OrePowerAlloc *alloc,
    const OrePowerCategory *cat,
    float step)
{
    int32_t level = ore_power_kind_level(alloc, cat);

    return level < 0 ? 1.0f : (float)level * step;
}

/** Get the category of the entity of a row of a field matched on the entity or its prefab.
 *
 * @param it The iterator
 * @param index Index of the optional OrePowerCategory field
 * @param row Row of the entity
 * @return The category, NULL for none
 */
static const OrePowerCategory* ore_power_field_category(
    ecs_iter_t *it,
    int8_t index,
    int32_t row)
{
    const OrePowerCategory *cat = ecs_field(it, OrePowerCategory, index);

    return cat && ecs_field_is_self(it, index) ? &cat[row] : cat;
}

int32_t ore_power_level(
    const ecs_world_t *world,
    ecs_entity_t building)
{
    const OrePowerCategory *cat = building ? ecs_get(world, building, OrePowerCategory) : NULL;

    return ore_power_kind_level(ecs_singleton_get(world, OrePowerAlloc), cat);
}

float ore_power_mul(
    const ecs_world_t *world,
    ecs_entity_t building)
{
    const OrePowerCategory *cat = building ? ecs_get(world, building, OrePowerCategory) : NULL;

    return ore_power_kind_mul(ecs_singleton_get(world, OrePowerAlloc), cat,
        ORE_CFG(world, "powerStep"));
}

bool ore_power_set(
    ecs_world_t *world,
    int32_t kind,
    int32_t level)
{
    OrePowerAlloc *alloc = ecs_singleton_get_mut(world, OrePowerAlloc);
    int32_t bars = ORE_CFG_I32(world, "powerBars");
    int32_t current, next, spare;
    bool result = false;
    if (!alloc || kind < 0 || kind >= ORE_POWER_KINDS || level < 1 || level > ORE_POWER_LEVELS) {
        goto done;
    }

    current = alloc->level[kind];
    next = current == level ? level - 1 : level;
    if (next > current) {
        spare = bars - (alloc->level[0] + alloc->level[1] + alloc->level[2] - current);
        next = next > spare ? spare : next;
        if (next <= current) {
            ore_toast(world, true, "Only %d power bars - free one somewhere else", bars);
            next = current;
        }
    }

    if (next >= 0 && next != current) {
        alloc->level[kind] = next;
        ecs_singleton_modified(world, OrePowerAlloc);
        result = true;
    }

done:
    return result;
}

/** Sum the production and the demand of the placed buildings and publish the power state; it is
 * only marked when a value changes.
 *
 * @param it Iterator with OrePowerState and OrePowerAlloc, while playing; its context is the
 * query of the buildings with their producer and consumer
 */
static void OrePowerUpdate(
    ecs_iter_t *it)
{
    OrePowerState *power = ecs_field(it, OrePowerState, 0);
    const OrePowerAlloc *alloc = ecs_field(it, OrePowerAlloc, 1);
    int32_t bars = ORE_CFG_I32(it->world, "powerBars");
    int32_t i, assigned = alloc->level[0] + alloc->level[1] + alloc->level[2];
    float production = 0, raw = 0;
    OrePowerState next;
    ecs_iter_t qit = ecs_query_iter(it->world, it->ctx);
    while (ecs_query_next(&qit)) {
        const OrePowerProducer *p = ecs_field(&qit, OrePowerProducer, 1);
        const OrePowerConsumer *c = ecs_field(&qit, OrePowerConsumer, 2);
        bool p_self = p && ecs_field_is_self(&qit, 1);
        bool c_self = c && ecs_field_is_self(&qit, 2);
        for (i = 0; i < qit.count; i ++) {
            production += p ? (p_self ? p[i].watts : p->watts) : 0.0f;
            raw += c ? (c_self ? c[i].watts : c->watts) : 0.0f;
        }
    }

    next.production = production;
    next.demand = bars > 0 ? raw * (float)assigned / (float)bars : raw;
    next.blackout = next.demand > 0 && production < next.demand;
    next.ratio = next.blackout ? production / next.demand : 1.0f;
    if (power->production != next.production || power->demand != next.demand ||
        power->ratio != next.ratio || power->blackout != next.blackout)
    {
        *power = next;
        ecs_singleton_modified(it->stage, OrePowerState);
    }
}

/** Give a placed building that is a worker (the factory) its crafter.
 *
 * @param it Iterator over the buildings whose OreBuilding was set, with OreWorker
 */
static void OreWorkerPlace(
    ecs_iter_t *it)
{
    const OreWorker *worker = ecs_field(it, OreWorker, 1);
    bool self = ecs_field_is_self(it, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (!ecs_owns(it->world, it->entities[i], OreCrafter)) {
            ecs_set(it->world, it->entities[i], OreCrafter, {
                .speed = self ? worker[i].speed : worker->speed
            });
        }
    }
}

/** Set the speed of the crafters of the workers to their speed times the multiplier of their
 * category, and stop them, holding their craft, in a blackout or at level 0.
 *
 * @param it Iterator over the workers' crafters, with OrePowerState and OrePowerAlloc, while
 * playing
 */
static void OrePowerWorkers(
    ecs_iter_t *it)
{
    OreCrafter *crafter = ecs_field(it, OreCrafter, 0);
    const OreWorker *worker = ecs_field(it, OreWorker, 1);
    const OrePowerState *power = ecs_field(it, OrePowerState, 4);
    const OrePowerAlloc *alloc = ecs_field(it, OrePowerAlloc, 5);
    bool worker_self = ecs_field_is_self(it, 1);
    bool dark = power->blackout && ecs_field_is_set(it, 3);
    float step = ORE_CFG(it->world, "powerStep");
    float mul, speed;
    int32_t i;
    bool paused;
    for (i = 0; i < it->count; i ++) {
        mul = ore_power_kind_mul(alloc, ore_power_field_category(it, 2, i), step);
        speed = (worker_self ? worker[i].speed : worker->speed) * mul;
        paused = dark || mul <= 0;
        if (crafter[i].speed != speed || crafter[i].paused != paused) {
            crafter[i].speed = speed;
            crafter[i].paused = paused;
            ecs_modified(it->stage, it->entities[i], OreCrafter);
        }
    }
}

/** Mine with the drills: without a blackout and with Mining above 0 a drill on a live deposit
 * adds dt times the multiplier to its timer, and every mining time of the ore it mines 1 into
 * the inventory. On a tile whose deposit is gone the timer resets.
 *
 * @param it Iterator over the placed drills, with OreMap, OrePowerState and OrePowerAlloc,
 * while playing
 */
static void OreDrillMine(
    ecs_iter_t *it)
{
    OreDrill *drill = ecs_field(it, OreDrill, 0);
    const OreBuilding *b = ecs_field(it, OreBuilding, 1);
    OreMap *map = ecs_field(it, OreMap, 3);
    const OrePowerState *power = ecs_field(it, OrePowerState, 4);
    const OrePowerAlloc *alloc = ecs_field(it, OrePowerAlloc, 5);
    float step = ORE_CFG(it->world, "powerStep");
    float rate = it->delta_time * ORE_CFG(it->world, "drillSpeed");
    const OreDeposit *d;
    const OreMined *mined;
    ecs_entity_t *cell, deposit;
    OreDrill before;
    float mul, interval;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        before = drill[i];
        drill[i].running = false;
        mul = ore_power_kind_mul(alloc, ore_power_field_category(it, 2, i), step);
        cell = !power->blackout && mul > 0 ? ore_map_deposit(map, b[i].row, b[i].col) : NULL;
        deposit = cell && *cell && ecs_is_alive(it->world, *cell) ? *cell : 0;
        d = deposit ? ecs_get(it->world, deposit, OreDeposit) : NULL;
        mined = d ? ecs_get(it->world, d->resource, OreMined) : NULL;
        if (cell && !deposit) {
            drill[i].timer = 0;
        } else if (mined) {
            interval = mined->time > 0 ? mined->time : 1.0f;
            drill[i].running = true;
            drill[i].timer += rate * mul;
            if (drill[i].timer >= interval) {
                drill[i].timer -= interval;
                ore_deposit_mine(it->stage, deposit);
            }
        }

        if (before.timer != drill[i].timer || before.running != drill[i].running) {
            ecs_modified(it->stage, it->entities[i], OreDrill);
        }
    }
}

/** Count a change the status lamps have to show.
 *
 * @param it Iterator over the entities whose component changed
 */
static void OreLampsTouch(
    ecs_iter_t *it)
{
    OreLamps *lamps = ecs_singleton_get_mut(it->world, OreLamps);
    if (lamps) {
        lamps->version ++;
    }
}

/** Find the building a lamp is part of: the nearest ancestor with OreBuilding. The lamps of a
 * ghost or of a model that is not placed have none.
 *
 * @param world The world
 * @param e The bulb of the lamp
 * @return The building, 0 for none
 */
static ecs_entity_t ore_lamp_owner(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    while (e && !result && !ecs_has_id(world, e, OreGhost)) {
        if (ecs_owns(world, e, OreBuilding)) {
            result = e;
        } else {
            e = ecs_get_parent(world, e);
        }
    }

    return result;
}

/** Show the state of a lamp; only the values that differ are written.
 *
 * @param stage The stage of the system
 * @param e The bulb
 * @param rgb Its color
 * @param em Its emission, NULL for none
 * @param pl Its point light, NULL for none
 * @param color The color to show
 * @param lit Whether it glows
 * @param lamp The lamp
 */
static void ore_lamp_show(
    ecs_world_t *stage,
    ecs_entity_t e,
    const Rgb *rgb,
    const Emissive *em,
    const PointLight *pl,
    Rgb color,
    bool lit,
    const OreStatusLamp *lamp)
{
    float strength = lit ? lamp->strength : 0;
    float glow = lit ? lamp->light : 0;
    PointLight next;
    if (rgb->r != color.r || rgb->g != color.g || rgb->b != color.b) {
        ecs_set_ptr(stage, e, Rgb, &color);
    }

    if (!em || em->strength != strength || em->color.r != color.r ||
        em->color.g != color.g || em->color.b != color.b)
    {
        ecs_set(stage, e, Emissive, { .strength = strength, .color = color });
    }

    if (pl && pl->intensity != glow) {
        next = *pl;
        next.intensity = glow;
        ecs_set_ptr(stage, e, PointLight, &next);
    }
}

/** Color the status lamps by the power of their buildings: dead in a blackout (a consumer) or
 * at level 0, blinking with the lamp signal, else by the level of the category.
 *
 * Only runs through the lamps when OreLamps counted a change since the last time.
 *
 * @param it Iterator with OreLamps, OrePowerState, OrePowerAlloc and OreBlink, while playing;
 * its context is the query of the lamps
 */
static void OreStatusLamps(
    ecs_iter_t *it)
{
    OreLamps *lamps = ecs_field(it, OreLamps, 0);
    const OrePowerState *power = ecs_field(it, OrePowerState, 1);
    const OrePowerAlloc *alloc = ecs_field(it, OrePowerAlloc, 2);
    const OreBlink *blink = ecs_field(it, OreBlink, 3);
    int32_t i, level, version = lamps->version;
    ecs_iter_t qit;
    ecs_entity_t owner;
    bool dead;
    if (version == lamps->applied) {
        goto done;
    }

    qit = ecs_query_iter(it->world, it->ctx);
    while (ecs_query_next(&qit)) {
        OreStatusLamp *lamp = ecs_field(&qit, OreStatusLamp, 0);
        const Rgb *rgb = ecs_field(&qit, Rgb, 1);
        const Emissive *em = ecs_field(&qit, Emissive, 2);
        const PointLight *pl = ecs_field(&qit, PointLight, 3);
        for (i = 0; i < qit.count; i ++) {
            if (!lamp[i].owner) {
                owner = ore_lamp_owner(it->world, qit.entities[i]);
                lamp[i].owner = owner;
                lamp[i].consumer = owner && ecs_has(it->world, owner, OrePowerConsumer);
            }

            owner = lamp[i].owner;
            if (!owner || !ecs_is_alive(it->world, owner)) {
                continue;
            }

            level = ore_power_kind_level(alloc, ecs_get(it->world, owner, OrePowerCategory));
            dead = (power->blackout && lamp[i].consumer) || level == 0;
            ore_lamp_show(it->stage, qit.entities[i], &rgb[i], em ? &em[i] : NULL,
                pl ? &pl[i] : NULL,
                dead ? lamp[i].dead : (level == 1 ? lamp[i].warn :
                    (level == 2 ? lamp[i].ok : lamp[i].live)),
                !dead || blink->lamp, &lamp[i]);
        }
    }

    lamps->applied = version;
done:
    return;
}

/** Click a bar of the power panel from a script, see ore_power_set; only while playing.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The category (an OrePowerKind, as an i32) and the bar (an i32)
 * @param result Receives whether the level changed, a bool
 */
static void ore_power_set_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = ore_state_is(ctx->world, OreGameStatePlaying) &&
        ore_power_set(ctx->world, *(int32_t*)argv[0].ptr, *(int32_t*)argv[1].ptr);
}

/** Register the observers that count the changes the status lamps show.
 *
 * @param world The world
 */
static void ore_lamps_observe(
    ecs_world_t *world)
{
    const char *names[] = {
        "OreLampsPower", "OreLampsAlloc", "OreLampsBlink", "OreLampsBuilding", "OreLampsLamp"
    };
    ecs_id_t ids[] = {
        ecs_id(OrePowerState), ecs_id(OrePowerAlloc), ecs_id(OreBlink), ecs_id(OreBuilding),
        ecs_id(OreStatusLamp)
    };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_observer(world, {
            .entity = ecs_entity(world, { .name = names[i] }),
            .query.terms = {{ .id = ids[i], .src.id = EcsSelf, .inout = EcsInOutNone }},
            .events = { EcsOnSet },
            .callback = OreLampsTouch
        });
    }
}

void ore_power_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);
    ecs_query_t *buildings, *lamps;

    ecs_add_pair(world, ecs_id(OreWorker), EcsOnInstantiate, EcsInherit);
    ecs_singleton_ensure(world, OrePowerAlloc);
    ecs_singleton_ensure(world, OrePowerState);
    ecs_singleton_ensure(world, OreLamps);

    buildings = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OrePowerBuildings" }),
        .terms = {
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OrePowerProducer), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OrePowerConsumer), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    lamps = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreStatusLampQuery" }),
        .terms = {
            { .id = ecs_id(OreStatusLamp), .src.id = EcsSelf },
            { .id = ecs_id(Rgb), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Emissive), .src.id = EcsSelf, .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(PointLight), .src.id = EcsSelf, .inout = EcsIn,
              .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreWorkerPlace" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreWorker), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn }
        },
        .events = { EcsOnSet },
        .callback = OreWorkerPlace
    });

    ore_lamps_observe(world);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePowerUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState),
              .inout = EcsInOut },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePowerUpdate,
        .ctx = buildings
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePowerWorkers" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCrafter), .src.id = EcsSelf },
            { .id = ecs_id(OreWorker), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn },
            { .id = ecs_id(OrePowerCategory), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OrePowerConsumer), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsInOutNone, .oper = EcsOptional },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePowerWorkers
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreDrillMine" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreDrill), .src.id = EcsSelf },
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OrePowerCategory), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsIn },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreDrillMine
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreStatusLamps" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreLamps), .src.id = ecs_id(OreLamps), .inout = EcsInOut },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = ecs_id(OreBlink), .src.id = ecs_id(OreBlink), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreStatusLamps,
        .ctx = lamps
    });

    ecs_function(world, {
        .name = "power_set",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "category", .type = ecs_id(ecs_i32_t) },
            { .name = "level", .type = ecs_id(ecs_i32_t) }
        },
        .callback = ore_power_set_function
    });
}
