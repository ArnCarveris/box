/**
 * @file render/environment.h
 * @brief The environment source contract: cubemaps of environment sources, their handles and
 * versions on the main thread, and the resources that create and write them.
 */

#ifndef BOX_RENDER_ENVIRONMENT_H
#define BOX_RENDER_ENVIRONMENT_H

#include "render.h"

/** Description of a BOX_RENDER_ENVIRONMENT create or update.
 *
 * A create makes the cubemap empty, also over an existing handle, and its source writes it
 * again; an update only changes the filter of the mip levels.
 */
typedef struct box_render_environment_desc_t {
    uint32_t resolution;    /**< Face edge of mip 0 in texels, a power of two from 8 to 1024 */
    uint32_t filter;        /**< EnvironmentFilter of the mip levels */
} box_render_environment_desc_t;

/** Description of a BOX_RENDER_ENVIRONMENT_IMAGE update, followed by its rgba16float texels
 * row by row, the first row at the top of the image (+Y).
 *
 * The backend converts the image into mip 0 of the cubemap of the handle before the
 * environments are processed and reports it written with version.
 */
typedef struct box_render_environment_image_t {
    uint32_t width;         /**< Width of the image in texels */
    uint32_t height;        /**< Height of the image in texels */
    int32_t version;        /**< EnvironmentStatus.version of the content */
    uint32_t pad;           /**< Unused, keeps the texels 8 byte aligned */
} box_render_environment_image_t;

/** Largest width of an environment image; a wider image is scaled down to it */
#define BOX_RENDER_ENVIRONMENT_IMAGE_MAX (4096)

/** Register the system that keeps the cubemaps of the environment sources, writes their
 * EnvironmentStatus.error and gives every lit view the environment of its LightSettings.
 *
 * Runs after the view extract. A cubemap is destroyed when its entity loses Environment, is
 * deleted or gets an invalid resolution.
 *
 * @param world The world
 */
void box_render_environment_init(
    ecs_world_t *world);

/** Get the backend handle of the cubemap of an environment source, assigning it on demand.
 *
 * Main thread, during RenderExtract. When the backend lacks the cubemap, or it has another
 * resolution, it is created empty in the packet of this frame, and the source writes it again.
 *
 * @param world The world
 * @param e Entity with Environment
 * @return The handle, 0 when the entity has no Environment or its resolution is invalid
 */
uint32_t box_render_environment_handle(
    ecs_world_t *world,
    ecs_entity_t e);

/** Get the handle of the cubemap of an environment source without creating it.
 *
 * @param world The world
 * @param e Entity with Environment
 * @return The handle, 0 when the backend has no cubemap of the entity
 */
uint32_t box_render_environment_find(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Get the content the cubemap of an environment source holds.
 *
 * @param world The world
 * @param e Entity with Environment
 * @return The EnvironmentStatus.version last sent into the current cubemap, 0 when it is empty
 *         or does not exist, so the source must write it
 */
int32_t box_render_environment_content(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Record that a source sends new content into its cubemap in the packet of this frame.
 *
 * Increments EnvironmentStatus.version, which is what the source passes to
 * box_gpu_environment_written.
 *
 * @param world The world
 * @param e Entity with Environment, whose handle the source got this frame
 * @return The new version
 */
int32_t box_render_environment_sent(
    ecs_world_t *world,
    ecs_entity_t e);

/** Report why a source has no content; EnvironmentStatus.error shows it unless the resolution
 * is invalid.
 *
 * @param world The world
 * @param e Entity with Environment
 * @param error The reason, copied, NULL when the source has content again
 */
void box_render_environment_error(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *error);

/** Check whether a packet runs the environment shader.
 *
 * @param p The packet
 * @return Whether it carries an environment image or a view draws its environment background
 */
bool box_render_environment_use(
    const box_render_packet_t *p);

/** Check whether a packet may process environments, so it needs the bake shader.
 *
 * @param p The packet
 * @return Whether it creates or updates a cubemap or carries an environment image
 */
bool box_render_environment_processes(
    const box_render_packet_t *p);

/** Register the EnvironmentImage source: its loads on the job system and the system that sends
 * a loaded image into its cubemap once per version.
 *
 * @param world The world
 */
void box_render_environment_image_init(
    ecs_world_t *world);

/** Copy a face of mip 0 of the cubemap of an environment into memory, waiting for the copy.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param handle Handle of the BOX_RENDER_ENVIRONMENT
 * @param face Face in WebGPU cube order: +X, -X, +Y, -Y, +Z, -Z
 * @param out Receives the linear RGBA texels row by row, four floats each
 * @param count Floats out holds
 * @return Whether the cubemap exists, was written and the texels were copied
 */
bool box_render_gpu_read_environment(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t face,
    float *out,
    int32_t count);

/** What the GPU backend did with the cubemap of an environment source */
typedef struct box_render_environment_state_t {
    int32_t written;        /**< Version the source last wrote, 0 while empty */
    int32_t processed;      /**< Version the mips and the SH were last processed for, 0 for
                             *   none */
    uint32_t filter;        /**< EnvironmentFilter the mips were last processed with */
    int32_t runs;           /**< Times the cubemap was processed since it was created */
} box_render_environment_state_t;

/** Get what the GPU backend did with the cubemap of an environment.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_environment.
 *
 * @param world The world
 * @param handle Handle of the BOX_RENDER_ENVIRONMENT
 * @param out Receives the state
 * @return Whether the cubemap exists
 */
bool box_render_gpu_environment_state(
    ecs_world_t *world,
    uint32_t handle,
    box_render_environment_state_t *out);

/** Copy the L2 SH irradiance of an environment into memory, waiting for the copy.
 *
 * The coefficients are in the basis and order of mesh.wgsl, convolved with the clamped cosine
 * as the probe SH: irradiance, which the forward pass divides by pi. Only call this when the
 * main thread renders, after box_render_sync, as box_render_gpu_read_environment.
 *
 * @param world The world
 * @param handle Handle of the BOX_RENDER_ENVIRONMENT
 * @param out Receives the linear rgb coefficients, w unused
 * @return Whether the cubemap exists, was processed and the SH was copied
 */
bool box_render_gpu_read_environment_sh(
    ecs_world_t *world,
    uint32_t handle,
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

/** Take the GPU time of the environment processing the GPU backend of a world measured since
 * the last call.
 *
 * The time of the processing of each packet is measured with the timestamp queries of the
 * probe captures, or from the CPU until the queue reports it done without them.
 *
 * @param world The world
 * @param out Receives the time, its probes the environments processed, zero when nothing
 *            arrived
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_environment_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out);

#endif
