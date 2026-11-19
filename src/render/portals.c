/**
 * @file render/portals.c
 * @brief Gives every view that culls by portals the visibility block of what its camera sees
 * of the areas of its scene, and writes its PortalStatus.
 *
 * The traversal runs with a frustum at least as wide as the view renders with. The cull pass
 * still tests the frustum of the view itself, so the wider one draws nothing more.
 */

#include "render.h"
#include "portal_marks.h"
#include "debug_portals.h"
#include "../portal/portal.h"

/** Aspect ratio of the traversal frustum when no surface knows its size */
#define BOX_RENDER_PORTALS_ASPECT (4.0f)

/** Factor the widest aspect ratio of the surfaces is widened by, for a surface that resizes */
#define BOX_RENDER_PORTALS_MARGIN (1.25f)

/** Get the aspect ratio the traversal frustums are built with: wider than every surface.
 *
 * @param world The world
 * @return The aspect ratio
 */
static float box_render_portals_aspect(
    const ecs_world_t *world)
{
    float result = 0;
    int32_t i;

    if (ecs_id(SurfaceStatus)) {
        ecs_iter_t it = ecs_each_id(world, ecs_id(SurfaceStatus));
        while (ecs_each_next(&it)) {
            const SurfaceStatus *status = ecs_field(&it, SurfaceStatus, 0);

            for (i = 0; i < it.count; i ++) {
                if (status[i].render_width > 0 && status[i].render_height > 0) {
                    result = glm_max(result, (float)status[i].render_width /
                        (float)status[i].render_height);
                }
            }
        }
    }

    result = result > 0 ? result * BOX_RENDER_PORTALS_MARGIN : BOX_RENDER_PORTALS_ASPECT;

    return result;
}

/** Count the areas a traversal sees, the outdoors included.
 *
 * @param vis The traversal
 * @return Areas with an entry
 */
static int32_t box_render_portals_areas(
    const box_portal_vis_t *vis)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);
    int32_t i, k, count = ecs_vec_count(&vis->entries), result = 0;

    for (i = 0; i < count; i ++) {
        bool counted = false;

        for (k = 0; k < i && !counted; k ++) {
            counted = entries[k].area == entries[i].area;
        }

        result += counted ? 0 : 1;
    }

    return result;
}

/** Write the PortalStatus of a view when it changed.
 *
 * @param world The world
 * @param stage The stage of the system, which the status is set through
 * @param view The view entity
 * @param index The index
 * @param vis What its camera saw
 * @param culling Whether the view culls by it
 * @param frozen Whether the traversal is from the camera as it was when it froze
 */
static void box_render_portals_status(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t view,
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    bool culling,
    bool frozen)
{
    const PortalStatus *current = ecs_get(world, view, PortalStatus);
    PortalStatus status = {
        .area = ecs_vec_get_t(&index->areas, box_portal_area_t, vis->root)->entity,
        .areas = box_render_portals_areas(vis),
        .entries = ecs_vec_count(&vis->entries),
        .portals_tested = vis->tested,
        .portals_passed = vis->passed,
        .portals_closed = vis->closed,
        .outdoors = vis->sky,
        .culling = culling,
        .frozen = frozen,
        .truncated = vis->truncated
    };

    bool same = current && current->area == status.area && current->areas == status.areas &&
        current->entries == status.entries &&
        current->portals_tested == status.portals_tested &&
        current->portals_passed == status.portals_passed &&
        current->portals_closed == status.portals_closed &&
        current->outdoors == status.outdoors && current->culling == status.culling &&
        current->frozen == status.frozen && current->truncated == status.truncated;

    if (!same) {
        ecs_set_ptr(stage, view, PortalStatus, &status);
    }
}

/** Check whether the traversal of a view is wanted: to cull by it, to draw its lines or to
 * keep it.
 *
 * @param view The view
 * @return Whether the view needs what its camera sees of the portal world
 */
static bool box_render_portals_wanted(
    const View *view)
{
    const PortalDebug *debug = &view->debug.portal;

    return (view->culling.enabled && view->culling.portals) || debug->lines || debug->areas ||
        debug->occluders;
}

/** Write the PortalTree of a view when it changed.
 *
 * @param world The world
 * @param stage The stage of the system, which the tree is set through
 * @param view The view entity
 * @param index The index
 * @param vis What its camera saw
 */
