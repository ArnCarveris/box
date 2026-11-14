/**
 * @file render/batch_bounds.c
 * @brief Local and world bounds of batches.
 *
 * The local bounds of a batch with parametric surfaces are the box its cull jobs test each
 * instance with. The world bounds hold the world box of every instance, which the views that
 * cull batches by their bounds (CullSettings.bounds) test before drawing the batch.
 */

#include "draw.h"
#include <float.h>
#include <math.h>

void box_draws_bounds(
    box_draw_batch_t *b,
    box_mesh_cache_t *cache)
{
    const box_render_surface_t *surfaces = ecs_vec_first(&b->surfaces);
    const box_mesh_t *mesh = NULL;
    int32_t i, count = ecs_vec_count(&b->surfaces);
    vec3 lo = { FLT_MAX, FLT_MAX, FLT_MAX }, hi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool plain = false;
    b->bounds_dirty = false;
    b->bounded = false;
    for (i = 0; i < count; i ++) {
        const float *p = surfaces[i].p[0];
        if (p[0] == 0.0f) {
            plain = true;
            continue;
        }

        vec3 c = { surfaces[i].p[22][0], surfaces[i].p[22][1], surfaces[i].p[22][2] };
        vec3 e = { surfaces[i].p[23][0], surfaces[i].p[23][1], surfaces[i].p[23][2] };
        vec3 a, z;
        glm_vec3_sub(c, e, a);
        glm_vec3_add(c, e, z);
        glm_vec3_minv(lo, a, lo);
        glm_vec3_maxv(hi, z, hi);
        b->bounded = true;
    }

    if (b->bounded && plain) {
        mesh = box_mesh_cache_get(cache, b->key.mesh);
    }

    if (mesh) {
        glm_vec3_minv(lo, ECS_CONST_CAST(float*, mesh->min), lo);
        glm_vec3_maxv(hi, ECS_CONST_CAST(float*, mesh->max), hi);
    }

    if (b->bounded) {
        glm_vec3_center(lo, hi, b->center);
        glm_vec3_sub(hi, b->center, b->extent);
    }
}

/** Check whether a batch gets world bounds for the views that cull batches by them: an opaque
 * mesh batch drawn by every view. Blended, transmissive and 2D batches are always drawn.
 *
 * @param b The batch
 * @return Whether it does
 */
static bool box_draws_world_bounded(
    const box_draw_batch_t *b)
{
    return box_render_is_mesh(b->key.pipeline) && b->key.blend == Opaque &&
        !b->key.transmissive && !b->key.water && !b->key.atlas;
}

void box_draws_world_bounds(
    box_draw_batch_t *b,
    box_mesh_cache_t *cache,
    bool wanted)
{
    const box_render_transform_t *t = ecs_vec_first(&b->transforms);
    int32_t i, r, count = ecs_vec_count(&b->transforms);
    float local[6], radius = 0.0f;
    vec3 lo = { FLT_MAX, FLT_MAX, FLT_MAX }, hi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool finite = true;
    if (!wanted || !box_draws_world_bounded(b) || !count) {
        if (b->key.dynamic_transform || !box_draws_world_bounded(b)) {
            b->world_valid = false;
        }

        goto done;
    }

    if (b->bounded) {
        glm_vec3_copy(b->center, local);
        glm_vec3_copy(b->extent, &local[3]);
    } else {
        const box_mesh_t *mesh = box_mesh_cache_get(cache, b->key.mesh);
        if (!mesh || !mesh->vertex_count) {
            b->world_valid = false;
            goto done;
        }

        for (r = 0; r < 3; r ++) {
            local[r] = (mesh->min[r] + mesh->max[r]) * 0.5f;
            local[3 + r] = (mesh->max[r] - mesh->min[r]) * 0.5f;
        }
    }

    if (!b->key.dynamic_transform && !b->world_dirty && b->world_valid &&
        !ecs_os_memcmp(local, b->world_local, ECS_SIZEOF(local)))
    {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        float extent2 = 0.0f;
        for (r = 0; r < 3; r ++) {
            const float *row = t[i].rows[r];
            float center = row[0] * local[0] + row[1] * local[1] + row[2] * local[2] + row[3];
            float extent = fabsf(row[0]) * local[3] + fabsf(row[1]) * local[4] +
                fabsf(row[2]) * local[5];
            finite = finite && isfinite(center) && isfinite(extent);
            lo[r] = glm_min(lo[r], center - extent);
            hi[r] = glm_max(hi[r], center + extent);
            extent2 += extent * extent;
        }

        radius = glm_max(radius, extent2);
    }

    b->world_valid = finite;
    for (r = 0; r < 3; r ++) {
        float pad = 1e-4f * (fabsf(lo[r]) + fabsf(hi[r]) + (hi[r] - lo[r])) + 1e-5f;
        b->world_min[r] = lo[r] - pad;
        b->world_max[r] = hi[r] + pad;
        b->world_valid = b->world_valid && isfinite(b->world_min[r]) &&
            isfinite(b->world_max[r]);
    }

    b->world_radius = sqrtf(radius) * 1.001f + 1e-5f;
    ecs_os_memcpy(b->world_local, local, ECS_SIZEOF(local));
    b->world_dirty = false;
done:
    return;
}

bool box_draws_layer_of(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_draw_layer_t *out)
{
    const PlanetAtmosphere *a = NULL;
    const WorldTransform *t = NULL;
    const box_mesh_t *mesh = NULL;
    bool result = false;
    int32_t r;
    if (b->key.atmosphere && d->world) {
        a = ecs_get(d->world, b->key.atmosphere, PlanetAtmosphere);
        t = ecs_get(d->world, b->key.atmosphere, WorldTransform);
    }

    if (!a || !t || !(a->radius > 0.0f) || !(a->planet_radius > 0.0f)) {
        goto done;
    }

    if (b->bounded) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b->center), out->local_center);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->extent), out->local_extent);
    } else {
        mesh = box_mesh_cache_get(box_mesh_cache_of(d->world), b->key.mesh);
        if (!mesh || !mesh->vertex_count) {
            goto done;
        }

        for (r = 0; r < 3; r ++) {
            out->local_center[r] = (mesh->min[r] + mesh->max[r]) * 0.5f;
            out->local_extent[r] = (mesh->max[r] - mesh->min[r]) * 0.5f;
        }
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, t->m[3]), out->center);
    out->ground = a->radius;
    out->top = a->radius * (1.0f + glm_max(a->thickness, 0.0f) / a->planet_radius);
    result = true;
done:
    return result;
}

bool box_draws_layer(
    const box_draw_layer_t *l,
    const box_render_transform_t *t)
{
    float inner = FLT_MAX, dist2 = 0.0f;
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        const float *row = t->rows[r];
        float center = row[0] * l->local_center[0] + row[1] * l->local_center[1] +
            row[2] * l->local_center[2] + row[3];
        dist2 += (center - l->center[r]) * (center - l->center[r]);
    }

    for (c = 0; c < 3; c ++) {
        float axis = sqrtf(t->rows[0][c] * t->rows[0][c] + t->rows[1][c] * t->rows[1][c] +
            t->rows[2][c] * t->rows[2][c]);
        inner = glm_min(inner, axis * l->local_extent[c]);
    }

    float pad = 1e-4f * l->top;

    return sqrtf(dist2) + l->ground <= inner + pad && inner <= l->top + pad;
}
