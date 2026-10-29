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

/** Forces on the particles of emitters, as a world singleton, on an entity that
 * ParticleEmitter.forces names or on the emitter entity itself.
 *
 * An emitter adds up the forces of the three, multiplying their turbulence scales. A frame
 * adds (acceleration + gravity + wander) * delta to the velocity v, pulls it toward the wind
 * with v = wind + (v - wind) * exp(-drag * delta), then moves by (v + drift) * delta.
 *
 * The drift is the velocity of a frame of reference the particles move with, as a world that
 * scrolls past a fixed camera: velocities, forces, curves and the wander field are relative to
 * it, so a layer looks as without drift. The ground plane of ParticleCollision stays put.
 */
ECS_STRUCT(ParticleForces, {
    vec3 gravity;           /**< Acceleration in world units per second squared added to the
                             *   emitter acceleration, default {0, -9.81, 0} */
    vec3 wind;              /**< Velocity in world units per second the drag pulls particles
                             *   toward; wind acts through drag only, default {0, 0, 0} */
    float drag;             /**< Fraction of the difference between the velocity and the wind
                             *   lost per second, as a rate of exponential decay, default 0 */
    float turbulence;       /**< Acceleration of a smooth wander field in world units per
                             *   second squared, default 0 */
    float turbulence_scale; /**< Frequency multiplier of the wander field, above 1 for smaller
                             *   swirls, default 1 */
    vec3 drift;             /**< Velocity in world units per second of the frame of reference
                             *   that moves the live particles from the next frame without
                             *   entering their velocity, default {0, 0, 0} */
});

/** Collision of the particles of the emitter entity with the horizontal plane at height.
 *
 * A particle that ends a frame below the plane is lifted onto it, then keeps bounce of its
 * speed into the plane, reversed, and loses friction of its speed along it, or dies with die.
 * A particle that dies early keeps its slot and its count in ParticleStats until its lifetime.
 */
ECS_STRUCT(ParticleCollision, {
    bool ground;            /**< Collide with the ground plane, default true */
    float height;           /**< World height of the ground plane, default 0 */
    float bounce;           /**< Share of the speed into the plane a hit keeps, reversed, 0 to
                             *   1, default 0.5 */
    float friction;         /**< Share of the speed along the plane a hit loses, 0 to 1,
                             *   default 0.2 */
    bool die;               /**< Kill a particle that hits the plane instead, default false */
});

/** An emitter that spawns rate times the frame delta particles a frame, carrying the fraction.
 * Its WorldTransform places the spawn volume and direction; live particles stay in world space
 * but for the drift of their ParticleForces. A full pool spawns nothing; a new max empties it.
 *
 * Particles move under the acceleration and the ParticleForces, collide with the ground plane
 * of a ParticleCollision of the emitter entity and die once their age reaches their lifetime.
 * ParticlePlayback pauses, stops and resets the emitter; ParticleBurst spawns on top of the rate.
 *
 * Each is a camera facing soft circle of its size, color times opacity, over the opaque image,
 * unless ParticleCurves and ParticleAppearance on the emitter change that. Every drawn property
 * has its own random stream, so changing one range does not change the other draws.
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
    ParticleRange radial;   /**< Speed added along the offset of the spawn point from the
                             *   origin in emitter space, a ring over a disc or torus, none at
                             *   the origin, default {0, 0} */
    ParticleRange lifetime; /**< Seconds a particle lives, default {1, 2} */
    ParticleRange size;     /**< Diameter of the drawn circle in world units, default
                             *   {0.1, 0.1} */
    ParticleRange rotation; /**< Initial rotation in degrees around the view axis, default
                             *   {0, 0} */
    ParticleRange spin;     /**< Rotation in degrees per second, default {0, 0} */
    ParticleRange opacity;  /**< Opacity the color is drawn with, default {1, 1} */
    vec3 acceleration;      /**< Acceleration in world units per second squared, default
                             *   {0, 0, 0} */
    float inherit_velocity; /**< Share of the world velocity of the emitter a particle gets at
                             *   spawn, the velocity measured from the WorldTransform between
                             *   frames, default 0 */
    rgb color;              /**< Display encoded color, default {255, 255, 255} */
    rgb color_variance;     /**< Display encoded amount per channel the color of a particle
                             *   is drawn around color, clamped to 0 to 255, default
                             *   {0, 0, 0} */
    ecs_entity_t forces;    /**< Entity whose ParticleForces apply besides those of the world
                             *   and of the emitter entity, default 0 for none */
    uint32_t seed;          /**< Seed of the random draws, default 0 */
    bool linger;            /**< Deleting the emitter leaves its live particles to finish
                             *   their lives in a pool that spawns nothing, default false
                             *   to free the pool at once */
});

