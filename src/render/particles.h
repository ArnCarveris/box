/**
 * @file render/particles.h
 * @brief Particle pools of the render packet: the parameter block of an emitter for a frame and
 * the extract that spawns into the pools and counts their particles.
 */

#ifndef BOX_RENDER_PARTICLES_H
#define BOX_RENDER_PARTICLES_H

#include "render.h"

/** First buffer handle of the particle pages: storage buffers whose ranges are pools */
#define BOX_RENDER_BUFFER_PARTICLE_FIRST (0x50000000u)

/** First buffer handle of the point tables of the EmitterMesh emitters */
#define BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST (0x58000000u)

/** Bytes of a particle in a pool, box_particle_t of src/particle/simulate.h */
#define BOX_RENDER_PARTICLE_SIZE (32)

/** First buffer handle of the corner buffers of the ParticleBillboard meshes */
#define BOX_RENDER_BUFFER_PARTICLE_MESH_FIRST (0x5C000000u)

/** Buffer handle of the curve table: the baked rows of the curves of every emitter, of
 * BOX_PARTICLE_CURVE_SAMPLES floats each */
#define BOX_RENDER_BUFFER_PARTICLE_CURVES (0x5FFFFFFFu)

/** Rows of the curve table when it is first created, doubled when it grows */
#define BOX_RENDER_PARTICLE_CURVE_ROWS (32u)

/** Fewest keys a sort of a pool sorts, the keys one workgroup sorts in shared memory */
#define BOX_RENDER_PARTICLE_SORT_MIN (512u)

/** A corner of a triangle of a billboard mesh, as Corner in etc/shaders/include/particle.wgsl */
typedef struct box_render_particle_corner_t {
    float position[4];      /**< Position in the space of the mesh times its shape scale, w the
                             *   u texture coordinate */
    float normal[4];        /**< Unit normal, w the v texture coordinate */
} box_render_particle_corner_t;

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
    float color[4];         /**< Display encoded color, 0 to 1, w unused */
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
    uint32_t args;          /**< Index of the indirect draw arguments of the pool, set by the
                             *   backend */
    uint32_t delta_us;      /**< Microseconds of the frame */
    uint32_t life_min;      /**< Shortest lifetime in microseconds */
    uint32_t life_span;     /**< Microseconds the longest lifetime is above life_min */
    uint32_t sort_first;    /**< First sort key of the pool in the key buffer, set by the
                             *   backend */
    float wind[4];          /**< World velocity the drag pulls toward, w the drag rate */
    float turbulence[4];    /**< Acceleration of the wander field, y its frequency scale, zw
                             *   unused */
    float wander[4];        /**< Phase of the wander field drawn from the seed, offset by the
                             *   displacement of the frame of reference, w unused */
    float variance[4];      /**< Display encoded color variance, 0 to 1, w unused */
    float inherit[4];       /**< World velocity every spawn of the frame gets, w unused */
    float ground[4];        /**< Height of the ground plane, y the bounce, z the friction, w
                             *   unused */
    float drift[3];         /**< World velocity of the frame of reference, added to the
                             *   position only */
    uint32_t collide;       /**< BOX_PARTICLE_GROUND_NONE, _BOUNCE or _DIE */
    uint32_t look;          /**< ParticleShape drawn, ParticleCircle while a billboard has no
                             *   mesh */
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
    uint32_t corners;       /**< Vertices drawn per particle: 6 for a quad, else the corners of
                             *   the billboard mesh */
    uint32_t sorted;        /**< 1 when the pool is drawn back to front through its sort keys */
    uint32_t sort_count;    /**< Keys sorted, a power of two of at least
                             *   BOX_RENDER_PARTICLE_SORT_MIN, 0 when not sorted */
    uint32_t styled;        /**< 1 when the emitter has a ParticleAppearance */
} box_render_particle_block_t;

/** A particle pool of the frame: a range of a particle page, simulated after the scatters and
 * drawn by the views of its scene */
typedef struct box_render_particle_pool_t {
    ecs_entity_t entity;    /**< Entity with the ParticleEmitter */
    ecs_entity_t scene;     /**< Scene root of the emitter, 0 for the default scene */
    uint32_t buffer;        /**< Handle of the particle page */
    uint32_t offset;        /**< Byte offset of the pool in the page */
    uint32_t capacity;      /**< Slots of the pool */
    bool clear;             /**< The range is zeroed before the passes, for a new pool */
    uint32_t table;         /**< Buffer handle of the mesh table, 0 for none */
    uint32_t slots;         /**< Data offset of the block.count uint32_t spawn slots */
    uint32_t texture;       /**< Texture handle of a sprite, 0 for white */
    uint32_t mesh;          /**< Buffer handle of the corners of a billboard, 0 for none */
    uint32_t curves;        /**< Buffer handle of the curve table, 0 before it exists */
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
