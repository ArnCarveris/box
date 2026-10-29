/**
 * @file critters.c
 * @brief The critters: the waves that spawn them at the edges of the map, their targets, attacks
 * and moves, the crowd separation and the critter spatial index, their deaths, and the
 * astronaut's health and its regen.
 *
 * A wave spends its budget on the tiers of OreArsenal its number unlocked, in the mix of its
 * style, and spawns its critters one by one on the sides of the map, never twice on one side.
 * There is no pathfinding: walls are targets, not obstacles, a critter chews what stands ahead.
 *
 * The systems run while playing in this order: OreWaves, OreCritterMove, OrePlayerHealth,
 * OreCritterDeath, then OreCrowd, which pushes overlapping critters apart and rebuilds the
 * spatial index (OreCritterIndex) the systems after it search.
 */

#include "game.h"
#include <float.h>
#include <math.h>

/** Tier of the behemoths, the last one: a wave holds at most cfg.waveBehemothCap of them */
#define ORE_TIER_BEHEMOTH (ORE_WAVE_TIERS - 1)

/** The constants of the critters' behavior and what a run of OreCritterMove shares */
typedef struct ore_critter_ctx_t {
    ecs_world_t *world;  /**< The world */
    ecs_world_t *stage;  /**< The stage of the system */
    OreMap *map;         /**< The tile grid */
    ore_grid_t grid;     /**< The grid of cfg */
    ecs_entity_t player; /**< The astronaut, 0 for none */
    bool buildings;      /**< Some building is placed, so the grid is worth searching */
    float dt;            /**< Seconds of the frame */
    float aggro;         /**< cfg.aggroRange */
    float retarget;      /**< cfg.retargetInterval */
    float probe;         /**< cfg.wallProbe */
    float swerve;        /**< cfg.critterJitter */
    float clamp;         /**< cfg.mapClamp */
} ore_critter_ctx_t;

/** The fields of a table of critters in OreCritterMove */
typedef struct ore_critter_fields_t {
    OreCritter *critter;     /**< The critters' state */
    const OreSpeed *speed;   /**< Their speeds */
    const OreDamage *damage; /**< Their damage per second */
    const OreRange *range;   /**< Their attack ranges */
    OreTarget *target;       /**< Their targets */
    OreVelocity *velocity;   /**< Their published velocities */
    Position *position;      /**< Their positions */
    Rotation *rotation;      /**< Their rotations, turned toward where they head */
} ore_critter_fields_t;

/** The tiers as a wave composes them */
typedef struct ore_wave_tiers_t {
    float cost[4];      /**< Budget of a critter of the tier, 0 for no tier */
    int32_t gate[4];    /**< First wave of the tier */
    float weight[4];    /**< Share of the style over the cost */
    int32_t behemoths;  /**< cfg.waveBehemothCap, below 0 for no cap */
} ore_wave_tiers_t;

/** The constants of the spawns of a wave, read once per run of OreWaves */
typedef struct ore_spawn_cfg_t {
    float retarget; /**< cfg.retargetInterval */
    float jitter;   /**< cfg.edgeSpawnJitter */
    float inset;    /**< cfg.edgeInset */
    float lo_x;     /**< X of the center of the west column */
    float hi_x;     /**< X of the center of the east column */
    float lo_z;     /**< Z of the center of the north row */
    float hi_z;     /**< Z of the center of the south row */
} ore_spawn_cfg_t;

/** The constants of the crowd separation, read once per run of OreCrowd */
typedef struct ore_crowd_cfg_t {
    float cell;     /**< cfg.crowdCell */
    float push;     /**< cfg.crowdPush */
    float top;      /**< cfg.crowdMaxSpeed */
    int32_t near;   /**< cfg.crowdNeighbors, at most ORE_CROWD_NEAR */
    int32_t bucket; /**< cfg.crowdBucket */
} ore_crowd_cfg_t;

/** The most overlapping neighbors of a critter in the separation */
typedef struct ore_crowd_near_t {
    int32_t j[ORE_CROWD_NEAR];  /**< Slot of the neighbor */
    float over[ORE_CROWD_NEAR]; /**< Overlap of the pair */
    float nx[ORE_CROWD_NEAR];   /**< Normal from the neighbor to the critter along X */
    float nz[ORE_CROWD_NEAR];   /**< Normal from the neighbor to the critter along Z */
    int32_t got;                /**< Neighbors kept */
    int32_t weak;               /**< The kept neighbor of the smallest overlap */
} ore_crowd_near_t;

/** Free the slots and cells of an index */
static ECS_DTOR(OreCritterIndex, ptr, {
    ecs_os_free(ptr->entities);
    ecs_os_free(ptr->x);
    ecs_os_free(ptr->z);
    ecs_os_free(ptr->r);
    ecs_os_free(ptr->px);
    ecs_os_free(ptr->pz);
    ecs_os_free(ptr->pos);
    ecs_os_free(ptr->cell_of);
    ecs_os_free(ptr->order);
    ecs_os_free(ptr->start);
})

/** Move the slots and cells of an index, leaving the source without them */
static ECS_MOVE(OreCritterIndex, dst, src, {
    ecs_os_free(dst->entities);
    ecs_os_free(dst->x);
    ecs_os_free(dst->z);
    ecs_os_free(dst->r);
    ecs_os_free(dst->px);
    ecs_os_free(dst->pz);
    ecs_os_free(dst->pos);
    ecs_os_free(dst->cell_of);
    ecs_os_free(dst->order);
    ecs_os_free(dst->start);
    *dst = *src;
    ecs_os_zeromem(src);
})

/** Find the astronaut.
 *
 * @param world The world
 * @return The entity with OrePlayer, 0 for none
 */
static ecs_entity_t ore_player_find(
    const ecs_world_t *world)
{
    ecs_iter_t it = ecs_each(world, OrePlayer);
    ecs_entity_t result = 0;
    while (ecs_each_next(&it)) {
        if (!result && it.count) {
            result = it.entities[0];
        }
    }

    return result;
}