/** What an emitter with ParticlePlayback does this frame */
ECS_ENUM(PlaybackState, {
    ParticlePlaying = 0,    /**< Emits and moves its particles */
    ParticlePaused = 1,     /**< The pool is frozen: nothing is emitted and nothing moves, and
                             *   the particles stay drawn */
    ParticleStopped = 2     /**< Emission ended, live particles finish their lives; playing
                             *   again restarts the clock and the burst schedule */
});

/** Playback control of a ParticleEmitter; an emitter without it plays.
 *
 * Every control is a component write, so a script, the REST API or the set command drives
 * it. The render extract keeps the clock and writes it back every frame.
 */
ECS_STRUCT(ParticlePlayback, {
    PlaybackState state;    /**< Playing, paused or stopped, default ParticlePlaying */
    float time;             /**< Seconds the emitter has played since it started, written by
                             *   the engine every frame; advances only while playing */
    bool reset;             /**< Empties the pool and zeroes the clock this frame, cleared again
                             *   by the frame */
});

/** Particles spawned in one frame on a schedule of the playback clock, on a ParticleEmitter
 * or on any descendant of one, sharing the pool of the nearest emitter at or above it.
 *
 * It goes off at seconds into the playback and repeat more times every at seconds; a burst at
 * 0 goes off once on the first frame.
 *
 * Bursts run alongside the continuous rate, so an emitter with rate 0 and a burst is a one
 * shot effect; a burst larger than the free slots of the pool spawns what fits.
 */
ECS_STRUCT(ParticleBurst, {
    int32_t count;          /**< Particles spawned each time it goes off, default 0 */
    float at;               /**< Seconds into the playback it first goes off, and its period,
                             *   default 0 for once on the first frame */
    int32_t repeat;         /**< Times it goes off again after the first, below zero for as
                             *   long as the emitter plays, default 0 */
});

/** Four keys of a value over the life of a particle, at normalized age 0, 1/3, 2/3 and 1,
 * linear between two keys; a curve naming one value four times is a constant */
ECS_STRUCT(ParticleCurve, {
    float start;            /**< Value at birth */
    float early;            /**< Value at a third of the life */
    float late;             /**< Value at two thirds of the life */
    float end;              /**< Value at death */
});

/** Drives the intensity of the PointLight or SpotLight of a ParticleEmitter entity from its
 * playback, written by the render extract every frame before the lights are collected.
 *
 * While it plays the light has intensity, times the curve over period seconds of the playback
 * clock when period is above 0, once and then its end key, or with loop. Paused holds the light,
 * stopped fades it linearly to zero over fade seconds of stopped frames, playing restarts it.
 *
 * Every emitter of an effect instance, root and layers, drives the light it owns, the copy of
 * its template's. The light goes out with the entity, also a lingering one.
 */
ECS_STRUCT(ParticleLight, {
    float intensity;        /**< Intensity of the light while the emitter plays, default 1 */
    ParticleCurve curve;    /**< Multiplier of the intensity over period, its keys at 0, 1/3,
                             *   2/3 and 1 of it, default 1 */
    float period;           /**< Seconds of the playback clock the curve spans, default 0 for
                             *   no curve */
    bool loop;              /**< Repeat the curve every period, default false for once */
    float fade;             /**< Seconds the light fades out over once the emitter stopped,
                             *   default 0 to go dark at once */
});

