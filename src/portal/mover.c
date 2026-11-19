/**
 * @file portal/mover.c
 * @brief The movers that carry areas, portals and occluders of the portal index.
 *
 * A mover is an entity with DynamicTransform, such as a ship. What it carries is built where the
 * mover stands, its rest, and kept in the rest arrays of the index.
 *
 * Point queries bring the point into the rest instead of moving the outlines. The portals, the
 * occluders, the areas' box and the boxes the cull pass tests are placed where the mover is.
 */

#include "portal.h"
#include <float.h>
#include <math.h>

/** Difference below which two transforms of a mover are the same */
#define BOX_PORTAL_MOVER_EPSILON (1e-6f)

/** Check whether an entity is a mover: it owns DynamicTransform, or has it through a prefab
 * other than the DynamicTransformSpread its descendants inherit.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is a mover
 */
static bool box_portal_is_mover(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_owns_id(world, e, DynamicTransform) ||
        (ecs_has_id(world, e, DynamicTransform) &&
            !ecs_has_pair(world, e, EcsIsA, DynamicTransformSpread));
}

ecs_entity_t box_portal_mover_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;

    for (; e && !result; e = ecs_get_parent(world, e)) {
        if (box_portal_is_mover(world, e)) {
            result = e;
        }
    }

    return result;
}

/** Get the world transform of a mover.
 *
 * @param world The world
 * @param e The mover
 * @param out Receives the transform, the identity when it has none
 */
static void box_portal_mover_pose(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    WorldTransform buf;
    const WorldTransform *t = box_transform_world(world, e, &buf);

    if (t) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, t->m), out);
    } else {
        glm_mat4_identity(out);
    }
}

int32_t box_portal_mover_slot(
    const ecs_world_t *world,
    box_portal_index_t *index,
    ecs_entity_t e)
{
    ecs_entity_t mover = box_portal_mover_of(world, e);
    box_portal_mover_t *movers = ecs_vec_first_t(&index->movers, box_portal_mover_t);
    int32_t i, count = ecs_vec_count(&index->movers), result = -1;

    for (i = 0; i < count && mover && result < 0; i ++) {
        if (movers[i].entity == mover) {
            result = i;
        }
    }

    if (mover && result < 0) {
        box_portal_mover_t *m = ecs_vec_append_t(NULL, &index->movers, box_portal_mover_t);

        ecs_os_memset_t(m, 0, box_portal_mover_t);
        m->entity = mover;
        box_portal_mover_pose(world, mover, m->rest);
        glm_mat4_copy(m->rest, m->placed);
        glm_mat4_identity(m->delta);
        glm_mat4_identity(m->inverse);
        m->level = true;
        result = count;
    }

    return result;
}

/** Copy one array of the index into its rest array.
 *
 * @param from The array
 * @param to The rest array
 * @param size Size of an element
 */
static void box_portal_movers_keep(
    const ecs_vec_t *from,
    ecs_vec_t *to,
    ecs_size_t size)
{
    int32_t count = ecs_vec_count(from);

    ecs_vec_set_count(NULL, to, size, count);
    if (count) {
        ecs_os_memcpy(ecs_vec_first(to), ecs_vec_first(from), size * count);
    }
}

void box_portal_movers_rest(
    box_portal_index_t *index)
{
    box_portal_movers_keep(&index->areas, &index->rest_areas, ECS_SIZEOF(box_portal_area_t));
    box_portal_movers_keep(&index->boxes, &index->rest_boxes, ECS_SIZEOF(box_portal_box_t));
    box_portal_movers_keep(&index->portals, &index->rest_portals,
        ECS_SIZEOF(box_portal_portal_t));
    box_portal_movers_keep(&index->occluders, &index->rest_occluders,
        ECS_SIZEOF(box_portal_occluder_t));
}

/** Check whether two transforms are the same.
 *
 * @param a A transform
 * @param b The other transform
 * @return Whether no element differs by more than BOX_PORTAL_MOVER_EPSILON
 */
static bool box_portal_mover_same(
    mat4 a,
    mat4 b)
{
    bool result = true;
    int32_t c, r;

    for (c = 0; c < 4 && result; c ++) {
        for (r = 0; r < 4 && result; r ++) {
            result = fabsf(a[c][r] - b[c][r]) <= BOX_PORTAL_MOVER_EPSILON;
        }
    }

    return result;
}

/** Check whether a transform keeps the axes of the world: every axis it turns into an axis.
 *
 * @param m The transform
 * @return Whether it does
 */