/** Get the position of a target that is still there: alive with hit points left, and not the
 * astronaut aboard a rocket.
 *
 * @param world The world
 * @param target The target, 0 for none
 * @return Its position, NULL when it is gone or has no hit points left
 */
static const Position* ore_target_position(
    const ecs_world_t *world,
    ecs_entity_t target)
{
    const OreHealth *health = NULL;
    const Position *result = NULL;
    if (target && ecs_is_alive(world, target) &&
        !(ecs_has(world, target, OrePlayer) && ore_rocket_aboard(world)))
    {
        health = ecs_get(world, target, OreHealth);
        result = health && health->value > 0 ? ecs_get(world, target, Position) : NULL;
    }

    return result;
}

/** Take the building of a tile when its center is closer to a point than the best so far.
 *
 * @param world The world
 * @param map The tile grid
 * @param row Row of the tile
 * @param col Column of the tile
 * @param x X of the point
 * @param z Z of the point
 * @param best_d2 The squared distance to beat, receives the building's
 * @param best Receives the building when it is closer
 */
static void ore_building_try(
    const ecs_world_t *world,
    OreMap *map,
    int32_t row,
    int32_t col,
    float x,
    float z,
    float *best_d2,
    ecs_entity_t *best)
{
    ecs_entity_t *cell = ore_map_building(map, row, col);
    const Position *p = cell && *cell ? ore_target_position(world, *cell) : NULL;
    float dx, dz, d2;
    if (p) {
        dx = p->x - x;
        dz = p->z - z;
        d2 = dx * dx + dz * dz;
        if (d2 < *best_d2) {
            *best_d2 = d2;
            *best = *cell;
        }
    }
}

/** Find the nearest building with hit points left strictly closer to a point than a distance,
 * measured to its center, searching the tile grid outward ring by ring from the point's tile.
 *
 * A ring is its top and bottom rows whole and the two ends of the rows between, clipped to the
 * map. A building first seen in ring k lies at least (k - 1) tiles away, so the search ends there.
 *
 * @param world The world
 * @param map The tile grid
 * @param grid The grid of cfg
 * @param x X of the point
 * @param z Z of the point
 * @param best_d2 The squared distance to beat, receives the building's
 * @return The building, 0 for none
 */
static ecs_entity_t ore_building_nearest(
    const ecs_world_t *world,
    OreMap *map,
    const ore_grid_t *grid,
    float x,
    float z,
    float *best_d2)
{
    int32_t row = ore_row_at(grid, z), col = ore_col_at(grid, x);
    int32_t rings = grid->rows > grid->cols ? grid->rows : grid->cols;
    ecs_entity_t result = 0;
    int32_t k, r, c, step, r0, r1, c0, c1;
    float near;
    for (k = 0; k <= rings; k ++) {
        near = (float)(k - 1) * grid->tile;
        if (k > 1 && near * near >= *best_d2) {
            break;
        }

        r0 = row - k < 0 ? 0 : row - k;
        r1 = row + k >= grid->rows ? grid->rows - 1 : row + k;
        c0 = col - k < 0 ? 0 : col - k;
        c1 = col + k >= grid->cols ? grid->cols - 1 : col + k;
        for (r = r0; r <= r1; r ++) {
            step = r == row - k || r == row + k ? 1 : 2 * k;
            for (c = step == 1 ? c0 : col - k; c <= c1; c += step) {
                if (c >= c0) {
                    ore_building_try(world, map, r, c, x, z, best_d2, &result);
                }
            }
        }
    }

    return result;
}

/** Acquire a target for a critter: the astronaut within cfg.aggroRange, unless a building is
 * strictly closer; without the astronaut in range the nearest building anywhere.
 *
 * @param ctx The context of the system run
 * @param x X of the critter
 * @param z Z of the critter
 * @return The target, 0 when there is no candidate
 */
static ecs_entity_t ore_critter_acquire(
    const ore_critter_ctx_t *ctx,
    float x,
    float z)
{
    const Position *pp = ctx->player ? ore_target_position(ctx->world, ctx->player) : NULL;
    float best_d2 = FLT_MAX, dx, dz, d2;
    ecs_entity_t result = 0, other = 0;
    if (pp) {
        dx = pp->x - x;
        dz = pp->z - z;
        d2 = dx * dx + dz * dz;
        if (d2 < ctx->aggro * ctx->aggro) {
            best_d2 = d2;
            result = ctx->player;
        }
    }

    if (ctx->buildings) {
        other = ore_building_nearest(ctx->world, ctx->map, &ctx->grid, x, z, &best_d2);
    }

    return other ? other : result;
}

/** Hurt the astronaut: it loses hit points, down to 0, and its hurt timer starts over. At 0 it
 * dies, in OrePlayerHealth later in the same frame. Aboard a rocket it is invulnerable.
 *
 * @param ctx The context of the system run
 * @param damage Hit points it loses
 */
static void ore_player_damage(
    const ore_critter_ctx_t *ctx,
    float damage)
{
    OreHealth *health = ecs_get_mut(ctx->world, ctx->player, OreHealth);
    OreHurt *hurt = ecs_get_mut(ctx->world, ctx->player, OreHurt);
    if (health && health->value > 0 && !ore_rocket_aboard(ctx->world)) {
        health->value -= damage;
        if (health->value <= 0) {
            health->value = 0;
        }

        if (hurt) {
            hurt->since = 0;
        }

        ecs_modified(ctx->stage, ctx->player, OreHealth);
    }
}

/** Chew a target: the astronaut or a building loses hit points. A building at 0 is destroyed
 * by OreBuildingDestroy.
 *
 * @param ctx The context of the system run
 * @param target The target
 * @param damage Hit points it loses
 */
