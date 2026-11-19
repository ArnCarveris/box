/**
 * @file render/debug_portals.c
 * @brief Lines of the portal world of a view (PortalDebug).
 */

#include "debug_portals.h"

/** Add a line from a point to another to the lines of a view.
 *
 * @param p The packet
 * @param v The view
 * @param a Start of the line
 * @param b End of the line
 * @param style BOX_RENDER_DEBUG_STYLE_* of the line
 */
static void box_render_debug_segment(
    box_render_packet_t *p,
    box_render_view_t *v,
    const float *a,
    const float *b,
    uint32_t style)
{
    vec3 along;
    glm_vec3_sub(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, a), along);

    if (glm_vec3_norm2(along) > 1e-12f && v->portal_debug_count < BOX_VIEW_DEBUG_BOXES_MAX) {
        mat4 pose;
        glm_mat4_identity(pose);
        glm_vec3_copy(along, pose[0]);
        glm_vec3_copy(ECS_CONST_CAST(float*, a), pose[3]);
        ecs_os_memcpy(ecs_vec_append_t(NULL, &p->portal_debug_poses, mat4), pose,
            ECS_SIZEOF(mat4));

        box_render_debug_box_t *box = ecs_vec_append_t(NULL, &p->portal_debug_boxes,
            box_render_debug_box_t);
        ecs_os_memset_t(box, 0, box_render_debug_box_t);
        glm_vec3_copy((vec3){1, 0, 0}, box->max);
        box->style = style;
        box->pose = (uint32_t)v->portal_pose_count;
        v->portal_pose_count ++;
        v->portal_debug_count ++;
    }
}

/** Add the outline of a polygon to the lines of a view.
 *
 * @param p The packet
 * @param v The view
 * @param verts Corners of the polygon, in order
 * @param count Corners
 * @param style BOX_RENDER_DEBUG_STYLE_* of the lines
 */
static void box_render_debug_loop(
    box_render_packet_t *p,
    box_render_view_t *v,
    const vec3 *verts,
    int32_t count,
    uint32_t style)
{
    int32_t i;

    for (i = 0; i < count; i ++) {
        box_render_debug_segment(p, v, verts[i], verts[(i + 1) % count], style);
    }
}

/** Add the volume of an area to the lines of a view: its outline at the floor and at the
 * ceiling and an edge at each corner.
 *
 * @param index The index
 * @param area The area
 * @param p The packet
 * @param v The view
 * @param style BOX_RENDER_DEBUG_STYLE_* of the lines
 */
static void box_render_debug_area(
    const box_portal_index_t *index,
    const box_portal_area_t *area,
    box_render_packet_t *p,
    box_render_view_t *v,
    uint32_t style)
{
    const vec2 *points = ecs_vec_get_t(&index->points, vec2, area->point_first);
    int32_t i, count = area->point_count;

    for (i = 0; i < count; i ++) {
        const float *a = points[i];
        const float *b = points[(i + 1) % count];
        vec3 floor_a = { a[0], area->y, a[1] };
        vec3 floor_b = { b[0], area->y, b[1] };
        vec3 top_a = { a[0], area->top, a[1] };
        vec3 top_b = { b[0], area->top, b[1] };

        box_render_debug_segment(p, v, floor_a, floor_b, style);
        box_render_debug_segment(p, v, top_a, top_b, style);
        box_render_debug_segment(p, v, floor_a, top_a, style);
    }
}

/** Check whether an entry before another one holds the same area.
 *
 * @param entries The entries of the traversal
 * @param at Index of the entry
 * @return Whether an earlier entry has its area
 */
static bool box_render_debug_repeats(
    const box_portal_entry_t *entries,
    int32_t at)
{
    bool result = false;
    int32_t i;

    for (i = 0; i < at && !result; i ++) {
        result = entries[i].area == entries[at].area;
    }

    return result;
}

/** Get the style of a portal on the side of an area the traversal reached.
 *
 * @param p The portal
 * @param entries The entries of the traversal
 * @param count Entries
 * @param index Index of the portal in the index
 * @return BOX_RENDER_DEBUG_STYLE_* of its outline
 */
static uint32_t box_render_debug_portal_style(
    const box_portal_portal_t *p,
    const box_portal_entry_t *entries,
    int32_t count,
    int32_t index)
{
    uint32_t result = p->open < BOX_PORTAL_SHUT ? BOX_RENDER_DEBUG_STYLE_PORTAL_SHUT :
        BOX_RENDER_DEBUG_STYLE_PORTAL_LEFT_OUT;
    bool passed = false;
    int32_t i;

    for (i = 0; i < count; i ++) {
        if (entries[i].via == index && !passed) {
            passed = !entries[i].sky_only;
            result = passed ? BOX_RENDER_DEBUG_STYLE_PORTAL_PASSED :
                BOX_RENDER_DEBUG_STYLE_PORTAL_SKY;
        }
    }

    return result;
}

/** Add the outlines of the portals on the sides of the areas a traversal reached, each once.
 *
 * @param index The index
 * @param vis What the camera saw
 * @param p The packet
 * @param v The view
 */
