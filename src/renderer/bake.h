/**
 * @file renderer/bake.h
 * @brief Probe, occupancy grid and spherical harmonics math of the baked lighting.
 *
 * The same math runs in the bake compute shader, so a change here changes both
 * sides. The state API is implemented by bake.c and holds no GPU types, which
 * lets tests and benchmarks drive the invalidation without a device.
 */

#ifndef BOX_RENDERER_BAKE_H
#define BOX_RENDERER_BAKE_H

#include <math.h>
#include "packet.h"

/** Coefficients an order 1 spherical harmonics probe holds per color channel */
#define BOX_SH_COEFS (4)

/** Basis value of the constant band */
#define BOX_SH_Y0 (0.282095f)

/** Basis factor of the linear band */
#define BOX_SH_Y1 (0.488603f)

/** Irradiance reconstruction factor of the constant band */
#define BOX_SH_A0 (0.886227f)

/** Irradiance reconstruction factor of the linear band */
#define BOX_SH_A1 (1.023327f)

/** Smallest distance the bake math accepts */
#define BOX_BAKE_EPSILON (1e-4f)

/** Fraction of an emitter's radiance its range reaches down to */
#define BOX_BAKE_CUTOFF (0.02f)

/** Order 1 spherical harmonics probe, laid out as the GPU storage buffer expects it */
typedef struct box_probe_t {
    float r[BOX_SH_COEFS];  /**< Red coefficients: constant, y, z and x band */
    float g[BOX_SH_COEFS];  /**< Green coefficients */
    float b[BOX_SH_COEFS];  /**< Blue coefficients */
} box_probe_t;

/** World bounds and probe grid of one static instance, the descriptor of its probes */
typedef struct box_probe_desc_t {
    float position[4];  /**< World center of the bounds, w is the bounds radius */
    float extent[4];    /**< World half extents of the bounds, w is 1 for a live probe */
    float span[3][4];   /**< World offset across the bounds along each local axis, w unused */
    uint32_t grid[4];   /**< Pool index of the first probe, then the probes on each axis */
} box_probe_desc_t;

/** Probes on one axis of the grid of an instance, at least one.
 *
 * @param d The descriptor
 * @param axis The axis
 * @return The probes on the axis
 */
static inline uint32_t box_probe_dim(
    const box_probe_desc_t *d,
    int axis)
{
    uint32_t result = d->grid[axis + 1];

    return result ? result : 1u;
}

/** Probes the grid of an instance holds, zero when its slot is empty.
 *
 * @param d The descriptor
 * @return The probes
 */
static inline uint32_t box_probe_count(
    const box_probe_desc_t *d)
{
    uint32_t result = 0;
    if (d->extent[3] != 0) {
        result = box_probe_dim(d, 0) * box_probe_dim(d, 1) * box_probe_dim(d, 2);
    }

    return result;
}

/** Probes an instance gets on each axis of its world bounds.
 *
 * @param extent World half extents of the instance
 * @param spacing World units between two probes
 * @param max Probes an axis gets at most
 * @param out The probes on each axis
 */
static inline void box_probe_dims(
    const float extent[3],
    float spacing,
    uint32_t max,
    uint32_t out[3])
{
    float step = spacing > BOX_BAKE_EPSILON ? spacing : BOX_BAKE_EPSILON;
    uint32_t limit = max ? max : 1u;
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float count = ceilf(2.0f * extent[axis] / step - BOX_BAKE_EPSILON);
        uint32_t n = count > 1.0f ? (uint32_t)count : 1u;
        out[axis] = n < limit ? n : limit;
    }
}

/** Grid coordinates of a probe of an instance.
 *
 * @param d The descriptor
 * @param index Probe in the grid of the instance
 * @param out The coordinate on each axis
 */
static inline void box_probe_grid_coord(
    const box_probe_desc_t *d,
    uint32_t index,
    uint32_t out[3])
{
    uint32_t nx = box_probe_dim(d, 0);
    uint32_t ny = box_probe_dim(d, 1);
    out[0] = index % nx;
    out[1] = (index / nx) % ny;
    out[2] = index / (nx * ny);
}

/** World position of one probe of the grid of an instance.
 *
 * @param d The descriptor
 * @param index Probe in the grid of the instance
 * @param out The world position
 */