static void ore_critter_attack(
    const ore_critter_ctx_t *ctx,
    ecs_entity_t target,
    float damage)
{
    OreHealth *health = NULL;
    if (target == ctx->player) {
        ore_player_damage(ctx, damage);
    } else {
        health = ecs_get_mut(ctx->world, target, OreHealth);
        if (health && health->value > 0) {
            health->value -= damage;
            ecs_modified(ctx->stage, target, OreHealth);
        }
    }
}

/** Walk a critter a frame toward its goal: the building on the tile cfg.wallProbe ahead that is
 * not its target becomes its target, then it moves at its speed.
 *
 * @param ctx The context of the system run
 * @param f The fields of the critter's table
 * @param i Row of the critter in the table
 * @param target The critter's target, receives the building ahead
 * @param dx X of the direction to the goal, unit length
 * @param dz Z of the direction to the goal, unit length
 */
static void ore_critter_walk(
    const ore_critter_ctx_t *ctx,
    const ore_critter_fields_t *f,
    int32_t i,
    ecs_entity_t *target,
    float dx,
    float dz)
{
    Position *p = &f->position[i];
    float speed = f->speed[i].value;
    ecs_entity_t *block = ore_map_building(ctx->map,
        ore_row_at(&ctx->grid, p->z + dz * ctx->probe),
        ore_col_at(&ctx->grid, p->x + dx * ctx->probe));
    if (block && *block && *block != *target) {
        *target = *block;
        f->critter[i].retarget = ctx->retarget;
    }

    f->velocity[i].x = dx * speed;
    f->velocity[i].z = dz * speed;
    p->x = glm_clamp(p->x + dx * speed * ctx->dt, -ctx->clamp, ctx->clamp);
    p->z = glm_clamp(p->z + dz * speed * ctx->dt, -ctx->clamp, ctx->clamp);
}

/** Play a frame of a critter: drop a target that is gone, acquire one when the retarget timer
 * runs out or there is none, attack it in range, otherwise walk toward the goal (the target, or
 * the world origin without one), turned by the critter's heading jitter.
 *
 * @param ctx The context of the system run
 * @param f The fields of the critter's table
 * @param i Row of the critter in the table
 */
static void ore_critter_step(
    const ore_critter_ctx_t *ctx,
    const ore_critter_fields_t *f,
    int32_t i)
{
    OreCritter *c = &f->critter[i];
    const Position *p = &f->position[i];
    ecs_entity_t target = f->target[i].value, next;
    const Position *tp = ore_target_position(ctx->world, target);
    float dx, dz, dist, a, rdx, rdz;
    target = tp ? target : 0;
    c->retarget -= ctx->dt;
    if (!target || c->retarget <= 0) {
        c->retarget = ctx->retarget * (0.75f + ore_random01(ctx->world) * 0.5f);
        next = ore_critter_acquire(ctx, p->x, p->z);
        if (next && next != target) {
            target = next;
            tp = ore_target_position(ctx->world, next);
        }
    }

    dx = (tp ? tp->x : 0) - p->x;
    dz = (tp ? tp->z : 0) - p->z;
    dist = sqrtf(dx * dx + dz * dz);
    c->attacking = target != 0 && dist <= f->range[i].value;
    f->velocity[i].x = 0;
    f->velocity[i].z = 0;
    if (c->attacking) {
        ore_critter_attack(ctx, target, f->damage[i].value * ctx->dt);
    }

    if (dist > 0.001f) {
        a = c->jitter * ctx->swerve;
        rdx = dx / dist * cosf(a) - dz / dist * sinf(a);
        rdz = dx / dist * sinf(a) + dz / dist * cosf(a);
        f->rotation[i].y = atan2f(rdx, rdz);
        if (!c->attacking) {
            ore_critter_walk(ctx, f, i, &target, rdx, rdz);
        }
    }

    f->target[i].value = target;
}

/** Play the critters with hit points left: targets, attacks and moves.
 *
 * @param it Iterator over the critters, with OreMap, while playing
 */
static void OreCritterMove(
    ecs_iter_t *it)
{
    const OreHealth *health = ecs_field(it, OreHealth, 1);
    ore_critter_fields_t f = {
        .critter = ecs_field(it, OreCritter, 0),
        .speed = ecs_field(it, OreSpeed, 2),
        .damage = ecs_field(it, OreDamage, 3),
        .range = ecs_field(it, OreRange, 4),
        .target = ecs_field(it, OreTarget, 5),
        .velocity = ecs_field(it, OreVelocity, 6),
        .position = ecs_field(it, Position, 7),
        .rotation = ecs_field(it, Rotation, 8)
    };
    ore_critter_ctx_t ctx = {
        .world = it->world,
        .stage = it->stage,
        .map = ecs_field(it, OreMap, 9),
        .grid = ore_grid_get(it->world),
        .player = ore_player_find(it->world),
        .buildings = ecs_count_id(it->world, ecs_id(OreBuilding)) > 0,
        .dt = it->delta_time,
        .aggro = ORE_CFG(it->world, "aggroRange"),
        .retarget = ORE_CFG(it->world, "retargetInterval"),
        .probe = ORE_CFG(it->world, "wallProbe"),
        .swerve = ORE_CFG(it->world, "critterJitter"),
        .clamp = ORE_CFG(it->world, "mapClamp")
    };
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (health[i].value > 0) {
            ore_critter_step(&ctx, &f, i);
        } else {
            f.velocity[i].x = 0;
            f.velocity[i].z = 0;
        }
    }
}

/** Create a critter: an instance of its prefab under the root of OreWaveState.
 *
 * @param world The world or a stage
 * @param prefab The critter prefab
 * @param x X of the critter
 * @param z Z of the critter
 * @param retarget cfg.retargetInterval, its retarget timer starts at retarget * U(0, 1)
 * @return The critter, 0 when the prefab is no critter
 */
