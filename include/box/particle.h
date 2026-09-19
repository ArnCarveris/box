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
 *
 * ParticleForces moves the particles of an emitter with gravity, wind, drag and
 * turbulence, and ParticleCollision gives them a ground plane and the colliders
 * of the scene to bounce off, slide along or die on.
 *
 * ParticlePlayback starts, pauses, stops and resets an emitter and carries the
 * clock its ParticleBurst schedules go off on, so every control of the effect
 * is a component write that a script, the REST API or a terminal command makes
 * without an endpoint of its own.
 *
 * An effect that plays once at a point of the world is a template of those
 * components: box_particle_burst_at and particle.burst_at play it from a pool
 * of instances per template and hand the instance back when its last particle
 * has died, so nothing is created or deleted per hit.
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
    rgb color_variance;     /**< Per channel amount the color of a particle is drawn around it */
    ecs_entity_t forces;    /**< Entity whose ParticleForces this emitter adds to its own */
    uint32_t seed;          /**< Seed of the random draw, the same seed gives the same frames */
});

/** Forces the particles of an emitter are moved by.
 *
 * An emitter adds three sets of forces: the component of the world, which is
 * the one set on the scene itself and reaches every emitter, the component of
 * the entity its `forces` member names, which is how one wind is shared by a
 * group, and the component it carries itself.
 *
 * Wind reaches a particle through the drag only: the drag pulls the velocity of
 * a particle towards the wind, so a particle without drag keeps its own course
 * whatever the wind is. Gravity is added to the `acceleration` of the emitter.
 *
 * The constructor gives the component the gravity of earth, so an emitter that
 * names `ParticleForces: {}` falls at 9.81 world units per second squared.
 */
ECS_STRUCT(ParticleForces, {
    vec3 gravity;     /**< Acceleration in world units per second squared */
    vec3 wind;        /**< Velocity the drag pulls a particle towards */
    float drag;       /**< Fraction of the difference with the wind lost per second */
    float turbulence; /**< Acceleration of the wander every particle is given */
});

/** What the particles of an emitter collide with and how they answer.
 *
 * The ground is a horizontal plane at `height`. The colliders are the entities
 * with a `Collider` that the extract finds within `reach` of the emitter in the
 * collision index, at most sixteen of them: a sphere collider is tested as a
 * sphere and every other shape as the box of its world bounds, which is what
 * keeps the test of a particle a handful of instructions.
 *
 * A particle that hits keeps `bounce` of the speed it had into the surface and
 * loses `friction` of the speed it had along it, or dies on the spot when `die`
 * is set, which is what a splash or a scorch mark is spawned from.
 */
ECS_STRUCT(ParticleCollision, {
    bool ground;    /**< Collide with the horizontal plane at height */
    float height;   /**< Height of the ground plane in world units */
    float reach;    /**< Distance around the emitter its colliders are taken from */
    float bounce;   /**< Fraction of the speed into the surface a hit keeps */
    float friction; /**< Fraction of the speed along the surface a hit loses */
    bool die;       /**< A particle that hits dies instead of bouncing */
});

/** Four keys of a curve over the normalised age of a particle.
 *
 * The keys sit at the start, a third in, two thirds in and at the end of the
 * life, and the value between two of them is the line between the two, so a
 * curve that names one value four times is a constant.
 */
ECS_STRUCT(ParticleCurve, {
    float start; /**< Value a particle is born with */
    float early; /**< Value a third of the way through its life */
    float late;  /**< Value two thirds of the way through its life */
    float end;   /**< Value it dies with */
});

/** Four colors a particle passes through over its life.
 *
 * The colors multiply the one the particle was drawn with, so a white particle
 * takes the gradient as it is and a colored one is tinted by it.
 */
ECS_STRUCT(ParticleGradient, {
    rgb start; /**< Color a particle is born with */
    rgb early; /**< Color a third of the way through its life */
    rgb late;  /**< Color two thirds of the way through its life */
    rgb end;   /**< Color it dies with */
});

/** What a particle of an emitter turns into over its life.
 *
 * Every curve multiplies what the particle was spawned with, except the
 * rotation, which adds turns, so the constructor hands out ones and zeros and
 * `ParticleCurves: {}` changes nothing. A smoke puff that grows and fades is
 * `size: {start: 1, early: 1.6, late: 2.2, end: 2.6}` with
 * `opacity: {start: 0, early: 1, late: 0.7, end: 0}`.
 */
