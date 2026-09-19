/**
 * @file renderer/particle.h
 * @brief Pools, passes and pipelines of the GPU particle simulation.
 *
 * A pool holds the channels of one emitter, one buffer each, and lives as long
 * as the packet keeps naming its emitter. The spawn and integrate passes write
 * those channels and the point sprite pass reads them, so no particle state
 * ever crosses back to the main thread.
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

/** Vertices one particle is expanded into, two triangles of a quad */
#define BOX_PARTICLE_VERTICES (6)

/** Storage buffers the two compute passes bind, the channels plus the bookkeeping */
#define BOX_PARTICLE_SIM_BINDINGS (8)

/** Storage buffers the point sprite pass binds, the channels it reads */
#define BOX_PARTICLE_DRAW_BINDINGS (5)

/** Particle pool of one emitter, owned by the renderer.
 *
 * Every channel of a particle has a buffer of its own: the position carries the
 * age in its w, the velocity the lifetime, the acceleration the rotation and
 * the color the opacity, and the size has a buffer to itself. A slot whose
 * lifetime is zero holds no particle and sits in the dead list, which the
 * integrate pass pushes to and the spawn pass pops from.
 */
typedef struct box_gpu_emitter_t {
    uint32_t id;              /**< Emitter of the packet the pool belongs to */
    uint32_t capacity;        /**< Particles the buffers hold */
    uint32_t points;          /**< Points the shape table buffer holds */
    bool seen;                /**< The packet of this frame carries its emitter */
    WGPUBuffer position;      /**< Position xyz and age w of every particle */
    WGPUBuffer velocity;      /**< Velocity xyz and lifetime w, zero for a free slot */
    WGPUBuffer force;         /**< Acceleration xyz and rotation w */
    WGPUBuffer color;         /**< Linear color rgb and opacity w */
    WGPUBuffer size;          /**< Size of every particle */
    WGPUBuffer dead;          /**< Indices of the free slots, the dead list */
    WGPUBuffer counts;        /**< Free slots, live particles and two spares */
    WGPUBuffer uniform;       /**< Emitter block of the frame */
    WGPUBuffer table;         /**< Shape table of the frame, one point per spawn candidate */
    WGPUBindGroup sim_group;  /**< Buffers the spawn and the integrate pass bind */
    WGPUBindGroup draw_group; /**< Buffers the point sprite pass reads */
} box_gpu_emitter_t;

/** Pools, pipelines and layouts the particle passes of a renderer share */
typedef struct box_particle_gpu_t {
    box_vec_t pools;          /**< box_gpu_emitter_t, one per emitter the renderer saw */
    uint32_t sim_shader;      /**< Shader handle the compute pipelines were built from */
    uint32_t draw_shader;     /**< Shader handle the sprite pipeline was built from */
    WGPUBindGroupLayout sim_layout;  /**< Uniform and channels of the compute passes */
    WGPUBindGroupLayout draw_layout; /**< Channels the point sprite pass reads */
    WGPUComputePipeline spawn;     /**< Pass that fills the slots the frame spawns into */
    WGPUComputePipeline integrate; /**< Pass that ages and moves every live particle */
    WGPURenderPipeline pipeline[BOX_PARTICLE_FORMATS]; /**< Sprite pass per target format */
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
 * The pools are renderer state: a packet only names its emitters, so a pool is
 * created the first time its emitter appears, kept while it keeps appearing and
 * released when it stops. The integrate pass runs before the spawn pass, so a
 * slot that expires this frame is spawned into again in the same frame.
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
 * The pass runs after the sorted draws, expands every slot of a pool into a
 * camera facing quad and drops the slots that hold no live particle, reading
 * the depth the scene left behind without writing it.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 * @param attachment The render target view the view draws into
 * @param hdr The passes of the view write the float scene target
 */
void box_particle_record(
    box_renderer_t *r,
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