static ecs_entity_t ore_critter_create(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x,
    float z,
    float retarget)
{
    const OreHealth *base = prefab ? ecs_get(world, prefab, OreHealth) : NULL;
    const OreWaveState *w = ecs_singleton_get(world, OreWaveState);
    ecs_entity_t root = w && w->root && ecs_is_alive(world, w->root) ? w->root : 0;
    ecs_entity_t e = 0;
    float wait, jitter, anim;
    if (base && ecs_has(world, prefab, OreSpeed)) {
        wait = retarget * ore_random01(world);
        jitter = (ore_random01(world) - 0.5f) * 2.0f;
        anim = ore_random01(world) * 2.0f * GLM_PIf;
        e = root ? ecs_new_w_pair(world, EcsChildOf, root) : ecs_new(world);
        ecs_add_pair(world, e, EcsIsA, prefab);
        ecs_set(world, e, Position, { x, 0, z });
        ecs_set(world, e, Rotation, { 0, 0, 0 });
        ecs_set(world, e, OreHealth, { base->value, base->value });
        ecs_set(world, e, OreTarget, { 0 });
        ecs_set(world, e, OreVelocity, { 0, 0 });
        ecs_set(world, e, OreCritter, {
            .retarget = wait, .jitter = jitter, .attacking = false, .anim = anim });
    }

    return e;
}

ecs_entity_t ore_critter_spawn(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x,
    float z)
{
    return ore_critter_create(world, prefab, x, z, ORE_CFG(world, "retargetInterval"));
}

void ore_critter_kill(
    ecs_world_t *world,
    ecs_entity_t critter)
{
    OreWaveState *w = ecs_singleton_get_mut(world, OreWaveState);
    if (w) {
        w->kills ++;
        ecs_singleton_modified(world, OreWaveState);
    }

    ore_critter_remains(world, critter);
    ecs_delete(world, critter);
}

/** Check whether a wave holds as many critters of a tier as it can: the behemoths at the cap.
 *
 * @param tiers The tiers
 * @param tier The tier
 * @param out Critters of every tier in the wave so far
 * @return Whether the tier is full
 */
static bool ore_wave_full(
    const ore_wave_tiers_t *tiers,
    int32_t tier,
    const int32_t *out)
{
    return tier == ORE_TIER_BEHEMOTH && tiers->behemoths >= 0 &&
        out[tier] >= tiers->behemoths;
}

/** Get the cost of the dearest tier a wave can still take, unlocked and not full.
 *
 * @param tiers The tiers
 * @param wave Number of the wave
 * @param out Critters of every tier in the wave so far
 * @return The cost, 0 for none
 */
static float ore_wave_top(
    const ore_wave_tiers_t *tiers,
    int32_t wave,
    const int32_t *out)
{
    float result = 0;
    int32_t t;
    for (t = 0; t < ORE_WAVE_TIERS; t ++) {
        if (wave >= tiers->gate[t] && tiers->cost[t] > 0 && !ore_wave_full(tiers, t, out)) {
            result = glm_max(result, tiers->cost[t]);
        }
    }

    return result;
}

/** Draw the style of a wave and set up its tiers: their costs, gates and weights (the share of
 * the style's mix over the cost), and the wave's budget cfg.waveBudgetBase *
 * cfg.waveBudgetGrowth ^ (wave - 1).
 *
 * @param world The world or a stage
 * @param w The wave state, receives the style and the budget
 * @param arsenal The prefabs of the tiers
 * @param tiers Receives the tiers
 */
static void ore_wave_tiers(
    ecs_world_t *world,
    OreWaveState *w,
    const OreArsenal *arsenal,
    ore_wave_tiers_t *tiers)
{
    float roll = ore_random01(world);
    float swarm = ORE_CFG(world, "swarmChance");
    float elite = ORE_CFG(world, "eliteChance");
    float growth = glm_max(ORE_CFG(world, "waveBudgetGrowth"), 1.0f);
    const char *mix = "cfg.mixedMix";
    const OreWaveTier *tier;
    OreWaveMix share;
    int32_t t;
    w->style = OreWaveStyleMixed;
    if (roll < swarm) {
        w->style = OreWaveStyleSwarm;
        mix = "cfg.swarmMix";
    } else if (roll < swarm + elite) {
        w->style = OreWaveStyleElite;
        mix = "cfg.eliteMix";
    }

    share = ecs_const_var_get_t(world, mix, OreWaveMix);
    tiers->behemoths = ORE_CFG_I32(world, "waveBehemothCap");
    for (t = 0; t < ORE_WAVE_TIERS; t ++) {
        tier = arsenal->critters[t] ? ecs_get(world, arsenal->critters[t], OreWaveTier) : NULL;
        tiers->cost[t] = tier ? tier->cost : 0;
        tiers->gate[t] = tier ? tier->wave : INT32_MAX;
        tiers->weight[t] = tiers->cost[t] > 0 ? share.tier[t] / tiers->cost[t] : 0;
    }

    w->budget = w->wave >= 1 ?
        ORE_CFG(world, "waveBudgetBase") * powf(growth, (float)(w->wave - 1)) : 0;
}

/** Pick the tier of the next critter of a wave: weighted at random among the unlocked tiers
 * that are not full, fit the budget left and pass the fit rule left - cost <= slots; with no
 * weight among them the dearest affordable tier.
 *
 * @param world The world or a stage
 * @param tiers The tiers
 * @param wave Number of the wave
 * @param out Critters of every tier in the wave so far
 * @param left Budget left
 * @param slots The cost of the dearest tier times the units left after this one
 * @return The tier, -1 for none
 */