static bool box_portal_mover_keeps_axes(
    mat4 m)
{
    bool result = true;
    int32_t c, r;

    for (c = 0; c < 3 && result; c ++) {
        int32_t along = 0;

        for (r = 0; r < 3; r ++) {
            along += fabsf(m[c][r]) > 1e-4f ? 1 : 0;
        }

        result = along == 1;
    }

    return result;
}

/** Transform an axis aligned box and take the axis aligned box around it.
 *
 * @param m The transform
 * @param min Smallest corner
 * @param max Largest corner
 * @param out_min Receives the smallest corner of the result
 * @param out_max Receives the largest corner of the result
 */
static void box_portal_mover_box(
    mat4 m,
    const float *min,
    const float *max,
    vec3 out_min,
    vec3 out_max)
{
    int32_t k;

    glm_vec3_fill(out_min, FLT_MAX);
    glm_vec3_fill(out_max, -FLT_MAX);

    for (k = 0; k < 8; k ++) {
        vec3 corner = {
            (k & 1) ? max[0] : min[0],
            (k & 2) ? max[1] : min[1],
            (k & 4) ? max[2] : min[2] };
        vec3 at;

        glm_mat4_mulv3(m, corner, 1.0f, at);
        glm_vec3_minv(out_min, at, out_min);
        glm_vec3_maxv(out_max, at, out_max);
    }
}

/** Place the areas and boxes of the movers that moved.
 *
 * @param index The index
 * @param changed Per mover slot, whether it moved since it was last placed
 */
static void box_portal_movers_place_areas(
    box_portal_index_t *index,
    const bool *changed)
{
    box_portal_mover_t *movers = ecs_vec_first_t(&index->movers, box_portal_mover_t);
    box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    const box_portal_area_t *rest = ecs_vec_first_t(&index->rest_areas, box_portal_area_t);
    box_portal_box_t *boxes = ecs_vec_first_t(&index->boxes, box_portal_box_t);
    const box_portal_box_t *rest_boxes = ecs_vec_first_t(&index->rest_boxes, box_portal_box_t);
    int32_t i, count = ecs_vec_count(&index->areas);

    for (i = 1; i < count; i ++) {
        box_portal_area_t *a = &areas[i];
        const box_portal_area_t *r = &rest[i];
        vec3 lo = { r->min[0], r->y, r->min[1] }, hi = { r->max[0], r->top, r->max[1] };
        vec3 min, max;

        if (a->mover < 0 || !changed[a->mover]) {
            continue;
        }

        box_portal_mover_box(movers[a->mover].delta, lo, hi, min, max);
        a->min[0] = min[0];
        a->min[1] = min[2];
        a->max[0] = max[0];
        a->max[1] = max[2];
        a->y = min[1];
        a->top = max[1];
    }

    count = ecs_vec_count(&index->boxes);
    for (i = 0; i < count; i ++) {
        int32_t mover = areas[boxes[i].area].mover;
        const box_portal_box_t *r = &rest_boxes[i];

        if (mover < 0 || !changed[mover]) {
            continue;
        }

        box_portal_mover_box(movers[mover].delta, r->min, r->max, boxes[i].min, boxes[i].max);
        boxes[i].exact = r->exact && movers[mover].level;
    }
}

/** Place a portal at the transform of its mover.
 *
 * @param m The transform from the rest to where the mover is
 * @param r The portal at the rest
 * @param p The portal, receives its corners, axes, plane and box
 */
static void box_portal_mover_portal(
    mat4 m,
    const box_portal_portal_t *r,
    box_portal_portal_t *p)
{
    int32_t k;

    glm_mat4_mulv3(m, ECS_CONST_CAST(float*, r->center), 1.0f, p->center);
    glm_mat4_mulv3(m, ECS_CONST_CAST(float*, r->normal), 0.0f, p->normal);
    glm_mat4_mulv3(m, ECS_CONST_CAST(float*, r->right), 0.0f, p->right);
    glm_mat4_mulv3(m, ECS_CONST_CAST(float*, r->up), 0.0f, p->up);
    glm_vec3_normalize(p->normal);
    glm_vec3_normalize(p->right);
    glm_vec3_normalize(p->up);
    p->d = -glm_vec3_dot(p->normal, p->center);
    glm_vec3_fill(p->min, FLT_MAX);
    glm_vec3_fill(p->max, -FLT_MAX);

    for (k = 0; k < 4; k ++) {
        glm_mat4_mulv3(m, ECS_CONST_CAST(float*, r->verts[k]), 1.0f, p->verts[k]);
        glm_vec3_minv(p->min, p->verts[k], p->min);
        glm_vec3_maxv(p->max, p->verts[k], p->max);
    }
}

