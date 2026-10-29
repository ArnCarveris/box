/**
 * @file critter_fx.c
 * @brief The critters' animation, deaths and corpses, the original's critter walk in
 * oreCritterSystem, the critter half of OreLimbs, and oreDamageCritter's death, oreCorpseMake and
 * OreCorpses.
 *
 * The walk is read from the critters' state: OreCritterMove sets their velocity and whether they
 * attack, OreCritterFx advances their walk cycle (OreCritter.anim) by the distance walked and
 * poses them, bobbing and waddling, and OreCritterLimbs swings their legs with the cycle.
 *
 * A critter that dies plays its death burst and leaves a corpse: a new instance of its prefab
 * with OreCorpse instead of OreCritter, which no critter system sees. A Tint on its root chars it
 * and OreCorpseChar darkens its emissive parts; OreCorpses sinks, deletes and caps the corpses.
 */

#include "game.h"
#include <math.h>

/** A corpse and its age, for picking the oldest above the cap */
typedef struct ore_corpse_age_t {
    ecs_entity_t corpse; /**< The corpse */
    float age;           /**< Its OreCorpse.age */
} ore_corpse_age_t;

/** Convert a rotation from the original's Euler order to box's: the angles about x, y and z
 * composed Rx * Ry * Rz (flecs-engine, glm_euler_xyz) to the same orientation composed
 * Ry * Rx * Rz (box, glm_euler_yxz).
 *
 * @param x Angle about x in the original's order
 * @param y Angle about y in the original's order
 * @param z Angle about z in the original's order
 * @return The rotation in box's order
 */
static Rotation ore_euler_yxz(
    float x,
    float y,
    float z)
{
    mat4 m;
    Rotation result;
    glm_euler_xyz((vec3){ x, y, z }, m);
    result.x = asinf(glm_clamp(-m[2][1], -1.0f, 1.0f));
    result.y = atan2f(m[2][0], m[2][2]);
    result.z = atan2f(m[0][1], m[1][1]);

    return result;
}

/** Walk the critters: the cycle advances by dt * walked speed * OreGait.step while one walks
 * (its velocity is 0 while it attacks), its body bobs |sin(anim)| * bob, 0 while it attacks, and
 * it waddles sin(anim / 2) * cfg.critterWaddle about z. OreCritterMove turns it to its heading.
 *
 * @param it Iterator over the critters with their gait, while playing
 */
static void OreCritterFx(
    ecs_iter_t *it)
{
    OreCritter *critter = ecs_field(it, OreCritter, 0);
    const OreVelocity *v = ecs_field(it, OreVelocity, 1);
    const OreGait *gait = ecs_field(it, OreGait, 2);
    Position *p = ecs_field(it, Position, 3);
    Rotation *r = ecs_field(it, Rotation, 4);
    bool self = ecs_field_is_self(it, 2);
    float waddle = ORE_CFG(it->world, "critterWaddle");
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const OreGait *g = self ? &gait[i] : gait;
        OreCritter *c = &critter[i];
        if (!c->attacking) {
            c->anim += it->delta_time * sqrtf(v[i].x * v[i].x + v[i].z * v[i].z) * g->step;
        }

        p[i].y = c->attacking ? 0.0f : fabsf(sinf(c->anim)) * g->bob;
        r[i].z = sinf(c->anim * 0.5f) * waddle;
    }
}

/** Swing the legs of the critters with their walk cycle: sin(anim + phase) * swing about x,
 * at cfg.critterAttackSwing of the swing while the critter attacks.
 *
 * @param it Iterator over the limbs below a critter, while playing
 */
static void OreCritterLimbs(
    ecs_iter_t *it)
{
    const OreLimb *limb = ecs_field(it, OreLimb, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    const OreCritter *critter = ecs_field(it, OreCritter, 2);
    float scale = critter->attacking ? ORE_CFG(it->world, "critterAttackSwing") : 1.0f;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        r[i].x = sinf(critter->anim + limb[i].phase) * limb[i].swing * scale;
    }
}