static int32_t ore_wave_pick(
    ecs_world_t *world,
    const ore_wave_tiers_t *tiers,
    int32_t wave,
    const int32_t *out,
    float left,
    float slots)
{
    float w[ORE_WAVE_TIERS], total = 0, r;
    int32_t t, dear = -1, fit = -1, result = -1;
    for (t = 0; t < ORE_WAVE_TIERS; t ++) {
        w[t] = 0;
        if (wave >= tiers->gate[t] && tiers->cost[t] > 0 && tiers->cost[t] <= left &&
            !ore_wave_full(tiers, t, out))
        {
            dear = dear < 0 || tiers->cost[t] > tiers->cost[dear] ? t : dear;
            if (left - tiers->cost[t] <= slots) {
                fit = t;
                w[t] = tiers->weight[t];
                total += w[t];
            }
        }
    }

    if (total > 0) {
        r = ore_random01(world) * total;
        for (t = 0; t < ORE_WAVE_TIERS && result < 0; t ++) {
            r -= w[t];
            result = w[t] > 0 && r <= 0 ? t : -1;
        }

        result = result < 0 ? fit : result;
    } else {
        result = dear;
    }

    return result;
}

/** Compose a wave: draw its style and spend its budget a critter at a time, up to
 * cfg.waveUnitCap critters, into the counts of OreWaveState.spawn.
 *
 * @param world The world or a stage
 * @param w The wave state of the wave
 * @param arsenal The prefabs of the tiers
 */
static void ore_wave_compose(
    ecs_world_t *world,
    OreWaveState *w,
    const OreArsenal *arsenal)
{
    int32_t cap = ORE_CFG_I32(world, "waveUnitCap");
    int32_t t, units = 0, pick = 0;
    ore_wave_tiers_t tiers;
    float left, top;
    ore_wave_tiers(world, w, arsenal, &tiers);
    for (t = 0; t < ORE_WAVE_TIERS; t ++) {
        w->spawn[t] = 0;
    }

    left = w->budget;
    top = ore_wave_top(&tiers, w->wave, w->spawn);
    while (units < cap && pick >= 0 && top > 0) {
        pick = ore_wave_pick(world, &tiers, w->wave, w->spawn, left,
            top * (float)(cap - units - 1));
        if (pick >= 0) {
            w->spawn[pick] ++;
            units ++;
            left -= tiers.cost[pick];
            top = ore_wave_full(&tiers, pick, w->spawn) ?
                ore_wave_top(&tiers, w->wave, w->spawn) : top;
        }
    }
}

/** Start the next wave: compose it and set the stagger of its spawns, min(cfg.spawnStagger,
 * cfg.spawnWindow / count); the timer to the wave after it starts at cfg.waveGap.
 *
 * @param world The world or a stage
 * @param w The wave state
 * @param arsenal The prefabs of the tiers
 */
static void ore_wave_start(
    ecs_world_t *world,
    OreWaveState *w,
    const OreArsenal *arsenal)
{
    float stagger = ORE_CFG(world, "spawnStagger");
    float window = ORE_CFG(world, "spawnWindow");
    int32_t t;
    w->wave ++;
    ore_wave_compose(world, w, arsenal);
    w->left = 0;
    for (t = 0; t < ORE_WAVE_TIERS; t ++) {
        w->left += w->spawn[t];
    }

    if (w->left > 0 && window > 0) {
        stagger = glm_min(stagger, window / (float)w->left);
    }

    w->stagger = stagger;
    w->spawn_timer = 0;
    w->timer = ORE_CFG(world, "waveGap");
    w->gap = w->timer;
}

/** Take the prefab of the next critter of a wave, weighted by the critters left of every tier.
 *
 * @param world The world or a stage
 * @param w The wave state, one less is left of the tier
 * @param arsenal The prefabs of the tiers
 * @return The prefab, 0 when none is left
 */
static ecs_entity_t ore_wave_next(
    ecs_world_t *world,
    OreWaveState *w,
    const OreArsenal *arsenal)
{
    ecs_entity_t result = 0;
    int32_t t, r, total = 0;
    for (t = 0; t < ORE_WAVE_TIERS; t ++) {
        total += w->spawn[t] > 0 && arsenal->critters[t] ? w->spawn[t] : 0;
    }

    if (total > 0) {
        r = (int32_t)(ore_random01(world) * (float)total);
        r = r >= total ? total - 1 : r;
        for (t = 0; t < ORE_WAVE_TIERS && !result; t ++) {
            if (w->spawn[t] > 0 && arsenal->critters[t]) {
                r -= w->spawn[t];
                if (r < 0) {
                    w->spawn[t] --;
                    result = arsenal->critters[t];
                }
            }
        }
    }

    return result;
}

/** Spawn the next critter of a wave on a side of the map other than the previous critter's:
 * cfg.edgeInset inside the edge tiles, at a uniform place along it, cfg.edgeSpawnJitter off on
 * both axes, kept on the map.
 *
 * @param world The world or a stage
 * @param w The wave state, receives the side
 * @param arsenal The prefabs of the tiers
 * @param cfg The constants of the spawns
 */
static void ore_wave_spawn(
    ecs_world_t *world,
    OreWaveState *w,
    const OreArsenal *arsenal,
    const ore_spawn_cfg_t *cfg)
{
    ecs_entity_t prefab = ore_wave_next(world, w, arsenal);
    float lo_x = cfg->lo_x + cfg->inset, hi_x = cfg->hi_x - cfg->inset;
    float lo_z = cfg->lo_z + cfg->inset, hi_z = cfg->hi_z - cfg->inset;
    float t, x, z;
    int32_t side;
    if (prefab) {
        side = (int32_t)(ore_random01(world) * 3.0f);
        side = side > 2 ? 2 : side;
        side += w->side >= 0 && side >= w->side ? 1 : 0;
        w->side = side;
        t = ore_random01(world);
        x = side < 2 ? lo_x + t * (hi_x - lo_x) : (side == 2 ? lo_x : hi_x);
        z = side < 2 ? (side == 0 ? lo_z : hi_z) : lo_z + t * (hi_z - lo_z);
        x += (ore_random01(world) - 0.5f) * 2.0f * cfg->jitter;
        z += (ore_random01(world) - 0.5f) * 2.0f * cfg->jitter;
        ore_critter_create(world, prefab, glm_clamp(x, cfg->lo_x, cfg->hi_x),
            glm_clamp(z, cfg->lo_z, cfg->hi_z), cfg->retarget);
    }
}

