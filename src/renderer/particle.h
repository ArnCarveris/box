/**
 * @file renderer/particle.h
 * @brief Pools, passes and pipelines of the GPU particle simulation.
 *
 * A pool holds one buffer per channel of an emitter while packets name it. Spawn
 * and integrate write the channels and the sprite pass reads them, all on the GPU.
 */

#ifndef BOX_RENDERER_PARTICLE_H
#define BOX_RENDERER_PARTICLE_H

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"
#include "packet.h"
#include "vec.h"
#include "renderer.h"

/** Target formats the point sprite pass is compiled for, the eight bit and the float one */
#define BOX_PARTICLE_FORMATS (2)

/** Vertices one particle is expanded into, the corners of a quad as a strip.
 *
 * The strip makes the same two triangles a list of six would, in the same
 * order and with the same winding, and shades four vertices instead of six,
 * which is a third of the vertex work of every particle of a frame.
 */
#define BOX_PARTICLE_VERTICES (4)

/** Storage buffers the two compute passes bind, the channels plus the bookkeeping */
#define BOX_PARTICLE_SIM_BINDINGS (7)

/** Bindings the point sprite pass takes: the four channels, the slots and the block */
#define BOX_PARTICLE_DRAW_BINDINGS (6)

/** Ways a pool is composited, blended over the image and added to it */
#define BOX_PARTICLE_BLENDS (2)

/** Particle pool of one emitter, owned by the renderer.
 *
 * Position (age in w) and velocity (lifetime in w) are full floats; color, opacity,
 * rotation and size are half floats. A slot with zero lifetime is on the dead
 * list, which shares one buffer with the compacted alive list.
 */
typedef struct box_gpu_emitter_t {
    uint32_t id;              /**< Emitter of the packet the pool belongs to */
    uint32_t capacity;        /**< Particles the buffers hold */
    uint32_t points;          /**< Entries the table buffer holds, points and colliders */
    uint32_t appearance;      /**< One of the BOX_PARTICLE_DRAW constants */
    uint32_t mesh;            /**< Mesh handle a billboard is drawn as, 0 for a quad */
    uint32_t indices;         /**< Indices the draw arguments of its billboard hold */
    uint32_t instances;       /**< Instances the draw arguments of a blended pool hold */
    bool additive;            /**< The pool is added to the image instead of blended over it */
    float origin[3];          /**< World position of its emitter, which sorts the pool */
    bool seen;                /**< The packet of this frame carries its emitter */
    bool stepped;             /**< The frame recorded its compute passes */
    WGPUBuffer position;      /**< Position xyz and age w of every particle */
    WGPUBuffer velocity;      /**< Velocity xyz and lifetime w, zero for a free slot */
    WGPUBuffer color;         /**< Linear color and opacity of every particle, four half floats */
    WGPUBuffer extra;         /**< Rotation and size of every particle, two half floats */
    WGPUBuffer slots;         /**< The dead list and the alive list of the frame behind it */
    WGPUBuffer counts;        /**< The free, live, alive and span counters of the pool */
    WGPUBuffer args;          /**< Arguments of the indirect dispatch and the two draws */
    WGPUBuffer uniform;       /**< Emitter block of the frame */
    WGPUBuffer table;         /**< Spawn points of the shape and the colliders behind them */
    WGPUBindGroup sim_group;  /**< Buffers the spawn and the integrate pass bind */
    WGPUBindGroup args_group; /**< Argument buffer the prepare and the finish pass write */
    WGPUBindGroup draw_group; /**< Buffers the point sprite pass reads */
} box_gpu_emitter_t;

/** One pool of a view, with the distance that sorts it back to front */
typedef struct box_particle_order_t {
    uint32_t pool;  /**< Index of the pool in the pools of the renderer */
    float distance; /**< Distance of its emitter to the camera of the view */
} box_particle_order_t;

/** Pools, pipelines and layouts the particle passes of a renderer share */
typedef struct box_particle_gpu_t {
    box_vec_t pools;          /**< box_gpu_emitter_t, one per emitter the renderer saw */
    uint32_t sim_shader;      /**< Shader handle the compute pipelines were built from */
    uint32_t draw_shader;     /**< Shader handle the sprite pipeline was built from */
    WGPUBindGroupLayout sim_layout;  /**< Uniform and channels of the compute passes */
    WGPUBindGroupLayout args_layout; /**< Argument buffer of the prepare and the finish pass */
    WGPUBindGroupLayout draw_layout; /**< Channels and block the point sprite pass reads */
    WGPUBindGroupLayout depth_layout; /**< Depth target the soft fade of a particle reads */
    WGPUBindGroup depth_group;       /**< Depth group of the view being recorded */
    WGPUTextureView depth_for;       /**< Depth view that group was built for */
    box_vec_t order;                 /**< box_particle_order_t of the pools of one view */
    WGPUComputePipeline alloc;     /**< Pass that takes the slots of the frame off the dead list */
    WGPUComputePipeline spawn;     /**< Pass that fills the slots the frame spawns into */
    WGPUComputePipeline integrate; /**< Pass that ages and moves every live particle */
    WGPUComputePipeline finish;    /**< Pass that writes the instances an added pool draws */
    WGPURenderPipeline pipeline[BOX_PARTICLE_FORMATS][BOX_PARTICLE_BLENDS]; /**< Quad pass */
    WGPURenderPipeline mesh[BOX_PARTICLE_FORMATS][BOX_PARTICLE_BLENDS]; /**< Billboard pass */
    bool failed;              /**< The particle shaders were reported as broken once */
} box_particle_gpu_t;

/** Drop the pipelines a particle shader built when its source changed.
 *
 * @param r The renderer
 * @param handle The shader handle that changed
 */
void box_particle_invalidate(
    box_renderer_t *r,
    uint32_t handle);

/** Spawn into and integrate the pool of every emitter of a packet.
 *
 * A pool is created when its emitter first appears and released when it stops
 * appearing. Integrate runs before spawn, so expired slots are reused this frame.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 */
void box_particle_simulate(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder);

/** Draw the particles of every pool over the image of a view.
 *
 * Runs after the sorted draws, depth tested without depth writes. Blended pools
 * are drawn back to front, then the additive pools.
 *
 * @param r The renderer
 * @param view The view being recorded
 * @param p The packet
 * @param encoder The command encoder
 * @param attachment The render target view the view draws into
 * @param hdr The passes of the view write the float scene target
 */
void box_particle_record(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment,
    bool hdr);

/** Release the pools, pipelines and layouts of the particle passes.
 *
 * @param r The renderer
 */
void box_particle_fini(
    box_renderer_t *r);

#endif
