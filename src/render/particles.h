/**
 * @file render/particles.h
 * @brief Particle pools of the render packet: the parameter block of an emitter for a frame and
 * the extract that spawns into the pools and counts their particles.
 */

#ifndef BOX_RENDER_PARTICLES_H
#define BOX_RENDER_PARTICLES_H

#include "render.h"

/** Buffer handle of the particle arena: one storage buffer whose ranges are the pools */
#define BOX_RENDER_BUFFER_PARTICLE_FIRST (0x50000000u)

/** Buffer handle of the point tables of the EmitterMesh emitters, one storage buffer of
 * BOX_PARTICLE_MESH_POINTS points per table */
#define BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST (0x58000000u)

/** Bytes of a particle in a pool, box_particle_t of src/particle/simulate.h */
#define BOX_RENDER_PARTICLE_SIZE (32)

/** Particles the arena holds at most, the default maxStorageBufferBindingSize of WebGPU */
#define BOX_RENDER_PARTICLE_ARENA_MAX (134217728u / BOX_RENDER_PARTICLE_SIZE)

/** Buffer handle of the curve table: the baked rows of the curves of every emitter, of
 * BOX_PARTICLE_CURVE_SAMPLES floats each */
#define BOX_RENDER_BUFFER_PARTICLE_CURVES (0x5FFFFFFFu)

/** Rows of the curve table when it is first created, doubled when it grows */
#define BOX_RENDER_PARTICLE_CURVE_ROWS (32u)

/** Spawn slot of a particle that spawns with a lifetime of 0, which takes no slot */
#define BOX_RENDER_PARTICLE_NO_SLOT (0xFFFFFFFFu)

/** The parameters of an emitter for one frame, as Emitter in etc/shaders/include/particle.wgsl.
 *
 * The integrate pass ages the used slots and moves the live ones, then the spawn pass writes
 * count particles with the spawn indices from first into the slots the extract picked. Ranges
 * are {min, max}; a max not above min hands out min.
 */