/** A flash of the PointLight next to it in an effect template each time box_particle_burst_at
 * plays the effect: it starts at intensity and fades linearly to zero over seconds of the play,
 * then stays dark until the next play.
 *
 * After a frame the light holds the value at ParticleEffect.time, and an instance does not
 * retire before its flashes faded.
 */
ECS_STRUCT(EffectFlash, {
    float intensity;        /**< Intensity of the light when the effect starts, default 0 for
                             *   the intensity of the light itself */
    float seconds;          /**< Seconds the flash fades to zero over, default 0 for a fifth of
                             *   a second */
});

/** Four display encoded colors over the life of a particle, at the keys of ParticleCurve,
 * blended in linear light between two keys */
ECS_STRUCT(ParticleGradient, {
    rgb start;              /**< Color at birth */
    rgb early;              /**< Color at a third of the life */
    rgb late;               /**< Color at two thirds of the life */
    rgb end;                /**< Color at death */
});

/** How the particles of an emitter change over their life, evaluated on the GPU.
 *
 * Every curve multiplies the value a particle spawned with except rotation, which adds turns.
 * The ctor sets the multipliers to 1, rotation to 0 and the gradient to white, so
 * `ParticleCurves: {}` changes nothing.
 */
ECS_STRUCT(ParticleCurves, {
    ParticleCurve size;     /**< Multiplier of the size, default 1 */
    ParticleCurve opacity;  /**< Multiplier of the opacity, default 1 */
    ParticleCurve rotation; /**< Turns added to the rotation, default 0 */
    ParticleGradient color; /**< Multiplier of the color, default white */
    ParticleCurve spin;     /**< Multiplier of the angular velocity, default 1: the rotation a
                             *   particle has turned is the area under it up to its age */
});

/** What a particle is drawn as */
ECS_ENUM(ParticleShape, {
    ParticleCircle = 0,     /**< A soft round sprite, the look without an appearance */
    ParticleSprite = 1,     /**< The texture of the appearance over a sheet of frames */
    ParticleBillboard = 2   /**< The mesh of the appearance kept facing the camera */
});

/** How the particles of an emitter blend with the image */
ECS_ENUM(ParticleBlend, {
    ParticleAlpha = 0,      /**< Blended by opacity and drawn back to front, for smoke and
                             *   debris */
    ParticleAdditive = 1    /**< Color times opacity added in any order, never darkening, for
                             *   fire, sparks and magic */
});

/** How the particles of an emitter look. Without it they are soft alpha blended circles.
 *
 * Particles draw in the HDR image of the view before bloom, so colors above 1 bloom, with the
 * aerial perspective of the atmosphere as meshes have it.
 */
ECS_STRUCT(ParticleAppearance, {
    ParticleShape shape;    /**< What a particle is drawn as, default ParticleCircle */
    ParticleBlend blend;    /**< How it blends, default ParticleAlpha */
    ecs_entity_t texture;   /**< TexturePath or Texture entity a sprite reads, white without */
    ecs_entity_t mesh;      /**< Entity whose Mesh3 or shape a billboard draws, scaled by the
                             *   size; a circle is drawn while it has none */
    int32_t columns;        /**< Frames across the texture, 0 and 1 for one */
    int32_t rows;           /**< Frames down the texture, 0 and 1 for one, read row by row */
    float fps;              /**< Frames a second, 0 to play the sheet once over the life */
    float softness;         /**< World units over which a particle fades as it nears the
                             *   geometry behind it, 0 for hard edges */
    float stretch;          /**< Length added to a sprite or circle along its screen space
                             *   velocity per unit of speed, 0 for none */
    float lit;              /**< Fraction of the color the lights of the scene shade, 0 unlit
                             *   to 1 */
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
    int32_t lingering;      /**< Pools of deleted ParticleEmitter.linger emitters whose
                             *   particles still live, simulated without spawns and counted in
                             *   particles and bytes, not in emitters */
    int32_t effects;        /**< Effect instances playing after this frame */
    int32_t recycled;       /**< Bursts since the previous frame that a full budget had replay
                             *   an instance that was playing */
    int32_t merged;         /**< Bursts since the previous frame that joined a play nearby */
    int32_t dropped;        /**< Bursts since the previous frame that a full budget dropped */
});