ECS_STRUCT(ParticleCurves, {
    ParticleCurve size;     /**< Multiplies the size the particle was spawned with */
    ParticleCurve opacity;  /**< Multiplies the opacity it was spawned with */
    ParticleCurve rotation; /**< Turns added to the rotation it was spawned with */
    ParticleGradient color; /**< Multiplies the color it was spawned with */
});

/** Shape a particle is drawn as */
ECS_ENUM(ParticleShape, {
    ParticleCircle = 0,   /**< Soft round sprite, the appearance of an emitter without one */
    ParticleSprite = 1,   /**< The texture of the appearance, over a sheet of frames */
    ParticleBillboard = 2 /**< The mesh of the appearance, kept facing the camera */
});

/** How a particle is composited over what is already drawn */
ECS_ENUM(ParticleBlend, {
    ParticleAlpha = 0,   /**< Blended over the image by its opacity, for smoke and debris */
    ParticleAdditive = 1 /**< Added to the image, for fire, sparks and magic */
});

/** How the particles of an emitter are drawn.
 *
 * A sprite reads `texture`, which is a texture entity, an entity with an Image
 * or an entity with PbrTextures whose albedo is taken; `columns` and `rows`
 * split it into a sheet of frames, which `fps` plays at, or once over the life
 * of the particle when it is zero. A billboard draws the Mesh3 of `mesh` at the
 * place, size and rotation of the particle instead of a quad.
 *
 * `softness` fades a particle out as it approaches the geometry behind it, over
 * that many world units, which is what stops a smoke puff from cutting a hard
 * line into a wall.
 *
 * `stretch` turns the sprite into a streak: it is laid over the direction the
 * particle moves in on the screen and grown along it by that many world units
 * per unit of speed, so a rain drop, a spark or a piece of debris is drawn as
 * the line it travels instead of as a dot. A sprite without a stretch keeps
 * facing the camera squarely and is turned by its own rotation.
 */
ECS_STRUCT(ParticleAppearance, {
    ParticleShape shape;  /**< Circle, sprite or billboard */
    ParticleBlend blend;  /**< Alpha or additive */
    ecs_entity_t texture; /**< Texture of the sprite, or the entity that names one */
    ecs_entity_t mesh;    /**< Entity whose mesh a billboard is drawn as */
    int32_t columns;      /**< Frames across the sheet, 0 and 1 are the whole texture */
    int32_t rows;         /**< Frames down the sheet, 0 and 1 are the whole texture */
    float fps;            /**< Frames per second, 0 plays the sheet once over the life */
    float softness;       /**< World units a particle fades out over near geometry */
    float stretch;        /**< World units the sprite grows along its velocity per unit of speed */
});

/** State the playback of an emitter is in */
ECS_ENUM(PlaybackState, {
    ParticlePlaying = 0, /**< The emitter emits and its particles move */
    ParticlePaused = 1,  /**< The pool is frozen: nothing is emitted and nothing moves */
    ParticleStopped = 2  /**< Emission ended, the particles that are alive finish */
});

/** Playback of the emitter of an entity.
 *
 * An emitter without the component plays, so a stream that is never controlled
 * costs nothing. Stopping ends the emission and leaves the particles that are
 * in the air to finish their life, pausing freezes the whole pool where it is
 * and starting a stopped emitter begins its clock again, which is what replays
 * a burst schedule from the top.
 *
 * `reset` is a trigger: the frame that sees it empties the pool of the emitter,
 * sets the clock back to zero and writes the member back to false.
 *
 * The clock is written by the engine, not read from it: it advances while the
 * emitter plays and stands still while it is paused or stopped, so the times
 * the bursts of the emitter go off at are the seconds it has been playing.
 */
ECS_STRUCT(ParticlePlayback, {
    PlaybackState state; /**< Playing, paused or stopped */
    double time;         /**< Seconds the emitter has been playing, written every frame */
    bool reset;          /**< Empty the pool of the emitter, cleared again by the frame */
});

/** Burst of particles an emitter spawns at a time of its playback.
 *
 * A burst runs alongside the continuous rate of the emitter and is capped by
 * the same pool, so a burst of more particles than the emitter keeps alive
 * fills what is free. The component sits on the emitter itself or on any of its
 * descendants, which is how one emitter is given several schedules.
 *
 * The burst goes off `at` seconds into the playback and `repeat` more times
 * every `at` seconds after that, so `{count: 200, at: 0.5, repeat: 3}` is four
 * puffs half a second apart. A burst at a time of zero goes off once, on the
 * first frame the emitter plays.
 */