/** Run the waves: while nothing is left to spawn the timer counts down and at 0 the next wave
 * starts; the critters of a wave spawn one every stagger seconds, the first at once.
 *
 * @param it Iterator with OreWaveState and OreArsenal, while playing
 */
static void OreWaves(
    ecs_iter_t *it)
{
    OreWaveState *w = ecs_field(it, OreWaveState, 0);
    const OreArsenal *arsenal = ecs_field(it, OreArsenal, 1);
    ore_grid_t grid = ore_grid_get(it->world);
    ore_spawn_cfg_t cfg = {
        .retarget = ORE_CFG(it->world, "retargetInterval"),
        .jitter = ORE_CFG(it->world, "edgeSpawnJitter"),
        .inset = ORE_CFG(it->world, "edgeInset"),
        .lo_x = ore_tile_x(&grid, 0),
        .hi_x = ore_tile_x(&grid, grid.cols - 1),
        .lo_z = ore_tile_z(&grid, 0),
        .hi_z = ore_tile_z(&grid, grid.rows - 1)
    };
    if (w->left > 0) {
        w->spawn_timer -= it->delta_time;
    } else {
        w->timer -= it->delta_time;
        if (w->timer <= 0) {
            ore_wave_start(it->world, w, arsenal);
        }
    }

    while (w->left > 0 && w->spawn_timer <= 0) {
        w->spawn_timer += w->stagger;
        w->left --;
        ore_wave_spawn(it->stage, w, arsenal, &cfg);
    }

    w->spawn_timer = glm_max(w->spawn_timer, 0.0f);
    w->gap = glm_max(w->gap, w->timer);
    w->frac = w->left > 0 ? 1.0f : (w->gap > 0 ? 1.0f - w->timer / w->gap : 0.0f);
    ecs_singleton_modified(it->stage, OreWaveState);
}

/** Kill the astronaut when it has no hit points left (ore_player_die: the splat, the fall and
 * the lost game), else count the time since it was hurt and after cfg.playerRegenDelay seconds
 * regenerate cfg.playerRegen hit points a second, not while aboard a rocket.
 *
 * @param it Iterator over the astronaut with its health, while playing
 */
static void OrePlayerHealth(
    ecs_iter_t *it)
{
    OreHealth *health = ecs_field(it, OreHealth, 1);
    OreHurt *hurt = ecs_field(it, OreHurt, 2);
    float regen = ORE_CFG(it->world, "playerRegen");
    float delay = ORE_CFG(it->world, "playerRegenDelay");
    bool aboard = ore_rocket_aboard(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (health[i].value <= 0) {
            ore_player_die(it->stage, it->entities[i]);
        } else {
            hurt[i].since += it->delta_time;
            if (!aboard && hurt[i].since >= delay && health[i].value < health[i].max) {
                health[i].value = glm_min(health[i].value + regen * it->delta_time,
                    health[i].max);
                ecs_modified(it->stage, it->entities[i], OreHealth);
            }
        }
    }
}

/** Kill the critters at 0 hit points or below (ore_critter_kill).
 *
 * @param it Iterator over the critters, while playing
 */
static void OreCritterDeath(
    ecs_iter_t *it)
{
    const OreHealth *health = ecs_field(it, OreHealth, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (health[i].value <= 0) {
            ore_critter_kill(it->stage, it->entities[i]);
        }
    }
}

/** Give an index room for a number of critters, keeping the slots it holds.
 *
 * @param index The index
 * @param count Slots it needs
 */
static void ore_index_grow(
    OreCritterIndex *index,
    int32_t count)
{
    int32_t cap = index->cap ? index->cap : 256;
    if (count > index->cap) {
        while (cap < count) {
            cap *= 2;
        }

        index->entities = ecs_os_realloc_n(index->entities, ecs_entity_t, cap);
        index->x = ecs_os_realloc_n(index->x, float, cap);
        index->z = ecs_os_realloc_n(index->z, float, cap);
        index->r = ecs_os_realloc_n(index->r, float, cap);
        index->px = ecs_os_realloc_n(index->px, float, cap);
        index->pz = ecs_os_realloc_n(index->pz, float, cap);
        index->pos = ecs_os_realloc_n(index->pos, Position*, cap);
        index->cell_of = ecs_os_realloc_n(index->cell_of, int32_t, cap);
        index->order = ecs_os_realloc_n(index->order, int32_t, cap);
        index->cap = cap;
    }
}

/** Size the cells of an index to the map: cells of cfg.crowdCell from its west and north edges,
 * and empty it.
 *
 * @param index The index
 * @param grid The grid of cfg
 * @param cell cfg.crowdCell
 */
static void ore_index_fit(
    OreCritterIndex *index,
    const ore_grid_t *grid,
    float cell)
{
    int32_t cells;
    index->cell = cell;
    index->x0 = grid->x0 - grid->tile * 0.5f;
    index->z0 = grid->z0 - grid->tile * 0.5f;
    index->dim_x = (int32_t)((float)grid->cols * grid->tile / cell) + 1;
    index->dim_z = (int32_t)((float)grid->rows * grid->tile / cell) + 1;
    index->count = 0;
    cells = index->dim_x * index->dim_z;
    if (cells + 1 > index->cells) {
        ecs_os_free(index->start);
        index->start = ecs_os_malloc_n(int32_t, cells + 1);
        index->cells = cells + 1;
    }
}

/** Get the cell of a point in an index, clamped to its cells.
 *
 * @param index The index
 * @param x X of the point
 * @param z Z of the point
 * @return The cell, row major
 */