static void box_render_portals_tree(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t view,
    const box_portal_index_t *index,
    const box_portal_vis_t *vis)
{
    const box_portal_entry_t *entries = ecs_vec_first_t(&vis->entries, box_portal_entry_t);
    const box_portal_area_t *areas = ecs_vec_first_t(&index->areas, box_portal_area_t);
    const box_portal_portal_t *portals = ecs_vec_first_t(&index->portals, box_portal_portal_t);
    const PortalTree *current = ecs_get(world, view, PortalTree);
    PortalTree tree;
    int32_t i, count = ecs_vec_count(&vis->entries);

    ecs_os_memset_t(&tree, 0, PortalTree);
    tree.count = count < BOX_PORTAL_TREE_MAX ? count : BOX_PORTAL_TREE_MAX;

    for (i = 0; i < tree.count; i ++) {
        tree.entries[i].area = areas[entries[i].area].entity;
        tree.entries[i].via = entries[i].via >= 0 ? portals[entries[i].via].entity : 0;
        tree.entries[i].depth = entries[i].depth;
        tree.entries[i].sky_only = entries[i].sky_only;
    }

    if (!current || ecs_os_memcmp(current, &tree, ECS_SIZEOF(PortalTree))) {
        ecs_set_ptr(stage, view, PortalTree, &tree);
    }
}

/** Build the traversal camera of a view: its frustum at an aspect ratio and its eye.
 *
 * @param v The view, with its camera data, view matrix and camera transform
 * @param aspect The aspect ratio of the frustum, widened to the one of the camera
 * @param camera Receives the traversal camera
 */
static void box_render_portals_camera(
    const box_render_view_t *v,
    float aspect,
    box_portal_camera_t *camera)
{
    const Camera *c = &v->camera_data;
    mat4 proj, view, view_proj;

    box_camera_projection(c, glm_max(aspect, c->aspect_ratio), 1.0f, proj);
    glm_mat4_copy(ECS_CONST_CAST(vec4*, v->view_matrix), view);
    glm_mat4_mul(proj, view, view_proj);
    box_portal_camera_init(camera, view_proj, v->camera_transform[3]);
}

void box_render_portals_capture(
    ecs_world_t *world,
    box_render_packet_t *p,
    box_render_view_t *v,
    float aspect)
{
    const box_portal_index_t *index = box_portal_index(world);
    box_portal_camera_t camera;
    box_portal_vis_t vis;

    v->portal_first = ecs_vec_count(&p->portals);
    v->portal_count = 0;
    if (!index || v->camera_data.ortho || !box_portal_index_has(index, v->scene)) {
        goto done;
    }

    if (!(aspect > 0)) {
        aspect = BOX_RENDER_PORTALS_ASPECT;
    }

    box_portal_vis_init(&vis);
    box_render_portals_camera(v, aspect, &camera);
    box_portal_traverse(index, v->scene, &camera, &vis);

    if (box_portal_block_pack(index, &vis, &p->portals, BOX_PORTAL_BLOCK_MAX)) {
        v->portal_count = ecs_vec_count(&p->portals) - v->portal_first;
    }

    box_render_portal_marks(index, &vis, camera.eye, p, v);
    box_portal_vis_fini(&vis);
done:
    return;
}

void box_render_portals(
    ecs_world_t *world,
    ecs_world_t *stage,
    box_render_packet_t *p)
{
    const box_portal_index_t *index = box_portal_index(world);
    int32_t i, count = index ? ecs_vec_count(&p->views) : 0;
    box_portal_vis_t vis;
    float aspect = 0;

    box_portal_vis_init(&vis);

    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const View *view = v->view && ecs_is_alive(world, v->view)
            ? ecs_get(world, v->view, View)
            : NULL;
        const Camera *c = &v->camera_data;
        box_portal_camera_t camera;
        bool frozen;

        v->portal_first = ecs_vec_count(&p->portals);
        v->portal_count = 0;
        v->portal_debug_count = 0;
        v->portal_pose_count = 0;
        if (!view || c->ortho || !box_portal_index_has(index, v->scene) ||
            !box_render_portals_wanted(view))
        {
            if (v->view && ecs_is_alive(world, v->view)) {
                box_portal_freeze(index, v->view, false, NULL);

                if (ecs_has(world, v->view, PortalStatus)) {
                    ecs_remove(stage, v->view, PortalStatus);
                    ecs_remove(stage, v->view, PortalTree);
                }
            }

            continue;
        }

        if (aspect <= 0) {
            aspect = box_render_portals_aspect(world);
        }

        box_render_portals_camera(v, aspect, &camera);
        frozen = box_portal_freeze(index, v->view, view->culling.freeze_portals, &camera);
        box_portal_traverse(index, v->scene, &camera, &vis);

        if (view->culling.enabled && view->culling.portals) {
            if (box_portal_block_pack(index, &vis, &p->portals, BOX_PORTAL_BLOCK_MAX)) {
                v->portal_count = ecs_vec_count(&p->portals) - v->portal_first;
            }

            box_render_portal_marks(index, &vis, camera.eye, p, v);
        }

        box_render_debug_portals(index, &vis, camera.eye, &view->debug.portal, p, v);
        box_render_portals_status(world, stage, v->view, index, &vis, v->portal_count > 0,
            frozen);
        box_render_portals_tree(world, stage, v->view, index, &vis);
    }

    box_portal_vis_fini(&vis);
}
