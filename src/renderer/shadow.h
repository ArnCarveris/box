/**
 * @file renderer/shadow.h
 * @brief Cache rule of the static cascade layers, shared by the renderer and the tests.
 *
 * Static casters are rendered into a cached depth layer per cascade, which only
 * has to be rendered again when the matrix of the cascade changed or when a
 * static caster inside the volume the cascade collects casters from moved. The
 * volume is the sphere of the cascade extruded towards the light, which the
 * light frustum of the cascade covers.
 */

#ifndef BOX_RENDERER_SHADOW_H
#define BOX_RENDERER_SHADOW_H

#include <math.h>
#include "packet.h"

/** State of the cached static depth layers of one view */
typedef struct box_shadow_cache_t {
    float matrix[BOX_SHADOW_CASCADE_MAX][16]; /**< Matrix a static layer was rendered with */
    bool dirty[BOX_SHADOW_CASCADE_MAX];       /**< The static layer must be rendered again */
    bool valid[BOX_SHADOW_CASCADE_MAX];       /**< The static layer holds a rendering */
    bool stale[BOX_SHADOW_CASCADE_MAX];       /**< The working layer differs from the static one */
} box_shadow_cache_t;

/** Check whether two cascade matrices hold the same values.
 *
 * @param a The first matrix, sixteen floats
 * @param b The second matrix, sixteen floats
 * @return True when every element matches
 */
static inline bool box_shadow_matrix_equal(
    const float *a,
    const float *b)
{
    bool result = true;
    int i;
    for (i = 0; i < 16 && result; i ++) {
        result = a[i] == b[i];
    }

    return result;
}

/** Check whether a world space sphere can cast into a cascade of a view.
 *
 * @param v The view
 * @param cascade Index of the cascade
 * @param bounds Center xyz and radius of the sphere
 * @return True when the light frustum of the cascade holds the sphere
 */
static inline bool box_shadow_in_cascade(
    const box_view_t *v,
    uint32_t cascade,
    const float *bounds)
{
    const float *sphere = v->shadow_bounds + cascade * 4;
    float radius = sphere[3] + bounds[3];
    float dx = bounds[0] - sphere[0];
    float dy = bounds[1] - sphere[1];
    float dz = bounds[2] - sphere[2];
    float along = dx * v->sun_dir[0] + dy * v->sun_dir[1] + dz * v->sun_dir[2];
    float square = dx * dx + dy * dy + dz * dz - along * along;
    float back = sphere[3] * (1.0f + BOX_SHADOW_EXTRUDE) + bounds[3];

    return square <= radius * radius && along <= radius && along >= -back;
}

/** Mark every cascade whose matrix changed since its layer was rendered.
 *
 * @param c The cache
 * @param v The view of this frame
 */
static inline void box_shadow_cache_view(
    box_shadow_cache_t *c,
    const box_view_t *v)
{
    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        if (!c->valid[i] || !box_shadow_matrix_equal(c->matrix[i], v->shadow_matrix + i * 16)) {
            c->dirty[i] = true;
        }
    }
}

/** Mark every cascade a changed static instance casts into.
 *
 * @param c The cache
 * @param v The view of this frame
 * @param bounds Center xyz and radius of the changed instance
 */
static inline void box_shadow_cache_delta(
    box_shadow_cache_t *c,
    const box_view_t *v,
    const float *bounds)
{
    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        if (!c->dirty[i] && box_shadow_in_cascade(v, i, bounds)) {
            c->dirty[i] = true;
        }
    }
}

/** Mark every cascade of a view, which an added or removed instance asks for.
 *
 * @param c The cache
 * @param v The view of this frame
 */
static inline void box_shadow_cache_all(
    box_shadow_cache_t *c,
    const box_view_t *v)
{
    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        c->dirty[i] = true;
    }
}

/** The cascades whose static layer is rendered this frame, nearest first.
 *
 * A cascade that never held a rendering is rendered as soon as it is dirty, so
 * the first frame of a scene fills every layer; after that at most one cascade
 * with a cached layer is rendered per frame and the others keep drawing with
 * the layer they hold, however many of them the frame made dirty.
 *
 * @param c The cache
 * @param v The view of this frame
 * @return One bit per cascade the frame renders
 */
static inline uint32_t box_shadow_cache_pick(
    const box_shadow_cache_t *c,
    const box_view_t *v)
{
    uint32_t result = 0;
    bool spent = false;
    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        if (!c->dirty[i]) {
            continue;
        }

        if (!c->valid[i]) {
            result |= 1u << i;
            continue;
        }

        if (!spent) {
            result |= 1u << i;
            spent = true;
        }
    }

    return result;
}

/** Record that the static layer of a cascade was rendered with its matrix.
 *
 * @param c The cache
 * @param v The view of this frame
 * @param cascade Index of the cascade
 */
static inline void box_shadow_cache_done(
    box_shadow_cache_t *c,
    const box_view_t *v,
    uint32_t cascade)
{
    memcpy(c->matrix[cascade], v->shadow_matrix + cascade * 16, 16 * sizeof(float));
    c->dirty[cascade] = false;
    c->valid[cascade] = true;
    c->stale[cascade] = true;
}

#endif
