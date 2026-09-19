/**
 * @file particle/effect.c
 * @brief One shot effects: a pool of instances per template and the play that reuses it.
 *
 * An effect is an ordinary template, so what it does is authored as components
 * and not as code. Playing it takes a free instance of that template from its
 * pool, or makes one when the pool is empty, places it, scales it and plays it
 * from the top. The engine ends the emission once the bursts of the template
 * have all gone off and disables the instance a lifetime later, which is the
 * moment its last particle has died; the instance stays for the next play, so a
 * stream of hits costs the entities of one pool instead of one entity per hit.
 */

#include "particle_internal.h"

/** Seconds a flash fades over when the template of the effect names none */
#define BOX_EFFECT_FLASH_SECONDS (0.2f)

/** Seconds into the playback the bursts of a template have all gone off.
 *
 * @param world The world
 * @param effect The template
 * @return The time the emission of the effect ends at
 */
static double box_effect_emit_end(
    ecs_world_t *world,
    ecs_entity_t effect)
{
    const ParticleBurst *burst = ecs_get(world, effect, ParticleBurst);
    double result = 0;
    if (burst && burst->count > 0 && burst->at > 0) {
        double times = burst->repeat < 0 ? 1.0 : (double)burst->repeat + 1.0;
        result = (double)burst->at * times;
    }

    return result;
}

/** Scale the sizes, the speeds and the spawn volume of an emitter.
 *
 * @param e The emitter, written in place
 * @param scale Size of the play
 */
static void box_effect_scale(
    ParticleEmitter *e,
    float scale)
{
    e->radius *= scale;
    e->extents = vec3_scale(e->extents, scale);
    e->speed.min *= scale;
    e->speed.max *= scale;
    e->size.min *= scale;
    e->size.max *= scale;
    e->acceleration = vec3_scale(e->acceleration, scale);
}

/** Find a free instance of a template in the pool of that template.
 *
 * The query holds the instances that are disabled, which is what an instance
 * whose play is over becomes, so the pool of a template is the part of it that
 * names that template.
 *
 * @param world The world
 * @param state The registry of the module
 * @param effect The template
 * @return The instance, or 0 when every instance of the pool is playing
 */
static ecs_entity_t box_effect_free(
    ecs_world_t *world,
    const ParticleState *state,
    ecs_entity_t effect)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_query_iter(world, state->effects);
    while (ecs_query_next(&it)) {
        const ParticleEffect *play = ecs_field(&it, ParticleEffect, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!result && play[i].effect == effect) {
                result = it.entities[i];
            }
        }
    }

    return result;
}

/** Give an instance the flash of its template, at the intensity it starts at.
 *
 * @param world The world
 * @param instance The instance
 * @param effect The template
 */
static void box_effect_flash(
    ecs_world_t *world,
    ecs_entity_t instance,
    ecs_entity_t effect)
{
    const EffectFlash *src = ecs_get(world, effect, EffectFlash);
    const PointLight *lamp = ecs_get(world, effect, PointLight);
    if (!src) {
        goto done;
    }

    EffectFlash *flash = ecs_ensure(world, instance, EffectFlash);
    flash->seconds = src->seconds;
    flash->intensity = src->intensity > 0
        ? src->intensity
        : (lamp ? lamp->intensity : 0);
    ecs_modified(world, instance, EffectFlash);

    PointLight *light = ecs_ensure(world, instance, PointLight);
    light->intensity = flash->intensity;
    light->range = lamp ? lamp->range : light->range;
    ecs_modified(world, instance, PointLight);
done:
    return;
}

/** Place an instance, scale it and play it from the top.
 *
 * @param world The world
 * @param instance The instance
 * @param effect The template
 * @param position Where the effect plays
 * @param scale Size of the play
 */
static void box_effect_play(
    ecs_world_t *world,
    ecs_entity_t instance,
    ecs_entity_t effect,
    vec3 position,
    float scale)
{
    ParticleEmitter emitter = *ecs_get(world, effect, ParticleEmitter);
    box_effect_scale(&emitter, scale);
    ecs_set_id(world, instance, ecs_id(ParticleEmitter),
        sizeof(ParticleEmitter), &emitter);
    ecs_add(world, instance, ParticleEmitterImpl);
    ecs_add(world, instance, WorldTransform);
    ecs_set(world, instance, Position, {position.x, position.y, position.z});
    box_effect_flash(world, instance, effect);

    ParticlePlayback *playback = ecs_ensure(world, instance, ParticlePlayback);
    playback->state = ParticlePlaying;
    playback->reset = true;
    ecs_modified(world, instance, ParticlePlayback);

    const EffectFlash *flash = ecs_get(world, instance, EffectFlash);
    double fade = flash ? (double)flash->seconds : 0.0;
    ParticleEffect *play = ecs_ensure(world, instance, ParticleEffect);
    play->effect = effect;
    play->scale = scale;
    play->time = 0;
    play->emit = box_effect_emit_end(world, effect);
    play->retire = play->emit +
        ((double)emitter.lifetime.max > fade ? (double)emitter.lifetime.max : fade);
    ecs_modified(world, instance, ParticleEffect);
    ecs_enable(world, instance, true);
}

ecs_entity_t box_particle_burst_at(
    ecs_world_t *world,
    ecs_entity_t effect,
    vec3 position,
    float scale)
{
    ParticleState *state = ecs_id(ParticleState)
        ? ecs_singleton_ensure(world, ParticleState)
        : NULL;
    ecs_entity_t result = 0;
    if (!state || !effect || !ecs_is_alive(world, effect) ||
        !ecs_has(world, effect, ParticleEmitter))
    {
        goto done;
    }

    result = box_effect_free(world, state, effect);
    if (!result) {
        result = ecs_has_id(world, effect, EcsPrefab)
            ? ecs_new_w_pair(world, EcsIsA, effect)
            : ecs_new_w_id(world, effect);
    }

    box_effect_play(world, result, effect, position, scale > 0 ? scale : 1.0f);
done:
    return result;
}

/** Fade the flash of one instance over the seconds its template names.
 *
 * @param flash The flash of the instance
 * @param light The light of the instance, written
 * @param time Seconds into the play
 */
static void box_effect_fade(
    const EffectFlash *flash,
    PointLight *light,
    double time)
{
    float seconds = flash->seconds > 0
        ? flash->seconds
        : BOX_EFFECT_FLASH_SECONDS;
    float left = 1.0f - (float)(time / (double)seconds);
    light->intensity = left > 0 ? flash->intensity * left : 0;
}

void BoxParticleEffects(
    ecs_iter_t *it)
{
    ParticleEffect *play = ecs_field(it, ParticleEffect, 0);
    ParticlePlayback *playback = ecs_field(it, ParticlePlayback, 1);
    PointLight *light = ecs_field(it, PointLight, 2);
    const EffectFlash *flash = ecs_field(it, EffectFlash, 3);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (light && flash) {
            box_effect_fade(&flash[i], &light[i], play[i].time);
        }

        if (play[i].time > play[i].emit &&
            playback[i].state == ParticlePlaying)
        {
            playback[i].state = ParticleStopped;
        }

        if (play[i].time >= play[i].retire) {
            playback[i].state = ParticleStopped;
            playback[i].reset = true;
            ecs_enable(it->stage, it->entities[i], false);
        }

        play[i].time += (double)it->delta_time;
    }
}