typedef struct box_render_particle_block_t {
    float model[4][4];      /**< World transform of the emitter, columns */
    float direction[4];     /**< Unit direction in emitter space or zero, w the cosine of the
                             *   spread */
    float extents[4];       /**< Half size of a box, w the radius of a sphere or disc */
    float acceleration[4];  /**< World acceleration plus gravity, w the seconds of the frame */
    float color[4];         /**< Display encoded color, 0 to 1, w the X of the shift: the
                             *   offset added to the live particles before they move, the move
                             *   of the render origin since their last frame for a world space
                             *   pool, zero otherwise */
    float speed[2];         /**< Range of the initial speed */
    float size[2];          /**< Range of the diameter */
    float rotation[2];      /**< Range of the initial rotation in degrees */
    float spin[2];          /**< Range of the spin in degrees per second */
    float opacity[2];       /**< Range of the opacity */
    float radial[2];        /**< Range of the initial speed away from the origin of the
                             *   emitter */
    uint32_t shape;         /**< EmitterShape */
    uint32_t seed;          /**< Seed of the draws */
    uint32_t count;         /**< Particles the spawn pass writes */
    uint32_t first;         /**< Spawn index of the first of them */
    uint32_t used;          /**< Slots from the start of the pool that may hold a particle */
    uint32_t slot_first;    /**< First spawn slot of the pool in the slot buffer, set by the
                             *   backend */
    uint32_t points;        /**< Points of the mesh table */
    uint32_t table_first;   /**< First point of the mesh table in the table buffer */
    uint32_t delta_us;      /**< Microseconds of the frame */
    uint32_t life_min;      /**< Shortest lifetime in microseconds */
    uint32_t life_span;     /**< Microseconds the longest lifetime is above life_min */
    uint32_t cards;         /**< Slot of the card words of a ParticleCard pool from the
                             *   start of the pool, its particle slots, 0 for other shapes */
    float wind[4];          /**< World velocity the drag pulls toward, w the drag rate */
    float turbulence[4];    /**< Acceleration of the wander field, y its frequency scale, z the
                             *   uniform world scale of the emitter, which multiplies the
                             *   velocity and force of its ParticleMotion, w 1 over the gust
                             *   scale of the global wind, 0 when the Wind pulls nothing */
    float wander[4];        /**< Phase of the wander field drawn from the seed, offset by the
                             *   displacement of the frame of reference, w the wrapped world X
                             *   offset of the gust field of the global wind */
    float variance[4];      /**< Display encoded color variance, 0 to 1, w the Y of the
                             *   shift */
    float inherit[4];       /**< World velocity every spawn of the frame gets, w the wrapped
                             *   world Z offset of the gust field of the global wind */
    float ground[4];        /**< Height of the ground plane, y the bounce, z the friction, w
                             *   the Z of the shift */
    float drift[3];         /**< World velocity of the frame of reference, added to the
                             *   position only */
    uint32_t collide;       /**< BOX_PARTICLE_GROUND_NONE, _BOUNCE or _DIE */
    uint32_t look;          /**< ParticleShape drawn */
    uint32_t blend;         /**< ParticleBlend */
    uint32_t columns;       /**< Frames across the sprite sheet, at least 1 */
    uint32_t rows;          /**< Frames down the sprite sheet, at least 1 */
    float fps;              /**< Frames a second, 0 to play the sheet once over the life */
    float softness;         /**< World units a particle fades over in front of the geometry */
    float stretch;          /**< Length added along the screen velocity per unit of speed */
    float lit;              /**< Fraction of the color the lights shade */
    uint32_t curve_rows[4]; /**< Rows in the curve table of the size, opacity, rotation turns
                             *   and spin curves, BOX_PARTICLE_CURVE_CONSTANT for a constant
                             *   one; the area of the spin is in the row after its own */
    float curve_values[4];  /**< Values of those curves while constant */
    float gradient[3];      /**< Linear color of a constant color multiplier */
    uint32_t gradient_row;  /**< First of the BOX_PARTICLE_GRADIENT_ROWS rows of the color
                             *   multiplier, BOX_PARTICLE_CURVE_CONSTANT for a constant one */
    float global_wind[3];   /**< World X and Z velocity of the Wind times ParticleForces
                             *   global_wind and the world scale at a gust factor of 1, z the
                             *   gust; all 0 without global wind */
    uint32_t styled;        /**< 1 when the emitter has a ParticleAppearance */
    uint32_t curve_max_rows[4]; /**< Rows of the max curves of the size, opacity, rotation and
                             *   spin, BOX_PARTICLE_CURVE_CONSTANT for none; the area of the
                             *   max spin is in the row after its own */
    uint32_t gradient_max_row; /**< First row of the max color multiplier,
                             *   BOX_PARTICLE_CURVE_CONSTANT for none */
    uint32_t motion;        /**< 1 when the emitter has a ParticleMotion that changes its
                             *   motion, which the passes read the members below for */
    uint32_t base;          /**< First slot of the pool in its particle page, set by the
                             *   backend */
    uint32_t local;         /**< 1 when the particles are spawned, moved and kept in the space
                             *   of model, ParticleEmitter.local */
    uint32_t motion_rows[4]; /**< First rows of the velocity, its max, the force and its max
                             *   of the ParticleMotion, as box_particle_motion_rows_t,
                             *   BOX_PARTICLE_CURVE_CONSTANT for a constant one or none */
    float motion_velocity[4]; /**< Velocity xyz and speed w while constant */
    float motion_force[4];  /**< Force xyz and drag w while constant */
    uint32_t sampled;       /**< BOX_RENDER_PARTICLE_SPAWN_GROUND and
                             *   BOX_RENDER_PARTICLE_COLLIDE_TERRAIN of the terrain of the pool */
    uint32_t sampled_pad[3]; /**< Zero */
    int32_t origin[4];      /**< The world origin split as box_gpu_origin_split, set by the
                             *   backend */
} box_render_particle_block_t;

/** The spawns of a pool stand on the surface of its terrain (ParticleEmitter.ground) */
#define BOX_RENDER_PARTICLE_SPAWN_GROUND (1u << 0)

/** The particles of a pool collide with the surface of its terrain under each of them
 * (ParticleCollision.terrain) */
#define BOX_RENDER_PARTICLE_COLLIDE_TERRAIN (1u << 1)

/** A particle pool of the frame: a range of the particle arena, simulated after the scatters
 * and drawn by the views of its scene */
typedef struct box_render_particle_pool_t {
    ecs_entity_t entity;    /**< Entity with the ParticleEmitter */
    ecs_entity_t scene;     /**< Scene root of the emitter, 0 for the default scene */
    uint32_t buffer;        /**< Handle of the particle arena */
    uint32_t offset;        /**< Byte offset of the pool in the arena */
    uint32_t capacity;      /**< Slots of the pool, with the card words of a ParticleCard
                             *   pool after its particle slots */
    bool clear;             /**< The range is zeroed before the passes, for a new pool */
    uint32_t table;         /**< Buffer handle of the mesh tables, 0 for none */
    uint32_t slots;         /**< Data offset of the block.count uint32_t spawn slots */
    uint32_t texture;       /**< Texture handle of a sprite, 0 for white */
    uint32_t curves;        /**< Buffer handle of the curve table, 0 when no curve of the
                             *   pool reads a row */
    ecs_entity_t terrain;   /**< Sampled terrain the passes bind for block.sampled, 0 for
                             *   none */
    box_render_particle_block_t block; /**< Parameters of the frame */
} box_render_particle_pool_t;

