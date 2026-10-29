/**
 * @file rocket.c
 * @brief The rocket on the pad: its parts, the Luminite it loads, the launch and the flight, the
 * shipped Luminite (the score) and the crewed launch that wins, with oreelse.rocket_install(),
 * rocket_uninstall(), rocket_load(), rocket_launch() and craft_key(), the craft keys.
 *
 * OreRocketState holds every value the UI shows, validity included; launch.flecs keeps the
 * Rocket model in step with it, nothing here writes the model's props.
 */

#include "game.h"

/** The constants of the rocket, read from cfg once per run of a system or a function */
typedef struct ore_rocket_cfg_t {
    int32_t per_unit; /**< Engines per unit, cfg.enginesPerUnit */
    int32_t per_bay;  /**< Luminite per cargo bay, cfg.cargoCapacity */
    int32_t max_bays; /**< Cargo bays a rocket takes, cfg.maxCargoBays */
    int32_t max_life; /**< Life supports a rocket takes, cfg.maxLifeSupport */
} ore_rocket_cfg_t;

/** Most stacks an uninstall returns: the part, engines, fuel storages and Luminite */
#define ORE_ROCKET_RETURNS (4)

/** Read the constants of the rocket.
 *
 * @param world The world
 * @return The constants
 */
static ore_rocket_cfg_t ore_rocket_cfg(
    const ecs_world_t *world)
{
    return (ore_rocket_cfg_t){
        .per_unit = ORE_CFG_I32(world, "enginesPerUnit"),
        .per_bay = ORE_CFG_I32(world, "cargoCapacity"),
        .max_bays = ORE_CFG_I32(world, "maxCargoBays"),
        .max_life = ORE_CFG_I32(world, "maxLifeSupport")
    };
}

/** Get the items of a rocket.
 *
 * @param world The world or a stage
 * @return The kit, all items 0 before rocket.flecs set it
 */
static OreRocketKit ore_rocket_kit(
    const ecs_world_t *world)
{
    const OreRocketKit *kit = ecs_singleton_get(world, OreRocketKit);

    return kit ? *kit : (OreRocketKit){ 0 };
}

bool ore_rocket_aboard(
    const ecs_world_t *world)
{
    const OreRocketState *rocket = ecs_singleton_get(world, OreRocketState);

    return rocket && rocket->aboard;
}

/** Check whether the actions on the rocket apply: playing, with a pad and the rocket idle.
 *
 * @param world The world or a stage
 * @param rocket The rocket
 * @return Whether parts can be installed, Luminite loaded and the rocket launched
 */
static bool ore_rocket_ready(
    const ecs_world_t *world,
    const OreRocketState *rocket)
{
    return rocket->pad && rocket->phase == OreLaunchPhaseIdle &&
        ore_state_is(world, OreGameStatePlaying);
}

/** Get the Luminite loading would move into the rocket: what the inventory holds, at most the
 * room left in the cargo bays.
 *
 * @param world The world or a stage
 * @param rocket The rocket
 * @param kit The items of a rocket
 * @param cfg The constants of the rocket
 * @return The Luminite, 0 or more
 */
static int32_t ore_rocket_pending(
    const ecs_world_t *world,
    const OreRocketState *rocket,
    const OreRocketKit *kit,
    const ore_rocket_cfg_t *cfg)
{
    int32_t room = rocket->cargo * cfg->per_bay - rocket->loaded;
    int32_t have = kit->luminite ? ore_inventory_count(world, kit->luminite) : 0;
    room = room > 0 ? room : 0;

    return have < room ? have : room;
}

/** Check whether a part can be installed on the rocket, the rocket ready: one is in the
 * inventory and the rocket has room for it. An engine needs fewer than cfg.enginesPerUnit per
 * unit, a fuel storage fewer than one per unit.
 *
 * @param world The world or a stage
 * @param rocket The rocket
 * @param kit The items of a rocket
 * @param cfg The constants of the rocket
 * @param item The part
 * @param warn Receives the warning of an engine or fuel storage without room, NULL for none;
 * may be NULL
 * @return Whether the part can be installed
 */
