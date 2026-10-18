/**
 * @file box/particle.h
 * @brief Particle emitters whose particles the renderer spawns and moves on the GPU.
 *
 * Particles never reach the ECS: an emitter sends a block of parameters in the render packet
 * and the renderer keeps the particles in pools it owns, so a fixed timestep gives the same
 * particles run after run.
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

/** Particles one emitter keeps alive at once at most; a larger ParticleEmitter.max is clamped */
#define BOX_PARTICLE_MAX (4194304)

/** Volume of an emitter that its particles spawn in, in the space of the emitter entity */
ECS_ENUM(EmitterShape, {
    EmitterPoint = 0,   /**< The origin of the emitter */
    EmitterSphere = 1,  /**< Inside the sphere of radius around the origin */
    EmitterBox = 2,     /**< Inside the box of half size extents around the origin */
    EmitterDisc = 3,    /**< Inside the disc of radius around the origin in the XZ plane */
    EmitterMesh = 4     /**< Over the surface of the Mesh3 or shape of source, area weighted,
                         *   from a table of sampled points refilled when the shape, the
                         *   source or the seed changes */
});

/** A random draw between two values, the same for the same seed and particle */
ECS_STRUCT(ParticleRange, {
    float min;          /**< Smallest value */
    float max;          /**< Largest value; a max not above min hands out min */
});

/** An emitter that spawns rate times the frame delta particles a frame, carrying the fraction.
 * Its WorldTransform places the spawn volume and direction; live particles stay in world space.
 * A full pool spawns nothing, and a change of max empties it.
 *
 * Particles move with semi implicit Euler and die once their age reaches their lifetime; each
 * is a camera facing soft circle of its size, color times opacity, over the opaque image.
 */
ECS_STRUCT(ParticleEmitter, {
    int32_t max;            /**< Particles alive at once, clamped to BOX_PARTICLE_MAX, default
                             *   1000, 0 for none */
    float rate;             /**< Particles spawned per second, default 0 */
    EmitterShape shape;     /**< Spawn volume, default EmitterPoint */
    float radius;           /**< Radius of EmitterSphere and EmitterDisc, default 0 */
    vec3 extents;           /**< Half size of EmitterBox, default {0, 0, 0} */
    ecs_entity_t source;    /**< Entity whose Mesh3 or shape EmitterMesh samples, default 0 for
                             *   the emitter entity itself */
    vec3 direction;         /**< Direction of the initial velocity in emitter space, default
                             *   {0, 1, 0}; {0, 0, 0} spawns particles at rest */
    float spread;           /**< Half angle in degrees of the cone around direction the
                             *   velocity is drawn in, 0 to 180, default 0 */
    ParticleRange speed;    /**< Initial speed in world units per second, default {1, 1} */
    ParticleRange lifetime; /**< Seconds a particle lives, default {1, 2} */
    ParticleRange size;     /**< Diameter of the drawn circle in world units, default
                             *   {0.1, 0.1} */
    ParticleRange rotation; /**< Initial rotation in degrees around the view axis, default
                             *   {0, 0} */
    ParticleRange spin;     /**< Rotation in degrees per second, default {0, 0} */
    ParticleRange opacity;  /**< Opacity the color is drawn with, default {1, 1} */
    vec3 acceleration;      /**< Acceleration in world units per second squared, default
                             *   {0, 0, 0} */
    rgb color;              /**< Display encoded color, default {255, 255, 255} */
    uint32_t seed;          /**< Seed of the random draws, default 0 */
});

/** Counters of the particle simulation, a singleton the render extract writes every frame.
 *
 * The live count comes from the spawn accounting of the extract, which knows when every
 * particle dies; nothing is read back from the GPU.
 */
ECS_STRUCT(ParticleStats, {
    int32_t emitters;       /**< Enabled ParticleEmitter entities */
    int32_t simulated;      /**< Pools with particles that ran the compute passes this frame */
    int32_t particles;      /**< Particles alive after this frame's spawns */
    int32_t spawned;        /**< Particles spawned this frame */
    int64_t bytes;          /**< Bytes of device memory the pools are kept in */
});

/** Register the box.particle module.
 *
 * Makes the render extract keep a pool for every enabled ParticleEmitter, simulate it in the
 * compute passes of the packet and draw it in the views of its scene, and writes ParticleStats.
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