static void box_render_debug_portal_outlines(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);
    const box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    const int32_t *links = ecs_vec_first_t(&index->links, int32_t);
    int32_t i, k, count = ecs_vec_count(&vis->entries);
    int32_t total = ecs_vec_count(&index->portals);
    bool *drawn = total ? ecs_os_calloc_n(bool, total) : NULL;

    for (i = 0; i < count; i ++) {
        const box_portal_area_t *area = ecs_vec_get_t(&index->areas, box_portal_area_t,
            entries[i].area);

        if (box_render_debug_repeats(entries, i)) {
            continue;
        }

        for (k = 0; k < area->link_count; k ++) {
            int32_t at = links[area->link_first + k];

            if (!drawn[at]) {
                drawn[at] = true;
                box_render_debug_loop(p, v, portals[at].verts, 4,
                    box_render_debug_portal_style(&portals[at], entries, count, at));
            }
        }
    }

    ecs_os_free(drawn);
}

/** Add the part of each portal the traversal looked through that the frustum of its entry
 * left.
 *
 * @param index The index
 * @param vis What the camera saw
 * @param p The packet
 * @param v The view
 */
static void box_render_debug_clipped(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);
    int32_t i, count = ecs_vec_count(&vis->entries);

    for (i = 0; i < count; i ++) {
        vec3 clipped[BOX_PORTAL_POLY_MAX];
        int32_t corners = 0;

        if (entries[i].parent >= 0 && entries[i].via >= 0) {
            const box_portal_entry_t *parent = &entries[entries[i].parent];
            const box_portal_portal_t *portal = ecs_vec_get_t(&index->portals,
                box_portal_portal_t, entries[i].via);

            corners = box_portal_clip(portal->verts, 4, parent->planes, parent->plane_count,
                clipped);
        }

        if (corners >= 3) {
            box_render_debug_loop(p, v, clipped, corners, BOX_RENDER_DEBUG_STYLE_CLIPPED);
        }
    }
}

/** Add the occluders of the areas a traversal reached.
 *
 * @param index The index
 * @param vis What the camera saw
 * @param eye The eye, which the occluders that face it turn to
 * @param p The packet
 * @param v The view
 */
static void box_render_debug_occluders(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    const float *eye,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);
    const box_portal_occluder_t *occluders = ecs_vec_first_t(&index->occluders,
        box_portal_occluder_t);
    int32_t i, k, count = ecs_vec_count(&vis->entries);
    int32_t total = ecs_vec_count(&index->occluders);

    for (k = 0; k < total; k ++) {
        bool reached = false;

        for (i = 0; i < count && !reached; i ++) {
            reached = entries[i].area == occluders[k].area;
        }

        if (reached) {
            vec3 verts[4], min, max, normal;

            box_portal_occluder_corners(&occluders[k], eye, normal, verts, min, max);
            box_render_debug_loop(p, v, verts, 4, BOX_RENDER_DEBUG_STYLE_OCCLUDER);
        }
    }
}

void box_render_debug_portals(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    const float *eye,
    const PortalDebug *settings,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    int32_t i, count = ecs_vec_count(&vis->entries);
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);

    v->portal_debug_first = ecs_vec_count(&p->portal_debug_boxes);
    v->portal_debug_count = 0;
    v->portal_pose_first = ecs_vec_count(&p->portal_debug_poses);
    v->portal_pose_count = 0;

    if (settings->areas) {
        for (i = 0; i < count; i ++) {
            if (!box_render_debug_repeats(entries, i)) {
                box_render_debug_area(index,
                    ecs_vec_get_t(&index->areas, box_portal_area_t, entries[i].area), p, v,
                    entries[i].area == vis->root ? BOX_RENDER_DEBUG_STYLE_AREA_HERE :
                        BOX_RENDER_DEBUG_STYLE_AREA_SEEN);
            }
        }
    }

    if (settings->lines) {
        box_render_debug_portal_outlines(index, vis, p, v);
        box_render_debug_clipped(index, vis, p, v);
    }

    if (settings->occluders) {
        box_render_debug_occluders(index, vis, eye, p, v);
    }
}

void box_render_debug_portals_append(
    box_render_packet_t *p,
    const box_render_view_t *v,
    int32_t *count,
    int32_t *poses)
{
    int32_t i, room = BOX_VIEW_DEBUG_BOXES_MAX - *count;
    int32_t n = v->portal_debug_count < room ? v->portal_debug_count : room;

    for (i = 0; i < n; i ++) {
        box_render_debug_box_t *box = ecs_vec_append_t(NULL, &p->debug_boxes,
            box_render_debug_box_t);
        *box = *ecs_vec_get_t(&p->portal_debug_boxes, box_render_debug_box_t,
            v->portal_debug_first + i);
        box->pose += (uint32_t)*poses;
    }

    for (i = 0; i < n; i ++) {
        ecs_os_memcpy(ecs_vec_append_t(NULL, &p->debug_poses, mat4), ecs_vec_get_t(
            &p->portal_debug_poses, mat4, v->portal_pose_first + i), ECS_SIZEOF(mat4));
    }

    *count += n;
    *poses += n;
}
