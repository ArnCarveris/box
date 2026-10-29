/**
 * @file drones.c
 * @brief The repair drones, the original's drones.c: a placed drone pad parks the drones of its
 * OreDroneBay over its bays, and they fly to the damaged buildings and weld them back to full,
 * paid from the inventory.
 *
 * A drone is a child of its pad, deleted with it, its Position relative to the pad's center. It
 * is ready without a blackout and with 1 of the cost item (Iron), else it flies home and docks;
 * its target is the nearest damaged building of the map no other drone of its pad targets.
 *
 * Docked -> Flying (to cfg.droneCruise above the target) -> Repairing (within cfg.droneArrive)
 * -> the next target, or Returning -> Docked. Repairing heals cfg.droneRepair hit points a
 * second; every cfg.droneHpPerIron of them one cost item leaves the inventory.
 */

#include "game.h"
#include <float.h>
#include <math.h>

/** Slack of the comparison of the iron credit with cfg.droneHpPerIron, so the sum of the heals
 * of a whole number of payments is not a rounding error short of it
 */
#define ORE_DRONE_CREDIT_SLACK (1e-3f)

/** Largest vertical distance squared to its bay at which a returning drone docks */
#define ORE_DRONE_DOCK_DY2 (0.01f)

/** The queries of OreDrones, its context */
typedef struct ore_drone_queries_t {
    ecs_query_t *drones;  /**< Every drone */
    ecs_query_t *repairs; /**< The buildings with their health and position */
} ore_drone_queries_t;

/** The constants of the drones and what a run of OreDrones shares */
typedef struct ore_drone_ctx_t {
    ecs_world_t *world;    /**< The world */
    ecs_world_t *stage;    /**< The stage of the system */
    ecs_query_t *drones;   /**< Every drone, to find the targets of a pad's drones */
    ecs_query_t *repairs;  /**< The buildings with their health and position */
    bool blackout;         /**< The base is in a blackout */
    float dt;              /**< Seconds of the frame */
    float speed;           /**< cfg.droneSpeed */
    float cruise;          /**< cfg.droneCruise */
    float arrive;          /**< cfg.droneArrive */
    float ease;            /**< cfg.droneEase */
    float repair;          /**< cfg.droneRepair */
    float per_iron;        /**< cfg.droneHpPerIron */
    float scan;            /**< cfg.droneScan */
} ore_drone_ctx_t;

/** Get the dock of a bay, relative to the pad's center.
 *
 * @param bay The drones of the pad
 * @param index The bay, 0 to count - 1
 * @param out Receives the position
 */
static void ore_drone_bay_pos(
    const OreDroneBay *bay,
    int32_t index,
    vec3 out)
{
    int32_t count = bay->count < 1 ? 1 : bay->count;
    float a = ((float)index + 0.5f) * 2.0f * GLM_PIf / (float)count;
    out[0] = sinf(a) * bay->radius;
    out[1] = bay->dock;
    out[2] = cosf(a) * bay->radius;
}

/** Check whether a pad has drones already.
 *
 * @param world The world
 * @param pad The pad
 * @return Whether a child of the pad is a drone
 */