static bool ore_rocket_installable(
    const ecs_world_t *world,
    const OreRocketState *rocket,
    const OreRocketKit *kit,
    const ore_rocket_cfg_t *cfg,
    ecs_entity_t item,
    const char **warn)
{
    int32_t units = rocket->cargo + rocket->life_support;
    bool result = false;
    if (warn) {
        *warn = NULL;
    }

    if (!item || ore_inventory_count(world, item) < 1) {
        goto done;
    }

    if (item == kit->engine) {
        result = rocket->engines < units * cfg->per_unit;
        if (!result && warn) {
            *warn = "Engines full for this payload - add a cargo bay (U) first";
        }
    } else if (item == kit->fuel) {
        result = rocket->fuel < units;
        if (!result && warn) {
            *warn = "Fuel tanks full for this payload - add a cargo bay (U) first";
        }
    } else if (item == kit->cargo) {
        result = rocket->cargo < cfg->max_bays;
    } else if (item == kit->life_support) {
        result = rocket->life_support < cfg->max_life;
    }

done:
    return result;
}

/** Compute the values of the rocket the UI and the model show from its parts, its load and
 * the inventory.
 *
 * @param world The world or a stage
 * @param rocket The rocket
 */
static void ore_rocket_derive(
    const ecs_world_t *world,
    OreRocketState *rocket)
{
    ore_rocket_cfg_t cfg = ore_rocket_cfg(world);
    OreRocketKit kit = ore_rocket_kit(world);
    bool ready = ore_rocket_ready(world, rocket);
    int32_t parts = rocket->engines + rocket->fuel + rocket->cargo + rocket->life_support;
    rocket->units = rocket->cargo + rocket->life_support;
    rocket->capacity = rocket->cargo * cfg.per_bay;
    rocket->valid = rocket->units > 0 && rocket->engines == rocket->units * cfg.per_unit &&
        rocket->fuel == rocket->units;
    rocket->install = (OreRocketParts){
        .engine = ready && ore_rocket_installable(world, rocket, &kit, &cfg, kit.engine, NULL),
        .fuel = ready && ore_rocket_installable(world, rocket, &kit, &cfg, kit.fuel, NULL),
        .cargo = ready && ore_rocket_installable(world, rocket, &kit, &cfg, kit.cargo, NULL),
        .life_support = ready &&
            ore_rocket_installable(world, rocket, &kit, &cfg, kit.life_support, NULL)
    };
    rocket->uninstall = (OreRocketParts){
        .engine = ready && rocket->engines > 0,
        .fuel = ready && rocket->fuel > 0,
        .cargo = ready && rocket->cargo > 0,
        .life_support = ready && rocket->life_support > 0
    };
    rocket->load = ready && ore_rocket_pending(world, rocket, &kit, &cfg) > 0;
    rocket->launch = ready && rocket->valid;
    rocket->shown = rocket->phase != OreLaunchPhaseIdle || (rocket->pad && parts > 0);
}

/** Derive the values of the rocket and mark the singleton when any value changed.
 *
 * @param world The world, or the stage of a system
 * @param rocket The rocket
 * @param before The rocket before the change
 */
static void ore_rocket_commit(
    ecs_world_t *world,
    OreRocketState *rocket,
    const OreRocketState *before)
{
    ore_rocket_derive(world, rocket);
    if (ecs_os_memcmp_t(before, rocket, OreRocketState)) {
        ecs_singleton_modified(world, OreRocketState);
    }
}

/** Empty the rocket: every part and the loaded Luminite are gone and it stands idle. The pad,
 * the shipped Luminite and whether the astronaut is aboard stay.
 *
 * @param rocket The rocket
 */
static void ore_rocket_scrap(
    OreRocketState *rocket)
{
    rocket->engines = 0;
    rocket->fuel = 0;
    rocket->cargo = 0;
    rocket->life_support = 0;
    rocket->loaded = 0;
    rocket->phase = OreLaunchPhaseIdle;
    rocket->timer = 0;
    rocket->speed = 0;
    rocket->altitude = 0;
}

