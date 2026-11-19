/**
 * @file portal/traverse.c
 * @brief The portal traversal: which areas a camera sees, and through which frustum each.
 *
 * Modelled on the VisArea traversal of CryEngine 1 and the culling camera of SECTR. Each
 * visited pair of an area and a frustum is an entry; an area seen through two portals has two.
 */

#include "portal.h"
#include <float.h>
#include <math.h>

/** Occluders a traversal accumulates at most */
#define BOX_PORTAL_OCCLUDERS_MAX (16)

/** Slack around the opening of a portal within which an eye near its plane stands in it */
#define BOX_PORTAL_OPENING_SLACK (0.1f)

/** Distance a portal may lie behind the portal it is seen through and still be looked at */
#define BOX_PORTAL_BEHIND_SLACK (0.01f)

/** An occluder a traversal passed: what is inside all its planes is hidden behind it */
typedef struct box_portal_blocker_t {
    int32_t occluder;       /**< Index of the occluder */
    int32_t plane_count;    /**< Planes */
    vec4 planes[5];         /**< Through the eye and its edges, and its own plane */
} box_portal_blocker_t;

/** The occluders a traversal passed so far */
typedef struct box_portal_blockers_t {
    int32_t count;          /**< Blockers */
    box_portal_blocker_t items[BOX_PORTAL_OCCLUDERS_MAX]; /**< The blockers */
} box_portal_blockers_t;

void box_portal_vis_init(
    box_portal_vis_t *vis)
{
    ecs_os_memset_t(vis, 0, box_portal_vis_t);
    ecs_vec_init_t(NULL, &vis->entries, box_portal_entry_t, 0);
    ecs_vec_init_t(NULL, &vis->pending, box_portal_entry_t, 0);
}

void box_portal_vis_fini(
    box_portal_vis_t *vis)
{
    ecs_vec_fini_t(NULL, &vis->entries, box_portal_entry_t);
    ecs_vec_fini_t(NULL, &vis->pending, box_portal_entry_t);
}

/** Check whether a portal is one a chain of entries already looked through.
 *
 * @param vis The traversal
 * @param entry Last entry of the chain
 * @param portal Index of the portal
 * @return Whether the entry or one of its parents was reached through the portal
 */
static bool box_portal_in_path(
    const box_portal_vis_t *vis,
    const box_portal_entry_t *entry,
    int32_t portal)
{
    bool result = entry->via == portal;
    int32_t parent = entry->parent;

    while (!result && parent >= 0) {
        const box_portal_entry_t *e = ecs_vec_get_t(&vis->entries, box_portal_entry_t, parent);
        result = e->via == portal;
        parent = e->parent;
    }

    return result;
}

/** Check whether a point, projected on the plane of a portal, is in its opening.
 *
 * @param p The portal
 * @param point The point
 * @return Whether the opening, with some slack, holds the projected point
 */
static bool box_portal_opening_holds(
    const box_portal_portal_t *p,
    const float *point)
{
    vec3 r;
    glm_vec3_sub(ECS_CONST_CAST(float*, point), ECS_CONST_CAST(float*, p->center), r);

    float along = fabsf(glm_vec3_dot(r, ECS_CONST_CAST(float*, p->right)));
    float across = fabsf(glm_vec3_dot(r, ECS_CONST_CAST(float*, p->up)));

    return along <= p->width * 0.5f + BOX_PORTAL_OPENING_SLACK &&
        across <= p->height * 0.5f + BOX_PORTAL_OPENING_SLACK;
}

/** Write the corners of an occluder as an eye sees it and the box around them.
 *
 * @param o The occluder
 * @param eye The eye, which an occluder with auto_orient_y turns to
 * @param normal Receives the normal it has for the eye
 * @param verts Receives the four corners
 * @param min Receives the smallest corner of the box around them
 * @param max Receives the largest corner of the box around them
 */
void box_portal_occluder_corners(
    const box_portal_occluder_t *o,
    const float *eye,
    float *normal,
    vec3 *verts,
    float *min,
    float *max)
{
    static const float corners[4][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
    vec3 right, up = {0, 1, 0};
    int32_t k;

    glm_vec3_copy(ECS_CONST_CAST(float*, o->normal), normal);
    if (o->auto_orient_y) {
        vec3 toward = { eye[0] - o->center[0], 0, eye[2] - o->center[2] };
        if (glm_vec3_norm(toward) > 1e-6f) {
            glm_vec3_normalize_to(toward, normal);
        }
    }

    if (fabsf(normal[1]) > 0.9f) {
        glm_vec3_copy((vec3){1, 0, 0}, right);
        glm_vec3_cross(normal, right, up);
    } else {
        glm_vec3_cross(up, normal, right);
    }

    glm_vec3_normalize(right);
    glm_vec3_normalize(up);
    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);

    for (k = 0; k < 4; k ++) {
        glm_vec3_copy(ECS_CONST_CAST(float*, o->center), verts[k]);
        glm_vec3_muladds(right, corners[k][0] * o->size[0] * 0.5f, verts[k]);
        glm_vec3_muladds(up, corners[k][1] * o->size[1] * 0.5f, verts[k]);
        glm_vec3_minv(min, verts[k], min);
        glm_vec3_maxv(max, verts[k], max);
    }
}

