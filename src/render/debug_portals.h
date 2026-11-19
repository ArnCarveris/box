/**
 * @file render/debug_portals.h
 * @brief Lines of the portal world of a view (PortalDebug): the portals, the part of them a
 * frustum leaves, the areas and the occluders the camera sees.
 *
 * Every line is a box of the BVH debug draw (debug_bvh.wgsl) that is flat on two axes and
 * placed by a pose, so the lines are drawn by the pipeline of those boxes, in a color of their
 * style instead of the color of a tree.
 */

#ifndef BOX_RENDER_DEBUG_PORTALS_H
#define BOX_RENDER_DEBUG_PORTALS_H

#include "render.h"
#include "../portal/portal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Write the lines of the portal world of a view into the portal debug boxes of the packet.
 *
 * @param index The index the traversal walked
 * @param vis What the camera of the view saw
 * @param eye World position of the eye
 * @param settings Which lines to draw
 * @param p The packet
 * @param v The view, whose portal debug ranges are set
 */
void box_render_debug_portals(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    const float *eye,
    const PortalDebug *settings,
    box_render_packet_t *p,
    box_render_view_t *v);

/** Append the lines of a view to the debug boxes and poses of the packet, which end with those
 * of the view.
 *
 * @param p The packet
 * @param v The view
 * @param count Boxes of the view so far, updated
 * @param poses Poses of the view so far, updated
 */
void box_render_debug_portals_append(
    box_render_packet_t *p,
    const box_render_view_t *v,
    int32_t *count,
    int32_t *poses);

#ifdef __cplusplus
}
#endif

#endif