/** Move the Luminite loading takes from the inventory into the rocket.
 *
 * @param world The world
 * @param rocket The rocket
 * @param kit The items of a rocket
 * @param cfg The constants of the rocket
 * @return The Luminite moved
 */
static int32_t ore_rocket_fill(
    ecs_world_t *world,
    OreRocketState *rocket,
    const OreRocketKit *kit,
    const ore_rocket_cfg_t *cfg)
{
    int32_t amount = ore_rocket_pending(world, rocket, kit, cfg);
    if (amount > 0 && ore_inventory_remove(world, kit->luminite, amount)) {
        rocket->loaded += amount;
    } else {
        amount = 0;
    }

    return amount;
}

/** Get the item a key applies to: an action with OreItemKey, or the item itself.
 *
 * @param world The world
 * @param key The action or the item
 * @return The item, 0 for none
 */
static ecs_entity_t ore_key_item(
    const ecs_world_t *world,
    ecs_entity_t key)
{
    const OreItemKey *k = key && ecs_is_alive(world, key) ? ecs_get(world, key, OreItemKey) : NULL;

    return k ? k->item : (key && ecs_is_alive(world, key) ? key : 0);
}

/** Install a part from the inventory on the rocket, the rocket ready. An engine or a fuel
 * storage without room for it shows a warning.
 *
 * @param world The world
 * @param item The part
 * @return Whether the part was installed
 */
static bool ore_rocket_install(
    ecs_world_t *world,
    ecs_entity_t item)
{
    OreRocketState *rocket = ecs_singleton_get_mut(world, OreRocketState);
    ore_rocket_cfg_t cfg = ore_rocket_cfg(world);
    OreRocketKit kit = ore_rocket_kit(world);
    OreRocketState before;
    const char *warn = NULL;
    bool result = false;
    if (!rocket || !ore_rocket_ready(world, rocket)) {
        goto done;
    }

    before = *rocket;
    result = ore_rocket_installable(world, rocket, &kit, &cfg, item, &warn) &&
        ore_inventory_remove(world, item, 1);
    if (warn) {
        ore_toast(world, true, "%s", warn);
    }

    if (result) {
        rocket->engines += item == kit.engine;
        rocket->fuel += item == kit.fuel;
        rocket->cargo += item == kit.cargo;
        rocket->life_support += item == kit.life_support;
        ore_rocket_commit(world, rocket, &before);
    }

done:
    return result;
}

/** Append the amount of a part an uninstall returned to its toast.
 *
 * @param buf The text of the toast
 * @param given Parts appended before
 * @param amount The amount
 * @param one The name of one
 * @param many The name of more
 * @return Parts appended, given plus one for an amount above 0
 */
static int32_t ore_rocket_returned(
    ecs_strbuf_t *buf,
    int32_t given,
    int32_t amount,
    const char *one,
    const char *many)
{
    if (amount > 0) {
        ecs_strbuf_append(buf, "%s%d %s", given ? ", " : "", amount, amount == 1 ? one : many);
        given ++;
    }

    return given;
}

/** Uninstall a part of the rocket into the inventory, the rocket ready. The engines and fuel
 * storages above the limits of the units left and the Luminite above the capacity left come
 * back with it, with an info toast; refused with a warning when they do not fit.
 *
 * @param world The world
 * @param item The part
 * @return Whether the part was uninstalled
 */
