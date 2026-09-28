/**
 * @file renderer/instrument.h
 * @brief Routes the WebGPU calls of the renderer through the per frame counters.
 *
 * gpu.h includes this after the WebGPU headers, so every renderer source calls
 * the functions below under the WebGPU names. Resources are tracked at creation
 * and release; the per frame counts cost one thread local load when not asked.
 */

#ifndef BOX_RENDERER_INSTRUMENT_H
#define BOX_RENDERER_INSTRUMENT_H

#include "counters.h"
#if defined(_WIN32)
#include "../platform/win32/pthread.h"
#elif !defined(BOX_PLATFORM_WEB)
#include <pthread.h>
#endif

/** Opaque renderer handle */
typedef struct box_renderer_t box_renderer_t;

/** An indirect draw of the frame being counted, resolved from its arguments */
typedef struct box_count_indirect_t {
    WGPUBuffer buffer;             /**< Buffer that holds the arguments */
    uint64_t offset;               /**< Byte of the buffer the arguments start at */
    bool indexed;                  /**< The arguments are those of an indexed draw */
    box_count_topology_t topology; /**< Primitive kind of the bound pipeline */
    int32_t pass;                  /**< Entry of the pass, -1 when not counted */
    int32_t pipeline;              /**< Entry of the pipeline, -1 when not counted */
    uint64_t dst;                  /**< Byte of the read back the arguments land at */
} box_count_indirect_t;

/** A range of an argument buffer the read back of a frame copies */
typedef struct box_count_range_t {
    WGPUBuffer buffer; /**< The buffer */
    uint64_t first;    /**< First byte copied */
    uint64_t end;      /**< Byte past the last one copied */
    uint64_t dst;      /**< Byte of the read back the range lands at */
} box_count_range_t;

/** The frame being counted and the pass that is being recorded */
typedef struct box_count_state_t {
    box_counters_t frame;          /**< Counts of the frame */
    int32_t pass;                  /**< Entry of the current pass, -1 when not counted */
    const void *pipeline;          /**< Pipeline bound in the current pass */
    int32_t pipeline_entry;        /**< Its entry, -1 when not counted */
    box_count_topology_t topology; /**< Its primitive kind */
    const void *groups[BOX_COUNT_SLOT_MAX]; /**< Bind group of each slot of the pass */
    uint64_t offsets[BOX_COUNT_SLOT_MAX];   /**< Hash of the dynamic offsets of each slot */
    box_count_indirect_t *indirect; /**< Indirect draws of the frame */
    uint32_t indirect_count;       /**< Entries of that array */
    uint32_t indirect_capacity;    /**< Entries it can hold */
} box_count_state_t;

/** Counting state of a renderer: the request, the frame in flight and the published one.
 *
 * The world writes request; the renderer latches it when a frame begins. A frame
 * with indirect draws waits for the read back of their arguments; while it waits
 * no other frame is counted. Only a finished frame is copied to published.
 */
typedef struct box_count_t {
    volatile uint32_t request;    /**< BOX_COUNT_ flags the world asked for */
    box_count_state_t state;      /**< The frame being counted */
    bool recording;               /**< A frame is being counted */
    bool pending;                 /**< The counted frame waits for its read back */
    bool busy;                    /**< The read back waits for its map */
    uint32_t wait;                /**< Frames that map has been waited for */
    box_count_range_t *ranges;    /**< Ranges the read back copies */
    uint32_t range_count;         /**< Entries of that array */
    uint32_t range_capacity;      /**< Entries it can hold */
    WGPUBuffer readback;          /**< Mappable copy of the indirect arguments */
    uint64_t readback_capacity;   /**< Bytes that buffer holds */
    uint64_t readback_size;       /**< Bytes the pending read back copied */
    box_counters_t published;     /**< The last finished frame */
    bool has_published;           /**< A frame was published */
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_t lock;         /**< Guards the published frame and GPU timings */
#endif
} box_count_t;

/** Initialize the counting state of a renderer.
 *
 * @param r The renderer
 */
void box_count_init(
    box_renderer_t *r);

/** Wait for a pending read back and release what the counting state holds.
 *
 * @param r The renderer
 */
void box_count_fini(
    box_renderer_t *r);

/** Release the lock of the counting state, after box_count_fini.
 *
 * @param r The renderer
 */
void box_count_destroy(
    box_renderer_t *r);

/** Take the lock that guards what the renderer publishes.
 *
 * @param r The renderer
 */
void box_count_lock(
    box_renderer_t *r);

/** Release the lock that guards what the renderer publishes.
 *
 * @param r The renderer
 */
void box_count_unlock(
    box_renderer_t *r);

/** Start counting a frame when the world asks for it and none is pending.
 *
 * @param r The renderer
 */
void box_count_begin(
    box_renderer_t *r);

/** Encode the copies of the indirect arguments of the frame being counted.
 *
 * @param r The renderer
 * @param encoder The command encoder of the frame, before it is finished
 */
void box_count_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder);

