/**
 * @file render/portal_marks.c
 * @brief Portal marks of a view, see portal_marks.h.
 */

#include "portal_marks.h"
#include <float.h>
#include <math.h>

/** Regions a view has, the region bits of its stencil */
#define BOX_RENDER_PORTAL_REGIONS ((int32_t)BOX_RENDER_STENCIL_REGION + 1)

/** What the marks of one view are built from */
typedef struct box_render_portal_marks_t {
    const box_portal_portal_t *portals; /**< The portals of the index */
    const box_portal_entry_t *entries;  /**< The entries of the traversal */
    int32_t count;          /**< Entries */
    const float *eye;       /**< World position of the eye */
    int32_t *region;        /**< Per entry, its region */
    bool *own;              /**< Per entry, whether it marks a region of its own */
    float *distance;        /**< Per entry, how far the eye is from its clipped portal */
    bool *visited;          /**< Per entry, whether its marks were written */
    uint32_t values[BOX_RENDER_PORTAL_REGIONS]; /**< Per region, its stencil value */
    box_render_packet_t *p; /**< The packet the marks are appended to */
} box_render_portal_marks_t;

/** Clip the portal an entry was reached through to what its parent sees, as the traversal did.
 *
 * @param m The marks
 * @param at Index of the entry, not one of the eye
 * @param out Receives the corners, BOX_PORTAL_POLY_MAX at most
 * @return Corners, fewer than 3 when nothing is left
 */
static int32_t box_render_portal_marks_clip(
    const box_render_portal_marks_t *m,
    int32_t at,
    vec3 *out)
{
    const box_portal_entry_t *e = &m->entries[at];
    const box_portal_entry_t *parent = &m->entries[e->parent];
    const box_portal_portal_t *portal = &m->portals[e->via];

    return box_portal_clip(portal->verts, 4, parent->planes, parent->plane_count, out);
}

/** Get the distance of a point to a segment.
 *
 * @param p The point
 * @param a Start of the segment
 * @param b End of the segment
 * @return The distance
 */
static float box_render_portal_marks_segment(
    const float *p,
    const float *a,
    const float *b)
{
    vec3 edge, to, nearest;
    glm_vec3_sub(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, a), edge);
    glm_vec3_sub(ECS_CONST_CAST(float*, p), ECS_CONST_CAST(float*, a), to);

    float length = glm_vec3_norm2(edge);
    float t = length > 0 ? glm_clamp(glm_vec3_dot(to, edge) / length, 0.0f, 1.0f) : 0.0f;

    glm_vec3_scale(edge, t, nearest);
    glm_vec3_add(nearest, ECS_CONST_CAST(float*, a), nearest);

    return glm_vec3_distance(ECS_CONST_CAST(float*, p), nearest);
}

/** Get the distance of the eye to the nearest point of a convex polygon.
 *
 * @param eye The eye
 * @param poly Corners of the polygon, in order
 * @param count Corners, at least 3
 * @return The distance
 */
static float box_render_portal_marks_nearest(
    const float *eye,
    const vec3 *poly,
    int32_t count)
{
    vec3 normal = {0, 0, 0}, to, edge, side;
    bool inside = true;
    float result = FLT_MAX;
    int32_t i;

    for (i = 0; i < count; i ++) {
        const float *a = poly[i];
        const float *b = poly[(i + 1) % count];

        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }

    for (i = 0; i < count; i ++) {
        const float *a = poly[i];
        const float *b = poly[(i + 1) % count];

        glm_vec3_sub(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, a), edge);
        glm_vec3_sub(ECS_CONST_CAST(float*, eye), ECS_CONST_CAST(float*, a), to);
        glm_vec3_cross(edge, to, side);
        inside = inside && glm_vec3_dot(side, normal) >= 0;
        result = glm_min(result, box_render_portal_marks_segment(eye, a, b));
    }

    float length = glm_vec3_norm(normal);
    if (inside && length > 0) {
        glm_vec3_sub(ECS_CONST_CAST(float*, eye), ECS_CONST_CAST(float*, poly[0]), to);
        result = fabsf(glm_vec3_dot(to, normal)) / length;
    }

    return result;
}

/** Give every entry its region and every region its stencil value.
 *
 * @param m The marks
 */
