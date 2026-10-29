/**
 * @file building_fx.c
 * @brief The buildings' animation and lights, the OreSpinners, OrePistons, OreShakes, OreVents,
 * OreWorkEmitters and OreWorkLights of the original's buildings.c.
 *
 * A part moves at the work rate of its building (the nearest ancestor with OreBuilding), read
 * from the game's state (ore_work_rate), and stops where it is at 0. The parts of a ghost or of
 * a model that is not placed have no building and stand still.
 *
 * The systems run only while playing; outside play the work emitters are paused. Emitters and
 * lights are only written when their values change.
 */

#include "game.h"
#include <math.h>

/** What the work rate of a building depends on, read once per system run */
typedef struct ore_work_t {
    const OrePowerState *power; /**< The power of the base */
    const OrePowerAlloc *alloc; /**< The levels of the categories */
    float step;                 /**< Multiplier of a level, cfg.powerStep */
} ore_work_t;

/** Read what the work rate depends on from the fields of a system.
 *
 * @param it The iterator
 * @param power Index of the OrePowerState field
 * @param alloc Index of the OrePowerAlloc field
 * @return The inputs of the work rate
 */
static ore_work_t ore_work_get(
    ecs_iter_t *it,
    int8_t power,
    int8_t alloc)
{
    ore_work_t result = {
        .power = ecs_field(it, OrePowerState, power),
        .alloc = ecs_field(it, OrePowerAlloc, alloc),
        .step = ORE_CFG(it->world, "powerStep")
    };

    return result;
}

/** Find the building a part belongs to: the nearest ancestor with OreBuilding of its own. The
 * parts of a ghost or of a model that is not placed have none.
 *
 * @param world The world
 * @param e The part
 * @return The building, 0 for none
 */
