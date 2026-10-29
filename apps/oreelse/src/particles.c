/**
 * @file particles.c
 * @brief The particle effects the game fires, the oreBurst, oreExplosion and oreCombust of the
 * original's game.c on top of box_particle_burst_at, the flash of their lights and the script
 * functions oreelse.explosion() and oreelse.combust(). The templates are in etc/particles.
 */

#include "game.h"
#include <math.h>

/** Lights of an instance the flash of a play is written to at most */
#define ORE_BLAST_LIGHTS (4)

/** Get the world to play an effect in: the stage of the main thread while the world is read
 * only, as box_particle_burst_at uses, so the writes after the burst follow its own.
 *
 * @param world The world or a stage
 * @return The world or the stage
 */
static ecs_world_t* ore_particles_world(
    ecs_world_t *world)
{
    return ecs_stage_is_readonly(world) ? ecs_get_stage(world, 0) : world;
}

ecs_entity_t ore_burst(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float scale)
{
    ecs_world_t *w = ore_particles_world(world);
    ecs_entity_t result = effect ? box_particle_burst_at(w, effect, position, scale) : 0;
    if (result && ecs_has(w, result, Rotation)) {
        ecs_set(w, result, Rotation, {0, 0, 0});
    }

    return result;
}

ecs_entity_t ore_burst_aimed(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float aim)
{
    ecs_world_t *w = ore_particles_world(world);
    ecs_entity_t result = box_particle_burst_at(w, effect, position, 1.0f);
    if (result) {
        ecs_set(w, result, Rotation, {0, aim, 0});
    }

    return result;
}

ecs_entity_t ore_explosion(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float scale)
{
    ecs_world_t *w = ore_particles_world(world);
    const ecs_world_t *real = ecs_get_world(w);
    float peak = ORE_CFG(real, "explosionPeak");
    float range = ORE_CFG(real, "explosionRange");
    ecs_entity_t result;
    scale = scale > 0 ? scale : 1.0f;

    result = ore_burst(w, effect, position, scale);
    if (result) {
        ecs_set(w, result, OreBlastFlash, { .peak = peak * scale, .range = range * scale });
    }

    return result;
}

ecs_entity_t ore_combust(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float radius)
{
    ecs_world_t *w = ore_particles_world(world);
    const ecs_world_t *real = ecs_get_world(w);
    float r = glm_clamp(radius,
        ORE_CFG(real, "combustRadiusMin"), ORE_CFG(real, "combustRadiusMax"));
    float s = sqrtf(r);
    vec3 at = { position[0], position[1] + ORE_CFG(real, "combustLift") * r, position[2] };
    ecs_entity_t result = ore_burst(w, effect, at, s);
    if (result) {
        ecs_set(w, result, Scale, {r, r, r});
        ecs_set(w, result, OreBlastFlash, {
            .peak = ORE_CFG(real, "combustPeak") * s,
            .range = ORE_CFG(real, "combustRange") + ORE_CFG(real, "combustRangeRadius") * r
        });
    }

    return result;
}

/** Give the lights of an instance, the children of the root with an EffectFlash and a
 * PointLight, the peak and the range of the flash of its play.
 *
 * A burst in a system sets OreBlastFlash after the commands that make the instance, so the
 * children exist when it merges. The play already lit a light at the peak of its EffectFlash,
 * the previous play's, so the light moves to this play's peak by the same ratio.
 *
 * @param it Iterator over the roots whose OreBlastFlash was set
 */
static void OreBlastFlashApply(
    ecs_iter_t *it)
{
    const OreBlastFlash *flash = ecs_field(it, OreBlastFlash, 0);
    int32_t i, j, count;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t lights[ORE_BLAST_LIGHTS];
        ecs_iter_t children = ecs_children(it->world, it->entities[i]);
        count = 0;
        while (ecs_children_next(&children)) {
            for (j = 0; j < children.count; j ++) {
                ecs_entity_t c = children.entities[j];
                if (count < ORE_BLAST_LIGHTS && ecs_has(it->world, c, EffectFlash) &&
                    ecs_has(it->world, c, PointLight))
                {
                    lights[count ++] = c;
                }
            }
        }

        for (j = 0; j < count; j ++) {
            EffectFlash f = *ecs_get(it->world, lights[j], EffectFlash);
            PointLight light = *ecs_get(it->world, lights[j], PointLight);
            bool changed = light.range != flash[i].range;
            if (f.intensity != flash[i].peak) {
                if (f.intensity > 0) {
                    light.intensity *= flash[i].peak / f.intensity;
                    changed = true;
                }

                f.intensity = flash[i].peak;
                ecs_set_ptr(it->world, lights[j], EffectFlash, &f);
            }

            if (changed) {
                light.range = flash[i].range;
                ecs_set_ptr(it->world, lights[j], PointLight, &light);
            }
        }
    }
}

/** The oreelse.explosion(effect, position, scale) script function.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The template, the position and the scale
 * @param result Receives the root of the instance, 0 for none
 */
static void ore_explosion_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(ecs_entity_t*)result->ptr = ore_explosion(ctx->world, *(ecs_entity_t*)argv[0].ptr,
        argv[1].ptr, *(float*)argv[2].ptr);
}

/** The oreelse.combust(effect, position, radius) script function.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The template, the position and the body radius
 * @param result Receives the root of the instance, 0 for none
 */
static void ore_combust_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(ecs_entity_t*)result->ptr = ore_combust(ctx->world, *(ecs_entity_t*)argv[0].ptr,
        argv[1].ptr, *(float*)argv[2].ptr);
}

void ore_particles_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreBlastFlashApply" }),
        .query.terms = {{ .id = ecs_id(OreBlastFlash), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = OreBlastFlashApply
    });

    ecs_function(world, {
        .name = "explosion",
        .parent = module,
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { .name = "effect", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "scale", .type = ecs_id(ecs_f32_t) }
        },
        .callback = ore_explosion_function
    });

    ecs_function(world, {
        .name = "combust",
        .parent = module,
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { .name = "effect", .type = ecs_id(ecs_entity_t) },
            { .name = "position", .type = ecs_id(vec3) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        },
        .callback = ore_combust_function
    });
}
