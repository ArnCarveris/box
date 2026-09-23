/**
 * @file box/particle.h
 * @brief Particle module: emitters whose particles are spawned and moved on the GPU.
 *
 * Emitters are placed by their entity transform; particles never reach the ECS
 * and are spawned and integrated by the renderer in compute passes. Random draws
 * hash the seed and spawn count, so a fixed timestep is deterministic.
 *
 * Forces, collision, playback and bursts are components on the emitter.
 * box_particle_burst_at plays a one shot effect template from a pool of
 * instances, so nothing is created or deleted per hit.
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
 * An emitter adds the forces of the world singleton, of the entity its `forces`
 * member names and of its own component. Wind acts through drag only; gravity
 * adds to the emitter `acceleration`. The constructor sets earth gravity.
 */
ECS_STRUCT(ParticleForces, {
    vec3 gravity;     /**< Acceleration in world units per second squared */
    vec3 wind;        /**< Velocity the drag pulls a particle towards */
    float drag;       /**< Fraction of the difference with the wind lost per second */
    float turbulence; /**< Acceleration of the wander every particle is given */
});

/** What the particles of an emitter collide with and how they answer.
 *
 * The ground is a plane at `height`; colliders are those within `reach` in the
 * collision index, at most sixteen, tested as spheres or world bounds boxes.
 *
 * A hit keeps `bounce` of the normal speed and loses `friction` of the tangent
 * speed, or kills the particle when `die` is set.
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
 * Every curve multiplies the spawned value except rotation, which adds turns,
 * so `ParticleCurves: {}` changes nothing.
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
 * A sprite reads `texture` (a texture, Image or PbrTextures entity), split into
 * `columns` by `rows` frames played at `fps`, or once per life when zero. A
 * billboard draws the Mesh3 of `mesh` instead of a quad.
 *
 * `softness` fades particles near geometry behind them over that many world
 * units. `stretch` lengthens a sprite along its screen velocity per unit of
 * speed; without it the sprite faces the camera and turns by its rotation.
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
 * An emitter without the component plays. Stopping ends emission and lets live
 * particles finish, pausing freezes the pool, and starting a stopped emitter
 * restarts its clock and burst schedule.
 *
 * `reset` empties the pool, zeroes the clock and is cleared by the frame. The
 * engine writes `time`, which advances only while playing.
 */
ECS_STRUCT(ParticlePlayback, {
    PlaybackState state; /**< Playing, paused or stopped */
    double time;         /**< Seconds the emitter has been playing, written every frame */
    bool reset;          /**< Empty the pool of the emitter, cleared again by the frame */
});

/** Burst of particles an emitter spawns at a time of its playback.
 *
 * A burst shares the pool of the emitter and sits on the emitter or any of its
 * descendants. It goes off `at` seconds into playback and `repeat` more times
 * every `at` seconds; a burst at zero goes off once on the first frame.
 */
ECS_STRUCT(ParticleBurst, {
    int32_t count;  /**< Particles the burst spawns in one frame */
    float at;       /**< Seconds into the playback it goes off, and the period of its repeats */
    int32_t repeat; /**< Times it goes off again, below zero to repeat while the emitter plays */
});

/** Budgets and culling that every emitter of a world shares.
 *
 * Pool and spawn ceilings are divided among emitters in proportion to what they
 * ask for. Emitters no view sees or beyond `cull_distance` are not simulated;
 * those beyond `step_distance` step every other frame with a double step.
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
 * Next to a PointLight, fades its intensity from `intensity` to zero over
 * `seconds` each time the effect plays, then leaves it dark.
 */
ECS_STRUCT(EffectFlash, {
    float intensity; /**< Intensity the flash starts at, 0 takes the one of the light */
    float seconds;   /**< Seconds it fades over, 0 for a fifth of a second */
});

/** One shot play of an effect template, on the instance that is playing it.
 *
 * box_particle_burst_at takes an instance from the template pool and plays it.
 * The engine ends emission after the last burst and returns the instance to the
 * pool when its last particle dies.
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
 * The template is a prefab or script template with an emitter, its curves,
 * bursts, appearance and optional PointLight with EffectFlash. A pooled
 * instance is reused; scale multiplies sizes, speeds and the spawn volume.
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