static ecs_entity_t ore_work_owner(
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

/** Find the building of a part once: a found owner is kept in the part's component.
 *
 * @param world The world
 * @param e The part
 * @param owner The owner of the part's component, 0 until found
 * @return The building, 0 for none
 */
static ecs_entity_t ore_work_find(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t *owner)
{
    if (!*owner) {
        *owner = ore_work_owner(world, e);
    }

    return *owner && ecs_is_alive(world, *owner) ? *owner : 0;
}

/** Get the work rate of a building, the original's oreWorkRate.
 *
 * @param world The world
 * @param work The inputs of the work rate
 * @param owner The building, 0 for none
 * @return 0 without a building, in a blackout for a consumer, for a drill that does not mine
 * and for an idle crafter, else the multiplier of its category, 1 without one
 */
static float ore_work_rate(
    const ecs_world_t *world,
    const ore_work_t *work,
    ecs_entity_t owner)
{
    const OreDrill *drill;
    const OreCrafter *crafter;
    const OrePowerCategory *cat;
    int32_t kind;
    float result = 0;
    if (!owner) {
        goto done;
    }

    if (work->power->blackout && ecs_has(world, owner, OrePowerConsumer)) {
        goto done;
    }

    drill = ecs_get(world, owner, OreDrill);
    if (drill && !drill->running) {
        goto done;
    }

    crafter = ecs_get(world, owner, OreCrafter);
    if (crafter && !(crafter->item && crafter->total > 0)) {
        goto done;
    }

    cat = ecs_get(world, owner, OrePowerCategory);
    kind = cat ? (int32_t)cat->kind : -1;
    result = kind >= 0 && kind < ORE_POWER_KINDS ?
        (float)work->alloc->level[kind] * work->step : 1.0f;
done:
    return result;
}

/** Get the point of a part relative to the ground under the map: the positions of the part and
 * its ancestors up to its building summed, as the buildings stand unturned.
 *
 * @param world The world
 * @param part The part
 * @param owner The building
 * @param out Receives the point
 */
static void ore_part_point(
    const ecs_world_t *world,
    ecs_entity_t part,
    ecs_entity_t owner,
    vec3 out)
{
    const Position *p;
    ecs_entity_t e = part;
    glm_vec3_zero(out);
    while (e) {
        p = ecs_get(world, e, Position);
        if (p) {
            out[0] += p->x;
            out[1] += p->y;
            out[2] += p->z;
        }

        e = e == owner ? 0 : ecs_get_parent(world, e);
    }
}

/** Get the stroke of a piston at its phase.
 *
 * @param phase The phase in radians
 * @return 0 at rest to 1 at the bottom of the stroke
 */
static float ore_stroke(
    float phase)
{
    return 0.5f - 0.5f * cosf(phase);
}

/** Spin the spinners about their y by speed times the work rate of their building a second.
 *
 * @param it Iterator over the spinners, with OrePowerState and OrePowerAlloc, while playing
 */
static void OreSpinners(
    ecs_iter_t *it)
{
    OreSpinner *spinner = ecs_field(it, OreSpinner, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    ore_work_t work = ore_work_get(it, 2, 3);
    ecs_entity_t owner;
    float mul;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        owner = ore_work_find(it->world, it->entities[i], &spinner[i].owner);
        mul = ore_work_rate(it->world, &work, owner);
        if (mul > 0) {
            r[i].y += spinner[i].speed * it->delta_time * mul;
        }
    }
}

/** Stroke the pistons: the phase advances speed times the work rate of the building a second
 * and the part sits at its rest minus the travel times the stroke.
 *
 * At the bottom of every stroke the piston's burst and spark play impact above it (the
 * factory's PressPuff and PressSparks where the ram lands).
 *
 * @param it Iterator over the pistons, with OrePowerState and OrePowerAlloc, while playing
 */
static void OrePistons(
    ecs_iter_t *it)
{
    OrePiston *piston = ecs_field(it, OrePiston, 0);
    Position *p = ecs_field(it, Position, 1);
    ore_work_t work = ore_work_get(it, 2, 3);
    float span = 2.0f * GLM_PIf, mul, was, at;
    ecs_entity_t owner;
    int32_t i;
    vec3 point;
    for (i = 0; i < it->count; i ++) {
        owner = ore_work_find(it->world, it->entities[i], &piston[i].owner);
        mul = ore_work_rate(it->world, &work, owner);
        if (mul <= 0) {
            continue;
        }

        was = piston[i].phase;
        piston[i].phase += piston[i].speed * it->delta_time * mul;
        at = piston[i].rest - piston[i].travel * ore_stroke(piston[i].phase);
        if (piston[i].slide) {
            p[i].z = at;
        } else {
            p[i].y = at;
        }

        if ((piston[i].burst || piston[i].spark) &&
            floorf((was - GLM_PIf) / span) != floorf((piston[i].phase - GLM_PIf) / span))
        {
            ore_part_point(it->world, it->entities[i], owner, point);
            point[1] += piston[i].impact;
            ore_burst(it->stage, piston[i].burst, point, 1.0f);
            ore_burst(it->stage, piston[i].spark, point, 1.0f);
        }
    }
}

/** Shake the shakes: while the building works the part bobs about its rest along y, its phase
 * advancing frequency * 2 PI times the work rate a second; otherwise it sits at its rest.
 *
 * @param it Iterator over the shakes, with OrePowerState and OrePowerAlloc, while playing
 */
static void OreShakes(
    ecs_iter_t *it)
{
    OreShake *shake = ecs_field(it, OreShake, 0);
    Position *p = ecs_field(it, Position, 1);
    ore_work_t work = ore_work_get(it, 2, 3);
    ecs_entity_t owner;
    float mul, wobble;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        owner = ore_work_find(it->world, it->entities[i], &shake[i].owner);
        if (!owner) {
            continue;
        }

        if (!shake[i].ready) {
            shake[i].rest = p[i].y;
            shake[i].ready = true;
        }

        mul = ore_work_rate(it->world, &work, owner);
        if (mul <= 0) {
            p[i].y = shake[i].rest;
        } else {
            shake[i].phase += shake[i].frequency * 2.0f * GLM_PIf * it->delta_time * mul;
            wobble = 0.7f + 0.3f * sinf(shake[i].phase * 0.37f);
            p[i].y = shake[i].rest + shake[i].amplitude * wobble * sinf(shake[i].phase);
        }
    }
}