/** Char the corpses: the parts that inherit their emission from the critter's prefab (its eyes,
 * visors and glowing spots) go dark; the Tint of the corpse colors the rest. Parts that own
 * their emission, the dark ones and the specks of the embers, do not match.
 *
 * @param it Iterator over the parts below a corpse that inherit Emissive, while playing
 */
static void OreCorpseChar(
    ecs_iter_t *it)
{
    const Emissive *em = ecs_field(it, Emissive, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_set(it->stage, it->entities[i], Emissive, { .strength = 0, .color = em->color });
    }
}

/** Delete the oldest corpses of a list until it holds at most a number of them.
 *
 * @param stage The stage of the system
 * @param list The corpses and their ages, the deleted ones are removed
 * @param cap Corpses kept at most, no limit for 0 or less
 */
static void ore_corpse_cull(
    ecs_world_t *stage,
    ecs_vec_t *list,
    int32_t cap)
{
    ore_corpse_age_t *a;
    int32_t i, oldest, count;
    while (cap > 0 && ecs_vec_count(list) > cap) {
        a = ecs_vec_first_t(list, ore_corpse_age_t);
        count = ecs_vec_count(list);
        oldest = 0;
        for (i = 1; i < count; i ++) {
            oldest = a[i].age > a[oldest].age ? i : oldest;
        }

        ecs_delete(stage, a[oldest].corpse);
        ecs_vec_remove_t(list, ore_corpse_age_t, oldest);
    }
}

/** Age the corpses: one lies cfg.corpseTime seconds of play, then sinks at cfg.corpseSink units
 * a second and is deleted below its OreCorpse.depth. Above cfg.corpseCap corpses the oldest are
 * deleted, so a corpse laid this frame takes the place of the oldest the next.
 *
 * @param it Iterator over the corpses with their positions, while playing
 */
static void OreCorpses(
    ecs_iter_t *it)
{
    float life = ORE_CFG(it->world, "corpseTime");
    float sink = ORE_CFG(it->world, "corpseSink");
    int32_t cap = ORE_CFG_I32(it->world, "corpseCap");
    ecs_vec_t list = {0};
    ore_corpse_age_t *slot;
    OreCorpse *corpse;
    Position *p;
    int32_t i;
    while (ecs_iter_next(it)) {
        corpse = ecs_field(it, OreCorpse, 0);
        p = ecs_field(it, Position, 1);
        for (i = 0; i < it->count; i ++) {
            corpse[i].age += it->delta_time;
            if (corpse[i].age >= life) {
                p[i].y -= sink * it->delta_time;
            }

            if (p[i].y < -corpse[i].depth) {
                ecs_delete(it->stage, it->entities[i]);
            } else {
                slot = ecs_vec_append_t(NULL, &list, ore_corpse_age_t);
                slot->corpse = it->entities[i];
                slot->age = corpse[i].age;
            }
        }
    }

    ore_corpse_cull(it->stage, &list, cap);
    ecs_vec_fini_t(NULL, &list, ore_corpse_age_t);
}

