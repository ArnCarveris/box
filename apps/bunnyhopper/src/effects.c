/**
 * @file effects.c
 * @brief Plays the effect templates of BunnyGame and moves their particles with the world.
 *
 * Particles live in world space while the world scrolls past the bunny, so every play shifts
 * the velocities of its layers by the world speed, and the wind of BunnyGame.effect_wind, which
 * the drag of a layer pulls its particles toward, follows the speed.
 */

#include "game.h"
#include <math.h>

/** Degrees of a radian */
#define BUNNY_DEG (57.29577951f)

void bunny_effect_queue(
    ecs_world_t *world,
    ecs_entity_t effect,
    float x,
    float y)
{
    BunnyRuntime *rt = ecs_singleton_get_mut(world, BunnyRuntime);
    if (!effect || rt->play_count >= BUNNY_PLAYS_MAX) {
        goto done;
    }

    rt->plays[rt->play_count ++] = (bunny_play_t){
        .effect = effect,
        .position = { x, y, 0.0f }
    };
done:
    return;
}

void bunny_effect_shift(
    const ParticleEmitter *src,
    float speed,
    ParticleEmitter *dst)
{
    vec3 d, mean, extent;
    float len, along = 0.0f, across = 0.0f;
    int32_t i;
    *dst = *src;
    glm_vec3_copy(ECS_CONST_CAST(float*, src->direction), d);
    if (speed <= 0.0f || glm_vec3_norm2(d) <= 0.0f) {
        goto done;
    }

    float spread = glm_clamp(src->spread, 0.0f, 180.0f) / BUNNY_DEG;
    float c = cosf(spread), s = sinf(fminf(spread, (float)GLM_PI_2));
    float mid = (src->speed.min + src->speed.max) * 0.5f;
    float top = fmaxf(src->speed.max, src->speed.min);
    float half = fabsf(src->speed.max - src->speed.min) * 0.5f + mid * (1.0f - c) * 0.5f;
    glm_vec3_normalize(d);
    glm_vec3_scale(d, mid * (1.0f + c) * 0.5f, mean);
    for (i = 0; i < 3; i ++) {
        extent[i] = half * fabsf(d[i]) + top * s * sqrtf(fmaxf(1.0f - d[i] * d[i], 0.0f));
    }

    mean[0] -= speed;
    len = glm_vec3_norm(mean);
    if (len <= 1e-4f) {
        goto done;
    }

    glm_vec3_scale(mean, 1.0f / len, d);
    for (i = 0; i < 3; i ++) {
        along += extent[i] * fabsf(d[i]);
        across = fmaxf(across, extent[i] * sqrtf(fmaxf(1.0f - d[i] * d[i], 0.0f)));
    }

    glm_vec3_copy(d, dst->direction);
    dst->spread = atan2f(across, len) * BUNNY_DEG;
    dst->speed.min = fmaxf(len - along, 0.0f);
    dst->speed.max = len + along;
done:
    return;
}

/** Give the layers of an instance the layers of its template, shifted by the world speed and
 * with seeds offset by the play, walking the instance and the template side by side.
 *
 * @param world The world
 * @param inst An entity of the instance
 * @param tmpl The entity of the template it was made from
 * @param speed The world speed
 * @param seed Offset of the seeds
 */
static void bunny_effect_layers(
    ecs_world_t *world,
    ecs_entity_t inst,
    ecs_entity_t tmpl,
    float speed,
    uint32_t seed)
{
    const ParticleEmitter *src = ecs_get(world, tmpl, ParticleEmitter);
    if (src && ecs_owns(world, inst, ParticleEmitter)) {
        ParticleEmitter e;
        bunny_effect_shift(src, speed, &e);
        e.seed = src->seed + seed;
        ecs_set_ptr(world, inst, ParticleEmitter, &e);
    }

    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    ecs_iter_t it = ecs_children(world, inst);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            *ecs_vec_append_t(NULL, &children, ecs_entity_t) = it.entities[i];
        }
    }

    for (int32_t i = 0; i < ecs_vec_count(&children); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&children, ecs_entity_t, i);
        const char *name = ecs_get_name(world, e);
        ecs_entity_t child = name ? ecs_lookup_child(world, tmpl, name) : 0;
        if (child) {
            bunny_effect_layers(world, e, child, speed, seed);
        }
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

/** Keep the wind of the effects at the world speed and play the effects the systems of the
 * frame queued, shifted by the world speed. Once the game is over the world stands still.
 *
 * Runs immediate, so a new instance has its layers when they are shifted.
 *
 * @param it Iterator of the system, without entities
 */
static void BunnyEffects(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    BunnyRuntime *rt = ecs_singleton_get_mut(world, BunnyRuntime);
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
    float speed = g && bunny_state_is(world, BunnyGameStatePlaying) ? g->speed : 0.0f;
    int32_t i, count = rt->play_count;
    rt->play_count = 0;

    const ParticleForces *f = g && g->effect_wind
        ? ecs_get(world, g->effect_wind, ParticleForces)
        : NULL;
    if (f && (f->wind[0] != -speed || f->wind[1] != 0.0f || f->wind[2] != 0.0f)) {
        ParticleForces wind = *f;
        wind.wind[0] = -speed;
        wind.wind[1] = 0.0f;
        wind.wind[2] = 0.0f;
        ecs_set_ptr(world, g->effect_wind, ParticleForces, &wind);
    }

    for (i = 0; i < count; i ++) {
        rt = ecs_singleton_get_mut(world, BunnyRuntime);
        bunny_play_t play = rt->plays[i];
        uint32_t index = rt->play_index ++;
        ecs_entity_t root = box_particle_burst_at(world, play.effect, play.position, 0.0f);
        if (root) {
            bunny_effect_layers(world, root, play.effect, speed, index);
        }
    }
}

void bunny_effects_import(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyEffects" }),
        .phase = EcsOnUpdate,
        .callback = BunnyEffects,
        .immediate = true
    });
}