/** State of the particle extract, private to particles.c */
typedef struct box_render_particles_t box_render_particles_t;

/** Register the system that keeps a particle pool for every enabled ParticleEmitter, spawns
 * into it and writes ParticleStats.
 *
 * The system counts every particle it spawns until it dies, so it knows the live particles and
 * the free slots of each pool without reading the GPU back; it does nothing until box.particle
 * hands it the emitters.
 *
 * It runs before the light extract, so the intensities it writes for ParticleLight light the
 * same frame.
 *
 * @param world The world
 */
void box_render_particles_init(
    ecs_world_t *world);

/** Make the particle extract follow the ParticleEmitter components, called by the import of
 * box.particle.
 *
 * @param world The world
 */
void box_render_particles_import(
    ecs_world_t *world);

/** Get the state of the particle extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract or before box.particle handed it the emitters
 */
box_render_particles_t* box_render_particles_get(
    const ecs_world_t *world);

/** Get the particles alive in the pool of an emitter after the last extract.
 *
 * @param s The state of the extract, NULL for none
 * @param emitter The emitter
 * @return The particles, 0 for an emitter without a pool or with a kept one
 */
int32_t box_render_particles_live(
    const box_render_particles_t *s,
    ecs_entity_t emitter);

/** Counts of the curve table of the particle extract */
typedef struct box_render_particle_curve_stats_t {
    int32_t rows;           /**< Rows of the table that were ever handed out */
    int32_t used;           /**< Rows that curves of an emitter use */
    int32_t capacity;       /**< Rows the buffer holds */
    int32_t entries;        /**< Distinct curves in use */
    int64_t bakes;          /**< Curves baked into rows since the table was made */
    int64_t samplings;      /**< ParticleCurves and ParticleMotion sampled to find their rows
                             *   since the table was made; a pool whose curves the table saw
                             *   before samples nothing */
} box_render_particle_curve_stats_t;

/** Get the counts of the curve table.
 *
 * @param s The state of the extract, NULL for none
 * @param out Receives the counts, zeroed for no state
 */
void box_render_particles_curve_stats(
    const box_render_particles_t *s,
    box_render_particle_curve_stats_t *out);

/** Get the rows of the curve table as the extract last sent them.
 *
 * @param s The state of the extract, NULL for none
 * @param rows Receives the rows that were handed out, NULL to ignore
 * @return The rows, BOX_PARTICLE_CURVE_SAMPLES floats each, NULL without rows
 */
const float* box_render_particles_curve_table(
    const box_render_particles_t *s,
    int32_t *rows);

/** Get the ground collision of an emitter, its plane at the height of its terrain below the
 * emitter where the terrain is resident there.
 *
 * @param world The world
 * @param e The emitter
 * @param transform World transform of the emitter
 * @param buf Holds the collision with the height of the terrain
 * @return The collision, NULL for none
 */
const ParticleCollision* box_render_particles_collision(
    const ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *transform,
    ParticleCollision *buf);

/** Pick the sampled terrain of the pool of a world space emitter: its ground, which its spawns
 * stand on, and the terrain of its ParticleCollision when it is the same or there is no ground,
 * which its particles collide with one by one.
 *
 * @param world The world
 * @param e The emitter
 * @param emitter Its values
 * @param entry The pool of the packet, receives the terrain and the flags of its block
 */
void box_render_particles_sampled(
    const ecs_world_t *world,
    ecs_entity_t e,
    const ParticleEmitter *emitter,
    box_render_particle_pool_t *entry);

/** Get the transform an emitter spawns with: its own, offset by the world position of the
 * entity it follows, along X and Z only when its spawns stand on a ground.
 *
 * @param world The world
 * @param emitter The emitter values
 * @param transform World transform of the emitter
 * @param buf Holds the offset transform
 * @return The transform
 */
const WorldTransform* box_render_particles_follow(
    const ecs_world_t *world,
    const ParticleEmitter *emitter,
    const WorldTransform *transform,
    WorldTransform *buf);

/** Check whether a packet runs a particle shader.
 *
 * @param p The packet
 * @param handle Handle of the shader
 * @return Whether it simulates a pool for the simulation shader, or a pool has particles a
 *         view may draw for the draw shader
 */
bool box_render_particles_use(
    const box_render_packet_t *p,
    uint32_t handle);

#endif