/** Stop counting the frame and publish it, or map its read back first.
 *
 * @param r The renderer
 * @param rendered The frame was rendered, false discards its counts
 */
void box_count_end(
    box_renderer_t *r,
    bool rendered);

/** Counted wgpuRenderPassEncoderDraw.
 *
 * @param pass The pass
 * @param vertices Vertices of an instance
 * @param instances Instances
 * @param first_vertex First vertex
 * @param first_instance First instance
 */
void box_count_draw(
    WGPURenderPassEncoder pass,
    uint32_t vertices,
    uint32_t instances,
    uint32_t first_vertex,
    uint32_t first_instance);

/** Counted wgpuRenderPassEncoderDrawIndexed.
 *
 * @param pass The pass
 * @param indices Indices of an instance
 * @param instances Instances
 * @param first_index First index
 * @param base_vertex Vertex added to every index
 * @param first_instance First instance
 */
void box_count_draw_indexed(
    WGPURenderPassEncoder pass,
    uint32_t indices,
    uint32_t instances,
    uint32_t first_index,
    int32_t base_vertex,
    uint32_t first_instance);

/** Counted wgpuRenderPassEncoderDrawIndirect.
 *
 * @param pass The pass
 * @param buffer Buffer of the arguments
 * @param offset Byte the arguments start at
 */
void box_count_draw_indirect(
    WGPURenderPassEncoder pass,
    WGPUBuffer buffer,
    uint64_t offset);

/** Counted wgpuRenderPassEncoderDrawIndexedIndirect.
 *
 * @param pass The pass
 * @param buffer Buffer of the arguments
 * @param offset Byte the arguments start at
 */
void box_count_draw_indexed_indirect(
    WGPURenderPassEncoder pass,
    WGPUBuffer buffer,
    uint64_t offset);

/** Counted wgpuRenderPassEncoderSetPipeline.
 *
 * @param pass The pass
 * @param pipeline The pipeline
 */
void box_count_set_render_pipeline(
    WGPURenderPassEncoder pass,
    WGPURenderPipeline pipeline);

/** Counted wgpuComputePassEncoderSetPipeline.
 *
 * @param pass The pass
 * @param pipeline The pipeline
 */
void box_count_set_compute_pipeline(
    WGPUComputePassEncoder pass,
    WGPUComputePipeline pipeline);

/** Counted wgpuRenderPassEncoderSetBindGroup.
 *
 * @param pass The pass
 * @param index Slot of the group
 * @param group The group
 * @param count Dynamic offsets
 * @param offsets The dynamic offsets
 */
void box_count_set_render_bind_group(
    WGPURenderPassEncoder pass,
    uint32_t index,
    WGPUBindGroup group,
    size_t count,
    const uint32_t *offsets);

/** Counted wgpuComputePassEncoderSetBindGroup.
 *
 * @param pass The pass
 * @param index Slot of the group
 * @param group The group
 * @param count Dynamic offsets
 * @param offsets The dynamic offsets
 */
void box_count_set_compute_bind_group(
    WGPUComputePassEncoder pass,
    uint32_t index,
    WGPUBindGroup group,
    size_t count,
    const uint32_t *offsets);

/** Counted wgpuComputePassEncoderDispatchWorkgroups.
 *
 * @param pass The pass
 * @param x Workgroups on x
 * @param y Workgroups on y
 * @param z Workgroups on z
 */
void box_count_dispatch(
    WGPUComputePassEncoder pass,
    uint32_t x,
    uint32_t y,
    uint32_t z);

/** Counted wgpuCommandEncoderBeginRenderPass, named by the label of the descriptor.
 *
 * @param encoder The command encoder
 * @param desc The pass descriptor
 * @return The pass encoder
 */
WGPURenderPassEncoder box_count_begin_render_pass(
    WGPUCommandEncoder encoder,
    const WGPURenderPassDescriptor *desc);

/** Counted wgpuCommandEncoderBeginComputePass, named by the label of the descriptor.
 *
 * @param encoder The command encoder
 * @param desc The pass descriptor, NULL for an unnamed pass
 * @return The pass encoder
 */
WGPUComputePassEncoder box_count_begin_compute_pass(
    WGPUCommandEncoder encoder,
    const WGPUComputePassDescriptor *desc);

/** Counted wgpuQueueWriteBuffer.
 *
 * @param queue The queue
 * @param buffer The buffer
 * @param offset Byte of the buffer the data lands at
 * @param data The data
 * @param size Bytes of the data
 */
void box_count_write_buffer(
    WGPUQueue queue,
    WGPUBuffer buffer,
    uint64_t offset,
    const void *data,
    size_t size);

/** Counted wgpuQueueWriteTexture.
 *
 * @param queue The queue
 * @param dst The texture region
 * @param data The texels
 * @param size Bytes of the texels
 * @param layout Layout of the texels
 * @param extent Size of the region
 */
