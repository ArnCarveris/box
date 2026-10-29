/**
 * @file weapons.c
 * @brief The weapons: the astronaut's automatic pistol, the gun, laser and missile turrets, the
 * tracers and missiles they fire and the laser beams. Shots hit critters only, in 2D.
 *
 * Damage only lowers the critters' hit points; OreCritterDeath kills the ones at 0 in the next
 * frame. The systems run while playing after OreCrowd, which rebuilt the spatial index.
 *
 * Their order: OrePistol, OreTurrets, OreProjectileFly; OreBeamHide hides the beams otherwise.
 * The effects of combat play where a shot is fired, a beam burns or a projectile ends.
 */

#include "game.h"
#include <math.h>

/** Most shots a weapon fires in one frame */
#define ORE_SHOTS_MAX (8)

/** The constants of the weapons and what a run of a weapons system shares */
typedef struct ore_weapons_ctx_t {
    ecs_world_t *world;           /**< The world */
    ecs_world_t *stage;           /**< The stage of the system */
    const OreCritterIndex *index; /**< The critter spatial index */
    ecs_entity_t root;            /**< Parent of the projectiles, 0 for none */
    float dt;                     /**< Seconds of the frame */
    float aim_y;                  /**< cfg.turretAimY */
    float aim_per_radius;         /**< cfg.turretAimPerRadius */
    float lead_cap;               /**< cfg.leadCap */
    float turn_rate;              /**< cfg.missileTurnRate */
    float reacquire;              /**< cfg.missileReacquire */
} ore_weapons_ctx_t;

/** The search for the nearest live critter */
typedef struct ore_nearest_t {
    const ecs_world_t *world; /**< The world */
    ecs_entity_t best;        /**< The nearest so far, 0 for none */
    float best_d2;            /**< Its squared distance, the range squared to start */
} ore_nearest_t;

/** The splash of an explosion */
typedef struct ore_splash_t {
    ecs_world_t *world; /**< The world */
    ecs_world_t *stage; /**< The stage of the system */
    float damage;       /**< Hit points every critter within the radius loses */
} ore_splash_t;

/** The effects of combat and their constants, read once per system run (ore_combat_fx_get) */
typedef struct ore_combat_fx_t {
    const OreEffects *effects; /**< The one shot effects the C plays, NULL for none */
    float hit_inset;           /**< cfg.turretHitInset */
    float blast_per;           /**< cfg.splashBlastPer */
    float blast_min;           /**< cfg.splashBlastMin */
    float blast_max;           /**< cfg.splashBlastMax */
    float blast_lift;          /**< cfg.splashBlastLift */
    float impact_y;            /**< cfg.impactY */
} ore_combat_fx_t;

/** Read the constants of the weapons.
 *
 * @param it The iterator of the system
 * @param index The critter spatial index
 * @param armory The projectiles of the run, NULL for none
 * @return The context
 */
static ore_weapons_ctx_t ore_weapons_ctx(
    ecs_iter_t *it,
    const OreCritterIndex *index,
    const OreArmory *armory)
{
    ore_weapons_ctx_t result = {
        .world = it->world,
        .stage = it->stage,
        .index = index,
        .root = armory && armory->root && ecs_is_alive(it->world, armory->root) ?
            armory->root : 0,
        .dt = it->delta_time,
        .aim_y = ORE_CFG(it->world, "turretAimY"),
        .aim_per_radius = ORE_CFG(it->world, "turretAimPerRadius"),
        .lead_cap = ORE_CFG(it->world, "leadCap"),
        .turn_rate = ORE_CFG(it->world, "missileTurnRate"),
        .reacquire = ORE_CFG(it->world, "missileReacquire")
    };

    return result;
}

/** Get the position of a live critter: alive, a critter and hit points left.
 *
 * @param world The world
 * @param critter The critter, 0 for none
 * @return Its position, NULL when it is gone, no critter or has no hit points left
 */
static const Position* ore_live_critter(
    const ecs_world_t *world,
    ecs_entity_t critter)
{
    const OreHealth *health = NULL;
    const Position *result = NULL;
    if (critter && ecs_is_alive(world, critter) && ecs_has(world, critter, OreCritter)) {
        health = ecs_get(world, critter, OreHealth);
        result = health && health->value > 0 ? ecs_get(world, critter, Position) : NULL;
    }

    return result;
}

/** Take a critter of the index when it is closer than the nearest so far and has hit points
 * left.
 *
 * @param ctx The search, an ore_nearest_t
 * @param critter The critter
 * @param d2 Its squared distance
 */
static void ore_nearest_visit(
    void *ctx,
    ecs_entity_t critter,
    float d2)
{
    ore_nearest_t *n = ctx;
    const OreHealth *health;
    if (d2 < n->best_d2) {
        health = ecs_get(n->world, critter, OreHealth);
        if (health && health->value > 0) {
            n->best_d2 = d2;
            n->best = critter;
        }
    }
}

/** Find the nearest live critter strictly within a range of a point on the ground.
 *
 * @param ctx The context of the system run
 * @param x X of the point
 * @param z Z of the point
 * @param range Largest distance on XZ
 * @return The critter, 0 for none
 */