static inline void box_probe_position(
    const box_probe_desc_t *d,
    uint32_t index,
    float out[3])
{
    uint32_t coord[3];
    box_probe_grid_coord(d, index, coord);

    int axis;
    for (axis = 0; axis < 3; axis ++) {
        out[axis] = d->position[axis];
    }

    int a;
    for (a = 0; a < 3; a ++) {
        uint32_t dim = box_probe_dim(d, a);
        float u = dim > 1 ? (float)coord[a] / ((float)dim - 1.0f) - 0.5f : 0;
        for (axis = 0; axis < 3; axis ++) {
            out[axis] += d->span[a][axis] * u;
        }
    }
}

/** Half extents of the cell one probe of an instance covers.
 *
 * @param d The descriptor
 * @param out The half extents
 */
static inline void box_probe_own(
    const box_probe_desc_t *d,
    float out[3])
{
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        out[axis] = d->extent[axis] / (float)box_probe_dim(d, axis);
    }
}

/** Grid coordinate a world position has in the probe grid of an instance.
 *
 * @param d The descriptor
 * @param point The world position
 * @param out The coordinate on each axis, unclamped
 */
static inline void box_probe_coord(
    const box_probe_desc_t *d,
    const float point[3],
    float out[3])
{
    float origin[3];
    box_probe_position(d, 0, origin);

    int a;
    for (a = 0; a < 3; a ++) {
        const float *span = d->span[a];
        float len = span[0] * span[0] + span[1] * span[1] + span[2] * span[2];
        float dot = (point[0] - origin[0]) * span[0] + (point[1] - origin[1]) * span[1] +
            (point[2] - origin[2]) * span[2];
        out[a] = len > BOX_BAKE_EPSILON ? dot / len * ((float)box_probe_dim(d, a) - 1.0f) : 0;
    }
}

/** Pool indices and trilinear weights of the eight probes around a grid coordinate.
 *
 * @param d The descriptor
 * @param coord The grid coordinate
 * @param index Pool index of every corner
 * @param weight Weight of every corner, summing to one
 */
static inline void box_probe_weights(
    const box_probe_desc_t *d,
    const float coord[3],
    uint32_t index[8],
    float weight[8])
{
    uint32_t dim[3];
    uint32_t lo[3];
    uint32_t hi[3];
    float f[3];
    int a;
    for (a = 0; a < 3; a ++) {
        dim[a] = box_probe_dim(d, a);
        float last = (float)dim[a] - 1.0f;
        float t = coord[a] < 0 ? 0 : (coord[a] > last ? last : coord[a]);
        float base = floorf(t);
        lo[a] = (uint32_t)base;
        hi[a] = lo[a] + 1u < dim[a] ? lo[a] + 1u : lo[a];
        f[a] = t - base;
    }

    int i;
    for (i = 0; i < 8; i ++) {
        uint32_t x = (i & 1) ? hi[0] : lo[0];
        uint32_t y = (i & 2) ? hi[1] : lo[1];
        uint32_t z = (i & 4) ? hi[2] : lo[2];
        index[i] = d->grid[0] + x + y * dim[0] + z * dim[0] * dim[1];
        weight[i] = ((i & 1) ? f[0] : 1.0f - f[0]) * ((i & 2) ? f[1] : 1.0f - f[1]) *
            ((i & 4) ? f[2] : 1.0f - f[2]);
    }
}

/** Interpolate the probe grid of an instance, the CPU twin of the mesh shader.
 *
 * @param d The descriptor
 * @param pool The probe pool of the mesh
 * @param coord The grid coordinate
 * @param out The interpolated probe
 */
static inline void box_probe_sample(
    const box_probe_desc_t *d,
    const box_probe_t *pool,
    const float coord[3],
    box_probe_t *out)
{
    uint32_t index[8];
    float weight[8];
    box_probe_weights(d, coord, index, weight);
    memset(out, 0, sizeof(box_probe_t));

    int i;
    for (i = 0; i < 8; i ++) {
        const box_probe_t *p = &pool[index[i]];
        int c;
        for (c = 0; c < BOX_SH_COEFS; c ++) {
            out->r[c] += p->r[c] * weight[i];
            out->g[c] += p->g[c] * weight[i];
            out->b[c] += p->b[c] * weight[i];
        }
    }
}

/** Voxel grid the occupancy bits of the static scene cover */
typedef struct box_voxel_grid_t {
    float min[4];      /**< World minimum corner, w unused */
    float cell[4];     /**< World size of one voxel, w is its diagonal */
    uint32_t dim[4];   /**< Voxels on each axis, w unused */
} box_voxel_grid_t;