void box_count_write_texture(
    WGPUQueue queue,
    const WGPUTexelCopyTextureInfo *dst,
    const void *data,
    size_t size,
    const WGPUTexelCopyBufferLayout *layout,
    const WGPUExtent3D *extent);

/** Tracked wgpuDeviceCreateBuffer.
 *
 * @param device The device
 * @param desc The descriptor
 * @return The buffer
 */
WGPUBuffer box_count_create_buffer(
    WGPUDevice device,
    const WGPUBufferDescriptor *desc);

/** Tracked wgpuBufferRelease.
 *
 * @param buffer The buffer
 */
void box_count_release_buffer(
    WGPUBuffer buffer);

/** Tracked wgpuDeviceCreateTexture.
 *
 * @param device The device
 * @param desc The descriptor
 * @return The texture
 */
WGPUTexture box_count_create_texture(
    WGPUDevice device,
    const WGPUTextureDescriptor *desc);

/** Tracked wgpuTextureRelease; textures the renderer did not create are ignored.
 *
 * @param texture The texture
 */
void box_count_release_texture(
    WGPUTexture texture);

/** Tracked wgpuDeviceCreateQuerySet.
 *
 * @param device The device
 * @param desc The descriptor
 * @return The query set
 */
WGPUQuerySet box_count_create_query_set(
    WGPUDevice device,
    const WGPUQuerySetDescriptor *desc);

/** Tracked wgpuQuerySetRelease.
 *
 * @param set The query set
 */
void box_count_release_query_set(
    WGPUQuerySet set);

/** Tracked wgpuDeviceCreateRenderPipeline, which keeps its label and topology.
 *
 * @param device The device
 * @param desc The descriptor
 * @return The pipeline
 */
WGPURenderPipeline box_count_create_render_pipeline(
    WGPUDevice device,
    const WGPURenderPipelineDescriptor *desc);

/** Tracked wgpuDeviceCreateComputePipeline, which keeps its label.
 *
 * @param device The device
 * @param desc The descriptor
 * @return The pipeline
 */
WGPUComputePipeline box_count_create_compute_pipeline(
    WGPUDevice device,
    const WGPUComputePipelineDescriptor *desc);

/** Tracked wgpuRenderPipelineRelease.
 *
 * @param pipeline The pipeline
 */
void box_count_release_render_pipeline(
    WGPURenderPipeline pipeline);

/** Tracked wgpuComputePipelineRelease.
 *
 * @param pipeline The pipeline
 */
void box_count_release_compute_pipeline(
    WGPUComputePipeline pipeline);

/** Bytes a texture holds over all its mips, layers and samples.
 *
 * @param desc The descriptor it was created with
 * @return The bytes, block compressed formats rounded up to their blocks
 */
uint64_t box_count_texture_bytes(
    const WGPUTextureDescriptor *desc);

/** Label of a WebGPU string view for a pass or pipeline name.
 *
 * @param label The string view
 * @param fallback Name when the view is empty
 * @param dst The buffer, BOX_COUNT_NAME_MAX characters
 */
void box_count_label(
    WGPUStringView label,
    const char *fallback,
    char *dst);

/** A WebGPU label for a descriptor, from a string literal */
#define BOX_GPU_LABEL(name) { .data = (name), .length = WGPU_STRLEN }

#ifndef BOX_RENDERER_INSTRUMENT_IMPL
#define wgpuRenderPassEncoderDraw box_count_draw
#define wgpuRenderPassEncoderDrawIndexed box_count_draw_indexed
#define wgpuRenderPassEncoderDrawIndirect box_count_draw_indirect
#define wgpuRenderPassEncoderDrawIndexedIndirect box_count_draw_indexed_indirect
#define wgpuRenderPassEncoderSetPipeline box_count_set_render_pipeline
#define wgpuComputePassEncoderSetPipeline box_count_set_compute_pipeline
#define wgpuRenderPassEncoderSetBindGroup box_count_set_render_bind_group
#define wgpuComputePassEncoderSetBindGroup box_count_set_compute_bind_group
#define wgpuComputePassEncoderDispatchWorkgroups box_count_dispatch
#define wgpuCommandEncoderBeginRenderPass box_count_begin_render_pass
#define wgpuCommandEncoderBeginComputePass box_count_begin_compute_pass
#define wgpuQueueWriteBuffer box_count_write_buffer
#define wgpuQueueWriteTexture box_count_write_texture
#define wgpuDeviceCreateBuffer box_count_create_buffer
#define wgpuBufferRelease box_count_release_buffer
#define wgpuDeviceCreateTexture box_count_create_texture
#define wgpuTextureRelease box_count_release_texture
#define wgpuDeviceCreateQuerySet box_count_create_query_set
#define wgpuQuerySetRelease box_count_release_query_set
#define wgpuDeviceCreateRenderPipeline box_count_create_render_pipeline
#define wgpuDeviceCreateComputePipeline box_count_create_compute_pipeline
#define wgpuRenderPipelineRelease box_count_release_render_pipeline
#define wgpuComputePipelineRelease box_count_release_compute_pipeline
#endif

#endif
