/**
 * @file fps/load.h
 * @brief The samples of the load panel: the time every thread of box worked per frame.
 */

#ifndef BOX_FPS_LOAD_H
#define BOX_FPS_LOAD_H

#include <box.h>

/** Register LoadSamples and LoadSummary, the clock of the main thread and the systems that
 * sample the threads at the start of every frame and end the frame of the main thread.
 *
 * @param world The world
 */
void box_fps_load_init(
    ecs_world_t *world);

/** Write the summary of the thread samples to LoadSummary when the load panel shows it
 * differently: the frames, the workers, the times and shares as the script formats them, or the
 * color of a share.
 *
 * @param world The world
 */
void box_fps_load_summary_sync(
    ecs_world_t *world);

/** Read the clock the intervals of the panels are measured on.
 *
 * @return Seconds on the clock of ecs_os_get_time
 */
double box_fps_now(void);

/** Get the color a time per frame shows in: of a bar, the rate or a share.
 *
 * @param ms The time in milliseconds
 * @return 0 for green, up to BOX_FPS_FAST_MS, 1 for yellow, up to BOX_FPS_SLOW_MS, 2 for red
 */
int32_t box_fps_level(
    double ms);

#endif