static void box_render_portal_marks_regions(
    box_render_portal_marks_t *m)
{
    bool wet[BOX_RENDER_PORTAL_REGIONS] = {0};
    int32_t i, regions = 1;
    vec3 clipped[BOX_PORTAL_POLY_MAX];

    for (i = 0; i < m->count; i ++) {
        const box_portal_entry_t *e = &m->entries[i];
        bool own = false;

        m->region[i] = 0;
        m->distance[i] = 0;
        m->visited[i] = false;

        if (e->parent >= 0) {
            const box_portal_portal_t *portal = &m->portals[e->via];
            bool standing = !portal->pass_through && e->whole && e->depth > 0;
            int32_t corners = box_render_portal_marks_clip(m, i, clipped);

            own = !standing && regions < BOX_RENDER_PORTAL_REGIONS && corners >= 3;
            m->region[i] = own ? regions ++ : m->region[e->parent];
            if (own) {
                m->distance[i] = box_render_portal_marks_nearest(m->eye, clipped, corners);
            }
        }

        m->own[i] = own;
        if (e->area == 0 && !e->sky_only) {
            wet[m->region[i]] = true;
        }
    }

    for (i = 0; i < BOX_RENDER_PORTAL_REGIONS; i ++) {
        m->values[i] = (uint32_t)i | (wet[i] ? 0u : BOX_RENDER_STENCIL_DRY);
    }
}

/** Append the mark of an entry: its clipped portal as a triangle fan.
 *
 * @param m The marks
 * @param at Index of the entry, which has a region of its own
 */
static void box_render_portal_marks_add(
    box_render_portal_marks_t *m,
    int32_t at)
{
    const box_portal_entry_t *e = &m->entries[at];
    vec3 clipped[BOX_PORTAL_POLY_MAX];
    int32_t i, corners = box_render_portal_marks_clip(m, at, clipped);
    box_render_portal_mark_t *mark = ecs_vec_append_t(NULL, &m->p->portal_marks,
        box_render_portal_mark_t);

    mark->first = ecs_vec_count(&m->p->portal_mark_verts);
    mark->count = (corners - 2) * 3;
    mark->parent = m->values[m->region[e->parent]];
    mark->child = m->values[m->region[at]];

    vec4 *verts = ecs_vec_grow_t(NULL, &m->p->portal_mark_verts, vec4, mark->count);

    for (i = 1; i + 1 < corners; i ++) {
        glm_vec4(clipped[0], 1.0f, verts[0]);
        glm_vec4(clipped[i], 1.0f, verts[1]);
        glm_vec4(clipped[i + 1], 1.0f, verts[2]);
        verts += 3;
    }
}

/** Append the marks of the children of an entry, nearest first, each followed by the marks of
 * its own children.
 *
 * @param m The marks
 * @param at Index of the entry
 */
static void box_render_portal_marks_visit(
    box_render_portal_marks_t *m,
    int32_t at)
{
    int32_t i;

    for (;;) {
        int32_t next = -1;

        for (i = at + 1; i < m->count; i ++) {
            bool child = m->entries[i].parent == at && !m->visited[i];

            if (child && (next < 0 || m->distance[i] < m->distance[next])) {
                next = i;
            }
        }

        if (next < 0) {
            break;
        }

        m->visited[next] = true;
        if (m->own[next]) {
            box_render_portal_marks_add(m, next);
        }

        box_render_portal_marks_visit(m, next);
    }
}

void box_render_portal_marks(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    const float *eye,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    int32_t i, count = ecs_vec_count(&vis->entries);
    box_render_portal_marks_t m = {
        .portals = ecs_vec_first_t(&index->portals, box_portal_portal_t),
        .entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t),
        .count = count,
        .eye = eye,
        .p = p
    };

    v->mark_first = ecs_vec_count(&p->portal_marks);
    v->mark_count = 0;
    v->stencil_clear = 0;
    if (!count) {
        goto done;
    }

    m.region = ecs_os_malloc_n(int32_t, count);
    m.own = ecs_os_malloc_n(bool, count);
    m.distance = ecs_os_malloc_n(float, count);
    m.visited = ecs_os_malloc_n(bool, count);

    box_render_portal_marks_regions(&m);
    v->stencil_clear = m.values[0];

    for (i = 0; i < count; i ++) {
        if (m.entries[i].parent < 0) {
            box_render_portal_marks_visit(&m, i);
        }
    }

    v->mark_count = ecs_vec_count(&p->portal_marks) - v->mark_first;

    ecs_os_free(m.region);
    ecs_os_free(m.own);
    ecs_os_free(m.distance);
    ecs_os_free(m.visited);
done:
    return;
}
