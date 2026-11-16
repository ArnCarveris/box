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

/** Get the local bounds every slot of a batch is tested with: the bounds of its parametric
 * surfaces, or of its mesh.
 *
 * @param b The batch
 * @param cache The mesh cache
 * @param local Receives the center, then the half size
 * @return Whether the batch has local bounds
 */
static bool box_draws_local_box(
    const box_draw_batch_t *b,
    box_mesh_cache_t *cache,
    float *local)
{
    const box_mesh_t *mesh = NULL;
    bool result = false;
    int32_t r;
    if (b->bounded) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b->center), local);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->extent), &local[3]);
        result = true;
        goto done;
    }

    mesh = cache ? box_mesh_cache_get(cache, b->key.mesh) : NULL;
    if (!mesh || !mesh->vertex_count) {
        goto done;
    }

    for (r = 0; r < 3; r ++) {
        local[r] = (mesh->min[r] + mesh->max[r]) * 0.5f;
        local[3 + r] = (mesh->max[r] - mesh->min[r]) * 0.5f;
    }

    result = true;
done:
    return result;
}

/** Place a box with a transform: the center and the half size of the box around it.
 *
 * @param t The transform
 * @param center Center of the box
 * @param extent Half size of the box
 * @param out_center Receives the placed center
 * @param out_extent Receives the half size around the placed box
 */
static void box_draws_place_box(
    const box_render_transform_t *t,
    const float *center,
    const float *extent,
    float *out_center,
    float *out_extent)
{
    int32_t r;
    for (r = 0; r < 3; r ++) {
        const float *row = t->rows[r];
        out_center[r] = row[0] * center[0] + row[1] * center[1] + row[2] * center[2] + row[3];
        out_extent[r] = fabsf(row[0]) * extent[0] + fabsf(row[1]) * extent[1] +
            fabsf(row[2]) * extent[2];
    }
}

void box_draws_groups(
    const box_render_draws_t *d,
    box_draw_batch_t *b,
    box_mesh_cache_t *cache)
{
    const box_render_transform_t *t = ecs_vec_first(&b->transforms);
    const uint32_t *movers = ecs_vec_first(&b->movers);
    int32_t i, g, r, count = ecs_vec_count(&b->movers), last = -1;
    float local[6];
    (void)d;
    if (!b->groups_dirty) {
        goto done;
    }

    b->groups_dirty = false;
    ecs_vec_clear(&b->groups);
    if (!box_draws_local_box(b, cache, local)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        uint32_t mover = movers[i] & BOX_RENDER_MOVER_INDEX;
        box_draw_group_t *group = last >= 0
            ? ecs_vec_get_t(&b->groups, box_draw_group_t, last) : NULL;
        float center[3], extent[3];
        if (!group || group->mover != mover) {
            group = NULL;
            for (g = 0; g < ecs_vec_count(&b->groups) && !group; g ++) {
                box_draw_group_t *cur = ecs_vec_get_t(&b->groups, box_draw_group_t, g);
                if (cur->mover == mover) {
                    group = cur;
                    last = g;
                }
            }
        }

        if (!group) {
            last = ecs_vec_count(&b->groups);
            group = ecs_vec_append_t(NULL, &b->groups, box_draw_group_t);
            group->mover = mover;
            for (r = 0; r < 3; r ++) {
                group->min[r] = FLT_MAX;
                group->max[r] = -FLT_MAX;
                group->extent[r] = 0.0f;
            }
        }

        box_draws_place_box(&t[i], local, &local[3], center, extent);
        for (r = 0; r < 3; r ++) {
            group->min[r] = glm_min(group->min[r], center[r] - extent[r]);
            group->max[r] = glm_max(group->max[r], center[r] + extent[r]);
            group->extent[r] = glm_max(group->extent[r], extent[r]);
        }
    }

done:
    return;
}

/** Compute the world bounds of a mover batch from the bounds of its groups, placed by their
 * movers.
 *
 * @param d The state
 * @param b The batch
 * @param cache The mesh cache
 */
