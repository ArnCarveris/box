/**
 * @file surface/schedule.h
 * @brief Whether a surface with a SurfaceSchedule renders this frame.
 */

#ifndef BOX_SURFACE_SCHEDULE_H
#define BOX_SURFACE_SCHEDULE_H

#include <box.h>
#include "../render/render.h"

/** Decide whether a surface renders this frame by its SurfaceSchedule, and write its
 * SurfaceScheduleStatus when that changed. A surface without a schedule always renders.
 *
 * Runs after the views of the frame were extracted and traversed their portals, so what the
 * views see is the sight of this frame.
 *
 * @param world The stage of the system
 * @param e The surface entity
 * @param p The packet of the frame, with its views
 * @param delta_time Seconds since the last frame
 * @param wait Seconds left before the surface is due again, kept by the caller, 0 at first
 * @return Whether the surface renders this frame
 */
bool box_surface_schedule_due(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_render_packet_t *p,
    float delta_time,
    float *wait);

#endif