/** Everything a probe is baked from, the input of the CPU twin of the bake pass */
typedef struct box_bake_scene_t {
    const box_probe_desc_t *probes; /**< Descriptor per probe */
    uint32_t probe_count;           /**< Probes in the array */
    const box_light_t *lights;      /**< Static lights */
    uint32_t light_count;           /**< Lights in the array */
    const box_emitter_t *emitters;  /**< Static emissive instances */
    uint32_t emitter_count;         /**< Emitters in the array */
    const uint32_t *occupancy;      /**< Occupancy bits of the grid */
    box_voxel_grid_t grid;          /**< Grid the occupancy bits cover */
    uint32_t steps;                 /**< Ray march samples per visibility test */
} box_bake_scene_t;

/** Region of the scene a change invalidated */
typedef struct box_bake_sphere_t {
    float center[3];  /**< World center of the region */
    float radius;     /**< Radius of the region */
} box_bake_sphere_t;

/** One instance whose probes wait to be baked */
typedef struct box_bake_job_t {
    uint32_t mesh;    /**< Mesh handle */
    uint32_t slot;    /**< Static instance slot */
    float distance;   /**< Squared distance of the instance to the camera */
} box_bake_job_t;

/** One probe the bake pass writes this frame */
typedef struct box_bake_slot_t {
    uint32_t slot;   /**< Static instance slot the probe belongs to */
    uint32_t index;  /**< Probe in the grid of the instance */
} box_bake_slot_t;

/** Probe descriptors and probe pool of one mesh, mirroring its static instance slots */
typedef struct box_bake_mesh_t {
    box_vec_t descs;      /**< box_probe_desc_t per slot */
    box_vec_t dirty;      /**< uint8_t, slot is queued for baking */
    box_vec_t jobs;       /**< box_bake_slot_t probes baked this frame */
    float bounds_min[3];  /**< Local bounds minimum of the mesh */
    float bounds_max[3];  /**< Local bounds maximum of the mesh */
    uint32_t probe_count; /**< Probes the pool of the mesh holds */
    uint32_t upload_lo;   /**< First descriptor changed since the last upload */
    uint32_t upload_hi;   /**< Descriptor past the last one changed */
    uint32_t stale_lo;    /**< First descriptor parked because no light reaches it */
    uint32_t stale_hi;    /**< Descriptor past the last parked one */
    bool pool_dirty;      /**< The pool must be laid out again */
} box_bake_mesh_t;

/** Probe bookkeeping of the renderer: descriptors, the dirty set and the last inputs */
typedef struct box_bake_state_t {
    box_vec_t meshes;      /**< box_bake_mesh_t indexed by mesh handle */
    box_vec_t dirty;       /**< box_bake_job_t probes waiting to be baked */
    box_vec_t lights;      /**< box_light_t static lights of the previous packet */
    box_vec_t emitters;    /**< box_emitter_t of the previous packet */
    box_vec_t changed;     /**< box_bake_sphere_t regions invalidated this frame */
    box_voxel_grid_t grid; /**< Grid the occupancy pass covers */
    float bounds_min[3];   /**< World minimum of the static scene */
    float bounds_max[3];   /**< World maximum of the static scene */
    float influence;       /**< Radius a changed static instance invalidates */
    float eye[3];          /**< Camera position the dirty set is sorted by */
    float spacing;         /**< World units between two probes of an instance */
    uint32_t probe_max;    /**< Probes an instance gets on one axis */
    uint32_t probe_count;  /**< Probes over every mesh */
    bool has_bounds;       /**< A static instance contributed to the bounds */
    bool grid_dirty;       /**< The occupancy grid must be filled again */
    bool enabled;          /**< Baking was enabled by the previous packet */
} box_bake_state_t;

/** Words the occupancy buffer of a grid needs.
 *
 * @param grid The grid
 * @return The number of 32 bit words
 */
static inline uint32_t box_voxel_words(
    const box_voxel_grid_t *grid)
{
    return (grid->dim[0] * grid->dim[1] * grid->dim[2] + 31u) / 32u;
}

/** Build a grid that covers world bounds with a number of voxels.
 *
 * @param min World minimum corner
 * @param max World maximum corner
 * @param dim Voxels on each axis
 * @param out The grid to fill
 */