static bool ore_drone_pad_has(
    ecs_world_t *world,
    ecs_entity_t pad)
{
    ecs_iter_t it = ecs_children(world, pad);
    bool result = false;
    int32_t i;
    while (!result && ecs_children_next(&it)) {
        for (i = 0; i < it.count && !result; i ++) {
            result = ecs_has(world, it.entities[i], OreDrone);
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Park the drones of a placed drone pad over its bays, docked, their scan timer at
 * cfg.droneScan * U(0, 1). A pad that has drones gets none, and none spawn while the world shuts
 * down (it deletes the pads, and the prefabs under them).
 *
 * @param it Iterator over the buildings whose OreBuilding was set
 */
static void OreDronePadPlace(
    ecs_iter_t *it)
{
    float scan = ORE_CFG(it->world, "droneScan");
    const OreDroneBay *bay;
    int32_t i, b;
    ecs_entity_t pad, e;
    vec3 p;
    for (i = 0; i < it->count && !ecs_is_fini(it->world); i ++) {
        pad = it->entities[i];
        bay = ecs_get(it->world, pad, OreDroneBay);
        if (!bay || !bay->drone || bay->count <= 0 || ore_drone_pad_has(it->world, pad)) {
            continue;
        }

        for (b = 0; b < bay->count; b ++) {
            ore_drone_bay_pos(bay, b, p);
            e = ecs_new_w_pair(it->world, EcsChildOf, pad);
            ecs_add_pair(it->world, e, EcsIsA, bay->drone);
            ecs_set(it->world, e, Position, { p[0], p[1], p[2] });
            ecs_set(it->world, e, OreDrone, {
                .pad = pad,
                .bay = b,
                .state = OreDroneStateDocked,
                .scan = scan * ore_random01(it->world)
            });
        }
    }
}

/** Check whether another drone of a pad targets a building.
 *
 * @param ctx The context of the system run
 * @param pad The pad
 * @param self The drone that asks
 * @param target The building
 * @return Whether a drone of the pad other than self targets it
 */
static bool ore_drone_claimed(
    const ore_drone_ctx_t *ctx,
    ecs_entity_t pad,
    ecs_entity_t self,
    ecs_entity_t target)
{
    ecs_iter_t it = ecs_query_iter(ctx->world, ctx->drones);
    bool result = false;
    int32_t i;
    while (!result && ecs_query_next(&it)) {
        const OreDrone *drone = ecs_field(&it, OreDrone, 0);
        for (i = 0; i < it.count && !result; i ++) {
            result = it.entities[i] != self && drone[i].pad == pad &&
                drone[i].target == target;
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Find the nearest damaged building of the map, 0 < health < max, that no other drone of the
 * pad targets. There is no range limit.
 *
 * @param ctx The context of the system run
 * @param self The drone
 * @param pad The pad of the drone
 * @param x X the distance is measured from
 * @param z Z the distance is measured from
 * @return The building, 0 for none
 */
static ecs_entity_t ore_drone_acquire(
    const ore_drone_ctx_t *ctx,
    ecs_entity_t self,
    ecs_entity_t pad,
    float x,
    float z)
{
    ecs_iter_t it = ecs_query_iter(ctx->world, ctx->repairs);
    ecs_entity_t result = 0;
    float best = FLT_MAX, dx, dz, d2;
    int32_t i;
    while (ecs_query_next(&it)) {
        const OreHealth *health = ecs_field(&it, OreHealth, 1);
        const Position *pos = ecs_field(&it, Position, 2);
        for (i = 0; i < it.count; i ++) {
            if (health[i].value <= 0 || health[i].value >= health[i].max) {
                continue;
            }

            dx = pos[i].x - x;
            dz = pos[i].z - z;
            d2 = dx * dx + dz * dz;
            if (d2 < best && !ore_drone_claimed(ctx, pad, self, it.entities[i])) {
                best = d2;
                result = it.entities[i];
            }
        }
    }

    return result;
}

/** Check whether a target is still a damaged building.
 *
 * @param world The world
 * @param target The target, 0 for none
 * @return Whether it is alive with 0 < health < max and a position
 */
static bool ore_drone_damaged(
    const ecs_world_t *world,
    ecs_entity_t target)
{
    const OreHealth *health = target && ecs_is_alive(world, target) ?
        ecs_get(world, target, OreHealth) : NULL;

    return health && health->value > 0 && health->value < health->max &&
        ecs_get(world, target, Position) != NULL;
}

/** Look for a target and take it.
 *
 * @param ctx The context of the system run
 * @param e The drone
 * @param drone Its state
 * @param pad Position of its pad
 * @param pos Its position, relative to the pad
 * @return Whether it has a target now
 */
static bool ore_drone_take(
    const ore_drone_ctx_t *ctx,
    ecs_entity_t e,
    OreDrone *drone,
    const Position *pad,
    const Position *pos)
{
    bool docked = drone->state == OreDroneStateDocked;
    ecs_entity_t next = ore_drone_acquire(ctx, e, drone->pad,
        pad->x + (docked ? 0 : pos->x), pad->z + (docked ? 0 : pos->z));
    if (next) {
        drone->target = next;
    }

    return next != 0;
}

/** Fly a drone a frame toward a goal: at cfg.droneSpeed, scaled from 20% to 100% within
 * cfg.droneEase of it, never past it.
 *
 * @param ctx The context of the system run
 * @param pos Its position, relative to the pad
 * @param goal The goal, relative to the pad
 */
static void ore_drone_move(
    const ore_drone_ctx_t *ctx,
    Position *pos,
    const vec3 goal)
{
    float dx = goal[0] - pos->x;
    float dy = goal[1] - pos->y;
    float dz = goal[2] - pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float v = ctx->speed, step;
    if (dist > 0.0001f) {
        if (ctx->ease > 0 && dist < ctx->ease) {
            v = ctx->speed * (0.2f + 0.8f * dist / ctx->ease);
        }

        step = v * ctx->dt;
        step = step > dist ? dist : step;
        pos->x += dx / dist * step;
        pos->y += dy / dist * step;
        pos->z += dz / dist * step;
    }
}

/** Weld a target a frame: it heals cfg.droneRepair * dt up to its maximum, and every
 * cfg.droneHpPerIron hit points of the drone's credit one cost item leaves the inventory. The
 * credit counts the hit points the target gained, not the heal, which rounds at its health.
 *
 * @param ctx The context of the system run
 * @param drone The drone
 * @param cost The item the repairs are paid in
 * @param health The health of the target
 */
static void ore_drone_weld(
    const ore_drone_ctx_t *ctx,
    OreDrone *drone,
    ecs_entity_t cost,
    OreHealth *health)
{
    float heal = ctx->repair * ctx->dt;
    float before = health->value;
    float room = health->max - before;
    heal = heal > room ? room : heal;
    health->value = heal < room ? before + heal : health->max;
    ecs_modified(ctx->stage, drone->target, OreHealth);
    drone->credit += health->value - before;
    while (ctx->per_iron > 0 && drone->credit >= ctx->per_iron - ORE_DRONE_CREDIT_SLACK &&
        ore_inventory_remove(ctx->stage, cost, 1))
    {
        drone->credit -= ctx->per_iron;
    }
}

/** Play a drone a frame, the original's OreDrones.
 *
 * @param ctx The context of the system run
 * @param e The drone
 * @param d Its state
 * @param pos Its position, relative to the pad
 */
static void ore_drone_update(
    const ore_drone_ctx_t *ctx,
    ecs_entity_t e,
    OreDrone *d,
    Position *pos)
{
    ecs_entity_t pad = d->pad ? d->pad : ecs_get_parent(ctx->world, e);
    const OreDroneBay *dock = pad && ecs_is_alive(ctx->world, pad) ?
        ecs_get(ctx->world, pad, OreDroneBay) : NULL;
    const Position *at = dock ? ecs_get(ctx->world, pad, Position) : NULL;
    const Position *tpos = NULL;
    OreHealth *health = NULL;
    float dx, dy, dz, arrive2 = ctx->arrive * ctx->arrive;
    bool ready, busy;
    vec3 bay, goal;
    if (!at) {
        goto done;
    }

    d->pad = pad;
    ore_drone_bay_pos(dock, d->bay, bay);
    ready = !ctx->blackout && dock->cost && ore_inventory_count(ctx->world, dock->cost) >= 1;
    busy = d->state == OreDroneStateFlying || d->state == OreDroneStateRepairing;
    if (!ore_drone_damaged(ctx->world, d->target)) {
        d->target = 0;
    }

    if (!ready) {
        d->target = 0;
        if (busy) {
            d->state = OreDroneStateReturning;
        }
    } else if (busy && !d->target) {
        d->state = ore_drone_take(ctx, e, d, at, pos) ?
            OreDroneStateFlying : OreDroneStateReturning;
        d->scan = ctx->scan;
    }

    if (d->state == OreDroneStateDocked || d->state == OreDroneStateReturning) {
        if (d->scan > 0) {
            d->scan -= ctx->dt;
        }

        if (ready && d->scan <= 0) {
            d->scan = ctx->scan;
            if (ore_drone_take(ctx, e, d, at, pos)) {
                d->state = OreDroneStateFlying;
            }
        }
    }

    if (d->target) {
        health = ecs_get_mut(ctx->world, d->target, OreHealth);
        tpos = ecs_get(ctx->world, d->target, Position);
    }

    if (!health || !tpos) {
        d->target = 0;
        if (d->state == OreDroneStateFlying || d->state == OreDroneStateRepairing) {
            d->state = OreDroneStateReturning;
        }
    }

    glm_vec3_copy(bay, goal);
    switch (d->state) {
    case OreDroneStateDocked:
        break;
    case OreDroneStateFlying:
        goal[0] = tpos->x - at->x;
        goal[1] = ctx->cruise;
        goal[2] = tpos->z - at->z;
        dx = goal[0] - pos->x;
        dy = goal[1] - pos->y;
        dz = goal[2] - pos->z;
        if (dx * dx + dy * dy + dz * dz < arrive2) {
            d->state = OreDroneStateRepairing;
        }

        break;
    case OreDroneStateRepairing:
        goal[0] = tpos->x - at->x;
        goal[1] = ctx->cruise;
        goal[2] = tpos->z - at->z;
        ore_drone_weld(ctx, d, dock->cost, health);
        if (health->value >= health->max) {
            d->target = 0;
            ready = !ctx->blackout && ore_inventory_count(ctx->world, dock->cost) >= 1;
            d->state = ready && ore_drone_take(ctx, e, d, at, pos) ?
                OreDroneStateFlying : OreDroneStateReturning;
            d->scan = ctx->scan;
        }

        break;
    case OreDroneStateReturning:
        dx = bay[0] - pos->x;
        dy = bay[1] - pos->y;
        dz = bay[2] - pos->z;
        if (dx * dx + dy * dy + dz * dz < arrive2 && dy * dy < ORE_DRONE_DOCK_DY2) {
            pos->x = bay[0];
            pos->y = bay[1];
            pos->z = bay[2];
            d->state = OreDroneStateDocked;
            d->scan = ctx->scan * ore_random01(ctx->world);
        }

        break;
    }

    if (d->state == OreDroneStateReturning) {
        dx = goal[0] - pos->x;
        dz = goal[2] - pos->z;
        if (dx * dx + dz * dz > arrive2) {
            goal[1] = ctx->cruise;
        }
    }

    ore_drone_move(ctx, pos, goal);
done:
    return;
}

/** Fly the drones, look for targets and weld them, see ore_drone_update.
 *
 * @param it Iterator over the drones, with OrePowerState, while playing; its context is the
 * ore_drone_queries_t
 */
static void OreDrones(
    ecs_iter_t *it)
{
    OreDrone *drone = ecs_field(it, OreDrone, 0);
    Position *pos = ecs_field(it, Position, 1);
    const OrePowerState *power = ecs_field(it, OrePowerState, 2);
    const ore_drone_queries_t *queries = it->ctx;
    ore_drone_ctx_t ctx = {
        .world = it->world,
        .stage = it->stage,
        .drones = queries->drones,
        .repairs = queries->repairs,
        .blackout = power->blackout,
        .dt = it->delta_time,
        .speed = ORE_CFG(it->world, "droneSpeed"),
        .cruise = ORE_CFG(it->world, "droneCruise"),
        .arrive = ORE_CFG(it->world, "droneArrive"),
        .ease = ORE_CFG(it->world, "droneEase"),
        .repair = ORE_CFG(it->world, "droneRepair"),
        .per_iron = ORE_CFG(it->world, "droneHpPerIron"),
        .scan = ORE_CFG(it->world, "droneScan")
    };
    Position before;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        before = pos[i];
        ore_drone_update(&ctx, it->entities[i], &drone[i], &pos[i]);
        if (before.x != pos[i].x || before.y != pos[i].y || before.z != pos[i].z) {
            ecs_modified(it->stage, it->entities[i], Position);
        }
    }
}

/** Free the context of OreDrones; the queries are deleted with the world.
 *
 * @param ctx The ore_drone_queries_t
 */
static void ore_drone_queries_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

void ore_drones_import(
    ecs_world_t *world)
{
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);
    ore_drone_queries_t *queries = ecs_os_calloc_t(ore_drone_queries_t);

    ecs_add_pair(world, ecs_id(OreDroneBay), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreDrone), EcsWith, DynamicTransform);

    queries->drones = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreDroneQuery" }),
        .terms = {{ .id = ecs_id(OreDrone), .src.id = EcsSelf, .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    queries->repairs = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreRepairQuery" }),
        .terms = {
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreDronePadPlace" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = EcsPrefab, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = OreDronePadPlace
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreDrones" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreDrone), .src.id = EcsSelf },
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreDrones,
        .ctx = queries,
        .ctx_free = ore_drone_queries_free
    });
}