/** Add the occluders of the area of an entry that are in its frustum to those passed.
 *
 * @param index The index
 * @param scene The scene that is traversed
 * @param eye The eye
 * @param entry The entry
 * @param blockers The occluders passed so far, updated
 */
static void box_portal_pass_occluders(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const float *eye,
    const box_portal_entry_t *entry,
    box_portal_blockers_t *blockers)
{
    const box_portal_occluder_t *occluders = ecs_vec_first_t(
        &index->occluders, box_portal_occluder_t);
    int32_t i, k, count = ecs_vec_count(&index->occluders);

    for (i = 0; i < count && blockers->count < BOX_PORTAL_OCCLUDERS_MAX; i ++) {
        const box_portal_occluder_t *o = &occluders[i];
        bool passed = o->scene != scene || o->area != entry->area;
        vec3 normal, verts[4], min, max;

        for (k = 0; k < blockers->count && !passed; k ++) {
            passed = blockers->items[k].occluder == i;
        }

        if (passed) {
            continue;
        }

        box_portal_occluder_corners(o, eye, normal, verts, min, max);
        if (!box_portal_box_inside(min, max, entry->planes, entry->plane_count)) {
            continue;
        }

        box_portal_blocker_t *b = &blockers->items[blockers->count];
        b->occluder = i;
        b->plane_count = box_portal_hull_planes(eye, verts, 4, b->planes, 4);
        if (b->plane_count < 3) {
            continue;
        }

        float *behind = b->planes[b->plane_count ++];
        glm_vec3_copy(normal, behind);
        behind[3] = -glm_vec3_dot(normal, ECS_CONST_CAST(float*, o->center));
        if (glm_vec3_dot(behind, ECS_CONST_CAST(float*, eye)) + behind[3] > 0) {
            glm_vec4_negate(behind);
        }

        blockers->count ++;
    }
}

/** Check whether an occluder the traversal passed hides a portal whole.
 *
 * @param blockers The occluders passed so far
 * @param p The portal
 * @return Whether the box around the portal is behind one of them
 */
static bool box_portal_hidden(
    const box_portal_blockers_t *blockers,
    const box_portal_portal_t *p)
{
    bool result = false;
    int32_t i;

    for (i = 0; i < blockers->count && !result; i ++) {
        const box_portal_blocker_t *b = &blockers->items[i];
        result = box_portal_box_contained(p->min, p->max, b->planes, b->plane_count);
    }

    return result;
}

/** Narrow the frustum of an entry to what an eye sees of it through a portal.
 *
 * @param camera The eye and its frustum
 * @param p The portal
 * @param from_front Whether the portal is crossed from its front area
 * @param parent The entry the portal is looked at from
 * @param child The entry behind the portal, whose planes are written
 * @return Whether the portal reaches into the frustum of the parent
 */
static bool box_portal_narrow(
    const box_portal_camera_t *camera,
    const box_portal_portal_t *p,
    bool from_front,
    const box_portal_entry_t *parent,
    box_portal_entry_t *child)
{
    vec3 clipped[BOX_PORTAL_POLY_MAX];
    int32_t corners = box_portal_clip(p->verts, 4, parent->planes, parent->plane_count, clipped);
    bool result = corners >= 3;

    if (result && !p->pass_through) {
        float sign = from_front ? 1.0f : -1.0f;
        int32_t n = box_portal_hull_planes(camera->eye, clipped, corners, child->planes,
            BOX_PORTAL_HULL_PLANES_MAX);

        child->hull_count = n;
        glm_vec3_scale(ECS_CONST_CAST(float*, p->normal), sign, child->planes[n]);
        child->planes[n ++][3] = p->d * sign;
        glm_vec4_copy(ECS_CONST_CAST(float*, camera->planes[4]), child->planes[n ++]);
        glm_vec4_copy(ECS_CONST_CAST(float*, camera->planes[5]), child->planes[n ++]);
        child->plane_count = n;
        child->whole = false;
    }

    return result;
}

/** Check whether a portal faces the eye of an entry and lies in front of the portal the entry
 * was reached through.
 *
 * @param portals The portals of the index
 * @param p The portal
 * @param e The entry it is looked at from
 * @param from_front Whether the portal is crossed from its front area
 * @param side Signed distance of the eye to the plane of the portal
 * @return Whether the entry can look through the portal
 */