/** What box_particle_burst_at does with a burst while a budget of ParticleSettings is full */
ECS_ENUM(EffectOverflow, {
    EffectRecycle = 0,      /**< The instance that has played longest plays the burst instead */
    EffectSkip = 1,         /**< A burst is dropped unless one view both sees it and has it
                             *   within effect_distance of its camera; a kept burst recycles */
    EffectMerge = 2         /**< A burst within effect_merge of a play of its template joins
                             *   it; with no play nearby it recycles */
});

/** The budgets of the effects box_particle_burst_at plays, a world singleton.
 *
 * A recycle never takes an instance that started in the same frame, and a burst that finds none
 * to take is dropped. A recycled instance empties its layers in the frame of the burst and
 * plays from the next.
 */
ECS_STRUCT(ParticleSettings, {
    int32_t effect_max;     /**< Instances of one template playing at once, default 0 for any */
    int32_t effect_total;   /**< Instances of every template playing at once, default 0 for
                             *   any */
    EffectOverflow effect_overflow; /**< What a burst does while a budget is full, default
                             *   EffectRecycle */
    float effect_distance;  /**< World distance from the camera of every view beyond which
                             *   EffectSkip drops a burst, default 0 for none */
    float effect_merge;     /**< World distance from a play of its template within which
                             *   EffectMerge joins a burst to it, default 0 */
});

/** A play of an effect template, on the root of an instance that box_particle_burst_at plays.
 *
 * It stays on the root of an instance that went back to the pool, disabled.
 */
ECS_STRUCT(ParticleEffect, {
    ecs_entity_t effect;    /**< The template, a prefab or a script template */
    float scale;            /**< Multiplier of the sizes, speeds and radial speeds, spawn
                             *   volumes and layer offsets, above 0 */
    float time;             /**< Seconds into the play, written by the engine every frame */
    float emit;             /**< Seconds into the play at which the last burst of its layers
                             *   goes off and their emission ends */
    float retire;           /**< Seconds into the play at which the instance goes back to the
                             *   pool: emit plus the longest lifetime of its layers, or its
                             *   longest EffectFlash when that ends later */
});

/** Play an effect template once at a world point.
 *
 * The template is a prefab or a script template whose root and descendants with an emitter are
 * layers that play together. A free instance from its pool, or a new one, is placed, scaled and
 * played, and goes back to the pool disabled once its layers and its flashes played out.
 *
 * A layer stops emitting after its last burst; a burst that repeats forever goes off once. The
 * budgets of ParticleSettings rule a burst while they are full, and deleting the template deletes
 * its instances. Scripts call it as box.particle.burst_at(effect, position, scale).
 *
 * @param world The world, or in a system on the main thread before EcsPreStore its stage or
 *              its world: the instance is made when the commands of the stage merge and
 *              plays as a burst outside a system does, from that frame
 * @param effect The template
 * @param position World position of the play
 * @param scale Multiplier of the sizes, speeds, spawn volumes and layer offsets of the
 *              template, 0 or less for the size of the template
 * @return The root of the instance that plays the burst, 0 when the template holds no emitter
 *         or a full budget dropped the burst
 */
ecs_entity_t box_particle_burst_at(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float scale);

/** Register the box.particle module.
 *
 * Makes the render extract keep a pool for every enabled ParticleEmitter, simulate it in the
 * compute passes of the packet, draw it in the views of its scene and drive its ParticleLight,
 * and writes ParticleStats.
 *
 * Plays the effects of box_particle_burst_at within the budgets of ParticleSettings.
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
