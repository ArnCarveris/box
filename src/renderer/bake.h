/**
 * @file renderer/bake.h
 * @brief Probe bake bookkeeping: chunks of probes around static receivers and their lights.
 *
 * Static instances that receive light register chunks of 4x4x4 probes around them. Each chunk
 * keeps the lights, emitters and occluders that reach it; the state packs them into one word
 * buffer that the bake shader reads, and hands out the probes to rebake each frame.
 */

#ifndef BOX_RENDERER_BAKE_H
#define BOX_RENDERER_BAKE_H

#include <math.h>
#include "packet.h"

/** Spherical harmonic coefficients of one color channel of a probe, L0 and L1 */
#define BOX_SH_COEFS (4)

/** Constant of the L0 spherical harmonic basis function */
#define BOX_SH_Y0 (0.282095f)

/** Constant of the L1 spherical harmonic basis functions */
#define BOX_SH_Y1 (0.488603f)

/** L0 basis constant convolved with the clamped cosine lobe */
#define BOX_SH_A0 (0.886227f)

/** L1 basis constant convolved with the clamped cosine lobe */
#define BOX_SH_A1 (1.023327f)

/** Small distance used to keep bake math away from degenerate cases */
#define BOX_BAKE_EPSILON (1e-4f)

/** Light contribution below which an emitter no longer reaches a probe */
#define BOX_BAKE_CUTOFF (0.02f)

/** Irradiance of one probe as L1 spherical harmonics per color channel */
typedef struct box_probe_t {
    float r[BOX_SH_COEFS]; /**< Coefficients of the red channel */
    float g[BOX_SH_COEFS]; /**< Coefficients of the green channel */
    float b[BOX_SH_COEFS]; /**< Coefficients of the blue channel */
} box_probe_t;

/** Probes of a chunk on one axis */
#define BOX_BAKE_CHUNK_DIM (4u)

/** Probes of a chunk */
#define BOX_BAKE_CHUNK_PROBES (64u)

/** Index that refers to no chunk or occluder */
#define BOX_BAKE_NONE UINT32_MAX

/** World bounds and occluder of one static instance slot */
typedef struct box_bake_instance_t {
    float center[3];    /**< Center of the world space bounding box */
    float extent[3];    /**< Half size of the world space bounding box */
    uint32_t occluder;  /**< Occluder index of the instance, BOX_BAKE_NONE if it occludes nothing */
    bool alive;         /**< The slot holds a visible instance */
    int8_t mode;        /**< Occluder mode: -1 never, 0 automatic, 1 always */
} box_bake_instance_t;

/** Oriented box that blocks light while baking */
typedef struct box_bake_occluder_t {
    float inverse[3][4];  /**< Rows of the world to unit cube transform of the box */
    float bounds_min[3];  /**< Minimum corner of the world space bounding box */
    float bounds_max[3];  /**< Maximum corner of the world space bounding box */
} box_bake_occluder_t;

/** Bake view of one mesh and its static instances */
typedef struct box_bake_mesh_t {
    box_vec_t instances;  /**< box_bake_instance_t per static slot */
    float bounds_min[3];  /**< Minimum corner of the mesh bounds in model space */
    float bounds_max[3];  /**< Maximum corner of the mesh bounds in model space */
} box_bake_mesh_t;

/** A cube of 4x4x4 probes and the lights and occluders that reach it */
typedef struct box_bake_chunk_t {
    int32_t coord[3];      /**< Chunk coordinate on the chunk grid */
    uint32_t receivers;    /**< Receiver instance references to the chunk */
    box_vec_t lights;      /**< uint32_t indices of the lights that reach the chunk */
    box_vec_t emitters;    /**< uint32_t indices of the emitters that reach the chunk */
    box_vec_t occluders;   /**< uint32_t indices of the occluders that overlap the chunk */
    uint32_t next_probe;   /**< Next probe of the chunk to hand out for baking */
    bool dirty;            /**< The probes of the chunk must be rebaked */
    bool fresh;            /**< The probes of the chunk must be zeroed on the GPU */
    bool occlusion_dirty;  /**< The occluders of the chunk changed this update */
} box_bake_chunk_t;

/** A dirty chunk waiting to be baked */
typedef struct box_bake_job_t {
    uint32_t chunk;  /**< Chunk index */
    float distance;  /**< Squared distance from the chunk center to the eye */
} box_bake_job_t;

/** Cached answer to whether any light or emitter reaches a chunk coordinate */
typedef struct box_bake_region_t {
    int32_t coord[3];  /**< Chunk coordinate on the chunk grid */
    uint32_t visit;    /**< Reference pass that last visited the region */
    bool lit;          /**< A light or emitter reaches the region */
} box_bake_region_t;