ecs_entity_t ore_corpse_spawn(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x,
    float z,
    float yaw)
{
    const ecs_world_t *real = ecs_get_world(world);
    const OreRadius *radius = prefab ? ecs_get(world, prefab, OreRadius) : NULL;
    const OreWaveState *w = ecs_singleton_get(real, OreWaveState);
    const OreArsenal *arsenal = ecs_singleton_get(real, OreArsenal);
    ecs_entity_t root = w && w->root && ecs_is_alive(real, w->root) ? w->root : 0;
    Tint tint = ecs_const_var_get_t(real, "cfg.corpseColor", Tint);
    float life = ORE_CFG(real, "corpseTime");
    float squash = ORE_CFG(real, "corpseSquash");
    float r = radius ? radius->value : 0.5f, tip, roll, s;
    ecs_entity_t e = 0, embers;
    Rotation pose;
    if (prefab && life > 0) {
        tip = ORE_CFG(real, "corpseTip") + ore_random01(world) * ORE_CFG(real, "corpseTipJitter");
        roll = (ore_random01(world) - 0.5f) * ORE_CFG(real, "corpseRoll");
        pose = ore_euler_yxz(tip, yaw, roll);
        e = root ? ecs_new_w_pair(world, EcsChildOf, root) : ecs_new(world);
        ecs_add_pair(world, e, EcsIsA, prefab);
        ecs_add_id(world, e, DynamicTransform);
        ecs_set(world, e, Position, { x, 0, z });
        ecs_set_ptr(world, e, Rotation, &pose);
        ecs_set(world, e, Scale, { 1, squash, 1 });
        ecs_set_ptr(world, e, Tint, &tint);
        ecs_set(world, e, OreCorpse, { .age = 0, .depth = r * 2.0f + 1.0f });
        if (arsenal && arsenal->embers) {
            s = r * 2.0f;
            embers = ecs_new_w_pair(world, EcsChildOf, e);
            ecs_set(world, embers, Position, { 0, r * 0.5f, 0 });
            ecs_set(world, embers, Rotation, { 0, ore_random01(world) * 2.0f * GLM_PIf, 0 });
            ecs_set(world, embers, Scale, { s, s, s });
            ecs_add_id(world, embers, arsenal->embers);
        }
    }

    return e;
}

void ore_critter_remains(
    ecs_world_t *world,
    ecs_entity_t critter)
{
    const ecs_world_t *real = ecs_get_world(world);
    const Position *p = ecs_get(world, critter, Position);
    const Rotation *r = ecs_get(world, critter, Rotation);
    const OreRadius *radius = ecs_get(world, critter, OreRadius);
    const OreDeathBurst *death = ecs_get(world, critter, OreDeathBurst);
    const OreArsenal *arsenal = ecs_singleton_get(real, OreArsenal);
    ecs_entity_t prefab = ecs_get_target(world, critter, EcsIsA, 0);
    ecs_entity_t burst = death ? death->burst : 0;
    float yaw = r ? r->y : 0;
    float size = radius ? radius->value : 0.5f;
    vec3 at;
    if (p) {
        at[0] = p->x;
        at[1] = p->y;
        at[2] = p->z;
        if (burst) {
            ore_combust(world, burst, at, size);
        } else if (arsenal && arsenal->splat) {
            ore_burst(world, arsenal->splat,
                (vec3){ at[0], at[1] + ORE_CFG(real, "splatLift"), at[2] }, 1.0f);
        }

        ore_corpse_spawn(world, prefab, at[0], at[2], yaw);
    }
}

/** Lay a corpse from a script, oreelse.corpse(prefab, position, yaw): ore_corpse_spawn on the
 * ground below the position.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The critter prefab, the position and the heading
 * @param result Receives the corpse, 0 for none
 */
static void ore_corpse_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const float *at = argv[1].ptr;
    (void)argc;
    *(ecs_entity_t*)result->ptr = ore_corpse_spawn(ctx->world, *(ecs_entity_t*)argv[0].ptr,
        at[0], at[2], *(float*)argv[2].ptr);
}

void ore_critter_fx_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OreGait), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreDeathBurst), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreLimb), EcsWith, DynamicTransform);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCritterFx" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCritter), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreVelocity), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreGait), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Rotation), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreCritterFx
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCritterLimbs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreLimb), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Rotation), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreCritter), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreCritterLimbs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCorpseChar" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Emissive), .src.id = EcsUp, .trav = EcsIsA, .inout = EcsIn },
            { .id = ecs_id(OreCorpse), .src.id = EcsUp, .trav = EcsChildOf,
              .inout = EcsInOutNone },
            { .id = playing, .src.id = state }
        },
        .callback = OreCorpseChar
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCorpses" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCorpse), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .run = OreCorpses
    });

    ecs_function(world, {
        .name = "corpse",
        .parent = module,
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { .name = "prefab", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "yaw", .type = ecs_id(ecs_f32_t) }
        },
        .callback = ore_corpse_function
    });
}
