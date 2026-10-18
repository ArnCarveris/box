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
    float acceleration[4];  /**< World acceleration, w the seconds of the frame */
    float color[4];         /**< Linear color, w unused */
    float speed[2];         /**< Range of the initial speed */
    float size[2];          /**< Range of the diameter */
    float rotation[2];      /**< Range of the initial rotation in degrees */
    float spin[2];          /**< Range of the spin in degrees per second */
    float opacity[2];       /**< Range of the opacity */
    float pad0[2];          /**< Unused */
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
    uint32_t pad1[9];       /**< Pads the block to the uniform offset alignment */
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
    box_render_particle_block_t block; /**< Parameters of the frame */
} box_render_particle_pool_t;

/** Register the system that keeps a particle pool for every enabled ParticleEmitter, spawns
 * into it and writes ParticleStats.
 *
 * The system counts every particle it spawns until it dies, so it knows the live particles and
 * the free slots of each pool without reading the GPU back; it does nothing until box.particle
 * hands it the emitters.
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