/** Puff the vents: the timer counts down by the work rate of the building and at 0 the vent's
 * burst plays at it and the timer starts again from its interval.
 *
 * @param it Iterator over the vents, with OrePowerState and OrePowerAlloc, while playing
 */
static void OreVents(
    ecs_iter_t *it)
{
    OreVent *vent = ecs_field(it, OreVent, 0);
    ore_work_t work = ore_work_get(it, 1, 2);
    ecs_entity_t owner;
    float mul;
    int32_t i;
    vec3 point;
    for (i = 0; i < it->count; i ++) {
        owner = ore_work_find(it->world, it->entities[i], &vent[i].owner);
        mul = ore_work_rate(it->world, &work, owner);
        if (mul <= 0) {
            continue;
        }

        vent[i].timer -= it->delta_time * mul;
        if (vent[i].timer <= 0) {
            vent[i].timer = vent[i].interval > 0 ? vent[i].interval : 0.5f;
            ore_part_point(it->world, it->entities[i], owner, point);
            ore_burst(it->stage, vent[i].burst, point, 1.0f);
        }
    }
}

/** Play the work emitters while their building works and stop them otherwise; the playback is
 * only written when it changes.
 *
 * @param it Iterator over the work emitters, with OrePowerState and OrePowerAlloc, while playing
 */
static void OreWorkEmitters(
    ecs_iter_t *it)
{
    OreWorkEmitter *emitter = ecs_field(it, OreWorkEmitter, 0);
    ore_work_t work = ore_work_get(it, 2, 3);
    ecs_entity_t owner;
    float mul;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        owner = ore_work_find(it->world, it->entities[i], &emitter[i].owner);
        if (owner) {
            mul = ore_work_rate(it->world, &work, owner);
            ore_playback_set(it->world, it->stage, it->entities[i],
                mul > 0 ? ParticlePlaying : ParticleStopped, false);
        }
    }
}

/** Pause the work emitters that play outside of play, so they stand still with the game;
 * OreWorkEmitters plays or stops them again when the game resumes.
 *
 * @param it Iterator over the work emitters, while not playing
 */
static void OreWorkEmittersHold(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ore_playback_set(it->world, it->stage, it->entities[i], ParticlePaused, true);
    }
}

/** Find the first piston among the descendants of an entity, breadth first per level as the
 * original's orePistonIn.
 *
 * @param world The world
 * @param e The entity, a building
 * @return The part with OrePiston, 0 for none
 */
