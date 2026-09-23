#ifndef BOX_RENDERER_BAKE_H
#define BOX_RENDERER_BAKE_H

#include <math.h>
#include "packet.h"

#define BOX_SH_COEFS (4)

#define BOX_SH_Y0 (0.282095f)

#define BOX_SH_Y1 (0.488603f)

#define BOX_SH_A0 (0.886227f)

#define BOX_SH_A1 (1.023327f)

#define BOX_BAKE_EPSILON (1e-4f)

#define BOX_BAKE_CUTOFF (0.02f)

typedef struct box_probe_t {
    float r[BOX_SH_COEFS];
    float g[BOX_SH_COEFS];
    float b[BOX_SH_COEFS];
} box_probe_t;

#define BOX_BAKE_CHUNK_DIM (4u)
#define BOX_BAKE_CHUNK_PROBES (64u)
#define BOX_BAKE_NONE UINT32_MAX

typedef struct box_bake_instance_t {
    float center[3];
    float extent[3];
    uint32_t occluder;
    bool alive;
    int8_t mode;
} box_bake_instance_t;

typedef struct box_bake_occluder_t {
    float inverse[3][4];
    float bounds_min[3];
    float bounds_max[3];
} box_bake_occluder_t;

typedef struct box_bake_mesh_t {
    box_vec_t instances;
    float bounds_min[3];
    float bounds_max[3];
} box_bake_mesh_t;

typedef struct box_bake_chunk_t {
    int32_t coord[3];
    uint32_t receivers;
    box_vec_t lights;
    box_vec_t emitters;
    box_vec_t occluders;
    uint32_t next_probe;
    bool dirty;
    bool fresh;
    bool occlusion_dirty;
} box_bake_chunk_t;

typedef struct box_bake_job_t {
    uint32_t chunk;
    float distance;
} box_bake_job_t;

typedef struct box_bake_region_t {
    int32_t coord[3];
    uint32_t visit;
    bool lit;
} box_bake_region_t;

typedef struct box_bake_state_t {
    box_vec_t meshes;
    box_vec_t chunks;
    box_vec_t hash;
    box_vec_t regions;
    box_vec_t region_hash;
    uint32_t region_visit;
    box_vec_t free_chunks;
    box_vec_t occluders;
    box_vec_t free_occluders;
    box_vec_t lights;
    box_vec_t emitters;
    box_vec_t dirty;
    box_vec_t jobs;
    box_vec_t data;
    float spacing;
    float occluder_min_size;
    float eye[3];
    uint32_t chunk_count;
    uint32_t probe_count;
    bool enabled;
    bool data_dirty;
    bool lists_dirty;
} box_bake_state_t;

uint32_t box_bake_find_chunk(const box_bake_state_t *s, const int32_t coord[3]);
bool box_bake_visible_sparse(const box_bake_state_t *s, const float from[3], const float to[3]);
void box_bake_sparse_probe(const box_bake_state_t *s, uint32_t chunk, uint32_t probe, box_probe_t *out);
void box_bake_pack(box_bake_state_t *s);

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

static inline float box_bake_emitter_range(
    const float color[3],
    float radius)
{
    float peak = color[0] > color[1] ? color[0] : color[1];
    peak = peak > color[2] ? peak : color[2];

    return radius * sqrtf(peak / BOX_BAKE_CUTOFF) + radius;
}

void box_bake_state_init(box_bake_state_t *s);
void box_bake_state_fini(box_bake_state_t *s);
void box_bake_state_apply(box_bake_state_t *s, const box_packet_t *p);
uint32_t box_bake_state_take(box_bake_state_t *s, uint32_t budget);
box_bake_mesh_t* box_bake_state_mesh(box_bake_state_t *s, uint32_t mesh);

#endif
