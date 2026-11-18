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
 */
static void box_render_portals_status(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t view,
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    bool culling)
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
        .culling = culling
    };

    bool same = current && current->area == status.area && current->areas == status.areas &&
        current->entries == status.entries &&
        current->portals_tested == status.portals_tested &&
        current->portals_passed == status.portals_passed &&
        current->portals_closed == status.portals_closed &&
        current->outdoors == status.outdoors && current->culling == status.culling;

    if (!same) {
        ecs_set_ptr(stage, view, PortalStatus, &status);
    }
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
        mat4 proj, view_proj;

        v->portal_first = ecs_vec_count(&p->portals);
        v->portal_count = 0;
        if (!view || !view->culling.enabled || !view->culling.portals || c->ortho ||
            !box_portal_index_has(index, v->scene))
        {
            continue;
        }

        if (aspect <= 0) {
            aspect = box_render_portals_aspect(world);
        }

        box_camera_projection(c, glm_max(aspect, c->aspect_ratio), 1.0f, proj);
        glm_mat4_mul(proj, v->view_matrix, view_proj);
        box_portal_camera_init(&camera, view_proj, v->camera_transform[3]);
        box_portal_traverse(index, v->scene, &camera, &vis);

        if (box_portal_block_pack(index, &vis, &p->portals, BOX_PORTAL_BLOCK_MAX)) {
            v->portal_count = ecs_vec_count(&p->portals) - v->portal_first;
        }

        box_render_portal_marks(index, &vis, v->camera_transform[3], p, v);

        box_render_portals_status(world, stage, v->view, index, &vis, v->portal_count > 0);
    }

    box_portal_vis_fini(&vis);
}
