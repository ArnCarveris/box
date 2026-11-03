/**
 * @file particle/particle.c
 * @brief Registers the box.particle components and hands the emitters to the render extract.
 */

#define BOX_PARTICLE_IMPL
#include <box.h>
#include "../render/render.h"
#include "effect.h"
#include "curves.h"

/** Construct ParticleEmitter values: a white stream of 1000 particles that rise at 1 unit per
 * second for 1 to 2 seconds.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_particle_emitter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ParticleEmitter *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ParticleEmitter){
            .max = 1000,
            .direction = {0, 1, 0},
            .speed = { 1.0f, 1.0f },
            .lifetime = { 1.0f, 2.0f },
            .size = { 0.1f, 0.1f },
            .opacity = { 1.0f, 1.0f },
            .color = {255, 255, 255}
        };
    }
}

/** Construct ParticleForces values: earth gravity, no wind, drag, turbulence or drift.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_particle_forces_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ParticleForces *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ParticleForces){
            .gravity = {0, -9.81f, 0},
            .turbulence_scale = 1.0f
        };
    }
}

/** Construct ParticleCollision values: a ground plane at height 0 that keeps half the speed
 * into it and a fifth of the speed along it.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_particle_collision_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ParticleCollision *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ParticleCollision){
            .ground = true,
            .bounce = 0.5f,
            .friction = 0.2f
        };
    }
}

/** Construct ParticleLight values: a light at intensity 1 while the emitter plays, with a curve
 * without keys, that goes dark at once when it stops.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_particle_light_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ParticleLight *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ParticleLight){ .intensity = 1.0f };
    }
}

void BoxParticleImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxParticle);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxCurve);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, EmitterShape);
    ECS_META_COMPONENT(world, ParticleRange);
    ECS_META_COMPONENT(world, ParticleForces);
    ecs_set_hooks(world, ParticleForces, { .ctor = box_particle_forces_ctor });
    ECS_META_COMPONENT(world, ParticleCollision);
    ecs_set_hooks(world, ParticleCollision, { .ctor = box_particle_collision_ctor });
    ECS_META_COMPONENT(world, ParticleEmitter);
    ecs_set_hooks(world, ParticleEmitter, { .ctor = box_particle_emitter_ctor });
    ECS_META_COMPONENT(world, PlaybackState);
    ECS_META_COMPONENT(world, ParticlePlayback);
    ECS_META_COMPONENT(world, ParticleBurst);
    ECS_META_COMPONENT(world, ParticleCurve);
    box_owned_hooks(world, ParticleCurve,
        BOX_OWNED_VEC(ParticleCurve, keys, CurveKey),
        BOX_OWNED_VEC(ParticleCurve, max_keys, CurveKey));
    ECS_META_COMPONENT(world, ParticleLight);
    box_owned_hooks(world, ParticleLight,
        BOX_OWNED_CURVE(ParticleLight, curve),
        BOX_OWNED_VEC(ParticleLight, curve.max_keys, CurveKey));
    ecs_set_hooks(world, ParticleLight, { .ctor = box_particle_light_ctor });
    ECS_META_COMPONENT(world, ParticleTimeline);
    box_owned_hooks(world, ParticleTimeline,
        BOX_OWNED_CURVE(ParticleTimeline, rate),
        BOX_OWNED_CURVE(ParticleTimeline, speed));
    ECS_META_COMPONENT(world, EffectFlash);
    ECS_META_COMPONENT(world, ParticleGradientKey);
    ECS_META_COMPONENT(world, ParticleGradient);
    box_owned_hooks(world, ParticleGradient,
        BOX_OWNED_VEC(ParticleGradient, keys, ParticleGradientKey),
        BOX_OWNED_VEC(ParticleGradient, max_keys, ParticleGradientKey));
    ECS_META_COMPONENT(world, ParticleCurves);
    box_owned_hooks(world, ParticleCurves,
        BOX_OWNED_CURVE(ParticleCurves, size),
        BOX_OWNED_VEC(ParticleCurves, size.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleCurves, opacity),
        BOX_OWNED_VEC(ParticleCurves, opacity.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleCurves, rotation),
        BOX_OWNED_VEC(ParticleCurves, rotation.max_keys, CurveKey),
        BOX_OWNED_VEC(ParticleCurves, color.keys, ParticleGradientKey),
        BOX_OWNED_VEC(ParticleCurves, color.max_keys, ParticleGradientKey),
        BOX_OWNED_CURVE(ParticleCurves, spin),
        BOX_OWNED_VEC(ParticleCurves, spin.max_keys, CurveKey));
    ECS_META_COMPONENT(world, ParticleMotion);
    box_owned_hooks(world, ParticleMotion,
        BOX_OWNED_CURVE(ParticleMotion, velocity_x),
        BOX_OWNED_VEC(ParticleMotion, velocity_x.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, velocity_y),
        BOX_OWNED_VEC(ParticleMotion, velocity_y.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, velocity_z),
        BOX_OWNED_VEC(ParticleMotion, velocity_z.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, force_x),
        BOX_OWNED_VEC(ParticleMotion, force_x.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, force_y),
        BOX_OWNED_VEC(ParticleMotion, force_y.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, force_z),
        BOX_OWNED_VEC(ParticleMotion, force_z.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, speed),
        BOX_OWNED_VEC(ParticleMotion, speed.max_keys, CurveKey),
        BOX_OWNED_CURVE(ParticleMotion, drag),
        BOX_OWNED_VEC(ParticleMotion, drag.max_keys, CurveKey));
    box_particle_curves_import(world);
    ECS_META_COMPONENT(world, ParticleShape);
    ECS_META_COMPONENT(world, ParticleBlend);
    ECS_META_COMPONENT(world, ParticleAppearance);
    ECS_META_COMPONENT(world, ParticleStats);
    ecs_singleton_set(world, ParticleStats, {0});
    ECS_META_COMPONENT(world, EffectOverflow);
    ECS_META_COMPONENT(world, ParticleSettings);
    ecs_singleton_set(world, ParticleSettings, {0});
    ECS_META_COMPONENT(world, ParticleEffect);

    box_render_particles_import(world);
    box_particle_effects_import(world);
}
