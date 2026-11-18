/**
 * @file render/portal_marks.h
 * @brief Portal marks of a view: the openings it draws into its stencil, so each pixel holds
 * the region of the innermost portal entry it is seen through, and water draws only in the
 * pixels of outdoor entries.
 *
 * Every entry of a traversal gets a region, the entry of the eye region 0. An entry whose eye
 * stands in its opening, or one past the last region, keeps the region of its parent. A region
 * is wet when an entry in it is the outdoors and sees more than the sky; every other region
 * carries BOX_RENDER_STENCIL_DRY.
 *
 * The marks are ordered parents before children and siblings nearest first, so a portal far
 * behind a near doorway never marks the pixels of the doorway.
 */

#ifndef BOX_RENDER_PORTAL_MARKS_H
#define BOX_RENDER_PORTAL_MARKS_H

#include "render.h"
#include "../portal/portal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Write the portal marks of a view and the stencil value its passes clear to.
 *
 * @param index The index the traversal walked
 * @param vis What the camera of the view saw (box_portal_traverse)
 * @param eye World position of the eye
 * @param p The packet, whose portal marks and their vertices are appended to
 * @param v The view, whose mark_first, mark_count and stencil_clear are set
 */
void box_render_portal_marks(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    const float *eye,
    box_render_packet_t *p,
    box_render_view_t *v);

#ifdef __cplusplus
}
#endif

#endif
