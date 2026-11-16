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

/** Write the summary of the thread samples to LoadSummary when it differs from it.
 *
 * @param world The world
 */
void box_fps_load_summary_sync(
    ecs_world_t *world);

#endif