static ecs_entity_t ore_piston_in(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, e);
    int32_t i;
    while (!result && ecs_children_next(&it)) {
        for (i = 0; i < it.count && !result; i ++) {
            if (ecs_has(world, it.entities[i], OrePiston)) {
                result = it.entities[i];
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
        goto done;
    }

    it = ecs_children(world, e);
    while (!result && ecs_children_next(&it)) {
        for (i = 0; i < it.count && !result; i ++) {
            result = ore_piston_in(world, it.entities[i]);
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

done:
    return result;
}

/** Get the intensity of the point light of a work light at its level: light * level, the share
 * pulse of it following the stroke of the building's piston.
 *
 * @param world The world
 * @param light The work light
 * @param owner The building
 * @return The intensity
 */
static float ore_work_glow(
    const ecs_world_t *world,
    OreWorkLight *light,
    ecs_entity_t owner)
{
    const OrePiston *piston;
    float result = light->light * light->level;
    if (result > 0 && light->pulse > 0) {
        if (!light->piston) {
            light->piston = ore_piston_in(world, owner);
        }

        piston = light->piston ? ecs_get(world, light->piston, OrePiston) : NULL;
        if (piston) {
            result *= 1.0f - light->pulse + light->pulse * ore_stroke(piston->phase);
        }
    }

    return result;
}

/** Light the work lights with the work of their building.
 *
 * A light is lit while its building works (or has power, with power) and hold seconds after,
 * never in a blackout of a consumer. A blinking light blinks blink times the work rate a second
 * of play and snaps to its level; a steady one eases toward it at cfg.workLightRate.
 *
 * Its emission and point light are only written when they change.
 *
 * @param it Iterator over the work lights, with OreClock, OrePowerState and OrePowerAlloc,
 * while playing
 */
static void OreWorkLights(
    ecs_iter_t *it)
{
    OreWorkLight *light = ecs_field(it, OreWorkLight, 0);
    const Emissive *em = ecs_field(it, Emissive, 1);
    const PointLight *pl = ecs_field(it, PointLight, 2);
    const OreClock *clock = ecs_field(it, OreClock, 3);
    ore_work_t work = ore_work_get(it, 4, 5);
    bool em_self = em && ecs_field_is_self(it, 1);
    float alpha = glm_min(it->delta_time * ORE_CFG(it->world, "workLightRate"), 1.0f);
    float mul, phase, want, strength, glow;
    const Emissive *cur;
    ecs_entity_t owner, e;
    bool starved, running, lit;
    PointLight next;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        e = it->entities[i];
        owner = ore_work_find(it->world, e, &light[i].owner);
        if (!owner) {
            continue;
        }

        starved = work.power->blackout && ecs_has(it->world, owner, OrePowerConsumer);
        mul = ore_work_rate(it->world, &work, owner);
        running = mul > 0;
        lit = !starved && (light[i].power || running);
        if (lit) {
            light[i].hot = light[i].hold;
        } else if (light[i].hot > 0) {
            light[i].hot -= it->delta_time;
            lit = !starved && light[i].hot > 0;
        }

        if (lit && running && light[i].blink > 0) {
            phase = clock->elapsed * light[i].blink * mul;
            lit = phase - floorf(phase) < 0.5f;
        }

        want = lit ? 1.0f : 0.0f;
        if (light[i].blink > 0) {
            light[i].level = want;
        } else {
            light[i].level += (want - light[i].level) * alpha;
            if (fabsf(want - light[i].level) < 1e-3f) {
                light[i].level = want;
            }
        }

        strength = light[i].off + (light[i].on - light[i].off) * light[i].level;
        cur = em ? (em_self ? &em[i] : em) : NULL;
        if (cur && cur->strength != strength) {
            ecs_set(it->stage, e, Emissive, { .strength = strength, .color = cur->color });
        }

        if (pl) {
            glow = ore_work_glow(it->world, &light[i], owner);
            if (pl[i].intensity != glow) {
                next = pl[i];
                next.intensity = glow;
                ecs_set_ptr(it->stage, e, PointLight, &next);
            }
        }
    }
}

void ore_building_fx_import(
    ecs_world_t *world)
{
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OreSpinner), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(OreSpinner), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(OrePiston), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(OrePiston), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(OreShake), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(OreShake), EcsWith, DynamicTransform);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreSpinners" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreSpinner), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Rotation), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreSpinners
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePistons" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePiston), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePistons
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreShakes" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreShake), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreShakes
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreVents" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreVent), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreVents
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWorkEmitters" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreWorkEmitter), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(ParticleEmitter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreWorkEmitters
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWorkEmittersHold" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreWorkEmitter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(ParticleEmitter), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OreWorkEmittersHold
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWorkLights" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreWorkLight), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Emissive), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(PointLight), .src.id = EcsSelf, .inout = EcsIn,
              .oper = EcsOptional },
            { .id = ecs_id(OreClock), .src.id = ecs_id(OreClock), .inout = EcsIn },
            { .id = ecs_id(OrePowerState), .src.id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_id(OrePowerAlloc), .src.id = ecs_id(OrePowerAlloc), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreWorkLights
    });
}