static inline void box_voxel_grid_build(
    const float min[3],
    const float max[3],
    const uint32_t dim[3],
    box_voxel_grid_t *out)
{
    float diagonal = 0;
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float size = max[axis] - min[axis];
        size = size > BOX_BAKE_EPSILON ? size : BOX_BAKE_EPSILON;
        out->min[axis] = min[axis];
        out->dim[axis] = dim[axis] ? dim[axis] : 1u;
        out->cell[axis] = size / (float)out->dim[axis];
        diagonal += out->cell[axis] * out->cell[axis];
    }

    out->min[3] = 0;
    out->dim[3] = 0;
    out->cell[3] = sqrtf(diagonal);
}

/** Voxel a world point falls in.
 *
 * @param grid The grid
 * @param point The world point
 * @param out The three voxel coordinates
 * @return true when the point is inside the grid
 */
static inline bool box_voxel_coord(
    const box_voxel_grid_t *grid,
    const float point[3],
    uint32_t out[3])
{
    bool result = true;
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float t = (point[axis] - grid->min[axis]) / grid->cell[axis];
        if (t < 0 || t >= (float)grid->dim[axis]) {
            result = false;
            break;
        }

        out[axis] = (uint32_t)t;
    }

    return result;
}

/** Test the occupancy bit of a world point.
 *
 * @param grid The grid
 * @param bits The occupancy bits
 * @param point The world point
 * @return true when the voxel of the point holds static geometry
 */
static inline bool box_voxel_solid(
    const box_voxel_grid_t *grid,
    const uint32_t *bits,
    const float point[3])
{
    bool result = false;
    uint32_t c[3];
    if (box_voxel_coord(grid, point, c)) {
        uint32_t index = c[0] + c[1] * grid->dim[0] + c[2] * grid->dim[0] * grid->dim[1];
        result = (bits[index >> 5u] & (1u << (index & 31u))) != 0;
    }

    return result;
}

/** Set the occupancy bits of every voxel a world box touches.
 *
 * @param grid The grid
 * @param bits The occupancy bits
 * @param lo Minimum corner of the box
 * @param hi Maximum corner of the box
 */
static inline void box_voxel_mark(
    const box_voxel_grid_t *grid,
    uint32_t *bits,
    const float lo[3],
    const float hi[3])
{
    uint32_t first[3];
    uint32_t last[3];
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float a = (lo[axis] - grid->min[axis]) / grid->cell[axis];
        float b = (hi[axis] - grid->min[axis]) / grid->cell[axis];
        float limit = (float)grid->dim[axis] - 1.0f;
        a = a < 0 ? 0 : (a > limit ? limit : a);
        b = b < 0 ? 0 : (b > limit ? limit : b);
        first[axis] = (uint32_t)a;
        last[axis] = (uint32_t)b;
    }

    uint32_t z;
    for (z = first[2]; z <= last[2]; z ++) {
        uint32_t y;
        for (y = first[1]; y <= last[1]; y ++) {
            uint32_t x;
            for (x = first[0]; x <= last[0]; x ++) {
                uint32_t index = x + y * grid->dim[0] + z * grid->dim[0] * grid->dim[1];
                bits[index >> 5u] |= 1u << (index & 31u);
            }
        }
    }
}

/** Fill the occupancy bits of a grid from the bounds of every probe.
 *
 * @param grid The grid
 * @param descs Descriptor per probe
 * @param count Probes in the array
 * @param bits The occupancy bits, cleared first
 */
static inline void box_bake_grid_build(
    const box_voxel_grid_t *grid,
    const box_probe_desc_t *descs,
    uint32_t count,
    uint32_t *bits)
{
    memset(bits, 0, (size_t)box_voxel_words(grid) * sizeof(uint32_t));

    uint32_t i;
    for (i = 0; i < count; i ++) {
        if (descs[i].extent[3] == 0) {
            continue;
        }

        float lo[3];
        float hi[3];
        int axis;
        for (axis = 0; axis < 3; axis ++) {
            lo[axis] = descs[i].position[axis] - descs[i].extent[axis];
            hi[axis] = descs[i].position[axis] + descs[i].extent[axis];
        }

        box_voxel_mark(grid, bits, lo, hi);
    }
}

/** Distance from the center of a box to its boundary along a direction.
 *
 * @param extent Half extents of the box
 * @param dir Unit direction
 * @return The distance
 */
static inline float box_bake_exit(
    const float extent[3],
    const float dir[3])
{
    float result = 0;
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float d = dir[axis] < 0 ? -dir[axis] : dir[axis];
        if (d > BOX_BAKE_EPSILON) {
            float t = extent[axis] / d;
            result = (result == 0 || t < result) ? t : result;
        }
    }

    return result;
}