static bool ore_rocket_uninstall(
    ecs_world_t *world,
    ecs_entity_t item)
{
    OreRocketState *rocket = ecs_singleton_get_mut(world, OreRocketState);
    ore_rocket_cfg_t cfg = ore_rocket_cfg(world);
    OreRocketKit kit = ore_rocket_kit(world);
    OreStack stacks[ORE_ROCKET_RETURNS];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    OreRocketState before, next;
    int32_t i, count = 0, units, spare_engines, spare_fuel, spare_luminite, given = 0;
    bool result = false;
    char *text;
    if (!rocket || !item || !ore_rocket_ready(world, rocket)) {
        goto done;
    }

    before = *rocket;
    next = *rocket;
    if (item == kit.engine && next.engines > 0) {
        next.engines --;
    } else if (item == kit.fuel && next.fuel > 0) {
        next.fuel --;
    } else if (item == kit.cargo && next.cargo > 0) {
        next.cargo --;
    } else if (item == kit.life_support && next.life_support > 0) {
        next.life_support --;
    } else {
        goto done;
    }

    units = next.cargo + next.life_support;
    spare_engines = next.engines > units * cfg.per_unit ? next.engines - units * cfg.per_unit : 0;
    spare_fuel = next.fuel > units ? next.fuel - units : 0;
    spare_luminite = next.loaded > next.cargo * cfg.per_bay ?
        next.loaded - next.cargo * cfg.per_bay : 0;
    next.engines -= spare_engines;
    next.fuel -= spare_fuel;
    next.loaded -= spare_luminite;

    stacks[count ++] = (OreStack){ .item = item, .amount = 1 };
    if (spare_engines) {
        stacks[count ++] = (OreStack){ .item = kit.engine, .amount = spare_engines };
    }

    if (spare_fuel) {
        stacks[count ++] = (OreStack){ .item = kit.fuel, .amount = spare_fuel };
    }

    if (spare_luminite) {
        stacks[count ++] = (OreStack){ .item = kit.luminite, .amount = spare_luminite };
    }

    if (!ore_inventory_fits(world, stacks, count)) {
        ore_toast(world, true, "No room in the inventory for the parts");
        goto done;
    }

    for (i = 0; i < count; i ++) {
        ore_inventory_add(world, stacks[i].item, stacks[i].amount);
    }

    *rocket = next;
    if (spare_engines || spare_fuel || spare_luminite) {
        ecs_strbuf_appendstr(&buf, "Payload removed - returned ");
        given = ore_rocket_returned(&buf, given, spare_engines, "engine", "engines");
        given = ore_rocket_returned(&buf, given, spare_fuel, "fuel tank", "fuel tanks");
        ore_rocket_returned(&buf, given, spare_luminite, "Luminite", "Luminite");
        text = ecs_strbuf_get(&buf);
        ore_toast(world, false, "%s", text);
        ecs_os_free(text);
    }

    ore_rocket_commit(world, rocket, &before);
    result = true;
done:
    return result;
}

/** Load the Luminite of the inventory into the rocket, the rocket ready: as much as the cargo
 * bays have room for.
 *
 * @param world The world
 * @return Whether Luminite was loaded
 */
static bool ore_rocket_load(
    ecs_world_t *world)
{
    OreRocketState *rocket = ecs_singleton_get_mut(world, OreRocketState);
    ore_rocket_cfg_t cfg = ore_rocket_cfg(world);
    OreRocketKit kit = ore_rocket_kit(world);
    OreRocketState before;
    bool result = false;
    if (rocket && ore_rocket_ready(world, rocket)) {
        before = *rocket;
        result = ore_rocket_fill(world, rocket, &kit, &cfg) > 0;
        ore_rocket_commit(world, rocket, &before);
    }

    return result;
}

/** Board the astronaut: it stands on the pad's axis, hidden, until the rocket arrives.
 *
 * @param world The world
 * @param rocket The rocket, its x and z the axis of the pad
 */
static void ore_rocket_board(
    ecs_world_t *world,
    OreRocketState *rocket)
{
    ecs_iter_t it = ecs_each(world, OrePlayer);
    int32_t i;
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_set(world, it.entities[i], Position, { rocket->x, 0, rocket->z });
            ecs_set(world, it.entities[i], Scale, { 0.001f, 0.001f, 0.001f });
        }
    }

    rocket->aboard = true;
}

/** Launch the rocket, ready and valid: it loads what it has room for and its engines ignite.
 * With life support the astronaut boards it.
 *
 * @param world The world
 * @return Whether the rocket launched
 */