static ecs_entity_t ore_nearest_live(
    const ore_weapons_ctx_t *ctx,
    float x,
    float z,
    float range)
{
    ore_nearest_t n = { .world = ctx->world, .best = 0, .best_d2 = range * range };
    if (ctx->index && range > 0) {
        ore_critter_each(ctx->index, x, z, range, ore_nearest_visit, &n);
    }

    return n.best;
}

/** Hurt a critter: it loses hit points, if it has any left. It dies in OreCritterDeath.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param critter The critter
 * @param damage Hit points it loses
 */
static void ore_critter_hurt(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t critter,
    float damage)
{
    OreHealth *health = critter && ecs_is_alive(world, critter) ?
        ecs_get_mut(world, critter, OreHealth) : NULL;
    if (health && health->value > 0 && damage > 0) {
        health->value -= damage;
        ecs_modified(stage, critter, OreHealth);
    }
}

/** Hurt a critter of the index caught in a splash, when it has hit points left.
 *
 * @param ctx The splash, an ore_splash_t
 * @param critter The critter
 * @param d2 Its squared distance to the explosion
 */
static void ore_splash_visit(
    void *ctx,
    ecs_entity_t critter,
    float d2)
{
    ore_splash_t *s = ctx;
    (void)d2;
    ore_critter_hurt(s->world, s->stage, critter, s->damage);
}

/** Get the rotation that turns +Z along a direction, in box's Euler order (Ry * Rx * Rz).
 *
 * @param dx X of the direction
 * @param dy Y of the direction
 * @param dz Z of the direction
 * @return The pitch and yaw, no roll
 */
static Rotation ore_heading(
    float dx,
    float dy,
    float dz)
{
    Rotation result = { atan2f(-dy, sqrtf(dx * dx + dz * dz)), atan2f(dx, dz), 0 };

    return result;
}

/** Fire a projectile: an instance of its prefab under the projectiles' parent, from a point
 * toward another at a speed, oriented along its velocity, with its damage, its splash and, for a
 * homing projectile, its target.
 *
 * @param ctx The context of the system run
 * @param prefab The projectile prefab, with OreProjectile
 * @param from The muzzle
 * @param to The point aimed at
 * @param speed Speed in units per second
 * @param damage Hit points of a hit
 * @param splash Splash radius, 0 for none
 * @param target The target a homing projectile flies to
 */
static void ore_projectile_fire(
    const ore_weapons_ctx_t *ctx,
    ecs_entity_t prefab,
    const vec3 from,
    const vec3 to,
    float speed,
    float damage,
    float splash,
    ecs_entity_t target)
{
    const OreProjectile *base = prefab ? ecs_get(ctx->world, prefab, OreProjectile) : NULL;
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    ecs_entity_t e;
    Rotation rot;
    if (base) {
        len = len < 0.001f ? 1.0f : len;
        rot = ore_heading(dx, dy, dz);
        e = ctx->root ? ecs_new_w_pair(ctx->stage, EcsChildOf, ctx->root) : ecs_new(ctx->stage);
        ecs_add_pair(ctx->stage, e, EcsIsA, prefab);
        ecs_add_id(ctx->stage, e, DynamicTransform);
        ecs_set(ctx->stage, e, Position, { from[0], from[1], from[2] });
        ecs_set_ptr(ctx->stage, e, Rotation, &rot);
        ecs_set(ctx->stage, e, OreProjectile, {
            .vx = dx / len * speed, .vy = dy / len * speed, .vz = dz / len * speed,
            .life = base->life, .radius = base->radius
        });
        ecs_set(ctx->stage, e, OreDamage, { damage });
        if (splash > 0) {
            ecs_set(ctx->stage, e, OreSplash, { splash });
        }

        if (ecs_has_id(ctx->world, prefab, OreHoming)) {
            ecs_set(ctx->stage, e, OreTarget, { target });
        }
    }
}

/** Fire the astronaut's pistol: every interval at the nearest live critter within its range,
 * aimed cfg.turretAimY above it without lead, from its muzzle height above the astronaut (the
 * hover included), in a blackout too, not while it is aboard the rocket.
 *
 * Its timer counts down and is held at 0 while there is no target, so it fires at once.
 *
 * @param it Iterator over the astronaut with its weapon, while playing
 */
static void OrePistol(
    ecs_iter_t *it)
{
    const OreHealth *health = ecs_field(it, OreHealth, 1);
    const OreDamage *damage = ecs_field(it, OreDamage, 2);
    const OreRange *range = ecs_field(it, OreRange, 3);
    const OreMuzzle *muzzle = ecs_field(it, OreMuzzle, 4);
    const OreAttackInterval *interval = ecs_field(it, OreAttackInterval, 5);
    OreAttackTimer *timer = ecs_field(it, OreAttackTimer, 6);
    const OreAmmo *ammo = ecs_field(it, OreAmmo, 7);
    const Position *p = ecs_field(it, Position, 8);
    ore_weapons_ctx_t ctx = ore_weapons_ctx(it, ecs_field(it, OreCritterIndex, 9),
        ecs_field(it, OreArmory, 10));
    const Position *tp;
    ecs_entity_t target;
    int32_t i, shots;
    bool aboard = ore_rocket_aboard(it->world);
    vec3 from, to;
    for (i = 0; !aboard && i < it->count; i ++) {
        if (health[i].value <= 0) {
            continue;
        }

        timer[i].value -= ctx.dt;
        for (shots = 0; timer[i].value <= 0 && shots < ORE_SHOTS_MAX; shots ++) {
            target = ore_nearest_live(&ctx, p[i].x, p[i].z, range[i].value);
            tp = target ? ecs_get(it->world, target, Position) : NULL;
            if (!tp) {
                timer[i].value = 0;
                break;
            }

            from[0] = p[i].x;
            from[1] = p[i].y + muzzle[i].height;
            from[2] = p[i].z;
            to[0] = tp->x;
            to[1] = tp->y + ctx.aim_y;
            to[2] = tp->z;
            ore_projectile_fire(&ctx, ammo[i].projectile, from, to, muzzle[i].speed,
                damage[i].value, 0, target);
            timer[i].value += interval[i].value;
        }
    }
}

