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
