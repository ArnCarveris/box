#ifndef FLECS_ENGINE_PARTICLES_H
#define FLECS_ENGINE_PARTICLES_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_PARTICLES_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* CPU-simulated, GPU-instanced particles.
 *
 * One pool entity owns a pool of particles; the whole world's live
 * particles are drawn with a single instanced draw call into the main
 * 3D pass (depth tested against the scene, no depth writes). There is
 * no entity per particle.
 *
 * Particles are round soft sprites billboarded toward the camera; a
 * stretch > 1 turns the sprite into a vertical streak (rain). A nonzero
 * axis orients the streak along that world-space direction instead
 * (line geometry, e.g. wind). Particles can be spawned three ways, all
 * of which end up calling
 * flecsEngine_particlesEmit:
 * - C game code calling flecsEngine_particlesEmit directly.
 * - FlecsParticleEmitter, for continuous, script-declarable emission.
 * - FlecsParticleBurst, for a one-shot script-declarable burst.
 * Game systems may also steer particles each frame by mutating the pool
 * array in the FlecsParticles component directly. */

#ifdef __cplusplus
extern "C" {
#endif

/* Color ramp over normalized particle age, evaluated on top of the
 * particle's own color (the first stop) and applied before the pool's
 * alpha envelope. An all-zero end stop means "no ramp": the particle
 * keeps its color for its whole life, exactly as before ramps existed.
 * An all-zero mid stop makes the ramp a two-stop color -> end gradient.
 * mid_pos is the normalized age of the mid stop; <= 0 selects 0.5.
 * Stop alpha is interpolated along with rgb and is then scaled by the
 * alpha envelope, so the two compose rather than override each other.
 * The pool's emissive is a separate additive term and is unaffected. */
ECS_STRUCT(FlecsParticleColorRamp, {
    FlecsRgba mid;
    FlecsRgba end;
    float mid_pos;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleColorRamp);

/* Evaluate a ramp at normalized age t (0..1) on top of base. Returns base
 * unchanged when the ramp has no end stop. Game code that steers a pool
 * directly can use this to match what the renderer draws. */
FlecsRgba flecsEngine_particlesRampColor(
    const FlecsParticleColorRamp *ramp,
    FlecsRgba base,
    float t);

ECS_ENUM(FlecsParticleBlendMode, {
    FlecsParticleBlendAlpha,
    FlecsParticleBlendAdditive,
});

extern ECS_COMPONENT_DECLARE(FlecsParticleBlendMode);

ECS_ENUM(FlecsParticleShapeMode, {
    FlecsParticleShapeBox,
    FlecsParticleShapeSphere,
    FlecsParticleShapeHemisphere,
    FlecsParticleShapeDisc,
    FlecsParticleShapeRing,
    FlecsParticleShapeCone,
});

extern ECS_COMPONENT_DECLARE(FlecsParticleShapeMode);

typedef struct flecs_particle_t {
    float pos[3];
    float vel[3];
    float axis[3];
    float size;      /* world-space sprite width */
    float grow;      /* size change per second */
    float stretch;   /* height/width ratio; <= 1 is a round sprite */
    float age;
    float max_age;
    float mass;
    float rotation;
    float spin;
    float brightness;
    float frame_offset;
    FlecsRgba color;
    FlecsParticleColorRamp ramp;
} flecs_particle_t;

ECS_ENUM(FlecsParticleEnvelopeMode, {
    FlecsParticleEnvelopeNone,
    FlecsParticleEnvelopeLinear,
    FlecsParticleEnvelopeEaseIn,
    FlecsParticleEnvelopeEaseOut,
    FlecsParticleEnvelopeEaseInOut,
});

ECS_ENUM(FlecsParticleSortMode, {
    FlecsParticleSortBackToFront,
    FlecsParticleSortNone,
});

ECS_STRUCT(FlecsParticleEnvelope, {
    FlecsParticleEnvelopeMode mode;
    float fade_in;
    float fade_out;
});

/* capacity/gravity/drag/emissive and the envelopes are the only
 * script-settable fields; particles and count are owned by the module
 * and are not reflected. */
ECS_STRUCT(FlecsParticles, {
    int32_t capacity;   /* pool size; 0 selects the default (256) */
    float gravity;      /* downward acceleration on every particle */
    float drag;         /* fraction of velocity shed per second */
    float emissive;     /* self-illumination strength */
    FlecsParticleSortMode sort_mode;
    FlecsParticleBlendMode blend_mode;

    float turbulence;
    float turbulence_scale;
    float turbulence_speed;

    float soft_depth;
    float lighting;

    ecs_entity_t texture;
    int32_t frames_x;
    int32_t frames_y;
    float frame_rate;

    FlecsParticleEnvelope size_envelope;
    FlecsParticleEnvelope alpha_envelope;
    FlecsParticleEnvelope emissive_envelope;

    /* Managed by the module */
    flecs_particle_t *particles;
    int32_t count;
    float sim_time;

    /* Cumulative stats, managed by the module */
    int32_t stat_emitted;
    int32_t stat_dropped;
    int32_t stat_ground;
    int32_t stat_expired;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleEnvelopeMode);
extern ECS_COMPONENT_DECLARE(FlecsParticleSortMode);
extern ECS_COMPONENT_DECLARE(FlecsParticleEnvelope);
extern ECS_COMPONENT_DECLARE(FlecsParticles);

ECS_STRUCT(FlecsParticleCurves, {
    ecs_entity_t size;
    ecs_entity_t alpha;
    ecs_entity_t emissive;
    ecs_entity_t stretch;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleCurves);

float flecsEngine_particlesEvalChannel(
    const ecs_world_t *world,
    const FlecsParticleEnvelope *envelope,
    ecs_entity_t curve,
    float t);


/* Continuous emitter: spawns rate particles/sec at the entity's world
 * position (from FlecsWorldTransform3, if present) plus offset. Disabled
 * or rate <= 0 emits nothing. pool == 0 emits into a FlecsParticles pool
 * on the same entity (added automatically if missing).
 *
 * With local_frame set, offset, velocity, the sampled velocity variance,
 * the emission shape and spread are expressed in the entity's own frame
 * and are rotated by its world rotation (scale removed), so a rotating
 * emitter carries its stream with it. Without it they stay world-space,
 * which is the default and what every existing emitter does.
 *
 * size/max_age/velocity are randomized per spawned particle within
 * +-variance (independently per axis for velocity). Since components are
 * copied on instantiation by default, many entities can share one setup
 * via an IsA prefab and still emit and accumulate independently. */
ECS_STRUCT(FlecsParticleEmitter, {
    ecs_entity_t pool;
    float rate;
    float size;
    float size_variance;
    float grow;
    float stretch;
    float max_age;
    float max_age_variance;
    FlecsRgba color;
    FlecsParticleColorRamp color_ramp;
    flecs_vec3_t velocity;
    flecs_vec3_t velocity_variance;
    flecs_vec3_t offset;  /* local offset from the entity's world position */
    flecs_vec3_t spread;
    float radial;
    FlecsParticleShapeMode shape;
    float shape_radius;
    float shape_angle;
    float shape_thickness;
    float rotation;
    float rotation_variance;
    float spin;
    float spin_variance;
    float brightness_variance;
    float mass;
    float inherit_velocity;
    bool enabled;
    bool local_frame;

    /* Managed by the module: fractional particle carried to the next tick */
    float accum;
    flecs_vec3_t prev_pos;
    bool has_prev;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleEmitter);

/* One-shot burst: setting FlecsParticleBurst emits count particles at the
 * entity's world position + offset on the next OnUpdate tick, after which
 * the component removes itself. Fields have the same meaning as on
 * FlecsParticleEmitter, minus rate/enabled. */
ECS_STRUCT(FlecsParticleBurst, {
    ecs_entity_t pool;
    int32_t count;
    float size;
    float size_variance;
    float grow;
    float stretch;
    float max_age;
    float max_age_variance;
    FlecsRgba color;
    FlecsParticleColorRamp color_ramp;
    flecs_vec3_t velocity;
    flecs_vec3_t velocity_variance;
    flecs_vec3_t offset;
    flecs_vec3_t spread;
    float radial;
    FlecsParticleShapeMode shape;
    float shape_radius;
    float shape_angle;
    float shape_thickness;
    float rotation;
    float rotation_variance;
    float spin;
    float spin_variance;
    float brightness_variance;
    float mass;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleBurst);

void flecsEngine_particlesBurst(
    ecs_world_t *world,
    ecs_entity_t pool,
    const float pos[3],
    const FlecsParticleBurst *burst);

ECS_STRUCT(FlecsParticleWind, {
    ecs_entity_t pool;
    float strength;
    int32_t width;
    int32_t height;
    float cell_size;
    const flecs_vec2_t *field;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleWind);

/* Height map collision: kills particles in the pool when they reach the
 * ground. corner_heights holds (width+1)*(depth+1) corner heights (the
 * FlecsTerrain layout); origin is the world xz of corner (0,0). pool == 0
 * targets a FlecsParticles pool on the same entity. */
ECS_STRUCT(FlecsParticleGround, {
    ecs_entity_t pool;
    int32_t width;
    int32_t depth;
    float cell_size;
    flecs_vec2_t origin;
    const float *corner_heights;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleGround);

/* Add a particle to a pool. When the pool is full the particle is dropped.
 * Fields left zero get usable defaults (stretch 1, max_age 1, size 0.1,
 * opaque white). */
void flecsEngine_particlesEmit(
    ecs_world_t *world,
    ecs_entity_t pool,
    const flecs_particle_t *particle);

void FlecsEngineParticlesImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