/** Find a part of a building, the original's oreChild: the child of that name, or, as the parts
 * of a placed building are unnamed, the child whose IsA base has that name.
 *
 * @param world The world
 * @param parent The building or a part of it
 * @param name The name of the part in the prefab
 * @return The part, 0 for none
 */
static ecs_entity_t ore_child(
    const ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t result = parent ? ecs_lookup_child(world, parent, name) : 0;
    ecs_entity_t base;
    const char *base_name;
    ecs_iter_t it;
    int32_t i;
    if (!parent || result) {
        goto done;
    }

    it = ecs_children(world, parent);
    while (!result && ecs_children_next(&it)) {
        for (i = 0; !result && i < it.count; i ++) {
            base = ecs_get_target(world, it.entities[i], EcsIsA, 0);
            base_name = base ? ecs_get_name(world, base) : NULL;
            result = base_name && !ecs_os_strcmp(base_name, name) ? it.entities[i] : 0;
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

done:
    return result;
}

/** Find the parts of a turret, its head, the head's fx and its hit_fx, and give a laser turret
 * its beam: an instance of the beam prefab under the turret, hidden, with SpatialIgnore. The
 * head gets DynamicTransform, as it turns, and so does hit_fx, which moves to where it hits.
 *
 * @param ctx The context of the system run
 * @param turret The turret's state, receives the head and the beam
 * @param e The turret
 * @param beam The beam of the turret, NULL for none
 */
static void ore_turret_parts(
    const ore_weapons_ctx_t *ctx,
    OreTurret *turret,
    ecs_entity_t e,
    const OreBeam *beam)
{
    ecs_entity_t b;
    if (!turret->head) {
        turret->head = ore_child(ctx->world, e, "head");
        if (turret->head) {
            ecs_add_id(ctx->stage, turret->head, DynamicTransform);
            turret->fx = ore_child(ctx->world, turret->head, "fx");
        }

        turret->hit_fx = ore_child(ctx->world, e, "hit_fx");
        if (turret->hit_fx) {
            ecs_add_id(ctx->stage, turret->hit_fx, DynamicTransform);
        }
    }

    if (beam && beam->prefab && !turret->beam) {
        b = ecs_new_w_pair(ctx->stage, EcsChildOf, e);
        ecs_add_pair(ctx->stage, b, EcsIsA, beam->prefab);
        ecs_add_id(ctx->stage, b, DynamicTransform);
        ecs_add_id(ctx->stage, b, SpatialIgnore);
        ecs_set(ctx->stage, b, Position, { 0, 0, 0 });
        ecs_set(ctx->stage, b, Rotation, { 0, 0, 0 });
        ecs_set(ctx->stage, b, Scale, { 1, 1, 1 });
        ecs_enable(ctx->stage, b, false);
        turret->beam = b;
    }
}

/** Hide the beam of a turret; it is only written when it shows.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param beam The beam, 0 for none
 */
static void ore_beam_hide(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t beam)
{
    if (beam && ecs_is_alive(world, beam) && !ecs_has_id(world, beam, EcsDisabled)) {
        ecs_enable(stage, beam, false);
    }
}

/** Show the beam of a turret from its muzzle to a point: halfway between them, turned toward the
 * point and scaled along Z to their distance. Only the values that change are written.
 *
 * @param ctx The context of the system run
 * @param beam The beam, a child of the turret
 * @param p Position of the turret
 * @param from The muzzle
 * @param to The point the beam hits
 */
static void ore_beam_show(
    const ore_weapons_ctx_t *ctx,
    ecs_entity_t beam,
    const Position *p,
    const vec3 from,
    const vec3 to)
{
    float bx = to[0] - from[0], by = to[1] - from[1], bz = to[2] - from[2];
    float len = sqrtf(bx * bx + by * by + bz * bz);
    Position at = {
        from[0] + bx * 0.5f - p->x, from[1] + by * 0.5f - p->y, from[2] + bz * 0.5f - p->z
    };
    Rotation rot = ore_heading(bx, by, bz);
    const Position *cp;
    const Rotation *cr;
    const Scale *cs;
    if (!beam || !ecs_is_alive(ctx->world, beam)) {
        goto done;
    }

    cp = ecs_get(ctx->world, beam, Position);
    cr = ecs_get(ctx->world, beam, Rotation);
    cs = ecs_get(ctx->world, beam, Scale);
    if (!cp || cp->x != at.x || cp->y != at.y || cp->z != at.z) {
        ecs_set_ptr(ctx->stage, beam, Position, &at);
    }

    if (!cr || cr->x != rot.x || cr->y != rot.y || cr->z != rot.z) {
        ecs_set_ptr(ctx->stage, beam, Rotation, &rot);
    }

    if (!cs || cs->x != 1 || cs->y != 1 || cs->z != len) {
        ecs_set(ctx->stage, beam, Scale, { 1, 1, len });
    }

    if (ecs_has_id(ctx->world, beam, EcsDisabled)) {
        ecs_enable(ctx->stage, beam, true);
    }

done:
    return;
}

/** Find the target of a turret: it keeps its target while that lives and is within range on
 * XZ, otherwise it takes the nearest live critter within range.
 *
 * @param ctx The context of the system run
 * @param target The turret's target, receives the new one
 * @param p Position of the turret
 * @param range Range of the turret
 * @return Position of the target, NULL for none
 */
static const Position* ore_turret_target(
    const ore_weapons_ctx_t *ctx,
    OreTarget *target,
    const Position *p,
    float range)
{
    const Position *result = ore_live_critter(ctx->world, target->value);
    float dx, dz;
    if (result) {
        dx = result->x - p->x;
        dz = result->z - p->z;
        result = dx * dx + dz * dz > range * range ? NULL : result;
    }

    if (!result) {
        target->value = ore_nearest_live(ctx, p->x, p->z, range);
        result = target->value ? ecs_get(ctx->world, target->value, Position) : NULL;
    }

    return result;
}

/** Lead a shot: with the target's velocity v not 0, estimate the time of flight from the
 * muzzle's height above the turret's center to the aim twice, the second time to the first lead
 * point, capped at cfg.leadCap, and aim at the target's XZ plus v times that time.
 *
 * @param ctx The context of the system run
 * @param target The target
 * @param tp Position of the target
 * @param p Position of the turret
 * @param muzzle The muzzle of the turret
 * @param lx Receives X of the point aimed at
 * @param lz Receives Z of the point aimed at
 */
static void ore_turret_lead(
    const ore_weapons_ctx_t *ctx,
    ecs_entity_t target,
    const Position *tp,
    const Position *p,
    const OreMuzzle *muzzle,
    float *lx,
    float *lz)
{
    const OreVelocity *v = ecs_get(ctx->world, target, OreVelocity);
    float speed = muzzle->speed > 0 ? muzzle->speed : 1.0f;
    float ay = tp->y + ctx->aim_y - (p->y + muzzle->height);
    float ax = tp->x - p->x, az = tp->z - p->z, t;
    *lx = tp->x;
    *lz = tp->z;
    if (v && (v->x != 0 || v->z != 0)) {
        t = sqrtf(ax * ax + ay * ay + az * az) / speed;
        ax = tp->x + v->x * t - p->x;
        az = tp->z + v->z * t - p->z;
        t = glm_min(sqrtf(ax * ax + ay * ay + az * az) / speed, ctx->lead_cap);
        *lx = tp->x + v->x * t;
        *lz = tp->z + v->z * t;
    }
}

/** Turn the head of a turret to an angle about Y; it is only written when it changes.
 *
 * @param ctx The context of the system run
 * @param turret The turret's state, receives the aim
 * @param aim The angle in radians
 */
static void ore_turret_aim(
    const ore_weapons_ctx_t *ctx,
    OreTurret *turret,
    float aim)
{
    const Rotation *r = turret->head && ecs_is_alive(ctx->world, turret->head) ?
        ecs_get(ctx->world, turret->head, Rotation) : NULL;
    turret->aim = aim;
    if (turret->head && (!r || r->x != 0 || r->y != aim || r->z != 0)) {
        ecs_set(ctx->stage, turret->head, Rotation, { 0, aim, 0 });
    }
}

/** Get the multiplier of the category of the turret of a row, its level times step.
 *
 * @param it The iterator of OreTurrets
 * @param alloc The allocation
 * @param step cfg.powerStep
 * @param row Row of the turret
 * @return The multiplier, 1 without a category
 */
static float ore_turret_mul(
    ecs_iter_t *it,
    const OrePowerAlloc *alloc,
    float step,
    int32_t row)
{
    const OrePowerCategory *cat = ecs_field(it, OrePowerCategory, 13);
    int32_t kind;
    float result = 1.0f;
    cat = cat && ecs_field_is_self(it, 13) ? &cat[row] : cat;
    kind = cat ? (int32_t)cat->kind : -1;
    if (kind >= 0 && kind < ORE_POWER_KINDS) {
        result = (float)alloc->level[kind] * step;
    }

    return result;
}

/** Read the effects of combat and their constants, once per system run.
 *
 * @param world The world
 * @return The effects and the constants
 */
static ore_combat_fx_t ore_combat_fx_get(
    const ecs_world_t *world)
{
    ore_combat_fx_t result = {
        .effects = ecs_singleton_get(world, OreEffects),
        .hit_inset = ORE_CFG(world, "turretHitInset"),
        .blast_per = ORE_CFG(world, "splashBlastPer"),
        .blast_min = ORE_CFG(world, "splashBlastMin"),
        .blast_max = ORE_CFG(world, "splashBlastMax"),
        .blast_lift = ORE_CFG(world, "splashBlastLift"),
        .impact_y = ORE_CFG(world, "impactY")
    };

    return result;
}

/** Get a part of a turret when it is alive.
 *
 * @param world The world
 * @param part The part, 0 for none
 * @return The part, 0 when it is gone
 */
static ecs_entity_t ore_fx_part(
    const ecs_world_t *world,
    ecs_entity_t part)
{
    return part && ecs_is_alive(world, part) ? part : 0;
}

/** Get the muzzle of a turret from its parts, the original's oreTurretMuzzle: the position of
 * its head plus the position of the head's fx turned by the aim, above the turret's position.
 *
 * @param world The world
 * @param turret The turret's state, with its head, its fx and its aim
 * @param p Position of the turret
 * @param out Receives the muzzle in world space, left as it is without both parts
 */
static void ore_turret_muzzle(
    const ecs_world_t *world,
    const OreTurret *turret,
    const Position *p,
    vec3 out)
{
    ecs_entity_t head = ore_fx_part(world, turret->head);
    ecs_entity_t fx = ore_fx_part(world, turret->fx);
    const Position *hp = head ? ecs_get(world, head, Position) : NULL;
    const Position *tp = fx ? ecs_get(world, fx, Position) : NULL;
    float s = sinf(turret->aim), c = cosf(turret->aim);
    if (hp && tp) {
        out[0] = p->x + hp->x + tp->x * c + tp->z * s;
        out[1] = p->y + hp->y + tp->y;
        out[2] = p->z + hp->z - tp->x * s + tp->z * c;
    }
}

/** Stop the effects of a turret that does not fire: the glow at the lens of a laser and the
 * sparks where its beam hits, the muzzle flash of a gun or missile turret, which goes dark at
 * once. Only written when they change.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param turret The turret's state, with its fx and hit_fx
 */
static void ore_turret_fx_stop(
    const ecs_world_t *world,
    ecs_world_t *stage,
    const OreTurret *turret)
{
    ore_playback_set(world, stage, ore_fx_part(world, turret->fx), ParticleStopped, false);
    ore_playback_set(world, stage, ore_fx_part(world, turret->hit_fx), ParticleStopped, false);
}

/** Go on with the muzzle flash of a turret that fires again after a pause, where its fade was
 * held: a paused fx plays on, a stopped or playing one is left alone.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param turret The turret's state, with its fx
 */
static void ore_turret_fx_resume(
    const ecs_world_t *world,
    ecs_world_t *stage,
    const OreTurret *turret)
{
    ecs_entity_t fx = ore_fx_part(world, turret->fx);
    const ParticlePlayback *playback = fx ? ecs_get(world, fx, ParticlePlayback) : NULL;
    if (playback && playback->state == ParticlePaused) {
        ore_playback_set(world, stage, fx, ParticlePlaying, false);
    }
}

/** Play the effects of a laser's beam from the muzzle to the point it hits: the glow at the lens
 * (BeamGlow) and the sparks (BeamSparks) of hit_fx, whose ParticleLight lights while they play.
 *
 * hit_fx moves to the end of the beam, set back toward the muzzle by the target's radius (0.5
 * without one) times cfg.turretHitInset, at most halfway.
 *
 * @param fx The effects of combat
 * @param world The world
 * @param stage The stage of the system
 * @param turret The turret's state, with its fx and hit_fx
 * @param p Position of the turret
 * @param from The muzzle
 * @param to The point the beam hits
 * @param radius Radius of the target, 0 for none
 */
static void ore_beam_fx(
    const ore_combat_fx_t *fx,
    const ecs_world_t *world,
    ecs_world_t *stage,
    const OreTurret *turret,
    const Position *p,
    const vec3 from,
    const vec3 to,
    float radius)
{
    ecs_entity_t glow = ore_fx_part(world, turret->fx);
    ecs_entity_t sparks = ore_fx_part(world, turret->hit_fx);
    float bx = to[0] - from[0], by = to[1] - from[1], bz = to[2] - from[2];
    float len = sqrtf(bx * bx + by * by + bz * bz);
    float inset = (radius > 0 ? radius : 0.5f) * fx->hit_inset;
    float back = len > 0.001f ? glm_min(inset / len, 0.5f) : 0;
    Position at = {
        from[0] + bx * (1.0f - back) - p->x,
        from[1] + by * (1.0f - back) - p->y,
        from[2] + bz * (1.0f - back) - p->z
    };
    const Position *cur;
    ore_playback_set(world, stage, glow, ParticlePlaying, false);
    if (sparks) {
        cur = ecs_get(world, sparks, Position);
        if (!cur || cur->x != at.x || cur->y != at.y || cur->z != at.z) {
            ecs_set_ptr(stage, sparks, Position, &at);
        }

        ore_playback_set(world, stage, sparks, ParticlePlaying, false);
    }
}

/** Play the effects of a shot of a turret at its muzzle: the SparkBurst, the muzzle flash of its
 * fx played from the start (it lights at its peak and fades) and, with OreLaunchFx, the launch
 * turned to the aim of the head.
 *
 * @param fx The effects of combat
 * @param world The world
 * @param stage The stage of the system
 * @param e The turret
 * @param turret The turret's state, with its fx and its aim
 * @param from The muzzle
 */
static void ore_turret_shot_fx(
    const ore_combat_fx_t *fx,
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    const OreTurret *turret,
    const vec3 from)
{
    ecs_entity_t flash = ore_fx_part(world, turret->fx);
    const OreLaunchFx *launch = ecs_get(world, e, OreLaunchFx);
    if (fx->effects) {
        ore_burst(stage, fx->effects->spark_burst, from, 1.0f);
    }

    if (flash && ecs_has(world, flash, ParticleLight)) {
        ecs_set(stage, flash, ParticlePlayback, { .state = ParticlePlaying, .reset = true });
    }

    if (launch && launch->burst) {
        ore_burst_aimed(stage, launch->burst, from, turret->aim);
    }
}

/** Play the effects of the end of a projectile, the original's: one with splash explodes
 * (BoomBurst cfg.splashBlastLift above it, scaled clamp(splash / cfg.splashBlastPer,
 * cfg.splashBlastMin, cfg.splashBlastMax)).
 *
 * One that hit a critter bursts into a SparkBurst and a HitPuff; one at cfg.impactY or below
 * throws up ImpactDust and a SparkBurst at that height.
 *
 * @param fx The effects of combat
 * @param stage The stage of the system
 * @param p Position of the projectile at its end
 * @param splash Splash radius, 0 for none
 * @param hit Whether it hit a critter
 */
static void ore_shot_end_fx(
    const ore_combat_fx_t *fx,
    ecs_world_t *stage,
    const Position *p,
    float splash,
    bool hit)
{
    const OreEffects *effects = fx->effects;
    float per = fx->blast_per > 0 ? fx->blast_per : 1.0f;
    vec3 at = { p->x, p->y, p->z };
    if (!effects) {
        goto done;
    }

    if (splash > 0) {
        at[1] += fx->blast_lift;
        ore_explosion(stage, effects->boom_burst, at,
            glm_clamp(splash / per, fx->blast_min, fx->blast_max));
    } else if (hit) {
        ore_burst(stage, effects->spark_burst, at, 1.0f);
        ore_burst(stage, effects->hit_puff, at, 1.0f);
    } else if (p->y <= fx->impact_y) {
        at[1] = fx->impact_y;
        ore_burst(stage, effects->impact_dust, at, 1.0f);
        ore_burst(stage, effects->spark_burst, at, 1.0f);
    }

done:
    return;
}

/** Play the turrets: target the critters, turn the heads, burn with the lasers and fire. A
 * turret that draws power does not fire in a blackout, and none with its Weapons multiplier at 0.
 *
 * The laser deals damage * dt * multiplier to its target every frame and shows its beam from the
 * muzzle of its parts, with its glow and sparks. Every shot plays the effects of the muzzle; a
 * turret that does not fire stops its effects.
 *
 * The gun and the missile turret count their timer down while they have a target and fire at 0
 * or below, adding interval / multiplier to it, so they keep that rate whatever the frame rate.
 *
 * @param it Iterator over the placed turrets, while playing
 */
static void OreTurrets(
    ecs_iter_t *it)
{
    OreTurret *turret = ecs_field(it, OreTurret, 0);
    const OreDamage *damage = ecs_field(it, OreDamage, 2);
    const OreRange *range = ecs_field(it, OreRange, 3);
    const OreMuzzle *muzzle = ecs_field(it, OreMuzzle, 4);
    OreTarget *target = ecs_field(it, OreTarget, 5);
    const Position *p = ecs_field(it, Position, 6);
    const OreAttackInterval *interval = ecs_field(it, OreAttackInterval, 7);
    OreAttackTimer *timer = ecs_field(it, OreAttackTimer, 8);
    const OreAmmo *ammo = ecs_field(it, OreAmmo, 9);
    const OreBeam *beam = ecs_field(it, OreBeam, 10);
    const OreSplash *splash = ecs_field(it, OreSplash, 11);
    bool consumer = ecs_field_is_set(it, 12);
    const OrePowerState *power = ecs_field(it, OrePowerState, 14);
    const OrePowerAlloc *alloc = ecs_field(it, OrePowerAlloc, 15);
    ore_weapons_ctx_t ctx = ore_weapons_ctx(it, ecs_field(it, OreCritterIndex, 16),
        ecs_field(it, OreArmory, 17));
    ore_combat_fx_t fx = ore_combat_fx_get(it->world);
    float step = ORE_CFG(it->world, "powerStep");
    bool ranged = interval && timer && ammo;
    bool lead = !beam && ranged && ammo->projectile &&
        !ecs_has_id(it->world, ammo->projectile, OreHoming);
    const OreRadius *radius;
    const Position *tp;
    float mul, lx, lz, aim, r;
    int32_t i, shots;
    vec3 from, to;
    for (i = 0; i < it->count; i ++) {
        ore_turret_parts(&ctx, &turret[i], it->entities[i], beam ? &beam[i] : NULL);
        mul = ore_turret_mul(it, alloc, step, i);
        tp = (consumer && power->blackout) || mul <= 0 ? NULL :
            ore_turret_target(&ctx, &target[i], &p[i], range[i].value);
        if (!tp) {
            ore_beam_hide(it->world, it->stage, turret[i].beam);
            ore_turret_fx_stop(it->world, it->stage, &turret[i]);
            continue;
        }

        ore_turret_fx_resume(it->world, it->stage, &turret[i]);
        lx = tp->x;
        lz = tp->z;
        if (lead) {
            ore_turret_lead(&ctx, target[i].value, tp, &p[i], &muzzle[i], &lx, &lz);
        }

        aim = atan2f(lx - p[i].x, lz - p[i].z);
        ore_turret_aim(&ctx, &turret[i], aim);
        from[0] = p[i].x + sinf(aim) * muzzle[i].forward;
        from[1] = p[i].y + muzzle[i].height;
        from[2] = p[i].z + cosf(aim) * muzzle[i].forward;
        if (beam) {
            ore_critter_hurt(it->world, it->stage, target[i].value,
                damage[i].value * ctx.dt * mul);
            radius = ecs_get(it->world, target[i].value, OreRadius);
            r = radius && radius->value > 0 ? radius->value * ctx.aim_per_radius : ctx.aim_y;
            to[0] = tp->x;
            to[1] = tp->y + r;
            to[2] = tp->z;
            ore_turret_muzzle(it->world, &turret[i], &p[i], from);
            ore_beam_show(&ctx, turret[i].beam, &p[i], from, to);
            ore_beam_fx(&fx, it->world, it->stage, &turret[i], &p[i], from, to,
                radius ? radius->value : 0);
            continue;
        }

        if (!ranged) {
            continue;
        }

        to[0] = lx;
        to[1] = tp->y + ctx.aim_y;
        to[2] = lz;
        timer[i].value -= ctx.dt;
        for (shots = 0; timer[i].value <= 0 && shots < ORE_SHOTS_MAX; shots ++) {
            ore_projectile_fire(&ctx, ammo[i].projectile, from, to, muzzle[i].speed,
                damage[i].value, splash ? splash[i].radius : 0, target[i].value);
            ore_turret_shot_fx(&fx, it->world, it->stage, it->entities[i], &turret[i], from);
            timer[i].value += interval[i].value / mul;
        }
    }
}

/** Steer a homing projectile: when its target is gone it takes the nearest live critter within
 * cfg.missileReacquire, then it turns toward the target by at most cfg.missileTurnRate * dt, or
 * straight at it within speed / turn rate. Its speed stays, its rotation follows its velocity.
 *
 * @param ctx The context of the system run
 * @param shot The projectile, receives its velocity
 * @param target Its target, receives the new one
 * @param p Its position
 * @param rot Receives its rotation
 */
static void ore_projectile_home(
    const ore_weapons_ctx_t *ctx,
    OreProjectile *shot,
    OreTarget *target,
    const Position *p,
    Rotation *rot)
{
    float sp = sqrtf(shot->vx * shot->vx + shot->vy * shot->vy + shot->vz * shot->vz);
    float dx, dy, dz, dist, tx, ty, tz, hx, hy, hz, lock, turn, dot, ox, oy, oz, olen, c, s;
    const Position *tp;
    if (sp < 0.0001f) {
        goto done;
    }

    tp = ore_live_critter(ctx->world, target->value);
    if (!tp && ctx->reacquire > 0) {
        target->value = ore_nearest_live(ctx, p->x, p->z, ctx->reacquire);
        tp = ore_live_critter(ctx->world, target->value);
    }

    if (tp) {
        dx = tp->x - p->x;
        dy = tp->y + ctx->aim_y - p->y;
        dz = tp->z - p->z;
        dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > 0.0001f) {
            tx = dx / dist;
            ty = dy / dist;
            tz = dz / dist;
            hx = shot->vx / sp;
            hy = shot->vy / sp;
            hz = shot->vz / sp;
            lock = ctx->turn_rate > 0 ? sp / ctx->turn_rate : 0;
            if (ctx->turn_rate > 0 && dist > lock) {
                turn = ctx->turn_rate * ctx->dt;
                dot = glm_clamp(hx * tx + hy * ty + hz * tz, -1.0f, 1.0f);
                if (acosf(dot) > turn) {
                    ox = tx - hx * dot;
                    oy = ty - hy * dot;
                    oz = tz - hz * dot;
                    olen = sqrtf(ox * ox + oy * oy + oz * oz);
                    if (olen > 0.0001f) {
                        c = cosf(turn);
                        s = sinf(turn) / olen;
                        tx = hx * c + ox * s;
                        ty = hy * c + oy * s;
                        tz = hz * c + oz * s;
                    }
                }
            }

            shot->vx = tx * sp;
            shot->vy = ty * sp;
            shot->vz = tz * sp;
        }
    }

    *rot = ore_heading(shot->vx, shot->vy, shot->vz);
done:
    return;
}

/** Fly the projectiles: homing ones steer, every one moves and its life goes down by dt. It
 * hits the nearest live critter within its radius on XZ and expires at life 0 or y 0 or below.
 *
 * On a hit or at its end a projectile with splash hurts every live critter within the splash
 * radius on XZ with its full damage, one without the critter it hit; then it plays the effects
 * of its end (the explosion, the hit, the impact on the ground) and is deleted.
 *
 * @param it Iterator over the projectiles, while playing
 */
static void OreProjectileFly(
    ecs_iter_t *it)
{
    OreProjectile *shot = ecs_field(it, OreProjectile, 0);
    const OreDamage *damage = ecs_field(it, OreDamage, 1);
    Position *p = ecs_field(it, Position, 2);
    Rotation *rot = ecs_field(it, Rotation, 3);
    const OreSplash *splash = ecs_field(it, OreSplash, 4);
    OreTarget *target = ecs_field(it, OreTarget, 5);
    bool homing = ecs_field_is_set(it, 6) && target;
    ore_weapons_ctx_t ctx = ore_weapons_ctx(it, ecs_field(it, OreCritterIndex, 7), NULL);
    ore_splash_t blast = { .world = it->world, .stage = it->stage };
    ore_combat_fx_t fx = ore_combat_fx_get(it->world);
    ecs_entity_t hit;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (homing) {
            ore_projectile_home(&ctx, &shot[i], &target[i], &p[i], &rot[i]);
        }

        p[i].x += shot[i].vx * ctx.dt;
        p[i].y += shot[i].vy * ctx.dt;
        p[i].z += shot[i].vz * ctx.dt;
        shot[i].life -= ctx.dt;
        hit = ore_nearest_live(&ctx, p[i].x, p[i].z, shot[i].radius);
        if (!hit && shot[i].life > 0 && p[i].y > 0) {
            continue;
        }

        if (splash) {
            blast.damage = damage[i].value;
            if (ctx.index) {
                ore_critter_each(ctx.index, p[i].x, p[i].z, splash[i].radius,
                    ore_splash_visit, &blast);
            }
        } else if (hit) {
            ore_critter_hurt(it->world, it->stage, hit, damage[i].value);
        }

        ore_shot_end_fx(&fx, it->stage, &p[i], splash ? splash[i].radius : 0, hit != 0);
        ecs_delete(it->stage, it->entities[i]);
    }
}