/** Place the portals and occluders of the movers that moved.
 *
 * @param index The index
 * @param changed Per mover slot, whether it moved since it was last placed
 */
static void box_portal_movers_place_openings(
    box_portal_index_t *index,
    const bool *changed)
{
    box_portal_mover_t *movers = ecs_vec_first_t(&index->movers, box_portal_mover_t);
    box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    const box_portal_portal_t *rest = ecs_vec_first_t(&index->rest_portals,
        box_portal_portal_t);
    box_portal_occluder_t *occluders = ecs_vec_first_t(&index->occluders,
        box_portal_occluder_t);
    const box_portal_occluder_t *rest_occluders = ecs_vec_first_t(&index->rest_occluders,
        box_portal_occluder_t);
    int32_t i, count = ecs_vec_count(&index->portals);

    for (i = 0; i < count; i ++) {
        int32_t mover = portals[i].mover;

        if (mover >= 0 && changed[mover]) {
            box_portal_mover_portal(movers[mover].delta, &rest[i], &portals[i]);
        }
    }

    count = ecs_vec_count(&index->occluders);
    for (i = 0; i < count; i ++) {
        box_portal_occluder_t *o = &occluders[i];
        const box_portal_occluder_t *r = &rest_occluders[i];

        if (o->mover < 0 || !changed[o->mover]) {
            continue;
        }

        glm_mat4_mulv3(movers[o->mover].delta, ECS_CONST_CAST(float*, r->center), 1.0f,
            o->center);
        glm_mat4_mulv3(movers[o->mover].delta, ECS_CONST_CAST(float*, r->normal), 0.0f,
            o->normal);
        glm_vec3_normalize(o->normal);
    }
}

void box_portal_movers_place(
    const ecs_world_t *world,
    box_portal_index_t *index)
{
    box_portal_mover_t *movers = ecs_vec_first_t(&index->movers, box_portal_mover_t);
    int32_t i, count = ecs_vec_count(&index->movers);
    bool *changed = count ? ecs_os_calloc_n(bool, count) : NULL;
    bool any = false;

    for (i = 0; i < count; i ++) {
        box_portal_mover_t *m = &movers[i];
        mat4 pose, rest_inverse;

        if (!ecs_is_alive(world, m->entity)) {
            continue;
        }

        box_portal_mover_pose(world, m->entity, pose);
        if (box_portal_mover_same(pose, m->placed)) {
            continue;
        }

        glm_mat4_copy(pose, m->placed);
        glm_mat4_inv(m->rest, rest_inverse);
        glm_mat4_mul(pose, rest_inverse, m->delta);
        glm_mat4_inv(m->delta, m->inverse);
        m->moved = !box_portal_mover_same(pose, m->rest);
        m->level = box_portal_mover_keeps_axes(m->delta);
        changed[i] = true;
        any = true;
    }

    if (any) {
        box_portal_movers_place_areas(index, changed);
        box_portal_movers_place_openings(index, changed);
        index->placements ++;
    }

    ecs_os_free(changed);
}

bool box_portal_mover_to_rest(
    const box_portal_index_t *index,
    int32_t area,
    const float *p,
    vec3 out)
{
    const box_portal_area_t *a = ecs_vec_get_t(&index->areas, box_portal_area_t, area);
    const box_portal_mover_t *m = a->mover >= 0
        ? ecs_vec_get_t(&index->movers, box_portal_mover_t, a->mover)
        : NULL;

    bool result = m && m->moved;

    if (result) {
        glm_mat4_mulv3(ECS_CONST_CAST(vec4*, m->inverse), ECS_CONST_CAST(float*, p), 1.0f, out);
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, p), out);
    }

    return result;
}

void box_portal_mover_rows(
    const box_portal_index_t *index,
    int32_t slot,
    vec4 *out)
{
    const box_portal_mover_t *m = ecs_vec_get_t(&index->movers, box_portal_mover_t, slot);
    int32_t row;

    for (row = 0; row < 3; row ++) {
        out[row][0] = m->inverse[0][row];
        out[row][1] = m->inverse[1][row];
        out[row][2] = m->inverse[2][row];
        out[row][3] = m->inverse[3][row];
    }
}

void box_portal_rows_box(
    const vec4 *rows,
    const float *center,
    const float *extent,
    vec3 out_center,
    vec3 out_extent)
{
    int32_t row;

    for (row = 0; row < 3; row ++) {
        const float *r = rows[row];

        out_center[row] = r[0] * center[0] + r[1] * center[1] + r[2] * center[2] + r[3];
        out_extent[row] = fabsf(r[0]) * extent[0] + fabsf(r[1]) * extent[1] +
            fabsf(r[2]) * extent[2];
    }
}