static int32_t ore_index_cell(
    const OreCritterIndex *index,
    float x,
    float z)
{
    int32_t cx = (int32_t)((x - index->x0) / index->cell);
    int32_t cz = (int32_t)((z - index->z0) / index->cell);
    cx = cx < 0 ? 0 : (cx >= index->dim_x ? index->dim_x - 1 : cx);
    cz = cz < 0 ? 0 : (cz >= index->dim_z ? index->dim_z - 1 : cz);

    return cz * index->dim_x + cx;
}

/** Sort the slots of an index into its cells by their positions.
 *
 * @param index The index
 */
static void ore_index_bin(
    OreCritterIndex *index)
{
    int32_t i, c, cells = index->dim_x * index->dim_z;
    ecs_os_memset(index->start, 0, (ecs_size_t)(cells + 1) * ECS_SIZEOF(int32_t));
    for (i = 0; i < index->count; i ++) {
        index->cell_of[i] = ore_index_cell(index, index->x[i], index->z[i]);
        index->start[index->cell_of[i] + 1] ++;
    }

    for (c = 0; c < cells; c ++) {
        index->start[c + 1] += index->start[c];
    }

    for (i = 0; i < index->count; i ++) {
        index->order[index->start[index->cell_of[i]] ++] = i;
    }

    for (c = cells; c > 0; c --) {
        index->start[c] = index->start[c - 1];
    }

    index->start[0] = 0;
}

/** Add the critters of a table with hit points left to an index.
 *
 * @param index The index
 * @param it Iterator of OreCrowd at a table of critters
 */
static void ore_index_gather(
    OreCritterIndex *index,
    ecs_iter_t *it)
{
    const OreHealth *health = ecs_field(it, OreHealth, 1);
    const OreRadius *radius = ecs_field(it, OreRadius, 2);
    Position *p = ecs_field(it, Position, 3);
    int32_t i, k;
    ore_index_grow(index, index->count + it->count);
    for (i = 0; i < it->count; i ++) {
        if (health[i].value > 0) {
            k = index->count ++;
            index->entities[k] = it->entities[i];
            index->x[k] = p[i].x;
            index->z[k] = p[i].z;
            index->r[k] = radius[i].value;
            index->pos[k] = &p[i];
        }
    }
}

/** Keep a neighbor of a critter when it overlaps it and is among the most overlapping: the
 * normal points from the neighbor to the critter, a pseudo-random one when they coincide.
 *
 * @param index The index
 * @param i Slot of the critter
 * @param j Slot of the neighbor
 * @param max Neighbors kept at most
 * @param near The neighbors kept
 */
static void ore_crowd_consider(
    const OreCritterIndex *index,
    int32_t i,
    int32_t j,
    int32_t max,
    ore_crowd_near_t *near)
{
    float dx = index->x[i] - index->x[j], dz = index->z[i] - index->z[j];
    float rr = index->r[i] + index->r[j], d2 = dx * dx + dz * dz, d, over, a;
    int32_t slot, q;
    if (j != i && d2 < rr * rr) {
        d = sqrtf(d2);
        over = rr - d;
        if (near->got < max || over > near->over[near->weak]) {
            if (d > 0.0001f) {
                dx /= d;
                dz /= d;
            } else {
                a = (float)(((int64_t)i * 7919 + (int64_t)j * 104729) % 629) * 0.01f;
                dx = cosf(a);
                dz = sinf(a);
            }

            slot = near->got < max ? near->got ++ : near->weak;
            near->j[slot] = j;
            near->over[slot] = over;
            near->nx[slot] = dx;
            near->nz[slot] = dz;
            near->weak = 0;
            for (q = 1; q < near->got; q ++) {
                near->weak = near->over[q] < near->over[near->weak] ? q : near->weak;
            }
        }
    }
}

/** Push a critter and its most overlapping neighbors apart: the neighbors in the cells within
 * ceil(2 r / cell) of its cell, at most cfg.crowdBucket of every cell, the cfg.crowdNeighbors
 * most overlapping kept, each pair pushed in inverse proportion to the masses r^2.
 *
 * @param index The index
 * @param cfg The constants of the separation
 * @param i Slot of the critter
 */
static void ore_crowd_pair(
    OreCritterIndex *index,
    const ore_crowd_cfg_t *cfg,
    int32_t i)
{
    int32_t cx = index->cell_of[i] % index->dim_x, cz = index->cell_of[i] / index->dim_x;
    int32_t reach = (int32_t)ceilf(2.0f * index->r[i] / cfg->cell);
    int32_t nx, nz, b, e, q, j;
    float mi = index->r[i] * index->r[i], mj, sum, wi, wj;
    ore_crowd_near_t near = { .got = 0, .weak = 0 };
    reach = reach < 1 ? 1 : reach;
    for (nz = cz - reach; nz <= cz + reach; nz ++) {
        for (nx = cx - reach; nx <= cx + reach; nx ++) {
            if (nz >= 0 && nz < index->dim_z && nx >= 0 && nx < index->dim_x) {
                b = index->start[nz * index->dim_x + nx];
                e = index->start[nz * index->dim_x + nx + 1];
                e = e - b > cfg->bucket ? b + cfg->bucket : e;
                for (; b < e; b ++) {
                    ore_crowd_consider(index, i, index->order[b], cfg->near, &near);
                }
            }
        }
    }

    for (q = 0; q < near.got; q ++) {
        j = near.j[q];
        mj = index->r[j] * index->r[j];
        sum = mi + mj;
        wi = sum > 0 ? mj / sum : 0.5f;
        wj = sum > 0 ? mi / sum : 0.5f;
        index->px[i] += near.nx[q] * near.over[q] * wi;
        index->pz[i] += near.nz[q] * near.over[q] * wi;
        index->px[j] -= near.nx[q] * near.over[q] * wj;
        index->pz[j] -= near.nz[q] * near.over[q] * wj;
    }
}