static bool ore_rocket_launch(
    ecs_world_t *world)
{
    OreRocketState *rocket = ecs_singleton_get_mut(world, OreRocketState);
    ore_rocket_cfg_t cfg = ore_rocket_cfg(world);
    OreRocketKit kit = ore_rocket_kit(world);
    OreRocketState before;
    bool result = false;
    if (!rocket || !ore_rocket_ready(world, rocket) || !rocket->valid) {
        goto done;
    }

    before = *rocket;
    ore_rocket_fill(world, rocket, &kit, &cfg);
    rocket->phase = OreLaunchPhaseIgnition;
    rocket->timer = ORE_CFG(world, "ignitionTime");
    rocket->speed = 0;
    rocket->altitude = 0;
    if (rocket->life_support > 0) {
        ore_rocket_board(world, rocket);
    }

    ore_rocket_commit(world, rocket, &before);
    result = true;
done:
    return result;
}

/** The rocket reached orbit: its Luminite is shipped and the pad is empty. A crewed rocket
 * wins the game, a cargo rocket shows the shipment.
 *
 * @param stage The stage of the system
 * @param rocket The rocket
 */
static void ore_rocket_arrive(
    ecs_world_t *stage,
    OreRocketState *rocket)
{
    bool crewed = rocket->life_support > 0;
    int32_t shipped = rocket->loaded;
    rocket->shipped += shipped;
    ore_rocket_scrap(rocket);
    if (crewed) {
        ore_state_set(stage, OreGameStateWon);
    } else {
        ore_toast(stage, false, "Shipment away!  +%d Luminite", shipped);
    }
}

/** Find the rocket pad: a placed building with OreLaunchPad.
 *
 * @param world The world
 * @param query The query of the pads
 * @param at Receives the position of the bottom of a rocket on it
 * @return The pad, 0 for none
 */
static ecs_entity_t ore_rocket_pad(
    const ecs_world_t *world,
    ecs_query_t *query,
    vec3 at)
{
    ecs_iter_t it = ecs_query_iter(world, query);
    ecs_entity_t result = 0;
    while (ecs_query_next(&it)) {
        const OreLaunchPad *pad = ecs_field(&it, OreLaunchPad, 0);
        const Position *p = ecs_field(&it, Position, 2);
        if (!result && it.count) {
            result = it.entities[0];
            at[0] = p[0].x;
            at[1] = p[0].y + pad->base_y;
            at[2] = p[0].z;
        }
    }

    return result;
}

/** Follow the pad and fly the rocket. Without a pad an idle rocket loses its parts and its
 * Luminite; a rocket in flight flies on.
 *
 * @param it Iterator with OreRocketState, its context the query of the pads, while playing
 */
static void OreRocketUpdate(
    ecs_iter_t *it)
{
    OreRocketState *rocket = ecs_field(it, OreRocketState, 0);
    OreRocketState before = *rocket;
    vec3 at = { 0, 0, 0 };
    ecs_entity_t pad = ore_rocket_pad(it->world, it->ctx, at);
    float step;
    if (pad != rocket->pad) {
        rocket->pad = pad;
        if (!pad && rocket->phase == OreLaunchPhaseIdle) {
            ore_rocket_scrap(rocket);
        }
    }

    if (pad && rocket->phase == OreLaunchPhaseIdle) {
        rocket->x = at[0];
        rocket->y = at[1];
        rocket->z = at[2];
    }

    if (rocket->phase == OreLaunchPhaseIgnition) {
        rocket->timer -= it->delta_time;
        if (rocket->timer <= 0) {
            rocket->timer = 0;
            rocket->speed = 0;
            rocket->phase = OreLaunchPhaseAscent;
        }
    } else if (rocket->phase == OreLaunchPhaseAscent) {
        rocket->speed += ORE_CFG(it->world, "launchAccel") * it->delta_time;
        step = rocket->speed * it->delta_time;
        rocket->y += step;
        rocket->altitude += step;
        if (rocket->altitude >= ORE_CFG(it->world, "launchAltitude")) {
            ore_rocket_arrive(it->stage, rocket);
        }
    }

    ore_rocket_commit(it->stage, rocket, &before);
}