/** Bake bookkeeping of a renderer */
typedef struct box_bake_state_t {
    box_vec_t meshes;          /**< box_bake_mesh_t per mesh handle */
    box_vec_t chunks;          /**< box_bake_chunk_t, free slots included */
    box_vec_t hash;            /**< uint32_t open addressing table of chunk index plus one */
    box_vec_t regions;         /**< box_bake_region_t cache */
    box_vec_t region_hash;     /**< uint32_t open addressing table of region index plus one */
    uint32_t region_visit;     /**< Counter of the current reference pass */
    box_vec_t free_chunks;     /**< uint32_t indices of free chunk slots */
    box_vec_t occluders;       /**< box_bake_occluder_t, free slots included */
    box_vec_t free_occluders;  /**< uint32_t indices of free occluder slots */
    box_vec_t lights;          /**< box_light_t of every static light */
    box_vec_t emitters;        /**< box_emitter_t of every static emitter */
    box_vec_t dirty;           /**< box_bake_job_t of the chunks that wait to be baked */
    box_vec_t jobs;            /**< uint32_t probe indices to bake this frame */
    box_vec_t data;            /**< uint32_t words of the packed buffer the bake shader reads */
    float spacing;             /**< World units between two probes */
    float occluder_min_size;   /**< Minimum size of two axes of an automatic occluder */
    float eye[3];              /**< Eye position that orders the dirty chunks */
    uint32_t chunk_count;      /**< Chunks in use */
    uint32_t probe_count;      /**< Probes of the chunks in use */
    bool enabled;              /**< Baking is enabled */
    bool data_dirty;           /**< The packed buffer must be rebuilt and uploaded */
    bool lists_dirty;          /**< The light and emitter lists of the chunks must be rebuilt */
} box_bake_state_t;

/** Find the chunk at a chunk coordinate.
 *
 * @param s The bake state
 * @param coord Chunk coordinate
 * @return The chunk index, BOX_BAKE_NONE if no chunk exists there
 */
uint32_t box_bake_find_chunk(
    const box_bake_state_t *s,
    const int32_t coord[3]);

/** Test whether no occluder blocks a segment, walking the chunks it crosses.
 *
 * @param s The bake state
 * @param from Start of the segment
 * @param to End of the segment
 * @return True if no occluder intersects the segment
 */
bool box_bake_visible_sparse(
    const box_bake_state_t *s,
    const float from[3],
    const float to[3]);

/** Bake one probe on the CPU, the reference for the bake shader.
 *
 * A probe inside an occluder gets -1 in its first red coefficient.
 *
 * @param s The bake state
 * @param chunk Chunk index
 * @param probe Probe index inside the chunk
 * @param out The probe to fill
 */
void box_bake_sparse_probe(
    const box_bake_state_t *s,
    uint32_t chunk,
    uint32_t probe,
    box_probe_t *out);

/** Pack chunks, lights, occluders and probe cells into the data buffer.
 *
 * @param s The bake state
 */
void box_bake_pack(
    box_bake_state_t *s);

/** Add a radiance sample from a direction to a probe.
 *
 * @param probe The probe to add to
 * @param dir Normalized direction towards the source
 * @param value Radiance of the sample per color channel
 */
static inline void box_sh_add(
    box_probe_t *probe,
    const float dir[3],
    const float value[3])
{
    float basis[BOX_SH_COEFS] = {
        BOX_SH_Y0, BOX_SH_Y1 * dir[1], BOX_SH_Y1 * dir[2], BOX_SH_Y1 * dir[0] };
    int i;
    for (i = 0; i < BOX_SH_COEFS; i ++) {
        probe->r[i] += value[0] * basis[i];
        probe->g[i] += value[1] * basis[i];
        probe->b[i] += value[2] * basis[i];
    }
}

/** Evaluate the irradiance of a probe for a surface normal.
 *
 * @param probe The probe
 * @param normal Normalized surface normal
 * @param out Irradiance per color channel, clamped to zero
 */
static inline void box_sh_eval(
    const box_probe_t *probe,
    const float normal[3],
    float out[3])
{
    float basis[BOX_SH_COEFS] = {
        BOX_SH_A0, BOX_SH_A1 * normal[1], BOX_SH_A1 * normal[2], BOX_SH_A1 * normal[0] };
    const float *channel[3] = { probe->r, probe->g, probe->b };
    int c;
    for (c = 0; c < 3; c ++) {
        float sum = 0;
        int i;
        for (i = 0; i < BOX_SH_COEFS; i ++) {
            sum += channel[c][i] * basis[i];
        }

        out[c] = sum > 0 ? sum : 0;
    }
}

/** Distance at which the light of an emitter drops below BOX_BAKE_CUTOFF.
 *
 * @param color Emitted color
 * @param radius Radius of the emitter
 * @return The range of the emitter
 */
static inline float box_bake_emitter_range(
    const float color[3],
    float radius)
{
    float peak = color[0] > color[1] ? color[0] : color[1];
    peak = peak > color[2] ? peak : color[2];

    return radius * sqrtf(peak / BOX_BAKE_CUTOFF) + radius;
}

/** Initialize an empty bake state.
 *
 * @param s The bake state
 */
void box_bake_state_init(
    box_bake_state_t *s);

/** Release the storage of a bake state.
 *
 * @param s The bake state
 */
void box_bake_state_fini(
    box_bake_state_t *s);

/** Apply the meshes, static instances and bake lights of a packet.
 *
 * @param s The bake state
 * @param p The packet
 */
void box_bake_state_apply(
    box_bake_state_t *s,
    const box_packet_t *p);

/** Hand out the probes to bake this frame, nearest chunks first.
 *
 * @param s The bake state
 * @param budget Maximum number of probes to hand out
 * @return The number of probe indices written to jobs
 */
uint32_t box_bake_state_take(
    box_bake_state_t *s,
    uint32_t budget);

/** Get the bake mesh of a mesh handle, creating it when missing.
 *
 * @param s The bake state
 * @param mesh Mesh handle
 * @return The bake mesh
 */
box_bake_mesh_t* box_bake_state_mesh(
    box_bake_state_t *s,
    uint32_t mesh);

#endif