/** Separate the critters of an index: the push of every critter, scaled by cfg.crowdPush * dt
 * and clamped to a length of cfg.crowdMaxSpeed * dt, moves its position, kept on the map.
 *
 * @param index The index, sorted into its cells
 * @param cfg The constants of the separation
 * @param dt Seconds of the frame
 * @param clamp cfg.mapClamp
 */
static void ore_crowd_separate(
    OreCritterIndex *index,
    const ore_crowd_cfg_t *cfg,
    float dt,
    float clamp)
{
    float step = cfg->top * dt, gain = cfg->push * dt, sx, sz, len;
    int32_t i;
    for (i = 0; i < index->count; i ++) {
        index->px[i] = 0;
        index->pz[i] = 0;
    }

    for (i = 0; i < index->count; i ++) {
        ore_crowd_pair(index, cfg, i);
    }

    for (i = 0; i < index->count; i ++) {
        sx = index->px[i] * gain;
        sz = index->pz[i] * gain;
        len = sqrtf(sx * sx + sz * sz);
        if (len > step && len > 0) {
            sx *= step / len;
            sz *= step / len;
        }

        index->pos[i]->x = glm_clamp(index->pos[i]->x + sx, -clamp, clamp);
        index->pos[i]->z = glm_clamp(index->pos[i]->z + sz, -clamp, clamp);
        index->x[i] = index->pos[i]->x;
        index->z[i] = index->pos[i]->z;
    }
}

/** Rebuild the critter spatial index from the critters with hit points left, push the ones that
 * overlap apart and sort the index again at their new positions.
 *
 * @param it Iterator over the critters with their radius, while playing
 */
static void OreCrowd(
    ecs_iter_t *it)
{
    OreCritterIndex *index = ecs_singleton_get_mut(it->world, OreCritterIndex);
    ore_grid_t grid = ore_grid_get(it->world);
    ore_crowd_cfg_t cfg = {
        .cell = ORE_CFG(it->world, "crowdCell"),
        .push = ORE_CFG(it->world, "crowdPush"),
        .top = ORE_CFG(it->world, "crowdMaxSpeed"),
        .near = ORE_CFG_I32(it->world, "crowdNeighbors"),
        .bucket = ORE_CFG_I32(it->world, "crowdBucket")
    };
    bool ok = index && cfg.cell > 0 && grid.rows > 0 && grid.cols > 0;
    cfg.near = cfg.near > ORE_CROWD_NEAR ? ORE_CROWD_NEAR : cfg.near;
    if (ok) {
        ore_index_fit(index, &grid, cfg.cell);
    } else if (index) {
        index->count = 0;
    }

    while (ecs_iter_next(it)) {
        if (ok) {
            ore_index_gather(index, it);
        }
    }

    if (ok) {
        ore_index_bin(index);
        if (index->count > 1 && cfg.push > 0 && cfg.near > 0 && cfg.bucket > 0) {
            ore_crowd_separate(index, &cfg, it->delta_time, ORE_CFG(it->world, "mapClamp"));
            ore_index_bin(index);
        }
    }
}

ecs_entity_t ore_critter_nearest(
    const OreCritterIndex *index,
    float x,
    float z,
    float range)
{
    float best = range * range, dx, dz, d2;
    int32_t reach, cx, cz, nx, nz, c, b, e, j;
    ecs_entity_t result = 0;
    bool on;
    if (index->count > 0 && index->cell > 0 && index->start) {
        reach = (int32_t)ceilf(range / index->cell);
        cx = (int32_t)floorf((x - index->x0) / index->cell);
        cz = (int32_t)floorf((z - index->z0) / index->cell);
        for (nz = cz - reach; nz <= cz + reach; nz ++) {
            for (nx = cx - reach; nx <= cx + reach; nx ++) {
                on = nz >= 0 && nz < index->dim_z && nx >= 0 && nx < index->dim_x;
                c = on ? nz * index->dim_x + nx : 0;
                b = on ? index->start[c] : 0;
                e = on ? index->start[c + 1] : 0;
                for (; b < e; b ++) {
                    j = index->order[b];
                    dx = index->x[j] - x;
                    dz = index->z[j] - z;
                    d2 = dx * dx + dz * dz;
                    if (d2 < best) {
                        best = d2;
                        result = index->entities[j];
                    }
                }
            }
        }
    }

    return result;
}

/** Spawn a critter from a script, oreelse.spawn_critter(prefab, x, z).
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The prefab, an entity, and the x and z, f32
 * @param result Receives the critter, 0 when the prefab is no critter
 */
static void ore_spawn_critter_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(ecs_entity_t*)result->ptr = ore_critter_spawn(ctx->world, *(ecs_entity_t*)argv[0].ptr,
        *(float*)argv[1].ptr, *(float*)argv[2].ptr);
}

void ore_critters_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OreWaveTier), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, ecs_id(OreTarget));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, ecs_id(OreVelocity));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, DynamicTransform);

    ecs_set_hooks(world, OreCritterIndex, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(OreCritterIndex),
        .move = ecs_move(OreCritterIndex),
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL
    });

    ecs_singleton_ensure(world, OreCritterIndex);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWaves" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreWaveState), .src.id = ecs_id(OreWaveState), .inout = EcsInOut },
            { .id = ecs_id(OreArsenal), .src.id = ecs_id(OreArsenal), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreWaves
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCritterMove" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCritter), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreSpeed), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreDamage), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreRange), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreTarget), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreVelocity), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Rotation), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreCritterMove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerHealth" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer), .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreHurt), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OrePlayerHealth
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCritterDeath" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCritter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreCritterDeath
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCrowd" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCritter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreRadius), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .run = OreCrowd
    });

    ecs_function(world, {
        .name = "spawn_critter",
        .parent = module,
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { .name = "prefab", .type = ecs_id(ecs_entity_t) },
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        },
        .callback = ore_spawn_critter_function
    });
}