/** March the occupancy grid between a probe and a light.
 *
 * The march starts outside the bounds of the probe's own instance and stops
 * before the light, so neither occludes the sample.
 *
 * @param grid The grid
 * @param bits The occupancy bits
 * @param from World position of the probe
 * @param own Half extents of the instance the probe belongs to
 * @param to World position of the light
 * @param radius Radius of the light
 * @param steps Samples to take
 * @return 1 when the light is visible, 0 when a voxel blocks it
 */
static inline float box_bake_visible(
    const box_voxel_grid_t *grid,
    const uint32_t *bits,
    const float from[3],
    const float own[3],
    const float to[3],
    float radius,
    uint32_t steps)
{
    float result = 1.0f;
    float delta[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    float dist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    if (dist < BOX_BAKE_EPSILON || !steps) {
        goto done;
    }

    float dir[3] = { delta[0] / dist, delta[1] / dist, delta[2] / dist };
    float margin = grid->cell[3];
    float t0 = box_bake_exit(own, dir) + margin;
    float t1 = dist - radius - margin;
    if (t1 <= t0) {
        goto done;
    }

    float step = (t1 - t0) / (float)steps;
    uint32_t i;
    for (i = 0; i < steps; i ++) {
        float t = t0 + ((float)i + 0.5f) * step;
        float point[3] = {
            from[0] + dir[0] * t, from[1] + dir[1] * t, from[2] + dir[2] * t };
        if (box_voxel_solid(grid, bits, point)) {
            result = 0;
            break;
        }
    }

done:
    return result;
}

/** Add the radiance arriving from a direction to a probe.
 *
 * @param probe The probe
 * @param dir Unit direction the radiance arrives from
 * @param value Linear radiance per channel
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

/** Reconstruct the irradiance of a probe for a surface normal.
 *
 * @param probe The probe
 * @param normal Unit surface normal
 * @param out Linear irradiance per channel, never negative
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

/** Radiance a static light delivers to a world position.
 *
 * @param light The light
 * @param position The world position
 * @param dir Unit direction from the position to the light
 * @param value Linear radiance per channel
 * @return true when the position is inside the range of the light
 */
static inline bool box_bake_light_sample(
    const box_light_t *light,
    const float position[3],
    float dir[3],
    float value[3])
{
    bool result = false;
    float delta[3] = {
        light->position[0] - position[0],
        light->position[1] - position[1],
        light->position[2] - position[2] };
    float dist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    float range = light->range > BOX_BAKE_EPSILON ? light->range : BOX_BAKE_EPSILON;
    if (dist >= range) {
        goto done;
    }

    float inv = 1.0f / (dist > BOX_BAKE_EPSILON ? dist : BOX_BAKE_EPSILON);
    dir[0] = delta[0] * inv;
    dir[1] = delta[1] * inv;
    dir[2] = delta[2] * inv;

    float ratio = dist / range;
    float window = 1.0f - ratio * ratio * ratio * ratio;
    window = window > 0 ? window : 0;
    float atten = window * window / (dist * dist + 1.0f);
    if (light->type == BOX_LIGHT_SPOT) {
        float cd = -(dir[0] * light->direction[0] + dir[1] * light->direction[1] +
            dir[2] * light->direction[2]);
        float span = light->cos_inner - light->cos_outer;
        span = span > BOX_BAKE_EPSILON ? span : BOX_BAKE_EPSILON;
        float cone = (cd - light->cos_outer) / span;
        cone = cone < 0 ? 0 : (cone > 1.0f ? 1.0f : cone);
        atten *= cone * cone;
    }

    value[0] = light->color[0] * light->intensity * atten;
    value[1] = light->color[1] * light->intensity * atten;
    value[2] = light->color[2] * light->intensity * atten;
    result = atten > 0;
done:
    return result;
}

/** Radiance an emissive static instance delivers to a world position.
 *
 * @param emitter The emitter
 * @param position The world position
 * @param dir Unit direction from the position to the emitter
 * @param value Linear radiance per channel
 * @return true when the position is inside the range of the emitter
 */
static inline bool box_bake_emitter_sample(
    const box_emitter_t *emitter,
    const float position[3],
    float dir[3],
    float value[3])
{
    bool result = false;
    float delta[3] = {
        emitter->position[0] - position[0],
        emitter->position[1] - position[1],
        emitter->position[2] - position[2] };
    float dist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    float range = emitter->range > BOX_BAKE_EPSILON ? emitter->range : BOX_BAKE_EPSILON;
    if (dist >= range) {
        goto done;
    }

    float inv = 1.0f / (dist > BOX_BAKE_EPSILON ? dist : BOX_BAKE_EPSILON);
    dir[0] = delta[0] * inv;
    dir[1] = delta[1] * inv;
    dir[2] = delta[2] * inv;

    float radius = emitter->radius > BOX_BAKE_EPSILON ? emitter->radius : BOX_BAKE_EPSILON;
    float reach = dist > radius ? dist : radius;
    float ratio = dist / range;
    float window = 1.0f - ratio * ratio * ratio * ratio;
    window = window > 0 ? window : 0;
    float atten = window * window * radius * radius / (reach * reach);
    value[0] = emitter->color[0] * atten;
    value[1] = emitter->color[1] * atten;
    value[2] = emitter->color[2] * atten;
    result = atten > 0;
done:
    return result;
}

/** Range at which the radiance of an emitter falls below the cutoff.
 *
 * @param color Linear emissive color times its strength
 * @param radius Radius of the emitter
 * @return The range
 */
static inline float box_bake_emitter_range(
    const float color[3],
    float radius)
{
    float peak = color[0] > color[1] ? color[0] : color[1];
    peak = peak > color[2] ? peak : color[2];

    return radius * sqrtf(peak / BOX_BAKE_CUTOFF) + radius;
}

/** Bake one probe of an instance, the CPU twin of the bake compute shader.
 *
 * @param scene The scene
 * @param desc Descriptor of the instance
 * @param index Probe in the grid of the instance
 * @param out The probe to fill
 */
static inline void box_bake_probe_at(
    const box_bake_scene_t *scene,
    const box_probe_desc_t *desc,
    uint32_t index,
    box_probe_t *out)
{
    float position[3];
    float own[3];
    box_probe_position(desc, index, position);
    box_probe_own(desc, own);
    memset(out, 0, sizeof(box_probe_t));

    uint32_t i;
    for (i = 0; i < scene->light_count; i ++) {
        const box_light_t *light = &scene->lights[i];
        float dir[3];
        float value[3];
        if (!box_bake_light_sample(light, position, dir, value)) {
            continue;
        }

        float vis = box_bake_visible(&scene->grid, scene->occupancy, position,
            own, light->position, 0, scene->steps);
        value[0] *= vis;
        value[1] *= vis;
        value[2] *= vis;
        box_sh_add(out, dir, value);
    }

    for (i = 0; i < scene->emitter_count; i ++) {
        const box_emitter_t *emitter = &scene->emitters[i];
        float dir[3];
        float value[3];
        if (!box_bake_emitter_sample(emitter, position, dir, value)) {
            continue;
        }

        float vis = box_bake_visible(&scene->grid, scene->occupancy, position,
            own, emitter->position, emitter->radius, scene->steps);
        value[0] *= vis;
        value[1] *= vis;
        value[2] *= vis;
        box_sh_add(out, dir, value);
    }
}

/** Bake the first probe of an instance of a scene.
 *
 * @param scene The scene
 * @param probe Index of the instance in the descriptor array
 * @param out The probe to fill
 */
static inline void box_bake_probe_cpu(
    const box_bake_scene_t *scene,
    uint32_t probe,
    box_probe_t *out)
{
    box_bake_probe_at(scene, &scene->probes[probe], 0, out);
}

/** Initialize the probe bookkeeping.
 *
 * @param s The state
 */
void box_bake_state_init(
    box_bake_state_t *s);

/** Release the probe bookkeeping.
 *
 * @param s The state
 */
void box_bake_state_fini(
    box_bake_state_t *s);

/** Apply a packet: update probe descriptors and mark the probes it invalidates.
 *
 * @param s The state
 * @param p The packet
 */
void box_bake_state_apply(
    box_bake_state_t *s,
    const box_packet_t *p);

/** Move the probes nearest to the camera out of the dirty set into per mesh job lists.
 *
 * @param s The state
 * @param budget Probes to take at most
 * @return The probes taken
 */
uint32_t box_bake_state_take(
    box_bake_state_t *s,
    uint32_t budget);

/** Mesh entry of the state, growing the table as needed.
 *
 * @param s The state
 * @param mesh Mesh handle
 * @return The entry
 */
box_bake_mesh_t* box_bake_state_mesh(
    box_bake_state_t *s,
    uint32_t mesh);

#endif