static bool box_portal_faces(
    const box_portal_portal_t *portals,
    const box_portal_portal_t *p,
    const box_portal_entry_t *e,
    bool from_front,
    float side)
{
    bool result = p->double_side || (from_front ? side <= 0 : side >= 0);

    if (result && e->via >= 0) {
        const box_portal_portal_t *via = &portals[e->via];
        vec3 offset;
        glm_vec3_sub(ECS_CONST_CAST(float*, p->center),
            ECS_CONST_CAST(float*, via->center), offset);

        float ahead = glm_vec3_dot(offset, ECS_CONST_CAST(float*, via->normal));
        result = (e->from_front ? ahead : -ahead) >= -BOX_PORTAL_BEHIND_SLACK;
    }

    return result;
}

/** Look from an entry through one portal of its area, and queue the area behind it when the
 * portal is open, faces the eye and reaches into the frustum of the entry.
 *
 * @param index The index
 * @param camera The eye and its frustum
 * @param vis The traversal, whose counts are updated
 * @param at Index of the entry in the entries of the traversal
 * @param portal Index of the portal
 * @param blockers The occluders passed so far
 */
static void box_portal_look(
    const box_portal_index_t *index,
    const box_portal_camera_t *camera,
    box_portal_vis_t *vis,
    int32_t at,
    int32_t portal,
    const box_portal_blockers_t *blockers)
{
    const box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    const box_portal_portal_t *p = &portals[portal];
    const box_portal_entry_t *e = ecs_vec_get_t(&vis->entries, box_portal_entry_t, at);
    box_portal_entry_t child = *e;
    bool from_front = p->front == e->area, seen = true;
    float side = glm_vec3_dot(ECS_CONST_CAST(float*, p->normal),
        ECS_CONST_CAST(float*, camera->eye)) + p->d;

    vis->tested ++;
    child.area = from_front ? p->back : p->front;
    child.parent = at;
    child.via = portal;
    child.depth = e->depth + 1;
    child.from_front = from_front;
    child.sky_only = p->sky_only && child.area == 0;

    if (p->open < BOX_PORTAL_SHUT) {
        vis->closed ++;
        seen = false;
    } else if (fabsf(side) >= BOX_PORTAL_NEAR_PASS || !box_portal_opening_holds(p, camera->eye)) {
        seen = box_portal_faces(portals, p, e, from_front, side);
        if (seen && box_portal_hidden(blockers, p)) {
            vis->occluded ++;
            seen = false;
        }

        seen = seen && box_portal_narrow(camera, p, from_front, e, &child);
    }

    if (seen) {
        vis->passed ++;
        *ecs_vec_append_t(NULL, &vis->pending, box_portal_entry_t) = child;
    }
}

void box_portal_traverse(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const box_portal_camera_t *camera,
    box_portal_vis_t *vis)
{
    const box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    const box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    const int32_t *links = ecs_vec_first_t(&index->links, int32_t);
    box_portal_blockers_t blockers;
    box_portal_entry_t *root;
    int32_t i;

    ecs_vec_clear(&vis->entries);
    ecs_vec_clear(&vis->pending);
    blockers.count = 0;
    vis->scene = scene;
    vis->root = box_portal_index_area_at(index, scene, camera->eye);
    vis->tested = 0;
    vis->passed = 0;
    vis->closed = 0;
    vis->occluded = 0;
    vis->sky = false;
    vis->truncated = false;

    root = ecs_vec_append_t(NULL, &vis->pending, box_portal_entry_t);
    ecs_os_memset_t(root, 0, box_portal_entry_t);
    root->area = vis->root;
    root->parent = -1;
    root->via = -1;
    root->whole = true;
    root->plane_count = 6;
    ecs_os_memcpy_n(root->planes, camera->planes, vec4, 6);

    while (ecs_vec_count(&vis->pending) && !vis->truncated) {
        int32_t at = ecs_vec_count(&vis->entries);
        box_portal_entry_t e = *ecs_vec_last_t(&vis->pending, box_portal_entry_t);
        const box_portal_area_t *area = &areas[e.area];

        if (at >= BOX_PORTAL_ENTRIES_MAX) {
            vis->truncated = true;
            continue;
        }

        ecs_vec_remove_last(&vis->pending);
        *ecs_vec_append_t(NULL, &vis->entries, box_portal_entry_t) = e;
        vis->sky |= e.area == 0;
        if (e.sky_only) {
            continue;
        }

        box_portal_pass_occluders(index, scene, camera->eye, &e, &blockers);
        if (e.depth >= BOX_PORTAL_DEPTH_MAX) {
            continue;
        }

        for (i = 0; i < area->link_count; i ++) {
            int32_t portal = links[area->link_first + i];

            if (portals[portal].scene == scene && !box_portal_in_path(vis, &e, portal)) {
                box_portal_look(index, camera, vis, at, portal, &blockers);
            }
        }
    }

    ecs_vec_clear(&vis->pending);
}