/** Hide the laser beams outside play: like the original's, which it drew every frame of play,
 * they do not stay on the pause or end screens. The effects of the turrets pause, the muzzle
 * flashes holding their light; OreTurrets plays or stops them again when the game resumes.
 *
 * @param it Iterator over the placed turrets, while not playing
 */
static void OreBeamHide(
    ecs_iter_t *it)
{
    const OreTurret *turret = ecs_field(it, OreTurret, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ore_beam_hide(it->world, it->stage, turret[i].beam);
        ore_playback_set(it->world, it->stage, ore_fx_part(it->world, turret[i].fx),
            ParticlePaused, true);
        ore_playback_set(it->world, it->stage, ore_fx_part(it->world, turret[i].hit_fx),
            ParticlePaused, true);
    }
}

/** Play the smoke trails of the missiles in flight, the emitters below a projectile, while
 * playing. A trail lingers after its missile, its particles finishing their lives where they
 * were left.
 *
 * @param it Iterator over the emitters below the projectiles, while playing
 */
static void OreTrails(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ore_playback_set(it->world, it->stage, it->entities[i], ParticlePlaying, false);
    }
}

/** Pause the smoke trails outside of play, so they stand still with the missiles; OreTrails
 * plays them again when the game resumes.
 *
 * @param it Iterator over the emitters below the projectiles, while not playing
 */