ECS_STRUCT(ParticleBurst, {
    int32_t count;  /**< Particles the burst spawns in one frame */
    float at;       /**< Seconds into the playback it goes off, and the period of its repeats */
    int32_t repeat; /**< Times it goes off again, below zero to repeat while the emitter plays */
});

/** Budgets and culling that every emitter of a world shares.
 *
 * The singleton caps what the pools of a world hold together and what a frame
 * spawns into them together. An emitter that asks for more than its share of
 * either gets the share: the ceiling is divided in proportion to what the
 * emitters asked for, so a scene that runs past its budget thins every stream
 * instead of stalling or dropping one of them.
 *
 * The particles of an emitter stay inside a sphere its speed, its lifetime and
 * its acceleration bound. An emitter whose sphere no view can see is not
 * simulated at all, one further than `cull_distance` from every camera is not
 * either, and one further than `step_distance` is simulated every other frame
 * with both frames in one step, which is the same motion at half the cost.
 */
ECS_STRUCT(ParticleSettings, {
    int32_t max_total;       /**< Particles every pool together holds, 0 for no ceiling */
    int32_t spawn_per_frame; /**< Particles every emitter spawns in a frame, 0 for none */
    float cull_margin;       /**< World units the sphere of an emitter is grown by */
    float cull_distance;     /**< Distance beyond which an emitter stops, 0 for no limit */
    float step_distance;     /**< Distance beyond which an emitter is stepped every other frame */
    bool cull;               /**< Emitters no view can see are not simulated */
});

/** What the emitters of a world cost the frame that was extracted last */
ECS_STRUCT(ParticleStats, {
    int32_t emitters;  /**< Emitters the frame carried */
    int32_t simulated; /**< Emitters the frame spawned into and stepped */
    int32_t culled;    /**< Emitters no view could see, or that were too far away */
    int32_t halved;    /**< Emitters the frame steps every other frame */
    int32_t particles; /**< Particles the pools of those emitters hold together */
    int32_t spawned;   /**< Particles the frame spawned */
    int64_t bytes;     /**< Bytes those pools cost on the device */
});

/** Fading flash of an effect, next to the emitter of its template.
 *
 * An effect that lights the scene it goes off in carries a PointLight and this
 * component: the play fades the intensity of the light from `intensity` to
 * nothing over `seconds` and leaves it dark after, so the muzzle flash of a
 * template is two components of that template instead of code that creates a
 * light per shot and deletes it again.
 */
ECS_STRUCT(EffectFlash, {
    float intensity; /**< Intensity the flash starts at, 0 takes the one of the light */
    float seconds;   /**< Seconds it fades over, 0 for a fifth of a second */
});

/** One shot play of an effect template, on the instance that is playing it.
 *
 * box_particle_burst_at takes an instance of a template from the pool of that
 * template, places it, scales it and plays it from the top. The engine ends the
 * emission once the bursts of the template have all gone off and hands the
 * instance back to the pool the moment its last particle has died, so the same
 * hit effect fired a hundred times a second costs the entities of one pool
 * instead of one entity per hit.
 */
ECS_STRUCT(ParticleEffect, {
    ecs_entity_t effect; /**< Template the instance was made from */
    float scale;         /**< Size of the play, 1 for the template as it was authored */
    double time;         /**< Seconds into the play, written every frame */
    double emit;         /**< Seconds into the play the emission ends at */
    double retire;       /**< Seconds into the play the instance is freed at */
});

/** Play an effect template once at a point of the world.
 *
 * The template is one entity, an ordinary prefab or script template holding the
 * emitter of the effect, its curves, its bursts, its appearance and an optional
 * PointLight with an EffectFlash. The play takes a free instance of that
 * template from its pool or makes one, so a stream of hits reuses the same
 * entities, and the scale multiplies the sizes, the speeds and the spawn volume
 * of the emitter, which is what lets one template cover a small and a large
 * explosion.
 *
 * @param world The world
 * @param effect The effect template
 * @param position Where the effect plays, in world space
 * @param scale Size of the play, 0 or less for the size of the template
 * @return The instance that is playing, 0 when the template holds no emitter
 */
ecs_entity_t box_particle_burst_at(
    ecs_world_t *world,
    ecs_entity_t effect,
    vec3 position,
    float scale);

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