/** Outside of play nothing can be done to the rocket: clear what the UI offers.
 *
 * @param it Iterator with OreRocketState, while not playing
 */
static void OreRocketIdle(
    ecs_iter_t *it)
{
    OreRocketState *rocket = ecs_field(it, OreRocketState, 0);
    OreRocketState before = *rocket;
    ore_rocket_commit(it->stage, rocket, &before);
}

/** Install a part on the rocket with a key, see ore_rocket_install.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The key: an action with OreItemKey, or the part itself
 * @param future Resolves with whether the part was installed
 */
static void ore_rocket_install_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    ore_resolve(future, ore_rocket_install(ctx->world,
        ore_key_item(ctx->world, *(ecs_entity_t*)argv[0].ptr)));
}

/** Uninstall a part of the rocket, see ore_rocket_uninstall.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The part, or an action with OreItemKey
 * @param future Resolves with whether the part was uninstalled
 */
static void ore_rocket_uninstall_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    ore_resolve(future, ore_rocket_uninstall(ctx->world,
        ore_key_item(ctx->world, *(ecs_entity_t*)argv[0].ptr)));
}

/** Load Luminite into the rocket, see ore_rocket_load.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether Luminite was loaded
 */
static void ore_rocket_load_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ore_resolve(future, ore_rocket_load(ctx->world));
}

/** Launch the rocket, see ore_rocket_launch.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether the rocket launched
 */
static void ore_rocket_launch_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ore_resolve(future, ore_rocket_launch(ctx->world));
}

/** Order an item with a key while playing: one order of one unit, of cfg.craftBatch units with
 * the run key held (see ore_craft_order).
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The key: an action with OreItemKey, or the item itself
 * @param future Resolves with whether units were ordered
 */
static void ore_craft_key_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t item = ore_key_item(world, *(ecs_entity_t*)argv[0].ptr);
    const OreActions *actions = ecs_singleton_get(world, OreActions);
    int32_t count = actions && ore_action_down(world, actions->run) ?
        ORE_CFG_I32(world, "craftBatch") : 1;
    (void)argc;

    ore_resolve(future, item && ore_state_is(world, OreGameStatePlaying) &&
        ore_craft_order(world, item, count) > 0);
}

void ore_rocket_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);
    struct {
        const char *name;
        ecs_entity_t param;
        ecs_async_function_callback_t callback;
    } functions[] = {
        { "rocket_install", ecs_id(ecs_entity_t), ore_rocket_install_function },
        { "rocket_uninstall", ecs_id(ecs_entity_t), ore_rocket_uninstall_function },
        { "rocket_load", 0, ore_rocket_load_function },
        { "rocket_launch", 0, ore_rocket_launch_function },
        { "craft_key", ecs_id(ecs_entity_t), ore_craft_key_function }
    };
    ecs_query_t *pads;
    int32_t i;

    ecs_add_pair(world, ecs_id(OreLaunchPad), EcsOnInstantiate, EcsInherit);
    ecs_singleton_ensure(world, OreRocketState);

    pads = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreRocketPads" }),
        .terms = {
            { .id = ecs_id(OreLaunchPad), .inout = EcsIn },
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreRocketUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreRocketState), .src.id = ecs_id(OreRocketState),
              .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreRocketUpdate,
        .ctx = pads
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreRocketIdle" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreRocketState), .src.id = ecs_id(OreRocketState),
              .inout = EcsInOut },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OreRocketIdle
    });

    for (i = 0; i < (int32_t)(sizeof(functions) / sizeof(functions[0])); i ++) {
        ecs_async_function(world, {
            .name = functions[i].name,
            .parent = module,
            .return_type = ecs_id(ecs_bool_t),
            .params = {{ functions[i].param ? "key" : NULL, functions[i].param }},
            .callback = functions[i].callback
        });
    }
}