static void OreTrailsHold(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ore_playback_set(it->world, it->stage, it->entities[i], ParticlePaused, true);
    }
}

void ore_weapons_import(
    ecs_world_t *world)
{
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OreAttackInterval), EcsWith, ecs_id(OreAttackTimer));
    ecs_add_pair(world, ecs_id(OreTurret), EcsWith, ecs_id(OreTarget));
    ecs_add_pair(world, ecs_id(OreProjectile), EcsWith, SpatialIgnore);
    ecs_singleton_ensure(world, OreArmory);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePistol" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreDamage), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreRange), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreMuzzle), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreAttackInterval), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreAttackTimer), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreAmmo), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreCritterIndex), .src.id = ecs_id(OreCritterIndex),
              .inout = EcsIn },
            { .id = ecs_id(OreArmory), .src.id = ecs_id(OreArmory), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePistol
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreTurrets" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreTurret), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreDamage), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreRange), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreMuzzle), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreTarget), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreAttackInterval), .src.id = EcsSelf, .inout = EcsIn,
              .oper = EcsOptional },
            { .id = ecs_id(OreAttackTimer), .src.id = EcsSelf, .inout = EcsInOut,
              .oper = EcsOptional },
            { .id = ecs_id(OreAmmo), .src.id = EcsSelf, .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OreBeam), .src.id = EcsSelf, .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OreSplash), .src.id = EcsSelf, .inout = EcsIn,
              .oper = EcsOptional },
            { .id = ecs_id(OrePowerConsumer), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsInOutNone, .oper = EcsOptional },
            { .id = ecs_id(OrePowerCategory), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = ecs_id(OreCritterIndex), .src.id = ecs_id(OreCritterIndex),
              .inout = EcsIn },
            { .id = ecs_id(OreArmory), .src.id = ecs_id(OreArmory), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreTurrets
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreProjectileFly" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreProjectile), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreDamage), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Rotation), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreSplash), .src.id = EcsSelf, .inout = EcsIn,
              .oper = EcsOptional },
            { .id = ecs_id(OreTarget), .src.id = EcsSelf, .inout = EcsInOut,
              .oper = EcsOptional },
            { .id = OreHoming, .src.id = EcsSelf, .inout = EcsInOutNone,
              .oper = EcsOptional },
            { .id = ecs_id(OreCritterIndex), .src.id = ecs_id(OreCritterIndex),
              .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreProjectileFly
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreBeamHide" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreTurret), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OreBeamHide
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreTrails" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ParticleEmitter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreProjectile), .src.id = EcsUp, .trav = EcsChildOf,
              .inout = EcsInOutNone },
            { .id = playing, .src.id = state }
        },
        .callback = OreTrails
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreTrailsHold" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(ParticleEmitter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreProjectile), .src.id = EcsUp, .trav = EcsChildOf,
              .inout = EcsInOutNone },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OreTrailsHold
    });
}
