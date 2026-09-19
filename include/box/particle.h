/**
 * @file box/particle.h
 * @brief Particle module: emitters whose particles are spawned and moved on the GPU.
 *
 * A ParticleEmitter describes how many particles live at once, how fast they
 * are spawned, the volume they are spawned in and the state they start with.
 * The particles themselves never reach the ECS: the frame packet carries the
 * parameters of every emitter and the renderer keeps the buffers, spawns into
 * them and integrates them in compute passes.
 *
 * The spawn volume is placed and oriented by the transform of the emitter
 * entity, so moving, rotating or parenting an emitter moves its stream like it
 * moves a mesh. The mesh and spline shapes spawn over the surface of the Mesh3
 * or along the Spline of the source entity.
 *
 * Every random draw comes from a hash of the seed and the number of particles
 * the emitter has spawned, so a fixed timestep produces the same particles on
 * every run and on every device.
 */

#ifndef BOX_PARTICLE_H
#define BOX_PARTICLE_H

#include "types.h"

#ifdef BOX_PARTICLE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Volume a ParticleEmitter draws its spawn points from */
ECS_ENUM(EmitterShape, {
    EmitterPoint = 0,  /**< The origin of the emitter */
    EmitterSphere = 1, /**< Inside the sphere of radius */
    EmitterBox = 2,    /**< Inside the box of extents */
    EmitterDisc = 3,   /**< Inside the disc of radius in the XZ plane */
    EmitterMesh = 4,   /**< Over the surface of the Mesh3 of source */
    EmitterSpline = 5  /**< Along the Spline of source */
});

/** Lowest and highest value a particle property is drawn between.
 *
 * A range whose max is not above its min hands out min, which is how a property
 * that should not vary is written.
 */
ECS_STRUCT(ParticleRange, {
    float min; /**< Lowest value of the draw */
    float max; /**< Highest value of the draw */
});

/** Emitter that spawns and integrates particles on the GPU.
 *
 * The constructor fills the defaults of every member, so an emitter authored as
 * `ParticleEmitter: {rate: 400}` keeps a thousand particles, a lifetime between
 * one and two seconds and white sprites of ten centimeters.
 */
ECS_STRUCT(ParticleEmitter, {
    int32_t max;            /**< Particles alive at once, clamped to 4194304 */
    float rate;             /**< Particles spawned per second */
    EmitterShape shape;     /**< Volume the spawn point is drawn from */
    float radius;           /**< Radius of the sphere and disc shapes */
    vec3 extents;           /**< Half extents of the box shape */
    ecs_entity_t source;    /**< Mesh3 or Spline entity of the mesh and spline shapes */
    vec3 direction;         /**< Direction particles leave in, in emitter space */
    float spread;           /**< Half angle in degrees of the cone around that direction */
    ParticleRange speed;    /**< Initial speed in world units per second */
    ParticleRange lifetime; /**< Seconds a particle lives */
    ParticleRange size;     /**< Initial size in world units */
    ParticleRange rotation; /**< Initial rotation of the sprite in degrees */
    ParticleRange opacity;  /**< Initial opacity */
    vec3 acceleration;      /**< Constant acceleration in world units per second squared */
    rgb color;              /**< Color a particle starts with */
    uint32_t seed;          /**< Seed of the random draw, the same seed gives the same frames */
});

/** Register the box.particle module.
 *
 * @param world The world
 */
void BoxParticleImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