static void box_draws_world_bounds_movers(
    const box_render_draws_t *d,
    box_draw_batch_t *b,
    box_mesh_cache_t *cache)
{
    vec3 lo = { FLT_MAX, FLT_MAX, FLT_MAX }, hi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    float radius = 0.0f;
    bool finite = true;
    int32_t g, r;
    box_draws_groups(d, b, cache);
    if (!ecs_vec_count(&b->groups)) {
        b->world_valid = false;
        goto done;
    }

    for (g = 0; g < ecs_vec_count(&b->groups); g ++) {
        const box_draw_group_t *group = ecs_vec_get_t(&b->groups, box_draw_group_t, g);
        const box_render_transform_t *pose = &box_draws_mover(d, group->mover)->pose;
        float center[3], extent[3], slot[3], zero[3] = { 0.0f, 0.0f, 0.0f }, c[3], e[3];
        float extent2 = 0.0f;
        for (r = 0; r < 3; r ++) {
            c[r] = (group->min[r] + group->max[r]) * 0.5f;
            e[r] = (group->max[r] - group->min[r]) * 0.5f;
        }

        box_draws_place_box(pose, c, e, center, extent);
        box_draws_place_box(pose, zero, group->extent, slot, slot);
        for (r = 0; r < 3; r ++) {
            finite = finite && isfinite(center[r]) && isfinite(extent[r]);
            lo[r] = glm_min(lo[r], center[r] - extent[r]);
            hi[r] = glm_max(hi[r], center[r] + extent[r]);
            extent2 += slot[r] * slot[r];
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
    b->world_dirty = false;
done:
    return;
}

void box_draws_shadow_movers(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    box_mesh_cache_t *cache = NULL;
    int32_t i, g, r, k, count = ecs_vec_count(&d->batches);
    bool moved = false;
    for (i = 1; d->shadow_record && !moved && i < ecs_vec_count(&d->movers); i ++) {
        moved = box_draws_mover(d, (uint32_t)i)->moved;
    }

    if (!moved) {
        goto done;
    }

    cache = box_mesh_cache_of(world);
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (!b->alive || !b->key.mover || !b->ready || !box_draws_caster(b)) {
            continue;
        }

        box_draws_groups(d, b, cache);
        for (g = 0; g < ecs_vec_count(&b->groups); g ++) {
            const box_draw_group_t *group = ecs_vec_get_t(&b->groups, box_draw_group_t, g);
            const box_draw_mover_t *m = box_draws_mover(d, group->mover);
            const box_render_transform_t *poses[2] = { &m->was, &m->pose };
            float c[3], e[3], center[3], extent[3];
            if (!m->moved) {
                continue;
            }

            for (r = 0; r < 3; r ++) {
                c[r] = (group->min[r] + group->max[r]) * 0.5f;
                e[r] = (group->max[r] - group->min[r]) * 0.5f;
            }

            for (k = 0; k < 2; k ++) {
                box_render_shadow_region_t *out = ecs_vec_append_t(NULL, &d->shadow_regions,
                    box_render_shadow_region_t);
                box_draws_place_box(poses[k], c, e, center, extent);
                out->scene = b->key.scene;
                for (r = 0; r < 3; r ++) {
                    out->min[r] = center[r] - extent[r];
                    out->max[r] = center[r] + extent[r];
                }
            }
        }
    }

done:
    return;
}

void box_draws_world_bounds(
    const box_render_draws_t *d,
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
        if (b->key.dynamic_transform || b->key.mover || !box_draws_world_bounded(b)) {
            b->world_valid = false;
        }

        goto done;
    }

    if (b->key.mover) {
        box_draws_world_bounds_movers(d, b, cache);
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
    WorldTransform buf;
    const WorldTransform *t = NULL;
    const box_mesh_t *mesh = NULL;
    bool result = false;
    int32_t r;
    if (b->key.atmosphere && d->world) {
        a = ecs_get(d->world, b->key.atmosphere, PlanetAtmosphere);
        t = box_transform_world(d->world, b->key.atmosphere, &buf);
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

bool box_draws_screen_bounds(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    const box_render_view_t *v,
    int32_t slot,
    float *out)
{
    box_render_transform_t tmp;
    const box_render_transform_t *t = box_draws_slot_world(d,
        ECS_CONST_CAST(box_draw_batch_t*, b), slot, &tmp);
    const box_mesh_t *mesh = NULL;
    const vec4 *m = v->view_matrix;
    float local[6], world[6];
    int32_t r, c;
    bool result = false;
    if (!box_render_is_mesh(b->key.pipeline) || b->key.water) {
        goto done;
    }

    if (b->bounded) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b->center), local);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->extent), &local[3]);
    } else {
        mesh = d->world ? box_mesh_cache_get(box_mesh_cache_of(d->world), b->key.mesh) : NULL;
        if (!mesh || !mesh->vertex_count) {
            goto done;
        }

        for (r = 0; r < 3; r ++) {
            local[r] = (mesh->min[r] + mesh->max[r]) * 0.5f;
            local[3 + r] = (mesh->max[r] - mesh->min[r]) * 0.5f;
        }
    }

    for (r = 0; r < 3; r ++) {
        const float *row = t->rows[r];
        world[r] = row[0] * local[0] + row[1] * local[1] + row[2] * local[2] + row[3];
        world[3 + r] = fabsf(row[0]) * local[3] + fabsf(row[1]) * local[4] +
            fabsf(row[2]) * local[5];
        world[3 + r] += 1e-4f * (fabsf(world[r]) + world[3 + r]) + 1e-5f;
    }

    out[0] = out[1] = FLT_MAX;
    out[2] = out[3] = -FLT_MAX;
    for (c = 0; c < 8; c ++) {
        float x = world[0] + ((c & 1) ? world[3] : -world[3]);
        float y = world[1] + ((c & 2) ? world[4] : -world[4]);
        float z = world[2] + ((c & 4) ? world[5] : -world[5]);
        float vx = m[0][0] * x + m[1][0] * y + m[2][0] * z + m[3][0];
        float vy = m[0][1] * x + m[1][1] * y + m[2][1] * z + m[3][1];
        float vz = m[0][2] * x + m[1][2] * y + m[2][2] * z + m[3][2];
        if (!v->camera_data.ortho) {
            if (!(vz < -1e-5f)) {
                goto done;
            }

            vx /= -vz;
            vy /= -vz;
        }

        out[0] = glm_min(out[0], vx);
        out[1] = glm_min(out[1], vy);
        out[2] = glm_max(out[2], vx);
        out[3] = glm_max(out[3], vy);
    }

    result = isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]) && isfinite(out[3]);
done:
    return result;
}
